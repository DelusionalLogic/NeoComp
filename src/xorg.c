#include "xorg.h"

#include "assert.h"
#include "logging.h"

bool xorgContext_init(struct X11Context* context, Display* display, int screen) {
    assert(context != NULL);
    assert(display != NULL);

    context->display = display;
    context->screen = screen;

    context->configs = glXGetFBConfigs(display, screen, &context->numConfigs);
    if(context->configs == NULL) {
        printf_errf("Failed retrieving the fboconfigs");
        return false;
    }

    return true;
}

const char* X11Protocols_Names[] = {
    COMPOSITE_NAME,
    XFIXES_NAME,
    DAMAGE_NAME,
    RENDER_NAME,
    SHAPENAME,
    RANDR_NAME,
    GLX_EXTENSION_NAME,
    "XINERAMA",
    SYNC_NAME,
};

const char* VERSION_NAMES[] = {
    "NO",
    "NO (OPTIONAL)",
    "YES",
    "0.2+",
};

int xorgContext_capabilities(struct X11Capabilities* caps, struct X11Context* context) {
    // Fetch if the extension is available
    for(size_t i = 0; i < PROTO_COUNT; i++) {
        if(!XQueryExtension(context->display, X11Protocols_Names[i],
                    &caps->opcode[i], &caps->event[i], &caps->error[i])) {
            caps->version[i] = XVERSION_NO;
            continue;
        }

        caps->version[i] = XVERSION_YES;
    }

    // The X docs say you should always fetch the version, even if you don't
    // care, because it's part of initialization
    if(caps->version[PROTO_COMPOSITE] == XVERSION_YES) {
        int major;
        int minor;
        XCompositeQueryVersion(context->display, &major, &minor);

        if(major > 0 || (major == 0 && minor > 2)) {
            caps->version[PROTO_COMPOSITE] = COMPOSITE_0_2;
        }
    }

    if(caps->version[PROTO_FIXES] == XVERSION_YES) {
        int major;
        int minor;
        XFixesQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_DAMAGE] == XVERSION_YES) {
        int major;
        int minor;
        XDamageQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_RENDER] == XVERSION_YES) {
        int major;
        int minor;
        XRenderQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_SHAPE] == XVERSION_YES) {
        int major;
        int minor;
        XShapeQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_RANDR] == XVERSION_YES) {
        int major;
        int minor;
        XRRQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_GLX] == XVERSION_YES) {
        int major;
        int minor;
        glXQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_XINERAMA] == XVERSION_YES) {
        int major;
        int minor;
        XineramaQueryVersion(context->display, &major, &minor);
    }

    if(caps->version[PROTO_SYNC] == XVERSION_YES) {
        int major;
        int minor;
        XSyncInitialize(context->display, &major, &minor);
    }

    return 0;
}

int xorgContext_ensure_capabilities(const struct X11Capabilities* caps) {
    bool missing = false;

	printf("Xorg Features: \n");
    for(size_t i = 0; i < PROTO_COUNT; i++) {
        printf("    %s: %s\n", X11Protocols_Names[i], VERSION_NAMES[caps->version[i]]);
        if(caps->version[i] == XVERSION_NO)
            missing = true;
    }

    return missing ? 1 : 0;
}

int xorgContext_convertEvent(const struct X11Capabilities* caps, enum X11Protocol proto, int ev) {
    return ev - caps->event[proto];
}

int xorgContext_convertError(const struct X11Capabilities* caps, enum X11Protocol proto, int ev) {
    return ev - caps->error[proto];
}

enum XExtensionVersion xorgContext_version(const struct X11Capabilities* caps, enum X11Protocol proto) {
    return caps->version[proto];
}

enum X11Protocol xorgContext_convertOpcode(const struct X11Capabilities* caps, int opcode) {
    for(size_t i = 0; i < PROTO_COUNT; i++) {
        if(caps->opcode[i] == opcode)
            return i;
    }
    return PROTO_COUNT;
}

GLXFBConfig* xorgContext_selectConfig(struct X11Context* context, VisualID visualid) {
    assert(visualid != 0);

	GLXFBConfig* selected = NULL;

    int value;
    for(int i = 0; i < context->numConfigs; i++) {
        GLXFBConfig fbconfig = context->configs[i];
        XVisualInfo* visinfo = glXGetVisualFromFBConfig(context->display, fbconfig);
        if (!visinfo || visinfo->visualid != visualid) {
            XFree(visinfo);
            continue;
        }
        XFree(visinfo);

        // We don't want to use anything multisampled
        glXGetFBConfigAttrib(context->display, fbconfig, GLX_SAMPLES, &value);
        if (value >= 2) {
            continue;
        }

        // We need to support pixmaps
        glXGetFBConfigAttrib(context->display, fbconfig, GLX_DRAWABLE_TYPE, &value);
        if (!(value & GLX_PIXMAP_BIT)) {
            continue;
        }

        // We need to be able to bind pixmaps to textures
        glXGetFBConfigAttrib(context->display, fbconfig,
                GLX_BIND_TO_TEXTURE_TARGETS_EXT,
                &value);
        if (!(value & GLX_TEXTURE_2D_BIT_EXT)) {
            continue;
        }

        // We want RGBA textures
        glXGetFBConfigAttrib(context->display, fbconfig,
                GLX_BIND_TO_TEXTURE_RGBA_EXT, &value);
        if (value == false) {
            continue;
        }

        selected = &context->configs[i];
        break;
    }
    // If we ran out of fbconfigs before we found something we like.
    if(selected == NULL)
        return NULL;

    return selected;
}

void xorgContext_delete(struct X11Context* context) {
    assert(context->display != NULL);
}
