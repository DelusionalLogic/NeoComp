#include "renderutil.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "common.h"

Matrix view;

void draw_rect(struct face* face, GLuint mvp, Vector3 pos, Vector2 size) {
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

    glUniformMatrix4fv(mvp, 1, GL_FALSE, root.m);

    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, face->vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, face->uv);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

void draw_tex(session_t* ps, struct face* face, const struct Texture* texture,
        const Vector3* pos, const Vector2* size) {
    // Render back to the backbuffer
    struct shader_program* passthough_program = assets_load("passthough.shader");
    if(passthough_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader\n");
        return;
    }
    struct Passthough* passthough_type = passthough_program->shader_type;
    shader_use(passthough_program);

    shader_set_uniform_bool(passthough_type->flip, texture->flipped);

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

