/*
 * Compton - a compositor for X11
 *
 * Based on `xcompmgr` - Copyright (c) 2003, Keith Packard
 *
 * Copyright (c) 2011-2013, Christopher Jeffrey
 * See LICENSE for more information.
 *
 */

#include "compton.h"

#include <ctype.h>

#include "common.h"

#include "atoms.h"
#include "logging.h"

#include "opengl.h"
#include "swiss.h"
#include "vmath.h"
#include "window.h"
#include "windowlist.h"
#include "blur.h"
#include "shadow.h"
#include "xtexture.h"
#include "timer.h"
#include "timeout.h"
#include "paths.h"

#include "assets/assets.h"
#include "assets/shader.h"

#include "renderutil.h"

#include "shaders/shaderinfo.h"

#include "logging.h"

#include "profiler/zone.h"
#include "profiler/render.h"
#include "profiler/dump_events.h"


// === Global constants ===

DECLARE_ZONE(global);
DECLARE_ZONE(input);
DECLARE_ZONE(preprocess);

DECLARE_ZONE(update);
DECLARE_ZONE(update_z);
DECLARE_ZONE(update_wintype);
DECLARE_ZONE(update_shadow_blacklist);
DECLARE_ZONE(update_fade_blacklist);
DECLARE_ZONE(update_invert_list);
DECLARE_ZONE(update_blur_blacklist);
DECLARE_ZONE(update_paint_blacklist);
DECLARE_ZONE(input_react);
DECLARE_ZONE(make_cutout);
DECLARE_ZONE(remove_input);
DECLARE_ZONE(prop_blur_damage);

DECLARE_ZONE(paint);
DECLARE_ZONE(effect_textures);
DECLARE_ZONE(blur_background);
DECLARE_ZONE(update_shadow);
DECLARE_ZONE(fetch_prop);

DECLARE_ZONE(update_fade);

DECLARE_ZONE(update_textures);

// From the header {{{

static inline win * find_win(session_t *ps, Window id) {
  if (!id)
    return NULL;

  for_components(it, &ps->win_list,
      COMPONENT_MUD, COMPONENT_TRACKS_WINDOW, CQ_END) {
      struct TracksWindowComponent* w = swiss_getComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, it.id);

      if (w->id == id)
          return swiss_getComponent(&ps->win_list, COMPONENT_MUD, it.id);
  }

  return NULL;
}

static void discard_ignore(session_t *ps, unsigned long sequence);

static void set_ignore(session_t *ps, unsigned long sequence);

static void set_ignore_next(session_t *ps) {
    set_ignore(ps, NextRequest(ps->dpy));
}

static int should_ignore(session_t *ps, unsigned long sequence);

static void wintype_arr_enable(bool arr[]) {
    wintype_t i;

    for (i = 0; i < NUM_WINTYPES; ++i) {
        arr[i] = true;
    }
}

static void free_wincondlst(c2_lptr_t **pcondlst) {
#ifdef CONFIG_C2
    while ((*pcondlst = c2_free_lptr(*pcondlst)))
        continue;
#endif
}

static void free_xinerama_info(session_t *ps) {
    if (ps->xinerama_scr_regs) {
        for (int i = 0; i < ps->xinerama_nscrs; ++i)
            free_region(ps, &ps->xinerama_scr_regs[i]);
        free(ps->xinerama_scr_regs);
    }
    cxfree(ps->xinerama_scrs);
    ps->xinerama_scrs = NULL;
    ps->xinerama_nscrs = 0;
}

static time_ms_t get_time_ms(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return tv.tv_sec % SEC_WRAP * 1000 + tv.tv_usec / 1000;
}

static struct timeval ms_to_tv(int timeout) {
    return (struct timeval) {
        .tv_sec = timeout / MS_PER_SEC,
        .tv_usec = timeout % MS_PER_SEC * (US_PER_SEC / MS_PER_SEC)
    };
}

static bool isdamagenotify(session_t *ps, const XEvent *ev) {
    return xorgContext_convertEvent(&ps->capabilities, PROTO_DAMAGE,  ev->type) == XDamageNotify;
}

static XTextProperty * make_text_prop(session_t *ps, char *str) {
    XTextProperty *pprop = ccalloc(1, XTextProperty);

    if (XmbTextListToTextProperty(ps->dpy, &str, 1,  XStringStyle, pprop)) {
        cxfree(pprop->value);
        free(pprop);
        pprop = NULL;
    }

    return pprop;
}

static bool wid_set_text_prop(session_t *ps, Window wid, Atom prop_atom, char *str) {
    XTextProperty *pprop = make_text_prop(ps, str);
    if (!pprop) {
        printf_errf("(\"%s\"): Failed to make text property.", str);
        return false;
    }

    XSetTextProperty(ps->dpy, wid, pprop, prop_atom);
    cxfree(pprop->value);
    cxfree(pprop);

    return true;
}

// Stop listening for events
static void win_ev_stop(session_t *ps, win *w) {
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    struct TracksWindowComponent* window = swiss_getComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, wid);
    struct HasClientComponent* client = swiss_getComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid);
    // Will get BadWindow if the window is destroyed
    set_ignore_next(ps);
    XSelectInput(ps->dpy, window->id, 0);

    if (client->id) {
        set_ignore_next(ps);
        XSelectInput(ps->dpy, client->id, 0);
    }
}

static bool wid_get_children(session_t *ps, Window w,
        Window **children, unsigned *nchildren) {
    Window troot, tparent;

    if (!XQueryTree(ps->dpy, w, &troot, &tparent, children, nchildren)) {
        *nchildren = 0;
        return false;
    }

    return true;
}

static bool validate_pixmap(session_t *ps, Pixmap pxmap) {
    if (!pxmap) return false;

    Window rroot = None;
    int rx = 0, ry = 0;
    unsigned rwid = 0, rhei = 0, rborder = 0, rdepth = 0;
    return XGetGeometry(ps->dpy, pxmap, &rroot, &rx, &ry,
            &rwid, &rhei, &rborder, &rdepth) && rwid && rhei;
}

static bool win_match(session_t *ps, win *w, c2_lptr_t *condlst) {
#ifdef CONFIG_C2
    return c2_match(ps, w, condlst, NULL);
#else
    return false;
#endif
}

static bool condlst_add(session_t *ps, c2_lptr_t **pcondlst, const char *pattern);

static long determine_evmask(session_t *ps, Window wid, win_evmode_t mode);

static win * find_toplevel2(session_t *ps, Window wid);

static win * find_win_all(session_t *ps, const Window wid) {
    if (!wid || PointerRoot == wid || wid == ps->root || wid == ps->overlay)
        return NULL;

    win *w = find_win(ps, wid);
    if (!w) w = find_toplevel(ps, wid);
    if (!w) w = find_toplevel2(ps, wid);
    return w;
}

static inline void win_set_focused(session_t *ps, win *w);

static void win_set_invert_color(session_t *ps, win *w, bool invert_color_new);

static void win_set_blur_background(session_t *ps, win *w, bool blur_background_new);

static void win_mark_client(session_t *ps, win *w, Window client);

static void win_recheck_client(session_t *ps, win *w);

static void configure_win(session_t *ps, XConfigureEvent *ce);

static bool wid_get_name(session_t *ps, Window w, char **name);

static bool wid_get_role(session_t *ps, Window w, char **role);

static int win_get_prop_str(session_t *ps, win *w, char **tgt,
        bool (*func_wid_get_prop_str)(session_t *ps, Window wid, char **tgt));

static int win_get_name(session_t *ps, win *w) {
    int ret = win_get_prop_str(ps, w, &w->name, wid_get_name);

    return ret;
}

static int win_get_role(session_t *ps, win *w) {
    int ret = win_get_prop_str(ps, w, &w->role, wid_get_role);

    return ret;
}

static bool win_get_class(session_t *ps, win *w);

#ifdef DEBUG_EVENTS
static int ev_serial(XEvent *ev);

static const char * ev_name(session_t *ps, XEvent *ev);

static Window ev_window(session_t *ps, XEvent *ev);
#endif

static void update_ewmh_active_win(session_t *ps);

#if defined(DEBUG_EVENTS) || defined(DEBUG_RESTACK)
static bool ev_window_name(session_t *ps, Window wid, char **name);
#endif

#ifdef CONFIG_LIBCONFIG
static void lcfg_lookup_bool(const config_t *config, const char *path, bool *value) {
    int ival;

    if (config_lookup_bool(config, path, &ival))
        *value = ival;
}

static int lcfg_lookup_int(const config_t *config, const char *path, int *value) {
#ifndef CONFIG_LIBCONFIG_LEGACY
    return config_lookup_int(config, path, value);
#else
    long lval;
    int ret;

    if ((ret = config_lookup_int(config, path, &lval)))
        *value = lval;

    return ret;
#endif
}
#endif

static bool ensure_glx_context(session_t *ps) {
    // Create GLX context
    if (!glx_has_context(ps))
        glx_init(ps, false);

    return ps->psglx->context;
}

static bool vsync_opengl_swc_init(session_t *ps);

static void vsync_opengl_swc_deinit(session_t *ps);

static void redir_start(session_t *ps);
static void redir_stop(session_t *ps);

static time_ms_t timeout_get_newrun(const timeout_t *ptmout) {
    long a = (ptmout->lastrun + (time_ms_t) (ptmout->interval * TIMEOUT_RUN_TOLERANCE) - ptmout->firstrun) / ptmout->interval;
    long b = (ptmout->lastrun + (time_ms_t) (ptmout->interval * (1 - TIMEOUT_RUN_TOLERANCE)) - ptmout->firstrun) / ptmout->interval;
  return ptmout->firstrun + (max_l(a, b) + 1) * ptmout->interval;
}

/**
 * Get the Xinerama screen a window is on.
 *
 * Return an index >= 0, or -1 if not found.
 */
static void cxinerama_win_upd_scr(session_t *ps, win_id wid) {
    struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, wid);
    struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, wid);
    w->xinerama_scr = -1;
    for (XineramaScreenInfo *s = ps->xinerama_scrs; s < ps->xinerama_scrs + ps->xinerama_nscrs; ++s)
        if (s->x_org <= physical->position.x && s->y_org <= physical->position.y
                && s->x_org + s->width >= physical->position.x + physical->size.x
                && s->y_org + s->height >= physical->position.y + physical->size.y) {
            w->xinerama_scr = s - ps->xinerama_scrs;
            return;
        }
}

static void cxinerama_upd_scrs(session_t *ps);
// }}}

/// Name strings for window types.
const char * const WINTYPES[NUM_WINTYPES] = {
  "unknown",
  "desktop",
  "dock",
  "toolbar",
  "menu",
  "utility",
  "splash",
  "dialog",
  "normal",
  "dropdown_menu",
  "popup_menu",
  "tooltip",
  "notify",
  "combo",
  "dnd",
};

/// Names of VSync modes.
const char * const VSYNC_STRS[NUM_VSYNC + 1] = {
  "none",             // VSYNC_NONE
  "opengl-swc",       // VSYNC_OPENGL_SWC
  "opengl",           // VSYNC_OPENGL
  NULL
};

const char* const StateNames[] = {
    "Hiding",
    "Invisible",
    "Waiting",
    "Activating",
    "Active",
    "Deactivating",
    "Inactive",
    "Destroying",
    "Destroyed",
};

/// Function pointers to init VSync modes.
static bool (* const (VSYNC_FUNCS_INIT[NUM_VSYNC]))(session_t *ps) = {
  [VSYNC_OPENGL_SWC   ] = vsync_opengl_swc_init,
};

/// Function pointers to deinitialize VSync.
static void (* const (VSYNC_FUNCS_DEINIT[NUM_VSYNC]))(session_t *ps) = {
  [VSYNC_OPENGL_SWC   ] = vsync_opengl_swc_deinit,
};

/// Names of root window properties that could point to a pixmap of
/// background.
const static char *background_props_str[] = {
  "_XROOTPMAP_ID",
  "_XSETROOT_ID",
  0,
};

// === Global variables ===

/// Pointer to current session, as a global variable. Only used by
/// <code>error()</code> and <code>reset_enable()</code>, which could not
/// have a pointer to current session passed in.
session_t *ps_g = NULL;

// === Error handling ===

static void
discard_ignore(session_t *ps, unsigned long sequence) {
  while (ps->ignore_head) {
    if ((long) (sequence - ps->ignore_head->sequence) > 0) {
      ignore_t *next = ps->ignore_head->next;
      free(ps->ignore_head);
      ps->ignore_head = next;
      if (!ps->ignore_head) {
        ps->ignore_tail = &ps->ignore_head;
      }
    } else {
      break;
    }
  }
}

static void
set_ignore(session_t *ps, unsigned long sequence) {
  if (ps->o.show_all_xerrors)
    return;

  ignore_t *i = malloc(sizeof(ignore_t));
  if (!i) return;

  i->sequence = sequence;
  i->next = 0;
  *ps->ignore_tail = i;
  ps->ignore_tail = &i->next;
}

static int
should_ignore(session_t *ps, unsigned long sequence) {
  discard_ignore(ps, sequence);
  return ps->ignore_head && ps->ignore_head->sequence == sequence;
}

// === Windows ===

/**
 * Get a specific attribute of a window.
 *
 * Returns a blank structure if the returned type and format does not
 * match the requested type and format.
 *
 * @param ps current session
 * @param w window
 * @param atom atom of attribute to fetch
 * @param length length to read
 * @param rtype atom of the requested type
 * @param rformat requested format
 * @return a <code>winprop_t</code> structure containing the attribute
 *    and number of items. A blank one on failure.
 */
winprop_t
wid_get_prop_adv(struct X11Context* xcontext, Window w, Atom atom, long offset, long length, Atom rtype, int rformat) {
  Atom type = None;
  int format = 0;
  unsigned long nitems = 0, after = 0;
  unsigned char *data = NULL;

  if (Success == XGetWindowProperty(xcontext->display, w, atom, offset, length,
        False, rtype, &type, &format, &nitems, &after, &data)
      && nitems && (AnyPropertyType == type || type == rtype)
      && (!rformat || format == rformat)
      && (8 == format || 16 == format || 32 == format)) {
      return (winprop_t) {
        .data.p8 = data,
        .nitems = nitems,
        .type = type,
        .format = format,
      };
  }

  cxfree(data);

  return (winprop_t) {
    .data.p8 = NULL,
    .nitems = 0,
    .type = AnyPropertyType,
    .format = 0
  };
}

void convert_xrects_to_relative_rect(XRectangle* rects, size_t rect_count, Vector2* extents, Vector2* offset, Vector* mrects) {
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
/*
static void fetch_shaped_window_face(session_t* ps, win_id wid) {
    struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, wid);
    if(w->face != NULL)
        face_unload_file(w->face);

    struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, wid);

    Vector2 extents = physical->size;
    // X has some insane notion that borders aren't part of the window.
    // Therefore a window with a border will have a bounding shape with
    // a negative upper left corner. This offset corrects for that, so we don't
    // have to deal with it downstream
    Vector2 offset = {{-w->border_size, -w->border_size}};

    struct TracksWindowComponent* window = swiss_getComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, wid);
    XserverRegion window_region = XFixesCreateRegionFromWindow(ps->dpy, window->id, ShapeBounding);

    int rect_count;
    XRectangle* rects = XFixesFetchRegion(ps->dpy, window_region, &rect_count);

    XFixesDestroyRegion(ps->dpy, window_region);

    Vector mrects;
    vector_init(&mrects, sizeof(struct Rect), rect_count);

    convert_xrects_to_relative_rect(rects, rect_count, &extents, &offset, &mrects);

    struct face* face = malloc(sizeof(struct face));
    // Triangulate the rectangles into a triangle vertex stream
    face_init_rects(face, &mrects);
    vector_kill(&mrects);
    face_upload(face);

    w->face = face;
}
*/

/**
 * Add a pattern to a condition linked list.
 */
static bool
condlst_add(session_t *ps, c2_lptr_t **pcondlst, const char *pattern) {
  if (!pattern)
    return false;

#ifdef CONFIG_C2
  if (!c2_parse(ps, pcondlst, pattern))
    exit(1);
#else
  printf_errfq(1, "(): Condition support not compiled in.");
#endif

  return true;
}

/**
 * Determine the event mask for a window.
 */
static long determine_evmask(session_t *ps, Window wid, win_evmode_t mode) {
    long evmask = NoEventMask;
    win *w = NULL;

    bool isMapped = false;
    if((w = find_win(ps, wid))) {
        win_id eid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
        if(win_mapped(&ps->win_list, eid))
            isMapped = true;
    }

    // Check if it's a mapped frame window
    if (WIN_EVMODE_FRAME == mode || isMapped) {
        evmask |= PropertyChangeMask;
    }

    // Check if it's a mapped client window
    // @INCOMPLETE: PropertyChangeMask should also be set if we are tracking extra atoms
    if (WIN_EVMODE_CLIENT == mode || isMapped) {
        if (ps->o.track_wdata /* || ps->track_atom_lst */ || ps->o.detect_client_opacity)
            evmask |= PropertyChangeMask;
    }

    return evmask;
}

/**
 * Find out the WM frame of a client window by querying X.
 *
 * @return struct _win object of the found window, NULL if not found
 */
static win * find_toplevel2(session_t *ps, Window wid) {
    struct _win* w = find_win(ps, wid);

    // We traverse through its ancestors to find out the frame
    while (wid && wid != ps->root && w == NULL) {
        Window troot;
        Window parent;
        Window *tchildren;
        unsigned tnchildren;

        // XQueryTree probably fails if you run compton when X is somehow
        // initializing (like add it in .xinitrc). In this case just leave it
        // alone.
        if (!XQueryTree(ps->dpy, wid, &troot, &parent, &tchildren,
                    &tnchildren)) {
            wid = 0;
            break;
        }

        cxfree(tchildren);

        wid = parent;
        w = find_win(ps, wid);
    }

    return w;
}

static bool get_root_tile(session_t *ps) {
    Pixmap pixmap = None;

    // Get the values of background attributes
    for (int p = 0; background_props_str[p]; p++) {
        Atom prop_atom = get_atom(&ps->xcontext, background_props_str[p]);
        winprop_t prop = wid_get_prop(&ps->xcontext, ps->root, prop_atom, 1L, XA_PIXMAP, 32);
        if (prop.nitems) {
            pixmap = *prop.data.p32;
            free_winprop(&prop);
            break;
        }
        free_winprop(&prop);
    }

    // Make sure the pixmap we got is valid
    if (pixmap && !validate_pixmap(ps, pixmap))
        pixmap = None;

    // Create a pixmap if there isn't any
    if (!pixmap) {
        pixmap = XCreatePixmap(ps->dpy, ps->root, 1, 1, ps->depth);

        //Fill pixmap with default color
        Picture root_picture;
        XRenderPictureAttributes pa = {
            .repeat = True,
        };
        root_picture = XRenderCreatePicture(
                ps->dpy, pixmap, XRenderFindVisualFormat(ps->dpy, ps->vis),
                CPRepeat, &pa);
        XRenderColor  c;

        c.red = c.green = c.blue = 0x8080;
        c.alpha = 0xffff;
        XRenderFillRectangle(ps->dpy, PictOpSrc, root_picture, &c, 0, 0, 1, 1);
        XRenderFreePicture(ps->dpy, root_picture);
    }

    XWindowAttributes attribs;
    XGetWindowAttributes(ps->xcontext.display, ps->root, &attribs);
    GLXFBConfig* fbconfig = xorgContext_selectConfig(&ps->xcontext, XVisualIDFromVisual(attribs.visual));

    if(!xtexture_bind(&ps->root_texture, fbconfig, pixmap)) {
        printf_errf("Failed binding the root texture to gl");
        return false;
    }

    return true;
}

/**
 * Paint root window content.
 */
static void paint_root(session_t *ps) {
    // @CLEANUP: This doesn't belong here, but rather when we get notified of
    // a new root texture
    if (!ps->root_texture.bound)
        get_root_tile(ps);

    assert(ps->root_texture.bound);
    glClearColor(0.0, 0.0, 1.0, 1.0);
    /* glClear(GL_COLOR_BUFFER_BIT); */

    glViewport(0, 0, ps->root_size.x, ps->root_size.y);

    glEnable(GL_DEPTH_TEST);

    struct face* face = assets_load("window.face");
    Vector3 pos = {{0, 0, 0.9999}};
    draw_tex(face, &ps->root_texture.texture, &pos, &ps->root_size);

    glDisable(GL_DEPTH_TEST);
}

/**
 * Look for the client window of a particular window.
 */
static Window find_client_win(session_t *ps, Window w) {
    if (wid_has_prop(ps, w, ps->atoms.atom_client)) {
        return w;
    }

    Window *children;
    unsigned int nchildren;
    unsigned int i;
    Window ret = 0;

    if (!wid_get_children(ps, w, &children, &nchildren)) {
        return 0;
    }

    for (i = 0; i < nchildren; ++i) {
        if ((ret = find_client_win(ps, children[i])))
            break;
    }

    cxfree(children);

    return ret;
}

static void paint_preprocess(session_t *ps) {
    size_t index;
    win_id* w_id = vector_getLast(&ps->order, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, *w_id);

        // @CLEANUP: This should probably be somewhere else
        w->fullscreen = win_is_fullscreen(ps, *w_id);

        w_id = vector_getPrev(&ps->order, &index);
    }
}

static void assign_depth(Swiss* em, Vector* order) {
    float z = 0;
    float z_step = 1.0 / em->size;
    size_t index;
    win_id* w_id = vector_getLast(order, &index);
    while(w_id != NULL) {
        struct ZComponent* zc = swiss_getComponent(em, COMPONENT_Z, *w_id);
        zc->z = z;

        z += z_step;
        w_id = vector_getPrev(order, &index);
    }
}

static void map_win(session_t *ps, win_id wid) {
    struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, wid);
    struct TracksWindowComponent* window = swiss_getComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, wid);
    assert(w != NULL);

    XWindowAttributes attribs;
    if (!XGetWindowAttributes(ps->dpy, window->id, &attribs)) {
        printf_errf("Failed getting window attributes while mapping");
        return;
    }
    w->border_size = attribs.border_width;
    w->override_redirect = attribs.override_redirect;

    // Don't care about window mapping if it's an InputOnly window
    // Try avoiding mapping a window twice
    if (InputOnly == attribs.class || win_mapped(&ps->win_list, wid))
        return;

    assert(ps->active_win != w);

    cxinerama_win_upd_scr(ps, wid);

    // Call XSelectInput() before reading properties so that no property
    // changes are lost
    XSelectInput(ps->dpy, window->id, determine_evmask(ps, window->id, WIN_EVMODE_FRAME));

    // Make sure the XSelectInput() requests are sent
    XFlush(ps->dpy);

    win_recheck_client(ps, w);

    assert(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid));

    // FocusIn/Out may be ignored when the window is unmapped, so we must
    // recheck focus here
    if (ps->o.track_focus)
        update_ewmh_active_win(ps);

    // Add a map event
    swiss_ensureComponent(&ps->win_list, COMPONENT_MAP, wid);
    struct MapComponent* map = swiss_getComponent(&ps->win_list, COMPONENT_MAP, wid);
    map->position = (Vector2){{attribs.x, attribs.y}};
    map->size = (Vector2){{
        attribs.width + w->border_size * 2,
        attribs.height + w->border_size * 2,
    }};

    struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, wid);
    stateful->state = STATE_WAITING;
    swiss_ensureComponent(&ps->win_list, COMPONENT_FOCUS_CHANGE, wid);

#ifdef CONFIG_DBUS
    // Send D-Bus signal
    if (ps->o.dbus) {
        cdbus_ev_win_mapped(ps, w);
    }
#endif
}

static void unmap_win(session_t *ps, win *w) {
    if(w == NULL)
        return;

    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    if (!win_mapped(&ps->win_list, wid))
        return;

    // Set focus out
    if(ps->active_win == w)
        ps->active_win = NULL;

    swiss_ensureComponent(&ps->win_list, COMPONENT_UNMAP, wid);

    // don't care about properties anymore
    win_ev_stop(ps, w);

#ifdef CONFIG_DBUS
    // Send D-Bus signal
    if (ps->o.dbus) {
        cdbus_ev_win_unmapped(ps, w);
    }
#endif
}

static void
win_set_invert_color(session_t *ps, win *w, bool invert_color_new) {
  if (w->invert_color == invert_color_new) return;

  w->invert_color = invert_color_new;
}

static void
win_set_blur_background(session_t *ps, win *w, bool blur_background_new) {
  if (w->blur_background == blur_background_new) return;

  w->blur_background = blur_background_new;
}

/**
 * Mark a window as the client window of another.
 *
 * @param ps current session
 * @param w struct _win of the parent window
 * @param client window ID of the client window
 */
static void
win_mark_client(session_t *ps, win *w, Window client) {
  win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
  struct HasClientComponent* clientComponent = swiss_addComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid);

  clientComponent->id = client;

  // If the window isn't mapped yet, stop here, as the function will be
  // called in map_win()
  if (!win_mapped(&ps->win_list, wid))
    return;

  XSelectInput(ps->dpy, client,
      determine_evmask(ps, client, WIN_EVMODE_CLIENT));

  // Make sure the XSelectInput() requests are sent
  XFlush(ps->dpy);

  swiss_ensureComponent(&ps->win_list, COMPONENT_WINTYPE_CHANGE, wid);

  // Get window name and class if we are tracking them
  if (ps->o.track_wdata) {
    win_get_name(ps, w);
    win_get_class(ps, w);
    win_get_role(ps, w);
  }

  // Update everything related to conditions
  /* win_on_factor_change(ps, w); */
}

/**
 * Unmark current client window of a window.
 *
 * @param ps current session
 * @param w struct _win of the parent window
 */
static void win_unmark_client(session_t *ps, win *w) {
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    struct HasClientComponent* client = swiss_getComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid);

    // Recheck event mask
    XSelectInput(ps->dpy, client->id, determine_evmask(ps, client->id, WIN_EVMODE_UNKNOWN));

    swiss_removeComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid);
}

/**
 * Recheck client window of a window.
 *
 * @param ps current session
 * @param w struct _win of the parent window
 */
static void
win_recheck_client(session_t *ps, win *w) {
  // Initialize wmwin to false
  w->wmwin = false;

  win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
  struct TracksWindowComponent* window = swiss_getComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, wid);

  // Look for the client window

  // Always recursively look for a window with WM_STATE, as Fluxbox
  // sets override-redirect flags on all frame windows.
  Window cw = find_client_win(ps, window->id);
#ifdef DEBUG_CLIENTWIN
  if (cw)
    printf_dbgf("(%#010lx): client %#010lx\n", w->id, cw);
#endif
  // Set a window's client window to itself if we couldn't find a
  // client window
  if (!cw) {
    cw = window->id;
    w->wmwin = !w->override_redirect;
#ifdef DEBUG_CLIENTWIN
    printf_dbgf("(%#010lx): client self (%s)\n", w->id,
        (w->wmwin ? "wmwin": "override-redirected"));
#endif
  }

  if(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid)) {
      win_unmark_client(ps, w);
  }

  // Mark the new one
  win_mark_client(ps, w, cw);
}

static bool
add_win(session_t *ps, Window id) {
  const static win win_def = {
    .border_size = 0,
    .override_redirect = false,
    .xinerama_scr = -1,
    .damage = None,

    .window_type = WINTYPE_UNKNOWN,
    .wmwin = false,

    .name = NULL,
    .class_instance = NULL,
    .class_general = NULL,
    .role = NULL,

    .fade = false,
    .shadow = false,
    .dim = false,
    .invert_color = false,
    .blur_background = false,
  };

  // Reject overlay window and already added windows
  if (id == ps->overlay || find_win(ps, id)) {
    return false;
  }

  // Allocate and initialize the new win structure
  win_id slot = swiss_allocate(&ps->win_list);
  win* new = swiss_addComponent(&ps->win_list, COMPONENT_MUD, slot);

  if (!new) {
    printf_errf("(%#010lx): Failed to allocate memory for the new window.", id);
    return false;
  }

  XWindowAttributes attribs;

  // Fill structure
  set_ignore_next(ps);
  if (!XGetWindowAttributes(ps->dpy, id, &attribs) || IsUnviewable == attribs.map_state) {
      // Failed to get window attributes probably means the window is gone
      // already. IsUnviewable means the window is already reparented
      // elsewhere.
      swiss_remove(&ps->win_list, slot);
    return false;
  }

  {
      struct StatefulComponent* stateful = swiss_addComponent(&ps->win_list, COMPONENT_STATEFUL, slot);
      stateful->state = STATE_INVISIBLE;
  }

  {
      struct FadesOpacityComponent* fo = swiss_addComponent(&ps->win_list, COMPONENT_FADES_OPACITY, slot);
      fade_init(&fo->fade, 0.0);
  }

  {
      struct FadesDimComponent* fo = swiss_addComponent(&ps->win_list, COMPONENT_FADES_DIM, slot);
      fade_init(&fo->fade, 0.0);
      swiss_addComponent(&ps->win_list, COMPONENT_DIM, slot);
  }

  {
      struct TracksWindowComponent* window = swiss_addComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, slot);
      window->id = id;
  }

  {
      struct ShapedComponent* shaped = swiss_addComponent(&ps->win_list, COMPONENT_SHAPED, slot);
      shaped->face = NULL;
  }
  swiss_addComponent(&ps->win_list, COMPONENT_SHAPE_DAMAGED, slot);

  swiss_addComponent(&ps->win_list, COMPONENT_BLUR_DAMAGED, slot);

  memcpy(new, &win_def, sizeof(win_def));

#ifdef DEBUG_EVENTS
  printf_dbgf("(%#010lx): %p\n", id, new);
#endif

  new->border_size = attribs.border_width;
  new->override_redirect = attribs.override_redirect;

  // Notify compton when the shape of a window changes
  if (xorgContext_version(&ps->capabilities, PROTO_SHAPE) >= XVERSION_YES) {
      // It will stop when the window is destroyed
      XShapeSelectInput(ps->dpy, id, ShapeNotifyMask);
  }

  if (InputOutput == attribs.class) {
      // Create Damage for window
      set_ignore_next(ps);
      new->damage = XDamageCreate(ps->dpy, id, XDamageReportNonEmpty);
  }

  struct PhysicalComponent* physical = swiss_addComponent(&ps->win_list, COMPONENT_PHYSICAL, slot);
  physical->position = (Vector2){{attribs.x, attribs.y}};
  physical->size = (Vector2){{
      attribs.width + new->border_size * 2,
      attribs.height + new->border_size * 2,
  }};

  struct ZComponent* z = swiss_addComponent(&ps->win_list, COMPONENT_Z, slot);
  z->z = 0;

  vector_putBack(&ps->order, &slot);

#ifdef CONFIG_DBUS
  // Send D-Bus signal
  if (ps->o.dbus) {
    cdbus_ev_win_added(ps, new);
  }
#endif

  // Already mapped
  if (IsViewable == attribs.map_state) {
      map_win(ps, swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, new));
  }

  return true;
}

static void
restack_win(session_t *ps, win *w, Window new_above) {
    win_id w_id = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    struct _win* w_above = find_win(ps, new_above);

    // @INSPECT @RESEARCH @UNDERSTAND @HACK: Sometimes we get a bogus
    // ConfigureNotify above value for a window that doesn't even exist
    // (badwindow from X11). For now we will just not restack anything then,
    // but it seems like a hack
    if(w_above == NULL)
        return;

    win_id above_id = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w_above);

    size_t w_loc = 0;
    size_t above_loc = 0;

    size_t index;
    win_id* t = vector_getFirst(&ps->order, &index);
    while(t != NULL) {
        if(*t == w_id)
            w_loc = index;

        if(*t == above_id)
            above_loc = index;
        t = vector_getNext(&ps->order, &index);
    }

    // Circulate moves the windows between the src and target, so we
    // have to move one after the target when we are moving backwards
    if(above_loc < w_loc) {
        above_loc++;
    }

    if(w_loc == above_loc)
        return;

    vector_circulate(&ps->order, w_loc, above_loc);
}

static bool
init_filters(session_t *ps);

static void
configure_win(session_t *ps, XConfigureEvent *ce) {
  // On root window changes
  if (ce->window == ps->root) {

    ps->root_size = (Vector2) {{
        ce->width, ce->height
    }};

    // Re-redirect screen if required
    if (ps->o.reredir_on_root_change) {
      redir_stop(ps);
      redir_start(ps);
    }

    // Reinitialize GLX on root change
    if (ps->o.glx_reinit_on_root_change && ps->psglx) {
        if (!glx_reinit(ps, true))
            printf_errf("(): Failed to reinitialize GLX, troubles ahead.");
        if (!init_filters(ps))
            printf_errf("(): Failed to initialize filters.");
    }

    // GLX root change callback
    glx_on_root_change(ps);

    return;
  }

  // Other window changes
  win *w = find_win(ps, ce->window);

  if(w == NULL)
    return;

  win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

  restack_win(ps, w, ce->above);

  {
    Vector2 position = {{ce->x, ce->y}};
    struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, wid);
    if(swiss_hasComponent(&ps->win_list, COMPONENT_MOVE, wid)) {
        // If we already have a move, just override it
        // @SPEED: We might want to deduplicate before we do this
        struct MoveComponent* move = swiss_getComponent(&ps->win_list, COMPONENT_MOVE, wid);
        move->newPosition = position;
    } else if(!vec2_eq(&physical->position, &position)) {
        // Only add a move if the reconfigure has a new position
        struct MoveComponent* move = swiss_addComponent(&ps->win_list, COMPONENT_MOVE, wid);
        move->newPosition = position;
    }

    w->border_size = ce->border_width;

    Vector2 size = {{
        ce->width + w->border_size * 2,
        ce->height + w->border_size * 2,
    }};
    if(swiss_hasComponent(&ps->win_list, COMPONENT_RESIZE, wid)) {
        // If we already have a resize, just override it
        // @SPEED: We might want to deduplicate before we do this
        struct ResizeComponent* resize = swiss_getComponent(&ps->win_list, COMPONENT_RESIZE, wid);
        resize->newSize = size;
    } else if(!vec2_eq(&physical->size, &size)) {
        // Only add a resize if the reconfigure has a size
        struct ResizeComponent* resize = swiss_addComponent(&ps->win_list, COMPONENT_RESIZE, wid);
        resize->newSize = size;
    }

    cxinerama_win_upd_scr(ps, wid);
  }

  // override_redirect flag cannot be changed after window creation, as far
  // as I know, so there's no point to re-match windows here.
  w->override_redirect = ce->override_redirect;
}

static void
circulate_win(session_t *ps, XCirculateEvent *ce) {
    win *w = find_win(ps, ce->window);

    if (!w) return;

    size_t w_loc = vector_find_uint64(&ps->order, swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w));
    size_t new_loc;
    if (ce->place == PlaceOnTop) {
        new_loc = vector_size(&ps->order) - 1;
    } else {
        new_loc = 0;
    }

    if(w_loc == new_loc)
        return;

    vector_circulate(&ps->order, w_loc, new_loc);
}

static void finish_destroy_win(session_t *ps, win_id wid) {
    struct _win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, wid);
    if (w == ps->active_win)
        ps->active_win = NULL;

    size_t order_index = vector_find_uint64(&ps->order, wid);
    vector_remove(&ps->order, order_index);

    swiss_remove(&ps->win_list, wid);
}

static void destroy_win(session_t *ps, win_id wid) {
    struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, wid);

    stateful->state = STATE_DESTROYING;
    swiss_ensureComponent(&ps->win_list, COMPONENT_DESTROY, wid);

#ifdef CONFIG_DBUS
    // Send D-Bus signal
    if (ps->o.dbus) {
        cdbus_ev_win_destroyed(ps, w);
    }
#endif
}

static inline void
root_damaged(session_t *ps) {
  if (ps->root_texture.bound) {
    xtexture_unbind(&ps->root_texture);
  }
  get_root_tile(ps);
}

static void damage_win(session_t *ps, XDamageNotifyEvent *de) {
    /*
       if (ps->root == de->drawable) {
       root_damaged();
       return;
       } */


    win *w = find_win(ps, de->drawable);

    if (!w)
        return;
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

  if (!win_mapped(&ps->win_list, wid))
        return;

    //Reset the XDamage region, so we continue to recieve new damage
    XDamageSubtract(ps->dpy, w->damage, None, None);

    swiss_ensureComponent(&ps->win_list, COMPONENT_CONTENTS_DAMAGED, wid);
    // @CLEANUP: We shouldn't damage the shadow here. It's more of an update
    // thing. Maybe make a function for quick or?
    swiss_ensureComponent(&ps->win_list, COMPONENT_SHADOW_DAMAGED, wid);
}

static int xerror(Display __attribute__((unused)) *dpy, XErrorEvent *ev) {
    session_t * const ps = ps_g;

    int o = 0;
    const char *name = "Unknown";

    if (should_ignore(ps, ev->serial)) {
        return 0;
    }

    if (xorgContext_convertOpcode(&ps->capabilities, ev->request_code) == PROTO_COMPOSITE
            && ev->minor_code == X_CompositeRedirectSubwindows) {
        fprintf(stderr, "Another composite manager is already running\n");
        exit(1);
    }

#define CASESTRRET2(s)   case s: name = #s; break

    o = xorgContext_convertError(&ps->capabilities, PROTO_FIXES, ev->error_code);
    switch (o) {
        CASESTRRET2(BadRegion);
    }

    o = xorgContext_convertError(&ps->capabilities, PROTO_DAMAGE, ev->error_code);
    switch (o) {
        CASESTRRET2(BadDamage);
    }

    o = xorgContext_convertError(&ps->capabilities, PROTO_RENDER, ev->error_code);
    switch (o) {
        CASESTRRET2(BadPictFormat);
        CASESTRRET2(BadPicture);
        CASESTRRET2(BadPictOp);
        CASESTRRET2(BadGlyphSet);
        CASESTRRET2(BadGlyph);
    }

    o = xorgContext_convertError(&ps->capabilities, PROTO_GLX, ev->error_code);
    switch (o) {
        CASESTRRET2(GLX_BAD_SCREEN);
        CASESTRRET2(GLX_BAD_ATTRIBUTE);
        CASESTRRET2(GLX_NO_EXTENSION);
        CASESTRRET2(GLX_BAD_VISUAL);
        CASESTRRET2(GLX_BAD_CONTEXT);
        CASESTRRET2(GLX_BAD_VALUE);
        CASESTRRET2(GLX_BAD_ENUM);
    }

    o = xorgContext_convertError(&ps->capabilities, PROTO_SYNC, ev->error_code);
    switch (o) {
        CASESTRRET2(XSyncBadCounter);
        CASESTRRET2(XSyncBadAlarm);
        CASESTRRET2(XSyncBadFence);
    }

    switch (ev->error_code) {
        CASESTRRET2(BadAccess);
        CASESTRRET2(BadAlloc);
        CASESTRRET2(BadAtom);
        CASESTRRET2(BadColor);
        CASESTRRET2(BadCursor);
        CASESTRRET2(BadDrawable);
        CASESTRRET2(BadFont);
        CASESTRRET2(BadGC);
        CASESTRRET2(BadIDChoice);
        CASESTRRET2(BadImplementation);
        CASESTRRET2(BadLength);
        CASESTRRET2(BadMatch);
        CASESTRRET2(BadName);
        CASESTRRET2(BadPixmap);
        CASESTRRET2(BadRequest);
        CASESTRRET2(BadValue);
        CASESTRRET2(BadWindow);
    }

#undef CASESTRRET2

    print_timestamp(ps);
    {
        char buf[BUF_LEN] = "";
        XGetErrorText(ps->dpy, ev->error_code, buf, BUF_LEN);
        printf("error %4d %-12s request %4d minor %4d serial %6lu: \"%s\"\n",
                ev->error_code, name, ev->request_code,
                ev->minor_code, ev->serial, buf);
    }

    /* print_backtrace(); */

    return 0;
}

/**
 * Get the value of a type-<code>Window</code> property of a window.
 *
 * @return the value if successful, 0 otherwise
 */
static Window
wid_get_prop_window(session_t *ps, Window wid, Atom aprop) {
  // Get the attribute
  Window p = None;
  winprop_t prop = wid_get_prop(&ps->xcontext, wid, aprop, 1L, XA_WINDOW, 32);

  // Return it
  if (prop.nitems) {
    p = *prop.data.p32;
  }

  free_winprop(&prop);

  return p;
}

/**
 * Set real focused state of a window.
 */
static void win_set_focused(session_t *ps, win *w) {
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    // Unmapped windows will have their focused state reset on map
    if (!win_mapped(&ps->win_list, wid))
        return;

    if (ps->active_win == w) return;

    if (ps->active_win) {
        assert(win_mapped(&ps->win_list, wid));

        ps->active_win = NULL;
    }

    ps->active_win = w;

    assert(ps->active_win == w);

    // Update everything related to conditions
    /* win_on_factor_change(ps, w); */

#ifdef CONFIG_DBUS
    // Send D-Bus signal
    if (ps->o.dbus) {
        if (ps->active_win == w)
            cdbus_ev_win_focusin(ps, w);
        else
            cdbus_ev_win_focusout(ps, w);
    }
#endif
}

bool wid_get_text_prop(session_t *ps, Window wid, Atom prop,
    char ***pstrlst, int *pnstr) {
  XTextProperty text_prop = { NULL, None, 0, 0 };

  if (!(XGetTextProperty(ps->dpy, wid, &text_prop, prop) && text_prop.value))
    return false;

  if (Success !=
      XmbTextPropertyToTextList(ps->dpy, &text_prop, pstrlst, pnstr)
      || !*pnstr) {
    *pnstr = 0;
    if (*pstrlst)
      XFreeStringList(*pstrlst);
    cxfree(text_prop.value);
    return false;
  }

  cxfree(text_prop.value);
  return true;
}

/**
 * Get the name of a window from window ID.
 */
static bool
wid_get_name(session_t *ps, Window wid, char **name) {
  XTextProperty text_prop = { NULL, None, 0, 0 };
  char **strlst = NULL;
  int nstr = 0;

  if (!(wid_get_text_prop(ps, wid, ps->atoms.atom_name_ewmh, &strlst, &nstr))) {
#ifdef DEBUG_WINDATA
    printf_dbgf("(%#010lx): _NET_WM_NAME unset, falling back to WM_NAME.\n", wid);
#endif

    if (!(XGetWMName(ps->dpy, wid, &text_prop) && text_prop.value)) {
      return false;
    }
    if (Success !=
        XmbTextPropertyToTextList(ps->dpy, &text_prop, &strlst, &nstr)
        || !nstr || !strlst) {
      if (strlst)
        XFreeStringList(strlst);
      cxfree(text_prop.value);
      return false;
    }
    cxfree(text_prop.value);
  }

  *name = mstrcpy(strlst[0]);

  XFreeStringList(strlst);

  return true;
}

/**
 * Get the role of a window from window ID.
 */
static bool
wid_get_role(session_t *ps, Window wid, char **role) {
  char **strlst = NULL;
  int nstr = 0;

  if (!wid_get_text_prop(ps, wid, ps->atoms.atom_role, &strlst, &nstr)) {
    return false;
  }

  *role = mstrcpy(strlst[0]);

  XFreeStringList(strlst);

  return true;
}

/**
 * Retrieve a string property of a window and update its <code>win</code>
 * structure.
 */
static int
win_get_prop_str(session_t *ps, win *w, char **tgt,
    bool (*func_wid_get_prop_str)(session_t *ps, Window wid, char **tgt)) {
  int ret = -1;
  char *prop_old = *tgt;

  win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

  if(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid))
    return false;

  struct HasClientComponent* client = swiss_getComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid);
  ret = func_wid_get_prop_str(ps, client->id, tgt);

  // Return -1 if func_wid_get_prop_str() failed, 0 if the property
  // doesn't change, 1 if it changes
  if (!ret)
    ret = -1;
  else if (prop_old && !strcmp(*tgt, prop_old))
    ret = 0;
  else
    ret = 1;

  // Keep the old property if there's no new one
  if (*tgt != prop_old)
    free(prop_old);

  return ret;
}

/**
 * Retrieve the <code>WM_CLASS</code> of a window and update its
 * <code>win</code> structure.
 */
static bool
win_get_class(session_t *ps, win *w) {
  char **strlst = NULL;
  int nstr = 0;

  win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

  if(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid))
    return false;



  // Free and reset old strings
  free(w->class_instance);
  free(w->class_general);
  w->class_instance = NULL;
  w->class_general = NULL;

  struct HasClientComponent* client = swiss_getComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid);
  if (!wid_get_text_prop(ps, client->id, ps->atoms.atom_class, &strlst, &nstr))
    return false;

  // Copy the strings if successful
  w->class_instance = mstrcpy(strlst[0]);

  if (nstr > 1)
    w->class_general = mstrcpy(strlst[1]);

  XFreeStringList(strlst);

#ifdef DEBUG_WINDATA
  printf_dbgf("(%#010lx): client = %#010lx, "
      "instance = \"%s\", general = \"%s\"\n",
      w->id, client->id, w->class_instance, w->class_general);
#endif

  return true;
}

#ifdef CONFIG_DBUS
/** @name DBus hooks
 */
///@{

/**
 * Set w->fade_force of a window.
 */
void
win_set_fade_force(session_t *ps, win *w, switch_t val) {
  if (val != w->fade_force) {
    w->fade_force = val;
    ps->skip_poll = true;
  }
}

/**
 * Set w->focused_force of a window.
 */
void
win_set_focused_force(session_t *ps, win *w, switch_t val) {
  if (val != w->focused_force) {
    w->focused_force = val;
    ps->skip_poll = true;
  }
}

/**
 * Set w->invert_color_force of a window.
 */
void
win_set_invert_color_force(session_t *ps, win *w, switch_t val) {
  if (val != w->invert_color_force) {
    w->invert_color_force = val;
    ps->skip_poll = true;
  }
}

/**
 * Enable focus tracking.
 */
void
opts_init_track_focus(session_t *ps) {
    // Already tracking focus
    if (ps->o.track_focus)
        return;

    ps->o.track_focus = true;
}

/**
 * Set no_fading_openclose option.
 */
void
opts_set_no_fading_openclose(session_t *ps, bool newval) {
    if (newval != ps->o.no_fading_openclose) {
        ps->o.no_fading_openclose = newval;

        ps->skip_poll = true;
    }
}

//!@}
#endif

#ifdef DEBUG_EVENTS
static int
ev_serial(XEvent *ev) {
  if ((ev->type & 0x7f) != KeymapNotify) {
    return ev->xany.serial;
  }
  return NextRequest(ev->xany.display);
}

static const char *
ev_name(session_t *ps, XEvent *ev) {
  static char buf[128];
  switch (ev->type & 0x7f) {
    CASESTRRET(CreateNotify);
    CASESTRRET(ConfigureNotify);
    CASESTRRET(DestroyNotify);
    CASESTRRET(MapNotify);
    CASESTRRET(UnmapNotify);
    CASESTRRET(ReparentNotify);
    CASESTRRET(CirculateNotify);
    CASESTRRET(Expose);
    CASESTRRET(PropertyNotify);
    CASESTRRET(ClientMessage);
  }

  if (xorgContext_version(&ps->capabilities, PROTO_DAMAGE) >= XVERSION_YES
          && xorgContext_convertEvent(&ps->capabilities, PROTO_DAMAGE,  ev->type) == XDamageNotify)
    return "Damage";

  if (xorgContext_version(&ps->capabilities, PROTO_SHAPE) >= XVERSION_YES
          && xorgContext_convertEvent(&ps->capabilities, PROTO_SHAPE,  ev->type) == ShapeNotify)
    return "ShapeNotify";

  if (xorgContext_version(&ps->capabilities, PROTO_SYNC)  >= XVERSION_YES) {
      o = xorgContext_convertEvent(&ps->capabilities, PROTO_SYNC, ev->error_code);
      int o = ev->type - ps->xsync_event;
      switch (o) {
          CASESTRRET(XSyncCounterNotify);
          CASESTRRET(XSyncAlarmNotify);
      }
  }

  sprintf(buf, "Event %d", ev->type);

  return buf;
}

static Window
ev_window(session_t *ps, XEvent *ev) {
  switch (ev->type) {
    case CreateNotify:
      return ev->xcreatewindow.window;
    case ConfigureNotify:
      return ev->xconfigure.window;
    case DestroyNotify:
      return ev->xdestroywindow.window;
    case MapNotify:
      return ev->xmap.window;
    case UnmapNotify:
      return ev->xunmap.window;
    case ReparentNotify:
      return ev->xreparent.window;
    case CirculateNotify:
      return ev->xcirculate.window;
    case Expose:
      return ev->xexpose.window;
    case PropertyNotify:
      return ev->xproperty.window;
    case ClientMessage:
      return ev->xclient.window;
    default:
      if (xorgContext_version(&ps->capabilities, PROTO_DAMAGE) >= XVERSION_YES
              && xorgContext_convertEvent(&ps->capabilities, PROTO_DAMAGE,  ev->type) == XDamageNotify) {
        return ((XDamageNotifyEvent *)ev)->drawable;
      }

      if (xorgContext_version(&ps->capabilities, PROTO_SHAPE) >= XVERSION_YES
              && xorgContext_convertEvent(&ps->capabilities, PROTO_SHAPE,  ev->type) == ShapeNotify) {
        return ((XShapeEvent *) ev)->window;
      }

      return 0;
  }
}

static inline const char *
ev_focus_mode_name(XFocusChangeEvent* ev) {
  switch (ev->mode) {
    CASESTRRET(NotifyNormal);
    CASESTRRET(NotifyWhileGrabbed);
    CASESTRRET(NotifyGrab);
    CASESTRRET(NotifyUngrab);
  }

  return "Unknown";
}

static inline const char *
ev_focus_detail_name(XFocusChangeEvent* ev) {
  switch (ev->detail) {
    CASESTRRET(NotifyAncestor);
    CASESTRRET(NotifyVirtual);
    CASESTRRET(NotifyInferior);
    CASESTRRET(NotifyNonlinear);
    CASESTRRET(NotifyNonlinearVirtual);
    CASESTRRET(NotifyPointer);
    CASESTRRET(NotifyPointerRoot);
    CASESTRRET(NotifyDetailNone);
  }

  return "Unknown";
}

static inline void
ev_focus_report(XFocusChangeEvent* ev) {
  printf("  { mode: %s, detail: %s }\n", ev_focus_mode_name(ev),
      ev_focus_detail_name(ev));
}

#endif

// === Events ===

inline static void
ev_create_notify(session_t *ps, XCreateWindowEvent *ev) {
  assert(ev->parent == ps->root);
  add_win(ps, ev->window);
}

inline static void
ev_configure_notify(session_t *ps, XConfigureEvent *ev) {
#ifdef DEBUG_EVENTS
  printf("  { send_event: %d, "
         " above: %#010lx, "
         " override_redirect: %d }\n",
         ev->send_event, ev->above, ev->override_redirect);
#endif
  configure_win(ps, ev);
}

static void ev_destroy_notify(session_t *ps, XDestroyWindowEvent *ev) {
    win *w = find_win(ps, ev->window);
    if(w == NULL)
        return;

    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    destroy_win(ps, wid);
}

static void ev_map_notify(session_t *ps, XMapEvent *ev) {
    win *w = find_win(ps, ev->window);
    if(w != NULL) {
        map_win(ps, swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w));
    }
}

static void ev_unmap_notify(session_t *ps, XUnmapEvent *ev) {
  win *w = find_win(ps, ev->window);

  if (w)
    unmap_win(ps, w);
}

static void ev_reparent_notify(session_t *ps, XReparentEvent *ev) {

    // If a window is reparented to the root then we only have to add it.
    if (ev->parent == ps->root) {
        add_win(ps, ev->window);
        return;
    }

    // If reparented anywhere else, we remove it. (We don't care about subwindows)
    win *w = find_win(ps, ev->window);
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    destroy_win(ps, wid);

    // Reset event mask in case something wrong happens
    XSelectInput(ps->dpy, ev->window, determine_evmask(ps, ev->window, WIN_EVMODE_UNKNOWN));

    // We just destroyed the window, so it shouldn't be found
    assert(!find_toplevel(ps, ev->window));

    // Find our new frame
    win *w_frame = find_toplevel2(ps, ev->parent);

    assert(w_frame != NULL);
    if(w_frame == NULL) {
        printf_errf("New parent for %lu doesn't have a parent frame", ev->window);
        return;
    }

    win_id wid_frame = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w_frame);
    struct TracksWindowComponent* window_frame = swiss_getComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, wid_frame);

    // If the frame already has a client window, then we are done
    if(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid_frame)) {
        // The client ID might be set to our id, in which case we don't
        // ACTUALLY have a client. So the client id has to be different from
        // our id before we are done
        struct HasClientComponent* client = swiss_getComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid_frame);
        if(client->id != window_frame->id)
            return;
    }

    // If it has WM_STATE, mark it the client window
    if (wid_has_prop(ps, ev->window, ps->atoms.atom_client)) {
        w_frame->wmwin = false;

        if(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid_frame))
            win_unmark_client(ps, w_frame);

        win_mark_client(ps, w_frame, ev->window);
    } else { // Otherwise, watch for WM_STATE on it
        XSelectInput(
            ps->dpy,
            ev->window,
            determine_evmask(ps, ev->window, WIN_EVMODE_UNKNOWN) | PropertyChangeMask
        );
    }
}

inline static void
ev_circulate_notify(session_t *ps, XCirculateEvent *ev) {
  circulate_win(ps, ev);
}

/**
 * Update current active window based on EWMH _NET_ACTIVE_WIN.
 *
 * Does not change anything if we fail to get the attribute or the window
 * returned could not be found.
 */
static void
update_ewmh_active_win(session_t *ps) {
  // Search for the window
  Window wid = wid_get_prop_window(ps, ps->root, ps->atoms.atom_ewmh_active_win);
  win *w = find_win_all(ps, wid);

  if(w == NULL) {
      printf_errf("Window with the id %zu was not found", wid);
      return;
  }

  // Mark the window focused. No need to unfocus the previous one.
  win_set_focused(ps, w);
}

inline static void
ev_property_notify(session_t *ps, XPropertyEvent *ev) {
#ifdef DEBUG_EVENTS
    {
        // Print out changed atom
        char *name = XGetAtomName(ps->dpy, ev->atom);
        printf_dbg("  { atom = %s }\n", name);
        cxfree(name);
    }
#endif

    if (ps->root == ev->window) {
        if (ps->o.track_focus && ps->atoms.atom_ewmh_active_win == ev->atom) {
            update_ewmh_active_win(ps);
            return;
        }

        // Destroy the root "image" if the wallpaper probably changed
        for (int p = 0; background_props_str[p]; p++) {
            if (ev->atom == get_atom(&ps->xcontext, background_props_str[p])) {
                root_damaged(ps);
                break;
            }
        }

        // Unconcerned about any other proprties on root window
        return;
    }

    // WM_STATE
    if (ev->atom == ps->atoms.atom_client) {
        // We don't care about WM_STATE changes if this is already a client
        // window
        if (find_toplevel(ps, ev->window) == NULL) {

            // Reset event
            XSelectInput(ps->dpy, ev->window, determine_evmask(ps, ev->window, WIN_EVMODE_UNKNOWN));

            win *w_frame = find_toplevel2(ps, ev->window);
            assert(w_frame != NULL);

            win_id wid_frame = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w_frame);
            struct TracksWindowComponent* window_frame = swiss_getComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, wid_frame);


            // wid_has_prop(ps, ev->window, ps->atoms.atom_client)) {
            if(ev->state == PropertyNewValue) {
                if(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid_frame)) {
                    struct HasClientComponent* client = swiss_getComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid_frame);
                    if(client->id != window_frame->id) {
                        w_frame->wmwin = false;
                        win_unmark_client(ps, w_frame);
                        win_mark_client(ps, w_frame, ev->window);
                    }
                } else {
                    w_frame->wmwin = false;
                    win_mark_client(ps, w_frame, ev->window);
                }
            }
        }
        }

        // If _NET_WM_WINDOW_TYPE changes... God knows why this would happen, but
        // there are always some stupid applications. (#144)
        if (ev->atom == ps->atoms.atom_win_type) {
            win *w = NULL;
            if ((w = find_toplevel(ps, ev->window))) {
                win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
                swiss_ensureComponent(&ps->win_list, COMPONENT_WINTYPE_CHANGE, wid);
            }
        }

        // If name changes
        if (ps->o.track_wdata
                && (ps->atoms.atom_name == ev->atom || ps->atoms.atom_name_ewmh == ev->atom)) {
            win *w = find_toplevel(ps, ev->window);
            if (w && 1 == win_get_name(ps, w)) {
                /* win_on_factor_change(ps, w); */
            }
        }

        // If class changes
        if (ps->o.track_wdata && ps->atoms.atom_class == ev->atom) {
            win *w = find_toplevel(ps, ev->window);
            if (w) {
                win_get_class(ps, w);
                /* win_on_factor_change(ps, w); */
            }
        }

        // If role changes
        if (ps->o.track_wdata && ps->atoms.atom_role == ev->atom) {
            win *w = find_toplevel(ps, ev->window);
            if (w && 1 == win_get_role(ps, w)) {
                /* win_on_factor_change(ps, w); */
            }
        }

        // Check for other atoms we are tracking
        {
            size_t index = 0;
            Atom* atom = vector_getFirst(&ps->atoms.extra, &index);
            while(atom != NULL) {
                if (*atom == ev->atom) {
                    win *w = find_win(ps, ev->window);
                    if (!w)
                        w = find_toplevel(ps, ev->window);
                    /* if (w) */
                    /* win_on_factor_change(ps, w); */
                }
                atom = vector_getNext(&ps->atoms.extra, &index);
            }
        }
    }

inline static void
ev_damage_notify(session_t *ps, XDamageNotifyEvent *ev) {
  damage_win(ps, ev);
  ps->skip_poll = true;
}

static void ev_shape_notify(session_t *ps, XShapeEvent *ev) {
    win *w = find_win(ps, ev->window);
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    swiss_ensureComponent(&ps->win_list, COMPONENT_SHAPE_DAMAGED, wid);
    // We need to mark some damage
    // The blur isn't damaged, because it will be cut out by the new geometry

    //The shadow is damaged because the outline (and therefore the inner clip) has changed.
    swiss_ensureComponent(&ps->win_list, COMPONENT_SHADOW_DAMAGED, wid);
}

/**
 * Handle ScreenChangeNotify events from X RandR extension.
 */
static void ev_screen_change_notify(session_t *ps, XRRScreenChangeNotifyEvent __attribute__((unused)) *ev) {
    if (ps->o.xinerama_shadow_crop)
        cxinerama_upd_scrs(ps);
}

static void
ev_handle(session_t *ps, XEvent *ev) {
  if ((ev->type & 0x7f) != KeymapNotify) {
    discard_ignore(ps, ev->xany.serial);
  }
#ifdef DEBUG_EVENTS
  if (!isdamagenotify(ps, ev)) {
    Window wid = ev_window(ps, ev);
    char *window_name = NULL;
    bool to_free = false;

    to_free = ev_window_name(ps, wid, &window_name);

    print_timestamp(ps);
    printf("event %10.10s serial %#010x window %#010lx \"%s\"\n",
      ev_name(ps, ev), ev_serial(ev), wid, window_name);

    if (to_free) {
      cxfree(window_name);
      window_name = NULL;
    }
  }

#endif

  switch (ev->type) {
    case CreateNotify:
      ev_create_notify(ps, (XCreateWindowEvent *)ev);
      break;
    case ConfigureNotify:
      ev_configure_notify(ps, (XConfigureEvent *)ev);
      break;
    case DestroyNotify:
      ev_destroy_notify(ps, (XDestroyWindowEvent *)ev);
      break;
    case MapNotify:
      ev_map_notify(ps, (XMapEvent *)ev);
      break;
    case UnmapNotify:
      ev_unmap_notify(ps, (XUnmapEvent *)ev);
      break;
    case ReparentNotify:
      ev_reparent_notify(ps, (XReparentEvent *)ev);
      break;
    case CirculateNotify:
      ev_circulate_notify(ps, (XCirculateEvent *)ev);
      break;
    case Expose:
      break;
    case PropertyNotify:
      ev_property_notify(ps, (XPropertyEvent *)ev);
      break;
    default:
      if (xorgContext_version(&ps->capabilities, PROTO_SHAPE) >= XVERSION_YES
              && xorgContext_convertEvent(&ps->capabilities, PROTO_SHAPE,  ev->type) == ShapeNotify) {
        ev_shape_notify(ps, (XShapeEvent *) ev);
        break;
      }
      if (xorgContext_version(&ps->capabilities, PROTO_RANDR) >= XVERSION_YES
              && xorgContext_convertEvent(&ps->capabilities, PROTO_RANDR,  ev->type) == RRScreenChangeNotify) {
        ev_screen_change_notify(ps, (XRRScreenChangeNotifyEvent *) ev);
        break;
      }
      if (isdamagenotify(ps, ev)) {
        ev_damage_notify(ps, (XDamageNotifyEvent *) ev);
        break;
      }
  }
}

// === Main ===

/**
 * Print usage text and exit.
 */
static void
usage(int ret) {
#define WARNING_DISABLED " (DISABLED AT COMPILE TIME)"
#define WARNING
  const static char *usage_text =
    "compton (" COMPTON_VERSION ")\n"
    "usage: compton [options]\n"
    "Options:\n"
    "\n"
    "-d display\n"
    "  Which display should be managed.\n"
    "\n"
    "-o opacity\n"
    "  The translucency for shadows. (default .75)\n"
    "\n"
    "-l left-offset\n"
    "  The left offset for shadows. (default -15)\n"
    "\n"
    "-t top-offset\n"
    "  The top offset for shadows. (default -15)\n"
    "\n"
    "-I fade-in-step\n"
    "  Opacity change between steps while fading in. (default 0.028)\n"
    "\n"
    "-O fade-out-step\n"
    "  Opacity change between steps while fading out. (default 0.03)\n"
    "\n"
    "-D fade-delta-time\n"
    "  The time between steps in a fade in milliseconds. (default 10)\n"
    "\n"
    "-m opacity\n"
    "  The opacity for menus. (default 1.0)\n"
    "\n"
    "-c\n"
    "  Enabled client-side shadows on windows.\n"
    "\n"
    "-C\n"
    "  Avoid drawing shadows on dock/panel windows.\n"
    "\n"
    "-z\n"
    "  Zero the part of the shadow's mask behind the window.\n"
    "\n"
    "-f\n"
    "  Fade windows in/out when opening/closing and when opacity\n"
    "  changes, unless --no-fading-openclose is used.\n"
    "\n"
    "-F\n"
    "  Equals to -f. Deprecated.\n"
    "\n"
    "-i opacity\n"
    "  Opacity of inactive windows. (0.1 - 1.0)\n"
    "\n"
    "-e opacity\n"
    "  Opacity of window titlebars and borders. (0.1 - 1.0)\n"
    "\n"
    "-G\n"
    "  Don't draw shadows on DND windows\n"
    "\n"
    "-b\n"
    "  Daemonize process.\n"
    "\n"
    "-S\n"
    "  Enable synchronous operation (for debugging).\n"
    "\n"
    "--show-all-xerrors\n"
    "  Show all X errors (for debugging).\n"
    "\n"
#undef WARNING
#ifndef CONFIG_LIBCONFIG
#define WARNING WARNING_DISABLED
#else
#define WARNING
#endif
    "--config path\n"
    "  Look for configuration file at the path. Use /dev/null to avoid\n"
    "  loading configuration file." WARNING "\n"
    "\n"
    "--write-pid-path path\n"
    "  Write process ID to a file.\n"
    "\n"
    "--shadow-red value\n"
    "  Red color value of shadow (0.0 - 1.0, defaults to 0).\n"
    "\n"
    "--shadow-green value\n"
    "  Green color value of shadow (0.0 - 1.0, defaults to 0).\n"
    "\n"
    "--shadow-blue value\n"
    "  Blue color value of shadow (0.0 - 1.0, defaults to 0).\n"
    "\n"
    "--inactive-opacity-override\n"
    "  Inactive opacity set by -i overrides value of _NET_WM_OPACITY.\n"
    "\n"
    "--inactive-dim value\n"
    "  Dim inactive windows. (0.0 - 1.0)\n"
    "\n"
    "--active-opacity opacity\n"
    "  Default opacity for active windows. (0.0 - 1.0)\n"
    "\n"
    "--mark-wmwin-focused\n"
    "  Try to detect WM windows and mark them as active.\n"
    "\n"
    "--shadow-exclude condition\n"
    "  Exclude conditions for shadows.\n"
    "\n"
    "--fade-exclude condition\n"
    "  Exclude conditions for fading.\n"
    "\n"
    "--no-fading-openclose\n"
    "  Do not fade on window open/close.\n"
    "\n"
    "--no-fading-destroyed-argb\n"
    "  Do not fade destroyed ARGB windows with WM frame. Workaround of bugs\n"
    "  in Openbox, Fluxbox, etc.\n"
    "\n"
    "--detect-rounded-corners\n"
    "  Try to detect windows with rounded corners and don't consider\n"
    "  them shaped windows. Affects --shadow-ignore-shaped,\n"
    "  and possibly others. You need to turn this\n"
    "  on manually if you want to match against rounded_corners in\n"
    "  conditions.\n"
    "\n"
    "--detect-client-opacity\n"
    "  Detect _NET_WM_OPACITY on client windows, useful for window\n"
    "  managers not passing _NET_WM_OPACITY of client windows to frame\n"
    "  windows.\n"
    "\n"
    "--vsync-aggressive\n"
    "  Attempt to send painting request before VBlank and do XFlush()\n"
    "  during VBlank. This switch may be lifted out at any moment.\n"
    "\n"
    "--alpha-step val\n"
    "  X Render backend: Step for pregenerating alpha pictures. \n"
    "  0.01 - 1.0. Defaults to 0.03.\n"
    "\n"
    "--paint-on-overlay\n"
    "  Painting on X Composite overlay window.\n"
    "\n"
    "--respect-prop-shadow\n"
    "  Respect _COMPTON_SHADOW. This a prototype-level feature, which\n"
    "  you must not rely on.\n"
    "\n"
    "--focus-exclude condition\n"
    "  Specify a list of conditions of windows that should always be\n"
    "  considered focused.\n"
    "\n"
    "--blur-background\n"
    "  Blur background of semi-transparent / ARGB windows. Bad in\n"
    "  performance. The switch name may change without prior\n"
    "  notifications.\n"
    "\n"
    "--blur-background-exclude condition\n"
    "  Exclude conditions for background blur.\n"
    "\n"
    "--invert-color-include condition\n"
    "  Specify a list of conditions of windows that should be painted with\n"
    "  inverted color. Resource-hogging, and is not well tested.\n"
    "\n"
    "--opacity-rule opacity:condition\n"
    "  Specify a list of opacity rules, in the format \"PERCENT:PATTERN\",\n"
    "  like \'50:name *= \"Firefox\"'. compton-trans is recommended over\n"
    "  this. Note we do not distinguish 100% and unset, and we don't make\n"
    "  any guarantee about possible conflicts with other programs that set\n"
    "  _NET_WM_WINDOW_OPACITY on frame or client windows.\n"
    "\n"
    "--shadow-exclude-reg geometry\n"
    "  Specify a X geometry that describes the region in which shadow\n"
    "  should not be painted in, such as a dock window region.\n"
    "  Use --shadow-exclude-reg \'x10+0-0\', for example, if the 10 pixels\n"
    "  on the bottom of the screen should not have shadows painted on.\n"
    "\n"
    "--xinerama-shadow-crop\n"
    "  Crop shadow of a window fully on a particular Xinerama screen to the\n"
    "  screen.\n"
    "\n"
    "--glx-swap-method undefined/copy/exchange/3/4/5/6/buffer-age\n"
    "  GLX backend: GLX buffer swap method we assume. Could be\n"
    "  undefined (0), copy (1), exchange (2), 3-6, or buffer-age (-1).\n"
    "  \"undefined\" is the slowest and the safest, and the default value.\n"
    "  1 is fastest, but may fail on some drivers, 2-6 are gradually slower\n"
    "  but safer (6 is still faster than 0). -1 means auto-detect using\n"
    "  GLX_EXT_buffer_age, supported by some drivers. Useless with\n"
    "  --glx-use-copysubbuffermesa.\n"
    "\n"
#undef WARNING
#ifndef CONFIG_DBUS
#define WARNING WARNING_DISABLED
#else
#define WARNING
#endif
    "--dbus\n"
    "  Enable remote control via D-Bus. See the D-BUS API section in the\n"
    "  man page for more details." WARNING "\n"
    "\n"
    "--benchmark cycles\n"
    "  Benchmark mode. Repeatedly paint until reaching the specified cycles.\n"
    "\n"
    "--benchmark-wid window-id\n"
    "  Specify window ID to repaint in benchmark mode. If omitted or is 0,\n"
    "  the whole screen is repainted.\n"
    ;
  FILE *f = (ret ? stderr: stdout);
  fputs(usage_text, f);
#undef WARNING
#undef WARNING_DISABLED

  exit(ret);
}

/**
 * Register a window as symbol, and initialize GLX context if wanted.
 */
static bool
register_cm(session_t *ps) {
  assert(!ps->reg_win);

  ps->reg_win = XCreateSimpleWindow(ps->dpy, ps->root, 0, 0, 1, 1, 0,
        None, None);

  if (!ps->reg_win) {
    printf_errf("(): Failed to create window.");
    return false;
  }

  XCompositeUnredirectWindow(ps->dpy, ps->reg_win, CompositeRedirectManual);

  {
    XClassHint *h = XAllocClassHint();
    if (h) {
      h->res_name = "compton";
      h->res_class = "xcompmgr";
    }
    Xutf8SetWMProperties(ps->dpy, ps->reg_win, "xcompmgr", "xcompmgr",
        NULL, 0, NULL, NULL, h);
    cxfree(h);
  }

  // Set _NET_WM_PID
  {
    long pid = getpid();
    if (!XChangeProperty(ps->dpy, ps->reg_win,
          get_atom(&ps->xcontext, "_NET_WM_PID"), XA_CARDINAL, 32, PropModeReplace,
          (unsigned char *) &pid, 1)) {
      printf_errf("(): Failed to set _NET_WM_PID.");
    }
  }

  // Set COMPTON_VERSION
  if (!wid_set_text_prop(ps, ps->reg_win, get_atom(&ps->xcontext, "COMPTON_VERSION"), COMPTON_VERSION)) {
    printf_errf("(): Failed to set COMPTON_VERSION.");
  }

  // Acquire X Selection _NET_WM_CM_S?
  if (!ps->o.no_x_selection) {
    unsigned len = strlen(REGISTER_PROP) + 2;
    int s = ps->scr;

    while (s >= 10) {
      ++len;
      s /= 10;
    }

    char *buf = malloc(len);
    snprintf(buf, len, REGISTER_PROP "%d", ps->scr);
    buf[len - 1] = '\0';
    XSetSelectionOwner(ps->dpy, get_atom(&ps->xcontext, buf), ps->reg_win, 0);
    free(buf);
  }

  return true;
}

/**
 * Reopen streams for logging.
 */
static bool
ostream_reopen(session_t *ps, const char *path) {
  if (!path)
    path = ps->o.logpath;
  if (!path)
    path = "/dev/null";

  bool success = freopen(path, "a", stdout);
  success = freopen(path, "a", stderr) && success;
  if (!success)
    printf_errfq(1, "(%s): freopen() failed.", path);

  return success;
}

/**
 * Fork program to background and disable all I/O streams.
 */
static inline bool
fork_after(session_t *ps) {
  if (getppid() == 1)
    return true;

  // GLX context must be released and reattached on fork
  if (glx_has_context(ps) && !glXMakeCurrent(ps->dpy, None, NULL)) {
      printf_errf("(): Failed to detach GLx context.");
      return false;
  }

  int pid = fork();

  if (-1 == pid) {
    printf_errf("(): fork() failed.");
    return false;
  }

  if (pid > 0) _exit(0);

  setsid();

  if (glx_has_context(ps)
          && !glXMakeCurrent(ps->dpy, get_tgt_window(ps), ps->psglx->context)) {
      printf_errf("(): Failed to make GLX context current.");
      return false;
  }

  // Mainly to suppress the _FORTIFY_SOURCE warning
  bool success = freopen("/dev/null", "r", stdin);
  if (!success) {
    printf_errf("(): freopen() failed.");
    return false;
  }

  return success;
}

/**
 * Write PID to a file.
 */
static inline bool
write_pid(session_t *ps) {
  if (!ps->o.write_pid_path)
    return true;

  FILE *f = fopen(ps->o.write_pid_path, "w");
  if (unlikely(!f)) {
    printf_errf("(): Failed to write PID to \"%s\".", ps->o.write_pid_path);
    return false;
  }

  fprintf(f, "%ld\n", (long) getpid());
  fclose(f);

  return true;
}

/**
 * Parse a long number.
 */
static inline bool
parse_long(const char *s, long *dest) {
  const char *endptr = NULL;
  long val = strtol(s, (char **) &endptr, 0);
  if (!endptr || endptr == s) {
    printf_errf("(\"%s\"): Invalid number.", s);
    return false;
  }
  while (isspace(*endptr))
    ++endptr;
  if (*endptr) {
    printf_errf("(\"%s\"): Trailing characters.", s);
    return false;
  }
  *dest = val;
  return true;
}

/**
 * Parse a X geometry.
 */
static inline bool
parse_geometry(session_t *ps, const char *src, geometry_t *dest) {
  geometry_t geom = { .wid = -1, .hei = -1, .x = -1, .y = -1 };
  long val = 0L;
  char *endptr = NULL;

#define T_STRIPSPACE() do { \
  while (*src && isspace(*src)) ++src; \
  if (!*src) goto parse_geometry_end; \
} while(0)

  T_STRIPSPACE();

  // Parse width
  // Must be base 10, because "0x0..." may appear
  if (!('+' == *src || '-' == *src)) {
    val = strtol(src, &endptr, 10);
    if (endptr && src != endptr) {
      geom.wid = val;
      assert(geom.wid >= 0);
      src = endptr;
    }
    T_STRIPSPACE();
  }

  // Parse height
  if ('x' == *src) {
    ++src;
    val = strtol(src, &endptr, 10);
    if (endptr && src != endptr) {
      geom.hei = val;
      if (geom.hei < 0) {
        printf_errf("(\"%s\"): Invalid height.", src);
        return false;
      }
      src = endptr;
    }
    T_STRIPSPACE();
  }

  // Parse x
  if ('+' == *src || '-' == *src) {
    val = strtol(src, &endptr, 10);
    if (endptr && src != endptr) {
      geom.x = val;
      if ('-' == *src && geom.x <= 0)
        geom.x -= 2;
      src = endptr;
    }
    T_STRIPSPACE();
  }

  // Parse y
  if ('+' == *src || '-' == *src) {
    val = strtol(src, &endptr, 10);
    if (endptr && src != endptr) {
      geom.y = val;
      if ('-' == *src && geom.y <= 0)
        geom.y -= 2;
      src = endptr;
    }
    T_STRIPSPACE();
  }

  if (*src) {
    printf_errf("(\"%s\"): Trailing characters.", src);
    return false;
  }

parse_geometry_end:
  *dest = geom;
  return true;
}

/**
 * Parse a list of opacity rules.
 */
static inline bool
parse_rule_opacity(session_t *ps, const char *src) {
#ifdef CONFIG_C2
  // Find opacity value
  char *endptr = NULL;
  long val = strtol(src, &endptr, 0);
  if (!endptr || endptr == src) {
    printf_errf("(\"%s\"): No opacity specified?", src);
    return false;
  }
  if (val > 100 || val < 0) {
    printf_errf("(\"%s\"): Opacity %ld invalid.", src, val);
    return false;
  }

  // Skip over spaces
  while (*endptr && isspace(*endptr))
    ++endptr;
  if (':' != *endptr) {
    printf_errf("(\"%s\"): Opacity terminator not found.", src);
    return false;
  }
  ++endptr;

  // Parse pattern
  // I hope 1-100 is acceptable for (void *)
  return c2_parsed(ps, &ps->o.opacity_rules, endptr, (void *) val);
#else
  printf_errf("(\"%s\"): Condition support not compiled in.", src);
  return false;
#endif
}

#ifdef CONFIG_LIBCONFIG
/**
 * Get a file stream of the configuration file to read.
 *
 * Follows the XDG specification to search for the configuration file.
 */
static FILE *
open_config_file(char *cpath, char **ppath) {
  const static char *config_filename = "/compton.conf";
  const static char *config_filename_legacy = "/.compton.conf";
  const static char *config_home_suffix = "/.config";
  const static char *config_system_dir = "/etc/xdg";

  char *dir = NULL, *home = NULL;
  char *path = cpath;
  FILE *f = NULL;

  if (path) {
    f = fopen(path, "r");
    if (f && ppath)
      *ppath = path;
    return f;
  }

  // Check user configuration file in $XDG_CONFIG_HOME firstly
  if (!((dir = getenv("XDG_CONFIG_HOME")) && strlen(dir))) {
    if (!((home = getenv("HOME")) && strlen(home)))
      return NULL;

    path = mstrjoin3(home, config_home_suffix, config_filename);
  }
  else
    path = mstrjoin(dir, config_filename);

  f = fopen(path, "r");

  if (f && ppath)
    *ppath = path;
  else
    free(path);
  if (f)
    return f;

  // Then check user configuration file in $HOME
  if ((home = getenv("HOME")) && strlen(home)) {
    path = mstrjoin(home, config_filename_legacy);
    f = fopen(path, "r");
    if (f && ppath)
      *ppath = path;
    else
      free(path);
    if (f)
      return f;
  }

  // Check system configuration file in $XDG_CONFIG_DIRS at last
  if ((dir = getenv("XDG_CONFIG_DIRS")) && strlen(dir)) {
    char *part = strtok(dir, ":");
    while (part) {
      path = mstrjoin(part, config_filename);
      f = fopen(path, "r");
      if (f && ppath)
        *ppath = path;
      else
        free(path);
      if (f)
        return f;
      part = strtok(NULL, ":");
    }
  }
  else {
    path = mstrjoin(config_system_dir, config_filename);
    f = fopen(path, "r");
    if (f && ppath)
      *ppath = path;
    else
      free(path);
    if (f)
      return f;
  }

  return NULL;
}

/**
 * Parse a condition list in configuration file.
 */
static inline void
parse_cfg_condlst(session_t *ps, const config_t *pcfg, c2_lptr_t **pcondlst,
    const char *name) {
  config_setting_t *setting = config_lookup(pcfg, name);
  if (setting) {
    // Parse an array of options
    if (config_setting_is_array(setting)) {
      int i = config_setting_length(setting);
      while (i--)
        condlst_add(ps, pcondlst, config_setting_get_string_elem(setting, i));
    }
    // Treat it as a single pattern if it's a string
    else if (CONFIG_TYPE_STRING == config_setting_type(setting)) {
      condlst_add(ps, pcondlst, config_setting_get_string(setting));
    }
  }
}

/**
 * Parse an opacity rule list in configuration file.
 */
static inline void
parse_cfg_condlst_opct(session_t *ps, const config_t *pcfg, const char *name) {
  config_setting_t *setting = config_lookup(pcfg, name);
  if (setting) {
    // Parse an array of options
    if (config_setting_is_array(setting)) {
      int i = config_setting_length(setting);
      while (i--)
        if (!parse_rule_opacity(ps, config_setting_get_string_elem(setting, i)))
          exit(1);
    }
    // Treat it as a single pattern if it's a string
    else if (CONFIG_TYPE_STRING == config_setting_type(setting)) {
      parse_rule_opacity(ps, config_setting_get_string(setting));
    }
  }
}

/**
 * Parse a configuration file from default location.
 */
static void
parse_config(session_t *ps, struct options_tmp *pcfgtmp) {
  char *path = NULL;
  FILE *f;
  config_t cfg;
  int ival = 0;
  double dval = 0.0;
  // libconfig manages string memory itself, so no need to manually free
  // anything
  const char *sval = NULL;

  f = open_config_file(ps->o.config_file, &path);
  if (!f) {
    if (ps->o.config_file) {
      printf_errfq(1, "(): Failed to read configuration file \"%s\".",
          ps->o.config_file);
      free(ps->o.config_file);
      ps->o.config_file = NULL;
    }
    return;
  }

  config_init(&cfg);
#ifndef CONFIG_LIBCONFIG_LEGACY
  {
    // dirname() could modify the original string, thus we must pass a
    // copy
    char *path2 = mstrcpy(path);
    char *parent = dirname(path2);

    if (parent)
      config_set_include_dir(&cfg, parent);

    free(path2);
  }
#endif

  {
    int read_result = config_read(&cfg, f);
    fclose(f);
    f = NULL;
    if (CONFIG_FALSE == read_result) {
      printf("Error when reading configuration file \"%s\", line %d: %s\n",
          path, config_error_line(&cfg), config_error_text(&cfg));
      config_destroy(&cfg);
      free(path);
      return;
    }
  }
  config_set_auto_convert(&cfg, 1);

  if (path != ps->o.config_file) {
    free(ps->o.config_file);
    ps->o.config_file = path;
  }

  // Get options from the configuration file. We don't do range checking
  // right now. It will be done later

  // -i (inactive_opacity)
  if (config_lookup_float(&cfg, "inactive-opacity", &dval))
    ps->o.inactive_opacity = normalize_d(dval) * 100.0;
  // --active_opacity
  if (config_lookup_float(&cfg, "active-opacity", &dval))
    ps->o.active_opacity = normalize_d(dval) * 100.0;
  // --opacity-fade-time
  if (config_lookup_float(&cfg, "opacity-fade-time", &dval))
    ps->o.opacity_fade_time = dval;
  // -c (shadow_enable)
  if (config_lookup_bool(&cfg, "shadow", &ival) && ival)
    wintype_arr_enable(ps->o.wintype_shadow);
  // -C (no_dock_shadow)
  lcfg_lookup_bool(&cfg, "no-dock-shadow", &pcfgtmp->no_dock_shadow);
  // -G (no_dnd_shadow)
  lcfg_lookup_bool(&cfg, "no-dnd-shadow", &pcfgtmp->no_dnd_shadow);
  // -m (menu_opacity)
  config_lookup_float(&cfg, "menu-opacity", &pcfgtmp->menu_opacity);
  // -f (fading_enable)
  if (config_lookup_bool(&cfg, "fading", &ival) && ival)
    wintype_arr_enable(ps->o.wintype_fade);
  // --no-fading-open-close
  lcfg_lookup_bool(&cfg, "no-fading-openclose", &ps->o.no_fading_openclose);
  // --no-fading-destroyed-argb
  lcfg_lookup_bool(&cfg, "no-fading-destroyed-argb",
      &ps->o.no_fading_destroyed_argb);
  // --inactive-opacity-override
  lcfg_lookup_bool(&cfg, "inactive-opacity-override",
      &ps->o.inactive_opacity_override);
  // --inactive-dim
  config_lookup_float(&cfg, "inactive-dim", &ps->o.inactive_dim);
  // --dim-fade-time
  if (config_lookup_float(&cfg, "dim-fade-time", &dval))
      ps->o.dim_fade_time = dval;
  // --mark-wmwin-focused
  lcfg_lookup_bool(&cfg, "mark-wmwin-focused", &ps->o.mark_wmwin_focused);
  // --xinerama-shadow-crop
  lcfg_lookup_bool(&cfg, "xinerama-shadow-crop",
      &ps->o.xinerama_shadow_crop);
  // --detect-client-opacity
  lcfg_lookup_bool(&cfg, "detect-client-opacity",
      &ps->o.detect_client_opacity);
  // --vsync
  if (config_lookup_string(&cfg, "vsync", &sval) && !parse_vsync(ps, sval))
    exit(1);
  // --shadow-exclude
  parse_cfg_condlst(ps, &cfg, &ps->o.shadow_blacklist, "shadow-exclude");
  // --fade-exclude
  parse_cfg_condlst(ps, &cfg, &ps->o.fade_blacklist, "fade-exclude");
  // --focus-exclude
  parse_cfg_condlst(ps, &cfg, &ps->o.focus_blacklist, "focus-exclude");
  // --invert-color-include
  parse_cfg_condlst(ps, &cfg, &ps->o.invert_color_list, "invert-color-include");
  // --blur-background-exclude
  parse_cfg_condlst(ps, &cfg, &ps->o.blur_background_blacklist, "blur-background-exclude");
  // --opacity-rule
  parse_cfg_condlst_opct(ps, &cfg, "opacity-rule");
  // --blur-background
  lcfg_lookup_bool(&cfg, "blur-background", &ps->o.blur_background);
  // --blur-level
  lcfg_lookup_int(&cfg, "blur-level", &ps->o.blur_level);
  // --glx-swap-method
  if (config_lookup_string(&cfg, "glx-swap-method", &sval)
      && !parse_glx_swap_method(ps, sval))
    exit(1);
  // Wintype settings
  {
    wintype_t i;

    for (i = 0; i < NUM_WINTYPES; ++i) {
      char *str = mstrjoin("wintypes.", WINTYPES[i]);
      config_setting_t *setting = config_lookup(&cfg, str);
      free(str);
      if (setting) {
        if (config_setting_lookup_bool(setting, "shadow", &ival))
          ps->o.wintype_shadow[i] = (bool) ival;
        if (config_setting_lookup_bool(setting, "fade", &ival))
          ps->o.wintype_fade[i] = (bool) ival;
        if (config_setting_lookup_bool(setting, "focus", &ival))
          ps->o.wintype_focus[i] = (bool) ival;
        config_setting_lookup_float(setting, "opacity",
            &ps->o.wintype_opacity[i]);
      }
    }
  }

  config_destroy(&cfg);
}
#endif

/**
 * Process arguments and configuration files.
 */
static void
get_cfg(session_t *ps, int argc, char *const *argv, bool first_pass) {
  const static char *shortopts = "D:I:O:d:r:o:m:l:t:i:e:hscnfFCaSzGb";
  const static struct option longopts[] = {
    { "help", no_argument, NULL, 'h' },
    { "config", required_argument, NULL, 256 },
    { "shadow-opacity", required_argument, NULL, 'o' },
    { "shadow-offset-x", required_argument, NULL, 'l' },
    { "shadow-offset-y", required_argument, NULL, 't' },
    { "fade-in-step", required_argument, NULL, 'I' },
    { "fade-out-step", required_argument, NULL, 'O' },
    { "fade-delta", required_argument, NULL, 'D' },
    { "menu-opacity", required_argument, NULL, 'm' },
    { "shadow", no_argument, NULL, 'c' },
    { "no-dock-shadow", no_argument, NULL, 'C' },
    { "clear-shadow", no_argument, NULL, 'z' },
    { "fading", no_argument, NULL, 'f' },
    { "inactive-opacity", required_argument, NULL, 'i' },
    { "opacity-fade-time", required_argument, NULL, 'T' },
    { "frame-opacity", required_argument, NULL, 'e' },
    { "daemon", no_argument, NULL, 'b' },
    { "no-dnd-shadow", no_argument, NULL, 'G' },
    { "shadow-red", required_argument, NULL, 257 },
    { "shadow-green", required_argument, NULL, 258 },
    { "shadow-blue", required_argument, NULL, 259 },
    { "inactive-opacity-override", no_argument, NULL, 260 },
    { "inactive-dim", required_argument, NULL, 261 },
    { "mark-wmwin-focused", no_argument, NULL, 262 },
    { "shadow-exclude", required_argument, NULL, 263 },
    { "dim-fade-time", required_argument, NULL, 264 },
    { "no-fading-openclose", no_argument, NULL, 265 },
    { "shadow-ignore-shaped", no_argument, NULL, 266 },
    { "detect-rounded-corners", no_argument, NULL, 267 },
    { "detect-client-opacity", no_argument, NULL, 268 },
    { "vsync", required_argument, NULL, 270 },
    { "alpha-step", required_argument, NULL, 271 },
    { "paint-on-overlay", no_argument, NULL, 273 },
    { "use-ewmh-active-win", no_argument, NULL, 276 },
    { "respect-prop-shadow", no_argument, NULL, 277 },
    { "focus-exclude", required_argument, NULL, 279 },
    { "blur-background", no_argument, NULL, 283 },
    { "dbus", no_argument, NULL, 286 },
    { "logpath", required_argument, NULL, 287 },
    { "invert-color-include", required_argument, NULL, 288 },
    { "opengl", no_argument, NULL, 289 },
    { "benchmark", required_argument, NULL, 293 },
    { "benchmark-wid", required_argument, NULL, 294 },
    { "glx-use-copysubbuffermesa", no_argument, NULL, 295 },
    { "blur-background-exclude", required_argument, NULL, 296 },
    { "active-opacity", required_argument, NULL, 297 },
    { "glx-swap-method", required_argument, NULL, 299 },
    { "fade-exclude", required_argument, NULL, 300 },
    { "blur-level", required_argument, NULL, 301 },
    { "opacity-rule", required_argument, NULL, 304 },
    { "shadow-exclude-reg", required_argument, NULL, 305 },
    { "xinerama-shadow-crop", no_argument, NULL, 307 },
    { "write-pid-path", required_argument, NULL, 310 },
    { "show-all-xerrors", no_argument, NULL, 314 },
    { "no-fading-destroyed-argb", no_argument, NULL, 315 },
    { "version", no_argument, NULL, 318 },
    { "no-x-selection", no_argument, NULL, 319 },
    { "reredir-on-root-change", no_argument, NULL, 731 },
    { "glx-reinit-on-root-change", no_argument, NULL, 732 },
    // Must terminate with a NULL entry
    { NULL, 0, NULL, 0 },
  };

  int o = 0, longopt_idx = -1, i = 0;

  if (first_pass) {
    // Pre-parse the commandline arguments to check for --config and invalid
    // switches
    // Must reset optind to 0 here in case we reread the commandline
    // arguments
    optind = 1;
    while (-1 !=
        (o = getopt_long(argc, argv, shortopts, longopts, &longopt_idx))) {
      if (256 == o)
        ps->o.config_file = mstrcpy(optarg);
      else if ('d' == o)
        ps->o.display = mstrcpy(optarg);
      else if ('S' == o)
        ps->o.synchronize = true;
      else if (314 == o)
        ps->o.show_all_xerrors = true;
      else if (318 == o) {
        printf("%s\n", COMPTON_VERSION);
        exit(0);
      }
      else if ('?' == o || ':' == o)
        usage(1);
    }

    // Check for abundant positional arguments
    if (optind < argc)
      printf_errfq(1, "(): compton doesn't accept positional arguments.");

    return;
  }

  struct options_tmp cfgtmp = {
    .no_dock_shadow = false,
    .no_dnd_shadow = false,
    .menu_opacity = 1.0,
  };
  bool shadow_enable = false, fading_enable = false;
  char *lc_numeric_old = mstrcpy(setlocale(LC_NUMERIC, NULL));

  for (i = 0; i < NUM_WINTYPES; ++i) {
    ps->o.wintype_fade[i] = false;
    ps->o.wintype_shadow[i] = false;
    ps->o.wintype_opacity[i] = -1.0;
  }

  // Enforce LC_NUMERIC locale "C" here to make sure dots are recognized
  // instead of commas in atof().
  setlocale(LC_NUMERIC, "C");

#ifdef CONFIG_LIBCONFIG
  parse_config(ps, &cfgtmp);
#endif

  // Parse commandline arguments. Range checking will be done later.

  optind = 1;
  while (-1 !=
      (o = getopt_long(argc, argv, shortopts, longopts, &longopt_idx))) {
    long val = 0;
    switch (o) {
#define P_CASEBOOL(idx, option) case idx: ps->o.option = true; break
#define P_CASELONG(idx, option) \
      case idx: \
        if (!parse_long(optarg, &val)) exit(1); \
        ps->o.option = val; \
        break

      // Short options
      case 'h':
        usage(0);
        break;
      case 'd':
      case 'S':
      case 314:
      case 318:
      case 320:
        break;
      case 'c':
        shadow_enable = true;
        break;
      case 'C':
        cfgtmp.no_dock_shadow = true;
        break;
      case 'G':
        cfgtmp.no_dnd_shadow = true;
        break;
      case 'm':
        cfgtmp.menu_opacity = atof(optarg);
        break;
      case 'f':
      case 'F':
        fading_enable = true;
        break;
      case 'i':
        ps->o.inactive_opacity = (normalize_d(atof(optarg)) * 100.0);
        break;
      case 'T':
        ps->o.opacity_fade_time = atof(optarg);
        break;
      case 'n':
      case 'a':
      case 's':
        printf_errfq(1, "(): -n, -a, and -s have been removed.");
        break;
      P_CASEBOOL('b', fork_after_register);
      // Long options
      case 256:
        // --config
        break;
      P_CASEBOOL(260, inactive_opacity_override);
      case 261:
        // --inactive-dim
        ps->o.inactive_dim = atof(optarg);
        break;
      P_CASEBOOL(262, mark_wmwin_focused);
      case 263:
        // --shadow-exclude
        condlst_add(ps, &ps->o.shadow_blacklist, optarg);
        break;
      case 264:
        // --inactive-dim
        ps->o.dim_fade_time = atof(optarg);
        break;
      P_CASEBOOL(265, no_fading_openclose);
      P_CASEBOOL(268, detect_client_opacity);
      case 270:
        // --vsync
        if (!parse_vsync(ps, optarg))
          exit(1);
        break;
      P_CASEBOOL(277, respect_prop_shadow);
      case 279:
        // --focus-exclude
        condlst_add(ps, &ps->o.focus_blacklist, optarg);
        break;
      P_CASEBOOL(283, blur_background);
      P_CASEBOOL(286, dbus);
      case 287:
        // --logpath
        ps->o.logpath = mstrcpy(optarg);
        break;
      case 288:
        // --invert-color-include
        condlst_add(ps, &ps->o.invert_color_list, optarg);
        break;
      P_CASELONG(293, benchmark);
      case 294:
        // --benchmark-wid
        ps->o.benchmark_wid = strtol(optarg, NULL, 0);
        break;
      case 296:
        // --blur-background-exclude
        condlst_add(ps, &ps->o.blur_background_blacklist, optarg);
        break;
      case 297:
        // --active-opacity
        ps->o.active_opacity = (normalize_d(atof(optarg)) * 100.0);
        break;
      case 299:
        // --glx-swap-method
        if (!parse_glx_swap_method(ps, optarg))
          exit(1);
        break;
      case 300:
        // --fade-exclude
        condlst_add(ps, &ps->o.fade_blacklist, optarg);
        break;
      P_CASELONG(301, blur_level);
      case 304:
        // --opacity-rule
        if (!parse_rule_opacity(ps, optarg))
          exit(1);
        break;
      case 306:
        // --paint-exclude
        condlst_add(ps, &ps->o.paint_blacklist, optarg);
        break;
      P_CASEBOOL(307, xinerama_shadow_crop);
      case 310:
        // --write-pid-path
        ps->o.write_pid_path = mstrcpy(optarg);
        break;
      P_CASEBOOL(315, no_fading_destroyed_argb);
      P_CASEBOOL(319, no_x_selection);
      P_CASEBOOL(731, reredir_on_root_change);
      P_CASEBOOL(732, glx_reinit_on_root_change);
      default:
        usage(1);
        break;
#undef P_CASEBOOL
    }
  }

  // Restore LC_NUMERIC
  setlocale(LC_NUMERIC, lc_numeric_old);
  free(lc_numeric_old);

  // Range checking and option assignments
  ps->o.inactive_dim = normalize_d(ps->o.inactive_dim) * 100;
  cfgtmp.menu_opacity = normalize_d(cfgtmp.menu_opacity);
  if (shadow_enable)
    wintype_arr_enable(ps->o.wintype_shadow);
  ps->o.wintype_shadow[WINTYPE_DESKTOP] = false;
  if (cfgtmp.no_dock_shadow)
    ps->o.wintype_shadow[WINTYPE_DOCK] = false;
  if (cfgtmp.no_dnd_shadow)
    ps->o.wintype_shadow[WINTYPE_DND] = false;
  if (fading_enable)
    wintype_arr_enable(ps->o.wintype_fade);
  if (1.0 != cfgtmp.menu_opacity) {
    ps->o.wintype_opacity[WINTYPE_DROPDOWN_MENU] = cfgtmp.menu_opacity * 100;
    ps->o.wintype_opacity[WINTYPE_POPUP_MENU] = cfgtmp.menu_opacity * 100;
  }

  // Other variables determined by options

  // Determine whether we need to track focus changes
  if (ps->o.inactive_opacity || ps->o.active_opacity || ps->o.inactive_dim) {
    ps->o.track_focus = true;
  }
}

static bool
vsync_opengl_swc_init(session_t *ps) {

    if(!glx_hasglext(ps, "EXT_swap_control")) {
        printf_errf("No swap control extension, can't set the swap inteval. Expect no vsync");
        return false;
    }

    assert(ensure_glx_context(ps));

    // Get video sync functions
    if (!ps->psglx->glXSwapIntervalProc) {
        ps->psglx->glXSwapIntervalProc =
            (f_SwapIntervalEXT) glXGetProcAddress ((const GLubyte *) "glXSwapIntervalEXT");
    }
    if (!ps->psglx->glXSwapIntervalProc) {
        printf_errf("Failed to get EXT_swap_control function.");
        return false;
    }
    ps->psglx->glXSwapIntervalProc(ps->xcontext.display, glXGetCurrentDrawable(), 1);

    return true;
}

static void
vsync_opengl_swc_deinit(session_t *ps) {
  // The standard says it doesn't accept 0, but in fact it probably does
  if (glx_has_context(ps) && ps->psglx->glXSwapIntervalProc)
      ps->psglx->glXSwapIntervalProc(ps->xcontext.display, glXGetCurrentDrawable(), 0);
}

/**
 * Initialize current VSync method.
 */
bool
vsync_init(session_t *ps) {
  if (ps->o.vsync && VSYNC_FUNCS_INIT[ps->o.vsync]
      && !VSYNC_FUNCS_INIT[ps->o.vsync](ps)) {
    ps->o.vsync = VSYNC_NONE;
    return false;
  }
  else
    return true;
}

/**
 * Deinitialize current VSync method.
 */
void
vsync_deinit(session_t *ps) {
  if (ps->o.vsync && VSYNC_FUNCS_DEINIT[ps->o.vsync])
    VSYNC_FUNCS_DEINIT[ps->o.vsync](ps);
}

/**
 * Initialize X composite overlay window.
 */
static bool
init_overlay(session_t *ps) {
  ps->overlay = XCompositeGetOverlayWindow(ps->dpy, ps->root);
  if (ps->overlay) {
    // Set window region of the overlay window, code stolen from
    // compiz-0.8.8
    XserverRegion region = XFixesCreateRegion(ps->dpy, NULL, 0);
    XFixesSetWindowShapeRegion(ps->dpy, ps->overlay, ShapeBounding, 0, 0, 0);
    XFixesSetWindowShapeRegion(ps->dpy, ps->overlay, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(ps->dpy, region);

    // Listen to Expose events on the overlay
    XSelectInput(ps->dpy, ps->overlay, ExposureMask);

    // Retrieve DamageNotify on root window if we are painting on an
    // overlay
    // root_damage = XDamageCreate(ps->dpy, root, XDamageReportNonEmpty);

    // Unmap overlay, firstly. But this typically does not work because
    // the window isn't created yet.
    // XUnmapWindow(ps->dpy, ps->overlay);
    // XFlush(ps->dpy);
  }
  else {
    fprintf(stderr, "Cannot get X Composite overlay window. Falling "
        "back to painting on root window.\n");
    exit(1);
  }
#ifdef DEBUG_REDIR
  printf_dbgf("(): overlay = %#010lx\n", ps->overlay);
#endif

  return ps->overlay;
}

/**
 * Query needed X Render / OpenGL filters to check for their existence.
 */
static bool
init_filters(session_t *ps) {
  // Blur filter
  if (ps->o.blur_background) {
      if (!glx_init_blur(ps))
          return false;
  }

  return true;
}

/**
 * Redirect all windows.
 */
static void redir_start(session_t *ps) {
#ifdef DEBUG_REDIR
    print_timestamp(ps);
    printf_dbgf("(): Screen redirected.\n");
#endif

    // Map overlay window. Done firstly according to this:
    // https://bugzilla.gnome.org/show_bug.cgi?id=597014
    if (ps->overlay)
        XMapWindow(ps->dpy, ps->overlay);

    /* XCompositeRedirectSubwindows(ps->dpy, ps->root, CompositeRedirectManual); */

    /* XShapeCombineShape(ps->dpy, ps->overlay, ShapeBounding, 0, 0, 0x2800001, ShapeBounding, ShapeSubtract); */

    /*
    // Unredirect GL context window as this may have an effect on VSync:
    // < http://dri.freedesktop.org/wiki/CompositeSwap >
    XCompositeUnredirectWindow(ps->dpy, ps->reg_win, CompositeRedirectManual);
    if (ps->o.paint_on_overlay && ps->overlay) {
    XCompositeUnredirectWindow(ps->dpy, ps->overlay,
    CompositeRedirectManual);
    } */

    // Must call XSync() here
    XSync(ps->dpy, False);
}

/**
 * Get the poll time.
 */
static time_ms_t
timeout_get_poll_time(session_t *ps) {
  const time_ms_t now = get_time_ms();
  time_ms_t wait = TIME_MS_MAX;

  // Traverse throught the timeout linked list
  for (timeout_t *ptmout = ps->tmout_lst; ptmout; ptmout = ptmout->next) {
    if (ptmout->enabled) {
      time_ms_t newrun = timeout_get_newrun(ptmout);
      if (newrun <= now) {
        wait = 0;
        break;
      }
      else {
        time_ms_t newwait = newrun - now;
        if (newwait < wait)
          wait = newwait;
      }
    }
  }

  return wait;
}

/**
 * Insert a new timeout.
 */
timeout_t *
timeout_insert(session_t *ps, time_ms_t interval,
    bool (*callback)(session_t *ps, timeout_t *ptmout), void *data) {
  const static timeout_t tmout_def = {
    .enabled = true,
    .data = NULL,
    .callback = NULL,
    .firstrun = 0L,
    .lastrun = 0L,
    .interval = 0L,
  };

  const time_ms_t now = get_time_ms();
  timeout_t *ptmout = malloc(sizeof(timeout_t));
  if (!ptmout)
    printf_errfq(1, "(): Failed to allocate memory for timeout.");
  memcpy(ptmout, &tmout_def, sizeof(timeout_t));

  ptmout->interval = interval;
  ptmout->firstrun = now;
  ptmout->lastrun = now;
  ptmout->data = data;
  ptmout->callback = callback;
  ptmout->next = ps->tmout_lst;
  ps->tmout_lst = ptmout;

  return ptmout;
}

/**
 * Drop a timeout.
 *
 * @return true if we have found the timeout and removed it, false
 *         otherwise
 */
bool
timeout_drop(session_t *ps, timeout_t *prm) {
  timeout_t **pplast = &ps->tmout_lst;

  for (timeout_t *ptmout = ps->tmout_lst; ptmout;
      pplast = &ptmout->next, ptmout = ptmout->next) {
    if (prm == ptmout) {
      *pplast = ptmout->next;
      free(ptmout);

      return true;
    }
  }

  return false;
}

/**
 * Clear all timeouts.
 */
static void
timeout_clear(session_t *ps) {
  timeout_t *ptmout = ps->tmout_lst;
  timeout_t *next = NULL;
  while (ptmout) {
    next = ptmout->next;
    free(ptmout);
    ptmout = next;
  }
}

/**
 * Run timeouts.
 *
 * @return true if we have ran a timeout, false otherwise
 */
static bool
timeout_run(session_t *ps) {
  const time_ms_t now = get_time_ms();
  bool ret = false;
  timeout_t *pnext = NULL;

  for (timeout_t *ptmout = ps->tmout_lst; ptmout; ptmout = pnext) {
    pnext = ptmout->next;
    if (ptmout->enabled) {
      const time_ms_t max = now +
        (time_ms_t) (ptmout->interval * TIMEOUT_RUN_TOLERANCE);
      time_ms_t newrun = timeout_get_newrun(ptmout);
      if (newrun <= max) {
        ret = true;
        timeout_invoke(ps, ptmout);
      }
    }
  }

  return ret;
}

/**
 * Invoke a timeout.
 */
void
timeout_invoke(session_t *ps, timeout_t *ptmout) {
  const time_ms_t now = get_time_ms();
  ptmout->lastrun = now;
  // Avoid modifying the timeout structure after running timeout, to
  // make it possible to remove timeout in callback
  if (ptmout->callback)
    ptmout->callback(ps, ptmout);
}

/**
 * Reset a timeout to initial state.
 */
void
timeout_reset(session_t *ps, timeout_t *ptmout) {
  ptmout->firstrun = ptmout->lastrun = get_time_ms();
}

/**
 * Unredirect all windows.
 */
static void redir_stop(session_t *ps) {
#ifdef DEBUG_REDIR
    print_timestamp(ps);
    printf_dbgf("(): Screen unredirected.\n");
#endif
    XCompositeUnredirectSubwindows(ps->dpy, ps->root, CompositeRedirectManual);
    // Unmap overlay window
    if (ps->overlay)
        XUnmapWindow(ps->dpy, ps->overlay);

    // Must call XSync() here
    XSync(ps->dpy, False);
}

/**
 * Main loop.
 */
static bool
mainloop(session_t *ps) {
  // Don't miss timeouts even when we have a LOT of other events!
  timeout_run(ps);

  // Process existing events
  // Sometimes poll() returns 1 but no events are actually read,
  // causing XNextEvent() to block, I have no idea what's wrong, so we
  // check for the number of events here.
  while(XEventsQueued(ps->dpy, QueuedAfterReading)) {
    XEvent ev = { };

    XNextEvent(ps->dpy, &ev);
    ev_handle(ps, &ev);

    ps->skip_poll = true;

    return true;
  }

  /* return false; */

#ifdef CONFIG_DBUS
  if (ps->o.dbus) {
    cdbus_loop(ps);
  }
#endif

  if (ps->reset)
    return false;

  // Calculate timeout
  struct timeval *ptv = NULL;
  {
    // Consider skip_poll firstly
    if (ps->skip_poll || ps->o.benchmark) {
      ptv = malloc(sizeof(struct timeval));
      ptv->tv_sec = 0L;
      ptv->tv_usec = 0L;
    }

    // Don't continue looping for 0 timeout
    if (ptv && timeval_isempty(ptv)) {
      free(ptv);
      return false;
    }

    // Now consider the waiting time of other timeouts
    time_ms_t tmout_ms = timeout_get_poll_time(ps);
    if (tmout_ms < TIME_MS_MAX) {
      if (!ptv) {
        ptv = malloc(sizeof(struct timeval));
        *ptv = ms_to_tv(tmout_ms);
      }
      else if (timeval_ms_cmp(ptv, tmout_ms) > 0) {
        *ptv = ms_to_tv(tmout_ms);
      }
    }

    // Don't continue looping for 0 timeout
    if (ptv && timeval_isempty(ptv)) {
      free(ptv);
      return false;
    }
  }

  // Polling
  fds_poll(ps, ptv);
  free(ptv);
  ptv = NULL;

  return true;
}

static void
cxinerama_upd_scrs(session_t *ps) {
  free_xinerama_info(ps);

  if (!ps->o.xinerama_shadow_crop) return;

  if (!XineramaIsActive(ps->dpy)) return;

  ps->xinerama_scrs = XineramaQueryScreens(ps->dpy, &ps->xinerama_nscrs);

  // Just in case the shit hits the fan...
  if (!ps->xinerama_nscrs) {
    cxfree(ps->xinerama_scrs);
    ps->xinerama_scrs = NULL;
    return;
  }

  ps->xinerama_scr_regs = allocchk(malloc(sizeof(XserverRegion *)
        * ps->xinerama_nscrs));
  for (int i = 0; i < ps->xinerama_nscrs; ++i) {
    const XineramaScreenInfo * const s = &ps->xinerama_scrs[i];
    XRectangle r = { .x = s->x_org, .y = s->y_org,
      .width = s->width, .height = s->height };
    ps->xinerama_scr_regs[i] = XFixesCreateRegion(ps->dpy, &r, 1);
  }
}

/**
 * Initialize a session.
 *
 * @param ps_old old session, from which the function will take the X
 *    connection, then free it
 * @param argc number of commandline arguments
 * @param argv commandline arguments
 */
session_t * session_init(session_t *ps_old, int argc, char **argv) {
  const static session_t s_def = {
    .dpy = NULL,
    .scr = 0,
    .vis = NULL,
    .depth = 0,
    .root = None,
    .root_size = {{0}},
    // .root_damage = None,
    .overlay = None,
    .reg_win = None,
    .o = {
      .config_file = NULL,
      .display = NULL,
      .mark_wmwin_focused = false,
      .fork_after_register = false,
      .synchronize = false,
      .blur_level = 0,
      .stoppaint_force = UNSET,
      .dbus = false,
      .benchmark = 0,
      .benchmark_wid = None,
      .logpath = NULL,

      .vsync = VSYNC_NONE,

      .wintype_shadow = { false },
      .shadow_blacklist = NULL,
      .respect_prop_shadow = false,
      .xinerama_shadow_crop = false,

      .wintype_fade = { false },
      .no_fading_openclose = false,
      .no_fading_destroyed_argb = false,
      .fade_blacklist = NULL,

      .wintype_opacity = { -1.0 },
      .inactive_opacity = 100.0,
      .inactive_opacity_override = false,
      .active_opacity = 100.0,
      .opacity_fade_time = 1000.0,
      .detect_client_opacity = false,

      .blur_background = false,
      .blur_background_blacklist = NULL,
      .inactive_dim = 100.0,
      .inactive_dim_fixed = false,
      .dim_fade_time = 1000.0,
      .invert_color_list = NULL,
      .opacity_rules = NULL,

      .wintype_focus = { false },
      .focus_blacklist = NULL,

      .track_focus = false,
      .track_wdata = false,
    },

    .pfds_read = NULL,
    .pfds_write = NULL,
    .pfds_except = NULL,
    .nfds_max = 0,
    .tmout_lst = NULL,

    .time_start = { 0, 0 },
    .idling = false,
    .ignore_head = NULL,
    .ignore_tail = NULL,
    .reset = false,

    .win_list = {0},
    .active_win = NULL,

#ifdef CONFIG_DBUS
    .dbus_conn = NULL,
    .dbus_service = NULL,
#endif
  };

  // Allocate a session and copy default values into it
  session_t *ps = malloc(sizeof(session_t));
  memcpy(ps, &s_def, sizeof(session_t));
  ps_g = ps;
  ps->ignore_tail = &ps->ignore_head;
  gettimeofday(&ps->time_start, NULL);

  wintype_arr_enable(ps->o.wintype_focus);
  ps->o.wintype_focus[WINTYPE_UNKNOWN] = false;
  ps->o.wintype_focus[WINTYPE_NORMAL] = false;
  ps->o.wintype_focus[WINTYPE_UTILITY] = false;

  // First pass
  get_cfg(ps, argc, argv, true);

  swiss_clearComponentSizes(&ps->win_list);
  swiss_enableAllAutoRemove(&ps->win_list);
  swiss_setComponentSize(&ps->win_list, COMPONENT_MUD, sizeof(struct _win));
  swiss_setComponentSize(&ps->win_list, COMPONENT_PHYSICAL, sizeof(struct PhysicalComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_Z, sizeof(struct ZComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_TEXTURED, sizeof(struct TexturedComponent));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_TEXTURED);
  swiss_setComponentSize(&ps->win_list, COMPONENT_BINDS_TEXTURE, sizeof(struct BindsTextureComponent));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_BINDS_TEXTURE);
  swiss_setComponentSize(&ps->win_list, COMPONENT_MAP, sizeof(struct MapComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_MOVE, sizeof(struct MoveComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_RESIZE, sizeof(struct ResizeComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_TRACKS_WINDOW, sizeof(struct TracksWindowComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_HAS_CLIENT, sizeof(struct HasClientComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_FOCUS_CHANGE, sizeof(struct FocusChangedComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_TINT, sizeof(struct TintComponent));

  swiss_setComponentSize(&ps->win_list, COMPONENT_OPACITY, sizeof(struct OpacityComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_FADES_OPACITY, sizeof(struct FadesOpacityComponent));

  swiss_setComponentSize(&ps->win_list, COMPONENT_DIM, sizeof(struct DimComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_FADES_DIM, sizeof(struct FadesDimComponent));

  swiss_setComponentSize(&ps->win_list, COMPONENT_SHADOW, sizeof(struct glx_shadow_cache));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_SHADOW);
  swiss_setComponentSize(&ps->win_list, COMPONENT_BLUR, sizeof(struct glx_blur_cache));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_BLUR);
  swiss_setComponentSize(&ps->win_list, COMPONENT_WINTYPE_CHANGE, sizeof(struct WintypeChangedComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_SHAPED, sizeof(struct ShapedComponent));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_SHAPED);
  swiss_setComponentSize(&ps->win_list, COMPONENT_SHAPE_DAMAGED, sizeof(struct ShapeDamagedEvent));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_SHAPE_DAMAGED);
  swiss_setComponentSize(&ps->win_list, COMPONENT_STATEFUL, sizeof(struct StatefulComponent));

  swiss_setComponentSize(&ps->win_list, COMPONENT_DEBUGGED, sizeof(struct DebuggedComponent));
  swiss_init(&ps->win_list, 512);

  vector_init(&ps->order, sizeof(win_id), 512);

  // Inherit old Display if possible, primarily for resource leak checking
  if (ps_old && ps_old->dpy)
    ps->dpy = ps_old->dpy;

  // Open Display
  if (!ps->dpy) {
    ps->dpy = XOpenDisplay(ps->o.display);
    if (!ps->dpy) {
      printf_errfq(1, "(): Can't open display.");
    }
  }

  XSetErrorHandler(xerror);
  if (ps->o.synchronize) {
XSynchronize(ps->dpy, 1);
}

  ps->scr = DefaultScreen(ps->dpy);
  ps->root = RootWindow(ps->dpy, ps->scr);

  ps->vis = DefaultVisual(ps->dpy, ps->scr);
  ps->depth = DefaultDepth(ps->dpy, ps->scr);

  // Start listening to events on root earlier to catch all possible root
  // geometry changes
  XSelectInput(ps->dpy, ps->root,
    SubstructureNotifyMask
    | ExposureMask
    | StructureNotifyMask
    | PropertyChangeMask);

  XFlush(ps->dpy);

  //Fetch the root region to set shape later
  ps->root_region = XFixesCreateRegionFromWindow(ps->dpy, ps->root, ShapeInput);

  ps->root_size = (Vector2) {{
      DisplayWidth(ps->dpy, ps->scr), DisplayHeight(ps->dpy, ps->scr)
  }};

  // Build a safe representation of display name
  {
    char *display_repr = DisplayString(ps->dpy);
    if (!display_repr)
      display_repr = "unknown";
    display_repr = mstrcpy(display_repr);

    // Convert all special characters in display_repr name to underscore
    {
      char *pdisp = display_repr;

      while (*pdisp) {
        if (!isalnum(*pdisp))
          *pdisp = '_';
        ++pdisp;
      }
    }

    ps->o.display_repr = display_repr;
  }

  if(!xorgContext_init(&ps->xcontext, ps->dpy, ps->scr)) {
    printf_errf("Failed initializing the xorg context");
    exit(1);
  }

  atoms_init(&ps->atoms, &ps->xcontext);

  // Second pass
  get_cfg(ps, argc, argv, false);

  // Overlay must be initialized before double buffer, and before creation
  // of OpenGL context.
  init_overlay(ps);

  add_shader_type(&global_info);
  add_shader_type(&downsample_info);
  add_shader_type(&upsample_info);
  add_shader_type(&passthough_info);
  add_shader_type(&profiler_info);
  add_shader_type(&text_info);
  add_shader_type(&shadow_info);
  add_shader_type(&stencil_info);
  add_shader_type(&colored_info);

  assets_add_handler(struct shader, "vs", vert_shader_load_file, shader_unload_file);
  assets_add_handler(struct shader, "fs", frag_shader_load_file, shader_unload_file);
  assets_add_handler(struct face, "face", face_load_file, face_unload_file);
  assets_add_handler(struct shader_program, "shader", shader_program_load_file,
      shader_program_unload_file);

  assets_add_path("./assets/");
  add_xdg_asset_paths();

  // Initialize OpenGL as early as possible
  if (!glx_init(ps, true))
    exit(1);

  if(xorgContext_capabilities(&ps->capabilities, &ps->xcontext) != 0) {
      printf_errf("Failed getting xorg capabilities");
      exit(1);
  }

  if(xorgContext_ensure_capabilities(&ps->capabilities)) {
      printf_errf("One of the required X extensions were missing");
      exit(1);
  }

  // Monitor screen changes if vsync_sw is enabled and we are using
  // an auto-detected refresh rate, or when Xinerama features are enabled
  if (ps->o.xinerama_shadow_crop)
    XRRSelectInput(ps->dpy, ps->root, RRScreenChangeNotifyMask);

  // Initialize VSync
  if (!vsync_init(ps))
    exit(1);

  cxinerama_upd_scrs(ps);

  // Create registration window
  if (!ps->reg_win && !register_cm(ps))
    exit(1);

  text_debug_load("fonts/Roboto-Light.ttf");

  bezier_init(&ps->curve, 0.4, 0.0, 0.2, 1);

  // Initialize filters, must be preceded by OpenGL context creation
  if (!init_filters(ps))
    exit(1);

  fds_insert(ps, ConnectionNumber(ps->dpy), POLLIN);

  XGrabServer(ps->dpy);

  xtexture_init(&ps->root_texture, &ps->xcontext);
  get_root_tile(ps);

  redir_start(ps);

  {
    Window root_return, parent_return;
    Window *children;
    unsigned int nchildren;

    XQueryTree(ps->dpy, ps->root, &root_return,
      &parent_return, &children, &nchildren);

    for (unsigned i = 0; i < nchildren; i++) {
      add_win(ps, children[i]);
    }

    cxfree(children);
  }

  if (ps->o.track_focus) {
      update_ewmh_active_win(ps);
  }

  XUngrabServer(ps->dpy);
  // ALWAYS flush after XUngrabServer()!
  XFlush(ps->dpy);

  // Initialize DBus
  if (ps->o.dbus) {
#ifdef CONFIG_DBUS
    cdbus_init(ps);
    if (!ps->dbus_conn) {
      cdbus_destroy(ps);
      ps->o.dbus = false;
    }
#else
    printf_errfq(1, "(): DBus support not compiled in!");
#endif
  }

  // Fork to background, if asked
  if (ps->o.fork_after_register) {
    if (!fork_after(ps)) {
      session_destroy(ps);
      return NULL;
    }
  }

  // Redirect output stream
  if (ps->o.fork_after_register || ps->o.logpath)
    ostream_reopen(ps, NULL);

  write_pid(ps);

  // Free the old session
  if (ps_old)
    free(ps_old);

  return ps;
}

/**
 * Destroy a session.
 *
 * Does not close the X connection or free the <code>session_t</code>
 * structure, though.
 *
 * @param ps session to destroy
 */
void session_destroy(session_t *ps) {
  redir_stop(ps);

  // Stop listening to events on root window
  XSelectInput(ps->dpy, ps->root, 0);

#ifdef CONFIG_DBUS
  // Kill DBus connection
  if (ps->o.dbus)
    cdbus_destroy(ps);

  free(ps->dbus_service);
#endif

  text_debug_unload();

  // Free window linked list
  for_components(it, &ps->win_list,
      COMPONENT_MUD, COMPONENT_STATEFUL, CQ_END) {
      win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, it.id);
      struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

      if (win_mapped(&ps->win_list, it.id) && stateful->state != STATE_DESTROYING)
          win_ev_stop(ps, w);

  }
  for_components(it, &ps->win_list,
      COMPONENT_TEXTURED, CQ_END) {
      struct TexturedComponent* textured = swiss_getComponent(&ps->win_list, COMPONENT_TEXTURED, it.id);
      texture_delete(&textured->texture);
      renderbuffer_delete(&textured->stencil);
  }
  swiss_resetComponent(&ps->win_list, COMPONENT_TEXTURED);
  for_components(it, &ps->win_list,
      COMPONENT_BINDS_TEXTURE, CQ_END) {
      struct BindsTextureComponent* bindsTexture = swiss_getComponent(&ps->win_list, COMPONENT_BINDS_TEXTURE, it.id);
      wd_delete(&bindsTexture->drawable);
  }
  swiss_resetComponent(&ps->win_list, COMPONENT_BINDS_TEXTURE);
  for_components(it, &ps->win_list,
      COMPONENT_SHADOW, CQ_END) {
      struct glx_shadow_cache* shadow = swiss_getComponent(&ps->win_list, COMPONENT_SHADOW, it.id);
      shadow_cache_delete(shadow);
  }
  swiss_resetComponent(&ps->win_list, COMPONENT_SHADOW);
  for_components(it, &ps->win_list,
      COMPONENT_BLUR, CQ_END) {
      struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, it.id);
      blur_cache_delete(blur);
  }
  swiss_resetComponent(&ps->win_list, COMPONENT_BLUR);
  for_components(it, &ps->win_list,
      COMPONENT_SHAPED, CQ_END) {
      struct ShapedComponent* shaped = swiss_getComponent(&ps->win_list, COMPONENT_SHAPED, it.id);
      face_unload_file(shaped->face);
  }
  swiss_resetComponent(&ps->win_list, COMPONENT_SHAPED);
  for_components(it, &ps->win_list,
      COMPONENT_SHAPE_DAMAGED, CQ_END) {
      struct ShapeDamagedEvent* damaged = swiss_getComponent(&ps->win_list, COMPONENT_SHAPE_DAMAGED, it.id);
      vector_kill(&damaged->rects);
  }
  swiss_resetComponent(&ps->win_list, COMPONENT_SHAPE_DAMAGED);

#ifdef CONFIG_C2
  // Free blacklists
  free_wincondlst(&ps->o.shadow_blacklist);
  free_wincondlst(&ps->o.fade_blacklist);
  free_wincondlst(&ps->o.focus_blacklist);
  free_wincondlst(&ps->o.invert_color_list);
  free_wincondlst(&ps->o.blur_background_blacklist);
  free_wincondlst(&ps->o.opacity_rules);
  free_wincondlst(&ps->o.paint_blacklist);
#endif

  // Free tracked atom list
  atoms_kill(&ps->atoms);

  // Free ignore linked list
  {
    ignore_t *next = NULL;
    for (ignore_t *ign = ps->ignore_head; ign; ign = next) {
      next = ign->next;

      free(ign);
    }

    // Reset head and tail
    ps->ignore_head = NULL;
    ps->ignore_tail = &ps->ignore_head;
  }

  xtexture_delete(&ps->root_texture);

  free(ps->o.config_file);
  free(ps->o.write_pid_path);
  free(ps->o.display);
  free(ps->o.display_repr);
  free(ps->o.logpath);
  free(ps->pfds_read);
  free(ps->pfds_write);
  free(ps->pfds_except);
  free_xinerama_info(ps);

  xorgContext_delete(&ps->xcontext);

  glx_destroy(ps);

  swiss_kill(&ps->win_list);

  // Release overlay window
  if (ps->overlay) {
    XCompositeReleaseOverlayWindow(ps->dpy, ps->overlay);
    ps->overlay = None;
  }

  // Free reg_win
  if (ps->reg_win) {
    XDestroyWindow(ps->dpy, ps->reg_win);
    ps->reg_win = None;
  }

  // Flush all events
  XSync(ps->dpy, True);

#ifdef DEBUG_XRC
  // Report about resource leakage
  xrc_report_xid();
#endif

  // Free timeouts
  timeout_clear(ps);

  if (ps == ps_g)
    ps_g = NULL;
}

void calculate_window_opacity(session_t* ps, Swiss* em) {
    for_components(it, &ps->win_list,
        COMPONENT_MUD, COMPONENT_FOCUS_CHANGE, COMPONENT_STATEFUL, CQ_END) {
        win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

        // Try obeying window type opacity
        if(ps->o.wintype_opacity[w->window_type] != -1.0) {
            f->newOpacity = ps->o.wintype_opacity[w->window_type];
            f->newDim = 100.0;
            continue;
        }

        if(stateful->state != STATE_INVISIBLE && stateful->state != STATE_HIDING && stateful->state != STATE_DESTROYING) {
            void* val;
            if (c2_matchd(ps, w, ps->o.opacity_rules, NULL, &val)) {
                f->newOpacity = (double)(long)val;
                f->newDim = 100.0;
                continue;
            }
        }

        f->newOpacity = 100.0;
        f->newDim = 100.0;

        // Respect inactive_opacity in some cases
        if (stateful->state == STATE_DEACTIVATING || stateful->state == STATE_INACTIVE) {
            f->newOpacity = ps->o.inactive_opacity;
            f->newDim = ps->o.inactive_dim;
            continue;
        }

        // Respect active_opacity only when the window is physically focused
        if (ps->o.active_opacity && ps->active_win == w) {
            f->newOpacity = ps->o.active_opacity;
            f->newDim = 100.0;
        }
    }
}

// @CLEANUP: This shouldn't be here
bool do_win_fade(struct Bezier* curve, double dt, Swiss* em) {
    bool skip_poll = false;

    Vector fadeable;
    vector_init(&fadeable, sizeof(struct Fading*), 64);

    // Collect everything fadeable
    for_components(it, em,
        COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        struct Fading* fade = &fo->fade;
        vector_putBack(&fadeable, &fade);
    }
    for_components(it, em,
        COMPONENT_FADES_DIM, CQ_END) {
        struct FadesDimComponent* fo = swiss_getComponent(em, COMPONENT_FADES_DIM, it.id);
        struct Fading* fade = &fo->fade;
        vector_putBack(&fadeable, &fade);
    }

    // Actually fade them
    size_t index = 0;
    struct Fading** fade_ptr = vector_getFirst(&fadeable, &index);
    while(fade_ptr != NULL) {
        struct Fading* fade = *fade_ptr;
        fade->value = fade->keyframes[fade->head].target;

        if(!fade_done(fade)) {
            // @CLEANUP: Maybe a while loop?
            for(size_t i = fade->head; i != fade->tail; ) {
                // Increment before the body to skip head and process tail
                i = (i+1) % FADE_KEYFRAMES;

                struct FadeKeyframe* keyframe = &fade->keyframes[i];
                if(!keyframe->ignore){
                    keyframe->time += dt;
                } else {
                    keyframe->ignore = false;
                }

                double x = keyframe->time / keyframe->duration;
                if(x >= 1.0) {
                    // We're done, clean out the time and set this as the head
                    keyframe->time = 0.0;
                    keyframe->duration = -1;
                    fade->head = i;

                    // Force the value. We are still going to blend it with stuff
                    // on top of this
                    fade->value = keyframe->target;
                } else {
                    double t = bezier_getSplineValue(curve, x);
                    fade->value = lerp(fade->value, keyframe->target, t);
                }
            }

            // We had a least one fade that did something
            skip_poll = true;
        }

        fade_ptr = vector_getNext(&fadeable, &index);
    }

    vector_kill(&fadeable);
    return skip_poll;
}

void update_window_textures(Swiss* em, struct X11Context* xcontext, struct Framebuffer* fbo) {
    static const enum ComponentType req_types[] = {
        COMPONENT_BINDS_TEXTURE,
        COMPONENT_TEXTURED,
        COMPONENT_CONTENTS_DAMAGED,
        CQ_END
    };
    struct SwissIterator it = {0};
    swiss_getFirst(em, req_types, &it);
    if(it.done)
        return;

    framebuffer_resetTarget(fbo);
    framebuffer_bind(fbo);

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    struct shader_program* program = assets_load("stencil.shader");
    if(program->shader_type_info != &stencil_info) {
        printf_errf("Shader was not a stencil shader\n");
        return;
    }
    struct Stencil* shader_type = program->shader_type;

    shader_set_future_uniform_sampler(shader_type->tex_scr, 0);

    shader_use(program);

    // @RESEARCH: According to the spec (https://www.khronos.org/registry/OpenGL/extensions/EXT/GLX_EXT_texture_from_pixmap.txt)
    // we should always grab the server before binding glx textures, and keep
    // server until we are done with the textures. Experiments show that it
    // completely kills rendering performance for chrome and electron.
    // - Delusional 19/08-2018
    XGrabServer(xcontext->display);
    glXWaitX();

    while(!it.done) {
        struct ShapedComponent* shaped = swiss_getComponent(em, COMPONENT_SHAPED, it.id);
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it.id);

        XSyncFence fence = XSyncCreateFence(xcontext->display, bindsTexture->drawable.wid, false);
        XSyncTriggerFence(xcontext->display, fence);
        XSyncAwaitFence(xcontext->display, &fence, 1);
        XSyncDestroyFence(xcontext->display, fence);

        if(!wd_bind(&bindsTexture->drawable)) {
            // If we fail to bind we just assume that the window must have been
            // closed and keep the old texture
            printf_err("Failed binding drawable for %zu", it.id);
            swiss_getNext(em, &it);
            continue;
        }

        framebuffer_resetTarget(fbo);
        framebuffer_targetTexture(fbo, &textured->texture);
        framebuffer_targetRenderBuffer_stencil(fbo, &textured->stencil);
        framebuffer_rebind(fbo);

        Vector2 offset = textured->texture.size;
        vec2_sub(&offset, &bindsTexture->drawable.texture.size);

        Matrix old_view = view;
        view = mat4_orthogonal(0, textured->texture.size.x, 0, textured->texture.size.y, -1, 1);
        glViewport(0, 0, textured->texture.size.x, textured->texture.size.y);

        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        assert(bindsTexture->drawable.bound);
        texture_bind(&bindsTexture->drawable.texture, GL_TEXTURE0);

        shader_set_uniform_bool(shader_type->flip, bindsTexture->drawable.texture.flipped);

        draw_rect(shaped->face, shader_type->mvp, (Vector3){{0, offset.y, 0}}, bindsTexture->drawable.texture.size);

        view = old_view;

        wd_unbind(&bindsTexture->drawable);

        swiss_getNext(em, &it);
    }

    XUngrabServer(xcontext->display);
    glXWaitX();
}

static void commit_opacity_change(Swiss* em, double fade_time) {
    for_components(it, em,
            COMPONENT_UNMAP, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fadesOpacity = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);

        fade_keyframe(&fadesOpacity->fade, 0, fade_time);
    }
}

static void damage_blur_over_fade(Swiss* em) {
    Vector order;
    vector_init(&order, sizeof(uint64_t), em->size);

    for_components(it, em,
            COMPONENT_MUD, CQ_END) {
        vector_putBack(&order, &it.id);
    }
    vector_qsort(&order, window_zcmp, em);

    // @HACK @IMPROVEMENT: This should rather be done with a (dynamically
    // sized) bitfield. We can extract it from the swiss datastructure, which
    // uses a bunch of bitfields. - Jesper Jensen 06/10-2018
    bool* changes = calloc(sizeof(bool) * em->capacity, 1);
    size_t uniqueChanged = 0;

    for_components(it, em, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        if(!fade_done(&fo->fade)) {
            uniqueChanged += changes[it.id] ? 0 : 1;
            changes[it.id] = true;
        }
    }
    for_components(it, em, COMPONENT_FADES_DIM, CQ_END) {
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_DIM, it.id);
        if(!fade_done(&fo->fade)) {
            uniqueChanged += changes[it.id] ? 0 : 1;
            changes[it.id] = true;
        }
    }

    struct ChangeRecord {
        size_t order_slot;
        size_t id;
    };

    struct ChangeRecord* order_slots = malloc(sizeof(struct ChangeRecord) * uniqueChanged);
    {
        size_t nextSlot = 0;
        for(size_t i = 0; i < em->capacity; i++) {
            if(!changes[i])
                continue;

            order_slots[nextSlot] = (struct ChangeRecord){
                .order_slot = vector_find_uint64(&order, i),
                .id = i,
            };
            nextSlot++;
        }
        assert(nextSlot == uniqueChanged);
    }
    free(changes);

    {
        for(size_t i = 0; i < uniqueChanged; i++) {
            struct ChangeRecord* change = &order_slots[i];
            assert(change->order_slot >= 0);

            size_t index = change->order_slot;
            win_id* other_id = vector_getPrev(&order, &index);
            while(other_id != NULL) {
                if(win_overlap(em, change->id, *other_id)) {
                    swiss_ensureComponent(em, COMPONENT_BLUR_DAMAGED, *other_id);
                }

                other_id = vector_getPrev(&order, &index);
            }
        }
    }
    free(order_slots);

    vector_kill(&order);
}

static void finish_destroyed_windows(Swiss* em, session_t* ps) {
    for_components(it, em, COMPONENT_STATEFUL, COMPONENT_SHADOW, CQ_END) {
        struct glx_shadow_cache* shadow = swiss_getComponent(em, COMPONENT_SHADOW, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED || stateful->state == STATE_INVISIBLE) {
            shadow_cache_delete(shadow);
            swiss_removeComponent(em, COMPONENT_SHADOW, it.id);
        }
    }

    for_components(it, em, COMPONENT_STATEFUL, COMPONENT_BLUR, CQ_END) {
        struct glx_blur_cache* blur = swiss_getComponent(em, COMPONENT_BLUR, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED || stateful->state == STATE_INVISIBLE) {
            blur_cache_delete(blur);
            swiss_removeComponent(em, COMPONENT_BLUR, it.id);
        }
    }

    for_components(it, em, COMPONENT_STATEFUL, COMPONENT_TINT, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED || stateful->state == STATE_INVISIBLE) {
            swiss_removeComponent(em, COMPONENT_TINT, it.id);
        }
    }

    // Destroy shaped components of destroyed windows
    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_SHAPED, CQ_END) {
        struct ShapedComponent* shaped = swiss_getComponent(em, COMPONENT_SHAPED, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED) {
            if(shaped->face != NULL)
                face_unload_file(shaped->face);
            swiss_removeComponent(em, COMPONENT_SHAPED, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_STATEFUL, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED) {
            finish_destroy_win(ps, it.id);
        }
    }
}

static void transition_faded_entities(Swiss* em) {
    // Update state when fading complete
    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        if(fade_done(&fo->fade)) {
            if(stateful->state == STATE_ACTIVATING) {
                stateful->state = STATE_ACTIVE;
            } else if(stateful->state == STATE_DEACTIVATING) {
                stateful->state = STATE_INACTIVE;
            } else if(stateful->state == STATE_HIDING) {
                stateful->state = STATE_INVISIBLE;
            } else if(stateful->state == STATE_DESTROYING) {
                stateful->state = STATE_DESTROYED;
                // @RESEARCH: Are we leaking memory from the cache/shadow here?
            }
        }
    }
}

static void remove_texture_invis_windows(Swiss* em) {
    for_components(it, em, COMPONENT_STATEFUL, COMPONENT_TEXTURED, CQ_END) {
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_INVISIBLE || stateful->state == STATE_DESTROYED) {
            texture_delete(&textured->texture);
            renderbuffer_delete(&textured->stencil);
            swiss_removeComponent(em, COMPONENT_TEXTURED, it.id);
        }
    }
}

static void syncronize_fade_opacity(Swiss* em) {
    for_components(it, em,
            COMPONENT_FADES_OPACITY, CQ_END) {
    struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
    if(fo->fade.value < 100.0) {
            swiss_ensureComponent(em, COMPONENT_OPACITY, it.id);
        }
    }
    for_components(it, em,
            COMPONENT_OPACITY, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        struct OpacityComponent* opacity = swiss_getComponent(em, COMPONENT_OPACITY, it.id);

        opacity->opacity = fo->fade.value;
    }
    for_components(it, em,
            COMPONENT_OPACITY, CQ_END) {
        struct OpacityComponent* opacity = swiss_getComponent(em, COMPONENT_OPACITY, it.id);
        if(opacity->opacity >= 100.0) {
            swiss_removeComponent(em, COMPONENT_OPACITY, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_DIM, COMPONENT_FADES_DIM, CQ_END) {
        struct FadesDimComponent* fo = swiss_getComponent(em, COMPONENT_FADES_DIM, it.id);
        struct DimComponent* dim = swiss_getComponent(em, COMPONENT_DIM, it.id);

        dim->dim = fo->fade.value;
    }
}

static void update_focused_state(Swiss* em, session_t* ps) {
    for_components(it, em,
            COMPONENT_MUD, COMPONENT_STATEFUL, CQ_END) {
        win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        // We are don't track focus from these states
        if(stateful->state == STATE_HIDING || stateful->state == STATE_INVISIBLE
                || stateful->state == STATE_DESTROYING || stateful->state == STATE_DESTROYED) {
            continue;
        }

        enum WindowState newState;

        newState = STATE_DEACTIVATING;

        if(ps->o.wintype_focus[w->window_type])
            newState = STATE_ACTIVATING;
        else if(ps->o.mark_wmwin_focused && w->wmwin)
            newState = STATE_ACTIVATING;
        else if(ps->active_win == w)
            newState = STATE_ACTIVATING;
        else if(win_mapped(em, it.id) && win_match(ps, w, ps->o.focus_blacklist)) {
            newState = STATE_ACTIVATING;
        }

        // If a window is inactive and we are deactivating, then there's no change
        if(newState == STATE_DEACTIVATING && stateful->state == STATE_INACTIVE)
            continue;
        // Likewise for active
        if(newState == STATE_ACTIVATING && stateful->state == STATE_ACTIVE)
            continue;

        if(newState != stateful->state) {
            stateful->state = newState;
            swiss_ensureComponent(&ps->win_list, COMPONENT_FOCUS_CHANGE, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_STATEFUL, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);
        swiss_ensureComponent(em, COMPONENT_DEBUGGED, it.id);
        if(stateful->state == STATE_ACTIVATING) {
            swiss_ensureComponent(em, COMPONENT_DEBUGGED, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_STATEFUL, COMPONENT_DEBUGGED, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);
        if(stateful->state == STATE_DEACTIVATING || stateful->state == STATE_HIDING) {
            swiss_removeComponent(em, COMPONENT_DEBUGGED, it.id);
        }
    }
}

static void start_focus_fade(Swiss* em, double fade_time, double dim_fade_time) {
    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        fade_keyframe(&fo->fade, f->newOpacity, fade_time);
    }
    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_FADES_DIM, CQ_END) {
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct FadesDimComponent* fo = swiss_getComponent(em, COMPONENT_FADES_DIM, it.id);
        fade_keyframe(&fo->fade, f->newDim, dim_fade_time);
    }
}

static void commit_resize(Swiss* em) {
    for_components(it, em,
            COMPONENT_RESIZE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_CONTENTS_DAMAGED, it.id);
    }

    for_components(it, em,
            COMPONENT_RESIZE, COMPONENT_SHADOW, COMPONENT_CONTENTS_DAMAGED, CQ_END) {
        struct ResizeComponent* resize = swiss_getComponent(em, COMPONENT_RESIZE, it.id);
        struct glx_shadow_cache* shadow = swiss_getComponent(em, COMPONENT_SHADOW, it.id);

        shadow_cache_resize(shadow, &resize->newSize);
        swiss_ensureComponent(em, COMPONENT_SHADOW_DAMAGED, it.id);
    }

    for_components(it, em,
            COMPONENT_RESIZE, COMPONENT_BLUR, COMPONENT_CONTENTS_DAMAGED, CQ_END) {
        struct ResizeComponent* resize = swiss_getComponent(em, COMPONENT_RESIZE, it.id);
        struct glx_blur_cache* blur = swiss_getComponent(em, COMPONENT_BLUR, it.id);


        if(!blur_cache_resize(blur, &resize->newSize)) {
            printf_errf("Failed resizing window blur");
        }
        swiss_ensureComponent(em, COMPONENT_BLUR_DAMAGED, it.id);
    }

    for_components(it, em,
            COMPONENT_RESIZE, COMPONENT_TEXTURED, COMPONENT_CONTENTS_DAMAGED, CQ_END) {
        struct ResizeComponent* resize = swiss_getComponent(em, COMPONENT_RESIZE, it.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it.id);

        texture_resize(&textured->texture, &resize->newSize);
        renderbuffer_resize(&textured->stencil, &resize->newSize);
    }

    for_components(it, em,
            COMPONENT_RESIZE, COMPONENT_PHYSICAL, COMPONENT_MUD, COMPONENT_CONTENTS_DAMAGED, CQ_END) {
        struct ResizeComponent* resize = swiss_getComponent(em, COMPONENT_RESIZE, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        physical->size = resize->newSize;
    }
}

static void commit_reshape(Swiss* em, struct X11Context* context) {
    for_components(it, em,
            COMPONENT_SHAPED, COMPONENT_SHAPE_DAMAGED, CQ_END) {
        struct ShapedComponent* shaped = swiss_getComponent(em, COMPONENT_SHAPED, it.id);
        struct ShapeDamagedEvent* shapeDamaged = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);

        if(shaped->face != NULL)
            face_unload_file(shaped->face);

        struct face* face = malloc(sizeof(struct face));
        // Triangulate the rectangles into a triangle vertex stream
        face_init_rects(face, &shapeDamaged->rects);
        vector_kill(&shapeDamaged->rects);
        face_upload(face);

        shaped->face = face;
    }
}

static void commit_move(Swiss* em) {
    for_components(it, em,
            COMPONENT_MOVE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_BLUR_DAMAGED, it.id);
    }

    for_components(it, em,
            COMPONENT_MOVE, COMPONENT_PHYSICAL, CQ_END) {
        struct MoveComponent* move = swiss_getComponent(em, COMPONENT_MOVE, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        physical->position = move->newPosition;
    }
}

void commit_destroy(Swiss* em) {
    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_DESTROY, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);
        stateful->state = STATE_DESTROYING;
    }
}

static void commit_unmap(Swiss* em, struct X11Context* xcontext) {
    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_UNMAP, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        // Fading out
        // @HACK If we are being destroyed, we don't want to stop doing that
        if(stateful->state != STATE_DESTROYING)
            stateful->state = STATE_HIDING;
    }
    for_components(it, em,
            COMPONENT_UNMAP, COMPONENT_BINDS_TEXTURE, CQ_END) {
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it.id);
        wd_delete(&bindsTexture->drawable);
    }

    // Unmapping a window causes us to stop redirecting it
    {
        for_components(it, em,
                COMPONENT_UNMAP, COMPONENT_TRACKS_WINDOW, COMPONENT_REDIRECTED, CQ_END) {
            struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);

            XCompositeUnredirectWindow(xcontext->display, tracksWindow->id, CompositeRedirectManual);
        }
        swiss_removeComponentWhere(
            em,
            COMPONENT_REDIRECTED,
            (enum ComponentType[]){COMPONENT_REDIRECTED, COMPONENT_TRACKS_WINDOW, COMPONENT_UNMAP, CQ_END}
        );
    }

    swiss_removeComponentWhere(
        em,
        COMPONENT_BINDS_TEXTURE,
        (enum ComponentType[]){COMPONENT_BINDS_TEXTURE, COMPONENT_UNMAP, CQ_END}
    );

    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_UNMAP, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        // Fading out
        // @HACK If we are being destroyed, we don't want to stop doing that
        if(stateful->state == STATE_DESTROYING) {
            swiss_removeComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
            swiss_removeComponent(em, COMPONENT_HAS_CLIENT, it.id);
            stateful->state = STATE_HIDING;
        }
    }
}

static void commit_map(Swiss* em, struct Atoms* atoms, struct X11Context* xcontext) {
    // Mapping a window causes us to start redirecting it
    {
        zone_enter(&ZONE_fetch_prop);
        for_components(it, em,
                COMPONENT_MAP, COMPONENT_HAS_CLIENT, COMPONENT_TRACKS_WINDOW, CQ_END) {
            struct HasClientComponent* hasClient = swiss_godComponent(em, COMPONENT_HAS_CLIENT, it.id);

            winprop_t prop = wid_get_prop(xcontext, hasClient->id, atoms->atom_bypass, 1L, XA_CARDINAL, 32);
            // A value of 1 means that the window has taken special care to ask
            // us not to do compositing.
            if(prop.nitems == 0 || *prop.data.p32 != 1) {
                swiss_ensureComponent(em, COMPONENT_REDIRECTED, it.id);
            }
            free_winprop(&prop);
        }
        zone_leave(&ZONE_fetch_prop);

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

    // Resize textures when mapping a window with a texture
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_REDIRECTED, COMPONENT_TEXTURED, CQ_END) {
        struct MapComponent* map = swiss_getComponent(em, COMPONENT_MAP, it.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it.id);

        texture_resize(&textured->texture, &map->size);
        renderbuffer_resize(&textured->stencil, &map->size);
    }

    // Create a texture when mapping windows without one
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_REDIRECTED, CQ_NOT, COMPONENT_TEXTURED, CQ_END) {
        struct MapComponent* map = swiss_getComponent(em, COMPONENT_MAP, it.id);

        struct TexturedComponent* textured = swiss_addComponent(em, COMPONENT_TEXTURED, it.id);

        if(texture_init(&textured->texture, GL_TEXTURE_2D, &map->size) != 0)  {
            printf_errf("Failed initializing window contents texture");
        }

        if(renderbuffer_stencil_init(&textured->stencil, &map->size) != 0)  {
            printf_errf("Failed initializing window contents stencil");
        }
    }

    // When we map a window, and blur/shadow isn't there, we want to add them.
    for_components(it, em,
            COMPONENT_MUD, COMPONENT_MAP, COMPONENT_TEXTURED, CQ_NOT, COMPONENT_SHADOW, CQ_END) {
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

        if(w->shadow) {
            struct glx_shadow_cache* shadow = swiss_addComponent(em, COMPONENT_SHADOW, it.id);

            if(shadow_cache_init(shadow) != 0) {
                printf_errf("Failed initializing window shadow");
                swiss_removeComponent(em, COMPONENT_SHADOW, it.id);
            }
        }
    }

    for_components(it, em,
            COMPONENT_MUD, COMPONENT_MAP, COMPONENT_TEXTURED, CQ_NOT, COMPONENT_BLUR, CQ_END) {
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

        if(w->blur_background) {
            struct glx_blur_cache* blur = swiss_addComponent(em, COMPONENT_BLUR, it.id);

            if(blur_cache_init(blur) != 0) {
                printf_errf("Failed initializing window blur");
                swiss_removeComponent(em, COMPONENT_BLUR, it.id);
            }
        }
    }

    for_components(it, em,
            COMPONENT_MUD, COMPONENT_MAP, CQ_NOT, COMPONENT_TINT, CQ_END) {
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

        if(w->blur_background) {
            struct TintComponent* tint = swiss_addComponent(em, COMPONENT_TINT, it.id);
            tint->color = (Vector4){{1, 1, 1, .0}};
        }
    }

    // No matter what, when we remap a window we want to make sure the blur and
    // shadow are the correct size
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_SHADOW, CQ_END) {
        struct MapComponent* map = swiss_getComponent(em, COMPONENT_MAP, it.id);
        struct glx_shadow_cache* shadow = swiss_getComponent(em, COMPONENT_SHADOW, it.id);

        shadow_cache_resize(shadow, &map->size);
        swiss_ensureComponent(em, COMPONENT_SHADOW_DAMAGED, it.id);
    }

    for_components(it, em,
            COMPONENT_MAP, COMPONENT_BLUR, CQ_END) {
        struct MapComponent* map = swiss_getComponent(em, COMPONENT_MAP, it.id);
        struct glx_blur_cache* blur = swiss_getComponent(em, COMPONENT_BLUR, it.id);

        blur_cache_resize(blur, &map->size);
        swiss_ensureComponent(em, COMPONENT_BLUR_DAMAGED, it.id);
    }

    // After a map we'd like to immediately bind the window.
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_BINDS_TEXTURE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_CONTENTS_DAMAGED, it.id);
    }

    for_components(it, em,
            COMPONENT_MAP, COMPONENT_PHYSICAL, CQ_END) {
        struct MapComponent* map = swiss_getComponent(em, COMPONENT_MAP, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        physical->position = map->position;
        physical->size = map->size;
    }

    // We want to fatch the wintype on a map, useful because we don't track the
    // wintype when unmapped
    for_components(it, em, COMPONENT_MAP, CQ_END) {
        swiss_addComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
    }
}

void fill_wintype_changes(Swiss* em, session_t* ps) {
    // Fetch the new window type
    for_components(it, em,
            COMPONENT_WINTYPE_CHANGE, COMPONENT_HAS_CLIENT, CQ_END) {
        struct WintypeChangedComponent* wintypeChanged = swiss_getComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
        struct HasClientComponent* client = swiss_getComponent(em, COMPONENT_HAS_CLIENT, it.id);

        // Detect window type here
        set_ignore_next(ps);
        winprop_t prop = wid_get_prop(&ps->xcontext, client->id, ps->atoms.atom_win_type, 32L, XA_ATOM, 32);

        wintypeChanged->newType = WINTYPE_UNKNOWN;

        for (unsigned i = 0; i < prop.nitems; ++i) {
            for (wintype_t j = 1; j < NUM_WINTYPES; ++j) {
                if (ps->atoms.atoms_wintypes[j] == (Atom) prop.data.p32[i]) {
                    wintypeChanged->newType = j;
                }
            }
        }

        free_winprop(&prop);
    }

    // Guess the window type if not provided
    for_components(it, em,
            COMPONENT_WINTYPE_CHANGE, COMPONENT_HAS_CLIENT, CQ_END) {
        struct WintypeChangedComponent* wintypeChanged = swiss_getComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
        struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
        struct HasClientComponent* client = swiss_getComponent(em, COMPONENT_HAS_CLIENT, it.id);

        // Conform to EWMH standard, if _NET_WM_WINDOW_TYPE is not present, take
        // override-redirect windows or windows without WM_TRANSIENT_FOR as
        // _NET_WM_WINDOW_TYPE_NORMAL, otherwise as _NET_WM_WINDOW_TYPE_DIALOG.
        if (wintypeChanged->newType == WINTYPE_UNKNOWN) {
            if (w->override_redirect || !wid_has_prop(ps, client->id, ps->atoms.atom_transient))
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

static void fetchSortedWindowsWithArr(Swiss* em, Vector* result, CType* query) {
    for_componentsArr(it, em, query) {
        vector_putBack(result, &it.id);
    }
    vector_qsort(result, window_zcmp, em);
}
#define fetchSortedWindowsWith(em, result, ...) \
    fetchSortedWindowsWithArr(em, result, (CType[]){ __VA_ARGS__ })

/**
 * Do the actual work.
 *
 * @param ps current session
 */
void session_run(session_t *ps) {
    struct ProfilerWriterSession profSess;
    profilerWriter_init(&profSess);


    paint_preprocess(ps);

    timestamp lastTime;
    if(!getTime(&lastTime)) {
        printf_errf("Failed getting time");
        session_destroy(ps);
        exit(1);
    }

    assign_depth(&ps->win_list, &ps->order);

    // Initialize idling
    ps->idling = false;

    // Main loop
    while (!ps->reset) {

        zone_start(&ZONE_global);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        zone_enter(&ZONE_input);

        while (mainloop(ps));

        Swiss* em = &ps->win_list;

        for_components(it, em,
                COMPONENT_PHYSICAL, COMPONENT_TRACKS_WINDOW, COMPONENT_SHAPE_DAMAGED, CQ_END) {
            struct TracksWindowComponent* window = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
            struct ShapeDamagedEvent* shapeDamaged = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);

            // @HACK @MUD: The first half of this would be better placed in the X11
            // input processor.

            XWindowAttributes attribs;
            if (!XGetWindowAttributes(ps->xcontext.display, window->id, &attribs)) {
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

            XserverRegion window_region = XFixesCreateRegionFromWindow(ps->xcontext.display, window->id, ShapeBounding);

            XRectangle default_clip = {.x = offset.x, .y = offset.y, .width = extents.x, .height = extents.y};
            XserverRegion default_clip_region = XFixesCreateRegion(ps->xcontext.display, &default_clip, 1);
            XFixesIntersectRegion(ps->xcontext.display, window_region, window_region, default_clip_region);

            int rect_count;
            XRectangle* rects = XFixesFetchRegion(ps->xcontext.display, window_region, &rect_count);

            XFixesDestroyRegion(ps->xcontext.display, window_region);

            vector_init(&shapeDamaged->rects, sizeof(struct Rect), rect_count);

            convert_xrects_to_relative_rect(rects, rect_count, &extents, &offset, &shapeDamaged->rects);
        }

        // Process all the events added by X
        fill_wintype_changes(&ps->win_list, ps);

        zone_leave(&ZONE_input);

        // Placed after mainloop to avoid counting input time
        timestamp currentTime;
        if(!getTime(&currentTime)) {
            printf_errf("Failed getting time");
            exit(1);
        }

        double dt = timeDiff(&lastTime, &currentTime);

        ps->skip_poll = false;

        if (ps->o.benchmark) {
            if (ps->o.benchmark_wid) {
                win *w = find_win(ps, ps->o.benchmark_wid);
                if (!w) {
                    printf_errf("(): Couldn't find specified benchmark window.");
                    session_destroy(ps);
                    exit(1);
                }
            }
        }

        // idling will be turned off later if desired.
        ps->idling = true;

        zone_enter(&ZONE_preprocess);

        paint_preprocess(ps);

        zone_leave(&ZONE_preprocess);

        zone_enter(&ZONE_update);

        zone_enter(&ZONE_update_z);
        assign_depth(&ps->win_list, &ps->order);
        zone_leave(&ZONE_update_z);

        zone_enter(&ZONE_update_wintype);
        for_components(it, em, COMPONENT_WINTYPE_CHANGE, CQ_END) {
            swiss_ensureComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        }

        for_components(it, em,
                COMPONENT_WINTYPE_CHANGE, CQ_END) {
            struct WintypeChangedComponent* wintypeChanged = swiss_getComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
            struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

            w->window_type = wintypeChanged->newType;
        }

        swiss_resetComponent(em, COMPONENT_WINTYPE_CHANGE);
        zone_leave(&ZONE_update_wintype);


        if (ps->o.shadow_blacklist) {
            zone_enter(&ZONE_update_shadow_blacklist);
            for_components(it, em,
                    COMPONENT_MUD, CQ_END) {
                struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
                if (win_mapped(em, it.id)) {
                    w->shadow = (ps->o.wintype_shadow[w->window_type]
                            && !win_match(ps, w, ps->o.shadow_blacklist)
                            && !(ps->o.respect_prop_shadow));
                }
            }
            zone_leave(&ZONE_update_shadow_blacklist);
        }

        if (ps->o.fade_blacklist) {
            zone_enter(&ZONE_update_fade_blacklist);
            for_components(it, em,
                    COMPONENT_MUD, CQ_END) {
                struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

                if(win_mapped(em, it.id)) {
                    // Ignore other possible causes of fading state changes after window
                    // gets unmapped
                    if (win_match(ps, w, ps->o.fade_blacklist)) {
                        w->fade = false;
                    } else {
                        w->fade = ps->o.wintype_fade[w->window_type];
                    }
                }
            }
            zone_leave(&ZONE_update_fade_blacklist);
        }

        if (ps->o.invert_color_list) {
            zone_enter(&ZONE_update_invert_list);
            for_components(it, em,
                    COMPONENT_MUD, CQ_END) {
                struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
                if(win_mapped(em, it.id)) {
                    bool invert_color_new = win_match(ps, w, ps->o.invert_color_list);
                    win_set_invert_color(ps, w, invert_color_new);
                }
            }
            zone_leave(&ZONE_update_invert_list);
        }

        if (ps->o.blur_background_blacklist) {
            zone_enter(&ZONE_update_blur_blacklist);
            for_components(it, em,
                    COMPONENT_MUD, CQ_END) {
                struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
                if(win_mapped(em, it.id)) {
                    bool blur_background_new = ps->o.blur_background
                        && !win_match(ps, w, ps->o.blur_background_blacklist);

                    win_set_blur_background(ps, w, blur_background_new);
                }
            }
            zone_leave(&ZONE_update_blur_blacklist);
        }

        if (ps->o.paint_blacklist) {
            zone_enter(&ZONE_update_paint_blacklist);
            for_components(it, em,
                    COMPONENT_MUD, CQ_END) {
                struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
                if(win_mapped(em, it.id)) {
                    w->paint_excluded = win_match(ps, w, ps->o.paint_blacklist);
                }
            }
            zone_leave(&ZONE_update_paint_blacklist);
        }

        zone_enter(&ZONE_input_react);
        commit_destroy(&ps->win_list);
        commit_map(&ps->win_list, &ps->atoms, &ps->xcontext);
        commit_unmap(&ps->win_list, &ps->xcontext);
        commit_opacity_change(&ps->win_list, ps->o.opacity_fade_time);
        commit_move(&ps->win_list);
        commit_resize(&ps->win_list);
        commit_reshape(&ps->win_list, &ps->xcontext);
        zone_leave(&ZONE_input_react);

        zone_enter(&ZONE_make_cutout);
        {
            XserverRegion newShape = XFixesCreateRegion(ps->dpy, NULL, 0);
            for_components(it, em,
                    COMPONENT_MUD, COMPONENT_TRACKS_WINDOW, COMPONENT_PHYSICAL, CQ_NOT, COMPONENT_REDIRECTED, CQ_END) {
                struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
                struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

                if(win_mapped(em, it.id)) {
                    XserverRegion windowRegion = XFixesCreateRegionFromWindow(ps->xcontext.display, tracksWindow->id, ShapeBounding);
                    XFixesTranslateRegion(ps->dpy, windowRegion, physical->position.x+1, physical->position.y+1);
                    XFixesUnionRegion(ps->xcontext.display, newShape, newShape, windowRegion);
                    XFixesDestroyRegion(ps->xcontext.display, windowRegion);
                }
            }
            XFixesInvertRegion(ps->dpy, newShape, &(XRectangle){0, 0, ps->root_size.x, ps->root_size.y}, newShape);
            XFixesSetWindowShapeRegion(ps->dpy, ps->overlay, ShapeBounding, 0, 0, newShape);
            XFixesDestroyRegion(ps->xcontext.display, newShape);
        }
        zone_leave(&ZONE_make_cutout);

        zone_enter(&ZONE_remove_input);
        // Remove all the X events
        // @CLEANUP: Should maybe be done last in the frame
        swiss_resetComponent(&ps->win_list, COMPONENT_MAP);
        swiss_resetComponent(&ps->win_list, COMPONENT_UNMAP);
        swiss_resetComponent(&ps->win_list, COMPONENT_MOVE);
        swiss_removeComponentWhere(
            &ps->win_list,
            COMPONENT_RESIZE,
            (enum ComponentType[]){COMPONENT_PHYSICAL, CQ_END}
        );

        for_components(it, em, COMPONENT_SHAPE_DAMAGED, CQ_END) {
            struct ShapeDamagedEvent* shapeDamaged = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);
            if(shapeDamaged->rects.elementSize != 0) {
                vector_kill(&shapeDamaged->rects);
            }
        }
        swiss_resetComponent(&ps->win_list, COMPONENT_SHAPE_DAMAGED);

        zone_leave(&ZONE_remove_input);

        zone_enter(&ZONE_prop_blur_damage);
        // Damage the blur of windows on top of damaged windows
        for_components(it, &ps->win_list,
            COMPONENT_CONTENTS_DAMAGED, CQ_END) {

            size_t order_slot = vector_find_uint64(&ps->order, it.id);
            assert(order_slot >= 0);

            win_id* other_id = vector_getNext(&ps->order, &order_slot);
            while(other_id != NULL) {

                if(win_overlap(&ps->win_list, it.id, *other_id)) {
                    swiss_ensureComponent(&ps->win_list, COMPONENT_BLUR_DAMAGED, *other_id);
                }

                other_id = vector_getNext(&ps->order, &order_slot);
            }
        }
        zone_leave(&ZONE_prop_blur_damage);

        zone_enter(&ZONE_update_textures);
        update_window_textures(&ps->win_list, &ps->xcontext, &ps->psglx->blur.fbo);
        zone_leave(&ZONE_update_textures);

        update_focused_state(&ps->win_list, ps);
        calculate_window_opacity(ps, &ps->win_list);
        start_focus_fade(&ps->win_list, ps->o.opacity_fade_time, ps->o.dim_fade_time);
        swiss_resetComponent(&ps->win_list, COMPONENT_FOCUS_CHANGE);

        zone_enter(&ZONE_update_fade);

        damage_blur_over_fade(&ps->win_list);
        syncronize_fade_opacity(&ps->win_list);
        if(do_win_fade(&ps->curve, dt, &ps->win_list)) {
            ps->skip_poll = true;
        }

        zone_leave(&ZONE_update_fade);

        transition_faded_entities(&ps->win_list);
        remove_texture_invis_windows(&ps->win_list);
        finish_destroyed_windows(&ps->win_list, ps);
        zone_leave(&ZONE_update);

        Vector opaque;
        vector_init(&opaque, sizeof(win_id), ps->order.size);
        for_components(it, &ps->win_list,
                COMPONENT_MUD, COMPONENT_TEXTURED, CQ_NOT, COMPONENT_OPACITY, COMPONENT_PHYSICAL, CQ_END) {
            vector_putBack(&opaque, &it.id);
        }
        vector_qsort(&opaque, window_zcmp, &ps->win_list);
        Vector transparent;
        vector_init(&transparent, sizeof(win_id), ps->order.size);
        for_components(it, &ps->win_list,
                COMPONENT_MUD, COMPONENT_TEXTURED, /* COMPONENT_OPACITY, */ COMPONENT_PHYSICAL, CQ_END) {
            vector_putBack(&transparent, &it.id);
        }
        vector_qsort(&transparent, window_zcmp, &ps->win_list);

        Vector opaque_shadow;
        vector_init(&opaque_shadow, sizeof(win_id), ps->order.size);
        fetchSortedWindowsWith(&ps->win_list, &opaque_shadow,
                COMPONENT_MUD, COMPONENT_Z, COMPONENT_PHYSICAL, CQ_NOT, COMPONENT_OPACITY, COMPONENT_SHADOW, CQ_END);

        zone_enter(&ZONE_effect_textures);

        zone_enter(&ZONE_update_shadow);
        windowlist_updateShadow(ps, &transparent);
        zone_leave(&ZONE_update_shadow);

        windowlist_updateBlur(ps);

        zone_leave(&ZONE_effect_textures);

        {
            static int paint = 0;

            zone_enter(&ZONE_paint);

            glDepthMask(GL_TRUE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
            glDrawBuffers(1, DRAWBUFS);
            glViewport(0, 0, ps->root_size.x, ps->root_size.y);

            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
            glDepthFunc(GL_LESS);

            windowlist_drawBackground(ps, &opaque);
            windowlist_drawTint(ps);
            windowlist_draw(ps, &opaque);

            paint_root(ps);

            windowlist_drawTransparent(ps, &transparent);

#ifdef DEBUG_WINDOWS
            windowlist_drawDebug(&ps->win_list, ps);
#endif

            vector_kill(&opaque_shadow);
            vector_kill(&transparent);
            vector_kill(&opaque);

            /* { */
            /*     glDisable(GL_DEPTH_TEST); */
            /*     glDisable(GL_BLEND); */
            /*     struct face* face = assets_load("window.face"); */
            /*     for_components(it, &ps->win_list, */
            /*             COMPONENT_PHYSICAL, COMPONENT_BLUR, COMPONENT_Z, CQ_END) { */
            /*         struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, it.id); */
            /*         struct glx_blur_cache* blur = swiss_getComponent(&ps->win_list, COMPONENT_BLUR, it.id); */
            /*         struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, it.id); */

            /*         Vector2 glPos = X11_rectpos_to_gl(ps, &physical->position, &physical->size); */
            /*         Vector3 dglPos = vec3_from_vec2(&glPos, z->z + 0.000001); */

            /*         draw_tex(face, &blur->texture[0], &dglPos, &(Vector2){{100, 100}}); */
            /*     } */
            /* } */
            zone_leave(&ZONE_paint);

            paint++;
            if (ps->o.benchmark && paint >= ps->o.benchmark) {
                profilerWriter_kill(&profSess);
                session_destroy(ps);
                exit(0);
            }
            XSync(ps->dpy, False);
        }


        swiss_resetComponent(&ps->win_list, COMPONENT_CONTENTS_DAMAGED);

        // Finish the profiling before the vsync, since we don't want that to drag out the time
        struct ZoneEventStream* event_stream = zone_package(&ZONE_global);
#ifdef DEBUG_PROFILE
        /* profiler_render(event_stream); */
        profilerWriter_emitFrame(&profSess, event_stream);
#endif

        glXSwapBuffers(ps->dpy, get_tgt_window(ps));
        glFinish();

        lastTime = currentTime;
    }

    profilerWriter_kill(&profSess);
}
