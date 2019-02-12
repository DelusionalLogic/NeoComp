#include "buffer.h"

#include "logging.h"

#include <assert.h>

int bo_init(struct BufferObject* bo, const size_t size) {
    glGenBuffers(1, &bo->gl);
    if(bo->gl == 0) {
        printf_errf("Failed generating bufferobject");
        return 1;
    }

    glBindBuffer(GL_TEXTURE_BUFFER, bo->gl);
    glBufferData(GL_TEXTURE_BUFFER, size, NULL, GL_STREAM_DRAW);

    return 0;
}

void bo_delete(struct BufferObject* bo) {
    glDeleteBuffers(1, &bo->gl);
    bo->gl = 0;
}

bool bo_initialized(const struct BufferObject* bo) {
    return bo->gl != 0
        && bo->allocated;
}

void bo_update(struct BufferObject* bo, size_t offset, size_t size, void* data) {
    assert(data != NULL);

    glBindBuffer(GL_TEXTURE_BUFFER, bo->gl);
    glBufferSubData(GL_TEXTURE_BUFFER, offset, size, data);
}
