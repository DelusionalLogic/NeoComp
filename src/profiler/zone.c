#include "zone.h"

#include <stdio.h>
#include <assert.h>

struct ProgramZone* current = NULL;

void zone_enter(struct ProgramZone* zone) {
    if(zone->bound) {
        printf("Zone %s is already started\n", zone->name);
        return;
    }

    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &zone->startTime) != 0) {
        printf("Failed setting starttime for the zone %s\n", zone->name);
        return;
    }

    // Initialize the pointers, since we need to do this for every time we
    // enter
    zone->nextChild = &zone->child;
    zone->child = NULL;
    zone->next = NULL;

    zone->bound = true;

    // We need to bootstrap current if theres nothing in there. We only support
    // a single outer thing, so you can't stop the global zone and then start
    // a new one. AKA ONLY ONE GLOBAL
    if(current == NULL) {
        current = zone;
        return;
    }

    zone->parent = current;

    // If we are not the first. The current zones nextChild ptr will point to
    // where to attach ourselves
    *current->nextChild = zone;

    //Now that we have "used up" the currents nextChild, we need to tell it to
    //use our next ptr as the next child
    current->nextChild = &zone->next;

    current = zone;
}

void zone_leave(struct ProgramZone* zone) {
    if(!zone->parent) {
        printf("Tried to leave the global Zone %s\n", zone->name);
        return;
    }
    assert(zone->parent);

    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &zone->endTime) != 0) {
        printf("Failed setting endtime for the zone %s\n", zone->name);
        return;
    }

    // When we end a zone, the parent of the zone is again the current zone.
    current = zone->parent;
    zone->bound = false;
}

struct ProgramZone* zone_package(struct ProgramZone* zone) {
    zone->bound = false;

    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &zone->endTime) != 0) {
        printf("Failed setting endtime for the zone %s\n", zone->name);
        return NULL;
    }

    struct ProgramZone* now = current;

    current = NULL;
    return now;
}
