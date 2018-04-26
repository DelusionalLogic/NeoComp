#include "timer.h"

bool getTime(timestamp* stamp) {
    if(clock_gettime(CLOCK_MONOTONIC, stamp)) {
        return false;
    }
    return true;
}

double timeDiff(timestamp* t1, timestamp* t2) {
    double ms = 0;
    if ((t2->tv_nsec - t1->tv_nsec) < 0) {
        ms += (t2->tv_sec - t1->tv_sec - 1) * 1000;
        ms += (t2->tv_nsec - t1->tv_nsec + 1000000000UL) / 1000000.0;
    } else {
        ms += (t2->tv_sec - t1->tv_sec) * 1000;
        ms += (t2->tv_nsec - t1->tv_nsec) / 1000000.0;
    }

    return ms;
}
