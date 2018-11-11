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

struct XTextureInformation {
    GLXFBConfig* config;

    bool flipped;
    int depth;

    bool hasRGB;
    bool hasRGBA;

    int rgbDepth;
    int rgbAlpha;
};

bool xtexinfo_init(struct XTextureInformation* texinfo, struct X11Context* context, GLXFBConfig* fbconfig);
void xtexinfo_delete(struct XTextureInformation* texinfo);

bool xtexture_init(struct XTexture* tex, struct X11Context* context);
void xtexture_delete(struct XTexture* tex);

bool xtexture_bind(struct XTexture* tex, struct XTextureInformation* texinfo, Pixmap pixmap);
bool xtexture_unbind(struct XTexture* tex);
