#pragma once

#include "swiss.h"

#include "xorg.h"
#include "framebuffer.h"

void texturesystem_tick(Swiss* em, struct X11Context* xcontext, struct Framebuffer* fbo);
