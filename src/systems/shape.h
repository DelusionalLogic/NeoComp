#pragma once

#include "swiss.h"
#include "xorg.h"

void shapesystem_updateShapes(Swiss* em, struct X11Context* xcontext);
void shapesystem_finish(Swiss* em);
void shapesystem_delete(Swiss* em);
