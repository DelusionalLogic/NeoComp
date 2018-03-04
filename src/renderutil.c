#include "renderutil.h"

#include "assets/assets.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "common.h"

static Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}


void draw_rect(struct face* face, GLuint mvp, Vector2 pos, Vector2 size) {
    Matrix root = IDENTITY_MATRIX;
    {
        Matrix op = {{
        2  , 0  , 0 , 0 ,
        0  , 2  , 0 , 0 ,
        0  , 0  , 1 , 0 ,
        -1 , -1 , 0 , 1 ,
        }};
        root = mat4_multiply(&root, &op);
    }
    {
        Matrix op = {{
        size.x , 0      , 0 , 0 ,
        0      , size.y , 0 , 0 ,
        0      , 0      , 1 , 0 ,
        pos.x  , pos.y  , 0 , 1 ,
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
        const Vector2* root_size, const Vector2* pos, const Vector2* size) {
    // Render back to the backbuffer
    struct shader_program* passthough_program = assets_load("passthough.shader");
    if(passthough_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }

    Vector2 glRectPos = X11_rectpos_to_gl(ps, pos, size);

    struct Passthough* passthough_type = passthough_program->shader_type;
    shader_use(passthough_program);

    shader_set_uniform_bool(passthough_type->flip, false);

    texture_bind(texture, GL_TEXTURE0);

    // Bind the default framebuffer and draw back to the backbuffer to actually
    // render the frame to screen
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
        glDrawBuffers(1, DRAWBUFS);
    }

    //Final render
    {
        Vector2 pixeluv = {{1.0f, 1.0f}};
        vec2_div(&pixeluv, root_size);

        Vector2 scale = pixeluv;
        vec2_mul(&scale, size);

        Vector2 relpos = pixeluv;
        vec2_mul(&relpos, &glRectPos);

#ifdef DEBUG_GLX
        printf_dbgf("glpos: %f %f, relpos %f %f scale %f %f\n",
                glRectPos.x, glRectPos.y, relpos.x, relpos.y, scale.x,
                scale.y);
#endif

        draw_rect(face, passthough_type->mvp, relpos, scale);
    }

    // Restore the default rendering context
    glUseProgram(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

