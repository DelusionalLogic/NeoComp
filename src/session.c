#include "session.h"

#include "common.h"

#include <libconfig.h>

/**
 * Print usage text and exit.
 */
void usage(int ret) {
#define WARNING_DISABLED " (DISABLED AT COMPILE TIME)"
#define WARNING
  const static char *usage_text =
    "compton (" COMPTON_VERSION ")\n"
    "usage: compton [options]\n"
    "Options:\n"
    "\n"
    "-d display\n"
    "  Which display should be managed.\n"
    "\n"
    "-m opacity\n"
    "  The opacity for menus. (default 1.0)\n"
    "\n"
    "-c\n"
    "  Enabled client-side shadows on windows.\n"
    "\n"
    "-C\n"
    "  Avoid drawing shadows on dock/panel windows.\n"
    "\n"
    "-f\n"
    "  Fade windows in/out when opening/closing and when opacity\n"
    "  changes, unless --no-fading-openclose is used.\n"
    "\n"
    "-i opacity\n"
    "  Opacity of inactive windows. (0.1 - 1.0)\n"
    "\n"
    "-e opacity\n"
    "  Opacity of window titlebars and borders. (0.1 - 1.0)\n"
    "\n"
    "-G\n"
    "  Don't draw shadows on DND windows\n"
    "\n"
#undef WARNING
#ifndef CONFIG_LIBCONFIG
#define WARNING WARNING_DISABLED
#else
#define WARNING
#endif
    "--config path\n"
    "  Look for configuration file at the path. Use /dev/null to avoid\n"
    "  loading configuration file." WARNING "\n"
    "\n"
    "--inactive-dim value\n"
    "  Dim inactive windows. (0.0 - 1.0)\n"
    "\n"
    "--active-opacity opacity\n"
    "  Default opacity for active windows. (0.0 - 1.0)\n"
    "\n"
    "--mark-wmwin-focused\n"
    "  Try to detect WM windows and mark them as active.\n"
    "\n"
    "--blur-background\n"
	"  Blur background of semi-transparent / ARGB windows. Bad in\n"
	"  performance. The switch name may change without prior\n"
	"  notifications.\n"
    "\n"
    "--benchmark cycles\n"
    "  Benchmark mode. Repeatedly paint until reaching the specified cycles.\n"
    ;
  FILE *f = (ret ? stderr: stdout);
  fputs(usage_text, f);
#undef WARNING
#undef WARNING_DISABLED

  exit(ret);
}

#ifdef CONFIG_LIBCONFIG
/**
 * Get a file stream of the configuration file to read.
 *
 * Follows the XDG specification to search for the configuration file.
 */
static FILE *
open_config_file(char *cpath, char **ppath) {
  const static char *config_filename = "/compton.conf";
  const static char *config_filename_legacy = "/.compton.conf";
  const static char *config_home_suffix = "/.config";
  const static char *config_system_dir = "/etc/xdg";

  char *dir = NULL, *home = NULL;
  char *path = cpath;
  FILE *f = NULL;

  if (path) {
    f = fopen(path, "r");
    if (f && ppath)
      *ppath = path;
    return f;
  }

  // Check user configuration file in $XDG_CONFIG_HOME firstly
  if (!((dir = getenv("XDG_CONFIG_HOME")) && strlen(dir))) {
      if (!((home = getenv("HOME")) && strlen(home)))
          return NULL;

      path = cmalloc(strlen(home) + strlen(config_home_suffix)
              + strlen(config_filename) + 1, char);

      strcpy(path, home);
      strcat(path, config_home_suffix);
      strcat(path, config_filename);
  } else {
      path = cmalloc(strlen(dir) + strlen(config_filename) + 1, char);

      strcpy(path, dir);
      strcat(path, config_filename);
  }

  f = fopen(path, "r");

  if (f && ppath)
    *ppath = path;
  else
    free(path);
  if (f)
    return f;

  // Then check user configuration file in $HOME
  if ((home = getenv("HOME")) && strlen(home)) {
    path = cmalloc(strlen(home) + strlen(config_filename_legacy) + 1, char);
    strcpy(path, home);
    strcat(path, config_filename_legacy);
    f = fopen(path, "r");
    if (f && ppath)
      *ppath = path;
    else
      free(path);
    if (f)
      return f;
  }

  // Check system configuration file in $XDG_CONFIG_DIRS at last
  if ((dir = getenv("XDG_CONFIG_DIRS")) && strlen(dir)) {
    char *part = strtok(dir, ":");
    while (part) {
      path = cmalloc(strlen(home) + strlen(config_filename) + 1, char);
      strcpy(path, home);
      strcat(path, config_filename);
      f = fopen(path, "r");
      if (f && ppath)
        *ppath = path;
      else
        free(path);
      if (f)
        return f;
      part = strtok(NULL, ":");
    }
  }
  else {
    path = cmalloc(strlen(config_system_dir) + strlen(config_filename) + 1, char);
    strcpy(path, config_system_dir);
    strcat(path, config_filename);
    f = fopen(path, "r");
    if (f && ppath)
      *ppath = path;
    else
      free(path);
    if (f)
      return f;
  }

  return NULL;
}

static void lcfg_lookup_bool(const config_t *config, const char *path, bool *value) {
    int ival;

    if (config_lookup_bool(config, path, &ival))
        *value = ival;
}

static int lcfg_lookup_int(const config_t *config, const char *path, int *value) {
    return config_lookup_int(config, path, value);
}

static inline double __attribute__((const)) clamp_double(double d) {
    return (d > 1.0 ? 1.0 : (d < 0.0 ? 0.0 : d));
}

/**
 * Parse a configuration file from default location.
 */
void parse_config(session_t *ps, struct options_tmp *pcfgtmp) {
    char *path = NULL;
    FILE *f;
    config_t cfg;
    int ival = 0;
    double dval = 0.0;
    // libconfig manages string memory itself, so no need to manually free
    // anything

    f = open_config_file(ps->o.config_file, &path);
    if (!f) {
        if (ps->o.config_file) {
            printf_errfq(1, "(): Failed to read configuration file \"%s\".",
                    ps->o.config_file);
            free(ps->o.config_file);
            ps->o.config_file = NULL;
        }
        return;
    }

    config_init(&cfg);
    {
        // dirname() could modify the original string, thus we must pass a
        // copy
        char *path2 = mstrcpy(path);
        char *parent = dirname(path2);

        if (parent)
            config_set_include_dir(&cfg, parent);

        free(path2);
    }

    {
        int read_result = config_read(&cfg, f);
        fclose(f);
        f = NULL;
        if (CONFIG_FALSE == read_result) {
            printf("Error when reading configuration file \"%s\", line %d: %s\n",
                    path, config_error_line(&cfg), config_error_text(&cfg));
            config_destroy(&cfg);
            free(path);
            return;
        }
    }
    config_set_auto_convert(&cfg, 1);

    if (path != ps->o.config_file) {
        free(ps->o.config_file);
        ps->o.config_file = path;
    }

    // Get options from the configuration file. We don't do range checking
    // right now. It will be done later

    // -i (inactive_opacity)
    if (config_lookup_float(&cfg, "inactive-opacity", &dval))
        ps->o.inactive_opacity = clamp_double(dval) * 100.0;
    // -I (active_opacity)
    if (config_lookup_float(&cfg, "active-opacity", &dval))
        ps->o.active_opacity = clamp_double(dval) * 100.0;
    // --opacity-fade-time
    if (config_lookup_float(&cfg, "opacity-fade-time", &dval))
        ps->o.opacity_fade_time = dval;
    // --bg-opacity-fade-time
    if (config_lookup_float(&cfg, "bg-opacity-fade-time", &dval))
        ps->o.bg_opacity_fade_time = dval;
    // --inactive-dim
    config_lookup_float(&cfg, "inactive-dim", &ps->o.inactive_dim);
    // --dim-fade-time
    if (config_lookup_float(&cfg, "dim-fade-time", &dval))
        ps->o.dim_fade_time = dval;
    // --blur-background
    lcfg_lookup_bool(&cfg, "blur-background", &ps->o.blur_background);
    // --blur-level
    lcfg_lookup_int(&cfg, "blur-level", &ps->o.blur_level);
    // Wintype settings
    {
        wintype_t i;

        for (i = 0; i < NUM_WINTYPES; ++i) {
            char* str = cmalloc(9 + strlen(WINTYPES[i]) + 1, char);
            strcpy(str, "wintypes.");
            strcat(str, WINTYPES[i]);
            config_setting_t *setting = config_lookup(&cfg, str);
            free(str);
            if (setting) {
                if (config_setting_lookup_bool(setting, "focus", &ival))
                    ps->o.wintype_focus[i] = (bool) ival;
                config_setting_lookup_float(setting, "opacity",
                        &ps->o.wintype_opacity[i]);
                if (config_setting_lookup_bool(setting, "shadow", &ival))
                    ps->o.wintype_shadow[i] = (bool) ival;
            }
        }
    }

    config_destroy(&cfg);
}
#endif
