#include "shader.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "assets.h"

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
    free(asset);
}

static void shader_program_link(struct shader_program* program) {
    program->gl_program = glCreateProgram();
    if(program->gl_program == 0) {
        printf("Failed creating program\n");
        return;
    }

    glAttachShader(program->gl_program, program->fragment->gl_shader);
    glAttachShader(program->gl_program, program->vertex->gl_shader);

    //@INCOMPLETE We need some way of getting the variables
    glBindAttribLocation(program->gl_program, 0, "vertex");
    glBindAttribLocation(program->gl_program, 1, "uv");

    glLinkProgram(program->gl_program);

    GLint status = GL_FALSE;
    glGetProgramiv(program->gl_program, GL_LINK_STATUS, &status);
    if (GL_FALSE == status) {
        printf("Failed linking shader\n");

        GLint log_len = 0;
        glGetProgramiv(program->gl_program, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len) {
            char log[log_len + 1];
            glGetProgramInfoLog(program->gl_program, log_len, NULL, log);
            printf("-- %s\n", log);
            fflush(stdout);
        }

        glDeleteProgram(program->gl_program);
    }
}

struct shader_program* shader_program_load_file(const char* path) {
    FILE* file = fopen(path, "r");
    if(file == NULL) {
        printf("Failed opening shader program file %s\n", path);
        return NULL;
    }

    struct shader_program* program = malloc(sizeof(struct shader_program));
    program->vertex = NULL;
    program->fragment = NULL;
    program->gl_program = -1;

    char* line = NULL;
    size_t line_size = 0;

    size_t read;
    while((read = getline(&line, &line_size, file)) != -1) {
        char type[64];
        char path[64];
        int matches = sscanf(line, "%511s %511s", type, path);

        if(matches != 2)
            continue;

        if(strcmp(type, "vertex")) {
            if(program->vertex != NULL) {
                printf("Multiple vertex shader defs in file %s, ignoring %s\n", path, line);
                continue;
            }
            program->vertex = assets_load(path);
        } else if(strcmp(type, "fragment")) {
            if(program->fragment != NULL) {
                printf("Multiple fragment shader defs in file %s, ignoring %s\n", path, line);
                continue;
            }
            program->fragment = assets_load(path);
        }
    }

    free(line);
    fclose(file);

    if(program->vertex == NULL) {
        printf("vertex shader not set in %s\n", path);
        // @LEAK We might be leaking the fragment shader, but the assets manager
        // still has a hold of it
        free(program);
        return NULL;
    }
    if(program->fragment == NULL) {
        printf("fragment shader not set in %s\n", path);
        // @LEAK We might be leaking the vertex shader, but the assets manager
        // still has a hold of it
        free(program);
        return NULL;
    }

    shader_program_link(program);

    return program;
}

void shader_program_unload_file(struct shader_program* asset) {
}
