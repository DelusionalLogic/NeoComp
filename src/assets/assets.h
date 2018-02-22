#pragma once

#include <stdlib.h>

typedef struct type_info* type_id;

#define typeid(TYPE) type_find(#TYPE, sizeof(TYPE))
type_id type_find(char* type, size_t size);
char* type_id_name(int id);

typedef type_id asset_type;

typedef void* asset_loader(const char* path);
typedef void* asset_unloader(const char* path);

void assets_add_handler_internal(asset_type type, const char* extension,
        asset_loader* loader, asset_unloader* unloader);
#define assets_add_handler(type, extension, loader, unloader) \
    assets_add_handler_internal(typeid(type), extension, (asset_loader*)loader, \
            (asset_unloader*)unloader)

void* assets_load(const char* path);

void assets_add_path(const char* path);
