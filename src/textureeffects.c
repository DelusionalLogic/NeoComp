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
bool texture_blur(struct TextureBlurData* data, struct Texture* texture, int stength, bool transparent) {
    assert(texture_initialized(texture));

    struct Texture* otherPtr = data->swap;

    framebuffer_resetTarget(data->buffer);

    assert(texture_initialized(otherPtr));

    // Disable the options. We will restore later
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

	framebuffer_targetTexture(data->buffer, otherPtr);
	framebuffer_bind(data->buffer);
	//Clear the new texture to transparent
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

    struct shader_program* downscale_program = assets_load("downscale.shader");
    if(downscale_program->shader_type_info != &downsample_info) {
        printf("shader was not a downsample shader\n");
        return false;
    }

    struct Downsample* downscale_type = downscale_program->shader_type;

    // Use the shader
    shader_use(downscale_program);

    shader_set_uniform_bool(downscale_type->flip, false);

    Vector2 pixeluv = {{1.0f, 1.0f}};
    vec2_div(&pixeluv, &texture->size);
    Vector2 halfpixel = {{1.0f, 1.0f}};
    vec2_div(&halfpixel, &texture->size);
    Vector2 zero_vec = {{0.0, 0.0}};

    struct face* face = assets_load("window.face");

    // Downscale
    for (int i = 0; i < stength; i++) {
        Vector2 sourceSize = texture->size;
        vec2_idiv(&sourceSize, pow(2, i));

        Vector2 targetSize = sourceSize;
        vec2_idiv(&targetSize, 2);

        // Set up to draw to the secondary texture
        framebuffer_resetTarget(data->buffer);
        framebuffer_targetTexture(data->buffer, otherPtr);
        framebuffer_bind(data->buffer);

        glViewport(0, 0, texture->size.x, texture->size.y);

        // @CLEANUP Do we place this here or after the swap?
        texture_bind(texture, GL_TEXTURE0);

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

            draw_rect(face, downscale_type->mvp, zero_vec, scale);
        }

        // Swap main and secondary
        {
            struct Texture* tmp = otherPtr;
            otherPtr = texture;
            texture = tmp;
        }
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
    shader_use(upsample_program);
    shader_set_uniform_bool(upsample_type->flip, false);

    // Upscale
    for (int i = 0; i < stength; i++) {
        Vector2 sourceSize = texture->size;
        vec2_idiv(&sourceSize, pow(2, stength - i));

        Vector2 targetSize = sourceSize;
        vec2_imul(&targetSize, 2);

        // Set up to draw to the secondary texture
        framebuffer_resetTarget(data->buffer);
        framebuffer_targetTexture(data->buffer, otherPtr);
        framebuffer_bind(data->buffer);

        glViewport(0, 0, texture->size.x, texture->size.y);

        // @CLEANUP Do we place this here or after the swap?
        texture_bind(texture, GL_TEXTURE0);

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

            draw_rect(face, upsample_type->mvp, zero_vec, scale);
        }

        // Swap main and secondary
        {
            struct Texture* tmp = otherPtr;
            otherPtr = texture;
            texture = tmp;
        }
    }

    return true;
}
