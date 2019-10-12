#include "assets.h"

#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <Judy.h>

#include "logging.h"

// Types {{{
struct type_info {
    int id;
    size_t size;
};

static Pvoid_t types = NULL;
static int next_type_id;

type_id type_find(char* type_name, size_t size) {
    struct type_info** type = NULL;
    JSLG(type, types, type_name);
    if(type != NULL) {
        return *type;
    }

    JSLI(type, types, type_name);
    if(type == NULL) {
        printf("Couldn't allocate space for type_info for %s\n", type_name);
        return NULL;
    }
    *type = malloc(sizeof(struct type_info));

    (*type)->id = next_type_id++;
    (*type)->size = size;

    return *type;
}
// }}}

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
#define MAX_PATHS 16
char paths[MAX_PATHS][MAX_PATH_LENGTH];
static size_t num_paths = 0;

static int notifyFd;
static Pvoid_t watches;

void assets_init() {
    notifyFd = inotify_init1(IN_NONBLOCK);
}

void assets_add_handler_internal(asset_type type, const char* extension,
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
    // Make room for the separating / and the final \0
    char* path = malloc(p1_len + p2_len + 2);
    strcpy(path, p1);
    path[p1_len] = '/';
    strcpy(path + p1_len + 1, p2);
    path[p1_len + p2_len + 1] = '\0';
    return path;
}

char* assets_resolve_path(const char* path) {
    char* resolved = NULL;
    for(int i = 0; i < num_paths; i++) {
        resolved = append_path(paths[i], path);

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

    char* abspath = assets_resolve_path(path);
    if(abspath == NULL) {
        printf("No file named %s\n", path);
        free(abspath);
        return NULL;
    }

    // Copy the path to save for watch
    size_t pathlen = strlen(path);
    char* pathcpy = malloc(pathlen + 1);
    memcpy(pathcpy, path, pathlen + 1);

    printf("Adding watch for %s\n", abspath);
    int watch = inotify_add_watch(notifyFd, abspath, IN_MODIFY);
    if(watch == -1) {
        printf("Failed establishing inotify watch on %s\n", path);
        free(abspath);
        return NULL;
    }

    // Save the watch id
    char** watchPath;
    JLI(watchPath, watches, watch);
    if(watchPath == NULL) {
        printf("Failed allocating space for the watch for %s\n", path);
        free(abspath);
        return NULL;
    }

    *watchPath = pathcpy;

    // Allocate space in the loaded array before we actually load the asset to
    // make it easy to bail
    JSLI(asset, handler->loaded, path);
    if(asset == NULL) {
        printf("Failed allocating space for the asset %s\n", path);
        free(abspath);
        return NULL;
    }

    *asset = handler->loader(abspath);

    free(abspath);
    return *asset;
}

void assets_hotload() {
    struct inotify_event *event = malloc(sizeof(struct inotify_event) * NAME_MAX + 1);
    while(read(notifyFd, event, sizeof(struct inotify_event) * NAME_MAX + 1) != -1) {
        printf("EVENT\n");
        char **pathPtr;
        JLG(pathPtr, watches, event->wd);

        struct asset_handler* handler = find_handler(*pathPtr);
        if(handler == NULL) {
            printf("There was no handler for this path %s\n", "shadow.shader");
            return;
        }

        void** asset = NULL;
        JSLG(asset, handler->loaded, *pathPtr);
        assert(asset != NULL);

        handler->unloader(*asset);
        int i = 0;
        JSLD(i, handler->loaded, *pathPtr);
    }
    free(event);

    if(errno != EAGAIN) {
        printf("Bad errorno on hotload read %s\n", strerror(errno));
    }
}

void assets_add_path(const char* new_path) {
    if(strlen(new_path) >= MAX_PATH_LENGTH) {
        printf_errf("String too long for path %s", new_path);
        return;
    }

    if(num_paths >= MAX_PATHS) {
        printf_errf("We were already full of paths when %s came along", new_path);
        return;
    }

    printf_dbgf("Using asset path %s", new_path);

    char* path = paths[num_paths++];
    strncpy(path, new_path, MAX_PATH_LENGTH);
}
