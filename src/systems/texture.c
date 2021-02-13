#include "systems/texture.h"

#include "../texture.h"
#include "logging.h"
#include "renderbuffer.h"
#include "window.h"
#include "assets/shader.h"
#include "assets/assets.h"
#include "shaders/include.h"
#include "profiler/zone.h"
#include "renderutil.h"

DECLARE_ZONE(texture_tick);
DECLARE_ZONE(x_communication);

DECLARE_ZONE(update_textures);
DECLARE_ZONE(update_single_texture);

static struct Framebuffer fbo;

void texturesystem_init() {
    if(!framebuffer_init(&fbo)) {
        printf_errf("Failed initializing the global framebuffer");
    }
}

void texturesystem_delete() {
    framebuffer_delete(&fbo);
}

static void update_window_textures(Swiss* em, struct X11Context* xcontext) {
    zone_scope(&ZONE_update_textures);
    static const enum ComponentType req_types[] = {
        COMPONENT_BINDS_TEXTURE,
        COMPONENT_TEXTURED,
        COMPONENT_CONTENTS_DAMAGED,
        CQ_END
    };
    struct SwissIterator it = {0};
    swiss_getFirst(em, req_types, &it);
    if(it.done)
        return;

    framebuffer_resetTarget(&fbo);
    framebuffer_bind(&fbo);

    glEnable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_ZERO, GL_ZERO, GL_REPLACE);

    struct shader_program* program = assets_load("stencil.shader");
    if(program->shader_type_info != &stencil_info) {
        printf_errf("Shader was not a stencil shader\n");
        return;
    }
    struct Stencil* shader_type = program->shader_type;

    shader_set_future_uniform_sampler(shader_type->tex_scr, 0);

    shader_use(program);

    // @RESEARCH: According to the spec (https://www.khronos.org/registry/OpenGL/extensions/EXT/GLX_EXT_texture_from_pixmap.txt)
    // we should always grab the server before binding glx textures, and keep
    // server until we are done with the textures. Experiments show that it
    // completely kills rendering performance for chrome and electron.
    // - Delusional 19/08-2018
    zone_enter(&ZONE_x_communication);
    XGrabServer(xcontext->display);
    glXWaitX();
    zone_leave(&ZONE_x_communication);

    struct WindowDrawable** drawables = malloc(sizeof(struct WindowDrawable*) * em->size);
    size_t drawable_count = 0;
    {
        for_componentsArr(it2, em, req_types) {
            struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it2.id);
            drawables[drawable_count] = &bindsTexture->drawable;
            drawable_count++;
        }
    }

    for_componentsArr(it2, em, req_types) {
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it2.id);

        XSyncFence fence = XSyncCreateFence(xcontext->display, bindsTexture->drawable.wid, false);
        XSyncTriggerFence(xcontext->display, fence);
        XSyncAwaitFence(xcontext->display, &fence, 1);
        XSyncDestroyFence(xcontext->display, fence);
    }

    zone_enter(&ZONE_x_communication);
    if(!wd_bind(xcontext, drawables, drawable_count)) {
        // If we fail to bind we just assume that the window must have been
        // closed and keep the old texture
        printf_err("Failed binding some drawable");
        zone_leave(&ZONE_x_communication);
    }

    free(drawables);
    zone_leave(&ZONE_x_communication);

    glClearColor(0, 0, 0, 0);

    for_componentsArr(it2, em, req_types) {
        zone_scope(&ZONE_update_single_texture);

        struct ShapedComponent* shaped = swiss_getComponent(em, COMPONENT_SHAPED, it2.id);
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it2.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it2.id);
        framebuffer_resetTarget(&fbo);
        framebuffer_targetTexture(&fbo, &textured->texture);
        framebuffer_targetRenderBuffer_stencil(&fbo, &textured->stencil);
        framebuffer_rebind(&fbo);

        Vector2 offset = textured->texture.size;
        vec2_sub(&offset, &bindsTexture->drawable.texture.size);

        Matrix old_view = view;
        view = mat4_orthogonal(0, textured->texture.size.x, 0, textured->texture.size.y, -1, 1);
        glViewport(0, 0, textured->texture.size.x, textured->texture.size.y);

        // If the texture didn't bind, we just clear the window without rendering on top.
        if(bindsTexture->drawable.bound) {
            texture_bind(&bindsTexture->drawable.texture, GL_TEXTURE0);

            shader_set_uniform_bool(shader_type->flip, bindsTexture->drawable.texture.flipped);

            draw_rect(shaped->face, shader_type->mvp, (Vector3){{0, offset.y, 0}}, bindsTexture->drawable.texture.size);
        }

        view = old_view;

    }

    for_componentsArr(it2, em, req_types) {
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(em, COMPONENT_BINDS_TEXTURE, it2.id);

        // The texture might not have bound successfully
        if(bindsTexture->drawable.bound) {
            wd_unbind(&bindsTexture->drawable);
        }
    }

    glDisable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    zone_enter(&ZONE_x_communication);
    XUngrabServer(xcontext->display);
    glXWaitX();
    zone_leave(&ZONE_x_communication);
}


void texturesystem_tick(Swiss* em, struct X11Context* xcontext) {
    zone_scope(&ZONE_texture_tick);
    // Resize textures when mapping a window with a texture
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_PHYSICAL, COMPONENT_REDIRECTED, COMPONENT_TEXTURED, CQ_END) {
        struct PhysicalComponent* phy = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it.id);

        texture_resize(&textured->texture, &phy->size);
        renderbuffer_resize(&textured->stencil, &phy->size);
    }

    // Create a texture when mapping windows without one
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_REDIRECTED, COMPONENT_PHYSICAL, CQ_NOT, COMPONENT_TEXTURED, CQ_END) {
        struct PhysicalComponent* phy = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        struct TexturedComponent* textured = swiss_addComponent(em, COMPONENT_TEXTURED, it.id);

        if(texture_init(&textured->texture, GL_TEXTURE_2D, &phy->size) != 0)  {
            printf_errf("Failed initializing window contents texture");
        }

        if(renderbuffer_stencil_init(&textured->stencil, &phy->size) != 0)  {
            printf_errf("Failed initializing window contents stencil");
        }
    }

    // We just added a texture, that means we have to refill it
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_TEXTURED, COMPONENT_BINDS_TEXTURE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_CONTENTS_DAMAGED, it.id);
    }

    // Resizing a window requires a new texture
    for_components(it, em,
            COMPONENT_RESIZE, COMPONENT_TEXTURED, CQ_END) {
        struct ResizeComponent* resize = swiss_getComponent(em, COMPONENT_RESIZE, it.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it.id);

        texture_resize(&textured->texture, &resize->newSize);
        renderbuffer_resize(&textured->stencil, &resize->newSize);
    }

    for_components(it, em,
            COMPONENT_RESIZE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_CONTENTS_DAMAGED, it.id);
    }

    update_window_textures(em, xcontext);
}
