#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "common.h"

bool win_overlap(win* w1, win* w2);
bool win_covers(win* w);
bool win_is_solid(win* w);

struct WindowDrawable {
    Window wid;

    bool bound;
    Pixmap pixmap;
    GLXDrawable glxPixmap;
    struct Texture* texture;
};

bool wd_bind(struct WindowDrawable* drawable, Display* display, Window wid);
void wd_delete(struct WindowDrawable* drawable);
