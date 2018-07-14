#include "atoms.h"

#include "session.h"

Atom get_atom(struct _session_t* ps, const char* atom_name) {
  return XInternAtom(ps->dpy, atom_name, False);
}

void atoms_get(struct _session_t* ps, struct Atoms* atoms) {
    ps->atoms.atom_frame_extents = get_atom(ps, "_NET_FRAME_EXTENTS");
    ps->atoms.atom_client = get_atom(ps, "WM_STATE");
    ps->atoms.atom_name = XA_WM_NAME;
    ps->atoms.atom_name_ewmh = get_atom(ps, "_NET_WM_NAME");
    ps->atoms.atom_class = XA_WM_CLASS;
    ps->atoms.atom_role = get_atom(ps, "WM_WINDOW_ROLE");
    ps->atoms.atom_transient = XA_WM_TRANSIENT_FOR;
    ps->atoms.atom_client_leader = get_atom(ps, "WM_CLIENT_LEADER");
    ps->atoms.atom_ewmh_active_win = get_atom(ps, "_NET_ACTIVE_WINDOW");
    ps->atoms.atom_compton_shadow = get_atom(ps, "_COMPTON_SHADOW");
    ps->atoms.atom_bypass = get_atom(ps, "_NET_WM_BYPASS_COMPOSITOR");

    ps->atoms.atom_win_type = get_atom(ps, "_NET_WM_WINDOW_TYPE");
    ps->atoms.atoms_wintypes[WINTYPE_UNKNOWN] = 0;
    ps->atoms.atoms_wintypes[WINTYPE_DESKTOP] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_DESKTOP");
    ps->atoms.atoms_wintypes[WINTYPE_DOCK] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_DOCK");
    ps->atoms.atoms_wintypes[WINTYPE_TOOLBAR] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_TOOLBAR");
    ps->atoms.atoms_wintypes[WINTYPE_MENU] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_MENU");
    ps->atoms.atoms_wintypes[WINTYPE_UTILITY] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_UTILITY");
    ps->atoms.atoms_wintypes[WINTYPE_SPLASH] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_SPLASH");
    ps->atoms.atoms_wintypes[WINTYPE_DIALOG] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_DIALOG");
    ps->atoms.atoms_wintypes[WINTYPE_NORMAL] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_NORMAL");
    ps->atoms.atoms_wintypes[WINTYPE_DROPDOWN_MENU] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU");
    ps->atoms.atoms_wintypes[WINTYPE_POPUP_MENU] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_POPUP_MENU");
    ps->atoms.atoms_wintypes[WINTYPE_TOOLTIP] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_TOOLTIP");
    ps->atoms.atoms_wintypes[WINTYPE_NOTIFY] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_NOTIFICATION");
    ps->atoms.atoms_wintypes[WINTYPE_COMBO] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_COMBO");
    ps->atoms.atoms_wintypes[WINTYPE_DND] = get_atom(ps,
                  "_NET_WM_WINDOW_TYPE_DND");
}
