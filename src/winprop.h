#pragma once

/// Structure representing Window property value.
typedef struct {
  // All pointers have the same length, right?
  // I wanted to use anonymous union but it's a GNU extension...
  union {
    unsigned char *p8;
    short *p16;
    long *p32;
  } data;
  unsigned long nitems;
  Atom type;
  int format;
} winprop_t;
