#pragma once

#include "vector.h"
#include "intercept/xorg.h"

#include <X11/Xlib-xcb.h>

extern Vector eventQ;

void setProperty(Window win, Atom atom, uint32_t value);
long inputMask(Window win);
void setWindowAttr(Window window, XWindowAttributes* attrs);
