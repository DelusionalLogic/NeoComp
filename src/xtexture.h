#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "texture.h"
#include "xorg.h"

struct XTexture {
    struct X11Context* context;

    bool bound;
    int depth;
    Pixmap pixmap;
    GLXDrawable glxPixmap;
    struct Texture texture;
};

bool xtexture_init(struct XTexture* tex, struct X11Context* context);
void xtexture_delete(struct XTexture* tex);

bool xtexture_bind(struct XTexture* tex, GLXFBConfig* fbconfig, Pixmap pixmap);
bool xtexture_unbind(struct XTexture* tex);
