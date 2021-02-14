#include "xorg.h"

#include "profiler/zone.h"

#include "logging.h"
#include "window.h"

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

void xorgsystem_tick(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms) {
    doMap(em, xcontext, atoms);
    doUnmap(em, xcontext, atoms);
}
