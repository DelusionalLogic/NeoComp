#include "atoms.h"

#include "session.h"

Atom get_atom_internal(Display* display, const char* atom_name) {
  return XInternAtom(display, atom_name, False);
}

void atoms_init(struct Atoms* atoms, Display* display) {
    atoms->atom_client = get_atom_internal(display, "WM_STATE");
    atoms->atom_name = XA_WM_NAME;
    atoms->atom_name_ewmh = get_atom_internal(display, "_NET_WM_NAME");
    atoms->atom_class = XA_WM_CLASS;
    atoms->atom_role = get_atom_internal(display, "WM_WINDOW_ROLE");
    atoms->atom_transient = XA_WM_TRANSIENT_FOR;
    atoms->atom_client_leader = get_atom_internal(display, "WM_CLIENT_LEADER");
    atoms->atom_ewmh_active_win = get_atom_internal(display, "_NET_ACTIVE_WINDOW");
    atoms->atom_compton_shadow = get_atom_internal(display, "_COMPTON_SHADOW");
    atoms->atom_bypass = get_atom_internal(display, "_NET_WM_BYPASS_COMPOSITOR");

    atoms->atom_win_type = get_atom_internal(display, "_NET_WM_WINDOW_TYPE");
    atoms->atoms_wintypes[WINTYPE_UNKNOWN] = 0;
    atoms->atoms_wintypes[WINTYPE_DESKTOP] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_DESKTOP");
    atoms->atoms_wintypes[WINTYPE_DOCK] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_DOCK");
    atoms->atoms_wintypes[WINTYPE_TOOLBAR] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_TOOLBAR");
    atoms->atoms_wintypes[WINTYPE_MENU] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_MENU");
    atoms->atoms_wintypes[WINTYPE_UTILITY] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_UTILITY");
    atoms->atoms_wintypes[WINTYPE_SPLASH] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_SPLASH");
    atoms->atoms_wintypes[WINTYPE_DIALOG] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_DIALOG");
    atoms->atoms_wintypes[WINTYPE_NORMAL] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_NORMAL");
    atoms->atoms_wintypes[WINTYPE_DROPDOWN_MENU] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU");
    atoms->atoms_wintypes[WINTYPE_POPUP_MENU] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_POPUP_MENU");
    atoms->atoms_wintypes[WINTYPE_TOOLTIP] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_TOOLTIP");
    atoms->atoms_wintypes[WINTYPE_NOTIFY] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_NOTIFICATION");
    atoms->atoms_wintypes[WINTYPE_COMBO] = get_atom_internal(display,
               "_NET_WM_WINDOW_TYPE_COMBO");
    atoms->atoms_wintypes[WINTYPE_DND] = get_atom_internal(display,
                  "_NET_WM_WINDOW_TYPE_DND");

    vector_init(&atoms->extra, sizeof(Atom), 4);
}

void atoms_kill(struct Atoms* atoms) {
    vector_kill(&atoms->extra);
}
