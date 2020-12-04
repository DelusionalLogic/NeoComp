#include "xorg.h"
#include "intercept/xorg.h"

#include "assert.h"
#include "logging.h"

#include "profiler/zone.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define REGISTER_PROP "_NET_WM_CM_S"

DECLARE_ZONE(x_error);

DECLARE_ZONE(select_config);
DECLARE_ZONE(select_config_visual);
DECLARE_ZONE(select_config_attribs);
DECLARE_ZONE(event_preprocess);

struct X11Context* current_xctx = NULL;

static char* eventName(struct X11Capabilities* caps, XErrorEvent* ev) {
    int o = 0;
#define CASESTRRET2(s)   case s: return #s; break

    o = xorgContext_convertError(caps, PROTO_FIXES, ev->error_code);
    switch (o) {
        CASESTRRET2(BadRegion);
    }

    o = xorgContext_convertError(caps, PROTO_DAMAGE, ev->error_code);
    switch (o) {
        CASESTRRET2(BadDamage);
    }

    o = xorgContext_convertError(caps, PROTO_RENDER, ev->error_code);
    switch (o) {
        CASESTRRET2(BadPictFormat);
        CASESTRRET2(BadPicture);
        CASESTRRET2(BadPictOp);
        CASESTRRET2(BadGlyphSet);
        CASESTRRET2(BadGlyph);
    }

    o = xorgContext_convertError(caps, PROTO_GLX, ev->error_code);
    switch (o) {
        CASESTRRET2(GLX_BAD_SCREEN);
        CASESTRRET2(GLX_BAD_ATTRIBUTE);
        CASESTRRET2(GLX_NO_EXTENSION);
        CASESTRRET2(GLX_BAD_VISUAL);
        CASESTRRET2(GLX_BAD_CONTEXT);
        CASESTRRET2(GLX_BAD_VALUE);
        CASESTRRET2(GLX_BAD_ENUM);
    }

    o = xorgContext_convertError(caps, PROTO_SYNC, ev->error_code);
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

static int xerror(Display *dpy, XErrorEvent *ev) {
    struct X11Context * xctx = current_xctx;
    assert(dpy == xctx->display);

    if (xorgContext_convertOpcode(&xctx->capabilities, ev->request_code) == PROTO_COMPOSITE
            && ev->minor_code == X_CompositeRedirectSubwindows) {
        fprintf(stderr, "Another composite manager is already running\n");
        exit(1);
    }

    const char* name = eventName(&xctx->capabilities, ev);

    struct timeval tm;
    if (gettimeofday(&tm, NULL))
        return 0;

    printf("[ %5ld.%02ld ] ", tm.tv_sec, tm.tv_usec / 10000);
#define BUF_LEN 80
    {
        zone_insta_extra(&ZONE_x_error, "%s:%ld", name, ev->request_code);
        char buf[BUF_LEN] = "";
        XGetErrorText(dpy, ev->error_code, buf, BUF_LEN);
        printf("error %4d %-12s request %4d minor %4d serial %6lu resource %4lu: \"%s\"\n",
                ev->error_code, name, ev->request_code,
                ev->minor_code, ev->serial, ev->resourceid, buf);
    }
#undef BUF_LEN

    /* print_backtrace(); */

    return 0;
}


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
    "X-Resource"
};

const char* VERSION_NAMES[] = {
    "NO",
    "NO (OPTIONAL)",
    "YES",
    "0.2+",
};

// @CLEANUP Reduce to one argument. No reason to take in the caps separately
static int xorgContext_capabilities(struct X11Capabilities* caps, struct X11Context* context) {
    // Fetch if the extension is available
    for(size_t i = 0; i < PROTO_COUNT; i++) {
        if(!XQueryExtensionH(context->display, X11Protocols_Names[i],
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
        XCompositeQueryVersionH(context->display, &major, &minor);

        if(major > 0 || (major == 0 && minor > 2)) {
            caps->version[PROTO_COMPOSITE] = COMPOSITE_0_2;
        }
    }

    if(caps->version[PROTO_FIXES] == XVERSION_YES) {
        int major;
        int minor;
        XFixesQueryVersionH(context->display, &major, &minor);
    }

    if(caps->version[PROTO_DAMAGE] == XVERSION_YES) {
        int major;
        int minor;
        XDamageQueryVersionH(context->display, &major, &minor);
    }

    if(caps->version[PROTO_RENDER] == XVERSION_YES) {
        int major;
        int minor;
        XRenderQueryVersionH(context->display, &major, &minor);
    }

    if(caps->version[PROTO_SHAPE] == XVERSION_YES) {
        int major;
        int minor;
        XShapeQueryVersionH(context->display, &major, &minor);
    }

    if(caps->version[PROTO_RANDR] == XVERSION_YES) {
        int major;
        int minor;
        XRRQueryVersionH(context->display, &major, &minor);
    }

    if(caps->version[PROTO_GLX] == XVERSION_YES) {
        int major;
        int minor;
        glXQueryVersionH(context->display, &major, &minor);
    }

    if(caps->version[PROTO_XINERAMA] == XVERSION_YES) {
        int major;
        int minor;
        XineramaQueryVersionH(context->display, &major, &minor);
    }

    if(caps->version[PROTO_SYNC] == XVERSION_YES) {
        int major;
        int minor;
        XSyncInitializeH(context->display, &major, &minor);
    }

    if(caps->version[PROTO_RES] == XVERSION_YES) {
        int major;
        int minor;
        XResQueryVersionH(context->display, &major, &minor);
    }

    return 0;
}

int winvis_compar(const void* av, const void* bv, void* userdata) {
    const struct WinVis* a = av;
    const struct WinVis* b = bv;
    return a->id - b->id;
};

static XTextProperty * make_text_prop(Display* dpy, char *str) {
    XTextProperty *pprop = calloc(1, sizeof(XTextProperty));
    if(pprop == NULL) {
        printf_errf("Allocation failure");
        exit(1);
    }

    if (XmbTextListToTextPropertyH(dpy, &str, 1,  XStringStyle, pprop)) {
        XFreeH(pprop->value);
        free(pprop);
        pprop = NULL;
    }

    return pprop;
}

static bool wid_set_text_prop(Display* dpy, Window wid, Atom prop_atom, char *str) {
    XTextProperty *pprop = make_text_prop(dpy, str);
    if (!pprop) {
        printf_errf("(\"%s\"): Failed to make text property.", str);
        return false;
    }

    XSetTextPropertyH(dpy, wid, pprop, prop_atom);
    XFreeH(pprop->value);
    XFreeH(pprop);

    return true;
}


static Window register_cm(Display* dpy, int screen, Window root) {
    Window win = XCreateSimpleWindowH(dpy, root, 0, 0, 1, 1, 0, None, None);

    if (!win) {
        printf_errf("(): Failed to create window.");
        return -1;
    }

    XCompositeUnredirectWindowH(dpy, win, CompositeRedirectManual);

    {
        XClassHint *h = XAllocClassHint();
        if (h) {
            h->res_name = "compton";
            h->res_class = "xcompmgr";
        }
        Xutf8SetWMPropertiesH(dpy, win, "xcompmgr", "xcompmgr", NULL, 0, NULL, NULL, h);
        XFreeH(h);
    }

    // Set _NET_WM_PID
    {
        long pid = getpid();
        if (!XChangePropertyH(dpy, win,
                    XInternAtomH(dpy, "_NET_WM_PID", False), XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *) &pid, 1)) {
            printf_errf("(): Failed to set _NET_WM_PID.");
        }
    }

    // Set COMPTON_VERSION
    if (!wid_set_text_prop(dpy, win, XInternAtomH(dpy, "COMPTON_VERSION", False), COMPTON_VERSION)) {
        printf_errf("(): Failed to set COMPTON_VERSION.");
    }

    // Acquire X Selection _NET_WM_CM_S
    unsigned len = strlen(REGISTER_PROP) + 2;
    int s = screen;

    while (s >= 10) {
        ++len;
        s /= 10;
    }

    char *buf = malloc(len);
    snprintf(buf, len, REGISTER_PROP "%d", screen);
    buf[len - 1] = '\0';
    XSetSelectionOwnerH(dpy, XInternAtomH(dpy, buf, False), win, 0);
    free(buf);

    return win;
}

bool xorgContext_init(struct X11Context* context, Display* display, int screen, struct Atoms* atoms) {
    assert(context != NULL);
    assert(display != NULL);

    assert(current_xctx == NULL);
    current_xctx = context;

    context->display = display;
    context->screen = screen;
    context->root = RootWindowH(display, screen);
    // The overlay is set later

    //Register the error handler
    XSetErrorHandler(xerror);
    // Flush all xorg calls immediately
    /* XSynchronize(context->display, true); */

    context->configs = glXGetFBConfigsH(display, screen, &context->numConfigs);
    if(context->configs == NULL) {
        printf_errf("Failed retrieving the fboconfigs");
        return false;
    }
    vector_init(&context->cfgs, sizeof(struct WinVis), context->numConfigs);
    for(int i = 0; i < context->numConfigs; i++) {
        int value; // Scratch value
        struct WinVis vis;
        GLXFBConfig fbconfig = context->configs[i];

        glXGetFBConfigAttribH(display, fbconfig, GLX_BIND_TO_TEXTURE_TARGETS_EXT, &value);
        if (!(value & GLX_TEXTURE_2D_BIT_EXT))
            // Cant bind to 2d textures, not supported;
            continue;

        glXGetFBConfigAttribH(display, fbconfig, GLX_BIND_TO_TEXTURE_RGB_EXT, &value);
        vis.rgb = value;

        glXGetFBConfigAttribH(display, fbconfig, GLX_BIND_TO_TEXTURE_RGBA_EXT, &value);
        vis.rgba = value;

        if(!vis.rgba && !vis.rgb) {
            // Cant bind rgb or rgba, not supported.
            continue;
        }

        glXGetFBConfigAttribH(context->display, fbconfig, GLX_VISUAL_ID, (int*)&vis.id);

        vis.raw = &context->configs[i];

        vector_putBack(&context->cfgs, &vis);
    }

    vector_qsort(&context->cfgs, winvis_compar, NULL);

    xorgContext_capabilities(&context->capabilities, context);

    context->winParent = NULL;
    context->client = NULL;
    context->damage = NULL;
    context->mapped = NULL;
    context->active = NULL;
    context->bypassed = NULL;
    vector_init(&context->eventBuf, sizeof(struct Event), 64);
    context->readCursor = 0;

    context->atoms = atoms;
    atoms_init(atoms, context->display);

    context->reg = register_cm(display, screen, context->root);
    if(context->reg == -1) {
        printf_errf("Can't lock compositor");
        return false;
    }

    XResClientIdSpec spec = {
        .client = context->reg,
        .mask = XRES_CLIENT_ID_XID_MASK
    };
    long ret_ids = 0;
    XResClientIdValue* value;
    XResQueryClientIdsH(display, 1, &spec, &ret_ids, &value);
    if(ret_ids != 0) {
        context->xres = value->spec.client;
    }
    XFree(value);

    vector_init(&context->resAtoms, sizeof(struct AtomEntry), 16);
    vector_init(&context->resNames, sizeof(char*), 16);

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

int winvis_lookup(const void* av, const void* bv, void* userdata) {
    const VisualID* a = av;
    const struct WinVis* b = bv;
    return *a - b->id;
};

GLXFBConfig* xorgContext_selectConfig(struct X11Context* context, VisualID visualid) {
    zone_scope(&ZONE_select_config);
    assert(visualid != 0);

    size_t loc = vector_bisect(&context->cfgs, &visualid, winvis_lookup, NULL);
    if(loc == -1) {
        return NULL;
    }

    struct WinVis* vis = vector_get(&context->cfgs, loc);
    return vis->raw;
}

void xorgContext_delete(struct X11Context* context) {
    assert(context != NULL);
    assert(context->display != NULL);

    assert(current_xctx == context);
    current_xctx = NULL;

    free(context->configs);
}

static bool isWindowActive(const struct X11Context* xctx, Window w) {
    Word_t rc;
    J1T(rc, xctx->active, w);
    return rc != 0;
}

static bool isWindowMapped(const struct X11Context* xctx, Window w) {
    Word_t rc;
    J1T(rc, xctx->mapped, w);
    return rc != 0;
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

static Window findRoot(const struct X11Context* xctx, Window xid) {
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

static void windowCreate(struct X11Context* xctx, Window xid, int x, int y, int border, int width, int height) {
    Word_t rc;
    J1S(rc, xctx->active, xid);
    if(rc == 0) {
        // @CLEANUP @CONSISTENCY: I really want to assert here, but during
        // initialization someone might create a window in between our calls to
        // subscribe to window create events and bootstrap the state
        // assert(false);
        // For now just do nothing
        return;
    }

    Damage damage = XDamageCreateH(xctx->display, xid, XDamageReportNonEmpty);
    uint64_t* pValue;
    JLI(pValue, xctx->damage, xid);
    if(pValue == PJERR) {
        printf_errf("Allocation error");
        return;
    }
    *pValue = damage;

    if (xorgContext_version(&xctx->capabilities, PROTO_SHAPE) >= XVERSION_YES) {
        // Subscribe to events when the window shape changes
        XShapeSelectInputH(xctx->display, xid, ShapeNotifyMask);
    }

    struct Event event = {
        .type = ET_ADD,
        .add.xid = xid,
        .add.mapped = false,
        .add.pos = (Vector2){{x - border, y - border}},
        .add.size = (Vector2){{
          width + border * 2,
          height + border * 2,
        }},
        .add.border_size = border,
    };
    pushEvent(xctx, event);
}

static bool findClosestClient(const struct X11Context* xctx, const Window top, Window* client) {
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

static bool isWindowBypassed(struct X11Context* xctx, Window xid) {
    Word_t rc;
    J1T(rc, xctx->bypassed, xid);
    if(rc == 1) {
        return true;
    }
    return false;
}

static bool isFrameBypassed(struct X11Context* xctx, Window xid) {

    if(isWindowBypassed(xctx, xid)) {
        return true;
    }

    Window client;
    bool hasClient = findClosestClient(xctx, xid, &client);
    if(hasClient) {
        if(isWindowBypassed(xctx, client)) {
            return true;
        }
    }

    return false;
}

static void windowMap(struct X11Context* xctx, Window xid) {
    Word_t rc;
    J1S(rc, xctx->mapped, xid);
    assert(rc == 1);

    if(!isWindowActive(xctx, xid))
        return;

    struct Event event;
    if(isFrameBypassed(xctx, xid)) {
        event = (struct Event){
            .type = ET_BYPASS,
            .bypass.xid = xid,
        };
    } else {
        event = (struct Event){
            .type = ET_MAP,
            .map.xid = xid,
        };
    }
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

    JLD(rc_int, xctx->damage, xid);
    if(rc_int == JERR) {
        printf_errf("Alloc error");
        return;
    } else if(rc_int == 0) {
        printf_errf("WARN Window didn't have damage");
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

    assert(isWindowActive(xctx, xid));

    struct Event event = {
        .type = ET_CLIENT,
        .cli.xid = xid,
        .cli.client_xid = client_xid,
    };
    pushEvent(xctx, event);
}

static void createMandr(struct X11Context* xctx, Window xid, float x, float y, float border, bool or, float width, float height, Window above) {
    // The window wasn't active, so we swallow the destroy
    if(!isWindowActive(xctx, xid))
        return;

    struct Event event = {
        .type = ET_MANDR,
        .mandr.xid = xid,
        .mandr.pos = (Vector2){{x - border, y - border}},
        .mandr.size = (Vector2){{
          width + border * 2,
          height + border * 2,
        }},
        .mandr.border_size = border,
        .mandr.override_redirect = or,
    };
    pushEvent(xctx, event);
    struct Event restack = {
        .type = ET_RESTACK,
        .restack.xid = xid,
        .restack.loc = LOC_BELOW,
        .restack.above = above,
    };
    pushEvent(xctx, restack);
}

static void refreshFocus(struct X11Context* xctx) {
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

static void refreshRoot(struct X11Context* xctx) {
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

static int32_t xBypassState(const struct X11Context* xctx, const Window win) {
    xcb_connection_t* xcb = XGetXCBConnectionH(xctx->display);

    xcb_get_property_cookie_t cookie = xcb_get_propertyH(xcb, false, win, xctx->atoms->atom_bypass, XA_CARDINAL, 0, 1);

    xcb_generic_error_t *error;
    xcb_get_property_reply_t *reply = xcb_get_property_replyH(xcb, cookie, &error);
    if(reply == NULL) {
        printf_errf("Failed fetching bypass property: code %d", error->error_code);
        free(error);
        return -1;
    }

    if(reply->type == None) {
        // The property was not found, so just move on
        free(reply);
        return -1;
    }

    if(reply->type != XA_CARDINAL) {
        printf_errf("The _NET_WM_BYPASS_COMPOSITOR property was not a cardinal");
        free(reply);
        return -1;
    }
    if(reply->format != 32) {
        printf_errf("The _NET_WM_BYPASS_COMPOSITOR property was not 32bit int");
        free(reply);
        return -1;
    }
    if(xcb_get_property_value_lengthH(reply) == 0) {
        printf_errf("The _NET_WM_BYPASS_COMPOSITOR property was 0 size");
        free(reply);
        return -1;
    }

    int32_t bypass = *(uint32_t*)xcb_get_property_valueH(reply);
    free(reply);

    return bypass;
}

static bool findAffectedWindow(const struct X11Context* xctx, const Window win, Window* affected) {
    Word_t rc;

    Window frame = findRoot(xctx, win);
    // If the frame isn't active dont emit
    if(!isWindowActive(xctx, frame)) {
        return false;
    }

    // If we are the toplevel, emit. If not, we have to
    // check if we are the current client
    if(frame == win) {
        *affected = frame;
        return true;
    }

    // If the window isn't a client it can't affect the
    // frame
    J1T(rc, xctx->client, win);
    if(rc == 0) {
        return false;
    }

    Window client;
    bool found = findClosestClient(xctx, frame, &client);
    assert(found);
    // If the window with this event isn't the client, we
    // dont care
    if(client != win)
        return false;

    *affected = frame;
    return true;
}


static void fillBuffer(struct X11Context* xctx) {
    XEvent raw = {};
    XNextEventH(xctx->display, &raw);

    switch (raw.type) {
        case CreateNotify: {
            XCreateWindowEvent* ev = (XCreateWindowEvent *)&raw;
            // Ignore our overlay  and reg window.
            if(ev->window == xctx->overlay) break;
            if(ev->window == xctx->reg) break;
            zone_scope_extra(&ZONE_event_preprocess, "Create");
            if(ev->parent == xctx->root) {
                windowCreate(xctx, ev->window, ev->x, ev->y, ev->border_width, ev->width, ev->height);
                XSelectInputH(xctx->display, ev->window, PropertyChangeMask);
            } else {
                attachSubtree(xctx, ev->parent, ev->window);
                // Watch for possible WM_STATE
                XSelectInputH(xctx->display, ev->window, PropertyChangeMask);
            }
            break;
        }
        case DestroyNotify: {
            XDestroyWindowEvent* ev = (XDestroyWindowEvent *)&raw;
            zone_scope_extra(&ZONE_event_preprocess, "Destroy");

            Window frame = findRoot(xctx, ev->window);
            bool old_bypassed = isFrameBypassed(xctx, frame);

            Word_t rc;
            J1U(rc, xctx->mapped, ev->window);

            createDestroyWin(xctx, ev->window);
            detachSubtree(xctx, ev->window);

            bool bypassed = isFrameBypassed(xctx, frame);
            if(frame != ev->window
                    && old_bypassed != bypassed) {
                // It's not possible for a destroy to make a window suddenly bypassed
                assert(bypassed == false);
                if(isWindowMapped(xctx, frame)) {
                    struct Event event = {
                        .type = ET_MAP,
                        .map.xid = frame,
                    };
                    pushEvent(xctx, event);
                }
            }

            break;
        }
        case ReparentNotify: {
            XReparentEvent* ev = (XReparentEvent *)&raw;
            zone_scope_extra(&ZONE_event_preprocess, "ReparentNotify");
            // Since we only composite top level windows, reparenting to the
            // root looks like an new window to us.
            if (ev->parent == xctx->root) {
                XWindowAttributes attribs;
                if(!XGetWindowAttributesH(xctx->display, ev->window, &attribs)) {
                    break;
                }
                windowCreate(xctx, ev->window, attribs.x, attribs.y, attribs.border_width, attribs.width, attribs.height);

                if(isFrameBypassed(xctx, ev->window)) {
                    Word_t rc;
                    J1T(rc, xctx->mapped, ev->window);
                    if(rc == 1) {
                        struct Event event = {
                            .type = ET_BYPASS,
                            .bypass.xid = ev->window,
                        };
                        pushEvent(xctx, event);
                    }
                } else {
                    Word_t rc;
                    J1T(rc, xctx->mapped, ev->window);
                    if(rc == 1) {
                        struct Event map = {
                            .type = ET_MAP,
                            .map.xid = ev->window,
                        };
                        pushEvent(xctx, map);
                    }
                }

                detachSubtree(xctx, ev->window);
            } else {
                // DestroyWin expects the damage to already be freed.
                Damage* damage;
                JLG(damage, xctx->damage, ev->window);
                if(damage != NULL) {
                    // @HACK @COMPLETENESS: We have some tests that reparent
                    // windows from one subwindow to another. This doesn't
                    // actually occur, since we don't subscribe to substructure
                    // notify on anything but the root. That causes other
                    // problems though, so we will have to fix that at some
                    // point. For that reason i'm keeping the tests and keeping
                    // this code even though it will never be hit.
                    XDamageDestroyH(xctx->display, *damage);
                }

                createDestroyWin(xctx, ev->window);
                Window frame = findRoot(xctx, ev->parent);

                Window oldClient;
                bool hasOldClient = findClosestClient(xctx, frame, &oldClient);

                attachSubtree(xctx, ev->parent, ev->window);

                Window client;
                if(findClosestClient(xctx, frame, &client)) {
                    if(!hasOldClient || client != oldClient){
                        createGetsClient(xctx, frame, client);
                        if(isWindowBypassed(xctx, client)) {
                            struct Event event = {
                                .type = ET_BYPASS,
                                .bypass.xid = frame,
                            };
                            pushEvent(xctx, event);
                        }
                    }
                }
            }
            break;
        }
        case ConfigureNotify: {
            XConfigureEvent* ev = (XConfigureEvent *)&raw;
            zone_scope_extra(&ZONE_event_preprocess, "Configure");
            if(ev->window == xctx->root) {
                // Root changes change the rendering canvas
                struct Event event = {
                    .type = ET_CCHANGE,
                    .cchange.size = (Vector2){{ev->width, ev->height}},
                };
                pushEvent(xctx, event);
            } else {
                createMandr(xctx, ev->window, ev->x, ev->y, ev->border_width, ev->override_redirect, ev->width, ev->height, ev->above);
            }
            break;
        }
        case CirculateNotify: {
            XCirculateEvent* ev = (XCirculateEvent*)&raw;
            zone_scope_extra(&ZONE_event_preprocess, "Circulate");
            if(!isWindowActive(xctx, ev->window)) {
                break;
            }
            struct Event restack = {
                .type = ET_RESTACK,
                .restack.xid = ev->window,
                .restack.loc = ev->place == PlaceOnTop ? LOC_HIGHEST : LOC_LOWEST,
            };
            pushEvent(xctx, restack);
            break;
        }
        case MapNotify: {
            XMapEvent* ev = (XMapEvent*)&raw;
            zone_scope_extra(&ZONE_event_preprocess, "Map");
            windowMap(xctx, ev->window);
            break;
        }
        case UnmapNotify: {
            XUnmapEvent* ev = (XUnmapEvent*)&raw;
            zone_scope_extra(&ZONE_event_preprocess, "Unmap");
            // Some applications (firefox) send their own unmap events. I don't
            // care if they say the window is unmapped, if it's actually
            // unmapped Xorg will tell us.
            if(ev->send_event == true)
                break;

            Word_t rc;
            J1U(rc, xctx->mapped, ev->window);
            assert(rc == 1);

            XSelectInputH(xctx->display, ev->window, PropertyChangeMask);

            struct Event unmap = {
                .type = ET_UNMAP,
                .unmap.xid = ev->window,
            };
            pushEvent(xctx, unmap);
            break;
        }
        case PropertyNotify: {
            XPropertyEvent* ev = (XPropertyEvent *)&raw;
            zone_scope_extra(&ZONE_event_preprocess, "PropertyNotify");
            if(ev->window == xctx->root) {
                if (xctx->atoms->atom_ewmh_active_win == ev->atom) {
                    refreshFocus(xctx);
                    break;
                } else if (xctx->atoms->atom_xrootmapid == ev->atom
                        || xctx->atoms->atom_xsetrootid == ev->atom) {
                    refreshRoot(xctx);
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

                        if(isWindowActive(xctx, ev->window)) {
                            // The window is a frame, which means we don't need
                            // to process the client (frames can't be clients).
                            // We still track client status if this get's
                            // parented down
                            break;
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

                        if(isWindowActive(xctx, ev->window)) {
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
                } else if(ev->atom == xctx->atoms->atom_win_type
                    || ev->atom == xctx->atoms->atom_name_ewmh
                    || ev->atom == xctx->atoms->atom_role
                    || ev->atom == xctx->atoms->atom_name) {

                    Window affected;
                    if(!findAffectedWindow(xctx, ev->window, &affected)) {
                        break;
                    }

                    struct Event event = {
                        .type = ET_WINTYPE,
                        .wintype.xid = ev->window,
                    };
                    pushEvent(xctx, event);
                } else if(ev->atom == xctx->atoms->atom_class) {

                    Window affected;
                    if(!findAffectedWindow(xctx, ev->window, &affected)) {
                        break;
                    }

                    struct Event event = {
                        .type = ET_WINTYPE,
                        .wintype.xid = affected,
                    };
                    pushEvent(xctx, event);
                    struct Event classEvent = {
                        .type = ET_WINCLASS,
                        .winclass.xid = affected,
                    };
                    pushEvent(xctx, classEvent);
                } else if(ev->atom == xctx->atoms->atom_bypass) {

                    Window affected;
                    if(!findAffectedWindow(xctx, ev->window, &affected)) {
                        break;
                    }

                    if(ev->state == PropertyNewValue) {
                        if(xBypassState(xctx, ev->window) == 1) {
                            Word_t rc;
                            J1S(rc, xctx->bypassed, ev->window);
                            if(rc == 0) {
                                break;
                            }

                            if(isWindowMapped(xctx, affected)) {
                                struct Event event = {
                                    .type = ET_BYPASS,
                                    .bypass.xid = affected,
                                };
                                pushEvent(xctx, event);
                            }
                        } else {
                            Word_t rc;
                            J1U(rc, xctx->bypassed, ev->window);
                            if(rc == 1 && isWindowMapped(xctx, affected)) {
                                struct Event event = {
                                    .type = ET_MAP,
                                    .map.xid = affected,
                                };
                                pushEvent(xctx, event);
                            }
                        }
                    } else if(ev->state == PropertyDelete) {
                        Word_t rc;
                        J1U(rc, xctx->bypassed, ev->window);
                        if(rc == 1 && isWindowMapped(xctx, affected)) {
                            struct Event event = {
                                .type = ET_MAP,
                                .bypass.xid = affected,
                            };
                            pushEvent(xctx, event);
                        }
                    }
                }
                break;
            }
            // Fallthrough
        }
        default: {
            if(xorgContext_convertEvent(&xctx->capabilities, PROTO_DAMAGE,  raw.type) == XDamageNotify) {

                XDamageNotifyEvent* ev = (XDamageNotifyEvent*)&raw;
                zone_scope_extra(&ZONE_event_preprocess, "Damage(%#010X)", ev->drawable);

                assert(isWindowActive(xctx, ev->drawable));

                // We need to subtract the damage, even if we aren't mapped. If we don't
                // subtract the damage, we won't be notified of any new damage in the
                // future.
                Damage* damage;
                JLG(damage, xctx->damage, ev->drawable);
                XDamageSubtractH(xctx->display, *damage, None, None);

                if(isWindowBypassed(xctx, ev->drawable))
                    break;

                if(!isWindowMapped(xctx, ev->drawable))
                    break;

                struct Event event = {
                    .type = ET_DAMAGE,
                    .damage.xid = ev->drawable,
                };
                pushEvent(xctx, event);

                break;
            } else if(xorgContext_version(&xctx->capabilities, PROTO_SHAPE) >= XVERSION_YES
                    && xorgContext_convertEvent(&xctx->capabilities, PROTO_SHAPE, raw.type) == ShapeNotify) {
                XShapeEvent* ev = (XShapeEvent*)&raw;
                zone_scope_extra(&ZONE_event_preprocess, "Shape");

                if(!isWindowActive(xctx, ev->window)) {
                    break;
                }

                struct Event event = {
                    .type = ET_SHAPE,
                    .shape.xid = ev->window,
                };
                pushEvent(xctx, event);
            } else {
                // Discard unknown events
            }
        }
    }
}

void xorg_nextEvent(struct X11Context* xctx, struct Event* event) {
    while(true) {
        if(xctx->readCursor < vector_size(&xctx->eventBuf)) {
            *event = *(struct Event*)vector_get(&xctx->eventBuf, xctx->readCursor++);
            return;
        }

        if(!XEventsQueuedH(xctx->display, QueuedAfterReading)) {
            event->type = ET_NONE;
            return;
        }

        assert(xctx->readCursor >= vector_size(&xctx->eventBuf));

        xctx->readCursor = 0;
        vector_clear(&xctx->eventBuf);
        fillBuffer(xctx);
    }
}

static bool hasProperty(xcb_connection_t* xcb, Atom atom, Window xid) {
    xcb_get_property_cookie_t cookie = xcb_get_propertyH(xcb, false, xid, atom, XA_CARDINAL, 0, 1);

    xcb_generic_error_t *error;
    xcb_get_property_reply_t *reply = xcb_get_property_replyH(xcb, cookie, &error);
    if(reply == NULL) {
        printf_errf("Failed initializing window: code %d", error->error_code);
        free(error);
        return false;
    }

    bool ret = reply->type != None;
    free(reply);
    return ret;
}

static void recurseWindow(struct X11Context* xctx, Window xid);

static void beginSubWindow(struct X11Context* xctx, Window xid, Window parent) {
    attachSubtree(xctx, parent, xid);
    XSelectInputH(xctx->display, xid, PropertyChangeMask);

    XWindowAttributes attribs;
    if (!XGetWindowAttributesH(xctx->display, xid, &attribs)) {
        // Failed to get window attributes probably means the window is gone
        // already.
        return;
    }

    if(attribs.class == InputOnly) {
        return;
    }

    recurseWindow(xctx, xid);

    if(attribs.map_state == IsViewable) {
        windowMap(xctx, xid);
    }

    xcb_connection_t* xcb = XGetXCBConnectionH(xctx->display);

    if(hasProperty(xcb, xctx->atoms->atom_client, xid)) {
        Word_t rc;
        J1S(rc, xctx->client, xid);
        assert(rc == 1);
    }

}

static void recurseWindow(struct X11Context* xctx, Window xid) {
    Window root_return, parent_return;
    Window *children;
    unsigned int nchildren;

    XQueryTree(xctx->display, xid, &root_return, &parent_return,
            &children, &nchildren);

    for (unsigned i = 0; i < nchildren; i++) {
        beginSubWindow(xctx, children[i], xid);
    }
}


static void beginTopWindow(struct X11Context* xctx, Window xid) {
    if(xid == xctx->overlay || xid == xctx->reg) {
        // Ignore our overlay and reg window.
        return;
    }

    XWindowAttributes attribs;
    if (!XGetWindowAttributesH(xctx->display, xid, &attribs)) {
        // Failed to get window attributes probably means the window is gone
        // already.
        return;
    }

    if(attribs.class == InputOnly) {
        return;
    }

    windowCreate(xctx, xid, attribs.x, attribs.y, attribs.border_width, attribs.width, attribs.height);
    XSelectInputH(xctx->display, xid, PropertyChangeMask);

    recurseWindow(xctx, xid);

    Window client;
    if(findClosestClient(xctx, xid, &client)) {
        createGetsClient(xctx, xid, client);
    } else {
        client = xid;
    }

    if(xBypassState(xctx, client) == 1) {
        Word_t rc;
        J1S(rc, xctx->bypassed, client);
        assert(rc != 0);
    }

    if(attribs.map_state == IsViewable) {
        windowMap(xctx, xid);
    }
}

// Synthesize events for the initial state
void xorg_beginEvents(struct X11Context* xctx) {
    Window root_return, parent_return;
    Window *children;
    unsigned int nchildren;

    XQueryTree(xctx->display, xctx->root, &root_return,
            &parent_return, &children, &nchildren);

    for (unsigned i = 0; i < nchildren; i++) {
        beginTopWindow(xctx, children[i]);
    }

    XFree(children);

    refreshFocus(xctx);
    refreshRoot(xctx);
}

static int atomEntry_compar(const void* ptrNeedle, const void* ptrPivot, void* userdata) {
    Atom* needle = (Atom*)ptrNeedle;
    struct AtomEntry* pivot = (struct AtomEntry*)ptrPivot;

    return (*needle) - pivot->atom;
}

static int atomEntry_atomSort(const void* ptrA, const void* ptrB, void* userdata) {
    struct AtomEntry* a = (struct AtomEntry*)ptrA;
    struct AtomEntry* b = (struct AtomEntry*)ptrB;

    return a->atom - b->atom;
}

void xorg_resource(struct X11Context* xctx, struct XResourceUsage* usage) {
    int numRes;
    XResType* resTypes;
    XResQueryClientResourcesH(xctx->display, xctx->xres, &numRes, &resTypes);

    size_t slots[numRes];
    for(int i = 0; i < numRes; i++) {
        XResType* resType = &resTypes[i];

        size_t entrySlot = vector_bisect(&xctx->resAtoms, &resType->resource_type, &atomEntry_compar, NULL);
        if(entrySlot == -1) {
            slots[i] = -1;
            continue;
        }

        slots[i] = ((struct AtomEntry*)vector_get(&xctx->resAtoms, entrySlot))->index;
    }

    size_t new = 0;
    size_t nextSlot = vector_size(&xctx->resNames);
    for(int i = 0; i < numRes; i++) {
        if(slots[i] != -1) {
            continue;
        }

        XResType* resType = &resTypes[i];
        slots[i] = nextSlot + new++;

        struct AtomEntry entry = {
            .atom = resType->resource_type,
            .index = slots[i],
        };
        vector_putBack(&xctx->resAtoms, &entry);

        char* name = XGetAtomName(xctx->display, resType->resource_type);
        vector_putBack(&xctx->resNames, &name);
    }

    vector_qsort(&xctx->resAtoms, &atomEntry_atomSort, NULL);

    // Make space for new values
    vector_reserve(&usage->values, new);

    for(int i = 0; i < numRes; i++) {
        XResType* resType = &resTypes[i];
        assert(slots[i] != -1);

        uint64_t* value = vector_get(&usage->values, slots[i]);
        *value = resType->count;
    }
    // @HACK @FRAGILE
    usage->names = (char**)xctx->resNames.data;


    XFree(resTypes);
}
