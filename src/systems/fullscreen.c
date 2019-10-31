#include "fullscreen.h"

#include "window.h"

static bool rect_is_fullscreen(Vector2* root_size, Vector2* pos, Vector2* size) {
  return (
      pos->x <= 0 && pos->y <= 0
      && (pos->x + size->x) >= root_size->x 
      && (pos->y + size->y) >= root_size->y
  );
}

void fullscreensystem_determine(Swiss* em, Vector2* root_size) {
    for_components(it, em,
            COMPONENT_MUD, COMPONENT_PHYSICAL, CQ_END) {
        win* w = swiss_getComponent(em, COMPONENT_MUD, it.id);
        struct PhysicalComponent* p = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        w->fullscreen = rect_is_fullscreen(root_size, &p->position, &p->size);
    }
}
