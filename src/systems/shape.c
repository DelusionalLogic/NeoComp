#include "shape.h"

#include "intercept/xorg.h"

#include "profiler/zone.h"
#include "logging.h"
#include "window.h"
#include "assert.h"

DECLARE_ZONE(commit_reshape);
DECLARE_ZONE(fetch_shape);

static void convert_xrects_to_relative_rect(XRectangle* rects, size_t rect_count, Vector2* extents, Vector2* offset, Vector* mrects) {
    // Convert the XRectangles into application specific (and non-scaled) rectangles
    for(int i = 0; i < rect_count; i++) {
        struct Rect* mrect = vector_reserve(mrects, 1);
        mrect->pos.x = rects[i].x - offset->x;
        mrect->pos.y = extents->y - (rects[i].y - offset->y);

        mrect->size.x = rects[i].width;
        mrect->size.y = rects[i].height;

        vec2_div(&mrect->pos, extents);
        vec2_div(&mrect->size, extents);

        assert(mrect->size.x <= 1.0);
        assert(mrect->size.y <= 1.0);
    }

}

void shapesystem_updateShapes(Swiss* em, struct X11Context* xcontext) {
    for_components(it, em,
            COMPONENT_NEW, CQ_END) {
        struct ShapedComponent* shaped = swiss_addComponent(em, COMPONENT_SHAPED, it.id);
        shaped->face = NULL;
        swiss_ensureComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);
    }

    {
        zone_scope(&ZONE_fetch_shape);
        for_components(it, em,
                COMPONENT_PHYSICAL, COMPONENT_TRACKS_WINDOW, COMPONENT_SHAPE_DAMAGED, CQ_END) {
            struct TracksWindowComponent* window = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
            struct ShapeDamagedEvent* shapeDamaged = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);

            // @HACK @MUD: The first half of this would be better placed in the X11
            // input processor.

            XWindowAttributes attribs;
            if (!XGetWindowAttributesH(xcontext->display, window->id, &attribs)) {
                printf_errf("Failed getting window attributes for shape damage, keeping old shape");
                swiss_removeComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);
                continue;
            }

            Vector2 extents = {{attribs.width + attribs.border_width * 2, attribs.height + attribs.border_width * 2}};
            // X has some insane notion that borders aren't part of the window.
            // Therefore a window with a border will have a bounding shape with
            // a negative upper left corner. This offset corrects for that, so
            // we don't have to deal with it downstream
            Vector2 offset = {{-attribs.border_width, -attribs.border_width}};

            XserverRegion window_region = XFixesCreateRegionFromWindowH(xcontext->display, window->id, ShapeBounding);

            XRectangle default_clip = {.x = offset.x, .y = offset.y, .width = extents.x, .height = extents.y};
            XserverRegion default_clip_region = XFixesCreateRegionH(xcontext->display, &default_clip, 1);
            XFixesIntersectRegionH(xcontext->display, window_region, window_region, default_clip_region);
            XFixesDestroyRegion(xcontext->display, default_clip_region);

            int rect_count;
            XRectangle* rects = XFixesFetchRegionH(xcontext->display, window_region, &rect_count);

            XFixesDestroyRegionH(xcontext->display, window_region);

            vector_init(&shapeDamaged->rects, sizeof(struct Rect), rect_count);

            convert_xrects_to_relative_rect(rects, rect_count, &extents, &offset, &shapeDamaged->rects);
            XFree(rects);
        }
    }

    {
        zone_scope(&ZONE_commit_reshape);
        for_components(it, em,
                COMPONENT_SHAPED, COMPONENT_SHAPE_DAMAGED, CQ_END) {
            struct ShapedComponent* shaped = swiss_getComponent(em, COMPONENT_SHAPED, it.id);
            struct ShapeDamagedEvent* shapeDamaged = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);

            if(shaped->face != NULL) {
                face_unload_file(shaped->face);
                free(shaped->face);
            }

            struct face* face = malloc(sizeof(struct face));
            // Triangulate the rectangles into a triangle vertex stream
            face_init_rects(face, &shapeDamaged->rects);
            vector_kill(&shapeDamaged->rects);
            face_upload(face);

            shaped->face = face;
        }
    }
}

void shapesystem_finish(Swiss* em) {
    // Destroy shaped components of destroyed windows
    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_SHAPED, CQ_END) {
        struct ShapedComponent* shaped = swiss_getComponent(em, COMPONENT_SHAPED, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED) {
            if(shaped->face != NULL) {
                face_unload_file(shaped->face);
                free(shaped->face);
            }
            swiss_removeComponent(em, COMPONENT_SHAPED, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_SHAPE_DAMAGED, CQ_END) {
#ifndef NDEBUG
        struct ShapeDamagedEvent* d = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);
#endif
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED) {
            assert(d->rects.elementSize == 0);
            swiss_removeComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);
        }
    }
}

void shapesystem_delete(Swiss* em) {
    for_components(it, em,
            COMPONENT_SHAPED, CQ_END) {
        struct ShapedComponent* shaped = swiss_getComponent(em, COMPONENT_SHAPED, it.id);
        face_unload_file(shaped->face);
        free(shaped->face);
    }
    swiss_resetComponent(em, COMPONENT_SHAPED);
    for_components(it, em,
            COMPONENT_SHAPE_DAMAGED, CQ_END) {
        struct ShapeDamagedEvent* damaged = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);
        vector_kill(&damaged->rects);
    }
    swiss_resetComponent(em, COMPONENT_SHAPE_DAMAGED);
}
