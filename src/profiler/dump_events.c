#include "dump_events.h"

#include "logging.h"
#include "common.h"

#include <time.h>

#define NS_PER_MS  1000000L
#define NS_PER_US  1000L
#define NS_PER_SEC 1000000000L
#define US_PER_SEC 1000000L
#define MS_PER_SEC 1000L

static double timespec_micros(const struct timespec* time) {
    return ((double)time->tv_sec * US_PER_SEC)
        + ((double)time->tv_nsec / NS_PER_US);
}

int profilerWriter_init(struct ProfilerWriterSession* session) {
    session->fd = fopen("events.json", "w");
    if(clock_gettime(CLOCK_MONOTONIC, &session->start) != 0) {
        printf_errf("Failed getting start time for eventwriter");
        return 1;
    }

    fprintf(session->fd, "[\n");

    session->first = false;
    fprintf(session->fd, "{ \"pid\": 0, \"tid\": 0, \"ph\": \"I\", \"ts\": 0.00, \"name\": \"Profiler Start\" }");
    return 0;
}

void profilerWriter_kill(struct ProfilerWriterSession* session) {
    fprintf(session->fd, "]\n");

    fclose(session->fd);
}

void profilerWriter_emitFrame(struct ProfilerWriterSession* session, struct ZoneEventStream* stream) {
    if(!session->first) {
        fprintf(session->fd, ",\n");
    } else {
        session->first = false;
    }

    {
        struct timespec relative;
        timespec_subtract(&relative, &stream->start, &session->start);
        fprintf(session->fd, "{ \"pid\": 0, \"tid\": 0, \"ph\": \"%s\", \"ts\": %f, \"name\": \"%s\" }",
            "B",
            timespec_micros(&relative),
            stream->rootZone->name
        );
    }

    for(size_t i = 0; i < stream->events_num; i++) {
        fprintf(session->fd, ",\n");

        struct ZoneEvent* cursor = &stream->events[i];

        struct timespec relative;
        timespec_subtract(&relative, &cursor->time, &session->start);

        fprintf(
            session->fd,
            "{"
            "\"pid\": 0, "
            "\"tid\": 0, "
            "\"ph\": \"%s\", "
            "\"ts\": %f, "
            "\"name\": \"%s\", "
            "\"args\": { \"location\": \"%s\" }"
            "}",
            cursor->type == ZE_ENTER ? "B" : "E",
            timespec_micros(&relative),
            cursor->zone->name,
            cursor->location
       );
    }

    fprintf(session->fd, ",\n");

    {
        struct timespec relative;
        timespec_subtract(&relative, &stream->end, &session->start);
        fprintf(
            session->fd,
            "{"
            "\"pid\": 0, "
            "\"tid\": 0, "
            "\"ph\": \"%s\", "
            "\"ts\": %f, "
            "\"name\": \"%s\""
            "}",
            "E",
            timespec_micros(&relative),
            stream->rootZone->name
        );
    }
}
