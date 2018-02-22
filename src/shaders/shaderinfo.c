#include "shaderinfo.h"

#include <Judy.h>

static Pvoid_t shader_types = NULL;

void add_shader_type(struct shader_type_info* info) {
    struct shader_type_info** new_info;

    JSLG(new_info, shader_types, info->name);
    if(new_info != NULL) {
        printf("Shader type %s already registered\n", info->name);
        return;
    }

    JSLI(new_info, shader_types, info->name);
    if(new_info == NULL) {
        printf("Failed allocating space for the shader info\n");
        return;
    }

    *new_info = info;
}

struct shader_type_info* get_shader_type_info(char* name) {
    struct shader_type_info** info;

    JSLG(info, shader_types, name);
    if(info == NULL) {
        printf("Shader type %s not found\n", name);
        return NULL;
    }

    return *info;
}
