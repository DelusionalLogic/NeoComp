#include "windowlist.h"

#include "profiler/zone.h"

DECLARE_ZONE(paint_window);

#include "window.h"

void windowlist_draw(session_t* ps, win* head, float* z) {
    glx_mark(ps, head->id, true);
    (*z) = 1;
    glEnable(GL_DEPTH_TEST);
    for (win *w = head; w; w = w->next_trans) {

        zone_enter(&ZONE_paint_window);
        if(w->state == STATE_DESTROYING || w->state == STATE_HIDING
                || w->state == STATE_ACTIVATING || w->state == STATE_DEACTIVATING
                || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE) {
            win_draw(ps, w, *z);
        }
        zone_leave(&ZONE_paint_window);

        // @HACK: This shouldn't be hardcoded. As it stands, it will probably break
        // for more than 1k windows
        (*z) -= .0001;
    }
    glx_mark(ps, head->id, false);
}

void windowlist_drawoverlap(session_t* ps, win* head, win* overlap, float* z) {
    glx_mark(ps, head->id, true);
    (*z) = 1;
    glEnable(GL_DEPTH_TEST);
    for (win *w = head; w; w = w->next_trans) {
        if(!win_overlap(overlap, w))
            continue;

        if(w->state == STATE_DESTROYING || w->state == STATE_HIDING
                || w->state == STATE_ACTIVATING || w->state == STATE_DEACTIVATING
                || w->state == STATE_ACTIVE || w->state == STATE_INACTIVE) {
            win_draw(ps, w, *z);
        }

        // @HACK: This shouldn't be hardcoded. As it stands, it will probably break
        // for more than 1k windows
        (*z) -= .0001;
    }
    glx_mark(ps, head->id, false);
}
