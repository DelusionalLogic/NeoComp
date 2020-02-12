#include "assets.h"

#include <assert.h>
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
};

#define MAX_HANDLERS 16
struct asset_handler asset_handlers[MAX_HANDLERS] = {0};
static size_t num_handlers = 0;

#define MAX_PATH_LENGTH 64
#define MAX_PATHS 16
char paths[MAX_PATHS][MAX_PATH_LENGTH];
static size_t num_paths = 0;

#define MAX_DEPS 16
struct asset_handle {
    void* asset;
    char* path;
    struct asset_handler* handler;
    struct asset_handle *depends[MAX_DEPS];
    size_t num_depends;
};

static Pvoid_t loaded = NULL;

static int notifyFd;
static struct asset_handle* current;
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

// Watch for changes on an asset.
static void watch_asset(struct asset_handle *handle) {
    int watch = inotify_add_watch(notifyFd, handle->path, IN_MODIFY);
    if(watch == -1) {
        printf("Failed establishing inotify watch on %s\n", handle->path);
        return;
    }

    // Save the watch id
    char** value;
    JLI(value, watches, watch);
    if(value == NULL) {
        printf("Failed allocating space for the watch for %s\n", handle->path);
        inotify_rm_watch(notifyFd, watch);
        return;
    }

    *value = handle;
}

void* assets_load(const char* path) {
    struct asset_handle** handlePtr = NULL;
    JSLG(handlePtr, loaded, path);
    if(handlePtr != NULL) {
        assert(*handlePtr != NULL);

        if(current != NULL) {
            // Add a dependency. Keep in mind that _current_ here is not our
            // handle, but rather the handle that has loaded us.
            current->depends[current->num_depends] = *handlePtr;
            current->num_depends++;
        }

        return (*handlePtr)->asset;
    }

    struct asset_handler* handler = find_handler(path);
    if(handler == NULL) {
        printf("There was no handler for this path %s\n", path); return NULL;
    }

    char* abspath = assets_resolve_path(path);
    if(abspath == NULL) {
        printf("No file named %s\n", path);
        free(abspath);
        return NULL;
    }
    printf("Cold-loading file %s\n", abspath);

    // Copy the path to save for watch
    size_t pathlen = strlen(path);
    char* pathcpy = malloc(pathlen + 1);
    memcpy(pathcpy, path, pathlen + 1);

    // Allocate space in the loaded array before we actually load the asset to
    // make it easy to bail
    JSLI(handlePtr, loaded, path);
    if(handlePtr == NULL) {
        printf("Failed allocating space for the asset %s\n", path);
        free(abspath);
        return NULL;
    }

    struct asset_handle* parent = current;
    current = malloc(sizeof(struct asset_handle));
    if(current == NULL) {
        printf("Failed allocating space for the asset handle %s\n", path);
        current = parent;
        free(abspath);
        return NULL;
    }
    *handlePtr = current;
    if(parent != NULL) {
        parent->depends[parent->num_depends] = current;
        parent->num_depends++;
    }

    current->num_depends = 0;
    current->handler = handler;
    current->path = abspath;

    // Asset handlers are allowed to load subassets, which might invalidate all
    // pointers before this call
    void* asset = handler->loader(abspath);

    current->asset = asset;

    watch_asset(current);

    current = parent;

    return asset;
}

// Find assets with a dependency on this asset. In this context we call them
// _users_.
static void find_users(struct asset_handle* handle, struct asset_handle **users, size_t *num_users) {
    char name[MAX_PATH_LENGTH] = {0};
    struct asset_handle** valuePtr;
    JSLF(valuePtr, loaded, (uint8_t*)name);
    while(valuePtr != NULL) {
        struct asset_handle *value = *valuePtr;
        for(int i = 0; i < value->num_depends; i++) {
            if(value->depends[i] == handle) {
                assert(*num_users < MAX_DEPS);
                users[(*num_users)++] = value;
                break;
            }
        }
        JSLN(valuePtr, loaded, (uint8_t*)name);
    }
}

// Reload an assset that has changed on disk.
static void hotload_asset(struct asset_handle* handle) {
    printf("Hotloading %s\n", handle->path);
    struct asset_handle* parent = current;

    struct asset_handle *reload[MAX_DEPS];
    size_t num_reload = 0;

    find_users(handle, &reload, &num_reload);

    current = handle;
    handle->num_depends = 0;
    void* newAsset = handle->handler->loader(handle->path); 
    // If we fail to load the new asset, just leave the old one.
    if(newAsset == NULL) {
        printf("Reload failed \n");
        current = parent;
        return;
    }
    handle->handler->unloader(handle->asset);
    handle->asset = newAsset;

    // Reload everyone that depended on us.
    for(int i = 0; i < num_reload; i++) {
        hotload_asset(reload[i]);
    }

    current = parent;
}

// Called once per iteration. Figure out if any assets were hotloaded and
// reload them.
void assets_hotload() {
    struct inotify_event *event = malloc(sizeof(struct inotify_event) * NAME_MAX + 1);
    while(read(notifyFd, event, sizeof(struct inotify_event) * NAME_MAX + 1) != -1) {
        char **handlePtr;
        JLG(handlePtr, watches, event->wd);
        struct asset_handle* handle = *handlePtr;

        hotload_asset(handle);
    }

    if(errno != EAGAIN) {
        printf("Bad errorno on hotload read %s\n", strerror(errno));
    }

    free(event);
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
