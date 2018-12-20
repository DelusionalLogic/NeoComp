#pragma once

#include "vector.h"
#include "wintypes.h"
#include "xorg.h"

#include <X11/Xlib-xcb.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

struct _session_t;

struct Atoms {
  // Property atom to identify top-level frame window. Currently WM_STATE.
  Atom atom_client;
  // Atom of property WM_NAME.
  Atom atom_name;
  // Atom of property _NET_WM_NAME.
  Atom atom_name_ewmh;
  // Atom of property WM_CLASS.
  Atom atom_class;
  // Atom of property WM_WINDOW_ROLE.
  Atom atom_role;
  // Atom of property WM_TRANSIENT_FOR.
  Atom atom_transient;
  // Atom of property WM_CLIENT_LEADER.
  Atom atom_client_leader;
  // Atom of property _NET_ACTIVE_WINDOW.
  Atom atom_ewmh_active_win;
  // Atom of property _COMPTON_SHADOW.
  Atom atom_compton_shadow;
  // Atom of property _NET_BYPASS_COMPOSITOR.
  Atom atom_bypass;
  // Atom of property _NET_WM_WINDOW_TYPE.
  Atom atom_win_type;
  // Array of atoms of all possible window types.
  Atom atoms_wintypes[NUM_WINTYPES];

  Vector extra;
};

Atom get_atom(struct X11Context* context, const char* atom_name);
void atoms_init(struct Atoms* atoms, struct X11Context* context);
void atoms_kill(struct Atoms* atoms);
