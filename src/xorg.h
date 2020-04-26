#pragma once

#include "atoms.h"

#include "vmath.h"
#include "vector.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include <stdbool.h>
#include <stdio.h>

#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xdbe.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/sync.h>

#include <Judy.h>

struct _session_t;

enum X11Protocol {
    PROTO_COMPOSITE,
    PROTO_FIXES,
    PROTO_DAMAGE,
    PROTO_RENDER,
    PROTO_SHAPE,
    PROTO_RANDR,
    PROTO_GLX,
    PROTO_XINERAMA,
    PROTO_SYNC,
    PROTO_COUNT,
};

extern const char* VERSION_NAMES[];
enum XExtensionVersion {
    XVERSION_NO,
    XVERSION_NO_OPT,
    XVERSION_YES,
    COMPOSITE_0_2,
};

struct X11Capabilities {
    int opcode[PROTO_COUNT];
    int event[PROTO_COUNT];
    int error[PROTO_COUNT];

    enum XExtensionVersion version[PROTO_COUNT];
};

enum EventType {
    ET_NONE,
    ET_ADD,
    ET_DESTROY,
    ET_CLIENT,
    ET_MANDR,
    ET_RESTACK,
    ET_FOCUS,
    ET_NEWROOT,
    ET_RAW,
};

struct AddWin {
    Window xid;
    Damage xdamage;
    float border_size;
    Vector2 pos;
    Vector2 size;
    bool mapped;
    bool override_redirect;
};

struct DestroyWin {
    Window xid;
};

struct GetsClient {
    Window xid;
    Window client_xid;
};

struct MandR {
    Window xid;
    Vector2 pos;
    Vector2 size;
    float border_size;
};

struct Restack {
    Window xid;
    Window above;
};

struct Focus {
    Window xid;
};

struct NewRoot {
    Pixmap pixmap;
};

struct Event {
    enum EventType type;
    union {
        struct AddWin add;
        struct DestroyWin des;
        struct GetsClient cli;
        struct MandR mandr;
        struct Restack restack;
        struct Focus focus;
        struct NewRoot newRoot;
        XEvent raw;
    };
};

struct X11Context {
    Display* display;
    int screen;
    Window root;

    struct X11Capabilities capabilities;

    GLXFBConfig* configs;
    int numConfigs;

    // @CLEANUP: This should be internal but currently it lives in the session
    struct Atoms* atoms;

    void* winParent;
    void* client;

    void* active;
    Vector eventBuf;
    size_t readCursor;
};

Atom get_atom(struct X11Context* context, const char* atom_name);
bool xorgContext_init(struct X11Context* context, Display* display, int screen, struct Atoms* atoms);

// @CLEANUP: Take the context instead of the capabilities
int xorgContext_ensure_capabilities(const struct X11Capabilities* caps);
int xorgContext_convertEvent(const struct X11Capabilities* caps, enum X11Protocol proto, int ev);
int xorgContext_convertError(const struct X11Capabilities* caps, enum X11Protocol proto, int ev);
enum XExtensionVersion xorgContext_version(const struct X11Capabilities* caps, enum X11Protocol proto);
enum X11Protocol xorgContext_convertOpcode(const struct X11Capabilities* caps, int opcode);

GLXFBConfig* xorgContext_selectConfig(struct X11Context* context, VisualID visualid);

void xorgContext_delete(struct X11Context* context);

/* Others */
void ev_handle(struct _session_t *ps, struct X11Capabilities* capabilities, XEvent *ev);

void xorg_nextEvent(struct X11Context* xcontext, struct Event* event);
void xorg_beginEvents(struct X11Context* xcontext);
