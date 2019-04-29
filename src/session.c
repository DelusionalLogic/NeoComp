#include "session.h"

#include "common.h"
#include "c2.h"

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
    "-b\n"
    "  Daemonize process.\n"
    "\n"
    "--show-all-xerrors\n"
    "  Show all X errors (for debugging).\n"
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
    "--shadow-exclude condition\n"
    "  Exclude conditions for shadows.\n"
    "\n"
    "--fade-exclude condition\n"
    "  Exclude conditions for fading.\n"
    "\n"
    "--respect-prop-shadow\n"
    "  Respect _COMPTON_SHADOW. This a prototype-level feature, which\n"
    "  you must not rely on.\n"
    "\n"
    "--focus-exclude condition\n"
    "  Specify a list of conditions of windows that should always be\n"
    "  considered focused.\n"
    "\n"
    "--blur-background\n"
	"  Blur background of semi-transparent / ARGB windows. Bad in\n"
	"  performance. The switch name may change without prior\n"
	"  notifications.\n"
    "\n"
    "--blur-background-exclude condition\n"
    "  Exclude conditions for background blur.\n"
    "\n"
    "--opacity-rule opacity:condition\n"
    "  Specify a list of opacity rules, in the format \"PERCENT:PATTERN\",\n"
    "  like \'50:name *= \"Firefox\"'. compton-trans is recommended over\n"
    "  this. Note we do not distinguish 100% and unset, and we don't make\n"
    "  any guarantee about possible conflicts with other programs that set\n"
    "  _NET_WM_WINDOW_OPACITY on frame or client windows.\n"
    "\n"
    "--benchmark cycles\n"
    "  Benchmark mode. Repeatedly paint until reaching the specified cycles.\n"
    "\n"
    "--benchmark-wid window-id\n"
    "  Specify window ID to repaint in benchmark mode. If omitted or is 0,\n"
    "  the whole screen is repainted.\n"
    ;
  FILE *f = (ret ? stderr: stdout);
  fputs(usage_text, f);
#undef WARNING
#undef WARNING_DISABLED

  exit(ret);
}

/**
 * Add a pattern to a condition linked list.
 */
static bool condlst_add(session_t *ps, c2_lptr_t **pcondlst, const char *pattern) {
    if (!pattern)
        return false;

#ifdef CONFIG_C2
    if (!c2_parse(ps, pcondlst, pattern))
        exit(1);
#else
    printf_errfq(1, "(): Condition support not compiled in.");
#endif

    return true;
}

static void wintype_arr_enable(bool arr[]) {
    wintype_t i;

    for (i = 0; i < NUM_WINTYPES; ++i) {
        arr[i] = true;
    }
}

/**
 * Parse a list of opacity rules.
 */
static bool
parse_rule_opacity(session_t *ps, const char *src) {
#ifdef CONFIG_C2
    // Find opacity value
    char *endptr = NULL;
    long val = strtol(src, &endptr, 0);
    if (!endptr || endptr == src) {
        printf_errf("(\"%s\"): No opacity specified?", src);
        return false;
    }
    if (val > 100 || val < 0) {
        printf_errf("(\"%s\"): Opacity %ld invalid.", src, val);
        return false;
    }

    // Skip over spaces
    while (*endptr && isspace(*endptr))
        ++endptr;
    if (':' != *endptr) {
        printf_errf("(\"%s\"): Opacity terminator not found.", src);
        return false;
    }
    ++endptr;

    // Parse pattern
    // I hope 1-100 is acceptable for (void *)
    return c2_parsed(ps, &ps->o.opacity_rules, endptr, (void *) val);
#else
    printf_errf("(\"%s\"): Condition support not compiled in.", src);
    return false;
#endif
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

    path = mstrjoin3(home, config_home_suffix, config_filename);
  }
  else
    path = mstrjoin(dir, config_filename);

  f = fopen(path, "r");

  if (f && ppath)
    *ppath = path;
  else
    free(path);
  if (f)
    return f;

  // Then check user configuration file in $HOME
  if ((home = getenv("HOME")) && strlen(home)) {
    path = mstrjoin(home, config_filename_legacy);
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
      path = mstrjoin(part, config_filename);
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
    path = mstrjoin(config_system_dir, config_filename);
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

/**
 * Parse a condition list in configuration file.
 */
static void
parse_cfg_condlst(session_t *ps, const config_t *pcfg, c2_lptr_t **pcondlst,
    const char *name) {
  config_setting_t *setting = config_lookup(pcfg, name);
  if (setting) {
    // Parse an array of options
    if (config_setting_is_array(setting)) {
      int i = config_setting_length(setting);
      while (i--)
        condlst_add(ps, pcondlst, config_setting_get_string_elem(setting, i));
    }
    // Treat it as a single pattern if it's a string
    else if (CONFIG_TYPE_STRING == config_setting_type(setting)) {
      condlst_add(ps, pcondlst, config_setting_get_string(setting));
    }
  }
}

/**
 * Parse an opacity rule list in configuration file.
 */
static void
parse_cfg_condlst_opct(session_t *ps, const config_t *pcfg, const char *name) {
  config_setting_t *setting = config_lookup(pcfg, name);
  if (setting) {
    // Parse an array of options
    if (config_setting_is_array(setting)) {
      int i = config_setting_length(setting);
      while (i--)
        if (!parse_rule_opacity(ps, config_setting_get_string_elem(setting, i)))
          exit(1);
    }
    // Treat it as a single pattern if it's a string
    else if (CONFIG_TYPE_STRING == config_setting_type(setting)) {
      parse_rule_opacity(ps, config_setting_get_string(setting));
    }
  }
}

static void lcfg_lookup_bool(const config_t *config, const char *path, bool *value) {
    int ival;

    if (config_lookup_bool(config, path, &ival))
        *value = ival;
}

static int lcfg_lookup_int(const config_t *config, const char *path, int *value) {
    return config_lookup_int(config, path, value);
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
#ifndef CONFIG_LIBCONFIG_LEGACY
    {
        // dirname() could modify the original string, thus we must pass a
        // copy
        char *path2 = mstrcpy(path);
        char *parent = dirname(path2);

        if (parent)
            config_set_include_dir(&cfg, parent);

        free(path2);
    }
#endif

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
        ps->o.inactive_opacity = normalize_d(dval) * 100.0;
    // -I (active_opacity)
    if (config_lookup_float(&cfg, "active-opacity", &dval))
        ps->o.active_opacity = normalize_d(dval) * 100.0;
    // --opacity-fade-time
    if (config_lookup_float(&cfg, "opacity-fade-time", &dval))
        ps->o.opacity_fade_time = dval;
    // --bg-opacity-fade-time
    if (config_lookup_float(&cfg, "bg-opacity-fade-time", &dval))
        ps->o.bg_opacity_fade_time = dval;
    // -c (shadow_enable)
    if (config_lookup_bool(&cfg, "shadow", &ival) && ival)
        wintype_arr_enable(ps->o.wintype_shadow);
    // -C (no_dock_shadow)
    lcfg_lookup_bool(&cfg, "no-dock-shadow", &pcfgtmp->no_dock_shadow);
    // -G (no_dnd_shadow)
    lcfg_lookup_bool(&cfg, "no-dnd-shadow", &pcfgtmp->no_dnd_shadow);
    // -m (menu_opacity)
    config_lookup_float(&cfg, "menu-opacity", &pcfgtmp->menu_opacity);
    // -f (fading_enable)
    if (config_lookup_bool(&cfg, "fading", &ival) && ival)
        wintype_arr_enable(ps->o.wintype_fade);
    // --inactive-dim
    config_lookup_float(&cfg, "inactive-dim", &ps->o.inactive_dim);
    // --dim-fade-time
    if (config_lookup_float(&cfg, "dim-fade-time", &dval))
        ps->o.dim_fade_time = dval;
    // --mark-wmwin-focused
    lcfg_lookup_bool(&cfg, "mark-wmwin-focused", &ps->o.mark_wmwin_focused);
    // --shadow-exclude
    parse_cfg_condlst(ps, &cfg, &ps->o.shadow_blacklist, "shadow-exclude");
    // --fade-exclude
    parse_cfg_condlst(ps, &cfg, &ps->o.fade_blacklist, "fade-exclude");
    // --focus-exclude
    parse_cfg_condlst(ps, &cfg, &ps->o.focus_blacklist, "focus-exclude");
    // --blur-background-exclude
    parse_cfg_condlst(ps, &cfg, &ps->o.blur_background_blacklist, "blur-background-exclude");
    // --opacity-rule
    parse_cfg_condlst_opct(ps, &cfg, "opacity-rule");
    // --blur-background
    lcfg_lookup_bool(&cfg, "blur-background", &ps->o.blur_background);
    // --blur-level
    lcfg_lookup_int(&cfg, "blur-level", &ps->o.blur_level);
    // Wintype settings
    {
        wintype_t i;

        for (i = 0; i < NUM_WINTYPES; ++i) {
            char *str = mstrjoin("wintypes.", WINTYPES[i]);
            config_setting_t *setting = config_lookup(&cfg, str);
            free(str);
            if (setting) {
                if (config_setting_lookup_bool(setting, "shadow", &ival))
                    ps->o.wintype_shadow[i] = (bool) ival;
                if (config_setting_lookup_bool(setting, "fade", &ival))
                    ps->o.wintype_fade[i] = (bool) ival;
                if (config_setting_lookup_bool(setting, "focus", &ival))
                    ps->o.wintype_focus[i] = (bool) ival;
                config_setting_lookup_float(setting, "opacity",
                        &ps->o.wintype_opacity[i]);
            }
        }
    }

    config_destroy(&cfg);
}
#endif
