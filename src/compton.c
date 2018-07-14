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
#include "vmath.h"
#include "window.h"
#include "windowlist.h"
#include "blur.h"
#include "shadow.h"
#include "xtexture.h"
#include "timer.h"

#include "assets/assets.h"
#include "assets/shader.h"

#include "renderutil.h"

#include "shaders/shaderinfo.h"

#include "logging.h"

#include "profiler/zone.h"
#include "profiler/render.h"


// === Global constants ===

DECLARE_ZONE(global);
DECLARE_ZONE(input);
DECLARE_ZONE(preprocess);
DECLARE_ZONE(preprocess_window);
DECLARE_ZONE(update);
DECLARE_ZONE(paint);
DECLARE_ZONE(effect_textures);

// From the header {{{

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
#ifdef CONFIG_XINERAMA
    if (ps->xinerama_scr_regs) {
        for (int i = 0; i < ps->xinerama_nscrs; ++i)
            free_region(ps, &ps->xinerama_scr_regs[i]);
        free(ps->xinerama_scr_regs);
    }
    cxfree(ps->xinerama_scrs);
    ps->xinerama_scrs = NULL;
    ps->xinerama_nscrs = 0;
#endif
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
    return ps->damage_event + XDamageNotify == ev->type;
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
    // Will get BadWindow if the window is destroyed
    set_ignore_next(ps);
    XSelectInput(ps->dpy, w->id, 0);

    if (w->client_win) {
        set_ignore_next(ps);
        XSelectInput(ps->dpy, w->client_win, 0);
    }

    if (ps->shape_exists) {
        /* set_ignore_next(ps); */
        /* XShapeSelectInput(ps->dpy, w->id, 0); */
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

static void update_reg_ignore_expire(session_t *ps, const win *w) {
    if (w->to_paint && w->solid)
        ps->reg_ignore_expire = true;
}

static bool __attribute__((pure)) win_has_frame(const win *w) {
    return w->a.border_width
        || w->frame_extents.top || w->frame_extents.left
        || w->frame_extents.right || w->frame_extents.bottom;
}

static bool validate_pixmap(session_t *ps, Pixmap pxmap) {
    if (!pxmap) return false;

    Window rroot = None;
    int rx = 0, ry = 0;
    unsigned rwid = 0, rhei = 0, rborder = 0, rdepth = 0;
    return XGetGeometry(ps->dpy, pxmap, &rroot, &rx, &ry,
            &rwid, &rhei, &rborder, &rdepth) && rwid && rhei;
}

static bool win_match(session_t *ps, win *w, c2_lptr_t *condlst, const c2_lptr_t **cache) {
#ifdef CONFIG_C2
    return c2_match(ps, w, condlst, cache);
#else
    return false;
#endif
}

static bool condlst_add(session_t *ps, c2_lptr_t **pcondlst, const char *pattern);

static long determine_evmask(session_t *ps, Window wid, win_evmode_t mode);

static void clear_cache_win_leaders(session_t *ps) {
    size_t index;
    win *w = swiss_getFirst(&ps->win_list, &index);
    while(w != NULL) {
        w->cache_leader = None;
        w = swiss_getNext(&ps->win_list, &index);
    }
}

static win * find_toplevel2(session_t *ps, Window wid);

static win * find_win_all(session_t *ps, const Window wid) {
    if (!wid || PointerRoot == wid || wid == ps->root || wid == ps->overlay)
        return NULL;

    win *w = find_win(ps, wid);
    if (!w) w = find_toplevel(ps, wid);
    if (!w) w = find_toplevel2(ps, wid);
    return w;
}

static Window win_get_leader_raw(session_t *ps, win *w, int recursions);

static Window win_get_leader(session_t *ps, win *w) {
    return win_get_leader_raw(ps, w, 0);
}

static bool group_is_focused(session_t *ps, Window leader) {
    if (!leader)
        return false;

    size_t index;
    win *w = swiss_getFirst(&ps->win_list, &index);
    while(w != NULL) {
        if (win_get_leader(ps, w) == leader && !w->destroyed
                && win_is_focused_real(ps, w))
            return true;
        w = swiss_getNext(&ps->win_list, &index);
    }

    return false;
}

static void add_damage(session_t *ps, XserverRegion damage);

static void win_determine_mode(session_t *ps, win *w);

static void calc_dim(session_t *ps, win *w);

static void win_update_leader(session_t *ps, win *w);

static void win_set_leader(session_t *ps, win *w, Window leader);

static void win_update_focused(session_t *ps, win *w);

static inline void win_set_focused(session_t *ps, win *w);

static void win_determine_shadow(session_t *ps, win *w);

static void win_set_invert_color(session_t *ps, win *w, bool invert_color_new);

static void win_set_blur_background(session_t *ps, win *w, bool blur_background_new);

static void win_determine_blur_background(session_t *ps, win *w);

static void win_mark_client(session_t *ps, win *w, Window client);

static void win_recheck_client(session_t *ps, win *w);

static void configure_win(session_t *ps, XConfigureEvent *ce);

static bool wid_get_name(session_t *ps, Window w, char **name);

static bool wid_get_role(session_t *ps, Window w, char **role);

static int win_get_prop_str(session_t *ps, win *w, char **tgt,
        bool (*func_wid_get_prop_str)(session_t *ps, Window wid, char **tgt));

static int win_get_name(session_t *ps, win *w) {
    int ret = win_get_prop_str(ps, w, &w->name, wid_get_name);

#ifdef DEBUG_WINDATA
    printf_dbgf("(%#010lx): client = %#010lx, name = \"%s\", "
            "ret = %d\n", w->id, w->client_win, w->name, ret);
#endif

    return ret;
}

static int win_get_role(session_t *ps, win *w) {
    int ret = win_get_prop_str(ps, w, &w->role, wid_get_role);

#ifdef DEBUG_WINDATA
    printf_dbgf("(%#010lx): client = %#010lx, role = \"%s\", "
            "ret = %d\n", w->id, w->client_win, w->role, ret);
#endif

    return ret;
}

static bool win_get_class(session_t *ps, win *w);

#ifdef DEBUG_EVENTS
static int ev_serial(XEvent *ev);

static const char * ev_name(session_t *ps, XEvent *ev);

static Window ev_window(session_t *ps, XEvent *ev);
#endif

static void update_ewmh_active_win(session_t *ps);

static XserverRegion get_screen_region(session_t *ps) {
    XRectangle r;

    r.x = 0;
    r.y = 0;
    r.width = ps->root_width;
    r.height = ps->root_height;
    return XFixesCreateRegion(ps->dpy, &r, 1);
}

static void add_damage_win(session_t *ps, win *w) {
    add_damage(ps, None);
}

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

static bool vsync_drm_init(session_t *ps);

#ifdef CONFIG_VSYNC_DRM
static int vsync_drm_wait(session_t *ps);
#endif

static bool vsync_opengl_init(session_t *ps);

static bool vsync_opengl_oml_init(session_t *ps);

static bool vsync_opengl_swc_init(session_t *ps);

static bool vsync_opengl_mswc_init(session_t *ps);

static int vsync_opengl_wait(session_t *ps);

static int vsync_opengl_oml_wait(session_t *ps);

static void vsync_opengl_swc_deinit(session_t *ps);

static void vsync_opengl_mswc_deinit(session_t *ps);

static void vsync_wait(session_t *ps);

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
static void cxinerama_win_upd_scr(session_t *ps, win *w) {
#ifdef CONFIG_XINERAMA
    w->xinerama_scr = -1;
    for (XineramaScreenInfo *s = ps->xinerama_scrs;
            s < ps->xinerama_scrs + ps->xinerama_nscrs; ++s)
        if (s->x_org <= w->a.x && s->y_org <= w->a.y
                && s->x_org + s->width >= w->a.x + w->widthb
                && s->y_org + s->height >= w->a.y + w->heightb) {
            w->xinerama_scr = s - ps->xinerama_scrs;
            return;
        }
#endif
}

static void cxinerama_upd_scrs(session_t *ps);

static void session_destroy(session_t *ps);
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
  "drm",              // VSYNC_DRM
  "opengl",           // VSYNC_OPENGL
  "opengl-oml",       // VSYNC_OPENGL_OML
  "opengl-swc",       // VSYNC_OPENGL_SWC
  "opengl-mswc",      // VSYNC_OPENGL_MSWC
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
  [VSYNC_DRM          ] = vsync_drm_init,
  [VSYNC_OPENGL       ] = vsync_opengl_init,
  [VSYNC_OPENGL_OML   ] = vsync_opengl_oml_init,
  [VSYNC_OPENGL_SWC   ] = vsync_opengl_swc_init,
  [VSYNC_OPENGL_MSWC  ] = vsync_opengl_mswc_init,
};

/// Function pointers to wait for VSync.
static int (* const (VSYNC_FUNCS_WAIT[NUM_VSYNC]))(session_t *ps) = {
#ifdef CONFIG_VSYNC_DRM
  [VSYNC_DRM        ] = vsync_drm_wait,
#endif
  [VSYNC_OPENGL     ] = vsync_opengl_wait,
  [VSYNC_OPENGL_OML ] = vsync_opengl_oml_wait,
};

/// Function pointers to deinitialize VSync.
static void (* const (VSYNC_FUNCS_DEINIT[NUM_VSYNC]))(session_t *ps) = {
  [VSYNC_OPENGL_SWC   ] = vsync_opengl_swc_deinit,
  [VSYNC_OPENGL_MSWC  ] = vsync_opengl_mswc_deinit,
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

// === Fading ===

/**
 * Get the time left before next fading point.
 *
 * In milliseconds.
 */
static int
fade_timeout(session_t *ps) {
  return 10;
}

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
wid_get_prop_adv(const session_t *ps, Window w, Atom atom, long offset,
    long length, Atom rtype, int rformat) {
  Atom type = None;
  int format = 0;
  unsigned long nitems = 0, after = 0;
  unsigned char *data = NULL;

  if (Success == XGetWindowProperty(ps->dpy, w, atom, offset, length,
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

static void fetch_shaped_window_face(session_t* ps, struct _win* w) {
    if(w->face != NULL)
        face_unload_file(w->face);

    Vector2 extents = {{w->a.width, w->a.height}};

    XserverRegion window_region = XFixesCreateRegionFromWindow(ps->dpy, w->id, ShapeBounding);

    int count;
    XRectangle* rects = XFixesFetchRegion(ps->dpy, window_region, &count);

    XFixesDestroyRegion(ps->dpy, window_region);

    struct face* face = malloc(sizeof(struct face));
    // We want 3 floats (x, y, z) for every vertex of which there are
    // 6 (top-left bot-left top-right top-right bot-left bot-right) per rect
    face->vertex_buffer_data = malloc(sizeof(float) * 3 * 6 * count);
    face->uv_buffer_data = malloc(sizeof(float) * 2 * 6 * count);
    face->vertex_buffer_size = count * 6;
    face->uv_buffer_size = count * 6;

    for(int i = 0; i < count; i++) {
        // A single rect line in the vertex buffer is 3 * 6
        float* vertex_rect = &face->vertex_buffer_data[i * 3 * 6];
        // A single rect line in the uv buffer is 2 * 6
        float* uv_rect = &face->uv_buffer_data[i * 2 * 6];

        Vector2 rect_coord = {{rects[i].x, w->a.height - rects[i].y}};
        vec2_div(&rect_coord, &extents);
        Vector2 rect_size = {{rects[i].width, rects[i].height}};
        vec2_div(&rect_size, &extents);

        int vec_cnt = 0;
        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect_coord.x;
            vertex_vec[1] = rect_coord.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect_coord.x;
            uv_vec[1] = rect_coord.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect_coord.x;
            vertex_vec[1] = rect_coord.y - rect_size.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect_coord.x;
            uv_vec[1] = rect_coord.y - rect_size.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect_coord.x + rect_size.x;
            vertex_vec[1] = rect_coord.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect_coord.x + rect_size.x;
            uv_vec[1] = rect_coord.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect_coord.x + rect_size.x;
            vertex_vec[1] = rect_coord.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect_coord.x + rect_size.x;
            uv_vec[1] = rect_coord.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect_coord.x;
            vertex_vec[1] = rect_coord.y - rect_size.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect_coord.x;
            uv_vec[1] = rect_coord.y - rect_size.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect_coord.x + rect_size.x;
            vertex_vec[1] = rect_coord.y - rect_size.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect_coord.x + rect_size.x;
            uv_vec[1] = rect_coord.y - rect_size.y;
        }
    }

    glGenBuffers(1, &face->vertex);
    glGenBuffers(1, &face->uv);

    glBindBuffer(GL_ARRAY_BUFFER, face->vertex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * face->vertex_buffer_size * 3, face->vertex_buffer_data, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, face->uv);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * face->uv_buffer_size * 2, face->uv_buffer_data, GL_STATIC_DRAW);

    w->face = face;
}

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
static long
determine_evmask(session_t *ps, Window wid, win_evmode_t mode) {
  long evmask = NoEventMask;
  win *w = NULL;

  // Check if it's a mapped frame window
  if (WIN_EVMODE_FRAME == mode
      || ((w = find_win(ps, wid)) && IsViewable == w->a.map_state)) {
    evmask |= PropertyChangeMask;
    if (ps->o.track_focus && !ps->o.use_ewmh_active_win)
      evmask |= FocusChangeMask;
  }

  // Check if it's a mapped client window
  if (WIN_EVMODE_CLIENT == mode
      || ((w = find_toplevel(ps, wid)) && IsViewable == w->a.map_state)) {
    if (ps->o.track_wdata || ps->track_atom_lst || ps->o.detect_client_opacity)
      evmask |= PropertyChangeMask;
  }

  return evmask;
}

/**
 * Find out the WM frame of a client window by querying X.
 *
 * @param ps current session
 * @param wid window ID
 * @return struct _win object of the found window, NULL if not found
 */
static win *
find_toplevel2(session_t *ps, Window wid) {
  win *w = NULL;

  // We traverse through its ancestors to find out the frame
  while (wid && wid != ps->root && !(w = find_win(ps, wid))) {
    Window troot;
    Window parent;
    Window *tchildren;
    unsigned tnchildren;

    // XQueryTree probably fails if you run compton when X is somehow
    // initializing (like add it in .xinitrc). In this case
    // just leave it alone.
    if (!XQueryTree(ps->dpy, wid, &troot, &parent, &tchildren,
          &tnchildren)) {
      parent = 0;
      break;
    }

    cxfree(tchildren);

    wid = parent;
  }

  return w;
}

/**
 * Recheck currently focused window and set its <code>w->focused</code>
 * to true.
 *
 * @param ps current session
 * @return struct _win of currently focused window, NULL if not found
 */
static win *
recheck_focus(session_t *ps) {
  // Use EWMH _NET_ACTIVE_WINDOW if enabled
  if (ps->o.use_ewmh_active_win) {
    update_ewmh_active_win(ps);
    return ps->active_win;
  }

  // Determine the currently focused window so we can apply appropriate
  // opacity on it
  Window wid = 0;
  int revert_to;

  XGetInputFocus(ps->dpy, &wid, &revert_to);

  win *w = find_win_all(ps, wid);

#ifdef DEBUG_EVENTS
  print_timestamp(ps);
  printf_dbgf("(): %#010lx (%#010lx \"%s\") focused.\n", wid,
      (w ? w->id: None), (w ? w->name: NULL));
#endif

  // And we set the focus state here
  if (w) {
    win_set_focused(ps, w);
    return w;
  }

  return NULL;
}

static bool
get_root_tile(session_t *ps) {
    Pixmap pixmap = None;

    // Get the values of background attributes
    for (int p = 0; background_props_str[p]; p++) {
        winprop_t prop = wid_get_prop(ps, ps->root,
                get_atom(ps, background_props_str[p]),
                1L, XA_PIXMAP, 32);
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
    XGetWindowAttributes(ps->psglx->xcontext.display, ps->root, &attribs);
    GLXFBConfig* fbconfig = xorgContext_selectConfig(&ps->psglx->xcontext, XVisualIDFromVisual(attribs.visual));

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

    glViewport(0, 0, ps->root_width, ps->root_height);

    glEnable(GL_DEPTH_TEST);

    struct face* face = assets_load("window.face");
    Vector2 rootSize = {{ps->root_width, ps->root_height}};
    Vector3 pos = {{0, 0, 0.000001}};
    draw_tex(face, &ps->root_texture.texture, &pos, &rootSize);
}

/**
 * Look for the client window of a particular window.
 */
static Window
find_client_win(session_t *ps, Window w) {
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

/**
 * Retrieve frame extents from a window.
 */
static void
get_frame_extents(session_t *ps, win *w, Window client) {
  cmemzero_one(&w->frame_extents);

  winprop_t prop = wid_get_prop(ps, client, ps->atoms.atom_frame_extents,
    4L, XA_CARDINAL, 32);

  if (4 == prop.nitems) {
    const long * const extents = prop.data.p32;
    w->frame_extents.left = extents[0];
    w->frame_extents.right = extents[1];
    w->frame_extents.top = extents[2];
    w->frame_extents.bottom = extents[3];

    w->has_frame = win_has_frame(w);

  }
  free_winprop(&prop);
}

static win *
paint_preprocess(session_t *ps, Vector* paints) {
    win *t = NULL;

    bool unredir_possible = false;
    // Trace whether it's the highest window to paint
    bool is_highest = true;

    size_t index;
    win_id* w_id = vector_getLast(&ps->order, &index);
    while(w_id != NULL) {
        struct _win* w = swiss_get(&ps->win_list, *w_id);
        zone_enter(&ZONE_preprocess_window);
        bool to_paint = true;
        const bool mode_old = w->solid;

        // In case calling the fade callback function destroys this window
        double opacity_old = w->opacity;

        // @CLEANUP: This should probably be somewhere else
        w->fullscreen = win_is_fullscreen(ps, w);

        // Destroy reg_ignore on all windows if they should expire
        if (ps->reg_ignore_expire)
            free_region(ps, &w->reg_ignore);

        // Restore flags from last paint if the window is being faded out
        if (IsUnmapped == w->a.map_state) {
            w->fade = w->fade_last;
            win_set_invert_color(ps, w, w->invert_color_last);
            win_set_blur_background(ps, w, w->blur_background_last);
        }

        // Update window opacity target and dim state if asked
        if (WFLAG_OPCT_CHANGE & w->flags) {
            calc_dim(ps, w);
        }

        // Give up if it's not damaged or invisible, or it's unmapped and its
        // pixmap is gone (for example due to a ConfigureNotify), or when it's
        // excluded
        if(w->a.x + w->a.width < 1 || w->a.y + w->a.height < 1)
            to_paint = false;
        if(w->a.x >= ps->root_width || w->a.y >= ps->root_height)
            to_paint = false;
        /* if((IsUnmapped == w->a.map_state || w->destroyed)) */
        /*     to_paint = false; */
        if(w->paint_excluded)
            to_paint = false;

        // to_paint will never change afterward

        // Determine mode as early as possible
        if (to_paint && (!w->to_paint || w->opacity != opacity_old))
            win_determine_mode(ps, w);

        if (to_paint) {
            // @INCOMPLETE: Vary the shadow opacity
        }

        // Add window to damaged area if its painting status changes
        // or opacity changes
        if (to_paint != w->to_paint || w->opacity != opacity_old)
            add_damage_win(ps, w);

        // Destroy all reg_ignore above when window mode changes
        if ((to_paint && w->solid) != (w->to_paint && w->solid == mode_old))
            ps->reg_ignore_expire = true;

        if (to_paint) {
            // (Un)redirect screen
            // We could definitely unredirect the screen when there's no window to
            // paint, but this is typically unnecessary, may cause flickering when
            // fading is enabled, and could create inconsistency when the wallpaper
            // is not correctly set.
            if (ps->o.unredir_if_possible && is_highest && to_paint) {
                is_highest = false;
                if(win_covers(w))
                    unredir_possible = true;
            }

            // If the window doesn't want to be redirected, then who are we to argue
            if(w->state != STATE_DESTROYED && w->state != STATE_DESTROYING) {
                winprop_t prop = wid_get_prop(ps, w->id, ps->atoms.atom_bypass, 1L, XA_CARDINAL, 32);
                // A value of 1 means that the window has taken special care to ask
                // us not to do compositing.It would be great if we could just
                // unredirect this specific window, or if we could just run
                // a fastpath to pump out frames as fast as possible. I don't know if
                // we can unredirect the specific window, and doing a fastpath will
                // require some more refactoring.
                // For now we just assume the developer really means it and
                // unredirect the entire screen. -Delusional 20/03-2018
                if(prop.nitems && *prop.data.p32 == 1) {
                    unredir_possible = true;
                }
                free_winprop(&prop);
            }

            // Reset flags
            w->flags = 0;
        }

        // Avoid setting w->to_paint if w is to be freed
        bool destroyed = false;

        if (to_paint) {
            w->prev_trans = t;
            if(t != NULL)
                t->next_trans = w;
            t = w;
            vector_putBack(paints, w_id);
        } else {
            assert(w->fade_callback == NULL);
        }

        if (!destroyed) {
            w->to_paint = to_paint;

            if (w->to_paint) {
                // Save flags
                w->fade_last = w->fade;
                w->invert_color_last = w->invert_color;
                w->blur_background_last = w->blur_background;
            }
        }
        zone_leave(&ZONE_preprocess_window);

        w_id = vector_getPrev(&ps->order, &index);
    }


    // If possible, unredirect all windows and stop painting
    if (UNSET != ps->o.redirected_force)
        unredir_possible = !ps->o.redirected_force;

    // If there's no window to paint, and the screen isn't redirected,
    // don't redirect it.
    if (ps->o.unredir_if_possible && is_highest && !ps->redirected)
        unredir_possible = true;
    if (unredir_possible) {
        if (ps->redirected) {
            if (!ps->o.unredir_if_possible_delay || ps->tmout_unredir_hit) {
                redir_stop(ps);
            }
            else if (!ps->tmout_unredir->enabled) {
                timeout_reset(ps, ps->tmout_unredir);
                ps->tmout_unredir->enabled = true;
            }
        }
    } else if(!ps->redirected) {
        ps->tmout_unredir->enabled = false;
        redir_start(ps);
    }

    return t;
}

/**
 * Rebuild cached <code>screen_reg</code>.
 */
static void
rebuild_screen_reg(session_t *ps) {
  if (ps->screen_reg)
    XFixesDestroyRegion(ps->dpy, ps->screen_reg);
  ps->screen_reg = get_screen_region(ps);
}

static void
paint_all(session_t *ps, Vector* paints) {
  glx_paint_pre(ps);

  glDepthMask(GL_TRUE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
  glDrawBuffers(1, DRAWBUFS);
  glViewport(0, 0, ps->root_width, ps->root_height);

  glClearDepth(0.0);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glDepthFunc(GL_GREATER);

  float z = 0;
  windowlist_draw(ps, paints, &z);

  /* set_tgt_clip(ps, reg_paint, NULL); */
  glEnable(GL_DEPTH_TEST);
  paint_root(ps);
  glDisable(GL_DEPTH_TEST);


  {
      size_t index;
      win_id* w_id = vector_getFirst(paints, &index);
      while(w_id != NULL) {
          struct _win* w = swiss_get(&ps->win_list, *w_id);
          win_postdraw(ps, w, z);
          z += .0001;
          w_id = vector_getNext(paints, &index);
      }
  }
}

static void
add_damage(session_t *ps, XserverRegion damage) {
    free_region(ps, &damage);

    if (!damage) return;
}

static wintype_t
wid_get_prop_wintype(session_t *ps, Window wid) {
  set_ignore_next(ps);
  winprop_t prop = wid_get_prop(ps, wid, ps->atoms.atom_win_type, 32L, XA_ATOM, 32);

  for (unsigned i = 0; i < prop.nitems; ++i) {
    for (wintype_t j = 1; j < NUM_WINTYPES; ++j) {
      if (ps->atoms.atoms_wintypes[j] == (Atom) prop.data.p32[i]) {
        free_winprop(&prop);
        return j;
      }
    }
  }

  free_winprop(&prop);

  return WINTYPE_UNKNOWN;
}

static void map_win(session_t *ps, win_id wid) {
  struct _win* w = swiss_get(&ps->win_list, wid);
  assert(w != NULL);
  // Unmap overlay window if it got mapped but we are currently not
  // in redirected state.
  if (ps->overlay && w->id == ps->overlay && !ps->redirected) {
    XUnmapWindow(ps->dpy, ps->overlay);
    XFlush(ps->dpy);
  }

  // Don't care about window mapping if it's an InputOnly window
  // Try avoiding mapping a window twice
  if (InputOnly == w->a.class || IsViewable == w->a.map_state)
    return;

  assert(!win_is_focused_real(ps, w));

  w->a.map_state = IsViewable;

  cxinerama_win_upd_scr(ps, w);

  // Call XSelectInput() before reading properties so that no property
  // changes are lost
  XSelectInput(ps->dpy, w->id, determine_evmask(ps, w->id, WIN_EVMODE_FRAME));

  // Make sure the XSelectInput() requests are sent
  XFlush(ps->dpy);

  // Update window mode here to check for ARGB windows
  win_determine_mode(ps, w);

  // Detect client window here instead of in add_win() as the client
  // window should have been prepared at this point
  if (!w->client_win) {
    win_recheck_client(ps, w);
  }
  else {
    // Re-mark client window here
    win_mark_client(ps, w, w->client_win);
  }

  assert(w->client_win);

  // @HACK: We need to save the old state here before recheck focus might
  // change it. It kinda sucks, but such is life.
  enum WindowState oldstate = w->state;

  // FocusIn/Out may be ignored when the window is unmapped, so we must
  // recheck focus here
  if (ps->o.track_focus)
    recheck_focus(ps);

  // Update window focus state
  win_update_focused(ps, w);

  // Many things above could affect shadow
  win_determine_shadow(ps, w);

  win_determine_blur_background(ps, w);

  w->damaged = false;

  if(ps->redirected) {
      // If the window was invisible we need to bind it again, since it was
      // unbound.
      if(oldstate == STATE_INVISIBLE) {
          // configure_win might rebind, so we need to bind before
          if(!wd_bind(&w->drawable)) {
              printf_errf("Failed binding window drawable %s", w->name);
              return;
          }

          if(!blur_cache_resize(&w->glx_blur_cache, &w->drawable.texture.size)) {
              printf_errf("Failed resizing window blur %s", w->name);
              return;
          }
      } else if(oldstate == STATE_HIDING){
          // If we were in the process of unmapping before we need to rebind
          if(!wd_unbind(&w->drawable)) {
              printf_errf("Failed unbinding window on resize");
          }
          if(!wd_bind(&w->drawable)) {
              printf_errf("Failed rebinding window on resize");
          }
      }
  }

  // Set fading state
  w->in_openclose = true;

  w->state = STATE_WAITING;
  w->focus_changed = true;

  /* if any configure events happened while
     the window was unmapped, then configure
     the window to its correct place */
  if (w->need_configure) {
    configure_win(ps, &w->queue_configure);
  }

#ifdef CONFIG_DBUS
  // Send D-Bus signal
  if (ps->o.dbus) {
    cdbus_ev_win_mapped(ps, w);
  }
#endif
}

static void unmap_win(session_t *ps, win *w) {
    if (!w || IsUnmapped == w->a.map_state) return;

    free_fence(ps, &w->fence);

    // Set focus out
    if(ps->active_win == w)
        ps->active_win = NULL;

    w->a.map_state = IsUnmapped;

    // Fading out
    w->state = STATE_HIDING;
    w->flags |= WFLAG_OPCT_CHANGE;

    win_start_opacity(w, 0, ps->o.opacity_fade_time);

    w->in_openclose = true;

    // don't care about properties anymore
    win_ev_stop(ps, w);

#ifdef CONFIG_DBUS
    // Send D-Bus signal
    if (ps->o.dbus) {
        cdbus_ev_win_unmapped(ps, w);
    }
#endif
}

static void win_determine_mode(session_t *ps, win *w) {
    if (w->pictfmt && w->pictfmt->type == PictTypeDirect && w->pictfmt->direct.alphaMask) {
        w->solid = false;
    } else if (w->opacity != 100.0) {
        w->solid = false;
    } else {
        w->solid = true;
    }
}

/**
 * Determine whether a window is to be dimmed.
 */
static void
calc_dim(session_t *ps, win *w) {
  bool dim;

  // Make sure we do nothing if the window is unmapped / destroyed
  if (w->destroyed || IsViewable != w->a.map_state)
    return;

  if (ps->o.inactive_dim && !(w->focused)) {
    dim = true;
  } else {
    dim = false;
  }

  if (dim != w->dim) {
    w->dim = dim;
    add_damage_win(ps, w);
  }
}

/**
 * Determine if a window should fade on opacity change.
 */
static void
win_determine_fade(session_t *ps, win *w) {
  // To prevent it from being overwritten by last-paint value if the window is
  // unmapped on next frame, write w->fade_last as well
  if (UNSET != w->fade_force)
    w->fade_last = w->fade = w->fade_force;
  else if (ps->o.no_fading_openclose && w->in_openclose)
    w->fade_last = w->fade = false;
  else if (ps->o.no_fading_destroyed_argb && w->destroyed
      && !w->solid && w->client_win && w->client_win != w->id) {
    w->fade_last = w->fade = false;
  }
  // Ignore other possible causes of fading state changes after window
  // gets unmapped
  else if (IsViewable != w->a.map_state) {
  }
  else if (win_match(ps, w, ps->o.fade_blacklist, &w->cache_fblst))
    w->fade = false;
  else
    w->fade = ps->o.wintype_fade[w->window_type];
}

/**
 * Determine if a window should have shadow, and update things depending
 * on shadow state.
 */
static void
win_determine_shadow(session_t *ps, win *w) {
  bool shadow_new = w->shadow;

  if (IsViewable == w->a.map_state)
    shadow_new = (ps->o.wintype_shadow[w->window_type]
        && !win_match(ps, w, ps->o.shadow_blacklist, &w->cache_sblst)
        && !(ps->o.respect_prop_shadow));

  w->shadow = shadow_new;
}

static void
win_set_invert_color(session_t *ps, win *w, bool invert_color_new) {
  if (w->invert_color == invert_color_new) return;

  w->invert_color = invert_color_new;

  add_damage_win(ps, w);
}

/**
 * Determine if a window should have color inverted.
 */
static void
win_determine_invert_color(session_t *ps, win *w) {
  bool invert_color_new = w->invert_color;

  if (UNSET != w->invert_color_force)
    invert_color_new = w->invert_color_force;
  else if (IsViewable == w->a.map_state)
    invert_color_new = win_match(ps, w, ps->o.invert_color_list,
        &w->cache_ivclst);

  win_set_invert_color(ps, w, invert_color_new);
}

static void
win_set_blur_background(session_t *ps, win *w, bool blur_background_new) {
  if (w->blur_background == blur_background_new) return;

  w->blur_background = blur_background_new;

  // Only consider window damaged if it's previously painted with background
  // blurred
  if (!w->solid || (ps->o.blur_background_frame))
    add_damage_win(ps, w);
}

/**
 * Determine if a window should have background blurred.
 */
static void
win_determine_blur_background(session_t *ps, win *w) {
  if (IsViewable != w->a.map_state)
    return;

  bool blur_background_new = ps->o.blur_background
    && !win_match(ps, w, ps->o.blur_background_blacklist, &w->cache_bbblst);

  win_set_blur_background(ps, w, blur_background_new);
}

/**
 * Update window opacity according to opacity rules.
 */
static void
win_update_opacity_rule(session_t *ps, win *w) {
  if (IsViewable != w->a.map_state)
    return;

#ifdef CONFIG_C2
  // If long is 32-bit, unfortunately there's no way could we express "unset",
  // so we just entirely don't distinguish "unset" and OPAQUE
  double opacity = 100.0;
  void *val = NULL;
  if (c2_matchd(ps, w, ps->o.opacity_rules, &w->cache_oparule, &val))
    opacity = ((double) (long) val);
#endif
}

/**
 * Function to be called on window type changes.
 */
static void
win_on_wtype_change(session_t *ps, win *w) {
  win_determine_shadow(ps, w);
  win_determine_fade(ps, w);
  win_update_focused(ps, w);
  if (ps->o.invert_color_list)
    win_determine_invert_color(ps, w);
  if (ps->o.opacity_rules)
    win_update_opacity_rule(ps, w);
}

/**
 * Function to be called on window data changes.
 */
static void
win_on_factor_change(session_t *ps, win *w) {
  if (ps->o.shadow_blacklist)
    win_determine_shadow(ps, w);
  if (ps->o.fade_blacklist)
    win_determine_fade(ps, w);
  if (ps->o.invert_color_list)
    win_determine_invert_color(ps, w);
  if (ps->o.focus_blacklist)
    win_update_focused(ps, w);
  if (ps->o.blur_background_blacklist)
    win_determine_blur_background(ps, w);
  if (ps->o.opacity_rules)
    win_update_opacity_rule(ps, w);
  if (IsViewable == w->a.map_state && ps->o.paint_blacklist)
    w->paint_excluded = win_match(ps, w, ps->o.paint_blacklist,
        &w->cache_pblst);
  if (IsViewable == w->a.map_state && ps->o.unredir_if_possible_blacklist)
    w->unredir_if_possible_excluded = win_match(ps, w,
        ps->o.unredir_if_possible_blacklist, &w->cache_uipblst);
}

/**
 * Update cache data in struct _win that depends on window size.
 */
static void
calc_win_size(session_t *ps, win *w) {
  w->widthb = w->a.width + w->a.border_width * 2;
  w->heightb = w->a.height + w->a.border_width * 2;
  w->flags |= WFLAG_SIZE_CHANGE;
}

/**
 * Update window type.
 */
static void
win_upd_wintype(session_t *ps, win *w) {
  const wintype_t wtype_old = w->window_type;

  // Detect window type here
  w->window_type = wid_get_prop_wintype(ps, w->client_win);

  // Conform to EWMH standard, if _NET_WM_WINDOW_TYPE is not present, take
  // override-redirect windows or windows without WM_TRANSIENT_FOR as
  // _NET_WM_WINDOW_TYPE_NORMAL, otherwise as _NET_WM_WINDOW_TYPE_DIALOG.
  if (WINTYPE_UNKNOWN == w->window_type) {
    if (w->a.override_redirect
        || !wid_has_prop(ps, w->client_win, ps->atoms.atom_transient))
      w->window_type = WINTYPE_NORMAL;
    else
      w->window_type = WINTYPE_DIALOG;
  }

  if (w->window_type != wtype_old) {
      win_on_wtype_change(ps, w);
  }
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
  w->client_win = client;

  // If the window isn't mapped yet, stop here, as the function will be
  // called in map_win()
  if (IsViewable != w->a.map_state)
    return;

  XSelectInput(ps->dpy, client,
      determine_evmask(ps, client, WIN_EVMODE_CLIENT));

  // Make sure the XSelectInput() requests are sent
  XFlush(ps->dpy);

  win_upd_wintype(ps, w);

  // Get frame widths. The window is in damaged area already.
  get_frame_extents(ps, w, client);

  // Get window group
  if (ps->o.track_leader)
    win_update_leader(ps, w);

  // Get window name and class if we are tracking them
  if (ps->o.track_wdata) {
    win_get_name(ps, w);
    win_get_class(ps, w);
    win_get_role(ps, w);
  }

  // Update everything related to conditions
  win_on_factor_change(ps, w);

  // Update window focus state
  win_update_focused(ps, w);
}

/**
 * Unmark current client window of a window.
 *
 * @param ps current session
 * @param w struct _win of the parent window
 */
static void
win_unmark_client(session_t *ps, win *w) {
  Window client = w->client_win;

  w->client_win = None;

  // Recheck event mask
  XSelectInput(ps->dpy, client,
      determine_evmask(ps, client, WIN_EVMODE_UNKNOWN));
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

  // Look for the client window

  // Always recursively look for a window with WM_STATE, as Fluxbox
  // sets override-redirect flags on all frame windows.
  Window cw = find_client_win(ps, w->id);
#ifdef DEBUG_CLIENTWIN
  if (cw)
    printf_dbgf("(%#010lx): client %#010lx\n", w->id, cw);
#endif
  // Set a window's client window to itself if we couldn't find a
  // client window
  if (!cw) {
    cw = w->id;
    w->wmwin = !w->a.override_redirect;
#ifdef DEBUG_CLIENTWIN
    printf_dbgf("(%#010lx): client self (%s)\n", w->id,
        (w->wmwin ? "wmwin": "override-redirected"));
#endif
  }

  // Unmark the old one
  if (w->client_win && w->client_win != cw)
    win_unmark_client(ps, w);

  // Mark the new one
  win_mark_client(ps, w, cw);
}

static bool
add_win(session_t *ps, Window id) {
  const static win win_def = {
    .next = -1,
    .prev_trans = NULL,
    .next_trans = NULL,

    .id = None,
    .a = { },
    .face = NULL,
    .state = STATE_INVISIBLE,
#ifdef CONFIG_XINERAMA
    .xinerama_scr = -1,
#endif
    .pictfmt = NULL,
    .damaged = false,
    .damage = None,
    .border_size = None,
    .flags = 0,
    .need_configure = false,
    .queue_configure = { },
    .reg_ignore = None,
    .widthb = 0,
    .heightb = 0,
    .destroyed = false,
    .to_paint = false,
    .in_openclose = false,

    .client_win = None,
    .window_type = WINTYPE_UNKNOWN,
    .wmwin = false,
    .leader = None,
    .cache_leader = None,

    .focused = false,
    .focused_force = UNSET,

    .name = NULL,
    .class_instance = NULL,
    .class_general = NULL,
    .role = NULL,
    .cache_sblst = NULL,
    .cache_fblst = NULL,
    .cache_fcblst = NULL,
    .cache_ivclst = NULL,
    .cache_bbblst = NULL,
    .cache_oparule = NULL,

    .opacity = 0.0,
	.fadeTime = 0.0,
	.fadeDuration = -1.0,

    .fade = false,
    .fade_force = UNSET,
    .fade_callback = NULL,

    .frame_extents = MARGIN_INIT,

    .shadow = false,

    .dim = false,

    .invert_color = false,
    .invert_color_force = UNSET,

    .blur_background = false,

    .stencil = {0},
    .stencil_damaged = true,
  };

  // Reject overlay window and already added windows
  if (id == ps->overlay || find_win(ps, id)) {
    return false;
  }

  // Allocate and initialize the new win structure
  win_id slot;
  swiss_putBack(&ps->win_list, &win_def, &slot);
  win* new = swiss_get(&ps->win_list, slot);

#ifdef DEBUG_EVENTS
  printf_dbgf("(%#010lx): %p\n", id, new);
#endif

  if (!new) {
    printf_errf("(%#010lx): Failed to allocate memory for the new window.", id);
    return false;
  }

  // Fill structure
  new->id = id;

  set_ignore_next(ps);
  if (!XGetWindowAttributes(ps->dpy, id, &new->a)
      || IsUnviewable == new->a.map_state) {
      // Failed to get window attributes probably means the window is gone
      // already. IsUnviewable means the window is already reparented
      // elsewhere.
      swiss_remove(&ps->win_list, slot);
    return false;
  }

  fetch_shaped_window_face(ps, new);

  // Notify compton when the shape of a window changes
  if (ps->shape_exists) {
      // It will stop when the window is destroyed
      XShapeSelectInput(ps->dpy, new->id, ShapeNotifyMask);
  }

  // Delay window mapping
  int map_state = new->a.map_state;
  assert(IsViewable == map_state || IsUnmapped == map_state);
  new->a.map_state = IsUnmapped;

  if (InputOutput == new->a.class) {
      // Get window picture format
      new->pictfmt = XRenderFindVisualFormat(ps->dpy, new->a.visual);

      // Create Damage for window
      set_ignore_next(ps);
      new->damage = XDamageCreate(ps->dpy, id, XDamageReportNonEmpty);
  }

  calc_win_size(ps, new);

  if(!wd_init(&new->drawable, &ps->psglx->xcontext, new->id)) {
      printf_errf("Failed initializing window drawable");
      free(new);
      return false;
  }

  if(!blur_cache_init(&new->glx_blur_cache)) {
      printf_errf("Failed initializing window blur");
      wd_delete(&new->drawable);
      free(new);
      return false;
  }

  // @PERFORMANCE @MEMORY: Theres no reason so have a shadow cache for windows
  // that don't have a shadow. But to make it easy to program i'll just have it
  // for all windows for now
  if(shadow_cache_init(&new->shadow_cache) != 0){
      printf_errf("Failed initializing window shadow");

      blur_cache_delete(&new->glx_blur_cache);
      wd_delete(&new->drawable);
      free(new);

      return false;
  }

  // @PERFORMANCE @MEMORY: We only need stencil buffers for ARGB windows, but
  // for now we'll just have it for all windows
  if(renderbuffer_stencil_init(&new->stencil, NULL) != 0){
      printf_errf("Failed initializing window stencil");

      shadow_cache_delete(&new->shadow_cache);
      blur_cache_delete(&new->glx_blur_cache);
      wd_delete(&new->drawable);
      free(new);

      return false;
  }

  vector_putBack(&ps->order, &slot);

#ifdef CONFIG_DBUS
  // Send D-Bus signal
  if (ps->o.dbus) {
    cdbus_ev_win_added(ps, new);
  }
#endif

  if (IsViewable == map_state) {
      map_win(ps, swiss_indexOfPointer(&ps->win_list, new));
  }

  return true;
}

static void
restack_win(session_t *ps, win *w, Window new_above) {

    update_reg_ignore_expire(ps, w);

    win_id w_id = swiss_indexOfPointer(&ps->win_list, w);

    struct _win* w_above = find_win(ps, new_above);
    assert(w_above != NULL);
    win_id above_id = swiss_indexOfPointer(&ps->win_list, w_above);

    size_t w_loc;
    size_t above_loc;

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

    ps->root_width = ce->width;
    ps->root_height = ce->height;

    rebuild_screen_reg(ps);

    // Re-redirect screen if required
    if (ps->o.reredir_on_root_change && ps->redirected) {
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

    force_repaint(ps);

    return;
  }

  // Other window changes
  win *w = find_win(ps, ce->window);

  if (!w)
    return;

  if (w->a.map_state == IsUnmapped) {
    /* save the configure event for when the window maps */
    w->need_configure = true;
    w->queue_configure = *ce;
    restack_win(ps, w, ce->above);
  } else {
    if (!(w->need_configure)) {
      restack_win(ps, w, ce->above);
    }

    bool factor_change = false;

    // Windows restack (including window restacks happened when this
    // window is not mapped) could mess up all reg_ignore
    ps->reg_ignore_expire = true;

    w->need_configure = false;

    // If window geometry did not change, don't free extents here
    if (w->a.x != ce->x || w->a.y != ce->y
        || w->a.width != ce->width || w->a.height != ce->height
        || w->a.border_width != ce->border_width) {
      factor_change = true;
      free_region(ps, &w->border_size);
    }

    w->a.x = ce->x;
    w->a.y = ce->y;

    if (w->a.width != ce->width || w->a.height != ce->height
            || w->a.border_width != ce->border_width) {
        if(ps->redirected) {
            if(!wd_unbind(&w->drawable)) {
                printf_errf("Failed unbinding window on resize");
            }
            if(!wd_bind(&w->drawable)) {
                printf_errf("Failed rebinding window on resize");
            }
            w->stencil_damaged = true;

            if(!blur_cache_resize(&w->glx_blur_cache, &w->drawable.texture.size)) {
                printf_errf("Failed resizing window blur %s", w->name);
                return;
            }
        }
    }

    if (w->a.width != ce->width || w->a.height != ce->height
        || w->a.border_width != ce->border_width) {
      w->a.width = ce->width;
      w->a.height = ce->height;
      w->a.border_width = ce->border_width;

      if(w->a.border_width == 0)
          w->has_frame = win_has_frame(w);

      calc_win_size(ps, w);
    }

    if (factor_change) {
      cxinerama_win_upd_scr(ps, w);
      win_on_factor_change(ps, w);
    }
  }

  // override_redirect flag cannot be changed after window creation, as far
  // as I know, so there's no point to re-match windows here.
  w->a.override_redirect = ce->override_redirect;
}

static void
circulate_win(session_t *ps, XCirculateEvent *ce) {
    win *w = find_win(ps, ce->window);

    if (!w) return;

    size_t w_loc = vector_find_uint64(&ps->order, swiss_indexOfPointer(&ps->win_list, w));
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
    struct _win* w = swiss_get(&ps->win_list, wid);
    if (w == ps->active_win)
        ps->active_win = NULL;

    if(w->prev_trans != NULL)
        w->prev_trans->next_trans = w->next_trans;
    if(w->next_trans != NULL)
        w->next_trans->prev_trans = w->prev_trans;

    size_t order_index = vector_find_uint64(&ps->order, wid);
    vector_remove(&ps->order, order_index);

    // Unhook the window from the legacy linked list
    size_t index = 0;
    struct _win* p = swiss_getFirst(&ps->win_list, &index);
    while(p != NULL) {
        if (p->next == wid) {
            p->next = w->next;
            break;
        }
        p = swiss_getNext(&ps->win_list, &index);
    }

    swiss_remove(&ps->win_list, wid);
}

static void destroy_win(session_t *ps, struct _win* w) {
    if (w) {
        w->destroyed = true;

        // You can only destroy a window that is already hiding or invisible
        assert(w->state == STATE_HIDING || w->state == STATE_INVISIBLE);

        w->state = STATE_DESTROYING;

#ifdef CONFIG_DBUS
        // Send D-Bus signal
        if (ps->o.dbus) {
            cdbus_ev_win_destroyed(ps, w);
        }
#endif
    }
}

static inline void
root_damaged(session_t *ps) {
  if (ps->root_texture.bound) {
    xtexture_unbind(&ps->root_texture);
  }
  get_root_tile(ps);

  // Mark screen damaged
  force_repaint(ps);
}

static void
damage_win(session_t *ps, XDamageNotifyEvent *de) {
  /*
  if (ps->root == de->drawable) {
    root_damaged();
    return;
  } */


  win *w = find_win(ps, de->drawable);

  if (!w) return;

  if (IsViewable != w->a.map_state)
    return;

  //Reset the XDamage region, so we continue to recieve new damage
  XDamageSubtract(ps->dpy, w->damage, None, None);

  w->damaged = true;

  w->stencil_damaged = true;

  // Damage all the bg blurs of the windows on top of this one
  for (win *t = w; t; t = t->prev_trans) {
      // @CLEANUP: Ideally we should just recalculate the blur right now. We need
      // to render the windows behind this though, and that takes time. For now we
      // just do it indirectly
      if(win_overlap(w, t))
          t->glx_blur_cache.damaged = true;
  }
}

/**
 * Xlib error handler function.
 */
static int
xerror(Display __attribute__((unused)) *dpy, XErrorEvent *ev) {
  session_t * const ps = ps_g;

  int o = 0;
  const char *name = "Unknown";

  if (should_ignore(ps, ev->serial)) {
    return 0;
  }

  if (ev->request_code == ps->composite_opcode
      && ev->minor_code == X_CompositeRedirectSubwindows) {
    fprintf(stderr, "Another composite manager is already running\n");
    exit(1);
  }

#define CASESTRRET2(s)   case s: name = #s; break

  o = ev->error_code - ps->xfixes_error;
  switch (o) {
    CASESTRRET2(BadRegion);
  }

  o = ev->error_code - ps->damage_error;
  switch (o) {
    CASESTRRET2(BadDamage);
  }

  o = ev->error_code - ps->render_error;
  switch (o) {
    CASESTRRET2(BadPictFormat);
    CASESTRRET2(BadPicture);
    CASESTRRET2(BadPictOp);
    CASESTRRET2(BadGlyphSet);
    CASESTRRET2(BadGlyph);
  }

  if (ps->glx_exists) {
      o = ev->error_code - ps->glx_error;
      switch (o) {
          CASESTRRET2(GLX_BAD_SCREEN);
          CASESTRRET2(GLX_BAD_ATTRIBUTE);
          CASESTRRET2(GLX_NO_EXTENSION);
          CASESTRRET2(GLX_BAD_VISUAL);
          CASESTRRET2(GLX_BAD_CONTEXT);
          CASESTRRET2(GLX_BAD_VALUE);
          CASESTRRET2(GLX_BAD_ENUM);
      }
  }

#ifdef CONFIG_XSYNC
  if (ps->xsync_exists) {
    o = ev->error_code - ps->xsync_error;
    switch (o) {
      CASESTRRET2(XSyncBadCounter);
      CASESTRRET2(XSyncBadAlarm);
      CASESTRRET2(XSyncBadFence);
    }
  }
#endif

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

  // print_backtrace();

  return 0;
}

static void
expose_root(session_t *ps, XRectangle *rects, int nrects) {
  XserverRegion region = XFixesCreateRegion(ps->dpy, rects, nrects);
  add_damage(ps, region);
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
  winprop_t prop = wid_get_prop(ps, wid, aprop, 1L, XA_WINDOW, 32);

  // Return it
  if (prop.nitems) {
    p = *prop.data.p32;
  }

  free_winprop(&prop);

  return p;
}

/**
 * Update focused state of a window.
 */
static void win_update_focused(session_t *ps, win *w) {
    bool oldFocused = w->focused;

    // If the window has forced focus we don't need any further calculation
    if (UNSET != w->focused_force) {
        w->focused = w->focused_force;

        if(w->focused != oldFocused)
            w->focus_changed = true;

        return;
    }

    w->focused = win_is_focused_real(ps, w);

    if(ps->o.wintype_focus[w->window_type])
        w->focused = true;
    else if(ps->o.mark_wmwin_focused && w->wmwin)
        w->focused = true;
    else if(ps->active_win == w)
        w->focused = true;
    else if(ps->o.mark_ovredir_focused
            && w->id == w->client_win && !w->wmwin) {
        w->focused = true;
    } else if(IsViewable == w->a.map_state
            && win_match(ps, w, ps->o.focus_blacklist, &w->cache_fcblst)) {
        w->focused = true;
    } else if (ps->o.track_leader && ps->active_leader
            && win_get_leader(ps, w) == ps->active_leader) {
        // If window grouping detection is enabled, mark the window active if
        // its group is
        w->focused = true;
    }

    if(w->focused != oldFocused)
        w->focus_changed = true;
}

/**
 * Set real focused state of a window.
 */
static void win_set_focused(session_t *ps, win *w) {
    // Unmapped windows will have their focused state reset on map
    if (IsUnmapped == w->a.map_state)
        return;

    if (win_is_focused_real(ps, w) == true) return;

    if (ps->active_win) {
        assert(ps->active_win->a.map_state != IsUnmapped);

        win* old_active = ps->active_win;
        ps->active_win = NULL;

        Window leader = win_get_leader(ps, old_active);
        if (!win_is_focused_real(ps, w) && leader && leader == ps->active_leader && !group_is_focused(ps, leader)) {
            ps->active_leader = None;

            //Update the focused state of all other windows lead by leader
            size_t index = 0;
            win* t = swiss_getFirst(&ps->win_list, &index);
            while(t != NULL) {
                if (win_get_leader(ps, t) == leader)
                    win_update_focused(ps, t);
                t = swiss_getNext(&ps->win_list, &index);
            }
        }

        win_update_focused(ps, old_active);
    }

    ps->active_win = w;

    assert(win_is_focused_real(ps, w) == true);

    win_update_focused(ps, w);

    // If window grouping detection is enabled
    if (ps->o.track_leader) {
        Window leader = win_get_leader(ps, w);

        // If the window gets focused, replace the old active_leader
        if (win_is_focused_real(ps, w) && leader != ps->active_leader) {
            Window active_leader_old = ps->active_leader;

            ps->active_leader = leader;

            //Update the focused state of all other windows lead by old leader
            size_t index = 0;
            win* t = swiss_getFirst(&ps->win_list, &index);
            while(t != NULL) {

                if (win_get_leader(ps, t) == active_leader_old)
                    win_update_focused(ps, t);

                if (win_get_leader(ps, t) == leader)
                    win_update_focused(ps, t);

                t = swiss_getNext(&ps->win_list, &index);
            }
        }
    }

    // Update everything related to conditions
    win_on_factor_change(ps, w);

#ifdef CONFIG_DBUS
    // Send D-Bus signal
    if (ps->o.dbus) {
        if (win_is_focused_real(ps, w))
            cdbus_ev_win_focusin(ps, w);
        else
            cdbus_ev_win_focusout(ps, w);
    }
#endif
}

/**
 * Update leader of a window.
 */
static void
win_update_leader(session_t *ps, win *w) {
  Window leader = None;

  // Read the leader properties
  if (ps->o.detect_transient && !leader)
    leader = wid_get_prop_window(ps, w->client_win, ps->atoms.atom_transient);

  if (ps->o.detect_client_leader && !leader)
    leader = wid_get_prop_window(ps, w->client_win, ps->atoms.atom_client_leader);

  win_set_leader(ps, w, leader);

#ifdef DEBUG_LEADER
  printf_dbgf("(%#010lx): client %#010lx, leader %#010lx, cache %#010lx\n", w->id, w->client_win, w->leader, win_get_leader(ps, w));
#endif
}

/**
 * Set leader of a window.
 */
static void
win_set_leader(session_t *ps, win *w, Window nleader) {
  // If the leader changes
  if (w->leader != nleader) {
    Window cache_leader_old = win_get_leader(ps, w);

    w->leader = nleader;

    // Forcefully do this to deal with the case when a child window
    // gets mapped before parent, or when the window is a waypoint
    clear_cache_win_leaders(ps);

    // Update the old and new window group and active_leader if the window
    // could affect their state.
    Window cache_leader = win_get_leader(ps, w);
    if (win_is_focused_real(ps, w) && cache_leader_old != cache_leader) {
      ps->active_leader = cache_leader;

      //Update the focused state of all other windows lead by leader
      size_t prev_index = 0;
      win* t = swiss_getFirst(&ps->win_list, &prev_index);
      while(t != NULL) {

          if (win_get_leader(ps, t) == cache_leader_old)
              win_update_focused(ps, t);

          if (win_get_leader(ps, t) == cache_leader)
              win_update_focused(ps, t);

          t = swiss_getNext(&ps->win_list, &prev_index);
      }
    }
    // Otherwise, at most the window itself is affected
    else {
      win_update_focused(ps, w);
    }

    // Update everything related to conditions
    win_on_factor_change(ps, w);
  }
}

/**
 * Internal function of win_get_leader().
 */
static Window
win_get_leader_raw(session_t *ps, win *w, int recursions) {
  // Rebuild the cache if needed
  if (!w->cache_leader && (w->client_win || w->leader)) {
    // Leader defaults to client window
    if (!(w->cache_leader = w->leader))
      w->cache_leader = w->client_win;

    // If the leader of this window isn't itself, look for its ancestors
    if (w->cache_leader && w->cache_leader != w->client_win) {
      win *wp = find_toplevel(ps, w->cache_leader);
      if (wp) {
        // Dead loop?
        if (recursions > WIN_GET_LEADER_MAX_RECURSION)
          return None;

        w->cache_leader = win_get_leader_raw(ps, wp, recursions + 1);
      }
    }
  }

  return w->cache_leader;
}

/**
 * Get the value of a text property of a window.
 */
bool
wid_get_text_prop(session_t *ps, Window wid, Atom prop,
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

  // Can't do anything if there's no client window
  if (!w->client_win)
    return false;

  // Get the property
  ret = func_wid_get_prop_str(ps, w->client_win, tgt);

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

  // Can't do anything if there's no client window
  if (!w->client_win)
    return false;

  // Free and reset old strings
  free(w->class_instance);
  free(w->class_general);
  w->class_instance = NULL;
  w->class_general = NULL;

  // Retrieve the property string list
  if (!wid_get_text_prop(ps, w->client_win, ps->atoms.atom_class, &strlst, &nstr))
    return false;

  // Copy the strings if successful
  w->class_instance = mstrcpy(strlst[0]);

  if (nstr > 1)
    w->class_general = mstrcpy(strlst[1]);

  XFreeStringList(strlst);

#ifdef DEBUG_WINDATA
  printf_dbgf("(%#010lx): client = %#010lx, "
      "instance = \"%s\", general = \"%s\"\n",
      w->id, w->client_win, w->class_instance, w->class_general);
#endif

  return true;
}

/**
 * Force a full-screen repaint.
 */
void
force_repaint(session_t *ps) {
  assert(ps->screen_reg);
  XserverRegion reg = None;
  if (ps->screen_reg && (reg = copy_region(ps, ps->screen_reg))) {
    ps->skip_poll = true;
    add_damage(ps, reg);
  }
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
    win_determine_fade(ps, w);
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
    win_update_focused(ps, w);
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
    win_determine_invert_color(ps, w);
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

    if (!ps->o.use_ewmh_active_win) {
        // Start listening to FocusChange events
        size_t index = 0;
        win* w = swiss_getFirst(&ps->win_list, &index);
        while(w != NULL) {

            if (IsViewable == w->a.map_state)
                XSelectInput(ps->dpy, w->id,
                        determine_evmask(ps, w->id, WIN_EVMODE_FRAME));

            w = swiss_getNext(&ps->win_list, &index);
        }

        // Recheck focus
        recheck_focus(ps);
    }
}

/**
 * Set no_fading_openclose option.
 */
void
opts_set_no_fading_openclose(session_t *ps, bool newval) {
    if (newval != ps->o.no_fading_openclose) {
        ps->o.no_fading_openclose = newval;

        size_t index = 0;
        win* w = swiss_getFirst(&ps->win_list, &index);
        while(w != NULL) {
            win_determine_fade(ps, w);

            w = swiss_getNext(&ps->win_list, &index);
        }

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
    CASESTRRET(FocusIn);
    CASESTRRET(FocusOut);
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

  if (isdamagenotify(ps, ev))
    return "Damage";

  if (ps->shape_exists && ev->type == ps->shape_event)
    return "ShapeNotify";

#ifdef CONFIG_XSYNC
  if (ps->xsync_exists) {
    int o = ev->type - ps->xsync_event;
    switch (o) {
      CASESTRRET(XSyncCounterNotify);
      CASESTRRET(XSyncAlarmNotify);
    }
  }
#endif

  sprintf(buf, "Event %d", ev->type);

  return buf;
}

static Window
ev_window(session_t *ps, XEvent *ev) {
  switch (ev->type) {
    case FocusIn:
    case FocusOut:
      return ev->xfocus.window;
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
      if (isdamagenotify(ps, ev)) {
        return ((XDamageNotifyEvent *)ev)->drawable;
      }

      if (ps->shape_exists && ev->type == ps->shape_event) {
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

/**
 * Determine whether we should respond to a <code>FocusIn/Out</code>
 * event.
 */
/*
inline static bool
ev_focus_accept(XFocusChangeEvent *ev) {
  return NotifyNormal == ev->mode || NotifyUngrab == ev->mode;
}
*/

static inline void
ev_focus_in(session_t *ps, XFocusChangeEvent *ev) {
#ifdef DEBUG_EVENTS
  ev_focus_report(ev);
#endif

  recheck_focus(ps);
}

inline static void
ev_focus_out(session_t *ps, XFocusChangeEvent *ev) {
#ifdef DEBUG_EVENTS
  ev_focus_report(ev);
#endif

  recheck_focus(ps);
}

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
    destroy_win(ps, w);
}

static void ev_map_notify(session_t *ps, XMapEvent *ev) {
    win *w = find_win(ps, ev->window);
    if(w != NULL) {
        map_win(ps, swiss_indexOfPointer(&ps->win_list, w));
    }
}

inline static void
ev_unmap_notify(session_t *ps, XUnmapEvent *ev) {
  win *w = find_win(ps, ev->window);

  if (w)
    unmap_win(ps, w);
}

inline static void
ev_reparent_notify(session_t *ps, XReparentEvent *ev) {
#ifdef DEBUG_EVENTS
  printf_dbg("  { new_parent: %#010lx, override_redirect: %d }\n",
      ev->parent, ev->override_redirect);
#endif

  if (ev->parent == ps->root) {
    add_win(ps, ev->window);
  } else {
    win *w = find_win(ps, ev->window);
    destroy_win(ps, w);

    // Reset event mask in case something wrong happens
    XSelectInput(ps->dpy, ev->window,
        determine_evmask(ps, ev->window, WIN_EVMODE_UNKNOWN));

    // Check if the window is an undetected client window
    // Firstly, check if it's a known client window
    if (!find_toplevel(ps, ev->window)) {
      // If not, look for its frame window
      win *w_top = find_toplevel2(ps, ev->parent);
      // If found, and the client window has not been determined, or its
      // frame may not have a correct client, continue
      if (w_top && (!w_top->client_win
            || w_top->client_win == w_top->id)) {
        // If it has WM_STATE, mark it the client window
        if (wid_has_prop(ps, ev->window, ps->atoms.atom_client)) {
          w_top->wmwin = false;
          win_unmark_client(ps, w_top);
          win_mark_client(ps, w_top, ev->window);
        }
        // Otherwise, watch for WM_STATE on it
        else {
          XSelectInput(ps->dpy, ev->window,
              determine_evmask(ps, ev->window, WIN_EVMODE_UNKNOWN)
              | PropertyChangeMask);
        }
      }
    }
  }
}

inline static void
ev_circulate_notify(session_t *ps, XCirculateEvent *ev) {
  circulate_win(ps, ev);
}

inline static void
ev_expose(session_t *ps, XExposeEvent *ev) {
  if (ev->window == ps->root || (ps->overlay && ev->window == ps->overlay)) {
    int more = ev->count + 1;
    if (ps->n_expose == ps->size_expose) {
      if (ps->expose_rects) {
        ps->expose_rects = realloc(ps->expose_rects,
          (ps->size_expose + more) * sizeof(XRectangle));
        ps->size_expose += more;
      } else {
        ps->expose_rects = malloc(more * sizeof(XRectangle));
        ps->size_expose = more;
      }
    }

    ps->expose_rects[ps->n_expose].x = ev->x;
    ps->expose_rects[ps->n_expose].y = ev->y;
    ps->expose_rects[ps->n_expose].width = ev->width;
    ps->expose_rects[ps->n_expose].height = ev->height;
    ps->n_expose++;

    if (ev->count == 0) {
      expose_root(ps, ps->expose_rects, ps->n_expose);
      ps->n_expose = 0;
    }
  }
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

  // Mark the window focused. No need to unfocus the previous one.
  if (w) win_set_focused(ps, w);
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
    if (ps->o.track_focus && ps->o.use_ewmh_active_win
        && ps->atoms.atom_ewmh_active_win == ev->atom) {
      update_ewmh_active_win(ps);
    }
    else {
      // Destroy the root "image" if the wallpaper probably changed
      for (int p = 0; background_props_str[p]; p++) {
        if (ev->atom == get_atom(ps, background_props_str[p])) {
          root_damaged(ps);
          break;
        }
      }
    }

    // Unconcerned about any other proprties on root window
    return;
  }

  // If WM_STATE changes
  if (ev->atom == ps->atoms.atom_client) {
    // Check whether it could be a client window
    if (!find_toplevel(ps, ev->window)) {
      // Reset event mask anyway
      XSelectInput(ps->dpy, ev->window,
          determine_evmask(ps, ev->window, WIN_EVMODE_UNKNOWN));

      win *w_top = find_toplevel2(ps, ev->window);
      // Initialize client_win as early as possible
      if (w_top && (!w_top->client_win || w_top->client_win == w_top->id)
          && wid_has_prop(ps, ev->window, ps->atoms.atom_client)) {
        w_top->wmwin = false;
        win_unmark_client(ps, w_top);
        win_mark_client(ps, w_top, ev->window);
      }
    }
  }

  // If _NET_WM_WINDOW_TYPE changes... God knows why this would happen, but
  // there are always some stupid applications. (#144)
  if (ev->atom == ps->atoms.atom_win_type) {
    win *w = NULL;
    if ((w = find_toplevel(ps, ev->window)))
      win_upd_wintype(ps, w);
  }

  // If frame extents property changes
  if (ev->atom == ps->atoms.atom_frame_extents) {
    win *w = find_toplevel(ps, ev->window);
    if (w) {
      get_frame_extents(ps, w, ev->window);
      // If frame extents change, the window needs repaint
      add_damage_win(ps, w);
    }
  }

  // If name changes
  if (ps->o.track_wdata
      && (ps->atoms.atom_name == ev->atom || ps->atoms.atom_name_ewmh == ev->atom)) {
    win *w = find_toplevel(ps, ev->window);
    if (w && 1 == win_get_name(ps, w)) {
      win_on_factor_change(ps, w);
    }
  }

  // If class changes
  if (ps->o.track_wdata && ps->atoms.atom_class == ev->atom) {
    win *w = find_toplevel(ps, ev->window);
    if (w) {
      win_get_class(ps, w);
      win_on_factor_change(ps, w);
    }
  }

  // If role changes
  if (ps->o.track_wdata && ps->atoms.atom_role == ev->atom) {
    win *w = find_toplevel(ps, ev->window);
    if (w && 1 == win_get_role(ps, w)) {
      win_on_factor_change(ps, w);
    }
  }

  // If a leader property changes
  if ((ps->o.detect_transient && ps->atoms.atom_transient == ev->atom)
      || (ps->o.detect_client_leader && ps->atoms.atom_client_leader == ev->atom)) {
    win *w = find_toplevel(ps, ev->window);
    if (w) {
      win_update_leader(ps, w);
    }
  }

  // Check for other atoms we are tracking
  for (latom_t *platom = ps->track_atom_lst; platom; platom = platom->next) {
    if (platom->atom == ev->atom) {
      win *w = find_win(ps, ev->window);
      if (!w)
        w = find_toplevel(ps, ev->window);
      if (w)
        win_on_factor_change(ps, w);
      break;
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

    /*
     * Empty border_size may indicated an
     * unmapped/destroyed window, in which case
     * seemingly BadRegion errors would be triggered
     * if we attempt to rebuild border_size
     */
    if (w->border_size) {
        // Mark the old border_size as damaged
        add_damage(ps, w->border_size);
    }

    fetch_shaped_window_face(ps, w);
    // We need to mark some damage
    // The blur isn't damaged, because it will be cut out by the new geometry
    w->stencil_damaged = true;
    //The shadow is damaged because the outline (and therefore the inner clip) has changed.
    w->shadow_damaged = true;

    update_reg_ignore_expire(ps, w);
}

/**
 * Handle ScreenChangeNotify events from X RandR extension.
 */
static void ev_screen_change_notify(session_t *ps, XRRScreenChangeNotifyEvent __attribute__((unused)) *ev) {
    if (ps->o.xinerama_shadow_crop)
        cxinerama_upd_scrs(ps);
}

#if defined(DEBUG_EVENTS) || defined(DEBUG_RESTACK)
/**
 * Get a window's name from window ID.
 */
static bool
ev_window_name(session_t *ps, Window wid, char **name) {
  bool to_free = false;

  *name = "";
  if (wid) {
    *name = "(Failed to get title)";
    if (ps->root == wid)
      *name = "(Root window)";
    else if (ps->overlay == wid)
      *name = "(Overlay)";
    else {
      win *w = find_win(ps, wid);
      if (!w)
        w = find_toplevel(ps, wid);

      if (w && w->name)
        *name = w->name;
      else if (!(w && w->client_win
            && (to_free = wid_get_name(ps, w->client_win, name))))
          to_free = wid_get_name(ps, wid, name);
    }
  }

  return to_free;
}
#endif

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
    case FocusIn:
      ev_focus_in(ps, (XFocusChangeEvent *)ev);
      break;
    case FocusOut:
      ev_focus_out(ps, (XFocusChangeEvent *)ev);
      break;
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
      ev_expose(ps, (XExposeEvent *)ev);
      break;
    case PropertyNotify:
      ev_property_notify(ps, (XPropertyEvent *)ev);
      break;
    default:
      if (ps->shape_exists && ev->type == ps->shape_event) {
        ev_shape_notify(ps, (XShapeEvent *) ev);
        break;
      }
      if (ps->randr_exists && ev->type == (ps->randr_event + RRScreenChangeNotify)) {
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
    "  Dim inactive windows. (0.0 - 1.0, defaults to 0)\n"
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
    "--mark-ovredir-focused\n"
    "  Mark windows that have no WM frame as active.\n"
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
    "  --unredir-if-possible, and possibly others. You need to turn this\n"
    "  on manually if you want to match against rounded_corners in\n"
    "  conditions.\n"
    "\n"
    "--detect-client-opacity\n"
    "  Detect _NET_WM_OPACITY on client windows, useful for window\n"
    "  managers not passing _NET_WM_OPACITY of client windows to frame\n"
    "  windows.\n"
    "\n"
    "--vsync vsync-method\n"
    "  Set VSync method. There are (up to) 5 VSync methods currently\n"
    "  available:\n"
    "    none = No VSync\n"
#undef WARNING
#ifndef CONFIG_VSYNC_DRM
#define WARNING WARNING_DISABLED
#else
#define WARNING
#endif
    "    drm = VSync with DRM_IOCTL_WAIT_VBLANK. May only work on some\n"
    "      (DRI-based) drivers." WARNING "\n"
#undef WARNING
#define WARNING
    "    opengl = Try to VSync with SGI_video_sync OpenGL extension. Only\n"
    "      work on some drivers." WARNING"\n"
    "    opengl-oml = Try to VSync with OML_sync_control OpenGL extension.\n"
    "      Only work on some drivers." WARNING"\n"
    "    opengl-swc = Try to VSync with SGI_swap_control OpenGL extension.\n"
    "      Only work on some drivers. Works only with GLX backend." WARNING "\n"
    "    opengl-mswc = Try to VSync with MESA_swap_control OpenGL\n"
    "      extension. Basically the same as opengl-swc above, except the\n"
    "      extension we use." WARNING "\n"
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
    "--use-ewmh-active-win\n"
    "  Use _NET_WM_ACTIVE_WINDOW on the root window to determine which\n"
    "  window is focused instead of using FocusIn/Out events.\n"
    "\n"
    "--respect-prop-shadow\n"
    "  Respect _COMPTON_SHADOW. This a prototype-level feature, which\n"
    "  you must not rely on.\n"
    "\n"
    "--unredir-if-possible\n"
    "  Unredirect all windows if a full-screen opaque window is\n"
    "  detected, to maximize performance for full-screen windows.\n"
    "\n"
    "--unredir-if-possible-delay ms\n"
    "  Delay before unredirecting the window, in milliseconds.\n"
    "  Defaults to 0.\n"
    "\n"
    "--unredir-if-possible-exclude condition\n"
    "  Conditions of windows that shouldn't be considered full-screen\n"
    "  for unredirecting screen.\n"
    "\n"
    "--focus-exclude condition\n"
    "  Specify a list of conditions of windows that should always be\n"
    "  considered focused.\n"
    "\n"
    "--inactive-dim-fixed\n"
    "  Use fixed inactive dim value.\n"
    "\n"
    "--detect-transient\n"
    "  Use WM_TRANSIENT_FOR to group windows, and consider windows in\n"
    "  the same group focused at the same time.\n"
    "\n"
    "--detect-client-leader\n"
    "  Use WM_CLIENT_LEADER to group windows, and consider windows in\n"
    "  the same group focused at the same time. WM_TRANSIENT_FOR has\n"
    "  higher priority if --detect-transient is enabled, too.\n"
    "\n"
    "--blur-background\n"
    "  Blur background of semi-transparent / ARGB windows. Bad in\n"
    "  performance. The switch name may change without prior\n"
    "  notifications.\n"
    "\n"
    "--blur-background-frame\n"
    "  Blur background of windows when the window frame is not opaque.\n"
    "  Implies --blur-background. Bad in performance. The switch name\n"
    "  may change.\n"
    "\n"
    "--blur-background-fixed\n"
    "  Use fixed blur strength instead of adjusting according to window\n"
    "  opacity.\n"
    "\n"
    "--blur-kern matrix\n"
    "  Specify the blur convolution kernel, with the following format:\n"
    "    WIDTH,HEIGHT,ELE1,ELE2,ELE3,ELE4,ELE5...\n"
    "  The element in the center must not be included, it will be forever\n"
    "  1.0 or changing based on opacity, depending on whether you have\n"
    "  --blur-background-fixed.\n"
    "  A 7x7 Gaussian blur kernel looks like:\n"
    "    --blur-kern '7,7,0.000003,0.000102,0.000849,0.001723,0.000849,0.000102,0.000003,0.000102,0.003494,0.029143,0.059106,0.029143,0.003494,0.000102,0.000849,0.029143,0.243117,0.493069,0.243117,0.029143,0.000849,0.001723,0.059106,0.493069,0.493069,0.059106,0.001723,0.000849,0.029143,0.243117,0.493069,0.243117,0.029143,0.000849,0.000102,0.003494,0.029143,0.059106,0.029143,0.003494,0.000102,0.000003,0.000102,0.000849,0.001723,0.000849,0.000102,0.000003'\n"
    "  Up to 4 blur kernels may be specified, separated with semicolon, for\n"
    "  multi-pass blur.\n"
    "  May also be one the predefined kernels: 3x3box (default), 5x5box,\n"
    "  7x7box, 3x3gaussian, 5x5gaussian, 7x7gaussian, 9x9gaussian,\n"
    "  11x11gaussian.\n"
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
#undef WARNING
#ifndef CONFIG_XINERAMA
#define WARNING WARNING_DISABLED
#else
#define WARNING
#endif
    "\n"
    "--xinerama-shadow-crop\n"
    "  Crop shadow of a window fully on a particular Xinerama screen to the\n"
    "  screen." WARNING "\n"
    "\n"
#undef WARNING
#define WARNING
    "--glx-no-stencil\n"
    "  GLX backend: Avoid using stencil buffer. Might cause issues\n"
    "  when rendering transparent content. My tests show a 15% performance\n"
    "  boost.\n"
    "\n"
    "--glx-copy-from-front\n"
    "  GLX backend: Copy unmodified regions from front buffer instead of\n"
    "  redrawing them all. My tests with nvidia-drivers show a 5% decrease\n"
    "  in performance when the whole screen is modified, but a 30% increase\n"
    "  when only 1/4 is. My tests on nouveau show terrible slowdown. Could\n"
    "  work with --glx-swap-method but not --glx-use-copysubbuffermesa.\n"
    "\n"
    "--glx-use-copysubbuffermesa\n"
    "  GLX backend: Use MESA_copy_sub_buffer to do partial screen update.\n"
    "  My tests on nouveau shows a 200% performance boost when only 1/4 of\n"
    "  the screen is updated. May break VSync and is not available on some\n"
    "  drivers. Overrides --glx-copy-from-front.\n"
    "\n"
    "--glx-no-rebind-pixmap\n"
    "  GLX backend: Avoid rebinding pixmap on window damage. Probably\n"
    "  could improve performance on rapid window content changes, but is\n"
    "  known to break things on some drivers (LLVMpipe, xf86-video-intel,\n"
    "  etc.).\n"
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
    "--glx-use-gpushader4\n"
    "  GLX backend: Use GL_EXT_gpu_shader4 for some optimization on blur\n"
    "  GLSL code. My tests on GTX 670 show no noticeable effect.\n"
    "\n"
    "--xrender-sync\n"
    "  Attempt to synchronize client applications' draw calls with XSync(),\n"
    "  used on GLX backend to ensure up-to-date window content is painted.\n"
#undef WARNING
#ifndef CONFIG_XSYNC
#define WARNING WARNING_DISABLED
#else
#define WARNING
#endif
    "\n"
    "--xrender-sync-fence\n"
    "  Additionally use X Sync fence to sync clients' draw calls. Needed\n"
    "  on nvidia-drivers with GLX backend for some users." WARNING "\n"
    "\n"
    "--glx-fshader-win shader\n"
    "  GLX backend: Use specified GLSL fragment shader for rendering window\n"
    "  contents.\n"
    "\n"
    "--force-win-blend\n"
    "  Force all windows to be painted with blending. Useful if you have a\n"
    "  --glx-fshader-win that could turn opaque pixels transparent.\n"
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

  // Unredirect the window if it's redirected, just in case
  if (ps->redirected)
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
          get_atom(ps, "_NET_WM_PID"), XA_CARDINAL, 32, PropModeReplace,
          (unsigned char *) &pid, 1)) {
      printf_errf("(): Failed to set _NET_WM_PID.");
    }
  }

  // Set COMPTON_VERSION
  if (!wid_set_text_prop(ps, ps->reg_win, get_atom(ps, "COMPTON_VERSION"), COMPTON_VERSION)) {
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
    XSetSelectionOwner(ps->dpy, get_atom(ps, buf), ps->reg_win, 0);
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

  // -D (fade_delta)
  if (lcfg_lookup_int(&cfg, "fade-delta", &ival))
    ps->o.fade_delta = ival;
  // -i (inactive_opacity)
  if (config_lookup_float(&cfg, "inactive-opacity", &dval))
    ps->o.inactive_opacity = normalize_d(dval) * 100.0;
  // --active_opacity
  if (config_lookup_float(&cfg, "active-opacity", &dval))
    ps->o.active_opacity = normalize_d(dval) * 100.0;
  // --opacity-fade-time
  if (config_lookup_float(&cfg, "opacity-fade-time", &dval))
    ps->o.opacity_fade_time = dval;
  // -z (clear_shadow)
  lcfg_lookup_bool(&cfg, "clear-shadow", &ps->o.clear_shadow);
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
  // --mark-wmwin-focused
  lcfg_lookup_bool(&cfg, "mark-wmwin-focused", &ps->o.mark_wmwin_focused);
  // --mark-ovredir-focused
  lcfg_lookup_bool(&cfg, "mark-ovredir-focused",
      &ps->o.mark_ovredir_focused);
  // --xinerama-shadow-crop
  lcfg_lookup_bool(&cfg, "xinerama-shadow-crop",
      &ps->o.xinerama_shadow_crop);
  // --detect-client-opacity
  lcfg_lookup_bool(&cfg, "detect-client-opacity",
      &ps->o.detect_client_opacity);
  // --vsync
  if (config_lookup_string(&cfg, "vsync", &sval) && !parse_vsync(ps, sval))
    exit(1);
  // --alpha-step
  config_lookup_float(&cfg, "alpha-step", &ps->o.alpha_step);
  // --paint-on-overlay
  lcfg_lookup_bool(&cfg, "paint-on-overlay", &ps->o.paint_on_overlay);
  // --use-ewmh-active-win
  lcfg_lookup_bool(&cfg, "use-ewmh-active-win",
      &ps->o.use_ewmh_active_win);
  // --unredir-if-possible
  lcfg_lookup_bool(&cfg, "unredir-if-possible",
      &ps->o.unredir_if_possible);
  // --unredir-if-possible-delay
  if (lcfg_lookup_int(&cfg, "unredir-if-possible-delay", &ival))
    ps->o.unredir_if_possible_delay = ival;
  // --inactive-dim-fixed
  lcfg_lookup_bool(&cfg, "inactive-dim-fixed", &ps->o.inactive_dim_fixed);
  // --detect-transient
  lcfg_lookup_bool(&cfg, "detect-transient", &ps->o.detect_transient);
  // --detect-client-leader
  lcfg_lookup_bool(&cfg, "detect-client-leader",
      &ps->o.detect_client_leader);
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
  // --unredir-if-possible-exclude
  parse_cfg_condlst(ps, &cfg, &ps->o.unredir_if_possible_blacklist, "unredir-if-possible-exclude");
  // --blur-background
  lcfg_lookup_bool(&cfg, "blur-background", &ps->o.blur_background);
  // --blur-background-frame
  lcfg_lookup_bool(&cfg, "blur-background-frame",
      &ps->o.blur_background_frame);
  // --blur-background-fixed
  lcfg_lookup_bool(&cfg, "blur-background-fixed",
      &ps->o.blur_background_fixed);
  // --blur-level
  lcfg_lookup_int(&cfg, "blur-level", &ps->o.blur_level);
  // --glx-no-stencil
  lcfg_lookup_bool(&cfg, "glx-no-stencil", &ps->o.glx_no_stencil);
  // --glx-copy-from-front
  lcfg_lookup_bool(&cfg, "glx-copy-from-front", &ps->o.glx_copy_from_front);
  // --glx-use-copysubbuffermesa
  lcfg_lookup_bool(&cfg, "glx-use-copysubbuffermesa", &ps->o.glx_use_copysubbuffermesa);
  // --glx-no-rebind-pixmap
  lcfg_lookup_bool(&cfg, "glx-no-rebind-pixmap", &ps->o.glx_no_rebind_pixmap);
  // --glx-swap-method
  if (config_lookup_string(&cfg, "glx-swap-method", &sval)
      && !parse_glx_swap_method(ps, sval))
    exit(1);
  // --glx-use-gpushader4
  lcfg_lookup_bool(&cfg, "glx-use-gpushader4", &ps->o.glx_use_gpushader4);
  // --xrender-sync
  lcfg_lookup_bool(&cfg, "xrender-sync", &ps->o.xrender_sync);
  // --xrender-sync-fence
  lcfg_lookup_bool(&cfg, "xrender-sync-fence", &ps->o.xrender_sync_fence);
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
    { "mark-ovredir-focused", no_argument, NULL, 264 },
    { "no-fading-openclose", no_argument, NULL, 265 },
    { "shadow-ignore-shaped", no_argument, NULL, 266 },
    { "detect-rounded-corners", no_argument, NULL, 267 },
    { "detect-client-opacity", no_argument, NULL, 268 },
    { "vsync", required_argument, NULL, 270 },
    { "alpha-step", required_argument, NULL, 271 },
    { "paint-on-overlay", no_argument, NULL, 273 },
    { "sw-opti", no_argument, NULL, 274 },
    { "vsync-aggressive", no_argument, NULL, 275 },
    { "use-ewmh-active-win", no_argument, NULL, 276 },
    { "respect-prop-shadow", no_argument, NULL, 277 },
    { "unredir-if-possible", no_argument, NULL, 278 },
    { "focus-exclude", required_argument, NULL, 279 },
    { "inactive-dim-fixed", no_argument, NULL, 280 },
    { "detect-transient", no_argument, NULL, 281 },
    { "detect-client-leader", no_argument, NULL, 282 },
    { "blur-background", no_argument, NULL, 283 },
    { "blur-background-frame", no_argument, NULL, 284 },
    { "blur-background-fixed", no_argument, NULL, 285 },
    { "dbus", no_argument, NULL, 286 },
    { "logpath", required_argument, NULL, 287 },
    { "invert-color-include", required_argument, NULL, 288 },
    { "opengl", no_argument, NULL, 289 },
    { "glx-no-stencil", no_argument, NULL, 291 },
    { "glx-copy-from-front", no_argument, NULL, 292 },
    { "benchmark", required_argument, NULL, 293 },
    { "benchmark-wid", required_argument, NULL, 294 },
    { "glx-use-copysubbuffermesa", no_argument, NULL, 295 },
    { "blur-background-exclude", required_argument, NULL, 296 },
    { "active-opacity", required_argument, NULL, 297 },
    { "glx-no-rebind-pixmap", no_argument, NULL, 298 },
    { "glx-swap-method", required_argument, NULL, 299 },
    { "fade-exclude", required_argument, NULL, 300 },
    { "blur-level", required_argument, NULL, 301 },
    { "glx-use-gpushader4", no_argument, NULL, 303 },
    { "opacity-rule", required_argument, NULL, 304 },
    { "shadow-exclude-reg", required_argument, NULL, 305 },
    { "paint-exclude", required_argument, NULL, 306 },
    { "xinerama-shadow-crop", no_argument, NULL, 307 },
    { "unredir-if-possible-exclude", required_argument, NULL, 308 },
    { "unredir-if-possible-delay", required_argument, NULL, 309 },
    { "write-pid-path", required_argument, NULL, 310 },
    { "vsync-use-glfinish", no_argument, NULL, 311 },
    { "xrender-sync", no_argument, NULL, 312 },
    { "xrender-sync-fence", no_argument, NULL, 313 },
    { "show-all-xerrors", no_argument, NULL, 314 },
    { "no-fading-destroyed-argb", no_argument, NULL, 315 },
    { "force-win-blend", no_argument, NULL, 316 },
    { "glx-fshader-win", required_argument, NULL, 317 },
    { "version", no_argument, NULL, 318 },
    { "no-x-selection", no_argument, NULL, 319 },
    { "no-name-pixmap", no_argument, NULL, 320 },
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
      else if (320 == o)
        ps->o.no_name_pixmap = true;
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
    ps->o.wintype_opacity[i] = 100.0;
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
      P_CASELONG('D', fade_delta);
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
      P_CASEBOOL('z', clear_shadow);
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
      P_CASEBOOL(264, mark_ovredir_focused);
      P_CASEBOOL(265, no_fading_openclose);
      P_CASEBOOL(266, shadow_ignore_shaped);
      P_CASEBOOL(268, detect_client_opacity);
      case 270:
        // --vsync
        if (!parse_vsync(ps, optarg))
          exit(1);
        break;
      case 271:
        // --alpha-step
        ps->o.alpha_step = atof(optarg);
        break;
      P_CASEBOOL(273, paint_on_overlay);
      P_CASEBOOL(275, vsync_aggressive);
      P_CASEBOOL(276, use_ewmh_active_win);
      P_CASEBOOL(277, respect_prop_shadow);
      P_CASEBOOL(278, unredir_if_possible);
      case 279:
        // --focus-exclude
        condlst_add(ps, &ps->o.focus_blacklist, optarg);
        break;
      P_CASEBOOL(280, inactive_dim_fixed);
      P_CASEBOOL(281, detect_transient);
      P_CASEBOOL(282, detect_client_leader);
      P_CASEBOOL(283, blur_background);
      P_CASEBOOL(284, blur_background_frame);
      P_CASEBOOL(285, blur_background_fixed);
      P_CASEBOOL(286, dbus);
      case 287:
        // --logpath
        ps->o.logpath = mstrcpy(optarg);
        break;
      case 288:
        // --invert-color-include
        condlst_add(ps, &ps->o.invert_color_list, optarg);
        break;
      P_CASEBOOL(291, glx_no_stencil);
      P_CASEBOOL(292, glx_copy_from_front);
      P_CASELONG(293, benchmark);
      case 294:
        // --benchmark-wid
        ps->o.benchmark_wid = strtol(optarg, NULL, 0);
        break;
      P_CASEBOOL(295, glx_use_copysubbuffermesa);
      case 296:
        // --blur-background-exclude
        condlst_add(ps, &ps->o.blur_background_blacklist, optarg);
        break;
      case 297:
        // --active-opacity
        ps->o.active_opacity = (normalize_d(atof(optarg)) * 100.0);
        break;
      P_CASEBOOL(298, glx_no_rebind_pixmap);
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
      P_CASEBOOL(303, glx_use_gpushader4);
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
      case 308:
        // --unredir-if-possible-exclude
        condlst_add(ps, &ps->o.unredir_if_possible_blacklist, optarg);
        break;
      P_CASELONG(309, unredir_if_possible_delay);
      case 310:
        // --write-pid-path
        ps->o.write_pid_path = mstrcpy(optarg);
        break;
      P_CASEBOOL(311, vsync_use_glfinish);
      P_CASEBOOL(312, xrender_sync);
      P_CASEBOOL(313, xrender_sync_fence);
      P_CASEBOOL(315, no_fading_destroyed_argb);
      P_CASEBOOL(316, force_win_blend);
      case 317:
        ps->o.glx_fshader_win_str = mstrcpy(optarg);
        break;
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
  ps->o.fade_delta = max_i(ps->o.fade_delta, 1);
  ps->o.inactive_dim = normalize_d(ps->o.inactive_dim);
  cfgtmp.menu_opacity = normalize_d(cfgtmp.menu_opacity);
  ps->o.alpha_step = normalize_d_range(ps->o.alpha_step, 0.01, 1.0);
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

  // --blur-background-frame implies --blur-background
  if (ps->o.blur_background_frame)
    ps->o.blur_background = true;

  if (ps->o.xrender_sync_fence)
    ps->o.xrender_sync = true;

  // Other variables determined by options

  // Determine whether we need to track focus changes
  if (ps->o.inactive_opacity || ps->o.active_opacity || ps->o.inactive_dim) {
    ps->o.track_focus = true;
  }

  // Determine whether we track window grouping
  if (ps->o.detect_transient || ps->o.detect_client_leader) {
    ps->o.track_leader = true;
  }
}

/**
 * Initialize DRM VSync.
 *
 * @return true for success, false otherwise
 */
static bool
vsync_drm_init(session_t *ps) {
#ifdef CONFIG_VSYNC_DRM
  // Should we always open card0?
  if (ps->drm_fd < 0 && (ps->drm_fd = open("/dev/dri/card0", O_RDWR)) < 0) {
    printf_errf("(): Failed to open device.");
    return false;
  }

  if (vsync_drm_wait(ps))
    return false;

  return true;
#else
  printf_errf("(): Program not compiled with DRM VSync support.");
  return false;
#endif
}

#ifdef CONFIG_VSYNC_DRM
/**
 * Wait for next VSync, DRM method.
 *
 * Stolen from: https://github.com/MythTV/mythtv/blob/master/mythtv/libs/libmythtv/vsync.cpp
 */
static int
vsync_drm_wait(session_t *ps) {
  int ret = -1;
  drm_wait_vblank_t vbl;

  vbl.request.type = _DRM_VBLANK_RELATIVE,
  vbl.request.sequence = 1;

  do {
     ret = ioctl(ps->drm_fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
     vbl.request.type &= ~_DRM_VBLANK_RELATIVE;
  } while (ret && errno == EINTR);

  if (ret)
    fprintf(stderr, "vsync_drm_wait(): VBlank ioctl did not work, "
        "unimplemented in this drmver?\n");

  return ret;

}
#endif

/**
 * Initialize OpenGL VSync.
 *
 * Stolen from: http://git.tuxfamily.org/?p=ccm/cairocompmgr.git;a=commitdiff;h=efa4ceb97da501e8630ca7f12c99b1dce853c73e
 * Possible original source: http://www.inb.uni-luebeck.de/~boehme/xvideo_sync.html
 *
 * @return true for success, false otherwise
 */
static bool
vsync_opengl_init(session_t *ps) {
  if (!ensure_glx_context(ps))
    return false;

  // Get video sync functions
  if (!ps->psglx->glXGetVideoSyncSGI)
    ps->psglx->glXGetVideoSyncSGI = (f_GetVideoSync)
      glXGetProcAddress((const GLubyte *) "glXGetVideoSyncSGI");
  if (!ps->psglx->glXWaitVideoSyncSGI)
    ps->psglx->glXWaitVideoSyncSGI = (f_WaitVideoSync)
      glXGetProcAddress((const GLubyte *) "glXWaitVideoSyncSGI");
  if (!ps->psglx->glXWaitVideoSyncSGI || !ps->psglx->glXGetVideoSyncSGI) {
    printf_errf("(): Failed to get glXWait/GetVideoSyncSGI function.");
    return false;
  }

  return true;
}

static bool
vsync_opengl_oml_init(session_t *ps) {
  if (!ensure_glx_context(ps))
    return false;

  // Get video sync functions
  if (!ps->psglx->glXGetSyncValuesOML)
    ps->psglx->glXGetSyncValuesOML = (f_GetSyncValuesOML)
      glXGetProcAddress ((const GLubyte *) "glXGetSyncValuesOML");
  if (!ps->psglx->glXWaitForMscOML)
    ps->psglx->glXWaitForMscOML = (f_WaitForMscOML)
      glXGetProcAddress ((const GLubyte *) "glXWaitForMscOML");
  if (!ps->psglx->glXGetSyncValuesOML || !ps->psglx->glXWaitForMscOML) {
    printf_errf("(): Failed to get OML_sync_control functions.");
    return false;
  }

  return true;
}

static bool
vsync_opengl_swc_init(session_t *ps) {
  if (!ensure_glx_context(ps))
    return false;

  // Get video sync functions
  if (!ps->psglx->glXSwapIntervalProc)
    ps->psglx->glXSwapIntervalProc = (f_SwapIntervalSGI)
      glXGetProcAddress ((const GLubyte *) "glXSwapIntervalSGI");
  if (!ps->psglx->glXSwapIntervalProc) {
    printf_errf("(): Failed to get SGI_swap_control function.");
    return false;
  }
  ps->psglx->glXSwapIntervalProc(1);

  return true;
}

static bool
vsync_opengl_mswc_init(session_t *ps) {
  if (!ensure_glx_context(ps))
    return false;

  // Get video sync functions
  if (!ps->psglx->glXSwapIntervalMESAProc)
    ps->psglx->glXSwapIntervalMESAProc = (f_SwapIntervalMESA)
      glXGetProcAddress ((const GLubyte *) "glXSwapIntervalMESA");
  if (!ps->psglx->glXSwapIntervalMESAProc) {
    printf_errf("(): Failed to get MESA_swap_control function.");
    return false;
  }
  ps->psglx->glXSwapIntervalMESAProc(1);

  return true;
}

/**
 * Wait for next VSync, OpenGL method.
 */
static int
vsync_opengl_wait(session_t *ps) {
  unsigned vblank_count = 0;

  ps->psglx->glXGetVideoSyncSGI(&vblank_count);
  ps->psglx->glXWaitVideoSyncSGI(2, (vblank_count + 1) % 2, &vblank_count);
  // I see some code calling glXSwapIntervalSGI(1) afterwards, is it required?

  return 0;
}

/**
 * Wait for next VSync, OpenGL OML method.
 *
 * https://mail.gnome.org/archives/clutter-list/2012-November/msg00031.html
 */
static int
vsync_opengl_oml_wait(session_t *ps) {
  int64_t ust = 0, msc = 0, sbc = 0;

  ps->psglx->glXGetSyncValuesOML(ps->dpy, ps->reg_win, &ust, &msc, &sbc);
  ps->psglx->glXWaitForMscOML(ps->dpy, ps->reg_win, 0, 2, (msc + 1) % 2,
      &ust, &msc, &sbc);

  return 0;
}

static void
vsync_opengl_swc_deinit(session_t *ps) {
  // The standard says it doesn't accept 0, but in fact it probably does
  if (glx_has_context(ps) && ps->psglx->glXSwapIntervalProc)
    ps->psglx->glXSwapIntervalProc(0);
}

static void
vsync_opengl_mswc_deinit(session_t *ps) {
  if (glx_has_context(ps) && ps->psglx->glXSwapIntervalMESAProc)
    ps->psglx->glXSwapIntervalMESAProc(0);
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
 * Wait for next VSync.
 */
static void
vsync_wait(session_t *ps) {
  if (!ps->o.vsync)
    return;

  if (VSYNC_FUNCS_WAIT[ps->o.vsync])
    VSYNC_FUNCS_WAIT[ps->o.vsync](ps);
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
    ps->o.paint_on_overlay = false;
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
  if (ps->o.blur_background || ps->o.blur_background_frame) {
      if (!glx_init_blur(ps))
          return false;
  }

  return true;
}

/**
 * Redirect all windows.
 */
static void
redir_start(session_t *ps) {
  if (!ps->redirected) {
#ifdef DEBUG_REDIR
    print_timestamp(ps);
    printf_dbgf("(): Screen redirected.\n");
#endif

    // Map overlay window. Done firstly according to this:
    // https://bugzilla.gnome.org/show_bug.cgi?id=597014
    if (ps->overlay)
      XMapWindow(ps->dpy, ps->overlay);

    XCompositeRedirectSubwindows(ps->dpy, ps->root, CompositeRedirectManual);

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

    size_t index = 0;
    win* w = swiss_getFirst(&ps->win_list, &index);
    while(w != NULL) {
        // If the window was mapped, then we need to do the mapping again
        if(w->a.map_state == IsViewable) {
            if(!wd_bind(&w->drawable)) {
                printf_errf("Failed binding window drawable %s", w->name);
                return;
            }

            if(!blur_cache_resize(&w->glx_blur_cache, &w->drawable.texture.size)) {
                printf_errf("Failed resizing window blur %s", w->name);
                return;
            }
        }

        w = swiss_getNext(&ps->win_list, &index);
    }

    ps->redirected = true;

    // Repaint the whole screen
    force_repaint(ps);
  }
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
static void
redir_stop(session_t *ps) {
  if (ps->redirected) {
#ifdef DEBUG_REDIR
    print_timestamp(ps);
    printf_dbgf("(): Screen unredirected.\n");
#endif
    // Destroy all Pictures as they expire once windows are unredirected
    // If we don't destroy them here, looks like the resources are just
    // kept inaccessible somehow
    size_t index = 0;
    win* w = swiss_getFirst(&ps->win_list, &index);
    while(w != NULL) {
        if(w->a.map_state == IsViewable) {
            wd_unbind(&w->drawable);
        }

        w = swiss_getNext(&ps->win_list, &index);
    }

    XCompositeUnredirectSubwindows(ps->dpy, ps->root, CompositeRedirectManual);
    // Unmap overlay window
    if (ps->overlay)
      XUnmapWindow(ps->dpy, ps->overlay);

    // Must call XSync() here
    XSync(ps->dpy, False);

    ps->redirected = false;
  }
}

/**
 * Unredirection timeout callback.
 */
static bool
tmout_unredir_callback(session_t *ps, timeout_t *tmout) {
  ps->tmout_unredir_hit = true;
  tmout->enabled = false;

  return true;
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

    return true;
  }

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
    // Then consider fading timeout
    else if (!ps->idling) {
      ptv = malloc(sizeof(struct timeval));
      *ptv = ms_to_tv(fade_timeout(ps));
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
#ifdef CONFIG_XINERAMA
  free_xinerama_info(ps);

  if (!ps->o.xinerama_shadow_crop || !ps->xinerama_exists) return;

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
#endif
}

/**
 * Initialize a session.
 *
 * @param ps_old old session, from which the function will take the X
 *    connection, then free it
 * @param argc number of commandline arguments
 * @param argv commandline arguments
 */
static session_t *
session_init(session_t *ps_old, int argc, char **argv) {
  const static session_t s_def = {
    .dpy = NULL,
    .scr = 0,
    .vis = NULL,
    .depth = 0,
    .root = None,
    .root_height = 0,
    .root_width = 0,
    // .root_damage = None,
    .overlay = None,
    .screen_reg = None,
    .reg_win = None,
    .o = {
      .config_file = NULL,
      .display = NULL,
      .glx_no_stencil = false,
      .glx_copy_from_front = false,
      .mark_wmwin_focused = false,
      .mark_ovredir_focused = false,
      .fork_after_register = false,
      .synchronize = false,
      .paint_on_overlay = false,
      .blur_level = 0,
      .unredir_if_possible = false,
      .unredir_if_possible_blacklist = NULL,
      .unredir_if_possible_delay = 0,
      .redirected_force = UNSET,
      .stoppaint_force = UNSET,
      .dbus = false,
      .benchmark = 0,
      .benchmark_wid = None,
      .logpath = NULL,

      .vsync = VSYNC_NONE,
      .vsync_aggressive = false,

      .wintype_shadow = { false },
      .clear_shadow = false,
      .shadow_blacklist = NULL,
      .respect_prop_shadow = false,
      .xinerama_shadow_crop = false,

      .wintype_fade = { false },
      .fade_delta = 10,
      .no_fading_openclose = false,
      .no_fading_destroyed_argb = false,
      .fade_blacklist = NULL,

      .wintype_opacity = { 0.0 },
      .inactive_opacity = 100.0,
      .inactive_opacity_override = false,
      .active_opacity = 100.0,
      .opacity_fade_time = 1000.0,
      .detect_client_opacity = false,
      .alpha_step = 0.03,

      .blur_background = false,
      .blur_background_frame = false,
      .blur_background_fixed = false,
      .blur_background_blacklist = NULL,
      .inactive_dim = 0.0,
      .inactive_dim_fixed = false,
      .invert_color_list = NULL,
      .opacity_rules = NULL,

      .wintype_focus = { false },
      .use_ewmh_active_win = false,
      .focus_blacklist = NULL,
      .detect_transient = false,
      .detect_client_leader = false,

      .track_focus = false,
      .track_wdata = false,
      .track_leader = false,
    },

    .pfds_read = NULL,
    .pfds_write = NULL,
    .pfds_except = NULL,
    .nfds_max = 0,
    .tmout_lst = NULL,

    .time_start = { 0, 0 },
    .redirected = false,
    .reg_ignore_expire = false,
    .idling = false,
    .fade_time = 0L,
    .ignore_head = NULL,
    .ignore_tail = NULL,
    .reset = false,

    .expose_rects = NULL,
    .size_expose = 0,
    .n_expose = 0,

    .win_list = {0},
    .active_win = NULL,
    .active_leader = None,

    .cgsize = 0,

#ifdef CONFIG_VSYNC_DRM
    .drm_fd = -1,
#endif

    .xfixes_event = 0,
    .xfixes_error = 0,
    .damage_event = 0,
    .damage_error = 0,
    .render_event = 0,
    .render_error = 0,
    .composite_event = 0,
    .composite_error = 0,
    .composite_opcode = 0,
    .has_name_pixmap = false,
    .shape_exists = false,
    .shape_event = 0,
    .shape_error = 0,
    .randr_exists = 0,
    .randr_event = 0,
    .randr_error = 0,
    .glx_exists = false,
    .glx_event = 0,
    .glx_error = 0,
    .xrfilter_convolution_exists = false,

    .track_atom_lst = NULL,

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

  swiss_init(&ps->win_list, sizeof(struct _win), 512);

  vector_init(&ps->order, sizeof(win_id), 16);

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

  // Start listening to events on root earlier to catch all possible
  // root geometry changes
  XSelectInput(ps->dpy, ps->root,
    SubstructureNotifyMask
    | ExposureMask
    | StructureNotifyMask
    | PropertyChangeMask);
  XFlush(ps->dpy);

  ps->root_width = DisplayWidth(ps->dpy, ps->scr);
  ps->root_height = DisplayHeight(ps->dpy, ps->scr);

  if (!XRenderQueryExtension(ps->dpy,
        &ps->render_event, &ps->render_error)) {
    fprintf(stderr, "No render extension\n");
    exit(1);
  }

  if (!XQueryExtension(ps->dpy, COMPOSITE_NAME, &ps->composite_opcode,
        &ps->composite_event, &ps->composite_error)) {
    fprintf(stderr, "No composite extension\n");
    exit(1);
  }

  {
    int composite_major = 0, composite_minor = 0;

    XCompositeQueryVersion(ps->dpy, &composite_major, &composite_minor);

    if (!ps->o.no_name_pixmap
        && (composite_major > 0 || composite_minor >= 2)) {
      ps->has_name_pixmap = true;
    }
  }

  if (!XDamageQueryExtension(ps->dpy, &ps->damage_event, &ps->damage_error)) {
    fprintf(stderr, "No damage extension\n");
    exit(1);
  }

  if (!XFixesQueryExtension(ps->dpy, &ps->xfixes_event, &ps->xfixes_error)) {
    fprintf(stderr, "No XFixes extension\n");
    exit(1);
  }

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

  // Second pass
  get_cfg(ps, argc, argv, false);

  // Query X Shape
  if (XShapeQueryExtension(ps->dpy, &ps->shape_event, &ps->shape_error)) {
    ps->shape_exists = true;
  }

  if (ps->o.xrender_sync_fence) {
#ifdef CONFIG_XSYNC
    // Query X Sync
    if (XSyncQueryExtension(ps->dpy, &ps->xsync_event, &ps->xsync_error)) {
      // TODO: Fencing may require version >= 3.0?
      int major_version_return = 0, minor_version_return = 0;
      if (XSyncInitialize(ps->dpy, &major_version_return, &minor_version_return))
        ps->xsync_exists = true;
    }
    if (!ps->xsync_exists) {
      printf_errf("(): X Sync extension not found. No X Sync fence sync is "
          "possible.");
      exit(1);
    }
#else
    printf_errf("(): X Sync support not compiled in. --xrender-sync-fence "
        "can't work.");
    exit(1);
#endif
  }

  // Query X RandR
  if (ps->o.xinerama_shadow_crop) {
    if (XRRQueryExtension(ps->dpy, &ps->randr_event, &ps->randr_error))
      ps->randr_exists = true;
    else
      printf_errf("(): No XRandR extension, automatic screen change "
          "detection impossible.");
  }

  // Query X Xinerama extension
  if (ps->o.xinerama_shadow_crop) {
#ifdef CONFIG_XINERAMA
    int xinerama_event = 0, xinerama_error = 0;
    if (XineramaQueryExtension(ps->dpy, &xinerama_event, &xinerama_error))
      ps->xinerama_exists = true;
#else
    printf_errf("(): Xinerama support not compiled in.");
#endif
  }

  rebuild_screen_reg(ps);

  // Overlay must be initialized before double buffer, and before creation
  // of OpenGL context.
  if (ps->o.paint_on_overlay)
    init_overlay(ps);

  // Initialize OpenGL as early as possible
  if (!glx_init(ps, true))
    exit(1);

  if(!xorgContext_init(&ps->psglx->xcontext, ps->dpy, ps->scr)) {
    printf_errf("Failed initializing the xorg context");
    exit(1);
  }

  // Monitor screen changes if vsync_sw is enabled and we are using
  // an auto-detected refresh rate, or when Xinerama features are enabled
  if (ps->randr_exists || ps->o.xinerama_shadow_crop)
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

  atoms_get(ps, &ps->atoms);

  {
    XRenderPictureAttributes pa;
    pa.subwindow_mode = IncludeInferiors;
  }

  // Initialize filters, must be preceded by OpenGL context creation
  if (!init_filters(ps))
    exit(1);

  fds_insert(ps, ConnectionNumber(ps->dpy), POLLIN);
  ps->tmout_unredir = timeout_insert(ps, ps->o.unredir_if_possible_delay,
      tmout_unredir_callback, NULL);
  ps->tmout_unredir->enabled = false;

  XGrabServer(ps->dpy);

  xtexture_init(&ps->root_texture, &ps->psglx->xcontext);
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
    recheck_focus(ps);
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
static void
session_destroy(session_t *ps) {
  redir_stop(ps);

  // Stop listening to events on root window
  XSelectInput(ps->dpy, ps->root, 0);

#ifdef CONFIG_DBUS
  // Kill DBus connection
  if (ps->o.dbus)
    cdbus_destroy(ps);

  free(ps->dbus_service);
#endif

  // Free window linked list
  {
    size_t index = 0;
    win* w = swiss_getFirst(&ps->win_list, &index);
    while(w != NULL) {
        if (IsViewable == w->a.map_state && !w->destroyed)
            win_ev_stop(ps, w);

        wd_delete(&w->drawable);

        w = swiss_getNext(&ps->win_list, &index);
    }
  }

#ifdef CONFIG_C2
  // Free blacklists
  free_wincondlst(&ps->o.shadow_blacklist);
  free_wincondlst(&ps->o.fade_blacklist);
  free_wincondlst(&ps->o.focus_blacklist);
  free_wincondlst(&ps->o.invert_color_list);
  free_wincondlst(&ps->o.blur_background_blacklist);
  free_wincondlst(&ps->o.opacity_rules);
  free_wincondlst(&ps->o.paint_blacklist);
  free_wincondlst(&ps->o.unredir_if_possible_blacklist);
#endif

  // Free tracked atom list
  {
    latom_t *next = NULL;
    for (latom_t *this = ps->track_atom_lst; this; this = next) {
      next = this->next;
      free(this);
    }

    ps->track_atom_lst = NULL;
  }

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

  // Free other X resources
  free_region(ps, &ps->screen_reg);
  free(ps->expose_rects);

  xtexture_delete(&ps->root_texture);

  free(ps->o.config_file);
  free(ps->o.write_pid_path);
  free(ps->o.display);
  free(ps->o.display_repr);
  free(ps->o.logpath);
  free(ps->pfds_read);
  free(ps->pfds_write);
  free(ps->pfds_except);
  free(ps->o.glx_fshader_win_str);
  free_xinerama_info(ps);

  xorgContext_delete(&ps->psglx->xcontext);

  glx_destroy(ps);

#ifdef CONFIG_VSYNC_DRM
  // Close file opened for DRM VSync
  if (ps->drm_fd >= 0) {
    close(ps->drm_fd);
    ps->drm_fd = -1;
  }
#endif

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
  ps->tmout_unredir = NULL;
  timeout_clear(ps);

  if (ps == ps_g)
    ps_g = NULL;
}

/**
 * Do the actual work.
 *
 * @param ps current session
 */
static void
session_run(session_t *ps) {
    win *t;

    Vector paints;
    vector_init(&paints, sizeof(win_id), ps->win_list.maxSize);

    ps->reg_ignore_expire = true;

    t = paint_preprocess(ps, &paints);

    timestamp lastTime;
    if(!getTime(&lastTime)) {
        printf_errf("Failed getting time");
        session_destroy(ps);
        exit(1);
    }

    if (ps->redirected)
        paint_all(ps, &paints);

    // Initialize idling
    ps->idling = false;

    // Main loop
    while (!ps->reset) {

        zone_start(&ZONE_global);

        zone_enter(&ZONE_input);

        while (mainloop(ps));

        zone_leave(&ZONE_input);

        // Placed after mainloop to avoid counting input time
        timestamp currentTime;
        if(!getTime(&currentTime)) {
            printf_errf("Failed getting time");
            session_destroy(ps);
            exit(1);
        }

        double delta = timeDiff(&lastTime, &currentTime);


        ps->skip_poll = false;

        if (ps->o.benchmark) {
            if (ps->o.benchmark_wid) {
                win *w = find_win(ps, ps->o.benchmark_wid);
                if (!w) {
                    printf_errf("(): Couldn't find specified benchmark window.");
                    session_destroy(ps);
                    exit(1);
                }
                add_damage_win(ps, w);
            }
            else {
                force_repaint(ps);
            }
        }

        // idling will be turned off during paint_preprocess() if needed
        ps->idling = true;

        zone_enter(&ZONE_preprocess);

        vector_clear(&paints);
        t = paint_preprocess(ps, &paints);
        ps->tmout_unredir_hit = false;

        zone_leave(&ZONE_preprocess);

        zone_enter(&ZONE_update);

        {
            size_t index;
            win_id* w_id = vector_getFirst(&paints, &index);
            while(w_id != NULL) {
                win_update(ps, swiss_get(&ps->win_list, *w_id), delta);
                w_id = vector_getNext(&paints, &index);
            }
        }

        {
            size_t index;
            struct _win* w = swiss_getFirst(&ps->win_list, &index);
            while(w != NULL) {
                if(w->state == STATE_DESTROYED) {
                    win_id w_id = swiss_indexOfPointer(&ps->win_list, w);
                    finish_destroy_win(ps, w_id);
                    size_t paints_slot = vector_find_uint64(&paints, w_id);
                    vector_remove(&paints, paints_slot);
                }
                w = swiss_getNext(&ps->win_list, &index);
            }
        }

        zone_leave(&ZONE_update);

        zone_enter(&ZONE_effect_textures);

        windowlist_updateStencil(ps, &paints);

        windowlist_updateShadow(ps, &paints);

        windowlist_updateBlur(ps, &paints);

        zone_leave(&ZONE_effect_textures);

        zone_enter(&ZONE_paint);

        static int paint = 0;
        if (ps->redirected || ps->o.stoppaint_force == OFF) {
            paint_all(ps, &paints);
            ps->reg_ignore_expire = false;
            paint++;
            if (ps->o.benchmark && paint >= ps->o.benchmark)
                exit(0);
            XSync(ps->dpy, False);
        }

        zone_leave(&ZONE_paint);

        // Finish the profiling before the vsync, since we don't want that to drag out the time
        struct ZoneEventStream* event_stream = zone_package(&ZONE_global);
#ifdef DEBUG_PROFILE
        profiler_render(event_stream);
#endif

        if (ps->o.vsync) {
            // Make sure all previous requests are processed to achieve best
            // effect
            XSync(ps->dpy, False);
            if (glx_has_context(ps)) {
                glXWaitX();
            }
        }

        /* // Wait for VBlank. We could do it aggressively (send the painting */
        /* // request and XFlush() on VBlank) or conservatively (send the request */
        /* // only on VBlank). */
        /* if (!ps->o.vsync_aggressive) */
        /*   vsync_wait(ps); */

        glXSwapBuffers(ps->dpy, get_tgt_window(ps));

        if (ps->o.vsync_aggressive)
            vsync_wait(ps);

        if (ps->idling)
            ps->fade_time = 0L;

        lastTime = currentTime;
    }
}

/**
 * Turn on the program reset flag.
 *
 * This will result in compton resetting itself after next paint.
 */
static void
reset_enable(int __attribute__((unused)) signum) {
  session_t * const ps = ps_g;

  ps->reset = true;
}

/**
 * The function that everybody knows.
 */
int
main(int argc, char **argv) {
  // Set locale so window names with special characters are interpreted
  // correctly
  setlocale(LC_ALL, "");

  // Set up SIGUSR1 signal handler to reset program
  {
    sigset_t block_mask;
    sigemptyset(&block_mask);
    const struct sigaction action= {
      .sa_handler = reset_enable,
      .sa_mask = block_mask,
      .sa_flags = 0
    };
    sigaction(SIGUSR1, &action, NULL);
  }

  // Main loop
  session_t *ps_old = ps_g;
  while (1) {
    ps_g = session_init(ps_old, argc, argv);
    if (!ps_g) {
      printf_errf("(): Failed to create new session.");
      return 1;
    }
    session_run(ps_g);
    ps_old = ps_g;
    session_destroy(ps_g);
  }

  free(ps_g);

  return 0;
}
