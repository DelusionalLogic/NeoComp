#pragma once

#include "wintypes.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

struct _session_t;

struct Atoms {
  /// Atom of <code>_NET_FRAME_EXTENTS</code>.
  Atom atom_frame_extents;
  /// Property atom to identify top-level frame window. Currently
  /// <code>WM_STATE</code>.
  Atom atom_client;
  /// Atom of property <code>WM_NAME</code>.
  Atom atom_name;
  /// Atom of property <code>_NET_WM_NAME</code>.
  Atom atom_name_ewmh;
  /// Atom of property <code>WM_CLASS</code>.
  Atom atom_class;
  /// Atom of property <code>WM_WINDOW_ROLE</code>.
  Atom atom_role;
  /// Atom of property <code>WM_TRANSIENT_FOR</code>.
  Atom atom_transient;
  /// Atom of property <code>WM_CLIENT_LEADER</code>.
  Atom atom_client_leader;
  /// Atom of property <code>_NET_ACTIVE_WINDOW</code>.
  Atom atom_ewmh_active_win;
  /// Atom of property <code>_COMPTON_SHADOW</code>.
  Atom atom_compton_shadow;
  /// Atom of property <come>_NET_BYPASS_COMPOSITOR</code>.
  Atom atom_bypass;
  /// Atom of property <code>_NET_WM_WINDOW_TYPE</code>.
  Atom atom_win_type;
  /// Array of atoms of all possible window types.
  Atom atoms_wintypes[NUM_WINTYPES];
};

Atom get_atom(struct _session_t* ps, const char* atom_name);
void atoms_get(struct _session_t* ps, struct Atoms* atoms);
