#include <GL/glx.h>

Status XGetWindowAttributesH(Display* dpy, Window window, XWindowAttributes* attrs) {
    return XGetWindowAttributes(dpy, window, attrs);
}

int XNextEventH(Display* dpy, XEvent* ev) {
    return XNextEvent(dpy, ev);
}

Window RootWindowH(Display* dpy, int scr) {
    return RootWindow(dpy, scr);
}
