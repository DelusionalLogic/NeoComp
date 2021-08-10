#pragma once

#include "swiss.h"
#include "../xorg.h"
#include "atoms.h"

// @CLEANUP: Remove this
struct _session_t;

void xorgsystem_fill_wintype(Swiss* em, struct _session_t* ps);
void xorgsystem_tick(Swiss* em, struct X11Context* xcontext, struct Atoms* atoms, Vector2* canvas_size);
