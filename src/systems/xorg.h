#pragma once

#include "swiss.h"
#include "../xorg.h"
#include "atoms.h"

void xorgsystem_tick(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms, Vector2* canvas_size);
