#include <GL/glx.h>

Status XGetWindowAttributesH(Display* dpy, Window window, XWindowAttributes* attrs);
int XNextEventH(Display* dpy, XEvent* ev);
Window RootWindowH(Display* dpy, int scr);
