#pragma once

#include "swiss.h"

#include "xorg.h"
#include "framebuffer.h"

void texturesystem_init();
void texturesystem_delete();
void texturesystem_tick(Swiss* em, struct X11Context* xcontext);
