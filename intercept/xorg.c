#include "xorg.h"

Status XGetWindowAttributesH(Display* dpy, Window window, XWindowAttributes* attrs) {
    return XGetWindowAttributes(dpy, window, attrs);
}

int XNextEventH(Display* dpy, XEvent* ev) {
    return XNextEvent(dpy, ev);
}

Window RootWindowH(Display* dpy, int scr) {
    return RootWindow(dpy, scr);
}

GLXFBConfig* glXGetFBConfigsH(Display* dpy, int scr, int* num) {
    return glXGetFBConfigs(dpy, scr, num);
}

Bool XQueryExtensionH(Display* dpy, char* name, int* opcode, int* event, int* error) {
    return XQueryExtension(dpy, name, opcode, event, error);
}
Status XCompositeQueryVersionH(Display* dpy, int* major, int* minor) {
    return XCompositeQueryVersion(dpy, major, minor);
}
Status XFixesQueryVersionH(Display* dpy, int* major, int* minor) {
    return XFixesQueryVersion(dpy, major, minor);
}
Status XDamageQueryVersionH(Display* dpy, int* major, int* minor) {
    return XDamageQueryVersion(dpy, major, minor);
}
Status XRenderQueryVersionH(Display* dpy, int* major, int* minor) {
    return XRenderQueryVersion(dpy, major, minor);
}
Status XShapeQueryVersionH(Display* dpy, int* major, int* minor) {
    return XShapeQueryVersion(dpy, major, minor);
}
Status XRRQueryVersionH(Display* dpy, int* major, int* minor) {
    return XRRQueryVersion(dpy, major, minor);
}
Status glXQueryVersionH(Display* dpy, int* major, int* minor) {
    return glXQueryVersion(dpy, major, minor);
}
Status XineramaQueryVersionH(Display* dpy, int* major, int* minor) {
    return XineramaQueryVersion(dpy, major, minor);
}
Status XSyncInitializeH(Display* dpy, int* major, int* minor) {
    return XSyncInitialize(dpy, major, minor);
}

Atom XInternAtomH(Display* dpy, char* name, Bool query) {
    return XInternAtom(dpy, name, query);
}

Damage XDamageCreateH(Display* dpy, Window win, int level) {
    return XDamageCreate(dpy, win, level);
}

void XDamageSubtractH(Display* dpy, Damage damage, XserverRegion repair, XserverRegion parts) {
    return XDamageSubtract(dpy, damage, repair, parts);
}

void XShapeSelectInputH(Display* dpy, Window win, unsigned long mask) {
    return XShapeSelectInput(dpy, win, mask);
}
