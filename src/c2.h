#pragma once
#define _GNU_SOURCE

#include "window.h"
#include "winprop.h"
#include "wintypes.h"

#include <stdio.h>
#include <string.h>
#include <fnmatch.h>
#include <ctype.h>
#include <assert.h>

#include <pcre.h>

struct _session_t;
struct _win;

// For compatiblity with <libpcre-8.20
#ifndef PCRE_STUDY_JIT_COMPILE
#define PCRE_STUDY_JIT_COMPILE    0
#define LPCRE_FREE_STUDY(extra)   pcre_free(extra)
#else
#define LPCRE_FREE_STUDY(extra)   pcre_free_study(extra)
#endif

typedef struct _c2_b c2_b_t;
typedef struct _c2_l c2_l_t;

/// Pointer to a condition tree.
typedef struct {
    bool isbranch : 1;
    union {
        c2_b_t *b;
        c2_l_t *l;
    };
} c2_ptr_t;

/// Initializer for c2_ptr_t.
#define C2_PTR_INIT { \
    .isbranch = false, \
    .l = NULL, \
}

/// Operator of a branch element.
typedef enum {
    C2_B_OUNDEFINED,
    C2_B_OAND,
    C2_B_OOR,
    C2_B_OXOR,
} c2_b_op_t;

/// Structure for branch element in a window condition
struct _c2_b {
    bool neg      : 1;
    c2_b_op_t op;
    c2_ptr_t opr1;
    c2_ptr_t opr2;
};

/// Initializer for c2_b_t.
#define C2_B_INIT { \
    .neg = false, \
    .op = C2_B_OUNDEFINED, \
    .opr1 = C2_PTR_INIT, \
    .opr2 = C2_PTR_INIT, \
}

/// Structure for leaf element in a window condition
struct _c2_l {
    bool neg    : 1;
    enum {
        C2_L_OEXISTS,
        C2_L_OEQ,
        C2_L_OGT,
        C2_L_OGTEQ,
        C2_L_OLT,
        C2_L_OLTEQ,
    } op        : 3;
    enum {
        C2_L_MEXACT,
        C2_L_MSTART,
        C2_L_MCONTAINS,
        C2_L_MWILDCARD,
        C2_L_MPCRE,
    } match     : 3;
    bool match_ignorecase : 1;
    char *tgt;
    Atom tgtatom;
    bool tgt_onframe;
    int index;
    enum {
        C2_L_PUNDEFINED,
        C2_L_PID,
        C2_L_PX,
        C2_L_PY,
        C2_L_PX2,
        C2_L_PY2,
        C2_L_PWIDTH,
        C2_L_PHEIGHT,
        C2_L_PWIDTHB,
        C2_L_PHEIGHTB,
        C2_L_PBDW,
        C2_L_PFULLSCREEN,
        C2_L_POVREDIR,
        C2_L_PFOCUSED,
        C2_L_PWMWIN,
        C2_L_PCLIENT,
        C2_L_PWINDOWTYPE,
        C2_L_PLEADER,
        C2_L_PNAME,
        C2_L_PCLASSG,
        C2_L_PCLASSI,
        C2_L_PROLE,
    } predef;
    enum c2_l_type {
        C2_L_TUNDEFINED,
        C2_L_TSTRING,
        C2_L_TCARDINAL,
        C2_L_TWINDOW,
        C2_L_TATOM,
        C2_L_TDRAWABLE,
    } type;
    int format;
    enum {
        C2_L_PTUNDEFINED,
        C2_L_PTSTRING,
        C2_L_PTINT,
    } ptntype;
    char *ptnstr;
    long ptnint;
    pcre *regex_pcre;
    pcre_extra *regex_pcre_extra;
};

/// Initializer for c2_l_t.
#define C2_L_INIT { \
    .neg = false, \
    .op = C2_L_OEXISTS, \
    .match = C2_L_MEXACT, \
    .match_ignorecase = false, \
    .tgt = NULL, \
    .tgtatom = 0, \
    .tgt_onframe = false, \
    .predef = C2_L_PUNDEFINED, \
    .index = -1, \
    .type = C2_L_TUNDEFINED, \
    .format = 0, \
    .ptntype = C2_L_PTUNDEFINED, \
    .ptnstr = NULL, \
    .ptnint = 0, \
}

const static c2_l_t leaf_def = C2_L_INIT;

/// Linked list type of conditions.
struct _c2_lptr {
    c2_ptr_t ptr;
    void *data;
    struct _c2_lptr *next;
};

typedef struct _c2_lptr c2_lptr_t;

/// Initializer for c2_lptr_t.
#define C2_LPTR_INIT { \
    .ptr = C2_PTR_INIT, \
    .data = NULL, \
    .next = NULL, \
}
