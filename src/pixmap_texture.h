#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "texture.h"

struct WindowDrawable {
    Window* window;
    Pixmap pixmap;
    GLXDrawable glxPixmap;

    bool bound;
    struct Texture* texture;
}

bool wd_init(struct WindowDrawable* drawable, Window* window);
void wd_delete(struct WindowDrawable* drawable);
