#pragma once

#include "include.h"

void add_shader_type(struct shader_type_info* info);

struct shader_type_info* get_shader_type_info(char* name);

void* new_shader_type(char* name);
