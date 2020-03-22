#include "opacity.h"

#include "profiler/zone.h"

DECLARE_ZONE(calculate_opacity);
DECLARE_ZONE(commit_opacity);

void calculate_window_opacity(session_t* ps, Swiss* em) {
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

void opacity_collect_fades(Swiss* em, Vector* fadeable) {
    for_components(it, em,
        COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        struct Fading* fade = &fo->fade;
        vector_putBack(fadeable, &fade);
    }
    for_components(it, em,
        COMPONENT_FADES_BGOPACITY, CQ_END) {
        struct FadesBgOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_BGOPACITY, it.id);
        struct Fading* fade = &fo->fade;
        vector_putBack(fadeable, &fade);
    }
}

void opacity_commit_fades(Swiss* em) {
    for_components(it, em,
            COMPONENT_FADES_OPACITY, CQ_END) {
    struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
    if(fo->fade.value < 100.0) {
            swiss_ensureComponent(em, COMPONENT_OPACITY, it.id);
        }
    }
    for_components(it, em,
            COMPONENT_OPACITY, COMPONENT_FADES_OPACITY, CQ_END) {
        struct FadesOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_OPACITY, it.id);
        struct OpacityComponent* opacity = swiss_getComponent(em, COMPONENT_OPACITY, it.id);

        opacity->opacity = fo->fade.value;
    }
    for_components(it, em,
            COMPONENT_OPACITY, CQ_END) {
        struct OpacityComponent* opacity = swiss_getComponent(em, COMPONENT_OPACITY, it.id);
        if(opacity->opacity >= 100.0) {
            swiss_removeComponent(em, COMPONENT_OPACITY, it.id);
        }
    }

    for_components(it, em,
            COMPONENT_FADES_BGOPACITY, CQ_END) {
        struct FadesBgOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_BGOPACITY, it.id);
        if(fo->fade.value < 100.0) {
            swiss_ensureComponent(em, COMPONENT_BGOPACITY, it.id);
        }
    }
    for_components(it, em,
            COMPONENT_BGOPACITY, COMPONENT_FADES_BGOPACITY, CQ_END) {
        struct FadesBgOpacityComponent* fo = swiss_getComponent(em, COMPONENT_FADES_BGOPACITY, it.id);
        struct BgOpacityComponent* opacity = swiss_getComponent(em, COMPONENT_BGOPACITY, it.id);

        opacity->opacity = fo->fade.value;
    }
    for_components(it, em,
            COMPONENT_BGOPACITY, CQ_END) {
        struct BgOpacityComponent* opacity = swiss_getComponent(em, COMPONENT_BGOPACITY, it.id);
        if(opacity->opacity >= 100.0) {
            swiss_removeComponent(em, COMPONENT_BGOPACITY, it.id);
        }
    }
}

void commit_opacity_change(Swiss* em, double fade_time, double bg_fade_time) {
    zone_enter(&ZONE_commit_opacity);
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
    zone_leave(&ZONE_commit_opacity);
}

