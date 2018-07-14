#pragma once
/*
 * Compton - a compositor for X11
 *
 * Based on `xcompmgr` - Copyright (c) 2003, Keith Packard
 *
 * Copyright (c) 2011-2013, Christopher Jeffrey
 * See LICENSE for more information.
 *
 */

#include "switch.h"
#include "timeout.h"

#include <X11/Xlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/poll.h>

#include <dbus/dbus.h>

#define CDBUS_SERVICE_NAME      "com.github.chjj.compton"
#define CDBUS_INTERFACE_NAME    CDBUS_SERVICE_NAME
#define CDBUS_OBJECT_NAME       "/com/github/chjj/compton"
#define CDBUS_ERROR_PREFIX      CDBUS_INTERFACE_NAME ".error"
#define CDBUS_ERROR_UNKNOWN     CDBUS_ERROR_PREFIX ".unknown"
#define CDBUS_ERROR_UNKNOWN_S   "Well, I don't know what happened. Do you?"
#define CDBUS_ERROR_BADMSG      CDBUS_ERROR_PREFIX ".bad_message"
#define CDBUS_ERROR_BADMSG_S    "Unrecognized command. Beware compton " \
                                "cannot make you a sandwich."
#define CDBUS_ERROR_BADARG      CDBUS_ERROR_PREFIX ".bad_argument"
#define CDBUS_ERROR_BADARG_S    "Failed to parse argument %d: %s"
#define CDBUS_ERROR_BADWIN      CDBUS_ERROR_PREFIX ".bad_window"
#define CDBUS_ERROR_BADWIN_S    "Requested window %#010x not found."
#define CDBUS_ERROR_BADTGT      CDBUS_ERROR_PREFIX ".bad_target"
#define CDBUS_ERROR_BADTGT_S    "Target \"%s\" not found."
#define CDBUS_ERROR_FORBIDDEN   CDBUS_ERROR_PREFIX ".forbidden"
#define CDBUS_ERROR_FORBIDDEN_S "Incorrect password, access denied."
#define CDBUS_ERROR_CUSTOM      CDBUS_ERROR_PREFIX ".custom"
#define CDBUS_ERROR_CUSTOM_S    "%s"

// Window type
typedef uint32_t cdbus_window_t;
#define CDBUS_TYPE_WINDOW       DBUS_TYPE_UINT32
#define CDBUS_TYPE_WINDOW_STR   DBUS_TYPE_UINT32_AS_STRING

typedef uint16_t cdbus_enum_t;
#define CDBUS_TYPE_ENUM         DBUS_TYPE_UINT16
#define CDBUS_TYPE_ENUM_STR     DBUS_TYPE_UINT16_AS_STRING

struct _session_t;
typedef struct _session_t session_t;

struct _win;
typedef struct _win win;

bool cdbus_init(session_t *ps);
void cdbus_destroy(session_t *ps);
void cdbus_loop(session_t *ps);

void cdbus_ev_win_added(session_t *ps, win *w);
void cdbus_ev_win_destroyed(session_t *ps, win *w);
void cdbus_ev_win_mapped(session_t *ps, win *w);
void cdbus_ev_win_unmapped(session_t *ps, win *w);
void cdbus_ev_win_focusout(session_t *ps, win *w);
void cdbus_ev_win_focusin(session_t *ps, win *w);

void win_set_fade_force(session_t *ps, win *w, switch_t val);
void win_set_focused_force(session_t *ps, win *w, switch_t val);
void win_set_invert_color_force(session_t *ps, win *w, switch_t val);

void opts_init_track_focus(session_t *ps);
void opts_set_no_fading_openclose(session_t *ps, bool newval);
