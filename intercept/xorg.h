#include <GL/glx.h>

#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xdbe.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/sync.h>

Status XGetWindowAttributesH(Display* dpy, Window window, XWindowAttributes* attrs);
int XNextEventH(Display* dpy, XEvent* ev);
int XEventsQueuedH(Display* dpy, int mode);
Window RootWindowH(Display* dpy, int scr);
int XSelectInputH(Display* dpy, Window win, long mask);

GLXFBConfig* glXGetFBConfigsH(Display* dpy, int scr, int* num);
Bool XQueryExtensionH(Display* dpy, const char* name, int* opcode, int* event, int* error);

Status XCompositeQueryVersionH(Display* dpy, int* major, int* minor);
Status XFixesQueryVersionH(Display* dpy, int* major, int* minor);
Status XDamageQueryVersionH(Display* dpy, int* major, int* minor);
Status XRenderQueryVersionH(Display* dpy, int* major, int* minor);
Status XShapeQueryVersionH(Display* dpy, int* major, int* minor);
Status XRRQueryVersionH(Display* dpy, int* major, int* minor);
Status glXQueryVersionH(Display* dpy, int* major, int* minor);
Status XineramaQueryVersionH(Display* dpy, int* major, int* minor);
Status XSyncInitializeH(Display* dpy, int* major, int* minor);

Atom XInternAtomH(Display* dpy, char* name, Bool query);

Damage XDamageCreateH(Display* dpy, Window win, int level);
void XDamageDestroyH(Display* dpy, Damage damage);
void XDamageSubtractH(Display* dpy, Damage damage, XserverRegion repair, XserverRegion parts);

XserverRegion XFixesCreateRegionH(Display* dpy, XRectangle* rectangles, int nrectangles);
XserverRegion XFixesCreateRegionFromWindowH(Display* dpy, Window window, int kind);
void XFixesTranslateRegionH(Display* dpy, XserverRegion region, int dx, int dy);
void XFixesUnionRegionH(Display* dpy, XserverRegion dst, XserverRegion src1, XserverRegion src2);
void XFixesIntersectRegionH(Display* dpy, XserverRegion dst, XserverRegion src1, XserverRegion src2);
void XFixesInvertRegionH(Display* dpy, XserverRegion dst, XRectangle* rect, XserverRegion src);
void XFixesDestroyRegionH(Display* dpy, XserverRegion region);
void XFixesSetWindowShapeRegionH(Display* dpy, Window win, int shape_kind, int x_off, int y_off, XserverRegion region);
XRectangle* XFixesFetchRegionH(Display* dpy, XserverRegion region, int* count_ret);

void XShapeSelectInputH(Display* dpy, Window win, unsigned long mask);

int glXGetFBConfigAttribH(Display* dpy, GLXFBConfig config, int attribute, int* value);
