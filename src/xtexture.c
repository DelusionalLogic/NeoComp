#include "xtexture.h"

bool xtexture_init(struct XTexture* tex, struct X11Context* context) {
    assert(tex != NULL);
    assert(context != NULL);

    tex->context = context;
    tex->pixmap = 0;
    texture_init_nospace(&tex->texture, GL_TEXTURE_2D, NULL);
    //If the context says textures pixmaps are inverted, then we need to tell
    //the texture
    tex->texture.flipped = context->y_inverted;
    return true;
}

void xtexture_delete(struct XTexture* tex) {
    assert(tex != NULL);
    if(tex->bound) {
        xtexture_unbind(tex);
    }
    texture_delete(&tex->texture);
}

bool xtexture_bind(struct XTexture* tex, Pixmap pixmap) {
    assert(tex != NULL);
    assert(!tex->bound);

    tex->pixmap = pixmap;

    Window root;
    int rx,  ry;
    uint32_t width, height;
    uint32_t border;
    uint32_t depth;
    if(!XGetGeometry(tex->context->display, tex->pixmap, &root, &rx,
                &ry, &width, &height, &border, &depth)) {
        printf_errf("Failed querying pixmap info for %#010lx", tex->pixmap);
        // @INCOMPLETE: free
        return false;
    }

    Vector2 size = {{width, height}};

    const int attrib[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
        None,
    };
    tex->glxPixmap = glXCreatePixmap(
            tex->context->display,
            tex->context->selected_config,
            tex->pixmap,
            attrib
            );

    tex->texture.size = size;

    texture_bind(&tex->texture, GL_TEXTURE0);
    glXBindTexImageEXT(tex->context->display, tex->glxPixmap,
            GLX_FRONT_LEFT_EXT, NULL);

    tex->bound = true;
    return true;
}

bool xtexture_unbind(struct XTexture* tex) {
    assert(tex != NULL);
    assert(tex->bound);

    texture_bind(&tex->texture, GL_TEXTURE0);
    glXReleaseTexImageEXT(tex->context->display, tex->pixmap,
            GLX_FRONT_LEFT_EXT);

    glXDestroyPixmap(tex->context->display, tex->pixmap);
    tex->pixmap = 0;

    tex->bound = false;
    return true;
}
