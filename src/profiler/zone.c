#include "zone.h"

#include <stdio.h>
#include <assert.h>

struct ZoneEvent event_stream[1024];
size_t event_cursor;

static void zone_event(struct ProgramZone* zone, enum ZoneEventType type, struct ZoneEvent* event) {
    event->zone = zone;
    event->type = type;

    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &event->time) != 0) {
        printf("Failed setting starttime for the zone %s\n", zone->name);
        return;
    }
}

void zone_enter(struct ProgramZone* zone) {
    struct ZoneEvent* event = &event_stream[event_cursor];
    zone_event(zone, ZE_ENTER, event);
    event_cursor++;
}

void zone_leave(struct ProgramZone* zone) {
    struct ZoneEvent* event = &event_stream[event_cursor];
    zone_event(zone, ZE_LEAVE, event);
    event_cursor++;
}

struct ZoneEvent* zone_package(struct ProgramZone* zone) {
    zone_leave(zone);

    struct ZoneEvent* event = &event_stream[event_cursor];
    event->type = ZE_END;
    event_cursor++;

    event_cursor = 0;
    return event_stream;
}
