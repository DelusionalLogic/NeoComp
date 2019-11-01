#include "systems/texture.h"

#include "../texture.h"
#include "logging.h"
#include "renderbuffer.h"
#include "window.h"

void texturesystem_tick(Swiss* em) {
    // Resize textures when mapping a window with a texture
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_REDIRECTED, COMPONENT_TEXTURED, CQ_END) {
        struct MapComponent* map = swiss_getComponent(em, COMPONENT_MAP, it.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it.id);

        texture_resize(&textured->texture, &map->size);
        renderbuffer_resize(&textured->stencil, &map->size);
    }

    // Create a texture when mapping windows without one
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_REDIRECTED, CQ_NOT, COMPONENT_TEXTURED, CQ_END) {
        struct MapComponent* map = swiss_getComponent(em, COMPONENT_MAP, it.id);

        struct TexturedComponent* textured = swiss_addComponent(em, COMPONENT_TEXTURED, it.id);

        if(texture_init(&textured->texture, GL_TEXTURE_2D, &map->size) != 0)  {
            printf_errf("Failed initializing window contents texture");
        }

        if(renderbuffer_stencil_init(&textured->stencil, &map->size) != 0)  {
            printf_errf("Failed initializing window contents stencil");
        }
    }

    // We just added a texture, that means we have to refill it
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_BINDS_TEXTURE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_CONTENTS_DAMAGED, it.id);
    }
}
