#include "xorg.h"

#include "assert.h"
#include "logging.h"

#include "profiler/zone.h"

#include <stdlib.h>
#include <string.h>

DECLARE_ZONE(select_config);

const char* X11Protocols_Names[] = {
    COMPOSITE_NAME,
    XFIXES_NAME,
    DAMAGE_NAME,
    RENDER_NAME,
    SHAPENAME,
    RANDR_NAME,
    GLX_EXTENSION_NAME,
    "XINERAMA",
    SYNC_NAME,
};

const char* VERSION_NAMES[] = {
    "NO",
    "NO (OPTIONAL)",
    "YES",
    "0.2+",
};

Atom get_atom(struct X11Context* context, const char* atom_name) {
  return get_atom_internal(context->display, atom_name);
}

// @CLEANUP Reduce to one argument. No reason to take in the caps separately
static int xorgContext_capabilities(struct X11Capabilities* caps, struct X11Context* context) {
    // Fetch if the extension is available
    for(size_t i = 0; i < PROTO_COUNT; i++) {
        if(!XQueryExtension(context->display, X11Protocols_Names[i],
                    &caps->opcode[i], &caps->event[i], &caps->error[i])) {
            caps->version[i] = XVERSION_NO;
            continue;
        }

        caps->version[i] = XVERSION_YES;
    }

    // The X docs say you should always fetch the version, even if you don't
    // care, because it's part of initialization
    if(caps->version[PROTO_COMPOSITE] == XVERSION_YES) {
        int major;
        int minor;
        XCompositeQueryVersion(context->display, &major, &minor);

        if(major > 0 || (major == 0 && minor > 2)) {
            caps->version[PROTO_COMPOSITE] = COMPOSITE_0_2;
        }
    }

    if(caps->version[PROTO_FIXES] == XVERSION_YES) {
        int major;
        int minor;
        XFixesQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_DAMAGE] == XVERSION_YES) {
        int major;
        int minor;
        XDamageQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_RENDER] == XVERSION_YES) {
        int major;
        int minor;
        XRenderQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_SHAPE] == XVERSION_YES) {
        int major;
        int minor;
        XShapeQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_RANDR] == XVERSION_YES) {
        int major;
        int minor;
        XRRQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_GLX] == XVERSION_YES) {
        int major;
        int minor;
        glXQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_XINERAMA] == XVERSION_YES) {
        int major;
        int minor;
        XineramaQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_SYNC] == XVERSION_YES) {
        int major;
        int minor;
        XSyncInitialize(context->display, &major, &minor);
    }

    return 0;
}


bool xorgContext_init(struct X11Context* context, Display* display, int screen, struct Atoms* atoms) {
    assert(context != NULL);
    assert(display != NULL);

    context->display = display;
    context->screen = screen;
    context->root = RootWindow(display, screen);

    context->configs = glXGetFBConfigs(display, screen, &context->numConfigs);
    if(context->configs == NULL) {
        printf_errf("Failed retrieving the fboconfigs");
        return false;
    }

    xorgContext_capabilities(&context->capabilities, context);

    context->active = NULL;
    vector_init(&context->eventBuf, sizeof(struct Event), 64);
    context->readCursor = 0;

    context->atoms = atoms;
    atoms_init(atoms, context->display);
    return true;
}

int xorgContext_ensure_capabilities(const struct X11Capabilities* caps) {
    bool missing = false;

	printf("Xorg Features: \n");
    for(size_t i = 0; i < PROTO_COUNT; i++) {
        printf("    %s: %s\n", X11Protocols_Names[i], VERSION_NAMES[caps->version[i]]);
        if(caps->version[i] == XVERSION_NO)
            missing = true;
    }

    return missing ? 1 : 0;
}

int xorgContext_convertEvent(const struct X11Capabilities* caps, enum X11Protocol proto, int ev) {
    return ev - caps->event[proto];
}

int xorgContext_convertError(const struct X11Capabilities* caps, enum X11Protocol proto, int ev) {
    return ev - caps->error[proto];
}

enum XExtensionVersion xorgContext_version(const struct X11Capabilities* caps, enum X11Protocol proto) {
    return caps->version[proto];
}

enum X11Protocol xorgContext_convertOpcode(const struct X11Capabilities* caps, int opcode) {
    for(size_t i = 0; i < PROTO_COUNT; i++) {
        if(caps->opcode[i] == opcode)
            return i;
    }
    return PROTO_COUNT;
}

GLXFBConfig* xorgContext_selectConfig(struct X11Context* context, VisualID visualid) {
    zone_scope(&ZONE_select_config);
    assert(visualid != 0);

	GLXFBConfig* selected = NULL;

    int value;
    for(int i = 0; i < context->numConfigs; i++) {
        GLXFBConfig fbconfig = context->configs[i];
        XVisualInfo* visinfo = glXGetVisualFromFBConfig(context->display, fbconfig);
        if (!visinfo || visinfo->visualid != visualid) {
            XFree(visinfo);
            continue;
        }
        XFree(visinfo);

        // We don't want to use anything multisampled
        glXGetFBConfigAttrib(context->display, fbconfig, GLX_SAMPLES, &value);
        if (value >= 2) {
            continue;
        }

        // We need to support pixmaps
        glXGetFBConfigAttrib(context->display, fbconfig, GLX_DRAWABLE_TYPE, &value);
        if (!(value & GLX_PIXMAP_BIT)) {
            continue;
        }

        // We need to be able to bind pixmaps to textures
        glXGetFBConfigAttrib(context->display, fbconfig,
                GLX_BIND_TO_TEXTURE_TARGETS_EXT,
                &value);
        if (!(value & GLX_TEXTURE_2D_BIT_EXT)) {
            continue;
        }

        // We want RGBA textures
        glXGetFBConfigAttrib(context->display, fbconfig,
                GLX_BIND_TO_TEXTURE_RGBA_EXT, &value);
        if (value == false) {
            continue;
        }

        selected = &context->configs[i];
        break;
    }
    // If we ran out of fbconfigs before we found something we like.
    if(selected == NULL)
        return NULL;

    return selected;
}

void xorgContext_delete(struct X11Context* context) {
    assert(context->display != NULL);
    free(context->configs);
}

static void pushEvent(struct X11Context* xctx, const struct Event event) {
    vector_putBack(&xctx->eventBuf, &event);
}

static void createAddWin(struct X11Context* xctx, Window xid) {
    struct Event event;
    // Default to no event.
    event.type = ET_NONE;
    event.add.xid = xid;
    XWindowAttributes attribs;

    if (!XGetWindowAttributes(xctx->display, xid, &attribs) || IsUnviewable == attribs.map_state) {
        // Failed to get window attributes probably means the window is gone
        // already. IsUnviewable means the window is already reparented
        // elsewhere.
        return;
    }

    // Only InputOutput windows have anything to composite (InputOnly doesn't
    // render anything). Since an InputOnly can't have InputOutput children we
    // can just completely disregard that window class
    if (InputOutput != attribs.class) {
        return;
    }

    event.add.xdamage = XDamageCreate(xctx->display, event.add.xid, XDamageReportNonEmpty);

    event.add.mapped = attribs.map_state == IsViewable;
    event.add.pos = (Vector2){{attribs.x, attribs.y}};
    event.add.size = (Vector2){{
      attribs.width + attribs.border_width * 2,
      attribs.height + attribs.border_width * 2,
    }};
    event.add.border_size = attribs.border_width;

    if (xorgContext_version(&xctx->capabilities, PROTO_SHAPE) >= XVERSION_YES) {
        // Subscribe to events when the window shape changes
        XShapeSelectInput(xctx->display, xid, ShapeNotifyMask);
    }

    Word_t rc;
    J1S(rc, xctx->active, xid);
    if(rc == 0) {
        printf_dbg("Window was already active");
    }

    event.type = ET_ADD;
    pushEvent(xctx, event);
}

static void createDestroyWin(struct X11Context* xctx, Window xid) {
    struct Event event;
    // Default to no event.
    event.type = ET_NONE;
    event.des.xid = xid;

    Word_t rc;
    J1U(rc, xctx->active, xid);
    if(rc == 0) {
        // The window wasn't active, so we swallow the destroy
        return;
    }

    event.type = ET_DESTROY;
    pushEvent(xctx, event);
}

static void createGetsClient(struct X11Context* xctx, Window xid, Window client_xid) {
    // @CLEANUP @HACK: We are still emitting a raw event for
    // reparenting. Ideally, the compositor shouldn't even have to
    // care about this parent nonsense. 
    struct Event event;
    event.type = ET_NONE;
    // Default to no event.
    event.cli.xid = xid;
    event.cli.client_xid = client_xid;

    Word_t rc;
    J1T(rc, xctx->active, xid);
    if(rc == 0) {
        // The window wasn't active, so we swallow the destroy
        return;
    }

    event.type = ET_CLIENT;
    pushEvent(xctx, event);
}

static void createMandr(struct X11Context* xctx, Window xid, float x, float y, float border, float width, float height, Window above) {
    Word_t rc;
    J1T(rc, xctx->active, xid);
    if(rc == 0) {
        // The window wasn't active, so we swallow the destroy
        return;
    }

    struct Event event = {
        .type = ET_MANDR,
        .mandr.xid = xid,
        .mandr.pos = (Vector2){{x, y}},
        .mandr.size = (Vector2){{
          width + border * 2,
          height + border * 2,
        }},
        .mandr.border_size = border,
    };
    pushEvent(xctx, event);
    struct Event restack = {
        .type = ET_RESTACK,
        .restack.xid = xid,
        .restack.above = above,
    };
    pushEvent(xctx, restack);
}

static void createFocus(struct X11Context* xctx) {
    Atom actual_type;
    int actual_format;
    unsigned long items;
    unsigned long left;
    unsigned char* data;
    // What a mess
    if(XGetWindowProperty(xctx->display, xctx->root,
        xctx->atoms->atom_ewmh_active_win, 0, 1, false, XA_WINDOW,
        &actual_type, &actual_format, &items, &left, &data) != Success) {
        printf_errf("Call to get the ewmh active window failed");
        return;
    }
    if(actual_type != XA_WINDOW) {
        XFree(data);
        printf_errf("The root windows ewmh focus property is not the correct type");
        return;
    }
    if(actual_format != 32) {
        XFree(data);
        printf_errf("The root windows ewmh focus property is not the correct format");
        return;
    }
    if(items == 0) {
        // No property value
        XFree(data);
        return;
    }

    // Read out the window id
    Window xid = *(long*)data;

    XFree(data);

    struct Event event = {
        .type = ET_FOCUS,
        .focus.xid = xid,
    };
    pushEvent(xctx, event);
}

static void fillBuffer(struct X11Context* xctx) {
    XEvent raw = {};
    XNextEvent(xctx->display, &raw);

    switch (raw.type) {
        case CreateNotify: {
            XCreateWindowEvent* ev = (XCreateWindowEvent *)&raw;
            createAddWin(xctx, ev->window);
            break;
        }
        case DestroyNotify: {
            XDestroyWindowEvent* ev = (XDestroyWindowEvent *)&raw;
            createDestroyWin(xctx, ev->window);
            break;
        }
        case ReparentNotify: {
            XReparentEvent* ev = (XReparentEvent *)&raw;
            // Since we only composite top level windows, reparenting to the
            // root looks like an new window to us.
            if (ev->parent == xctx->root) {
                createAddWin(xctx, ev->window);
            } else {
                createDestroyWin(xctx, ev->window);
                createGetsClient(xctx, ev->parent, ev->window);
            }
            break;
        }
        case ConfigureNotify: {
            XConfigureEvent* ev = (XConfigureEvent *)&raw;
            createMandr(xctx, ev->window, ev->x, ev->y, ev->border_width, ev->width, ev->height, ev->above);
            break;
        }
        case PropertyNotify: {
            XPropertyEvent* ev = (XPropertyEvent *)&raw;
            if(ev->window == xctx->root) {
                if (xctx->atoms->atom_ewmh_active_win == ev->atom) {
                    createFocus(xctx);
                    break;
                }
            }
            // Fallthrough
        }
        default: {
            struct Event event;
            event.type = ET_RAW;
            memcpy(&event.raw, &raw, sizeof(XEvent));
            pushEvent(xctx, event);
        }
    }
}

void xorg_nextEvent(struct X11Context* xctx, struct Event* event) {
    if(xctx->readCursor >= vector_size(&xctx->eventBuf)) {
        xctx->readCursor = 0;
        vector_clear(&xctx->eventBuf);
        fillBuffer(xctx);
    }

    if(xctx->readCursor >= vector_size(&xctx->eventBuf)) {
        event->type = ET_NONE;
        return;
    }

    *event = *(struct Event*)vector_get(&xctx->eventBuf, xctx->readCursor++);
}

// Synthesize events for the initial state
void xorg_beginEvents(struct X11Context* xctx) {
    Window root_return, parent_return;
    Window *children;
    unsigned int nchildren;

    XQueryTree(xctx->display, xctx->root, &root_return,
            &parent_return, &children, &nchildren);

    for (unsigned i = 0; i < nchildren; i++) {
        createAddWin(xctx, children[i]);
    }

    XFree(children);

    createFocus(xctx);
}
