#include "systems/texture.h"

#include "../texture.h"
#include "logging.h"
#include "renderbuffer.h"
#include "window.h"
#include "profiler/zone.h"

DECLARE_ZONE(texture_tick);

void texturesystem_tick(Swiss* em) {
    zone_scope(&ZONE_texture_tick);
    // Resize textures when mapping a window with a texture
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_PHYSICAL, COMPONENT_REDIRECTED, COMPONENT_TEXTURED, CQ_END) {
        struct PhysicalComponent* phy = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        struct TexturedComponent* textured = swiss_getComponent(em, COMPONENT_TEXTURED, it.id);

        texture_resize(&textured->texture, &phy->size);
        renderbuffer_resize(&textured->stencil, &phy->size);
    }

    // Create a texture when mapping windows without one
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_REDIRECTED, COMPONENT_PHYSICAL, CQ_NOT, COMPONENT_TEXTURED, CQ_END) {
        struct PhysicalComponent* phy = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        struct TexturedComponent* textured = swiss_addComponent(em, COMPONENT_TEXTURED, it.id);

        if(texture_init(&textured->texture, GL_TEXTURE_2D, &phy->size) != 0)  {
            printf_errf("Failed initializing window contents texture");
        }

        if(renderbuffer_stencil_init(&textured->stencil, &phy->size) != 0)  {
            printf_errf("Failed initializing window contents stencil");
        }
    }

    // We just added a texture, that means we have to refill it
    for_components(it, em,
            COMPONENT_MAP, COMPONENT_TEXTURED, COMPONENT_BINDS_TEXTURE, CQ_END) {
        swiss_ensureComponent(em, COMPONENT_CONTENTS_DAMAGED, it.id);
    }
}
