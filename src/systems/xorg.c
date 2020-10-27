#include "xorg.h"

#include "profiler/zone.h"

#include "logging.h"
#include "window.h"

DECLARE_ZONE(fetch_xprop);

static void free_winprop(winprop_t *pprop) {
    // Empty the whole structure to avoid possible issues
    if (pprop->data.p8) {
        XFree(pprop->data.p8);
        pprop->data.p8 = NULL;
    }
    pprop->nitems = 0;
}

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

static void delete_textures(Swiss* em, enum ComponentType* query) {
    for_componentsArr(it, em, query) {
        struct BindsTextureComponent* b = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it.id);

        wd_delete(&b->drawable);
    }
    swiss_removeComponentWhere(
        em,
        COMPONENT_BINDS_TEXTURE,
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

    delete_textures(em, (enum ComponentType[]) {
        COMPONENT_BINDS_TEXTURE, COMPONENT_UNMAP, CQ_END
    });
    delete_textures(em, (enum ComponentType[]) {
        COMPONENT_BINDS_TEXTURE, COMPONENT_BYPASS, CQ_END
    });
}

static void doMap(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms) {
    // Mapping a window causes us to start redirecting it
    {
        zone_enter(&ZONE_fetch_xprop);
        for_components(it, em,
                COMPONENT_MAP, COMPONENT_TRACKS_WINDOW, CQ_END) {
            swiss_ensureComponent(em, COMPONENT_REDIRECTED, it.id);
        }
        zone_leave(&ZONE_fetch_xprop);

        for_components(it, em,
                COMPONENT_MAP, COMPONENT_TRACKS_WINDOW, COMPONENT_REDIRECTED, CQ_END) {
            struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);

            XCompositeRedirectWindow(xcontext->display, tracksWindow->id, CompositeRedirectManual);
        }
    }

    // Mapping a window causes it to bind from X
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_TRACKS_WINDOW, COMPONENT_REDIRECTED, CQ_NOT, COMPONENT_BINDS_TEXTURE, CQ_END) {
        struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
        struct BindsTextureComponent* bindsTexture = swiss_addComponent(em, COMPONENT_BINDS_TEXTURE, it.id);

        if(!wd_init(&bindsTexture->drawable, xcontext, tracksWindow->id)) {
            printf_errf("Failed initializing window drawable on map");
        }
    }
}

void xorgsystem_tick(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms) {
    doMap(em, xcontext, atoms);
    doUnmap(em, xcontext, atoms);
}
