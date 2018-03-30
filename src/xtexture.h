#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "common.h"

#include "texture.h"

bool xtexture_init(struct XTexture* tex, struct X11Context* context);
void xtexture_delete(struct XTexture* tex);

bool xtexture_bind(struct XTexture* tex, Pixmap pixmap);
bool xtexture_unbind(struct XTexture* tex);
