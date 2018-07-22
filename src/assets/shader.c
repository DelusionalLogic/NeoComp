#include "shader.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include "logging.h"

#include "assets.h"
#include "../shaders/shaderinfo.h"

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

    // @FRAGILE 64 here is has to be the same as the MAXIMUM length of a shader
    // variable name
    char name[64] = {0};
    int* key;
    JSLF(key, program->attributes, (uint8_t*) name);
    while(key != NULL) {
        glBindAttribLocation(program->gl_program, *key, name);
        JSLN(key, program->attributes, (uint8_t*)name);
    }

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

static int parse_type(char* def, struct shader_value* uniform) {
    char type[64];
    char value[64];
    int matches = sscanf(def, "%63s %63[^\n]", type, value);

    // An EOF from matching means either an error or empty line. We will
    // just swallow those.
    if(matches == EOF)
        return 1;

    if(matches != 2 && matches != 1) {
        printf("Wrongly formatted variable def \"%s\", ignoring\n", def);
        return 1;
    }

    if(strcmp(type, "bool") == 0) {
        uniform->type = SHADER_VALUE_BOOL;

        uniform->required = true;
        if(matches == 2) {
            uniform->required = false;

            if(strcmp(value, "true") == 0) {
                uniform->stock.boolean = true;
            } else if(strcmp(value, "false") == 0) {
                uniform->stock.boolean = false;
            } else {
                printf("Unknown value for boolean \"%s\"\n", value);
                return 1;
            }
        }
    } else if(strcmp(type, "float") == 0) {
        uniform->type = SHADER_VALUE_FLOAT;
        uniform->required = true;
        if(matches == 2) {
            uniform->required = false;

            uniform->stock.flt = atof(value);
        }
    } else if(strcmp(type, "sampler") == 0) {
        uniform->type = SHADER_VALUE_SAMPLER;
        uniform->required = true;
    } else if(strcmp(type, "vec2") == 0) {
        uniform->type = SHADER_VALUE_VEC2;

        uniform->required = true;
        if(matches == 2) {
            uniform->required = false;
            int matches = sscanf(value, "%f,%f", &uniform->stock.vector.x, &uniform->stock.vector.y);

            if(matches != 2) {
                printf("Wrongly formatted vector2 def \"%s\", ignoring\n", value);
                return 1;
            }
        }
        return 0;
    } else if(strcmp(type, "vec3") == 0) {
        uniform->type = SHADER_VALUE_VEC3;

        uniform->required = true;
        if(matches == 2) {
            uniform->required = false;
            int matches = sscanf(
                value,
                "%f,%f,%f",
                &uniform->stock.vec3.x,
                &uniform->stock.vec3.y,
                &uniform->stock.vec3.z
            );

            if(matches != 2) {
                printf("Wrongly formatted vec3 def \"%s\", ignoring\n", value);
                return 1;
            }
        }
        return 0;
    } else if(strcmp(type, "ignored") == 0) {
        uniform->type = SHADER_VALUE_IGNORED;
        uniform->required = false;
        return 0;
    } else {
        printf("Unknown uniform type \"%s\"\n", type);
        return 1;
    }
    return 0;
}

struct shader_program* shader_program_load_file(const char* path) {
    FILE* file = fopen(path, "r");
    if(file == NULL) {
        printf("Failed opening shader program file %s: %s\n", path, strerror(errno));
        return NULL;
    }

    struct shader_program* program = malloc(sizeof(struct shader_program));
    program->vertex = NULL;
    program->fragment = NULL;
    program->attributes = NULL;
    program->gl_program = -1;

    char* shader_type = NULL;

    size_t uniform_cursor = 0;
    char names[SHADER_UNIFORMS_MAX][64] = {{0}};

    char* line = NULL;
    size_t line_size = 0;

    size_t read;
    read = getline(&line, &line_size, file);
    if(read <= 0 || strcmp(line, "#version 1\n") != 0) {
        printf("No version found at the start of file %s\n", path);
        return NULL;
    }

    while((read = getline(&line, &line_size, file)) != -1) {
        if(read == 0 || line[0] == '#')
            continue;

        // Remove the trailing newlines
        line[strcspn(line, "\r\n")] = '\0';

        char type[64];
        char value[64];
        int matches = sscanf(line, "%63s %63[^\n]", type, value);

        // An EOF from matching means either an error or empty line. We will
        // just swallow those.
        if(matches == EOF)
            continue;

        if(matches != 2) {
            printf("Wrongly formatted line \"%s\", ignoring\n", line);
            continue;
        }

        if(strcmp(type, "vertex") == 0) {
            if(program->vertex != NULL) {
                printf("Multiple vertex shader defs in file %s, ignoring \"%s\"\n", path, line);
                continue;
            }
            program->vertex = assets_load(value);
            if(program->vertex == NULL) {
                printf("Failed loading vertex shader for %s, ignoring \"%s\"\n", path, line);
            }
        } else if(strcmp(type, "fragment") == 0) {
            if(program->fragment != NULL) {
                printf("Multiple fragment shader defs in file %s, ignoring \"%s\"\n", path, line);
                continue;
            }
            program->fragment = assets_load(value);
            if(program->fragment == NULL) {
                printf("Failed loading fragment shader for %s, ignoring \"%s\"\n", path, line);
                continue;
            }
        } else if(strcmp(type, "type") == 0) {
            if(shader_type != NULL) {
                printf("Multiple type defs in file %s, ignoring \"%s\"\n", path, line);
                continue;
            }
            shader_type = strdup(value);
            if(shader_type == NULL) {
                printf("Failed duplicating type string for %s, ignoring \"%s\"\n", path, line);
                continue;
            }
        } else if(strcmp(type, "attrib") == 0) {
            int key;
            char name[64];
            int matches = sscanf(value, "%d %63s", &key, name);

            if(matches != 2) {
                printf("Couldn't parse the attrib definition \"%s\"\n", value);
                continue;
            }

            int* index;
            JSLG(index, program->attributes, (uint8_t*)name);
            if(index != NULL) {
                printf("Attrib name %s redefine ignored\n", name);
                continue;
            }

            JSLI(index, program->attributes, (uint8_t*)name);
            if(index == NULL) {
                printf("Failed inserting %s into the attribute array, ignoring\n", name);
            }
            *index = key;
        } else if(strcmp(type, "uniform") == 0) {
            if(uniform_cursor == SHADER_UNIFORMS_MAX) {
                printf("Too many uniforms in shader %s\n", path);
                continue;
            }
            char rest[128];
            int matches = sscanf(value, "%63s %127[^\n]", names[uniform_cursor], rest);

            if(matches != 2) {
                printf("Couldn't parse the uniform definition \"%s\"\n", value);
                continue;
            }

            if(parse_type(rest, &program->uniforms[uniform_cursor]) != 0)
                continue;

            uniform_cursor++;
        } else {
            printf("Unknown directive \"%s\" in shader file %s, ignoring\n", line, path);
        }
    }

    free(line);
    fclose(file);

    if(program->vertex == NULL) {
        printf("Vertex shader not set in %s\n", path);
        // @LEAK We might be leaking the fragment shader, but the assets manager
        // still has a hold of it
        if(shader_type != NULL)
            free(shader_type);
        free(program);
        return NULL;
    }
    if(program->fragment == NULL) {
        printf("Fragment shader not set in %s\n", path);
        // @LEAK We might be leaking the vertex shader, but the assets manager
        // still has a hold of it
        if(shader_type != NULL)
            free(shader_type);
        free(program);
        return NULL;
    }
    if(shader_type == NULL) {
        printf("Type not set in %s\n", path);
        // @LEAK We might be leaking the vertex shader, but the assets manager
        // still has a hold of it
        free(program);
        return NULL;
    }

    shader_program_link(program);

    struct shader_type_info* shader_info = get_shader_type_info(shader_type);
    if(shader_info == NULL) {
        printf("Failed to find shader type info for %s\n", shader_type);
        free(shader_type);
        glDeleteProgram(program->gl_program);
        free(program);
        return NULL;
    }

    program->shader_type_info = shader_info;

    program->shader_type = malloc(shader_info->size);
    if(program->shader_type == NULL) {
        printf("Failed to allocate space for the shader type\n");
        free(shader_type);
        glDeleteProgram(program->gl_program);
        free(program);
        return NULL;
    }

    program->uniforms_num = uniform_cursor;

    // Bind the static shadertype members to the shader_value structs
    for(int i = 0; i < shader_info->member_count; i++) {
        struct shader_uniform_info* uniform_info = &shader_info->members[i];
        struct shader_value** field = (struct shader_value**)(program->shader_type + uniform_info->offset);
        *field = NULL;
        for(int j = 0; j < uniform_cursor; j++) {
            if(strcmp(names[j], uniform_info->name) == 0) {
                *field = &program->uniforms[j];
                break;
            }
        }
        if(*field == NULL) {
            printf("Uniform \"%s\" is not defined in shader %s\n", uniform_info->name, path);
            exit(1);
        }
    }

    printf("Uniforms in shader \"%s\"\n", program->shader_type_info->name);
    // Bind the uniforms to the shader program
    for(int i = 0; i < uniform_cursor; i++) {
        struct shader_value* uniform = &program->uniforms[i];
        uniform->gl_uniform = glGetUniformLocation(program->gl_program, names[i]);
        printf("\tUniform \"%s\" has id %d\n", names[i], uniform->gl_uniform);
    }

    return program;
}

void shader_program_unload_file(struct shader_program* asset) {
    glDeleteProgram(asset->gl_program);
    free(asset->shader_type);
    Word_t freed;
    JSLFA(freed, asset->attributes);
    free(asset);
}

static void set_shader_uniform(const struct shader_value* uniform, const union shader_uniform_value* value) {
    switch(uniform->type) {
        case SHADER_VALUE_BOOL:
            glUniform1i(uniform->gl_uniform, value->boolean);
            break;
        case SHADER_VALUE_FLOAT:
            glUniform1f(uniform->gl_uniform, (double)value->flt);
            break;
        case SHADER_VALUE_VEC2:
            glUniform2f(uniform->gl_uniform, value->vector.x, value->vector.y);
            break;
        case SHADER_VALUE_VEC3:
            glUniform3f(uniform->gl_uniform, value->vec3.x, value->vec3.y, value->vec3.z);
            break;
        case SHADER_VALUE_SAMPLER:
            glUniform1i(uniform->gl_uniform, value->sampler);
            break;
        case SHADER_VALUE_IGNORED:
            // Ignored shaders aren't set
            break;
    }
}

void shader_use(struct shader_program* shader) {
    for(size_t i = 0; i < shader->uniforms_num; i++) {
        const struct shader_value* uniform = &shader->uniforms[i];
        if(uniform->required && !uniform->set) {
            printf("WARNING: Required uniform %zu not set in %s\n", i, shader->shader_type_info->name);
        }
        assert(!uniform->required || uniform->set);
    }

    glUseProgram(shader->gl_program);

    for(size_t i = 0; i < shader->uniforms_num; i++) {
        struct shader_value* uniform = &shader->uniforms[i];
        if(uniform->set) {
            set_shader_uniform(uniform, &uniform->value);
        } else if(!uniform->required) {
            set_shader_uniform(uniform, &uniform->stock);
        }
        shader_clear_future_uniform(uniform);
    }
}

void shader_set_uniform_bool(struct shader_value* uniform, bool value) {
    glUniform1i(uniform->gl_uniform, value);
}

void shader_set_uniform_float(struct shader_value* uniform, float value) {
    glUniform1f(uniform->gl_uniform, value);
}

void shader_set_uniform_vec2(struct shader_value* uniform, const Vector2* value) {
    glUniform2f(uniform->gl_uniform, value->x, value->y);
}

void shader_set_uniform_sampler(struct shader_value* uniform, int value) {
    glUniform1i(uniform->gl_uniform, value);
}

void shader_set_future_uniform_bool(struct shader_value* uniform, bool value) {
    uniform->value.boolean = value;
    uniform->set = true;
}

void shader_set_future_uniform_float(struct shader_value* uniform, float value) {
    uniform->value.flt = value;
    uniform->set = true;
}

void shader_set_future_uniform_vec2(struct shader_value* uniform, const Vector2* value) {
    uniform->value.vector = *value;
    uniform->set = true;
}

void shader_set_future_uniform_vec3(struct shader_value* uniform, const Vector3* value) {
    uniform->value.vec3 = *value;
    uniform->set = true;
}

void shader_set_future_uniform_sampler(struct shader_value* uniform, int value) {
    uniform->value.sampler = value;
    uniform->set = true;
}

void shader_clear_future_uniform(struct shader_value* uniform) {
    uniform->set = false;
}
