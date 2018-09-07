#define _GNU_SOURCE
#include "render.h"

#include <time.h>
#include <stdio.h>

#include "../assets/assets.h"
#include "../assets/face.h"
#include "../assets/shader.h"
#include "../shaders/shaderinfo.h"
#include "../renderutil.h"

#define NS_PER_MS  1000000L
#define NS_PER_SEC 1000000000L
#define US_PER_SEC 1000000L
#define MS_PER_SEC 1000

struct Block {
    size_t enter_event;
    size_t leave_event;

    struct timespec* start_time;
    struct ProgramZone* zone;
    double millis;
    float start;
    float end;

    char* userdata;
};

#define NUM_TRACKS 5
#define NUM_BLOCKS 1000

struct ProgramZone* root_zone;
double root_millis;

struct Block tracks[NUM_TRACKS][NUM_BLOCKS];
size_t track_cursors[NUM_TRACKS] = {0};

static struct Block* getCurrentBlock(size_t track) {
    assert(track < NUM_TRACKS);
    size_t cursor = track_cursors[track];
    assert(cursor <= NUM_BLOCKS);
    return &tracks[track][cursor-1];
}

static struct Block* getNextBlock(size_t track) {
    assert(track < NUM_TRACKS);
    track_cursors[track]++;
    return getCurrentBlock(track);
}

double timespec_millis(const struct timespec* time) {
    return ((double)time->tv_sec * MS_PER_SEC)
        + ((double)time->tv_nsec / NS_PER_MS);
}

float timespec_ilerp(const struct timespec* start, const struct timespec* end, const struct timespec* value) {
    struct timespec duration;
    timespec_subtract(&duration, end, start);

    struct timespec offset;
    timespec_subtract(&offset, value, start);

    return timespec_millis(&offset) / timespec_millis(&duration);
}

static void process(struct ZoneEventStream* stream) {
    int depth = 0;

    root_zone = stream->rootZone;
    {
        struct timespec duration;
        timespec_subtract(&duration, &stream->end, &stream->start);
        root_millis = timespec_millis(&duration);
    }

    for(size_t i = 0; i < stream->events_num; i++) {
        struct ZoneEvent* cursor = &stream->events[i];
        if(cursor->type == ZE_ENTER) {
            if(depth >= NUM_TRACKS) {
                printf("Internal Error: Too few tracks\n");
                depth++;
                continue;
            }
            struct Block* block = getNextBlock(depth);

            block->enter_event = i;
            block->start_time = &cursor->time;

            float offset = timespec_ilerp(&stream->start, &stream->end, &cursor->time);
            block->start = offset;
            block->zone = cursor->zone;

            block->userdata = cursor->userdata;

            depth++;
        } else if(cursor->type == ZE_LEAVE) {
            assert(depth >= 0);
            if(depth >= NUM_TRACKS) {
                printf("Internal Error: Too few tracks\n");
                depth--;
                continue;
            }
            depth--;
            struct Block* block = getCurrentBlock(depth);

            assert(block->zone == cursor->zone);

            block->leave_event = i;

            struct timespec duration = {0};
            timespec_subtract(&duration, &cursor->time, block->start_time);
            block->millis = timespec_millis(&duration);

            float offset = timespec_ilerp(&stream->start, &stream->end, &cursor->time);
            block->end = offset;
        }
    }
}

static void draw(const Vector2* pos, const Vector2* size) {
    struct face* face = assets_load("window.face");

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
    glDrawBuffers(1, DRAWBUFS);

    struct shader_program* profiler_program = assets_load("profiler.shader");
    if(profiler_program->shader_type_info != &profiler_info) {
        printf("Shader was not a profiler shader\n");
        return;
    }
    struct Profiler* profiler_type = profiler_program->shader_type;
    Vector3 color = {{.45, .2, .3}};
    shader_set_future_uniform_vec3(profiler_type->color, &color);
    shader_use(profiler_program);

    float bar_height = size->y / (NUM_TRACKS+1);

    {
        Vector2 scale = {{size->x, bar_height}};

        Vector2 relpos = {{pos->x, pos->y}};

        {
            Vector3 pos3 = vec3_from_vec2(&relpos, 0.0);
            draw_rect(face, profiler_type->mvp, pos3, scale);
        }
    }

    for(size_t track = 0; track < NUM_TRACKS; track++) {
        for(size_t cursor = 0; cursor < track_cursors[track]; cursor++) {
            struct Block* block = &tracks[track][cursor];
            float width = (block->end - block->start) * size->x;
            Vector2 scale = {{width, bar_height}};

            Vector2 relpos = {{block->start * size->x, bar_height * (track+1) + pos->y}};

            {
                Vector3 pos3 = vec3_from_vec2(&relpos, 0.0);
                draw_rect(face, profiler_type->mvp, pos3, scale);
            }
        }
    }

    glEnable(GL_SCISSOR_TEST);

    Vector2 textscale = {{1, 1}};
    {
        Vector2 pen = {{pos->x, bar_height + pos->y}};
        Vector2 scale = {{0}};

        Vector2 boxScale = {{size->x, bar_height}};
        Vector2 boxPos = {{pos->x, pos->y}};
        glScissor(boxPos.x, boxPos.y, boxScale.x, boxScale.y);

        {
            text_size(&debug_font, root_zone->name, &textscale, &scale);
            pen.y -= scale.y;

            text_draw(&debug_font, root_zone->name, &pen, &textscale);
        }

        {

            char *text;
            asprintf(&text, "%f ms", root_millis);
            text_size(&debug_font, text, &textscale, &scale);
            pen.y -= scale.y;

            text_draw(&debug_font, text, &pen, &textscale);
            free(text);
        }
    }

    for(size_t track = 0; track < NUM_TRACKS; track++) {
        for(size_t cursor = 0; cursor < track_cursors[track]; cursor++) {
            struct Block* block = &tracks[track][cursor];

            Vector2 pen = {{block->start * size->x + pos->x, bar_height * (track+2) + pos->y}};
            Vector2 scale = {{0}};

            float width = (block->end - block->start) * size->x;
            Vector2 boxScale = {{width, bar_height}};
            Vector2 boxPos = {{block->start * size->x, bar_height * (track+1) + pos->y}};
            glScissor(boxPos.x, boxPos.y, boxScale.x, boxScale.y);

            {
                text_size(&debug_font, block->zone->name, &textscale, &scale);
                pen.y -= scale.y;

                text_draw(&debug_font, block->zone->name, &pen, &textscale);
            }

            {
                char *text;
                asprintf(&text, "%f ms", block->millis);
                text_size(&debug_font, text, &textscale, &scale);
                pen.y -= scale.y;

                text_draw(&debug_font, text, &pen, &textscale);
                free(text);
            }

            {
                text_size(&debug_font, block->userdata, &textscale, &scale);
                pen.y -= scale.y;

                text_draw(&debug_font, block->userdata, &pen, &textscale);
            }
        }
    }
    glDisable(GL_SCISSOR_TEST);
}

void profiler_render(struct ZoneEventStream* event_stream) {
    process(event_stream);

    Vector2 diagramPos = {{0, 400}};
    Vector2 diagramSize = {{1920, 680}};
    draw(&diagramPos, &diagramSize);

    for(size_t track = 0; track < NUM_TRACKS; track++) {
        track_cursors[track] = 0;
    }
}
