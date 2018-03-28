#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "common.h"

bool win_overlap(win* w1, win* w2);
bool win_covers(win* w);
bool win_is_solid(win* w);

bool wd_init(struct WindowDrawable* drawable, struct X11Context* context, Window wid);
void wd_delete(struct WindowDrawable* drawable);

bool wd_bind(struct WindowDrawable* drawable);
bool wd_unbind(struct WindowDrawable* drawable);
