#include "xorg.h"

#include "profiler/zone.h"

#include "logging.h"
#include "window.h"

DECLARE_ZONE(make_cutout);

static void unredirect(Swiss* em, struct X11Context* xcontext, enum ComponentType* query) {
    for_componentsArr(it, em, query) {
        struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);

        XCompositeUnredirectWindow(xcontext->display, tracksWindow->id, CompositeRedirectManual);
    }
    swiss_removeComponentWhere(
        em,
        COMPONENT_REDIRECTED,
        query
    );
}

static void doUnmap(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms) {
    //@PERFORMANCE: Technically all windows that redirect also tracks a window,
    //so we don't really need to query for both
    unredirect(em, xcontext, (enum ComponentType[]) {
        COMPONENT_UNMAP, COMPONENT_TRACKS_WINDOW, COMPONENT_REDIRECTED, CQ_END
    });
    unredirect(em, xcontext, (enum ComponentType[]) {
        COMPONENT_BYPASS, COMPONENT_TRACKS_WINDOW, COMPONENT_REDIRECTED, CQ_END
    });
}

static void doMap(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms) {
    // Mapping a window causes us to start redirecting it
    {
        for_components(it, em,
                COMPONENT_MAP, COMPONENT_TRACKS_WINDOW, CQ_NOT, COMPONENT_BYPASS, CQ_END) {
            swiss_ensureComponent(em, COMPONENT_REDIRECTED, it.id);
        }

        for_components(it, em,
                COMPONENT_MAP, COMPONENT_TRACKS_WINDOW, COMPONENT_REDIRECTED, CQ_END) {
            struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);

            XCompositeRedirectWindow(xcontext->display, tracksWindow->id, CompositeRedirectManual);
        }
    }
}

void xorgsystem_tick(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms, Vector2* canvas_size) {
    doMap(em, xcontext, atoms);
    doUnmap(em, xcontext, atoms);

    {
        zone_scope(&ZONE_make_cutout);
        XserverRegion newShape = XFixesCreateRegionH(xcontext->display, NULL, 0);
        for_components(it, em,
                COMPONENT_MUD, COMPONENT_TRACKS_WINDOW, COMPONENT_PHYSICAL, CQ_NOT, COMPONENT_REDIRECTED, CQ_END) {
            struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
            struct _win* win = swiss_getComponent(em, COMPONENT_MUD, it.id);
            struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

            if(win_mapped(em, it.id)) {
                XserverRegion windowRegion = XFixesCreateRegionFromWindow(xcontext->display, tracksWindow->id, ShapeBounding);
                // @HACK: I'm not quite sure why I need to add 2 times the
                // border here. One makes sense since i'm subtracting that
                // from the positioin in the X11 layer.
                XFixesTranslateRegionH(xcontext->display, windowRegion, physical->position.x + win->border_size*2, physical->position.y + win->border_size*2);
                XFixesUnionRegionH(xcontext->display, newShape, newShape, windowRegion);
                XFixesDestroyRegionH(xcontext->display, windowRegion);
            }
        }
        XFixesInvertRegionH(xcontext->display, newShape, &(XRectangle){0, 0, canvas_size->x, canvas_size->y}, newShape);
        XFixesSetWindowShapeRegionH(xcontext->display, xcontext->overlay, ShapeBounding, 0, 0, newShape);
        XFixesDestroyRegionH(xcontext->display, newShape);
    }
}
