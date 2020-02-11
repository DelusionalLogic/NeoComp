#include "paths.h"

#include "assets/assets.h"
#include "vector.h"

#include <string.h>

static void add_xdg_home(Vector *scratch, const char* appPath, size_t appPath_len) {
    char* home = getenv("HOME");
    size_t home_len = strlen(home);

    {
        char* conf_dir = getenv("XDG_CONFIG_HOME");
        if(conf_dir == NULL) {
            if(home != NULL) {
                vector_putListBack(scratch, home, home_len);
                vector_putListBack(scratch, "/.config/", 9);
            }
        } else {
            vector_putListBack(scratch, conf_dir, strlen(conf_dir));
        }
    }
    // Take \0 from relPath
    vector_putListBack(scratch, appPath, appPath_len + 1);
    assets_add_path(scratch->data);
    vector_clear(scratch);
}

static void add_xdg_config_dirs(Vector* scratch, char* appPath, size_t appPath_len) {
    char* conf_dirs = getenv("XDG_CONFIG_DIRS");
    if(conf_dirs == NULL) {
        conf_dirs = "/etc/xdg/";
    }

    char* part_end;
    do {
        part_end = strchr(conf_dirs, ':');
        size_t part_len = (part_end != NULL ? (part_end - conf_dirs) : strlen(conf_dirs));
        vector_putListBack(scratch, conf_dirs, part_len);

        // Take \0 from relPath
        vector_putListBack(scratch, appPath, appPath_len + 1);
        assets_add_path(scratch->data);
        vector_clear(scratch);

        conf_dirs += part_len + 1;
    } while(part_end != NULL);
}

void add_xdg_asset_paths() {
    Vector curPath;
    vector_init(&curPath, sizeof(char), 64);

    char* appPath = "/neocomp/assets/";
    size_t appPath_len = 16;

    add_xdg_home(&curPath, appPath, appPath_len);
    add_xdg_config_dirs(&curPath, appPath, appPath_len);
    vector_kill(&curPath);
}
