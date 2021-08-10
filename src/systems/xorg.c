#include "xorg.h"

#include "profiler/zone.h"

#include "logging.h"
#include "window.h"

DECLARE_ZONE(make_cutout);

static winprop_t wid_get_prop(struct X11Context* xcontext, Window w, Atom atom, long offset, long length, Atom rtype, int rformat) {
    Atom type = None;
    int format = 0;
    unsigned long nitems = 0, after = 0;
    unsigned char *data = NULL;

    bool status = XGetWindowProperty(
        xcontext->display,
        w,
        atom,
        offset,
        length,
        False,
        rtype,
        &type,
        &format,
        &nitems,
        &after,
        &data
    );

    bool fail = false;

    if(!fail && status == Success)
        fail = true;

    if(!fail && nitems == 0)
        fail = true;

    if(!fail && AnyPropertyType != type && type != rtype)
        fail = true;

    if(!fail && format != 8 && format != 16 && format != 32)
        fail = true;

    if(fail) {
        XFree(data);
        return (winprop_t){
            .data.p8 = NULL,
            .nitems = 0,
            .type = AnyPropertyType,
            .format = 0
        };
    }

    return (winprop_t) {
        .data.p8 = data,
        .nitems = nitems,
        .type = type,
        .format = format,
    };
}

static inline bool wid_has_prop(const session_t *ps, Window w, Atom atom) {
    Atom type = None;
    int format;
    unsigned long nitems, after;
    unsigned char *data;

    bool status = XGetWindowProperty(
        ps->dpy,
        w,
        atom,
        0,
        0,
        False,
        AnyPropertyType,
        &type,
        &format,
        &nitems,
        &after,
        &data
    );

    if(status != Success)
        return false;

    XFree(data);

    if(type == 0)
        return false;

    return true;
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

void xorgsystem_fill_wintype(Swiss* em, session_t* ps) {
    // Fetch the new window type
    for_components(it, em,
            COMPONENT_WINTYPE_CHANGE, COMPONENT_TRACKS_WINDOW, CQ_END) {
        struct WintypeChangedComponent* wintypeChanged = swiss_getComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
        struct TracksWindowComponent* t = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
        Window cid = xorg_get_client(&ps->xcontext, t->id);

        // Detect window type here
        winprop_t prop = wid_get_prop(&ps->xcontext, cid, ps->atoms.atom_win_type, 0L, 32L, XA_ATOM, 32);

        wintypeChanged->newType = WINTYPE_UNKNOWN;

        for (unsigned i = 0; i < prop.nitems; ++i) {
            for (wintype_t j = 1; j < NUM_WINTYPES; ++j) {
                if (ps->atoms.atoms_wintypes[j] == (Atom) prop.data.p32[i]) {
                    wintypeChanged->newType = j;
                }
            }
        }

        if(prop.data.p8) {
            XFree(prop.data.p8);
            prop.data.p8 = NULL;
        }
        prop.nitems = 0;
    }

    // Guess the window type if not provided
    for_components(it, em,
            COMPONENT_WINTYPE_CHANGE, COMPONENT_TRACKS_WINDOW, CQ_END) {
        struct WintypeChangedComponent* wintypeChanged = swiss_getComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
        struct TracksWindowComponent* t = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
        Window cid = xorg_get_client(&ps->xcontext, t->id);

        // Conform to EWMH standard, if _NET_WM_WINDOW_TYPE is not present, take
        // override-redirect windows or windows without WM_TRANSIENT_FOR as
        // _NET_WM_WINDOW_TYPE_NORMAL, otherwise as _NET_WM_WINDOW_TYPE_DIALOG.
        if (wintypeChanged->newType == WINTYPE_UNKNOWN) {
            if (w->override_redirect || !wid_has_prop(ps, cid, ps->atoms.atom_transient))
                w->window_type = WINTYPE_NORMAL;
            else
                w->window_type = WINTYPE_DIALOG;
        }
    }

    // Remove the change if it's the same as the current
    for_components(it, em,
            COMPONENT_WINTYPE_CHANGE, CQ_END) {
        struct WintypeChangedComponent* wintypeChanged = swiss_getComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

        if(w->window_type == wintypeChanged->newType) {
            swiss_removeComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
            continue;
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
