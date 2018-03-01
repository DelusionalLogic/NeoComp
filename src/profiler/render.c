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

static inline int timespec_subtract(struct timespec *result,
        struct timespec *x, struct timespec *y) {
    // Perform the carry for the later subtraction by updating y
    if (x->tv_nsec < y->tv_nsec) {
        long nsec = (y->tv_nsec - x->tv_nsec) / NS_PER_SEC + 1;
        y->tv_nsec -= NS_PER_SEC * nsec;
        y->tv_sec += nsec;
    }

    if (x->tv_nsec - y->tv_nsec > NS_PER_SEC) {
        long nsec = (x->tv_nsec - y->tv_nsec) / NS_PER_SEC;
        y->tv_nsec += NS_PER_SEC * nsec;
        y->tv_sec -= nsec;
    }

    // Compute the time remaining to wait. tv_nsec is certainly positive
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_nsec = x->tv_nsec - y->tv_nsec;

    // Return 1 if result is negative
    return x->tv_sec < y->tv_sec;
}

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

static void process(struct ProgramZone* root, struct Measurement* measurement) {
    struct timespec diff = { 0 };
    timespec_subtract(&diff, &root->endTime, &root->startTime);

    measurement->height = ((float)diff.tv_nsec / NS_PER_MS) / 33.33f;
}

static void draw(size_t head, size_t tail, const Vector2* rootSize) {
    struct Face* face = assets_load("window.face");

    Vector2 pixeluv = {{1.0f, 1.0f}};
    vec2_div(&pixeluv, rootSize);

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

    int steps = 0;
    for(size_t i = head; i != tail; i = getLast(i)) {
        Vector2 scale = {{pixeluv.x * 2, mbuffer[i].height}};
        Vector2 relpos = {{pixeluv.x*steps * 2, 0}};
        draw_rect(face, profiler_type->mvp, relpos, scale);
        steps++;
    }
}

void profiler_render(struct ProgramZone* root, const Vector2* rootSize) {
    size_t next = getNext(head);
    process(root, &mbuffer[next]);
    head = next;

    draw(head, getNext(head), rootSize);
}
