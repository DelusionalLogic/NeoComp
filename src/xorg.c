#include "xorg.h"

#include "assert.h"
#include "logging.h"

#include "profiler/zone.h"

#include <stdlib.h>
#include <string.h>

#ifdef INTERCEPT
#define RootWindow(dpy, scr) RootWindowHook(dpy, scr)
#define XNextEvent(dpy, ev) XNextEventHook(dpy, ev)
bool XGetWindowAttributesHook(Display* dpy, Window window, XWindowAttributes* attrs);
#define XGetWindowAttributes(dpy, w, a) XGetWindowAttributesHook(dpy, w, a)
#endif

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

    context->winParent = NULL;
    context->client = NULL;
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

static bool detachSubtree(struct X11Context* xctx, Window xid) {
    int rc_int;
    JLD(rc_int, xctx->winParent, xid);
    if(rc_int == JERR) {
        printf_errf("Malloc error");
        return false;
    }

    return rc_int == 1;
}

static void attachSubtree(struct X11Context* xctx, Window xid, Window client_xid) {
    // Add the client->frame association
    Window* value;
    JLI(value, xctx->winParent, client_xid);
    if(value == PJERR){
        printf_errf("Malloc failed");
        return;
    } else if(*value != 0) {
        printf_errf("Client already had a frame?");
        return;
    }

    *value = xid;
}

static Window findRoot(struct X11Context* xctx, Window xid) {
    // Add the client->frame association
    Window* next = &xid;
    Window last;
    do {
        last = *next;
        JLG(next, xctx->winParent, last);
    } while(next != NULL);

    return last;
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
    int rc_int;
    JLD(rc_int, xctx->winParent, xid);
    if(rc_int == JERR) {
        printf_errf("Malloc error");
        return;
    } else if(rc_int == 1) {
        // Window was framed which means it won't be active
        return;
    }

    Word_t rc;
    J1U(rc, xctx->active, xid);
    if(rc == 0) {
        // The window wasn't active, so we swallow the destroy
        return;
    }

    // Framed window destroy causes all the subwindows to be destroyed which
    // should cause the client to be removed from clientMap

    struct Event event = {
        .type = ET_DESTROY,
        .des.xid = xid,
    };
    pushEvent(xctx, event);
}

static void createGetsClient(struct X11Context* xctx, Window xid, Window client_xid) {
    // @CLEANUP @HACK: We are still emitting an event for reparenting. Ideally,
    // the compositor shouldn't even have to care about this parent nonsense. 

    Word_t rc;
    J1T(rc, xctx->active, xid);
    assert(rc != 0);

    struct Event event = {
        .type = ET_CLIENT,
        .cli.xid = xid,
        .cli.client_xid = client_xid,
    };
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

static void createNewRoot(struct X11Context* xctx) {
    xcb_connection_t* xcb = XGetXCBConnection(xctx->display);

    Atom atoms[2] = {
        xctx->atoms->atom_xrootmapid,
        xctx->atoms->atom_xsetrootid,
    };

    xcb_get_property_cookie_t cookies[2];
    for(int i = 0; i < 2; i++) {
        cookies[i] = xcb_get_property(xcb, false, xctx->root, atoms[i], XA_PIXMAP, 0, 1);
    }

    xcb_get_property_reply_t *replies[2];
    for(int i = 0; i < 2; i++) {
        xcb_generic_error_t *error;
        replies[i] = xcb_get_property_reply(xcb, cookies[i], &error);
        if(replies[i] == NULL) {
            printf_errf("Failed fetching root pixmap property: code %d", error->error_code);
            free(error);
        }
    }

    bool found = false;
    Window xid = 0;
    for(int i = 0; i < 2; i++) {
        if(replies[i] == NULL)
            continue;

        xcb_get_property_reply_t *reply = replies[i];

        if(reply->type == None) {
            // The property was not found, so just move on
            free(reply);
            continue;
        }

        if(reply->type != XA_PIXMAP) {
            printf_errf("The root window pixmap property was not a pixmap");
            free(reply);
            continue;
        }
        if(reply->format != 32) {
            printf_errf("The root window pixmap property format was not a 32bit int");
            free(reply);
            continue;
        }
        if(xcb_get_property_value_length(reply) == 0) {
            printf_errf("The root window pixmap property was 0 size");
            free(reply);
            continue;
        }

        xid = *(uint32_t*)xcb_get_property_value(reply);
        found = true;
        free(reply);
    }

    if(found) {
        struct Event event = {
            .type = ET_NEWROOT,
            .newRoot.pixmap = xid,
        };
        pushEvent(xctx, event);
    }
}

static bool findClosestClient(struct X11Context* xctx, Window top, Window* client) {
    Word_t rc;

    void* next = NULL;
    void* current = NULL;
    J1S(rc, current, top);
    Word_t count = 1;

    while(count > 0) {
        Word_t index = 0;
        Window* value;
        JLF(value, xctx->winParent, index);
        while(value != NULL) {
            J1T(rc, current, *value);
            if(rc == 0) {
                // This window is not a child of the
                // current frontier
                JLN(value, xctx->winParent, index);
                continue;
            }

            J1T(rc, xctx->client, index);
            if(rc == 0) {
                // The window is not a client, so we have
                // to continue the search
                J1S(rc, next, index);
                JLN(value, xctx->winParent, index);
                continue;
            }

            // The window was a client and our search is done

            J1FA(rc, next);
            J1FA(rc, current);
            *client = index;
            return true;
        }

        void* tmp = next;
        next = current;
        current = tmp;
        J1FA(rc, next);
        J1C(count, current, 0, -1);
    }

    return false;
}

static void fillBuffer(struct X11Context* xctx) {
    XEvent raw = {};
    XNextEvent(xctx->display, &raw);

    switch (raw.type) {
        case CreateNotify: {
            XCreateWindowEvent* ev = (XCreateWindowEvent *)&raw;
            if(ev->parent == xctx->root) {
                createAddWin(xctx, ev->window);
            } else {
                attachSubtree(xctx, ev->parent, ev->window);
            }
            break;
        }
        case DestroyNotify: {
            XDestroyWindowEvent* ev = (XDestroyWindowEvent *)&raw;
            createDestroyWin(xctx, ev->window);
            detachSubtree(xctx, ev->window);
            break;
        }
        case ReparentNotify: {
            XReparentEvent* ev = (XReparentEvent *)&raw;
            // Since we only composite top level windows, reparenting to the
            // root looks like an new window to us.
            if (ev->parent == xctx->root) {
                createAddWin(xctx, ev->window);
                detachSubtree(xctx, ev->window);
            } else {
                createDestroyWin(xctx, ev->window);
                Window frame = findRoot(xctx, ev->parent);

                Window oldClient;
                bool hasOldClient = findClosestClient(xctx, frame, &oldClient);

                attachSubtree(xctx, ev->parent, ev->window);

                Window client;
                if(findClosestClient(xctx, frame, &client)) {
                    if(!hasOldClient || client != oldClient){
                        createGetsClient(xctx, frame, client);
                    }
                }
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
                } else if (xctx->atoms->atom_xrootmapid == ev->atom
                        || xctx->atoms->atom_xsetrootid == ev->atom) {
                    createNewRoot(xctx);
                    break;
                }
            } else {
                if(ev->atom == xctx->atoms->atom_client) {
                    if(ev->state == PropertyNewValue) {
                        Window frame = findRoot(xctx, ev->window);
                        //Save the old client
                        Window oldClient;
                        bool hasOldClient = findClosestClient(xctx, frame, &oldClient);

                        Word_t rc;
                        J1S(rc, xctx->client, ev->window);
                        if(rc != 1) { // The window was already a client
                            break;
                        }

                        J1T(rc, xctx->active, ev->window);
                        if(rc == 1) {
                            // The window is a frame, which means we don't need
                            // to process the client (frames can't be clients).
                            // We still track client status if this get's
                            // parented down
                            return;
                        }

                        Window client;
                        if(findClosestClient(xctx, frame, &client)) {
                            if(!hasOldClient || client != oldClient){
                                createGetsClient(xctx, frame, client);
                            }
                        }
                    } else if(ev->state == PropertyDelete) {
                        Window frame = findRoot(xctx, ev->window);
                        //Save the old client
                        Window oldClient;
                        bool hasOldClient = findClosestClient(xctx, frame, &oldClient);

                        Word_t rc;
                        J1U(rc, xctx->client, ev->window);
                        assert(rc == 1); // The window is not a client

                        J1T(rc, xctx->active, ev->window);
                        if(rc == 1) {
                            // The window is a frame, which means we don't need
                            // to process the client (frames can't be clients).
                            // We still track client status if this get's
                            // parented down
                            return;
                        }

                        Window client;
                        if(findClosestClient(xctx, frame, &client)) {
                            if(!hasOldClient || client != oldClient){
                                createGetsClient(xctx, frame, client);
                            }
                        }
                    }
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
    createNewRoot(xctx);
}
