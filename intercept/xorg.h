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
Window RootWindowH(Display* dpy, int scr);

GLXFBConfig* glXGetFBConfigsH(Display* dpy, int scr, int* num);
Bool XQueryExtensionH(Display* dpy, char* name, int* opcode, int* event, int* error);

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
void XDamageSubtract(Display* dpy, Damage damage, XserverRegion repair, XserverRegion parts);

void XShapeSelectInputH(Display* dpy, Window win, unsigned long mask);
