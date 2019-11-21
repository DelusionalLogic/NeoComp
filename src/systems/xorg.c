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

void xorgsystem_tick(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms) {
    // Mapping a window causes us to start redirecting it
    {
        zone_enter(&ZONE_fetch_xprop);
        for_components(it, em,
                COMPONENT_MAP, COMPONENT_HAS_CLIENT, COMPONENT_TRACKS_WINDOW, CQ_END) {
            struct HasClientComponent* hasClient = swiss_godComponent(em, COMPONENT_HAS_CLIENT, it.id);

            winprop_t prop = wid_get_prop_adv(xcontext, hasClient->id, atoms->atom_bypass, 0L, 1L, XA_CARDINAL, 32);

            // A value of 1 means that the window has taken special care to ask
            // us not to do compositing.
            if(prop.nitems == 0 || *prop.data.p32 != 1) {
                swiss_ensureComponent(em, COMPONENT_REDIRECTED, it.id);
            }

            free_winprop(&prop);
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
            COMPONENT_MAP, COMPONENT_TRACKS_WINDOW, COMPONENT_REDIRECTED, CQ_END) {
        struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
        struct BindsTextureComponent* bindsTexture = swiss_addComponent(em, COMPONENT_BINDS_TEXTURE, it.id);

        if(!wd_init(&bindsTexture->drawable, xcontext, tracksWindow->id)) {
            printf_errf("Failed initializing window drawable on map");
        }
    }
}
