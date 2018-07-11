#include "xtexture.h"

bool xtexture_init(struct XTexture* tex, struct X11Context* context) {
    assert(tex != NULL);
    assert(context != NULL);

    tex->context = context;
    tex->pixmap = 0;
    texture_init_nospace(&tex->texture, GL_TEXTURE_2D, NULL);
    return true;
}

void xtexture_delete(struct XTexture* tex) {
    assert(tex != NULL);
    if(tex->bound) {
        xtexture_unbind(tex);
    }
    texture_delete(&tex->texture);
}

bool xtexture_bind(struct XTexture* tex, GLXFBConfig* fbconfig, Pixmap pixmap) {
    assert(tex != NULL);
    assert(!tex->bound);
    assert(fbconfig != NULL);

    //If the context says textures pixmaps are inverted, then we need to tell
    //the texture
    int value;
    glXGetFBConfigAttrib(tex->context->display, *fbconfig, GLX_Y_INVERTED_EXT, &value);
    tex->texture.flipped = value;

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
    tex->depth = depth;

    Vector2 size = {{width, height}};

    int configDepth;
    int configAlpha;
    if (Success != glXGetFBConfigAttrib(tex->context->display, *fbconfig, GLX_BUFFER_SIZE, &configDepth)
            || Success != glXGetFBConfigAttrib(tex->context->display, *fbconfig, GLX_ALPHA_SIZE, &configAlpha)) {
        printf_errf("Failed getting depth and alpha depth for %#010lx", tex->pixmap);
        configDepth = depth;
        configAlpha = 0;
    }

    int texFmt;
    if (Success == glXGetFBConfigAttrib(tex->context->display, *fbconfig, GLX_BIND_TO_TEXTURE_RGBA_EXT, &value)) {
        if(value == true) { // We can bind to RGBA
            texFmt = GLX_TEXTURE_FORMAT_RGBA_EXT;
        }
    }

    // @CLEANUP: This should probably be extracted out of here
    if (Success == glXGetFBConfigAttrib(tex->context->display, *fbconfig, GLX_BIND_TO_TEXTURE_RGB_EXT, &value)) {
        if(value == true) { // We can bind to RGB
            // If we can only bind to RGB, then just do that
            if(texFmt == 0) {
                texFmt = GLX_TEXTURE_FORMAT_RGB_EXT;
            } else {
                // If theres no alpha data, we might as well bind to RGB only
                if(configAlpha == 0) {
                    texFmt = GLX_TEXTURE_FORMAT_RGB_EXT;
                }
                // @QUESTIONABLE: This is what compton does, and it fixes some
                // strange transparency jankyness with MPV, but I don't really
                // understand why it's required - Delusional 02/04-2018

                // If the depth requested matches the depth we get without
                // alpha data, then we just use RGB
                if(depth == configDepth - configAlpha) {
                    texFmt = GLX_TEXTURE_FORMAT_RGB_EXT;
                }
            }
        }
    }

    const int attrib[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, texFmt,
        None,
    };
    tex->glxPixmap = glXCreatePixmap(
            tex->context->display,
            *fbconfig,
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

    glXDestroyPixmap(tex->context->display, tex->glxPixmap);
    XFreePixmap(tex->context->display, tex->pixmap);
    tex->pixmap = 0;

    tex->bound = false;
    return true;
}
