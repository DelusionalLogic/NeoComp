#include "intercept/xorg.h"
#include "test/xorg.h"

#include "vector.h"

#include <Judy.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib-xcb.h>
#include <X11/Xatom.h>

size_t qCursor = 0;
Vector eventQ;
void* windowAttrs;

Window RootWindowH(Display* dpy, int scr) {
    return 0;
}

int XNextEventH(Display* dpy, XEvent* ev) {
    *ev = *(XEvent*)vector_get(&eventQ, qCursor);
    qCursor++;
    return 0;
}

int XEventsQueuedH(Display* dpy, int mode) {
    return vector_size(&eventQ) - qCursor;
}

Status XGetWindowAttributesH(Display* dpy, Window window, XWindowAttributes* attrs) {
    XWindowAttributes** value;
    JLG(value, windowAttrs, window);
    if(value != NULL) {
        *attrs = **value;
        return 1;
    }

    return XGetWindowAttributes(dpy, window, attrs);
}

GLXFBConfig* glXGetFBConfigsH(Display* dpy, int scr, int* num) {
    GLXFBConfig* config = malloc(sizeof(GLXFBConfig));
    *num = 1;
    return config;
}
Bool XQueryExtensionH(Display* dpy, const char* name, int* opcode, int* event, int* error) {
    if(strcmp(name, DAMAGE_NAME) == 0) {
        *opcode = 1;
        *event = 1;
        *error = 1;
    } else if(strcmp(name, SHAPENAME)) {
        *opcode = 2;
        *event = 2;
        *error = 2;
    } else {
        *opcode = 0;
        *event = 0;
        *error = 0;
    }
    return True;
}
Status XCompositeQueryVersionH(Display* dpy, int* major, int* minor) {
    *major = 0;
    *minor = 3;
    return 1;
}
void XFixesIntersectRegionH(Display* dpy, XserverRegion dst, XserverRegion src1, XserverRegion src2) {
}
XRectangle* XFixesFetchRegionH(Display* dpy, XserverRegion region, int* count_ret) {
	return NULL;
}
Status XFixesQueryVersionH(Display* dpy, int* major, int* minor) {
    return 1;
}
Status XDamageQueryVersionH(Display* dpy, int* major, int* minor) {
    return 1;
}
Status XRenderQueryVersionH(Display* dpy, int* major, int* minor) {
    return 1;
}
Status XShapeQueryVersionH(Display* dpy, int* major, int* minor) {
    return 1;
}
Status XRRQueryVersionH(Display* dpy, int* major, int* minor) {
    return 1;
}
Status glXQueryVersionH(Display* dpy, int* major, int* minor) {
    return 1;
}
Status XineramaQueryVersionH(Display* dpy, int* major, int* minor) {
    return 1;
}
Status XResQueryVersionH(Display* dpy, int* major, int* minor) {
    return 1;
}
Status XSyncInitializeH(Display* dpy, int* major, int* minor) {
    return 1;
}

int XmbTextListToTextPropertyH(Display* display,  char** list,  int count,  XICCEncodingStyle style,  XTextProperty* text_prop_return) {
    return 0;
}
void XSetTextPropertyH(Display* display,  Window w,  XTextProperty* text_prop,  Atom property) {
    return;
}
Window XCreateSimpleWindowH(Display* display,  Window parent,  int x,  int y,  unsigned int width,  unsigned int height,  unsigned int border_width,  unsigned long border,  unsigned long background) {
    return 0;
}
void XCompositeUnredirectWindowH(Display* dpy,  Window window,  int update) {
    return;
}
void Xutf8SetWMPropertiesH(Display* display,  Window w,  _Xconst char* window_name,  _Xconst char* icon_name,  char** argv,  int argc,  XSizeHints* normal_hints,  XWMHints* wm_hints,  XClassHint* class_hints) {
    return;
}
int XChangePropertyH(Display* display,  Window w,  Atom property,  Atom type,  int format,  int mode,  _Xconst unsigned char* data,  int nelements) {
    return 0;
}
int XSetSelectionOwnerH(Display* display,  Atom selection,  Window owner,  Time time) {
    return 0;
}
int XFreeH(void* data) {
    return 0;
}
Status XResQueryClientIdsH(Display* dpy,  long num_specs,  XResClientIdSpec* client_specs,  long* num_ids,  XResClientIdValue** client_ids) {
    assert(false);
    return 0;
}
Status XResQueryClientResourcesH(Display* dpy,  XID xid,  int* num_types,  XResType** types) {
    assert(false);
    return 0;
}

void* atoms;
int nextAtom = 1;

Atom XInternAtomH(Display* dpy, const char* name, Bool query) {
    Atom* value;
    JSLI(value, atoms, name);
    if(*value == 0) {
        *value = nextAtom++;
    }
    return *value;
}

Damage XDamageCreateH(Display* dpy, Window win, int level) {
    return 0;
}

void XDamageDestroyH(Display* dpy, Damage damage) {
}

void XDamageSubtractH(Display* dpy, Damage damage, XserverRegion repair, XserverRegion parts) {
}

void XShapeSelectInputH(Display* dpy, Window win, unsigned long mask) {
}


XserverRegion XFixesCreateRegionH(Display* dpy, XRectangle* rectangles, int nrectangles) {
    return 0;
}

XserverRegion XFixesCreateRegionFromWindowH(Display* dpy, Window window, int kind) {
    return 0;
}

void XFixesTranslateRegionH(Display* dpy, XserverRegion region, int dx, int dy) {
}

void XFixesUnionRegionH(Display* dpy, XserverRegion dst, XserverRegion src1, XserverRegion src2) {
}

void XFixesInvertRegionH(Display* dpy, XserverRegion dst, XRectangle* rect, XserverRegion src) {
}

void XFixesDestroyRegionH(Display* dpy, XserverRegion region) {
}

void XFixesSetWindowShapeRegionH(Display* dpy, Window win, int shape_kind, int x_off, int y_off, XserverRegion region) {
}

int glXGetFBConfigAttribH(Display* dpy, GLXFBConfig config, int attribute, int* value) {
    return 0;
}

xcb_connection_t* XGetXCBConnectionH(Display *dpy) {
    return NULL;
}

struct windowProperty {
    struct xcb_get_property_reply_t reply;
    char value[];
};
struct windowProperty nullProperty = {
    .reply = {
        .type = None,
        .format = 0,
        .length = sizeof(xcb_get_property_reply),
        .value_len = 0,
    },
    .value = {0, 0, 0, 0},
};
void* winProp;
void* reqs;
uint64_t nextSeq = 0;
xcb_get_property_cookie_t xcb_get_propertyH(xcb_connection_t* conn, uint8_t _delete, xcb_window_t window, xcb_atom_t property, xcb_atom_t type, uint32_t long_offset, uint32_t long_length) {
    void** perWindow;
    JLG(perWindow, winProp, window);

    struct windowProperty** value = NULL;
    if(perWindow != NULL)
        JLG(value, *perWindow, property);

    struct windowProperty** requestPtr;
    JLI(requestPtr, reqs, nextSeq);
    assert(requestPtr != NULL);

    if(value != NULL) {
        *requestPtr = *value;
    } else {
        *requestPtr = &nullProperty;
    }

    return (xcb_get_property_cookie_t) {
        .sequence = nextSeq++
    };
}

xcb_get_property_reply_t* xcb_get_property_replyH(xcb_connection_t* conn, xcb_get_property_cookie_t cookie, xcb_generic_error_t **e) {
    *e = NULL;

    struct windowProperty** value;
    JLG(value, reqs, cookie.sequence);
    assert(value != NULL);

    struct windowProperty* ret = malloc(sizeof(struct windowProperty) + (*value)->reply.value_len * 4);
    memcpy(ret, *value, sizeof(struct windowProperty) + (*value)->reply.value_len * 4);

    return &ret->reply;
}

void* xcb_get_property_valueH(const xcb_get_property_reply_t* reply) {
    struct windowProperty* prop = (struct windowProperty*) reply;
    return &prop->value;
}

int xcb_get_property_value_lengthH(const xcb_get_property_reply_t* reply) {
    return reply->value_len;
}

void setProperty(Window win, Atom atom, uint32_t value) {
    uint32_t value_len = 4;
    uint32_t total_len = sizeof(struct windowProperty) + value_len;
    struct windowProperty* prop = malloc(total_len);
    assert(prop != NULL);

    *prop = (struct windowProperty) {
        .reply = {
            .response_type = XCB_GET_PROPERTY,
            .length = total_len,
            .format = 32,
            .type = XA_CARDINAL,
            .bytes_after = 0,
            .value_len = value_len / 4,
        },
    };

    memcpy(&prop->value, &value, sizeof(value_len));

    // Not always an insert. If the value is already there we just modify it
    void** perWindow;
    JLI(perWindow, winProp, win);
    assert(perWindow != NULL);

    struct windowProperty** propPtr;
    JLI(propPtr, *perWindow, atom);
    assert(propPtr != NULL);

    *propPtr = prop;
}

void* inputMasks;
int XSelectInputH(Display* dpy, Window win, long mask) {
    long* value;
    JLI(value, inputMasks, win);
    *value = mask;
    return 1;
}
long inputMask(Window win) {
    long* value;
    JLG(value, inputMasks, win);
    if(value == NULL) {
        return -1;
    }
    return *value;
}

void setWindowAttr(Window window, XWindowAttributes* attrs) {
    XWindowAttributes** value;
    JLI(value, windowAttrs, window);
    *value = attrs;
}

