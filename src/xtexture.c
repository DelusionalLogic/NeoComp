#include "xtexture.h"

#include "logging.h"
#include "profiler/zone.h"

#include <assert.h>

DECLARE_ZONE(create_pixmap);
DECLARE_ZONE(bind_tex_image);
DECLARE_ZONE(fetch_properties);

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

bool xtexinfo_init(struct XTextureInformation* texinfo, struct X11Context* context, GLXFBConfig* fbconfig) {
    zone_enter(&ZONE_fetch_properties);

    texinfo->config = fbconfig;

    int value;
    glXGetFBConfigAttrib(context->display, *fbconfig, GLX_Y_INVERTED_EXT, &value);
    texinfo->flipped = value;

    if (Success != glXGetFBConfigAttrib(context->display, *fbconfig, GLX_BUFFER_SIZE, &texinfo->rgbDepth)
            || Success != glXGetFBConfigAttrib(context->display, *fbconfig, GLX_ALPHA_SIZE, &texinfo->rgbAlpha)) {
        printf_errf("Failed getting depth and alpha depth");
        return false;
    }

    if (Success != glXGetFBConfigAttrib(context->display, *fbconfig, GLX_BIND_TO_TEXTURE_RGBA_EXT, &value)) {
        printf_errf("Failed getting RGBA availability");
        return false;
    }
    texinfo->hasRGBA = value != 0 ? true : false;

    if (Success != glXGetFBConfigAttrib(context->display, *fbconfig, GLX_BIND_TO_TEXTURE_RGB_EXT, &value)) {
        printf_errf("Failed getting RGB availability");
        return false;
    }
    texinfo->hasRGB = value != 0 ? true : false;

    zone_leave(&ZONE_fetch_properties);
    return true;
}

void xtexinfo_delete(struct XTextureInformation* texinfo) {
}

bool xtexture_bind(struct XTexture* tex, struct XTextureInformation* texinfo, Pixmap pixmap) {
    assert(tex != NULL);
    assert(!tex->bound);
    assert(texinfo != NULL);

    tex->pixmap = pixmap;
    tex->texture.flipped = texinfo->flipped;

    Vector2 size;
    zone_enter(&ZONE_fetch_properties);
    {
        Window root;
        int rx,  ry;
        uint32_t border;
        uint32_t depth;
        uint32_t width;
        uint32_t height;
        if(!XGetGeometry(tex->context->display, tex->pixmap, &root, &rx,
                    &ry, &width, &height, &border, &depth)) {
            printf_errf("Failed querying pixmap info for %#010lx", tex->pixmap);
            // @INCOMPLETE: free
            return false;
        }

        tex->depth = depth;
        size = (Vector2){{width, height}};
    }
    zone_leave(&ZONE_fetch_properties);

    // @CLEANUP: Maybe move this somewhere else?
    int texFmt = GLX_TEXTURE_FORMAT_RGBA_EXT;
    if(!texinfo->hasRGBA) {
        if(texinfo->hasRGB) {
            texFmt = GLX_TEXTURE_FORMAT_RGB_EXT;
        } else {
            printf_errf("Internal Error: The drawable support neither RGB nor RGBA?");
            return false;
        }
    } else {
        if(texinfo->rgbAlpha == 0) {
            texFmt = GLX_TEXTURE_FORMAT_RGB_EXT;
        }

        // @QUESTIONABLE: This is what compton does, and it fixes some
        // strange transparency jankyness with MPV, but I don't really
        // understand why it's required - Delusional 02/04-2018

        // If the depth requested matches the depth we get without
        // alpha data, then we just use RGB
        if(tex->depth == texinfo->rgbDepth - texinfo->rgbAlpha) {
            texFmt = GLX_TEXTURE_FORMAT_RGB_EXT;
        }
    }

    zone_enter(&ZONE_create_pixmap);
    const int attrib[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, texFmt,
        None,
    };
    tex->glxPixmap = glXCreatePixmap(
        tex->context->display,
        *texinfo->config,
        tex->pixmap,
        attrib
    );
    zone_leave(&ZONE_create_pixmap);

    tex->texture.size = size;

    zone_enter(&ZONE_bind_tex_image);
    texture_bind(&tex->texture, GL_TEXTURE0);
    zone_leave(&ZONE_bind_tex_image);
    zone_enter(&ZONE_bind_tex_image);
    glXBindTexImageEXT(tex->context->display, tex->glxPixmap, GLX_FRONT_LEFT_EXT, NULL);
    zone_leave(&ZONE_bind_tex_image);

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
