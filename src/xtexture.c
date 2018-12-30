#include "xtexture.h"

#include "logging.h"
#include "profiler/zone.h"

#include <assert.h>
#include <xcb/composite.h>

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

struct ImportantTexInfo {
    xcb_get_geometry_cookie_t geometry_cookie;
    Vector2 size;
    int depth;
};

bool xtexture_bind(struct XTexture* tex[], struct XTextureInformation* texinfo[], xcb_pixmap_t pixmap[], size_t cnt) {
    assert(tex != NULL);
    assert(pixmap != NULL);
    assert(texinfo != NULL);

    for(size_t i = 0; i < cnt; i++) {
        assert(!tex[i]->bound);
    }

    for(size_t i = 0; i < cnt; i++) {
        tex[i]->pixmap = pixmap[i];
    }

    for(size_t i = 0; i < cnt; i++) {
        tex[i]->texture.flipped = texinfo[i]->flipped;
    }

    xcb_connection_t* xcb = XGetXCBConnection(tex[0]->context->display);

    struct ImportantTexInfo* infos = malloc(sizeof(struct ImportantTexInfo) * cnt);
    for(size_t i = 0; i < cnt; i++) {
        zone_enter(&ZONE_fetch_properties);
        struct ImportantTexInfo* info = &infos[i];
        info->geometry_cookie = xcb_get_geometry(xcb, tex[i]->pixmap);
        zone_leave(&ZONE_fetch_properties);
    }

    for(size_t i = 0; i < cnt; i++) {
        zone_enter(&ZONE_fetch_properties);
        struct ImportantTexInfo* info = &infos[i];
        xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(xcb, info->geometry_cookie, NULL);
        info->depth = reply->depth;
        info->size = (Vector2){{reply->width, reply->height}};
        zone_leave(&ZONE_fetch_properties);
    }

    for(size_t i = 0; i < cnt; i++) {
        tex[i]->depth = infos[i].depth;
    }

    int* formats = malloc(sizeof(int) * cnt);
    // @CLEANUP: Maybe move this somewhere else?
    for(size_t i = 0; i < cnt; i++) {
        formats[i] = GLX_TEXTURE_FORMAT_RGBA_EXT;
        if(!texinfo[i]->hasRGBA) {
            if(texinfo[i]->hasRGB) {
                formats[i] = GLX_TEXTURE_FORMAT_RGB_EXT;
            } else {
                printf_errf("Internal Error: The drawable support neither RGB nor RGBA?");
                free(infos);
                return false;
            }
        } else {
            if(texinfo[i]->rgbAlpha == 0) {
                formats[i] = GLX_TEXTURE_FORMAT_RGB_EXT;
            }

            // @QUESTIONABLE: This is what compton does, and it fixes some
            // strange transparency jankyness with MPV, but I don't really
            // understand why it's required - Delusional 02/04-2018

            // If the depth requested matches the depth we get without
            // alpha data, then we just use RGB
            if(tex[i]->depth == texinfo[i]->rgbDepth - texinfo[i]->rgbAlpha) {
                formats[i] = GLX_TEXTURE_FORMAT_RGB_EXT;
            }
        }
    }

    for(size_t i = 0; i < cnt; i++) {
        zone_enter(&ZONE_create_pixmap);
        const int attrib[] = {
            GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
            GLX_TEXTURE_FORMAT_EXT, formats[i],
            None,
        };
        tex[i]->glxPixmap = glXCreatePixmap(
            tex[i]->context->display,
            *texinfo[i]->config,
            tex[i]->pixmap,
            attrib
        );
        zone_leave(&ZONE_create_pixmap);
    }

    for(size_t i = 0; i < cnt; i++) {
        tex[i]->texture.size = infos[i].size;
    }

    for(size_t i = 0; i < cnt; i++) {
        zone_enter(&ZONE_bind_tex_image);
        texture_bind(&tex[i]->texture, GL_TEXTURE0);
        zone_leave(&ZONE_bind_tex_image);
        zone_enter(&ZONE_bind_tex_image);
        glXBindTexImageEXT(tex[i]->context->display, tex[i]->glxPixmap, GLX_FRONT_LEFT_EXT, NULL);
        zone_leave(&ZONE_bind_tex_image);
    }

    for(size_t i = 0; i < cnt; i++) {
        tex[i]->bound = true;
    }

    free(infos);
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
