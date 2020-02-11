#include "shape.h"

#include "logging.h"
#include "window.h"

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
            COMPONENT_PHYSICAL, COMPONENT_TRACKS_WINDOW, COMPONENT_SHAPE_DAMAGED, CQ_END) {
        struct TracksWindowComponent* window = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
        struct ShapeDamagedEvent* shapeDamaged = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);

        // @HACK @MUD: The first half of this would be better placed in the X11
        // input processor.

        XWindowAttributes attribs;
        if (!XGetWindowAttributes(xcontext->display, window->id, &attribs)) {
            printf_errf("Failed getting window attributes while mapping");
            swiss_removeComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);
            continue;
        }

        Vector2 extents = {{attribs.width + attribs.border_width * 2, attribs.height + attribs.border_width * 2}};
        // X has some insane notion that borders aren't part of the window.
        // Therefore a window with a border will have a bounding shape with
        // a negative upper left corner. This offset corrects for that, so
        // we don't have to deal with it downstream
        Vector2 offset = {{-attribs.border_width, -attribs.border_width}};

        XserverRegion window_region = XFixesCreateRegionFromWindow(xcontext->display, window->id, ShapeBounding);

        XRectangle default_clip = {.x = offset.x, .y = offset.y, .width = extents.x, .height = extents.y};
        XserverRegion default_clip_region = XFixesCreateRegion(xcontext->display, &default_clip, 1);
        XFixesIntersectRegion(xcontext->display, window_region, window_region, default_clip_region);

        int rect_count;
        XRectangle* rects = XFixesFetchRegion(xcontext->display, window_region, &rect_count);

        XFixesDestroyRegion(xcontext->display, window_region);

        vector_init(&shapeDamaged->rects, sizeof(struct Rect), rect_count);

        convert_xrects_to_relative_rect(rects, rect_count, &extents, &offset, &shapeDamaged->rects);
        XFree(rects);
    }
}
