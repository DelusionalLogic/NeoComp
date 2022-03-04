#include "opacity.h"

#include "profiler/zone.h"

DECLARE_ZONE(calculate_opacity);
DECLARE_ZONE(commit_opacity);

static void start_focus_fade(Swiss* em, double fade_time, double bg_fade_time, double dim_fade_time) {
    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        fade_keyframe(&fo->fade, f->newOpacity, fade_time);
    }
    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_FADES_BGOPACITY, CQ_END) {
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct FadesBgOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_BGOPACITY, it.id);
        fade_keyframe(&fo->fade, f->newOpacity, bg_fade_time);
    }
    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, COMPONENT_FADES_DIM, CQ_END) {
        struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
        struct FadesDimComponent* fo = swiss_getComponent(em, COMPONENT_FADES_DIM, it.id);
        fade_keyframe(&fo->fade, f->newDim, dim_fade_time);
    }

    swiss_removeComponentWhere(em, COMPONENT_TRANSITIONING,
            (enum ComponentType[]){COMPONENT_FOCUS_CHANGE, CQ_END});

    for_components(it, em,
            COMPONENT_FOCUS_CHANGE, CQ_END) {
        struct TransitioningComponent* t = swiss_addComponent(em, COMPONENT_TRANSITIONING, it.id);
        t->time = 0;
        t->duration = fmax(dim_fade_time, fmax(fade_time, bg_fade_time));
    }
}

static void calculate_window_opacity(session_t* ps, Swiss* em) {
    {
        zone_scope(&ZONE_calculate_opacity);
        for_components(it, &ps->win_list,
            COMPONENT_MUD, COMPONENT_FOCUS_CHANGE, COMPONENT_STATEFUL, CQ_END) {
            win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
            struct FocusChangedComponent* f = swiss_getComponent(em, COMPONENT_FOCUS_CHANGE, it.id);
            struct StatefulComponent* stateful = swiss_getComponent(&ps->win_list, COMPONENT_STATEFUL, it.id);

            // Try obeying window type opacity
            if(ps->o.wintype_opacity[w->window_type] != -1.0) {
                f->newOpacity = ps->o.wintype_opacity[w->window_type];
                f->newDim = 100.0;
                continue;
            }

            f->newOpacity = 100.0;
            f->newDim = 100.0;

            // Respect inactive_opacity in some cases
            if (stateful->state == STATE_DEACTIVATING || stateful->state == STATE_INACTIVE) {
                f->newOpacity = ps->o.inactive_opacity;
                f->newDim = ps->o.inactive_dim;
                continue;
            }

            // Respect active_opacity only when the window is physically focused
            if (ps->o.active_opacity && ps->active_win == w) {
                f->newOpacity = ps->o.active_opacity;
                f->newDim = 100.0;
            }
        }
    }

    start_focus_fade(&ps->win_list, ps->o.opacity_fade_time, ps->o.bg_opacity_fade_time, ps->o.dim_fade_time);
}

static void commit_opacity_change(Swiss* em, double fade_time, double bg_fade_time) {
    zone_scope(&ZONE_commit_opacity);
    for_components(it, em,
            COMPONENT_UNMAP, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fadesOpacity = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        fade_keyframe(&fadesOpacity->fade, 0, fade_time);
    }
    for_components(it, em,
            COMPONENT_UNMAP, COMPONENT_FADES_BGOPACITY, CQ_END) {
        struct FadesBgOpacityComponent* fadesOpacity = swiss_getComponent(em, COMPONENT_FADES_BGOPACITY, it.id);

        fade_keyframe(&fadesOpacity->fade, 0, bg_fade_time);
    }

    swiss_removeComponentWhere(em, COMPONENT_TRANSITIONING,
            (enum ComponentType[]){COMPONENT_UNMAP, CQ_END});

    for_components(it, em,
            COMPONENT_UNMAP, CQ_END) {
        struct TransitioningComponent* t = swiss_addComponent(em, COMPONENT_TRANSITIONING, it.id);
        t->time = 0;
        t->duration = fmax(fade_time, bg_fade_time);
    }
}

// @CLEANUP: Stop using the global session every tick
void opacity_tick(Swiss* em, session_t* ps) {
    commit_opacity_change(&ps->win_list, ps->o.opacity_fade_time, ps->o.bg_opacity_fade_time);
    calculate_window_opacity(ps, &ps->win_list);
}

void opacity_afterFade(Swiss* em) {
    zone_scope(&ZONE_commit_opacity);

    //Copy the fade value into the opacity components
    for_components(it, em,
            COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);

        if(fo->fade.value < 100.0) {
            swiss_ensureComponent(em, COMPONENT_OPACITY, it.id);
        } else if(fo->fade.value >= 100.0) {
            swiss_removeComponent(em, COMPONENT_OPACITY, it.id);
            continue;
        }

        struct OpacityComponent* opacity = swiss_getComponent(em, COMPONENT_OPACITY, it.id);
        opacity->opacity = fo->fade.value;
    }

    for_components(it, em,
            COMPONENT_FADES_BGOPACITY, CQ_END) {
        struct FadesBgOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_BGOPACITY, it.id);

        if(fo->fade.value < 100.0) {
            swiss_ensureComponent(em, COMPONENT_BGOPACITY, it.id);
        } else if(fo->fade.value >= 100.0) {
            swiss_removeComponent(em, COMPONENT_BGOPACITY, it.id);
            continue;
        }

        struct BgOpacityComponent* opacity = swiss_getComponent(em, COMPONENT_BGOPACITY, it.id);
        opacity->opacity = fo->fade.value;
    }
}
