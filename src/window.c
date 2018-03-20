#include "window.h"

#include "vmath.h"

bool win_overlap(win* w1, win* w2) {
    const Vector2 w1lpos = {{
        w1->a.x, w1->a.y,
    }};
    const Vector2 w1rpos = {{
        w1->a.x + w1->widthb, w1->a.y + w1->heightb,
    }};
    const Vector2 w2lpos = {{
        w2->a.x, w2->a.y,
    }};
    const Vector2 w2rpos = {{
        w2->a.x + w2->widthb, w2->a.y + w2->heightb,
    }};
    // If one rectangle is on left side of other
    if (w1lpos.x > w2rpos.x || w2lpos.x > w1rpos.x)
        return false;

    // If one rectangle is above other
    if (w1lpos.y > w2rpos.y || w2lpos.y > w1rpos.y)
        return false;

    return true;
}

bool win_covers(win* w) {
    return w->solid
        && (!w->has_frame || !w->frame_opacity)
        && w->fullscreen
        && !w->unredir_if_possible_excluded;
}
