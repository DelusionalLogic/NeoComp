#include "renderutil.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "common.h"

#include "debug.h"
#include "profiler/zone.h"

DECLARE_ZONE(draw_rect);

Matrix view;

void set_matrix(const struct shader_value* mvp, const Vector3 pos, const Vector2 size) {
    Matrix root = view;
    {
        Matrix op = {{
            size.x , 0      , 0     , 0 ,
            0      , size.y , 0     , 0 ,
            0      , 0      , 1     , 0 ,
            pos.x  , pos.y  , pos.z , 1 ,
        }};
        root = mat4_multiply(&root, &op);
    }

    shader_set_uniform_mat4(mvp, &root);
}

void draw_rect(const struct face* face, const struct shader_value* mvp, const Vector3 pos, const Vector2 size) {
    zone_scope(&ZONE_draw_rect);
    set_matrix(mvp, pos, size);

    face_bind(face);
    glDrawArrays(GL_TRIANGLES, 0, face->vertex_buffer.size / 3);
    debug_mark_draw();
}

void draw_colored_rect(const struct face* face, const Vector3* pos, const Vector2* size, const Vector4* color) {
    struct shader_program* profiler_program = assets_load("profiler.shader");
    if(profiler_program->shader_type_info != &profiler_info) {
        printf("Shader was not a profiler shader\n");
        // @INCOMPLETE: Make sure the config is correct
        return;
    }
    struct Profiler* profiler_type = profiler_program->shader_type;
    shader_set_future_uniform_vec3(profiler_type->color, &color->rgb);
	shader_set_future_uniform_float(profiler_type->opacity, color->w);
    shader_use(profiler_program);

    draw_rect(face, profiler_type->mvp, *pos, *size);
}

void draw_tex(struct face* face, const struct Texture* texture,
        const Vector3* pos, const Vector2* size) {
    // Render back to the backbuffer
    struct shader_program* passthough_program = assets_load("passthough.shader");
    if(passthough_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader\n");
        return;
    }
    struct Passthough* passthough_type = passthough_program->shader_type;
    shader_set_future_uniform_bool(passthough_type->flip, texture->flipped);
    shader_set_future_uniform_float(passthough_type->opacity, (float)1.0);
    shader_set_future_uniform_sampler(passthough_type->tex_scr, 0);

    shader_use(passthough_program);

    texture_bind(texture, GL_TEXTURE0);

    //Final render
    {
#ifdef DEBUG_GLX
        printf_dbgf("relpos %f %f scale %f %f\n", pos->x, pos->y,
                size->x, size->y);
#endif

        draw_rect(face, passthough_type->mvp, *pos, *size);
    }
}

