#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "textureeffects.h"

#include "framebuffer.h"

#include "renderutil.h"
#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"

#include <assert.h>

// Blurs a texture into that same texture.
bool texture_blur(struct TextureBlurData* data, struct Framebuffer* buffer, int stength, bool transparent) {
    assert(texture_initialized(data->tex));

    struct Texture* otherPtr = data->swap;

    assert(texture_initialized(otherPtr));

    struct shader_program* downscale_program = assets_load("downscale.shader");
    if(downscale_program->shader_type_info != &downsample_info) {
        printf("shader was not a downsample shader\n");
        return false;
    }

    struct Downsample* downscale_type = downscale_program->shader_type;

    shader_set_future_uniform_bool(downscale_type->flip, false);
    shader_set_future_uniform_sampler(downscale_type->tex_scr, 0);

    // Use the shader
    shader_use(downscale_program);

    // Disable the options. We will restore later
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);

    Matrix old_view = view;
    view = mat4_orthogonal(0, 1, 0, 1, -1, 1);

    // Set up to draw to the secondary texture
    framebuffer_resetTarget(buffer);
    framebuffer_targetTexture(buffer, otherPtr);
    framebuffer_targetRenderBuffer_stencil(buffer, data->depth);
    framebuffer_bind(buffer);

    //Clear the new texture to transparent
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    Vector2 pixeluv = {{1.0f, 1.0f}};
    vec2_div(&pixeluv, &data->tex->size);
    Vector2 halfpixel = {{1.0f, 1.0f}};
    vec2_div(&halfpixel, &data->tex->size);

    // @HACK: We just assume window is rectangular, which means this will work.
    // In the future we probably shouldn't
    struct face* face = assets_load("window.face");

    // Downscale
    for (int i = 0; i < stength; i++) {
        Vector2 sourceSize = data->tex->size;
        vec2_idiv(&sourceSize, pow(2, i));

        Vector2 targetSize = sourceSize;
        vec2_idiv(&targetSize, 2);

        // Set up to draw to the secondary texture
        framebuffer_resetTarget(buffer);
        framebuffer_targetTexture(buffer, otherPtr);
        framebuffer_targetRenderBuffer_stencil(buffer, data->depth);
        framebuffer_bind(buffer);

        glViewport(0, 0, data->tex->size.x, data->tex->size.y);

        // @CLEANUP Do we place this here or after the swap?
        texture_bind(data->tex, GL_TEXTURE0);

        // Set the shader parameters
        shader_set_uniform_vec2(downscale_type->pixeluv, &pixeluv);
        // Set the source texture
        shader_set_uniform_sampler(downscale_type->tex_scr, 0);

        // Do the render
        {
            const Vector2 roundSource = {{
                ceil(sourceSize.x), ceil(sourceSize.y),
            }};
            Vector2 uv_scale = pixeluv;
            vec2_mul(&uv_scale, &roundSource);

            const Vector2 roundTarget = {{
                ceil(targetSize.x), ceil(targetSize.y),
            }};
            Vector2 scale = pixeluv;
            vec2_mul(&scale, &roundTarget);

            Vector2 uv_max = pixeluv;
            vec2_mul(&uv_max, &sourceSize);
            vec2_sub(&uv_max, &halfpixel);

            if(transparent) {
                glClearColor(0.0, 0.0, 0.0, 0.0);
                glClear(GL_COLOR_BUFFER_BIT);
            }

            shader_set_uniform_vec2(downscale_type->extent, &uv_max);
            shader_set_uniform_vec2(downscale_type->uvscale, &uv_scale);

            draw_rect(face, downscale_type->mvp, VEC3_ZERO, scale);
        }

        // Swap main and secondary
        {
            struct Texture* tmp = otherPtr;
            otherPtr = data->tex;
            data->tex = tmp;
        }
    }

    // Switch to the upsample shader

    struct shader_program* upsample_program = assets_load("upsample.shader");
    if(upsample_program->shader_type_info != &upsample_info) {
        printf("Shader was not a upsample shader");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        view = old_view;
        return false;
    }

    struct Upsample* upsample_type = upsample_program->shader_type;


    // Use the shader
    shader_set_future_uniform_bool(upsample_type->flip, false);
    shader_set_future_uniform_sampler(upsample_type->tex_scr, 0);

    shader_use(upsample_program);

    // Upscale
    for (int i = 0; i < stength; i++) {
        Vector2 sourceSize = data->tex->size;
        vec2_idiv(&sourceSize, pow(2, stength - i));

        Vector2 targetSize = sourceSize;
        vec2_imul(&targetSize, 2);

        // Set up to draw to the secondary texture
        framebuffer_resetTarget(buffer);
        framebuffer_targetTexture(buffer, otherPtr);
        framebuffer_bind(buffer);

        glViewport(0, 0, data->tex->size.x, data->tex->size.y);

        // @CLEANUP Do we place this here or after the swap?
        texture_bind(data->tex, GL_TEXTURE0);

        // Set the shader parameters
        shader_set_uniform_vec2(upsample_type->pixeluv, &pixeluv);
        // Set the source texture
        shader_set_uniform_sampler(upsample_type->tex_scr, 0);

        // Do the render
        {
            const Vector2 roundSource = {{
                ceil(sourceSize.x), ceil(sourceSize.y),
            }};
            Vector2 uv_scale = pixeluv;
            vec2_mul(&uv_scale, &roundSource);

            const Vector2 roundTarget = {{
                ceil(targetSize.x), ceil(targetSize.y),
            }};
            Vector2 scale = pixeluv;
            vec2_mul(&scale, &roundTarget);

            Vector2 uv_max = pixeluv;
            vec2_mul(&uv_max, &sourceSize);
            vec2_sub(&uv_max, &halfpixel);

            glClearColor(0.0, 0.0, 0.0, 0.0);
            glClear(GL_COLOR_BUFFER_BIT);

            shader_set_uniform_vec2(upsample_type->extent, &uv_max);
            shader_set_uniform_vec2(upsample_type->uvscale, &uv_scale);

            draw_rect(face, upsample_type->mvp, VEC3_ZERO, scale);
        }

        // Swap main and secondary
        {
            struct Texture* tmp = otherPtr;
            otherPtr = data->tex;
            data->tex = tmp;
        }
    }

    glDepthMask(GL_TRUE);
    glStencilMask(255);

    view = old_view;
    return true;
}

struct OtherBlurData {
    Vector2 pixeluv;
    Vector2 halfpixel;
    struct Texture* ptr;
    struct Texture* other;
};

// Blurs a texture into that same texture.
bool textures_blur(Vector* datas, struct Framebuffer* buffer, int stength, bool transparent) {
    Matrix old_view = view;
    view = mat4_orthogonal(0, 1, 0, 1, -1, 1);

    // @HACK: We just assume window is rectangular, which means this will work.
    // In the future we probably shouldn't
    struct face* face = assets_load("window.face");

    Vector otherBlurVec;
    vector_init(&otherBlurVec, sizeof(struct OtherBlurData), datas->size);
    vector_reserve(&otherBlurVec, datas->size);

    size_t index;
    struct TextureBlurData* data = vector_getFirst(datas, &index);
    while(data != NULL) {
        assert(texture_initialized(data->tex));

        struct OtherBlurData* otherData = vector_get(&otherBlurVec, index);

        otherData->other = data->swap;

        assert(texture_initialized(otherData->other));

        otherData->pixeluv.x = 1.0f;
        otherData->pixeluv.y = 1.0f;
        vec2_div(&otherData->pixeluv, &data->tex->size);

        otherData->halfpixel.x = 1.0f;
        otherData->halfpixel.y = 1.0f;
        vec2_div(&otherData->halfpixel, &data->tex->size);

        data = vector_getNext(datas, &index);
    }

    framebuffer_resetTarget(buffer);
    framebuffer_bind(buffer);

    // Disable the options. We will restore later
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);

    struct shader_program* downscale_program = assets_load("downscale.shader");
    if(downscale_program->shader_type_info != &downsample_info) {
        printf("shader was not a downsample shader\n");
        return false;
    }

    struct Downsample* downscale_type = downscale_program->shader_type;

    shader_set_future_uniform_bool(downscale_type->flip, false);
    shader_set_future_uniform_sampler(downscale_type->tex_scr, 0);

    // Use the shader
    shader_use(downscale_program);

    data = vector_getFirst(datas, &index);
    while(data != NULL) {
        struct OtherBlurData* otherData = vector_get(&otherBlurVec, index);

        // Downscale
        for (int i = 0; i < stength; i++) {
            Vector2 sourceSize = data->tex->size;
            vec2_idiv(&sourceSize, pow(2, i));

            Vector2 targetSize = sourceSize;
            vec2_idiv(&targetSize, 2);

            // Set up to draw to the secondary texture
            framebuffer_resetTarget(buffer);
            framebuffer_targetTexture(buffer, otherData->other);
            framebuffer_targetRenderBuffer_stencil(buffer, data->depth);
            framebuffer_rebind(buffer);

            glViewport(0, 0, data->tex->size.x, data->tex->size.y);

            // @CLEANUP Do we place this here or after the swap?
            texture_bind(data->tex, GL_TEXTURE0);

            // Set the shader parameters
            shader_set_uniform_vec2(downscale_type->pixeluv, &otherData->pixeluv);

            // Do the render
            {
                const Vector2 roundSource = {{
                    ceil(sourceSize.x), ceil(sourceSize.y),
                }};
                Vector2 uv_scale = otherData->pixeluv;
                vec2_mul(&uv_scale, &roundSource);

                const Vector2 roundTarget = {{
                    ceil(targetSize.x), ceil(targetSize.y),
                }};
                Vector2 scale = otherData->pixeluv;
                vec2_mul(&scale, &roundTarget);

                Vector2 uv_max = otherData->pixeluv;
                vec2_mul(&uv_max, &sourceSize);
                vec2_sub(&uv_max, &otherData->halfpixel);

                shader_set_uniform_vec2(downscale_type->extent, &uv_max);
                shader_set_uniform_vec2(downscale_type->uvscale, &uv_scale);

                draw_rect(face, downscale_type->mvp, VEC3_ZERO, scale);
            }

            // Swap main and secondary
            {
                struct Texture* tmp = otherData->other;
                otherData->other = data->tex;
                data->tex = tmp;
            }
        }

        data = vector_getNext(datas, &index);
    }

    // Switch to the upsample shader
    struct shader_program* upsample_program = assets_load("upsample.shader");
    if(upsample_program->shader_type_info != &upsample_info) {
        printf("Shader was not a upsample shader");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        return false;
    }

    struct Upsample* upsample_type = upsample_program->shader_type;

    // Use the shader
    shader_set_future_uniform_bool(upsample_type->flip, false);
    shader_set_future_uniform_sampler(upsample_type->tex_scr, 0);

    shader_use(upsample_program);

    data = vector_getFirst(datas, &index);
    while(data != NULL) {
        struct OtherBlurData* otherData = vector_get(&otherBlurVec, index);

        // Upscale
        for (int i = 0; i < stength; i++) {
            Vector2 sourceSize = data->tex->size;
            vec2_idiv(&sourceSize, pow(2, stength - i));

            Vector2 targetSize = sourceSize;
            vec2_imul(&targetSize, 2);

            // Set up to draw to the secondary texture
            framebuffer_resetTarget(buffer);
            framebuffer_targetTexture(buffer, otherData->other);
            framebuffer_rebind(buffer);

            glViewport(0, 0, data->tex->size.x, data->tex->size.y);

            // @CLEANUP Do we place this here or after the swap?
            texture_bind(data->tex, GL_TEXTURE0);

            // Set the shader parameters
            shader_set_uniform_vec2(upsample_type->pixeluv, &otherData->pixeluv);
            // Set the source texture
            shader_set_uniform_sampler(upsample_type->tex_scr, 0);

            // Do the render
            {
                const Vector2 roundSource = {{
                    ceil(sourceSize.x), ceil(sourceSize.y),
                }};
                Vector2 uv_scale = otherData->pixeluv;
                vec2_mul(&uv_scale, &roundSource);

                const Vector2 roundTarget = {{
                    ceil(targetSize.x), ceil(targetSize.y),
                }};
                Vector2 scale = otherData->pixeluv;
                vec2_mul(&scale, &roundTarget);

                Vector2 uv_max = otherData->pixeluv;
                vec2_mul(&uv_max, &sourceSize);
                vec2_sub(&uv_max, &otherData->halfpixel);

                shader_set_uniform_vec2(upsample_type->extent, &uv_max);
                shader_set_uniform_vec2(upsample_type->uvscale, &uv_scale);

                draw_rect(face, upsample_type->mvp, VEC3_ZERO, scale);
            }

            // Swap main and secondary
            {
                struct Texture* tmp = otherData->other;
                otherData->other = data->tex;
                data->tex = tmp;
            }
        }

        data = vector_getNext(datas, &index);
    }

    view = old_view;
    return true;
}
