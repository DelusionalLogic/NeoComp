#pragma once

#include <stdbool.h>

struct _timeout_t;

typedef long time_ms_t;

struct _session_t;
typedef struct _session_t session_t;

/// Structure for a recorded timeout.
typedef struct _timeout_t {
  bool enabled;
  void *data;
  bool (*callback)(session_t *ps, struct _timeout_t *ptmout);
  time_ms_t interval;
  time_ms_t firstrun;
  time_ms_t lastrun;
  struct _timeout_t *next;
} timeout_t;

timeout_t *
timeout_insert(session_t *ps, time_ms_t interval,
    bool (*callback)(session_t *ps, timeout_t *ptmout), void *data);

void
timeout_invoke(session_t *ps, timeout_t *ptmout);

bool
timeout_drop(session_t *ps, timeout_t *prm);

void
timeout_reset(session_t *ps, timeout_t *ptmout);

