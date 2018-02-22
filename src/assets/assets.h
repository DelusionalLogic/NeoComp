#pragma once

typedef int asset_type;

typedef void* asset_loader(const char* path);
typedef void* asset_unloader(const char* path);

void assets_add_handler(asset_type type, const char* extension,
        asset_loader* loader, asset_unloader* unloader);

void* assets_load(const char* path);

void assets_add_path(const char* path);
