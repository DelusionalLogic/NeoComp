#include "shader.h"

#include <GL/gl.h>
#include <stdlib.h>
#include <stdio.h>

static struct shader* shader_load_file(const char* path, GLenum type) {

    FILE* file = fopen(path, "r");
    if(file == NULL) {
        printf("Failed loading shader file %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = malloc(length + 1);
    if(buffer == NULL) {
        printf("Failed allocating string for shader %s of length %ld\n", path, length);
        return NULL;
    }

    if(fread(buffer, 1, length, file) != length) {
        printf("Failed reading the shader %s\n", path);
        free(buffer);
        return NULL;
    }
    buffer[length] = '\0';

    fclose(file);

    struct shader* shader = malloc(sizeof(struct shader));

    // @INCOMPLETE We need to make this an argument
    shader->gl_shader = glCreateShader(type);
    if(shader < 0) {
        printf("Failed creating the shader object for %s\n", path);
        free(buffer);
        free(shader);
        return NULL;
    }

    glShaderSource(shader->gl_shader, 1, (const char**)&buffer, NULL);
    glCompileShader(shader->gl_shader);

    // GL Lets you free the string right after the compile
    free(buffer);

    int status = GL_FALSE;
    glGetShaderiv(shader->gl_shader, GL_COMPILE_STATUS, &status);
    if(status == GL_FALSE) {
        printf("Failed compiling shader %s\n", path);

        GLint log_len = 0;
        glGetShaderiv(shader->gl_shader, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len) {
            char log[log_len + 1];
            glGetShaderInfoLog(shader->gl_shader, log_len, NULL, log);
            printf(" -- %s\n", log);
            fflush(stdout);
        }

        return NULL;
    }

    return shader;
}

struct shader* vert_shader_load_file(const char* path) {
    return shader_load_file(path, GL_VERTEX_SHADER);
}

struct shader* frag_shader_load_file(const char* path) {
    return shader_load_file(path, GL_FRAGMENT_SHADER);
}


void shader_unload_file(struct shader* asset) {
    glDeleteShader(asset->gl_shader);
}

struct shader_program* shader_program_load_file(const char* path);
void shader_program_unload_file(struct shader_program* asset);
