// SPDX-License-Identifier: MIT
// Copyright (c) 2012-2014 Richard Grenville <pyxlcy@gmail.com>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <libgen.h>
#include <libconfig.h>

#include "common.h"
#include "config.h"

/**
 * Wrapper of libconfig's <code>config_lookup_int</code>.
 *
 * So it takes a pointer to bool.
 */
static inline int
lcfg_lookup_bool(const config_t *config, const char *path, bool *value) {
  int ival;

  int ret = config_lookup_bool(config, path, &ival);
  if (ret)
    *value = ival;

  return ret;
}

/**
 * Get a file stream of the configuration file to read.
 *
 * Follows the XDG specification to search for the configuration file.
 */
FILE *
open_config_file(char *cpath, char **ppath) {
  static const char *config_filename = "/compton.conf";
  static const char *config_filename_legacy = "/.compton.conf";
  static const char *config_home_suffix = "/.config";
  static const char *config_system_dir = "/etc/xdg";

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
void
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
static inline void
parse_cfg_condlst_opct(session_t *ps, const config_t *pcfg, const char *name) {
  config_setting_t *setting = config_lookup(pcfg, name);
  if (setting) {
    // Parse an array of options
    if (config_setting_is_array(setting)) {
      int i = config_setting_length(setting);
      while (i--)
        if (!parse_rule_opacity(ps, config_setting_get_string_elem(setting,
                i)))
          exit(1);
    }
    // Treat it as a single pattern if it's a string
    else if (CONFIG_TYPE_STRING == config_setting_type(setting)) {
      parse_rule_opacity(ps, config_setting_get_string(setting));
    }
  }
}

/**
 * Parse a configuration file from default location.
 */
void
parse_config(options_t *o, struct options_tmp *pcfgtmp) {
  char *path = NULL;
  FILE *f;
  config_t cfg;
  int ival = 0;
  bool bval;
  double dval = 0.0;
  // libconfig manages string memory itself, so no need to manually free
  // anything
  const char *sval = NULL;

  f = open_config_file(o->config_file, &path);
  if (!f) {
    if (o->config_file) {
      printf_errfq(1, "(): Failed to read configuration file \"%s\".",
          o->config_file);
      free(o->config_file);
      o->config_file = NULL;
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

  if (path != o->config_file) {
    free(o->config_file);
    o->config_file = path;
  }

  // Get options from the configuration file. We don't do range checking
  // right now. It will be done later

  // -D (fade_delta)
  if (config_lookup_int(&cfg, "fade-delta", &ival))
    o->fade_delta = ival;
  // -I (fade_in_step)
  if (config_lookup_float(&cfg, "fade-in-step", &dval))
    o->fade_in_step = normalize_d(dval) * OPAQUE;
  // -O (fade_out_step)
  if (config_lookup_float(&cfg, "fade-out-step", &dval))
    o->fade_out_step = normalize_d(dval) * OPAQUE;
  // -r (shadow_radius)
  config_lookup_int(&cfg, "shadow-radius", &o->shadow_radius);
  // -o (shadow_opacity)
  config_lookup_float(&cfg, "shadow-opacity", &o->shadow_opacity);
  // -l (shadow_offset_x)
  config_lookup_int(&cfg, "shadow-offset-x", &o->shadow_offset_x);
  // -t (shadow_offset_y)
  config_lookup_int(&cfg, "shadow-offset-y", &o->shadow_offset_y);
  // -i (inactive_opacity)
  if (config_lookup_float(&cfg, "inactive-opacity", &dval))
    o->inactive_opacity = normalize_d(dval) * OPAQUE;
  // --active_opacity
  if (config_lookup_float(&cfg, "active-opacity", &dval))
    o->active_opacity = normalize_d(dval) * OPAQUE;
  // -e (frame_opacity)
  config_lookup_float(&cfg, "frame-opacity", &o->frame_opacity);
  // -c (shadow_enable)
  if (config_lookup_bool(&cfg, "shadow", &ival) && ival)
    wintype_arr_enable(o->wintype_shadow);
  // -C (no_dock_shadow)
  lcfg_lookup_bool(&cfg, "no-dock-shadow", &pcfgtmp->no_dock_shadow);
  // -G (no_dnd_shadow)
  lcfg_lookup_bool(&cfg, "no-dnd-shadow", &pcfgtmp->no_dnd_shadow);
  // -m (menu_opacity)
  config_lookup_float(&cfg, "menu-opacity", &pcfgtmp->menu_opacity);
  // -f (fading_enable)
  if (config_lookup_bool(&cfg, "fading", &ival) && ival)
    wintype_arr_enable(o->wintype_fade);
  // --no-fading-open-close
  lcfg_lookup_bool(&cfg, "no-fading-openclose", &o->no_fading_openclose);
  // --no-fading-destroyed-argb
  lcfg_lookup_bool(&cfg, "no-fading-destroyed-argb",
      &o->no_fading_destroyed_argb);
  // --shadow-red
  config_lookup_float(&cfg, "shadow-red", &o->shadow_red);
  // --shadow-green
  config_lookup_float(&cfg, "shadow-green", &o->shadow_green);
  // --shadow-blue
  config_lookup_float(&cfg, "shadow-blue", &o->shadow_blue);
  // --shadow-exclude-reg
  if (config_lookup_string(&cfg, "shadow-exclude-reg", &sval))
    o->shadow_exclude_reg_str = strdup(sval);
  // --inactive-opacity-override
  lcfg_lookup_bool(&cfg, "inactive-opacity-override",
      &o->inactive_opacity_override);
  // --inactive-dim
  config_lookup_float(&cfg, "inactive-dim", &o->inactive_dim);
  // --mark-wmwin-focused
  lcfg_lookup_bool(&cfg, "mark-wmwin-focused", &o->mark_wmwin_focused);
  // --mark-ovredir-focused
  lcfg_lookup_bool(&cfg, "mark-ovredir-focused",
      &o->mark_ovredir_focused);
  // --shadow-ignore-shaped
  lcfg_lookup_bool(&cfg, "shadow-ignore-shaped",
      &o->shadow_ignore_shaped);
  // --detect-rounded-corners
  lcfg_lookup_bool(&cfg, "detect-rounded-corners",
      &o->detect_rounded_corners);
  // --xinerama-shadow-crop
  lcfg_lookup_bool(&cfg, "xinerama-shadow-crop",
      &o->xinerama_shadow_crop);
  // --detect-client-opacity
  lcfg_lookup_bool(&cfg, "detect-client-opacity",
      &o->detect_client_opacity);
  // --refresh-rate
  config_lookup_int(&cfg, "refresh-rate", &o->refresh_rate);
  // --vsync
  if (config_lookup_string(&cfg, "vsync", &sval))
    o->vsync = parse_vsync(sval);
  // --backend
  if (config_lookup_string(&cfg, "backend", &sval))
    o->backend = parse_backend(sval);
  // --alpha-step
  config_lookup_float(&cfg, "alpha-step", &o->alpha_step);
  // --sw-opti
  lcfg_lookup_bool(&cfg, "sw-opti", &o->sw_opti);
  // --use-ewmh-active-win
  lcfg_lookup_bool(&cfg, "use-ewmh-active-win",
      &o->use_ewmh_active_win);
  // --unredir-if-possible
  lcfg_lookup_bool(&cfg, "unredir-if-possible",
      &o->unredir_if_possible);
  // --unredir-if-possible-delay
  if (config_lookup_int(&cfg, "unredir-if-possible-delay", &ival))
    o->unredir_if_possible_delay = ival;
  // --inactive-dim-fixed
  lcfg_lookup_bool(&cfg, "inactive-dim-fixed", &o->inactive_dim_fixed);
  // --detect-transient
  lcfg_lookup_bool(&cfg, "detect-transient", &o->detect_transient);
  // --detect-client-leader
  lcfg_lookup_bool(&cfg, "detect-client-leader",
      &o->detect_client_leader);
  // --shadow-exclude
  parse_cfg_condlst(ps, &cfg, &o->shadow_blacklist, "shadow-exclude");
  // --fade-exclude
  parse_cfg_condlst(ps, &cfg, &o->fade_blacklist, "fade-exclude");
  // --focus-exclude
  parse_cfg_condlst(ps, &cfg, &o->focus_blacklist, "focus-exclude");
  // --invert-color-include
  parse_cfg_condlst(ps, &cfg, &o->invert_color_list, "invert-color-include");
  // --blur-background-exclude
  parse_cfg_condlst(ps, &cfg, &o->blur_background_blacklist, "blur-background-exclude");
  // --opacity-rule
  parse_cfg_condlst_opct(ps, &cfg, "opacity-rule");
  // --unredir-if-possible-exclude
  parse_cfg_condlst(ps, &cfg, &o->unredir_if_possible_blacklist, "unredir-if-possible-exclude");
  // --blur-background
  lcfg_lookup_bool(&cfg, "blur-background", &o->blur_background);
  // --blur-background-frame
  lcfg_lookup_bool(&cfg, "blur-background-frame",
      &o->blur_background_frame);
  // --blur-background-fixed
  lcfg_lookup_bool(&cfg, "blur-background-fixed",
      &o->blur_background_fixed);
  // --blur-kern
  if (config_lookup_string(&cfg, "blur-kern", &sval)
      && !parse_conv_kern_lst(ps, sval, o->blur_kerns, MAX_BLUR_PASS))
    exit(1);
  // --resize-damage
  config_lookup_int(&cfg, "resize-damage", &o->resize_damage);
  // --glx-no-stencil
  lcfg_lookup_bool(&cfg, "glx-no-stencil", &o->glx_no_stencil);
  // --glx-no-rebind-pixmap
  lcfg_lookup_bool(&cfg, "glx-no-rebind-pixmap", &o->glx_no_rebind_pixmap);
  // --glx-swap-method
  if (config_lookup_string(&cfg, "glx-swap-method", &sval)
      && !parse_glx_swap_method(ps, sval))
    exit(1);
  // --glx-use-gpushader4
  lcfg_lookup_bool(&cfg, "glx-use-gpushader4", &o->glx_use_gpushader4);

  if (lcfg_lookup_bool(&cfg, "clear-shadow", &bval))
    printf_errf("(): \"clear-shadow\" is removed as an option, and is always"
                " enabled now. Consider removing it from your config file");
  if (lcfg_lookup_bool(&cfg, "paint-on-overlay", &bval))
    printf_errf("(): \"paint-on-overlay\" has been removed as an option, and "
                "is enabled whenever possible");

  const char *deprecation_message = "has been removed. If you encounter problems "
    "without this feature, please feel free to open a bug report.";
  if (lcfg_lookup_bool(&cfg, "glx-use-copysubbuffermesa", &bval) && bval)
    printf_errf("(): \"glx-use-copysubbuffermesa\" %s", deprecation_message);
  if (lcfg_lookup_bool(&cfg, "glx-copy-from-front", &bval) && bval)
    printf_errf("(): \"glx-copy-from-front\" %s", deprecation_message);
  if (lcfg_lookup_bool(&cfg, "xrender-sync", &bval) && bval)
    printf_errf("(): \"xrender-sync\" %s", deprecation_message);
  if (lcfg_lookup_bool(&cfg, "xrender-sync-fence", &bval) && bval)
    printf_errf("(): \"xrender-sync-fence\" %s", deprecation_message);

  // Wintype settings

  for (wintype_t i = 0; i < NUM_WINTYPES; ++i) {
    char *str = mstrjoin("wintypes.", WINTYPES[i]);
    config_setting_t *setting = config_lookup(&cfg, str);
    free(str);
    if (setting) {
      if (config_setting_lookup_bool(setting, "shadow", &ival))
        o->wintype_shadow[i] = (bool) ival;
      if (config_setting_lookup_bool(setting, "fade", &ival))
        o->wintype_fade[i] = (bool) ival;
      if (config_setting_lookup_bool(setting, "focus", &ival))
        o->wintype_focus[i] = (bool) ival;

      double fval;
      if (config_setting_lookup_float(setting, "opacity", &fval))
        o->wintype_opacity[i] = normalize_d(fval);
    }
  }

  config_destroy(&cfg);
}
