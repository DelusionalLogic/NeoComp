#include "assets.h"

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <Judy.h>

#define MAX_EXTENSION 16
struct asset_handler {
    char extension[MAX_EXTENSION];
    asset_loader* loader;
    asset_unloader* unloader;
    Pvoid_t loaded;
};

#define MAX_HANDLERS 16
struct asset_handler asset_handlers[MAX_HANDLERS];
static size_t num_handlers = 0;

#define MAX_PATH_LENGTH 64
#define MAX_PATHS 2
char paths[MAX_PATHS][MAX_PATH_LENGTH];
static size_t num_paths = 0;

void assets_add_handler(asset_type type, const char* extension,
        asset_loader* loader, asset_unloader* unloader) {
    struct asset_handler* handler = &asset_handlers[num_handlers++];

    strncpy(handler->extension, extension, 16);
    handler->loader = loader;
    handler->unloader = unloader;
}

static struct asset_handler* find_handler(const char* path) {
    char* filename = basename(path);
    if(filename == NULL)
        return NULL;

    // We need to skip leading dots
    if(*filename == '.')
        filename++;

    char* extension = strstr(filename, ".");
    if(extension == NULL)
        return NULL;
    extension++;

    for(size_t i = 0; i < num_handlers; i++) {
        struct asset_handler* handler = &asset_handlers[i];
        if(strcmp(handler->extension, extension) == 0) {
            return handler;
        }
    }
    return NULL;
}

static char* append_path(const char* p1, const char* p2) {
    size_t p1_len = strlen(p1);
    size_t p2_len = strlen(p2);
    char* path = malloc(p1_len + p2_len + 1);
    strcpy(path, p1);
    path[p1_len] = '/';
    strcpy(path + p1_len + 1, p2);
    return path;
}

static char* resolve_path(const char* path) {
    char* resolved = NULL;
    for(int i = 0; i < num_paths; i++) {
        resolved = append_path(paths[0], path);

        if(access(resolved, R_OK) == 0) {
            break;
        }

        free(resolved);
        resolved = NULL;
    }
    return resolved;
}

void* assets_load(const char* path) {
    struct asset_handler* handler = find_handler(path);
    if(handler == NULL) {
        printf("There was no handler for this path %s\n", path); return NULL;
    }

    void** asset = NULL;
    JSLG(asset, handler->loaded, path);
    if(asset != NULL)
        return *asset;

    char* abspath = resolve_path(path);
    if(abspath == NULL) {
        printf("No file named %s\n", path);
        return NULL;
    }

    // Allocate space in the loaded array before we actually load the asset to
    // make it easy to bail
    JSLI(asset, handler->loaded, path);
    if(asset == NULL) {
        printf("Failed allocating space for the asset %s\n", path);
        return NULL;
    }

    *asset = handler->loader(abspath);

    return *asset;
}

void assets_add_path(const char* new_path) {
    if(strlen(new_path) >= MAX_PATH_LENGTH) {
        printf("String too long for path %s\n", new_path);
        return;
    }

    if(num_paths >= MAX_PATHS) {
        printf("We were already full of paths when %s came along\n", new_path);
        return;
    }

    char* path = paths[num_paths++];
    strncpy(path, new_path, MAX_PATH_LENGTH);
}
