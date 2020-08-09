#include "xorg.h"

#include "profiler/zone.h"

DECLARE_ZONE(x_call);

Status XGetWindowAttributesH(Display* dpy, Window window, XWindowAttributes* attrs) {
    zone_scope_extra(&ZONE_x_call, "GetWinAttr");
    return XGetWindowAttributes(dpy, window, attrs);
}

int XNextEventH(Display* dpy, XEvent* ev) {
    zone_scope_extra(&ZONE_x_call, "NextEvent");
    return XNextEvent(dpy, ev);
}

int XEventsQueuedH(Display* dpy, int mode) {
    zone_scope_extra(&ZONE_x_call, "EventsQueued");
    return XEventsQueued(dpy, mode);
}

Window RootWindowH(Display* dpy, int scr) {
    zone_scope_extra(&ZONE_x_call, "RootWindow");
    return RootWindow(dpy, scr);
}

int XSelectInputH(Display* dpy, Window win, long mask) {
    zone_scope_extra(&ZONE_x_call, "SelectInput");
    return XSelectInput(dpy, win, mask);
}

GLXFBConfig* glXGetFBConfigsH(Display* dpy, int scr, int* num) {
    zone_scope_extra(&ZONE_x_call, "FBConfigs");
    return glXGetFBConfigs(dpy, scr, num);
}

Bool XQueryExtensionH(Display* dpy, const char* name, int* opcode, int* event, int* error) {
    zone_scope_extra(&ZONE_x_call, "QueryExtension");
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
    zone_scope_extra(&ZONE_x_call, "DamageCreate");
    return XDamageCreate(dpy, win, level);
}

void XDamageDestroyH(Display* dpy, Damage damage) {
    zone_scope_extra(&ZONE_x_call, "DamageDestroy");
    return XDamageDestroy(dpy, damage);
}

void XDamageSubtractH(Display* dpy, Damage damage, XserverRegion repair, XserverRegion parts) {
    zone_scope_extra(&ZONE_x_call, "DamageSubtract");
    return XDamageSubtract(dpy, damage, repair, parts);
}

void XShapeSelectInputH(Display* dpy, Window win, unsigned long mask) {
    zone_scope_extra(&ZONE_x_call, "ShapeSelectInput");
    return XShapeSelectInput(dpy, win, mask);
}

XserverRegion XFixesCreateRegionH(Display* dpy, XRectangle* rectangles, int nrectangles) {
    zone_scope_extra(&ZONE_x_call, "CreateRegion");
    return XFixesCreateRegion(dpy, rectangles, nrectangles);
}

XserverRegion XFixesCreateRegionFromWindowH(Display* dpy, Window window, int kind) {
    zone_scope_extra(&ZONE_x_call, "CreateRegionFromWin");
    return XFixesCreateRegionFromWindow(dpy, window, kind);
}

void XFixesTranslateRegionH(Display* dpy, XserverRegion region, int dx, int dy) {
    zone_scope_extra(&ZONE_x_call, "TranslateRegion");
    return XFixesTranslateRegion(dpy, region, dx, dy);
}

void XFixesUnionRegionH(Display* dpy, XserverRegion dst, XserverRegion src1, XserverRegion src2) {
    zone_scope_extra(&ZONE_x_call, "UnionRegion");
    return XFixesUnionRegion(dpy, dst, src1, src2);
}

void XFixesIntersectRegionH(Display* dpy, XserverRegion dst, XserverRegion src1, XserverRegion src2) {
    zone_scope_extra(&ZONE_x_call, "IntersectRegion");
    return XFixesIntersectRegion(dpy, dst, src1, src2);
}

void XFixesInvertRegionH(Display* dpy, XserverRegion dst, XRectangle* rect, XserverRegion src) {
    zone_scope_extra(&ZONE_x_call, "InvertRegion");
    return XFixesInvertRegion(dpy, dst, rect, src);
}

void XFixesDestroyRegionH(Display* dpy, XserverRegion region) {
    zone_scope_extra(&ZONE_x_call, "DestroyRegion");
    return XFixesDestroyRegion(dpy, region);
}

void XFixesSetWindowShapeRegionH(Display* dpy, Window win, int shape_kind, int x_off, int y_off, XserverRegion region) {
    zone_scope_extra(&ZONE_x_call, "SetWindowShapeRegion");
    return XFixesSetWindowShapeRegion(dpy, win, shape_kind, x_off, y_off, region);
}

XRectangle* XFixesFetchRegionH(Display* dpy, XserverRegion region, int* count_ret) {
    zone_scope_extra(&ZONE_x_call, "XFixesFetchRegion");
    return XFixesFetchRegion(dpy, region, count_ret);
}


int glXGetFBConfigAttribH(Display* dpy, GLXFBConfig config, int attribute, int* value) {
    return glXGetFBConfigAttrib(dpy, config, attribute, value);
}
