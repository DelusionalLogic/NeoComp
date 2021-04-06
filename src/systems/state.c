#include "state.h"

#include "window.h"

void statesystem_tick(Swiss* em) {
    // Destroy starts the destruction.
    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_DESTROY, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);
        stateful->state = STATE_DESTROYING;
    }

    // Unmap causes windows to hide
    for_components(it, em,
            COMPONENT_STATEFUL, COMPONENT_UNMAP, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        // Fading out
        // @HACK If we are being destroyed, we don't want to stop doing that
        if(stateful->state != STATE_DESTROYING)
            stateful->state = STATE_HIDING;
    }
}
