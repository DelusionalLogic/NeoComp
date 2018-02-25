#include "renderutil.h"

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
    glDrawArrays(GL_QUADS, 0, 4);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

