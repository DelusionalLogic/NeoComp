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
#include "xtexture.h"
#include "buffer.h"
#include "timer.h"
#include "timeout.h"
#include "paths.h"
#include "debug.h"

#include "systems/blur.h"
#include "systems/shape.h"
#include "systems/shadow.h"
#include "systems/fullscreen.h"
#include "systems/texture.h"
#include "systems/physical.h"
#include "systems/xorg.h"
#include "systems/opacity.h"

#include "assets/assets.h"
#include "assets/shader.h"

#include "renderutil.h"

#include "shaders/shaderinfo.h"

#include "logging.h"

#include "profiler/zone.h"
#include "profiler/render.h"
#include "profiler/dump_events.h"

#include "intercept/xorg.h"

// === Global constants ===

DECLARE_ZONE(global);
DECLARE_ZONE(input);
DECLARE_ZONE(preprocess);
DECLARE_ZONE(handle_event);

DECLARE_ZONE(one_event);

DECLARE_ZONE(update);
DECLARE_ZONE(update_z);
DECLARE_ZONE(update_wintype);
DECLARE_ZONE(update_invert_list);
DECLARE_ZONE(input_react);
DECLARE_ZONE(make_cutout);
DECLARE_ZONE(remove_input);
DECLARE_ZONE(prop_blur_damage);

DECLARE_ZONE(x_error);
DECLARE_ZONE(x_communication);

DECLARE_ZONE(commit_move);
DECLARE_ZONE(commit_resize);

DECLARE_ZONE(paint);
DECLARE_ZONE(effect_textures);
DECLARE_ZONE(blur_background);
DECLARE_ZONE(fetch_prop);

DECLARE_ZONE(update_fade);

DECLARE_ZONE(update_textures);
DECLARE_ZONE(update_single_texture);

// From the header {{{

static win * find_win(session_t *ps, Window id) {
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

static void wintype_arr_enable(bool arr[]) {
    wintype_t i;

    for (i = 0; i < NUM_WINTYPES; ++i) {
        arr[i] = true;
    }
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

// @X11
static bool validate_pixmap(session_t *ps, Pixmap pxmap) {
    if (!pxmap) return false;

    Window rroot = None;
    int rx = 0, ry = 0;
    unsigned rwid = 0, rhei = 0, rborder = 0, rdepth = 0;
    return XGetGeometry(ps->dpy, pxmap, &rroot, &rx, &ry,
            &rwid, &rhei, &rborder, &rdepth) && rwid && rhei;
}

/**
 * Find out the WM frame of a client window using existing data.
 *
 * @param id window ID
 * @return struct _win object of the found window, NULL if not found
 */
static win_id find_toplevel(session_t *ps, Window id) {
    if (!id)
        return -1;

    for_components(it, &ps->win_list,
            COMPONENT_STATEFUL, COMPONENT_HAS_CLIENT, CQ_END) {
        struct HasClientComponent* client = swiss_getComponent(&ps->win_list, COMPONENT_HAS_CLIENT, it.id);
        struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

        if (client->id == id && stateful->state != STATE_DESTROYING)
            return it.id;
    }

    return -1;
}

static win * find_toplevel2(session_t *ps, Window wid);

static win * find_win_all(session_t *ps, const Window wid) {
    if (!wid || PointerRoot == wid || wid == ps->root || wid == ps->overlay)
        return NULL;

    win *w = find_win(ps, wid);
    if (!w) {
        win_id fid = find_toplevel(ps, wid);
        if(fid != -1)
            w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, fid);
    }
    if (!w) w = find_toplevel2(ps, wid);
    return w;
}

static void win_set_focused(session_t *ps, win *w);

static void win_mark_client(session_t *ps, win *w, Window client);

#ifdef DEBUG_EVENTS
static int ev_serial(XEvent *ev);

static const char * ev_name(session_t *ps, XEvent *ev);

static Window ev_window(session_t *ps, XEvent *ev);
#endif

#if defined(DEBUG_EVENTS) || defined(DEBUG_RESTACK)
static bool ev_window_name(session_t *ps, Window wid, char **name);
#endif

static bool vsync_opengl_swc_init(session_t *ps);

static void vsync_opengl_swc_deinit(session_t *ps);

static void redir_start(session_t *ps);
static void redir_stop(session_t *ps);

static time_ms_t timeout_get_newrun(const timeout_t *ptmout) {
    long a = (ptmout->lastrun + (time_ms_t) (ptmout->interval * TIMEOUT_RUN_TOLERANCE) - ptmout->firstrun) / ptmout->interval;
    long b = (ptmout->lastrun + (time_ms_t) (ptmout->interval * (1 - TIMEOUT_RUN_TOLERANCE)) - ptmout->firstrun) / ptmout->interval;
  return ptmout->firstrun + (max_l(a, b) + 1) * ptmout->interval;
}

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

// === Global variables ===

/// Pointer to current session, as a global variable. Only used by
/// <code>error()</code> and <code>reset_enable()</code>, which could not
/// have a pointer to current session passed in.
session_t *ps_g = NULL;

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
// @X11
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

/**
 * Paint root window content.
 */
static void paint_root(session_t *ps) {
    assert(ps->root_texture.bound);

    glViewport(0, 0, ps->root_size.x, ps->root_size.y);

    glEnable(GL_DEPTH_TEST);

    struct face* face = assets_load("window.face");
    Vector3 pos = {{0, 0, 0.9999}};
    draw_tex(face, &ps->root_texture.texture, &pos, &ps->root_size);

    glDisable(GL_DEPTH_TEST);
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

static void map_win(session_t *ps, struct MapWin* ev) {
    win *w = find_win(ps, ev->xid);
	if(w == NULL) {
        return;
    }
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    assert(w != NULL);

    assert(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid));

    // Add a map event
    swiss_ensureComponent(&ps->win_list, COMPONENT_MAP, wid);

    if(swiss_hasComponent(&ps->win_list, COMPONENT_UNMAP, wid)) {
        swiss_removeComponent(&ps->win_list, COMPONENT_UNMAP, wid);
    }

    struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, wid);
    stateful->state = STATE_WAITING;
    swiss_ensureComponent(&ps->win_list, COMPONENT_FOCUS_CHANGE, wid);
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
    if(swiss_hasComponent(&ps->win_list, COMPONENT_MAP, wid)) {
        swiss_removeComponent(&ps->win_list, COMPONENT_MAP, wid);
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
win_mark_client(session_t *ps, win *parent, Window client) {
  win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, parent);
  struct HasClientComponent* clientComponent = swiss_addComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid);

  clientComponent->id = client;

  // If the window isn't mapped yet, stop here, as the function will be
  // called in map_win()
  if (!win_mapped(&ps->win_list, wid))
    return;

  swiss_ensureComponent(&ps->win_list, COMPONENT_WINTYPE_CHANGE, wid);

  // Update everything related to conditions
  /* win_on_factor_change(ps, w); */
}

static bool add_win(session_t *ps, struct AddWin* ev) {
  const static win win_def = {
    .border_size = 0,
    .override_redirect = false,

    .window_type = WINTYPE_UNKNOWN,

    .name = NULL,
    .class_instance = NULL,
    .class_general = NULL,
    .role = NULL,

    .fade = true,
    .shadow = true,
    .dim = true,
    .invert_color = false,
  };

  // We don't add the overlay window
  if (ev->xid == ps->overlay) {
    return false;
  }

  // Allocate and initialize the new win structure
  win_id slot = swiss_allocate(&ps->win_list);
  win* new = swiss_addComponent(&ps->win_list, COMPONENT_MUD, slot);

  if (!new) {
    printf_errf("(%#010lx): Failed to allocate memory for the new window.", ev->xid);
    return false;
  }


  swiss_addComponent(&ps->win_list, COMPONENT_NEW, slot);
  {
      struct PhysicalComponent* phy = swiss_addComponent(&ps->win_list, COMPONENT_PHYSICAL, slot);
      phy->position = ev->pos;
      phy->size = ev->size;
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
      struct FadesOpacityComponent* fo = swiss_addComponent(&ps->win_list, COMPONENT_FADES_BGOPACITY, slot);
      fade_init(&fo->fade, 0.0);
  }
  {
      struct FadesDimComponent* fo = swiss_addComponent(&ps->win_list, COMPONENT_FADES_DIM, slot);
      fade_init(&fo->fade, 0.0);
      swiss_addComponent(&ps->win_list, COMPONENT_DIM, slot);
  }
  {
      struct TracksWindowComponent* window = swiss_addComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, slot);
      window->id = ev->xid;
  }
  {
      // Windows frame themselves until they get a client
      struct HasClientComponent* cli = swiss_addComponent(&ps->win_list, COMPONENT_HAS_CLIENT, slot);
      cli->id = ev->xid;
  }

  memcpy(new, &win_def, sizeof(win_def));

  new->border_size = ev->border_size;
  new->override_redirect = ev->override_redirect;

  struct ZComponent* z = swiss_addComponent(&ps->win_list, COMPONENT_Z, slot);
  z->z = 0;

  vector_putBack(&ps->order, &slot);

  return true;
}

static void
restack_win(session_t *ps, struct Restack* ev) {
    win *w = find_win(ps, ev->xid);
    if(w == NULL)
        return;
    win_id w_id = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    size_t w_loc;
    size_t new_loc;
    if(ev->loc == LOC_BELOW) {
        struct _win* w_above = find_win(ps, ev->above);

        // @INSPECT @RESEARCH @UNDERSTAND @HACK: Sometimes we get a bogus
        // ConfigureNotify above value for a window that doesn't even exist
        // (badwindow from X11). For now we will just not restack anything then,
        // but it seems like a hack
        if(w_above == NULL)
            return;

        win_id above_id = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w_above);

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
            new_loc = above_loc + 1;
        } else {
            new_loc = above_loc;
        }
    } else {
        w_loc = vector_find_uint64(&ps->order, w_id);

        if (ev->loc == LOC_HIGHEST) {
            new_loc = vector_size(&ps->order) - 1;
        } else {
            new_loc = 0;
        }
    }

    if(w_loc == new_loc)
        return;

    vector_circulate(&ps->order, w_loc, new_loc);
}

static void canvas_change(session_t* ps, struct CanvasChange* ev) {
    ps->root_size = ev->size;

    glViewport(0, 0, ps->root_size.x, ps->root_size.y);

    ps->psglx->view = mat4_orthogonal(0, ps->root_size.x, 0, ps->root_size.y, -.1, 1);
    view = ps->psglx->view;

    return;
}

static void
configure_win(session_t *ps, struct MandR* ev) {
  assert(ev->xid != ps->root);

  // Other window changes
  win *w = find_win(ps, ev->xid);

  if(w == NULL)
    return;

  win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

  w->border_size = ev->border_size;
  w->override_redirect = ev->override_redirect;
  physics_move_window(&ps->win_list, wid, &ev->pos, &ev->size);
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
}

static void
root_damaged(session_t *ps, struct NewRoot* ev) {
  if (ps->root_texture.bound) {
    xtexture_unbind(&ps->root_texture);
  }

  Pixmap pixmap = ev->pixmap;

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

  struct XTextureInformation texinfo;
  xtexinfo_init(&texinfo, &ps->xcontext, fbconfig);

  struct XTexture* texptr = &ps->root_texture;
  struct XTextureInformation* texinfoptr = &texinfo;
  if(!xtexture_bind(&ps->xcontext, &texptr, &texinfoptr, (xcb_pixmap_t*)&pixmap, 1)) {
      printf_errf("Failed binding the root texture to gl");
  }
}

static void damage_win(session_t *ps, struct Damage *ev) {
    // @PERFORMANCE: We are getting a DamageNotify while moving windows, which
    // means we are damaging the contents (and therefore rebinding the window)
    // for every move. In most cases, that's completely unnecessary. I don'r
    // know how to detect if a damage is caused by a move at this time.
    // - Delusional 16/11-2018

    win *w = find_win(ps, ev->xid);

    if (!w) {
        return;
    }


    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    if (!win_mapped(&ps->win_list, wid))
        return;

    swiss_ensureComponent(&ps->win_list, COMPONENT_CONTENTS_DAMAGED, wid);
}

static char* eventName(struct X11Context* xctx, XErrorEvent* ev) {
    int o = 0;
#define CASESTRRET2(s)   case s: return #s; break

    o = xorgContext_convertError(&xctx->capabilities, PROTO_FIXES, ev->error_code);
    switch (o) {
        CASESTRRET2(BadRegion);
    }

    o = xorgContext_convertError(&xctx->capabilities, PROTO_DAMAGE, ev->error_code);
    switch (o) {
        CASESTRRET2(BadDamage);
    }

    o = xorgContext_convertError(&xctx->capabilities, PROTO_RENDER, ev->error_code);
    switch (o) {
        CASESTRRET2(BadPictFormat);
        CASESTRRET2(BadPicture);
        CASESTRRET2(BadPictOp);
        CASESTRRET2(BadGlyphSet);
        CASESTRRET2(BadGlyph);
    }

    o = xorgContext_convertError(&xctx->capabilities, PROTO_GLX, ev->error_code);
    switch (o) {
        CASESTRRET2(GLX_BAD_SCREEN);
        CASESTRRET2(GLX_BAD_ATTRIBUTE);
        CASESTRRET2(GLX_NO_EXTENSION);
        CASESTRRET2(GLX_BAD_VISUAL);
        CASESTRRET2(GLX_BAD_CONTEXT);
        CASESTRRET2(GLX_BAD_VALUE);
        CASESTRRET2(GLX_BAD_ENUM);
    }

    o = xorgContext_convertError(&xctx->capabilities, PROTO_SYNC, ev->error_code);
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

    return "Unknown";
}

static int xerror(Display __attribute__((unused)) *dpy, XErrorEvent *ev) {
    session_t * const ps = ps_g;

    if (xorgContext_convertOpcode(&ps->xcontext.capabilities, ev->request_code) == PROTO_COMPOSITE
            && ev->minor_code == X_CompositeRedirectSubwindows) {
        fprintf(stderr, "Another composite manager is already running\n");
        exit(1);
    }

    const char* name = eventName(&ps->xcontext, ev);

    print_timestamp(ps);
    {
        zone_insta_extra(&ZONE_x_error, "%s:%ld", name, ev->request_code);
        char buf[BUF_LEN] = "";
        XGetErrorText(ps->dpy, ev->error_code, buf, BUF_LEN);
        printf("error %4d %-12s request %4d minor %4d serial %6lu resource %4lu: \"%s\"\n",
                ev->error_code, name, ev->request_code,
                ev->minor_code, ev->serial, ev->resourceid, buf);
    }

    /* print_backtrace(); */

    return 0;
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
    }

    ps->active_win = w;

    assert(ps->active_win == w);
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

  if (xorgContext_version(&ps->xcontext.capabilities, PROTO_DAMAGE) >= XVERSION_YES
          && xorgContext_convertEvent(&ps->xcontext.capabilities, PROTO_DAMAGE,  ev->type) == XDamageNotify)
    return "Damage";

  if (xorgContext_version(&ps->xcontext.capabilities, PROTO_SHAPE) >= XVERSION_YES
          && xorgContext_convertEvent(&ps->xcontext.capabilities, PROTO_SHAPE,  ev->type) == ShapeNotify)
    return "ShapeNotify";

  if (xorgContext_version(&ps->xcontext.capabilities, PROTO_SYNC)  >= XVERSION_YES) {
      o = xorgContext_convertEvent(&ps->xcontext.capabilities, PROTO_SYNC, ev->error_code);
      int o = ev->type - ps->xsync_event;
      switch (o) {
          CASESTRRET(XSyncCounterNotify);
          CASESTRRET(XSyncAlarmNotify);
      }
  }

  sprintf(buf, "Event %d", ev->type);

  return buf;
}

static const char *
ev_focus_mode_name(XFocusChangeEvent* ev) {
  switch (ev->mode) {
    CASESTRRET(NotifyNormal);
    CASESTRRET(NotifyWhileGrabbed);
    CASESTRRET(NotifyGrab);
    CASESTRRET(NotifyUngrab);
  }

  return "Unknown";
}

static const char *
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

static void
ev_focus_report(XFocusChangeEvent* ev) {
  printf("  { mode: %s, detail: %s }\n", ev_focus_mode_name(ev),
      ev_focus_detail_name(ev));
}

#endif

// === Events ===

static void ev_destroy_notify(session_t *ps, Window window) {
    win *w = find_win(ps, window);
    if(w == NULL)
        return;

    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    destroy_win(ps, wid);
}

static void ev_unmap_notify(session_t *ps, struct UnmapWin *ev) {
  win *w = find_win(ps, ev->xid);

  if (w)
    unmap_win(ps, w);
}

static void getsclient(session_t* ps, struct GetsClient* event) {
    // The new client shouldn't be the client of any other window
    assert(find_toplevel(ps, event->client_xid) == -1);

    // Find our new frame
    win *w_frame = find_toplevel2(ps, event->xid);

    assert(w_frame != NULL);
    if(w_frame == NULL) {
        printf_errf("New parent for %lu doesn't have a parent frame", event->client_xid);
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
    if (wid_has_prop(ps, event->client_xid, ps->atoms.atom_client)) {
        if(swiss_hasComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid_frame)) {
            swiss_removeComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wid_frame);
        }

        win_mark_client(ps, w_frame, event->client_xid);
    }
}

static void set_active_window(session_t* ps, struct Focus* ev) {
    if(ev->xid == 0) {
        ps->active_win = NULL;
        return;
    }
    win *w = find_win_all(ps, ev->xid);

    if(w == NULL) {
        printf_errf("Window with the id %zu was not found", ev->xid);
        return;
    }

    // Mark the window focused. No need to unfocus the previous one.
    win_set_focused(ps, w);
}

static void ev_shape_notify(session_t *ps, struct Shape *ev) {
    if(ev->xid == ps->overlay) {
        return;
    }

    win *w = find_win_all(ps, ev->xid);
    assert(w != NULL);

    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    swiss_ensureComponent(&ps->win_list, COMPONENT_SHAPE_DAMAGED, wid);
    // We need to mark some damage
    // The blur isn't damaged, because it will be cut out by the new geometry
}

// === Main ===

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

  // Acquire X Selection _NET_WM_CM_S
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
static bool
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
 * Parse a long number.
 */
static bool
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
 * Process arguments and configuration files.
 */
static void
get_cfg(session_t *ps, int argc, char *const *argv, bool first_pass) {
  const static char *shortopts = "D:I:O:d:r:o:m:l:t:i:e:hscnfFCaSzGb";
  const static struct option longopts[] = {
    { "help", no_argument, NULL, 'h' },
    { "config", required_argument, NULL, 256 },
    { "shadow", no_argument, NULL, 'c' },
    { "fading", no_argument, NULL, 'f' },
    { "active-opacity", required_argument, NULL, 'I' },
    { "inactive-opacity", required_argument, NULL, 'i' },
    { "opacity-fade-time", required_argument, NULL, 'T' },
    { "bg-opacity-fade-time", required_argument, NULL, 280 },
    { "frame-opacity", required_argument, NULL, 'e' },
    { "daemon", no_argument, NULL, 'b' },
    { "no-dnd-shadow", no_argument, NULL, 'G' },
    { "inactive-dim", required_argument, NULL, 261 },
    { "dim-fade-time", required_argument, NULL, 264 },
    { "shadow-ignore-shaped", no_argument, NULL, 266 },
    { "blur-background", no_argument, NULL, 283 },
    { "logpath", required_argument, NULL, 287 },
    { "opengl", no_argument, NULL, 289 },
    { "benchmark", required_argument, NULL, 293 },
    { "glx-use-copysubbuffermesa", no_argument, NULL, 295 },
    { "blur-level", required_argument, NULL, 301 },
    { "show-all-xerrors", no_argument, NULL, 314 },
    { "version", no_argument, NULL, 318 },
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
      printf_errfq(1, "neocomp doesn't accept positional arguments.");

    return;
  }

  struct options_tmp cfgtmp = {
  };
  char *lc_numeric_old = mstrcpy(setlocale(LC_NUMERIC, NULL));

  for (i = 0; i < NUM_WINTYPES; ++i) {
    ps->o.wintype_opacity[i] = -1.0;
  }
  for (i = 0; i < NUM_WINTYPES; ++i) {
    ps->o.wintype_shadow[i] = true;
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
      case 314:
      case 318:
      case 320:
        break;
      case 'c':
        // Skip, shadows enabled?
        break;
      case 'f':
        // Skip, fading enabled?
        break;
      case 'I':
        // --active-opacity
        ps->o.active_opacity = (normalize_d(atof(optarg)) * 100.0);
        break;
      case 'i':
        ps->o.inactive_opacity = (normalize_d(atof(optarg)) * 100.0);
        break;
      case 'T':
        ps->o.opacity_fade_time = atof(optarg);
        break;
      case 280:
        ps->o.bg_opacity_fade_time = atof(optarg);
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
      case 261:
        // --inactive-dim
        ps->o.inactive_dim = atof(optarg);
        break;
      case 264:
        // --inactive-dim
        ps->o.dim_fade_time = atof(optarg);
        break;
      P_CASEBOOL(283, blur_background);
      case 287:
        // --logpath
        ps->o.logpath = mstrcpy(optarg);
        break;
      P_CASELONG(293, benchmark);
      P_CASELONG(301, blur_level);
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
bool vsync_init(session_t *ps) {
	if (!vsync_opengl_swc_init(ps)) {
		return false;
	}

	return true;
}

/**
 * Deinitialize current VSync method.
 */
void vsync_deinit(session_t *ps) {
	vsync_opengl_swc_deinit(ps);
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

    // Must call XSync() here -- Why?
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

    // Must call XSync() here -- Why?
    XSync(ps->dpy, False);
}

static void ev_bypass(session_t* ps, struct Bypass* ev) {
    win *w = find_win(ps, ev->xid);
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);

    if(swiss_hasComponent(&ps->win_list, COMPONENT_UNMAP, wid)) {
        swiss_removeComponent(&ps->win_list, COMPONENT_UNMAP, wid);
    }else if(swiss_hasComponent(&ps->win_list, COMPONENT_MAP, wid)) {
        swiss_removeComponent(&ps->win_list, COMPONENT_MAP, wid);
    }

    struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, wid);
    stateful->state = STATE_WAITING;
    swiss_ensureComponent(&ps->win_list, COMPONENT_BYPASS, wid);
}

/**
 * Main loop.
 */
static bool
mainloop(session_t *ps) {
    while(true) {
        // Don't miss timeouts even when we have a LOT of other events!
        timeout_run(ps);

        // Process existing events
        // Sometimes poll() returns 1 but no events are actually read,
        // causing XNextEvent() to block, I have no idea what's wrong, so we
        // check for the number of events here.
        bool done = false;
        bool processed = false;
        while(!done) {
            struct Event event;

            xorg_nextEvent(&ps->xcontext, &event);
            switch(event.type) {
                case ET_ADD:
                    zone_enter_extra(&ZONE_one_event, "ADD");
                    processed = true;
                    add_win(ps, &event.add);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_DESTROY:
                    zone_enter_extra(&ZONE_one_event, "DESTROY");
                    processed = true;
                    ev_destroy_notify(ps, event.des.xid);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_MAP:
                    zone_enter_extra(&ZONE_one_event, "MAP");
                    processed = true;
                    map_win(ps, &event.map);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_UNMAP:
                    zone_enter_extra(&ZONE_one_event, "UNMAP");
                    processed = true;
                    ev_unmap_notify(ps, &event.unmap);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_BYPASS:
                    zone_enter_extra(&ZONE_one_event, "BYPASS");
                    processed = true;
                    ev_bypass(ps, &event.bypass);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_CLIENT:
                    zone_enter_extra(&ZONE_one_event, "CLIENT");
                    processed = true;
                    getsclient(ps, &event.cli);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_CCHANGE:
                    zone_enter_extra(&ZONE_one_event, "CCHANGE");
                    processed = true;
                    canvas_change(ps, &event.cchange);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_MANDR:
                    zone_enter_extra(&ZONE_one_event, "MANDR");
                    processed = true;
                    configure_win(ps, &event.mandr);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_RESTACK:
                    zone_enter_extra(&ZONE_one_event, "RESTACK");
                    processed = true;
                    restack_win(ps, &event.restack);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_FOCUS:
                    zone_enter_extra(&ZONE_one_event, "FOCUS");
                    processed = true;
                    set_active_window(ps, &event.focus);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_NEWROOT:
                    zone_enter_extra(&ZONE_one_event, "NEWROOT");
                    processed = true;
                    root_damaged(ps, &event.newRoot);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_WINTYPE:
                    zone_enter_extra(&ZONE_one_event, "WINTYPE");
                    processed = true;
                    swiss_ensureComponent(&ps->win_list, COMPONENT_WINTYPE_CHANGE, find_toplevel(ps, event.wintype.xid));
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_WINCLASS:
                    zone_enter_extra(&ZONE_one_event, "WINCLASS");
                    processed = true;
                    swiss_ensureComponent(&ps->win_list, COMPONENT_CLASS_CHANGE, find_toplevel(ps, event.winclass.xid));
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_DAMAGE:
                    zone_enter_extra(&ZONE_one_event, "DAMAGE");
                    processed = true;
                    damage_win(ps, &event.damage);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_SHAPE:
                    zone_enter_extra(&ZONE_one_event, "SHAPE");
                    processed = true;
                    ev_shape_notify(ps, &event.shape);
                    zone_leave(&ZONE_one_event);
                    break;
                case ET_NONE:
                    done = true;
                    break;
                default:
                    printf_errf("Unknown event type, ignoring");
            }
        }

        if(processed) {
            return false;
        }

        if (ps->reset)
            return false;

        // Consider skip_poll firstly
        if (ps->skip_poll || ps->o.benchmark) {
            return false;
        }

        // Calculate timeout
        struct timeval tv;

        time_ms_t tmout_ms = min_l(timeout_get_poll_time(ps), TIME_MS_MAX);
        tv = ms_to_tv(tmout_ms);

        // If we don't have a timeout skip the poll
        if (timeval_isempty(&tv)) {
            return false;
        }

        fds_poll(ps, &tv);
    }

    return false;
}

char* getDisplayName(Display* display) {
  // Build a safe representation of display name
    char *display_repr = DisplayString(display);
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

    return display_repr;
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
      .fork_after_register = false,
      .blur_level = 0,
      .benchmark = 0,
      .logpath = NULL,

      .wintype_opacity = { -1.0 },
      .inactive_opacity = 100.0,
      .active_opacity = 100.0,
      .opacity_fade_time = 1000.0,
      .bg_opacity_fade_time = 1000.0,

      .blur_background = false,
      .inactive_dim = 100.0,
      .inactive_dim_fixed = false,
      .dim_fade_time = 1000.0,

      .wintype_focus = { false },

      .track_focus = false,
      .track_wdata = true,
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

  swiss_setComponentSize(&ps->win_list, COMPONENT_BGOPACITY, sizeof(struct BgOpacityComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_FADES_BGOPACITY, sizeof(struct FadesBgOpacityComponent));

  swiss_setComponentSize(&ps->win_list, COMPONENT_DIM, sizeof(struct DimComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_FADES_DIM, sizeof(struct FadesDimComponent));

  swiss_setComponentSize(&ps->win_list, COMPONENT_SHADOW, sizeof(struct glx_shadow_cache));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_SHADOW);
  swiss_setComponentSize(&ps->win_list, COMPONENT_BLUR, sizeof(struct glx_blur_cache));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_BLUR);
  swiss_setComponentSize(&ps->win_list, COMPONENT_WINTYPE_CHANGE, sizeof(struct WintypeChangedComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_CLASS_CHANGE, sizeof(struct ClassChangedComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_SHAPED, sizeof(struct ShapedComponent));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_SHAPED);
  swiss_setComponentSize(&ps->win_list, COMPONENT_SHAPE_DAMAGED, sizeof(struct ShapeDamagedEvent));
  swiss_disableAutoRemove(&ps->win_list, COMPONENT_SHAPE_DAMAGED);
  swiss_setComponentSize(&ps->win_list, COMPONENT_STATEFUL, sizeof(struct StatefulComponent));
  swiss_setComponentSize(&ps->win_list, COMPONENT_TRANSITIONING, sizeof(struct TransitioningComponent));

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
  XSynchronize(ps->dpy, true);

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

  ps->o.display_repr = getDisplayName(ps->dpy);

  // Also initializes the atoms
  if(!xorgContext_init(&ps->xcontext, ps->dpy, ps->scr, &ps->atoms)) {
    printf_errf("Failed initializing the xorg context");
    exit(1);
  }

  // Second pass
  get_cfg(ps, argc, argv, false);

  // Overlay must be initialized before double buffer, and before creation
  // of OpenGL context.
  init_overlay(ps);
  // @CLEANUP @HACK This probably shouldn't be done here.
  ps->xcontext.overlay = ps->overlay;

  add_shader_type(&global_info);
  add_shader_type(&downsample_info);
  add_shader_type(&upsample_info);
  add_shader_type(&passthough_info);
  add_shader_type(&profiler_info);
  add_shader_type(&text_info);
  add_shader_type(&shadow_info);
  add_shader_type(&stencil_info);
  add_shader_type(&colored_info);
  add_shader_type(&graph_info);
  add_shader_type(&bgblit_info);
  add_shader_type(&postshadow_info);

  assets_init();

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

  if(xorgContext_ensure_capabilities(&ps->xcontext.capabilities)) {
      printf_errf("One of the required X extensions were missing");
      exit(1);
  }

  // Initialize VSync
  if (!vsync_init(ps))
    exit(1);

  // Create registration window
  if (!ps->reg_win && !register_cm(ps))
    exit(1);

  char* debug_font_loc = assets_resolve_path("Roboto-Light.ttf");
  if(debug_font_loc != NULL) {
      text_debug_load(debug_font_loc);
      free(debug_font_loc);
  } else {
      printf_errf("Failed finding the debug font");
  }

  bezier_init(&ps->curve, 0.29, 0.1, 0.29, 1);

  // Initialize filters, must be preceded by OpenGL context creation
  blursystem_init();
  glx_check_err(ps);

  XGrabServer(ps->xcontext.display);

  xtexture_init(&ps->root_texture, &ps->xcontext);

  redir_start(ps);

  xorg_beginEvents(&ps->xcontext);

  fds_insert(ps, ConnectionNumber(ps->xcontext.display), POLLIN);

  XUngrabServer(ps->xcontext.display);
  // ALWAYS flush after XUngrabServer()!
  XFlush(ps->dpy);

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

#ifdef FRAMERATE_DISPLAY
  init_debug_graph(&ps->debug_graph);
#endif

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

  text_debug_unload();

  // Free window linked list
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
  shadowsystem_delete(&ps->win_list);
  blursystem_delete(&ps->win_list);
  shapesystem_delete(&ps->win_list);

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
  free(ps->o.display);
  free(ps->o.display_repr);
  free(ps->o.logpath);
  free(ps->pfds_read);
  free(ps->pfds_write);
  free(ps->pfds_except);

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

  // Flush all events -- Why?
  XSync(ps->dpy, True);

  // Free timeouts
  timeout_clear(ps);

  if (ps == ps_g)
    ps_g = NULL;
}

// @CLEANUP: This shouldn't be here
bool do_win_fade(struct Bezier* curve, double dt, Swiss* em) {
    bool skip_poll = false;

    Vector fadeable;
    vector_init(&fadeable, sizeof(struct Fading*), 128);

    // Collect everything fadeable
    opacity_collect_fades(em, &fadeable);
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

                double time = fmax(keyframe->time - keyframe->lead, 0);
                double x = time / (keyframe->duration - keyframe->lead);
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

    glEnable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 0, 0);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

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
    zone_enter(&ZONE_x_communication);
    XGrabServer(xcontext->display);
    glXWaitX();
    zone_leave(&ZONE_x_communication);

    struct WindowDrawable** drawables = malloc(sizeof(struct WindowDrawable*) * em->size);
    size_t drawable_count = 0;
    {
        for_componentsArr(it2, em, req_types) {
            struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it2.id);
            drawables[drawable_count] = &bindsTexture->drawable;
            drawable_count++;
        }
    }

    for_componentsArr(it2, em, req_types) {
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it2.id);

        XSyncFence fence = XSyncCreateFence(xcontext->display, bindsTexture->drawable.wid, false);
        XSyncTriggerFence(xcontext->display, fence);
        XSyncAwaitFence(xcontext->display, &fence, 1);
        XSyncDestroyFence(xcontext->display, fence);
    }

    zone_enter(&ZONE_x_communication);
    if(!wd_bind(xcontext, drawables, drawable_count)) {
        // If we fail to bind we just assume that the window must have been
        // closed and keep the old texture
        printf_err("Failed binding some drawable");
        zone_leave(&ZONE_x_communication);
    }

    free(drawables);
    zone_leave(&ZONE_x_communication);

    glClearColor(0, 0, 0, 0);

    for_componentsArr(it2, em, req_types) {
        zone_scope(&ZONE_update_single_texture);

        struct ShapedComponent* shaped = swiss_getComponent(em, COMPONENT_SHAPED, it2.id);
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it2.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it2.id);
        framebuffer_resetTarget(fbo);
        framebuffer_targetTexture(fbo, &textured->texture);
        framebuffer_targetRenderBuffer_stencil(fbo, &textured->stencil);
        framebuffer_rebind(fbo);

        Vector2 offset = textured->texture.size;
        vec2_sub(&offset, &bindsTexture->drawable.texture.size);

        Matrix old_view = view;
        view = mat4_orthogonal(0, textured->texture.size.x, 0, textured->texture.size.y, -1, 1);
        glViewport(0, 0, textured->texture.size.x, textured->texture.size.y);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        // If the texture didn't bind, we just clear the window without rendering on top.
        if(bindsTexture->drawable.bound) {
            texture_bind(&bindsTexture->drawable.texture, GL_TEXTURE0);

            shader_set_uniform_bool(shader_type->flip, bindsTexture->drawable.texture.flipped);

            draw_rect(shaped->face, shader_type->mvp, (Vector3){{0, offset.y, 0}}, bindsTexture->drawable.texture.size);
        }

        view = old_view;

    }

    for_componentsArr(it2, em, req_types) {
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it2.id);

        // The texture might not have bound successfully
        if(bindsTexture->drawable.bound) {
            wd_unbind(&bindsTexture->drawable);
        }
    }

    glDisable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    zone_enter(&ZONE_x_communication);
    XUngrabServer(xcontext->display);
    glXWaitX();
    zone_leave(&ZONE_x_communication);
}

static void finish_destroyed_windows(Swiss* em, session_t* ps) {
    for_components(it, em, COMPONENT_STATEFUL, COMPONENT_TINT, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED || stateful->state == STATE_INVISIBLE) {
            swiss_removeComponent(em, COMPONENT_TINT, it.id);
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
            COMPONENT_STATEFUL, CQ_NOT, COMPONENT_TRANSITIONING, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_ACTIVATING) {
            stateful->state = STATE_ACTIVE;
        } else if(stateful->state == STATE_DEACTIVATING) {
            stateful->state = STATE_INACTIVE;
        } else if(stateful->state == STATE_HIDING) {
            stateful->state = STATE_INVISIBLE;
        } else if(stateful->state == STATE_DESTROYING) {
            stateful->state = STATE_DESTROYED;
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

DECLARE_ZONE(synchronize_opacity);

static void syncronize_fade_opacity(Swiss* em) {
    zone_scope(&ZONE_synchronize_opacity);

    opacity_commit_fades(em);

    for_components(it, em,
            COMPONENT_DIM, COMPONENT_FADES_DIM, CQ_END) {
        struct FadesDimComponent* fo = swiss_getComponent(em, COMPONENT_FADES_DIM, it.id);
        struct DimComponent* dim = swiss_getComponent(em, COMPONENT_DIM, it.id);

        dim->dim = fo->fade.value;
    }
}

DECLARE_ZONE(update_focused);

static void update_focused_state(Swiss* em, session_t* ps) {
    zone_scope(&ZONE_update_focused);
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

        if(ps->o.wintype_focus[w->window_type]) {
            newState = STATE_ACTIVATING;
        } else if(ps->active_win == w) {
            newState = STATE_ACTIVATING;
        }

        // If a window is inactive and we are deactivating, then there's no change
        if(newState == STATE_DEACTIVATING && stateful->state == STATE_INACTIVE) {
            continue;
        }
        // Likewise for active
        if(newState == STATE_ACTIVATING && stateful->state == STATE_ACTIVE) {
            continue;
        }

        if(newState != stateful->state) {
            stateful->state = newState;
            swiss_ensureComponent(&ps->win_list, COMPONENT_FOCUS_CHANGE, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_STATEFUL, CQ_NOT, COMPONENT_DEBUGGED, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);
        /* swiss_ensureComponent(em, COMPONENT_DEBUGGED, it.id); */
        if(stateful->state == STATE_ACTIVATING || stateful->state == STATE_ACTIVE) {
            swiss_ensureComponent(em, COMPONENT_DEBUGGED, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_DEBUGGED, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);
        if(stateful->state == STATE_DEACTIVATING || 
                stateful->state == STATE_HIDING || 
                stateful->state == STATE_INACTIVE || 
                stateful->state == STATE_INVISIBLE) {
            swiss_removeComponent(em, COMPONENT_DEBUGGED, it.id);
        }
    }
}

void start_focus_fade(Swiss* em, double fade_time, double bg_fade_time, double dim_fade_time) {
    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        fade_keyframe(&fo->fade, f->newOpacity, fade_time);
    }
    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_FADES_BGOPACITY, CQ_END) {
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct FadesBgOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_BGOPACITY, it.id);
        fade_keyframe(&fo->fade, f->newOpacity, bg_fade_time);
    }
    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_FADES_DIM, CQ_END) {
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct FadesDimComponent* fo = swiss_getComponent(em, COMPONENT_FADES_DIM, it.id);
        fade_keyframe(&fo->fade, f->newDim, dim_fade_time);
    }

    swiss_removeComponentWhere(em, COMPONENT_TRANSITIONING,
            (enum ComponentType[]){COMPONENT_FOCUS_CHANGE, CQ_END});

    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, CQ_END) {
        struct TransitioningComponent* t = swiss_addComponent(em, COMPONENT_TRANSITIONING, it.id);
        t->time = 0;
        t->duration = fmax(dim_fade_time, fmax(fade_time, bg_fade_time));
    }
}

static void commit_move(Swiss* em, Vector* order) {
    zone_scope(&ZONE_commit_move);
    for_components(it, em,
            COMPONENT_MOVE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_BLUR_DAMAGED, it.id);
    }

    // Damage all windows on top of windows that move
    for_components(it, em, COMPONENT_MOVE, CQ_END) {
        size_t order_slot = vector_find_uint64(order, it.id);
        assert(order_slot >= 0);

        win_id* other_id = vector_getNext(order, &order_slot);
        while(other_id != NULL) {
            swiss_ensureComponent(em, COMPONENT_BLUR_DAMAGED, *other_id);
            other_id = vector_getNext(order, &order_slot);
        }
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

    // Unmapping a window causes us to stop redirecting it
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

    // Texture init handled in system

    // When we map a window, and blur/shadow isn't there, we want to add them.
    // Blur and shadow have been migrated to the system

    for_components(it, em,
            COMPONENT_MUD, COMPONENT_MAP, CQ_NOT, COMPONENT_TINT, CQ_END) {
        struct TintComponent* tint = swiss_addComponent(em, COMPONENT_TINT, it.id);
        tint->color = (Vector4){{1, 1, 1, .017}};
    }

    // Blur and shadow have been migrated to the system

    // After a map we'd like to immediately bind the window.
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_BINDS_TEXTURE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_CONTENTS_DAMAGED, it.id);
    }
}

void fill_class_changes(Swiss* em, session_t* ps) {
    // Fetch the new class
    for_components(it, em,
            COMPONENT_CLASS_CHANGE, COMPONENT_HAS_CLIENT, CQ_END) {
        struct ClassChangedComponent* class = swiss_getComponent(em, COMPONENT_CLASS_CHANGE, it.id);
        struct HasClientComponent* client = swiss_getComponent(em, COMPONENT_HAS_CLIENT, it.id);

        memset(class, 0, sizeof(struct ClassChangedComponent));

        char **strlst = NULL;
        int nstr = 0;

        if (!wid_get_text_prop(ps, client->id, ps->atoms.atom_class, &strlst, &nstr)) {
            printf_dbgf("Failed fetching class property");
            swiss_removeComponent(em, COMPONENT_CLASS_CHANGE, it.id);
            continue;
        }

        // Copy the strings if successful
        class->instance = mstrcpy(strlst[0]);

        if (nstr > 1)
            class->general = mstrcpy(strlst[1]);

        XFreeStringList(strlst);
    }
}

void fill_wintype_changes(Swiss* em, session_t* ps) {
    // Fetch the new window type
    for_components(it, em,
            COMPONENT_WINTYPE_CHANGE, COMPONENT_HAS_CLIENT, CQ_END) {
        struct WintypeChangedComponent* wintypeChanged = swiss_getComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
        struct HasClientComponent* client = swiss_getComponent(em, COMPONENT_HAS_CLIENT, it.id);

        // Detect window type here
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

void damage_blur_over_damaged(Swiss* em, Vector* order) {
    // Damage the blur of windows on top of damaged windows
    for_components(it, em,
            COMPONENT_CONTENTS_DAMAGED, CQ_END) {

        size_t order_slot = vector_find_uint64(order, it.id);

        win_id* other_id = vector_getNext(order, &order_slot);
        while(other_id != NULL) {

            if(win_overlap(em, it.id, *other_id)) {
                swiss_ensureComponent(em, COMPONENT_BLUR_DAMAGED, *other_id);
            }

            other_id = vector_getNext(order, &order_slot);
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
#ifdef DEBUG_PROFILE
    struct ProfilerWriterSession profSess;
    profilerWriter_init(&profSess);
#endif

	fullscreensystem_determine(&ps->win_list, &ps->root_size);

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

        mainloop(ps);

        assets_hotload();

        Swiss* em = &ps->win_list;


        // Process all the events added by X

        zone_leave(&ZONE_input);

        // Placed after mainloop to avoid counting input time
        timestamp currentTime;
        if(!getTime(&currentTime)) {
            printf_errf("Failed getting time");
            exit(1);
        }

        double dt = timeDiff(&lastTime, &currentTime);

        ps->skip_poll = false;

        // idling will be turned off later if desired.
        ps->idling = true;

        zone_enter(&ZONE_preprocess);

        shapesystem_updateShapes(em, &ps->xcontext);
        fullscreensystem_determine(&ps->win_list, &ps->root_size);

        zone_leave(&ZONE_preprocess);

        zone_enter(&ZONE_update);

        zone_enter(&ZONE_update_z);
        assign_depth(&ps->win_list, &ps->order);
        zone_leave(&ZONE_update_z);

        zone_enter(&ZONE_update_wintype);

        fill_class_changes(&ps->win_list, ps);
        fill_wintype_changes(&ps->win_list, ps);

        // If the wintype actually changed (is still there), then the focus
        // might have changed
        for_components(it, em, COMPONENT_WINTYPE_CHANGE, CQ_END) {
            swiss_ensureComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        }

        // Update legacy fields in the mud structure
        for_components(it, em, COMPONENT_MUD, COMPONENT_CLASS_CHANGE, CQ_END) {
            struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
            struct ClassChangedComponent* class = swiss_getComponent(em, COMPONENT_CLASS_CHANGE, it.id);
            w->class_general = class->general;
            w->class_instance = class->instance;
        }

        for_components(it, em,
                COMPONENT_WINTYPE_CHANGE, CQ_END) {
            struct WintypeChangedComponent* wintypeChanged = swiss_getComponent(em, COMPONENT_WINTYPE_CHANGE, it.id);
            struct _win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);

            w->window_type = wintypeChanged->newType;
        }
        zone_leave(&ZONE_update_wintype);

        zone_enter(&ZONE_input_react);
        commit_destroy(&ps->win_list);
        xorgsystem_tick(&ps->win_list, &ps->xcontext, &ps->atoms);
        commit_map(&ps->win_list, &ps->atoms, &ps->xcontext);
        commit_unmap(&ps->win_list, &ps->xcontext);
        texturesystem_tick(&ps->win_list);
        commit_opacity_change(&ps->win_list, ps->o.opacity_fade_time, ps->o.bg_opacity_fade_time);
        physics_tick(&ps->win_list);
        commit_move(&ps->win_list, &ps->order);
        zone_leave(&ZONE_input_react);

        {
            zone_scope(&ZONE_make_cutout);
            XserverRegion newShape = XFixesCreateRegionH(ps->dpy, NULL, 0);
            for_components(it, em,
                    COMPONENT_MUD, COMPONENT_TRACKS_WINDOW, COMPONENT_PHYSICAL, CQ_NOT, COMPONENT_REDIRECTED, CQ_END) {
                struct TracksWindowComponent* tracksWindow = swiss_getComponent(em, COMPONENT_TRACKS_WINDOW, it.id);
                struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

                if(win_mapped(em, it.id)) {
                    XserverRegion windowRegion = XFixesCreateRegionFromWindow(ps->xcontext.display, tracksWindow->id, ShapeBounding);
                    XFixesTranslateRegionH(ps->dpy, windowRegion, physical->position.x+1, physical->position.y+1);
                    XFixesUnionRegionH(ps->xcontext.display, newShape, newShape, windowRegion);
                    XFixesDestroyRegionH(ps->xcontext.display, windowRegion);
                }
            }
            XFixesInvertRegionH(ps->dpy, newShape, &(XRectangle){0, 0, ps->root_size.x, ps->root_size.y}, newShape);
            XFixesSetWindowShapeRegionH(ps->dpy, ps->overlay, ShapeBounding, 0, 0, newShape);
            XFixesDestroyRegionH(ps->xcontext.display, newShape);
        }

        zone_enter(&ZONE_prop_blur_damage);
        damage_blur_over_damaged(&ps->win_list, &ps->order);
        zone_leave(&ZONE_prop_blur_damage);

        zone_enter(&ZONE_update_textures);
        update_window_textures(&ps->win_list, &ps->xcontext, &ps->psglx->shared_fbo);
        zone_leave(&ZONE_update_textures);

        update_focused_state(&ps->win_list, ps);
        calculate_window_opacity(ps, &ps->win_list);
        start_focus_fade(&ps->win_list, ps->o.opacity_fade_time, ps->o.bg_opacity_fade_time, ps->o.dim_fade_time);

        zone_enter(&ZONE_update_fade);

        syncronize_fade_opacity(&ps->win_list);
        if(do_win_fade(&ps->curve, dt, &ps->win_list)) {
            ps->skip_poll = true;
        }

        for_components(it, em,
                COMPONENT_TRANSITIONING, CQ_END) {
            struct TransitioningComponent* t = swiss_getComponent(em, COMPONENT_TRANSITIONING, it.id);
            t->time += dt;
        }

        for_components(it, em,
                COMPONENT_TRANSITIONING, CQ_END) {
            struct TransitioningComponent* t = swiss_getComponent(em, COMPONENT_TRANSITIONING, it.id);
            if(t->time >= t->duration)
                swiss_removeComponent(em, COMPONENT_TRANSITIONING, it.id);
        }

        zone_leave(&ZONE_update_fade);

        transition_faded_entities(&ps->win_list);
        shadowsystem_tick(em);
        blursystem_tick(em, &ps->order);
        remove_texture_invis_windows(&ps->win_list);
        finish_destroyed_windows(&ps->win_list, ps);
        shapesystem_finish(&ps->win_list);
        zone_leave(&ZONE_update);

        Vector opaque;
        vector_init(&opaque, sizeof(win_id), ps->order.size);
        for_components(it, &ps->win_list,
                COMPONENT_MUD, COMPONENT_TEXTURED, CQ_NOT, COMPONENT_BGOPACITY, COMPONENT_PHYSICAL, CQ_END) {
            vector_putBack(&opaque, &it.id);
        }
        vector_qsort(&opaque, window_zcmp, &ps->win_list);
        Vector transparent;
        vector_init(&transparent, sizeof(win_id), ps->order.size);
        // Even non-opaque windows have some transparent elements (shadow).
        // Trying to draw something as transparent when it only has opaque
        // elements isn't a problem, so we just include everything.
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

        shadowsystem_updateShadow(ps, &transparent);

        if(ps->o.blur_background)
            blursystem_updateBlur(&ps->win_list, &ps->root_size, &ps->root_texture.texture, ps->o.blur_level, (struct _session*) ps);

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
            draw_component_debug(&ps->win_list, &ps->root_size);
#endif

            vector_kill(&opaque_shadow);
            vector_kill(&transparent);
            vector_kill(&opaque);

            zone_leave(&ZONE_paint);

            paint++;
            if (ps->o.benchmark && paint >= ps->o.benchmark) {
#ifdef DEBUG_PROFILE
                profilerWriter_kill(&profSess);
#endif
                session_destroy(ps);
                exit(0);
            }
        }

        zone_enter(&ZONE_remove_input);
        // Remove all the X events
        // @CLEANUP: Should maybe be done last in the frame
        swiss_resetComponent(&ps->win_list, COMPONENT_MAP);
        swiss_resetComponent(&ps->win_list, COMPONENT_UNMAP);
        swiss_resetComponent(&ps->win_list, COMPONENT_BYPASS);
        swiss_resetComponent(&ps->win_list, COMPONENT_MOVE);
        swiss_resetComponent(&ps->win_list, COMPONENT_NEW);
        swiss_removeComponentWhere(
            &ps->win_list,
            COMPONENT_RESIZE,
            (enum ComponentType[]){COMPONENT_PHYSICAL, CQ_END}
        );

        swiss_resetComponent(em, COMPONENT_WINTYPE_CHANGE);
        swiss_resetComponent(em, COMPONENT_CLASS_CHANGE);

        swiss_resetComponent(em, COMPONENT_FOCUS_CHANGE);

        for_components(it, em, COMPONENT_SHAPE_DAMAGED, CQ_END) {
            struct ShapeDamagedEvent* shapeDamaged = swiss_getComponent(em, COMPONENT_SHAPE_DAMAGED, it.id);
            if(shapeDamaged->rects.elementSize != 0) {
                vector_kill(&shapeDamaged->rects);
            }
        }
        swiss_resetComponent(&ps->win_list, COMPONENT_SHAPE_DAMAGED);

        zone_leave(&ZONE_remove_input);

        swiss_resetComponent(&ps->win_list, COMPONENT_CONTENTS_DAMAGED);

#ifdef FRAMERATE_DISPLAY
        update_debug_graph(&ps->debug_graph, currentTime);
        draw_debug_graph(&ps->debug_graph, &(Vector2){{10, ps->root_size.y - 10}});
#endif

        // Finish the profiling before the vsync, since we don't want that to drag out the time
#ifdef DEBUG_PROFILE
        struct ZoneEventStream* event_stream = zone_package(&ZONE_global);
        /* profiler_render(event_stream); */
        profilerWriter_emitFrame(&profSess, event_stream);
#else
        zone_package(&ZONE_global);
#endif

        glXSwapBuffers(ps->dpy, get_tgt_window(ps));
        glFinish();

        lastTime = currentTime;
    }

#ifdef DEBUG_PROFILE
    profilerWriter_kill(&profSess);
#endif
}
