#include "render.h"

#include <time.h>

#include "../assets/assets.h"
#include "../assets/face.h"
#include "../assets/shader.h"
#include "../shaders/shaderinfo.h"
#include "../renderutil.h"

#define NS_PER_MS  1000000L
#define NS_PER_SEC 1000000000L
#define US_PER_SEC 1000000L
#define MS_PER_SEC 1000

struct Measurement {
    float height;
};

struct Measurement mbuffer[256];
size_t head;

static size_t getNext(size_t index) {
    return (index + 1) % 256;
}

static size_t getLast(size_t index) {
    if(index == 0)
        return 255;
    return (index - 1);
}

static void process(struct ZoneEvent* event_stream, struct Measurement* measurement) {
    struct timespec* start = NULL;
    struct timespec* end = NULL;
    int depth = 0;

    assert(event_stream[0].type == ZE_ENTER);
    start = &event_stream[0].time;

    struct ZoneEvent* cursor = &event_stream[1];
    while(cursor->type != ZE_END) {
        if(cursor->type == ZE_ENTER) {
            depth++;
        } else if(cursor->type == ZE_LEAVE) {
            assert(depth >= 0);

            if(depth == 0) {
                // We are leaving the root
                end = &cursor->time;
                break;
            }
            depth--;
        }
        cursor++;
    }

    assert(start != NULL);
    assert(end != NULL);

    struct timespec diff = { 0 };
    timespec_subtract(&diff, end, start);

    measurement->height = ((float)diff.tv_nsec / NS_PER_MS) / 33.33f;
}

static void draw(size_t head, size_t tail, const Vector2* size) {
    struct face* face = assets_load("window.face");

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
    glDrawBuffers(1, DRAWBUFS);

    struct shader_program* profiler_program = assets_load("profiler.shader");
    if(profiler_program->shader_type_info != &profiler_info) {
        printf("Shader was not a profiler shader\n");
        // @INCOMPLETE: Make sure the config is correct
        return;
    }
    struct Global* profiler_type = profiler_program->shader_type;
    shader_use(profiler_program);

    Matrix old_view = view;
    view = mat4_orthogonal(0, 1, 0, 1, -1, 1);
    int steps = 0;
    for(size_t i = head; i != tail; i = getLast(i)) {
        Vector2 scale = {{1.0f/255, mbuffer[i].height}};
        vec2_mul(&scale, size);

        Vector2 relpos = {{scale.x * steps, .2}};

        {
            Vector3 pos = vec3_from_vec2(&relpos, 0.0);
            draw_rect(face, profiler_type->mvp, pos, scale);
        }
        steps++;
    }
    view = old_view;
}

void profiler_render(struct ZoneEvent* event_stream) {
    size_t next = getNext(head);
    process(event_stream, &mbuffer[next]);
    head = next;

    Vector2 diagramSize = {{0.5, 0.5}};
    draw(head, getNext(head), &diagramSize);
}
