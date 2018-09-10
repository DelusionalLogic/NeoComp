/*
 * Compton - a compositor for X11
 *
 * Based on `xcompmgr` - Copyright (c) 2003, Keith Packard
 *
 * Copyright (c) 2011-2013, Christopher Jeffrey
 * See LICENSE for more information.
 *
 */
#define _GNU_SOURCE

#include "c2.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <string.h>

#include "session.h"
#include "atoms.h"
#include "common.h"
#include "logging.h"

#define C2_MAX_LEVELS 10

const static c2_ptr_t C2_PTR_NULL = C2_PTR_INIT;

#define c2_error(format, ...) \
do { \
    printf_err(format, ## __VA_ARGS__); \
} while(0)

/// Structure representing a predefined target.
typedef struct {
    const char *name;
    enum c2_l_type type;
    int format;
} c2_predef_t;

// Predefined targets.
const static c2_predef_t C2_PREDEFS[] = {
    [C2_L_PID         ] = { "id"                , C2_L_TCARDINAL  , 0  },
    [C2_L_PX          ] = { "x"                 , C2_L_TCARDINAL  , 0  },
    [C2_L_PY          ] = { "y"                 , C2_L_TCARDINAL  , 0  },
    [C2_L_PX2         ] = { "x2"                , C2_L_TCARDINAL  , 0  },
    [C2_L_PY2         ] = { "y2"                , C2_L_TCARDINAL  , 0  },
    [C2_L_PWIDTH      ] = { "width"             , C2_L_TCARDINAL  , 0  },
    [C2_L_PHEIGHT     ] = { "height"            , C2_L_TCARDINAL  , 0  },
    [C2_L_PWIDTHB     ] = { "widthb"            , C2_L_TCARDINAL  , 0  },
    [C2_L_PHEIGHTB    ] = { "heightb"           , C2_L_TCARDINAL  , 0  },
    [C2_L_PBDW        ] = { "border_width"      , C2_L_TCARDINAL  , 0  },
    [C2_L_PFULLSCREEN ] = { "fullscreen"        , C2_L_TCARDINAL  , 0  },
    [C2_L_POVREDIR    ] = { "override_redirect" , C2_L_TCARDINAL  , 0  },
    [C2_L_PFOCUSED    ] = { "focused"           , C2_L_TCARDINAL  , 0  },
    [C2_L_PWMWIN      ] = { "wmwin"             , C2_L_TCARDINAL  , 0  },
    [C2_L_PCLIENT     ] = { "client"            , C2_L_TWINDOW    , 0  },
    [C2_L_PWINDOWTYPE ] = { "window_type"       , C2_L_TSTRING    , 0  },
    [C2_L_PNAME       ] = { "name"              , C2_L_TSTRING    , 0  },
    [C2_L_PCLASSG     ] = { "class_g"           , C2_L_TSTRING    , 0  },
    [C2_L_PCLASSI     ] = { "class_i"           , C2_L_TSTRING    , 0  },
    [C2_L_PROLE       ] = { "role"              , C2_L_TSTRING    , 0  },
};

static int strcmp_wd(const char *needle, const char *src) {
    int ret = strcmp(needle, src);
    if (ret)
        return ret;

    char c = src[strlen(needle)];
    if (isalnum(c) || '_' == c) {
        return 1;
    } else {
        return 0;
    }
}

static bool c2_ptr_isempty(const c2_ptr_t p) {
    return !(p.isbranch ? (bool) p.b: (bool) p.l);
}

static void c2_ptr_reset(c2_ptr_t *pp) {
    if (pp) {
        memcpy(pp, &C2_PTR_NULL, sizeof(c2_ptr_t));
    }
}

static c2_ptr_t c2h_comb_tree(c2_b_op_t op, c2_ptr_t p1, c2_ptr_t p2) {
    c2_ptr_t p = {
        .isbranch = true,
        .b = malloc(sizeof(c2_b_t))
    };

    p.b->opr1 = p1;
    p.b->opr2 = p2;
    p.b->op = op;

    return p;
}

// Get the precedence value of a condition branch operator.
static int c2h_b_opp(c2_b_op_t op) {
    switch (op) {
        case C2_B_OAND:
            return 2;
        case C2_B_OOR:
            return 1;
        case C2_B_OXOR:
            return 1;
        case C2_B_OUNDEFINED:
            printf("Internal error: Tried to get precedence for UNDEFINED\n");
            assert(0);
            return 0;
    }
    return 0;
}

// Compare precedence of two condition branch operators.
//
// Associativity is left-to-right, forever.
//
// return positive number if op1 > op2, 0 if op1 == op2 in precedence,
// negative number otherwise
static int c2h_b_opcmp(c2_b_op_t op1, c2_b_op_t op2) {
    return c2h_b_opp(op1) - c2h_b_opp(op2);
}

static void c2_free(c2_ptr_t p);

static int c2_parse_grp(struct _session_t *ps, const char *pattern, int offset, c2_ptr_t *presult, int level);
static int c2_parse_target(struct _session_t *ps, const char *pattern, int offset, c2_ptr_t *presult);

static bool c2_l_postprocess(struct _session_t *ps, c2_l_t *pleaf);

static void c2_freep(c2_ptr_t *pp) {
    if (pp) {
        c2_free(*pp);
        c2_ptr_reset(pp);
    }
}

static void c2_dump_raw(c2_ptr_t p);

static void c2_dump(c2_ptr_t p) {
    c2_dump_raw(p);
    printf("\n");
    fflush(stdout);
}

static int c2_parse_legacy(session_t *ps, const char *pattern, int offset, c2_ptr_t *presult) {
    unsigned plen = strlen(pattern + offset);

    if (plen < 4 || ':' != pattern[offset + 1]
            || !strchr(pattern + offset + 2, ':')) {
        c2_error("pattern %s, pos %d: Legacy parser: Invalid format.", pattern, offset);
        return -1;
    }

    // Allocate memory for new leaf
    c2_l_t *pleaf = malloc(sizeof(c2_l_t));
    if (!pleaf)
        printf_errfq(1, "(): Failed to allocate memory for new leaf.");
    presult->isbranch = false;
    presult->l = pleaf;
    memcpy(pleaf, &leaf_def, sizeof(c2_l_t));
    pleaf->type = C2_L_TSTRING;
    pleaf->op = C2_L_OEQ;
    pleaf->ptntype = C2_L_PTSTRING;

    // Determine the pattern target
    switch (pattern[offset]) {
        case 'n':
            pleaf->predef = C2_L_PNAME;
            pleaf->type = C2_PREDEFS[C2_L_PNAME].type;
            pleaf->format = C2_PREDEFS[C2_L_PNAME].format;
            break;
        case 'i':
            pleaf->predef = C2_L_PCLASSI;
            pleaf->type = C2_PREDEFS[C2_L_PCLASSI].type;
            pleaf->format = C2_PREDEFS[C2_L_PCLASSI].format;
            break;
        case 'g':
            pleaf->predef = C2_L_PCLASSG;
            pleaf->type = C2_PREDEFS[C2_L_PCLASSG].type;
            pleaf->format = C2_PREDEFS[C2_L_PCLASSG].format;
            break;
        case 'r':
            pleaf->predef = C2_L_PROLE;
            pleaf->type = C2_PREDEFS[C2_L_PROLE].type;
            pleaf->format = C2_PREDEFS[C2_L_PROLE].format;
            break;
        default:
            c2_error("pattern %s, pos %d: Target \"%c\" invalid.\n", pattern, offset, pattern[offset]);
            return -1;
    }

    offset += 2;

    // Determine the match type
    switch (pattern[offset]) {
        case 'e':
            pleaf->match = C2_L_MEXACT;
            break;
        case 'a':
            pleaf->match = C2_L_MCONTAINS;
            break;
        case 's':
            pleaf->match = C2_L_MSTART;
            break;
        case 'w':
            pleaf->match = C2_L_MWILDCARD;
            break;
        case 'p':
            pleaf->match = C2_L_MPCRE;
            break;
        default:
            c2_error("pattern %s, pos %d: Type \"%c\" invalid.\n", pattern, offset, pattern[offset]);
            return -1;
    }
    ++offset;

    // Determine the pattern flags
    while (':' != pattern[offset]) {
        switch (pattern[offset]) {
            case 'i':
                pleaf->match_ignorecase = true;
                break;
            default:
                c2_error("pattern %s, pos %d: Flag \"%c\" invalid.", pattern, offset, pattern[offset]);
                return -1;
        }
        ++offset;
    }
    ++offset;

    // Copy the pattern
    pleaf->ptnstr = mstrcpy(pattern + offset);

    if (!c2_l_postprocess(ps, pleaf))
        return -1;

    return offset;
}


/**
 * Parse a condition string.
 */
c2_lptr_t * c2_parsed(session_t *ps, c2_lptr_t **pcondlst, const char *pattern,
        void *data) {
    if (!pattern)
        return NULL;

    // Parse the pattern
    c2_ptr_t result = C2_PTR_INIT;
    int offset = -1;

    if (strlen(pattern) >= 2 && ':' == pattern[1]) {
        offset = c2_parse_legacy(ps, pattern, 0, &result);
    } else {
        offset = c2_parse_grp(ps, pattern, 0, &result, 0);
    }

    if (offset < 0) {
        c2_freep(&result);
        return NULL;
    }

    // Insert to pcondlst
    {
        const static c2_lptr_t lptr_def = C2_LPTR_INIT;
        c2_lptr_t *plptr = malloc(sizeof(c2_lptr_t));
        if (!plptr) {
            printf_errfq(1, "(): Failed to allocate memory for new condition linked"
                    " list element.");
        }
        memcpy(plptr, &lptr_def, sizeof(c2_lptr_t));
        plptr->ptr = result;
        plptr->data = data;
        if (pcondlst) {
            plptr->next = *pcondlst;
            *pcondlst = plptr;
        }

#ifdef DEBUG_C2
        printf_dbgf("(\"%s\"): ", pattern);
        c2_dump(plptr->ptr);
#endif

        return plptr;
    }
}

#define C2H_SKIP_SPACES() { while (isspace(pattern[offset])) ++offset; }

static int c2_parse_op(const char *pattern, int offset, c2_ptr_t *presult) {
    c2_l_t * const pleaf = presult->l;

    // Parse negation marks
    C2H_SKIP_SPACES();
    while ('!' == pattern[offset]) {
        pleaf->neg = !pleaf->neg;
        ++offset;
        C2H_SKIP_SPACES();
    }

    // Parse qualifiers
    if ('*' == pattern[offset] || '^' == pattern[offset]
            || '%' == pattern[offset] || '~' == pattern[offset]) {
        switch (pattern[offset]) {
            case '*':
                pleaf->match = C2_L_MCONTAINS;
                break;
            case '^':
                pleaf->match = C2_L_MSTART;
                break;
            case '%':
                pleaf->match = C2_L_MWILDCARD;
                break;
            case '~':
                pleaf->match = C2_L_MPCRE;
                break;
            default:
                assert(0);
        }
        ++offset;
        C2H_SKIP_SPACES();
    }

    // Parse flags
    while ('?' == pattern[offset]) {
        pleaf->match_ignorecase = true;
        ++offset;
        C2H_SKIP_SPACES();
    }

    // Parse operator
    while ('=' == pattern[offset] || '>' == pattern[offset] || '<' == pattern[offset]) {
        if ('=' == pattern[offset] && C2_L_OGT == pleaf->op) {
            pleaf->op = C2_L_OGTEQ;
        } else if ('=' == pattern[offset] && C2_L_OLT == pleaf->op) {
            pleaf->op = C2_L_OLTEQ;
        } else if (pleaf->op) {
            c2_error("pattern %s, pos %d: Duplicate operator.", pattern, offset);
            return -1;
        } else {
            switch (pattern[offset]) {
                case '=': pleaf->op = C2_L_OEQ; break;
                case '>': pleaf->op = C2_L_OGT; break;
                case '<': pleaf->op = C2_L_OLT; break;
                default:  assert(0);
            }
        }
        ++offset;
        C2H_SKIP_SPACES();
    }

    // Check for problems
    if (C2_L_OEQ != pleaf->op && (pleaf->match || pleaf->match_ignorecase)) {
        c2_error("pattern %s, pos %d: Exists/greater-than/less-than operators cannot have a qualifier.", pattern, offset);
        return -1;
    }

    return offset;
}

static int c2_parse_pattern(session_t *ps, const char *pattern, int offset, c2_ptr_t *presult) {
    c2_l_t * const pleaf = presult->l;

    // Exists operator cannot have pattern
    if (!pleaf->op)
        return offset;

    C2H_SKIP_SPACES();

    char *endptr = NULL;
    if (!strcmp_wd("true", &pattern[offset])) { // Check for boolean patterns
        pleaf->ptntype = C2_L_PTINT;
        pleaf->ptnint = true;
        offset += strlen("true");
    } else if (!strcmp_wd("false", &pattern[offset])) {
        pleaf->ptntype = C2_L_PTINT;
        pleaf->ptnint = false;
        offset += strlen("false");
    } else if (pleaf->ptnint = strtol(pattern + offset, &endptr, 0), pattern + offset != endptr) { // Check for integer patterns
        pleaf->ptntype = C2_L_PTINT;
        offset = endptr - pattern;
        // Make sure we are stopping at the end of a word
        if (isalnum(pattern[offset])) {
            c2_error("pattern %s, pos %d: Trailing characters after a numeric pattern.", pattern, offset);
            return -1;
        }
    } else { // Check for string patterns
        bool raw = false;
        char delim = '\0';

        // String flags
        if ('r' == tolower(pattern[offset])) {
            raw = true;
            ++offset;
            C2H_SKIP_SPACES();
        }

        // Check for delimiters
        if ('\"' == pattern[offset] || '\'' == pattern[offset]) {
            pleaf->ptntype = C2_L_PTSTRING;
            delim = pattern[offset];
            ++offset;
        }

        if (C2_L_PTSTRING != pleaf->ptntype) {
            c2_error("pattern %s, pos %d: Invalid pattern type.", pattern, offset);
            return -1;
        }

        // Parse the string now
        // We can't determine the length of the pattern, so we use the length
        // to the end of the pattern string -- currently escape sequences
        // cannot be converted to a string longer than itself.
        char *tptnstr = malloc((strlen(pattern + offset) + 1) * sizeof(char));
        char *ptptnstr = tptnstr;
        pleaf->ptnstr = tptnstr;
        for (; pattern[offset] && delim != pattern[offset]; ++offset) {
            // Handle escape sequences if it's not a raw string
            if ('\\' == pattern[offset] && !raw) {
                switch(pattern[++offset]) {
                    case '\\':
                        *(ptptnstr++) = '\\';
                        break;
                    case '\'':
                        *(ptptnstr++) = '\'';
                        break;
                    case '\"':
                        *(ptptnstr++) = '\"';
                        break;
                    case 'a':
                        *(ptptnstr++) = '\a';
                        break;
                    case 'b':
                        *(ptptnstr++) = '\b';
                        break;
                    case 'f':
                        *(ptptnstr++) = '\f';
                        break;
                    case 'n':
                        *(ptptnstr++) = '\n';
                        break;
                    case 'r':
                        *(ptptnstr++) = '\r';
                        break;
                    case 't':
                        *(ptptnstr++) = '\t';
                        break;
                    case 'v':
                        *(ptptnstr++) = '\v';
                        break;
                    case 'o':
                    case 'x':
                        {
                            char *tstr = mstrncpy(pattern + offset + 1, 2);
                            char *pstr = NULL;
                            long val = strtol(tstr, &pstr,
                                    ('o' == pattern[offset] ? 8: 16));
                            free(tstr);
                            if (pstr != &tstr[2] || val <= 0) {
                                c2_error("pattern %s, pos %d: Invalid octal/hex escape sequence.", pattern, offset);
                                return -1;
                            }
                            assert(val < 256 && val >= 0);
                            *(ptptnstr++) = val;
                            offset += 2;
                            break;
                        }
                    default:
                        c2_error("pattern %s, pos %d: Invalid escape sequence.", pattern, offset);
                        return -1;
                }
            } else {
                *(ptptnstr++) = pattern[offset];
            }
        }

        if (!pattern[offset]) {
            c2_error("pattern %s, pos %d: Premature end of pattern string.", pattern, offset);
            return -1;
        }
        ++offset;
        *ptptnstr = '\0';
        pleaf->ptnstr = mstrcpy(tptnstr);
        free(tptnstr);
    }

    C2H_SKIP_SPACES();

    if (!pleaf->ptntype) {
        c2_error("pattern %s, pos %d: Invalid pattern type.", pattern, offset);
        return -1;
    }

    // Check if the type is correct
    if (!(((C2_L_TSTRING == pleaf->type
                        || C2_L_TATOM == pleaf->type)
                    && C2_L_PTSTRING == pleaf->ptntype)
                || ((C2_L_TCARDINAL == pleaf->type
                        || C2_L_TWINDOW == pleaf->type
                        || C2_L_TDRAWABLE == pleaf->type)
                    && C2_L_PTINT == pleaf->ptntype))) {
        c2_error("pattern %s, pos %d: Pattern type incompatible with target type.", pattern, offset);
        return -1;
    }

    if (C2_L_PTINT == pleaf->ptntype && pleaf->match) {
        c2_error("pattern %s, pos %d: Integer/boolean pattern cannot have operator qualifiers.", pattern, offset);
        return -1;
    }

    if (C2_L_PTINT == pleaf->ptntype && pleaf->match_ignorecase) {
        c2_error("pattern %s, pos %d: Integer/boolean pattern cannot have flags.", pattern, offset);
        return -1;
    }

    if (C2_L_PTSTRING == pleaf->ptntype
            && (C2_L_OGT == pleaf->op || C2_L_OGTEQ == pleaf->op
                || C2_L_OLT == pleaf->op || C2_L_OLTEQ == pleaf->op)) {
        c2_error("pattern %s, pos %d: String pattern cannot have an arithmetic operator.", pattern, offset);
        return -1;
    }

    return offset;
}

/**
 * Parse a group in condition string.
 *
 * @return offset of next character in string
 */
static int
c2_parse_grp(session_t *ps, const char *pattern, int offset, c2_ptr_t *presult, int level) {
    // Check for recursion levels
    if (level > C2_MAX_LEVELS) {
        c2_error("pattern %s, pos %d: Exceeded maximum recursion levels.", pattern, offset);
        return -1;
    }

    if (!pattern)
        return -1;

    // Expected end character
    const char endchar = (offset ? ')': '\0');

    // We use a system that a maximum of 2 elements are kept. When we find
    // the third element, we combine the elements according to operator
    // precedence. This design limits operators to have at most two-levels
    // of precedence and fixed left-to-right associativity.

    // For storing branch operators. ops[0] is actually unused
    c2_b_op_t ops[3] = { };
    // For storing elements
    c2_ptr_t eles[2] = { C2_PTR_INIT, C2_PTR_INIT };
    // Index of next free element slot in eles
    int elei = 0;
    // Pointer to the position of next element
    c2_ptr_t *pele = eles;
    // Negation flag of next operator
    bool neg = false;
    // Whether we are expecting an element immediately, is true at first, or
    // after encountering a logical operator
    bool next_expected = true;

    // Parse the pattern character-by-character
    for (; pattern[offset]; ++offset) {
        assert(elei <= 2);

        // Jump over spaces
        if (isspace(pattern[offset]))
            continue;

        // Handle end of group
        if (')' == pattern[offset])
            break;

        // Handle "!"
        if ('!' == pattern[offset]) {
            if (!next_expected) {
                c2_error("pattern %s, pos %d: Unexpected \"!\".", pattern, offset);
                goto c2_parse_grp_fail;
            }

            neg = !neg;
            continue;
        }

        // Handle AND and OR
        if ('&' == pattern[offset] || '|' == pattern[offset]) {
            if (next_expected) {
                c2_error("pattern %s, pos %d: Unexpected logical operator.", pattern, offset);
                goto c2_parse_grp_fail;
            }

            next_expected = true;
            if (!strcmp("&&", pattern + offset)) {
                ops[elei] = C2_B_OAND;
                ++offset;
            } else if (!strcmp("||", pattern + offset)) {
                ops[elei] = C2_B_OOR;
                ++offset;
            } else {
                c2_error("pattern %s, pos %d: Illegal logical operator.", pattern, offset);
                goto c2_parse_grp_fail;
            }

            continue;
        }

        // Parsing an element
        if (!next_expected) {
            c2_error("pattern %s, pos %d: Unexpected expression.", pattern, offset);
            goto c2_parse_grp_fail;
        }

        assert(!elei || ops[elei]);

        // If we are out of space
        if (2 == elei) {
            --elei;
            // If the first operator has higher or equal precedence, combine
            // the first two elements
            if (c2h_b_opcmp(ops[1], ops[2]) >= 0) {
                eles[0] = c2h_comb_tree(ops[1], eles[0], eles[1]);
                c2_ptr_reset(&eles[1]);
                pele = &eles[elei];
                ops[1] = ops[2];
            }
            // Otherwise, combine the second and the incoming one
            else {
                eles[1] = c2h_comb_tree(ops[2], eles[1], C2_PTR_NULL);
                assert(eles[1].isbranch);
                pele = &eles[1].b->opr2;
            }
            // The last operator always needs to be reset
            ops[2] = C2_B_OUNDEFINED;
        }

        // It's a subgroup if it starts with '('
        if ('(' == pattern[offset]) {
            if ((offset = c2_parse_grp(ps, pattern, offset + 1, pele, level + 1)) < 0)
                goto c2_parse_grp_fail;
        }
        // Otherwise it's a leaf
        else {
            if ((offset = c2_parse_target(ps, pattern, offset, pele)) < 0)
                goto c2_parse_grp_fail;

            assert(!pele->isbranch && !c2_ptr_isempty(*pele));

            if ((offset = c2_parse_op(pattern, offset, pele)) < 0)
                goto c2_parse_grp_fail;

            if ((offset = c2_parse_pattern(ps, pattern, offset, pele)) < 0)
                goto c2_parse_grp_fail;

            if (!c2_l_postprocess(ps, pele->l))
                goto c2_parse_grp_fail;
        }
        // Decrement offset -- we will increment it in loop update
        --offset;

        // Apply negation
        if (neg) {
            neg = false;
            if (pele->isbranch)
                pele->b->neg = !pele->b->neg;
            else
                pele->l->neg = !pele->l->neg;
        }

        next_expected = false;
        ++elei;
        pele = &eles[elei];
    }

    // Wrong end character?
    if (pattern[offset] && !endchar) {
        c2_error("pattern %s, pos %d: Expected end of string but found '%c'.", pattern, offset, pattern[offset]);
        goto c2_parse_grp_fail;
    }
    if (!pattern[offset] && endchar) {
        c2_error("pattern %s, pos %d: Expected '%c' but found end of string.", pattern, offset, endchar);
        goto c2_parse_grp_fail;
    }

        // Handle end of group
        if (!elei) {
            c2_error("pattern %s, pos %d: Empty group.", pattern, offset);
            goto c2_parse_grp_fail;
        }
    else if (next_expected) {
        c2_error("pattern %s, pos %d: Missing rule before end of group.", pattern, offset);
        goto c2_parse_grp_fail;
    }
    else if (elei > 1) {
        assert(2 == elei);
        assert(ops[1]);
        eles[0] = c2h_comb_tree(ops[1], eles[0], eles[1]);
        c2_ptr_reset(&eles[1]);
    }

    *presult = eles[0];

    if (')' == pattern[offset])
    ++offset;

    return offset;

    c2_parse_grp_fail:
    c2_freep(&eles[0]);
    c2_freep(&eles[1]);

    return -1;
}

    /**
     * Parse the target part of a rule.
     */
    static int c2_parse_target(session_t *ps, const char *pattern, int offset, c2_ptr_t *presult) {
        // Initialize leaf
        presult->isbranch = false;
        presult->l = malloc(sizeof(c2_l_t));
        if (!presult->l) {
            c2_error("pattern %s, pos %d: Failed to allocate memory for new leaf.", pattern, offset);
            return -1;
        }

        c2_l_t * const pleaf = presult->l;
        memcpy(pleaf, &leaf_def, sizeof(c2_l_t));

        // Parse negation marks
        while ('!' == pattern[offset]) {
            pleaf->neg = !pleaf->neg;
            ++offset;
            C2H_SKIP_SPACES();
        }

        // Copy target name out
        unsigned tgtlen = 0;
        for (; pattern[offset]
                && (isalnum(pattern[offset]) || '_' == pattern[offset]); ++offset) {
            ++tgtlen;
        }
        if (!tgtlen) {
            c2_error("pattern %s, pos %d: Empty target.", pattern, offset);
            return -1;
        }
        pleaf->tgt = mstrncpy(&pattern[offset - tgtlen], tgtlen);

        // Check for predefined targets
        for (unsigned i = 1; i < sizeof(C2_PREDEFS) / sizeof(C2_PREDEFS[0]); ++i) {
            if (!strcmp(C2_PREDEFS[i].name, pleaf->tgt)) {
                pleaf->predef = i;
                pleaf->type = C2_PREDEFS[i].type;
                pleaf->format = C2_PREDEFS[i].format;
                break;
            }
        }

        // Alias for predefined targets
        if (!pleaf->predef) {
#define TGTFILL(pdefid) \
            (pleaf->predef = pdefid, \
             pleaf->type = C2_PREDEFS[pdefid].type, \
             pleaf->format = C2_PREDEFS[pdefid].format)

            // if (!strcmp("WM_NAME", tgt) || !strcmp("_NET_WM_NAME", tgt))
            //   TGTFILL(C2_L_PNAME);
#undef TGTFILL

            // Alias for custom properties
#define TGTFILL(target, type, format) \
            (pleaf->target = mstrcpy(target), \
             pleaf->type = type, \
             pleaf->format = format)

            // if (!strcmp("SOME_ALIAS"))
            //   TGTFILL("ALIAS_TEXT", C2_L_TSTRING, 32);
#undef TGTFILL
        }

        C2H_SKIP_SPACES();

        // Parse target-on-frame flag
        if ('@' == pattern[offset]) {
            pleaf->tgt_onframe = true;
            ++offset;
            C2H_SKIP_SPACES();
        }

        // Parse index
        if ('[' == pattern[offset]) {
            offset++;

            C2H_SKIP_SPACES();

            int index = -1;
            char *endptr = NULL;

            index = strtol(pattern + offset, &endptr, 0);

            if (!endptr || pattern + offset == endptr) {
                c2_error("pattern %s, pos %d: No index number found after bracket.", pattern, offset);
                return -1;
            }

            if (index < 0) {
                c2_error("pattern %s, pos %d: Index number invalid.", pattern, offset);
                return -1;
            }

            if (pleaf->predef) {
                c2_error("pattern %s, pos %d: Predefined targets can't have index.", pattern, offset);
                return -1;
            }

            pleaf->index = index;
            offset = endptr - pattern;

            C2H_SKIP_SPACES();

            if (']' != pattern[offset]) {
                c2_error("pattern %s, pos %d: Index end marker not found.", pattern, offset);
                return -1;
            }

            ++offset;

            C2H_SKIP_SPACES();
        }

        // Parse target type and format
        if (':' == pattern[offset]) {
            ++offset;
            C2H_SKIP_SPACES();

            // Look for format
            bool hasformat = false;
            int format = 0;
            {
                char *endptr = NULL;
                format =  strtol(pattern + offset, &endptr, 0);
                assert(endptr);
                if ((hasformat = (endptr && endptr != pattern + offset)))
                    offset = endptr - pattern;
                C2H_SKIP_SPACES();
            }

            // Look for type
            enum c2_l_type type = C2_L_TUNDEFINED;
            {
                switch (pattern[offset]) {
                    case 'w':
                        type = C2_L_TWINDOW;
                        break;
                    case 'd':
                        type = C2_L_TDRAWABLE;
                        break;
                    case 'c':
                        type = C2_L_TCARDINAL;
                        break;
                    case 's':
                        type = C2_L_TSTRING;
                        break;
                    case 'a':
                        type = C2_L_TATOM;
                        break;
                    default: 
                        c2_error("pattern %s, pos %d: Invalid type character.", pattern, offset);
                        return -1;
                }

                if (type) {
                    if (pleaf->predef) {
                        printf_errf("(): Warning: Type specified for a default target will be ignored.");
                    }
                    else {
                        if (pleaf->type && type != pleaf->type)
                            printf_errf("(): Warning: Default type overridden on target.");
                        pleaf->type = type;
                    }
                }

                offset++;
                C2H_SKIP_SPACES();
            }

            // Default format
            if (!pleaf->format) {
                switch (pleaf->type) {
                    case C2_L_TWINDOW:
                    case C2_L_TDRAWABLE:
                    case C2_L_TATOM:
                        pleaf->format = 32;  break;
                    case C2_L_TSTRING:
                        pleaf->format = 8;   break;
                    default:
                        break;
                }
            }

            // Write format
            if (hasformat) {
                if (pleaf->predef)
                    printf_errf("(): Warning: Format \"%d\" specified on a default target will be ignored.", format);
                else if (C2_L_TSTRING == pleaf->type)
                    printf_errf("(): Warning: Format \"%d\" specified on a string target will be ignored.", format);
                else {
                    if (pleaf->format && pleaf->format != format)
                        printf_err("Warning: Default format %d overridden on target.",
                                pleaf->format);
                    pleaf->format = format;
                }
            }
        }

        if (!pleaf->type) {
            c2_error("pattern %s, pos %d: Target type cannot be determined.", pattern, offset);
            return -1;
        }

        // if (!pleaf->predef && !pleaf->format && C2_L_TSTRING != pleaf->type)
        //   c2_error("pattern %s, pos %d: Target format cannot be determined.", pattern, offset);

        if (pleaf->format && 8 != pleaf->format
                && 16 != pleaf->format && 32 != pleaf->format) {
            c2_error("pattern %s, pos %d: Invalid format.", pattern, offset);
            return -1;
        }

        return offset;
    }

/**
 * Do postprocessing on a condition leaf.
 */
static bool
c2_l_postprocess(session_t *ps, c2_l_t *pleaf) {
    // Give a pattern type to a leaf with exists operator, if needed
    if (C2_L_OEXISTS == pleaf->op && !pleaf->ptntype) {
        pleaf->ptntype =
            (C2_L_TSTRING == pleaf->type ? C2_L_PTSTRING: C2_L_PTINT);
    }

    // Get target atom if it's not a predefined one
    if (!pleaf->predef) {
        pleaf->tgtatom = get_atom(ps, pleaf->tgt);
        if (!pleaf->tgtatom) {
            c2_error("Failed to get atom for target \"%s\".", pleaf->tgt);
            return false;
        }
    }

    // Insert target Atom into atom track list
    if (pleaf->tgtatom) {
        bool found = false;
        for (latom_t *platom = ps->track_atom_lst; platom;
                platom = platom->next) {
            if (pleaf->tgtatom == platom->atom) {
                found = true;
                break;
            }
        }
        if (!found) {
            latom_t *pnew = malloc(sizeof(latom_t));
            if (!pnew)
                printf_errfq(1, "(): Failed to allocate memory for new track atom.");
            pnew->next = ps->track_atom_lst;
            pnew->atom = pleaf->tgtatom;
            ps->track_atom_lst = pnew;
        }
    }

    // Enable specific tracking options in compton if needed by the condition
    // TODO: Add track_leader
    if (pleaf->predef) {
        switch (pleaf->predef) {
            case C2_L_PFOCUSED: ps->o.track_focus = true; break;
            case C2_L_PNAME:
            case C2_L_PCLASSG:
            case C2_L_PCLASSI:
            case C2_L_PROLE:    ps->o.track_wdata = true; break;
            default:            break;
        }
    }

    // Warn about lower case characters in target name
    if (!pleaf->predef) {
        for (const char *pc = pleaf->tgt; *pc; ++pc) {
            if (islower(*pc)) {
                printf_errf("(): Warning: Lowercase character in target name \"%s\".", pleaf->tgt);
                break;
            }
        }
    }

    // PCRE patterns
    if (C2_L_PTSTRING == pleaf->ptntype && C2_L_MPCRE == pleaf->match) {
        const char *error = NULL;
        int erroffset = 0;
        int options = 0;

        // Ignore case flag
        if (pleaf->match_ignorecase)
            options |= PCRE_CASELESS;

        // Compile PCRE expression
        pleaf->regex_pcre = pcre_compile(pleaf->ptnstr, options,
                &error, &erroffset, NULL);
        if (!pleaf->regex_pcre) {
            c2_error("Pattern \"%s\": PCRE regular expression parsing failed on "
                    "offset %d: %s", pleaf->ptnstr, erroffset, error);
            return false;
        }
        pleaf->regex_pcre_extra = pcre_study(pleaf->regex_pcre,
                PCRE_STUDY_JIT_COMPILE, &error);
        if (!pleaf->regex_pcre_extra) {
            printf("Pattern \"%s\": PCRE regular expression study failed: %s",
                    pleaf->ptnstr, error);
        }

        // Free the target string
        // free(pleaf->tgt);
        // pleaf->tgt = NULL;
    }

    return true;
}
/**
 * Free a condition tree.
 */
static void c2_free(c2_ptr_t p) {
    // For a branch element
    if (p.isbranch) {
        c2_b_t * const pbranch = p.b;

        if (!pbranch)
            return;

        c2_free(pbranch->opr1);
        c2_free(pbranch->opr2);
        free(pbranch);
    }
    // For a leaf element
    else {
        c2_l_t * const pleaf = p.l;

        if (!pleaf)
            return;

        free(pleaf->tgt);
        free(pleaf->ptnstr);
        pcre_free(pleaf->regex_pcre);
        LPCRE_FREE_STUDY(pleaf->regex_pcre_extra);
        free(pleaf);
    }
}

/**
 * Free a condition tree in c2_lptr_t.
 */
c2_lptr_t *
c2_free_lptr(c2_lptr_t *lp) {
    if (!lp)
        return NULL;

    c2_lptr_t *pnext = lp->next;
    c2_free(lp->ptr);
    free(lp);

    return pnext;
}

/**
 * Get a string representation of a rule target.
 */
static const char *
c2h_dump_str_tgt(const c2_l_t *pleaf) {
    if (pleaf->predef)
        return C2_PREDEFS[pleaf->predef].name;
    else
        return pleaf->tgt;
}

/**
 * Get a string representation of a target.
 */
static const char *
c2h_dump_str_type(const c2_l_t *pleaf) {
    switch (pleaf->type) {
        case C2_L_TWINDOW:    return "w";
        case C2_L_TDRAWABLE:  return "d";
        case C2_L_TCARDINAL:  return "c";
        case C2_L_TSTRING:    return "s";
        case C2_L_TATOM:      return "a";
        case C2_L_TUNDEFINED: break;
    }

    return NULL;
}

/**
 * Dump a condition tree.
 */
static void
c2_dump_raw(c2_ptr_t p) {
    // For a branch
    if (p.isbranch) {
        const c2_b_t * const pbranch = p.b;

        if (!pbranch)
            return;

        if (pbranch->neg)
            putchar('!');

        printf("(");
        c2_dump_raw(pbranch->opr1);

        switch (pbranch->op) {
            case C2_B_OAND: printf(" && ");   break;
            case C2_B_OOR:  printf(" || ");   break;
            case C2_B_OXOR: printf(" XOR ");  break;
            default:        assert(0);        break;
        }

        c2_dump_raw(pbranch->opr2);
        printf(")");
    }
    // For a leaf
    else {
        const c2_l_t * const pleaf = p.l;

        if (!pleaf)
            return;

        if (C2_L_OEXISTS == pleaf->op && pleaf->neg)
            putchar('!');

        // Print target name, type, and format
        {
            printf("%s", c2h_dump_str_tgt(pleaf));
            if (pleaf->tgt_onframe)
                putchar('@');
            if (pleaf->index >= 0)
                printf("[%d]", pleaf->index);
            printf(":%d%s", pleaf->format, c2h_dump_str_type(pleaf));
        }

        // Print operator
        putchar(' ');

        if (C2_L_OEXISTS != pleaf->op && pleaf->neg)
            putchar('!');

        switch (pleaf->match) {
            case C2_L_MEXACT:     break;
            case C2_L_MCONTAINS:  putchar('*');   break;
            case C2_L_MSTART:     putchar('^');   break;
            case C2_L_MPCRE:      putchar('~');   break;
            case C2_L_MWILDCARD:  putchar('%');   break;
        }

        if (pleaf->match_ignorecase)
            putchar('?');

        switch (pleaf->op) {
            case C2_L_OEXISTS:                        break;
            case C2_L_OEQ:      fputs("=",  stdout);  break;
            case C2_L_OGT:      fputs(">",  stdout);  break;
            case C2_L_OGTEQ:    fputs(">=", stdout);  break;
            case C2_L_OLT:      fputs("<",  stdout);  break;
            case C2_L_OLTEQ:    fputs("<=",  stdout); break;
        }

        if (C2_L_OEXISTS == pleaf->op)
            return;

        // Print pattern
        putchar(' ');
        switch (pleaf->ptntype) {
            case C2_L_PTINT:
                printf("%ld", pleaf->ptnint);
                break;
            case C2_L_PTSTRING:
                // TODO: Escape string before printing out?
                printf("\"%s\"", pleaf->ptnstr);
                break;
            default:
                assert(0);
                break;
        }
    }
}

/**
 * Get the type atom of a condition.
 */
static Atom
c2_get_atom_type(const c2_l_t *pleaf) {
    switch (pleaf->type) {
        case C2_L_TCARDINAL:
            return XA_CARDINAL;
        case C2_L_TWINDOW:
            return XA_WINDOW;
        case C2_L_TSTRING:
            return XA_STRING;
        case C2_L_TATOM:
            return XA_ATOM;
        case C2_L_TDRAWABLE:
            return XA_DRAWABLE;
        default:
            assert(0);
            break;
    }

    assert(0);
    return AnyPropertyType;
}

/**
 * Match a window against a single leaf window condition.
 *
 * For internal use.
 */
static void c2_match_once_leaf(session_t *ps, win *w, const c2_l_t *pleaf,
        bool *pres, bool *perr) {
    assert(pleaf);

    win_id wad = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    struct TracksWindowComponent* window = swiss_getComponent(&ps->win_list, COMPONENT_TRACKS_WINDOW, wad);

    Window wid;
    if(pleaf->tgt_onframe) {
        struct HasClientComponent* client = swiss_getComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wad);
        wid = client->id;
    } else {
        wid = window->id;
    }

    // Return if wid is missing
    if (!pleaf->predef && !wid)
        return;

    const int idx = (pleaf->index < 0 ? 0: pleaf->index);

    switch (pleaf->ptntype) {
        // Deal with integer patterns
        case C2_L_PTINT:
            {
                long tgt = 0;

                struct PhysicalComponent* physical = swiss_godComponent(&ps->win_list, COMPONENT_PHYSICAL, wad);

                // Get the value
                // A predefined target
                if (pleaf->predef) {
                    *perr = false;
                    switch (pleaf->predef) {
                        case C2_L_PID:      tgt = wid;                      break;
                        case C2_L_PX:
                            tgt = physical->position.x;
                            break;
                        case C2_L_PY:
                            tgt = physical->position.y;
                            break;
                        case C2_L_PX2:
                            tgt = physical->position.x + physical->size.x;
                            break;
                        case C2_L_PY2:
                            tgt = physical->position.y + physical->size.y;
                            break;
                        case C2_L_PWIDTH:
                        case C2_L_PWIDTHB:
                            tgt = physical->size.x;
                            break;
                        case C2_L_PHEIGHT:
                        case C2_L_PHEIGHTB:
                            tgt = physical->size.y;
                            break;
                        case C2_L_PBDW:     tgt = 0;        break;
                        case C2_L_PFULLSCREEN: tgt = win_is_fullscreen(ps, wad); break;
                        case C2_L_POVREDIR: tgt = w->a.override_redirect;   break;
                        case C2_L_PFOCUSED: tgt = ps->active_win == w; break;
                        case C2_L_PWMWIN:   tgt = w->wmwin;                 break;
                        case C2_L_PCLIENT: {
                            struct HasClientComponent* client = swiss_godComponent(&ps->win_list, COMPONENT_HAS_CLIENT, wad);
                            tgt = client != NULL ? client->id : None;
                            break;
                        }
                        default:            *perr = true; assert(0);        break;
                    }
                }
                // A raw window property
                else {
                    winprop_t prop = wid_get_prop_adv(ps, wid, pleaf->tgtatom,
                            idx, 1L, c2_get_atom_type(pleaf), pleaf->format);
                    if (prop.nitems) {
                        *perr = false;
                        tgt = winprop_get_int(prop);
                    }
                    free_winprop(&prop);
                }

                if (*perr)
                    return;

                // Do comparison
                switch (pleaf->op) {
                    case C2_L_OEXISTS:
                        *pres = (pleaf->predef ? tgt: true);
                        break;
                    case C2_L_OEQ:   *pres = (tgt == pleaf->ptnint);  break;
                    case C2_L_OGT:   *pres = (tgt > pleaf->ptnint);   break;
                    case C2_L_OGTEQ: *pres = (tgt >= pleaf->ptnint);  break;
                    case C2_L_OLT:   *pres = (tgt < pleaf->ptnint);   break;
                    case C2_L_OLTEQ: *pres = (tgt <= pleaf->ptnint);  break;
                    default:         *perr = true; assert(0);         break;
                }
            }
            break;
            // String patterns
        case C2_L_PTSTRING:
            {
                const char *tgt = NULL;
                char *tgt_free = NULL;

                // A predefined target
                if (pleaf->predef) {
                    switch (pleaf->predef) {
                        case C2_L_PWINDOWTYPE:  tgt = WINTYPES[w->window_type];
                                                break;
                        case C2_L_PNAME:        tgt = w->name;            break;
                        case C2_L_PCLASSG:      tgt = w->class_general;   break;
                        case C2_L_PCLASSI:      tgt = w->class_instance;  break;
                        case C2_L_PROLE:        tgt = w->role;            break;
                        default:                assert(0);                break;
                    }
                }
                // If it's an atom type property, convert atom to string
                else if (C2_L_TATOM == pleaf->type) {
                    winprop_t prop = wid_get_prop_adv(ps, wid, pleaf->tgtatom,
                            idx, 1L, c2_get_atom_type(pleaf), pleaf->format);
                    Atom atom = winprop_get_int(prop);
                    if (atom) {
                        tgt_free = XGetAtomName(ps->dpy, atom);
                    }
                    if (tgt_free) {
                        tgt = tgt_free;
                    }
                    free_winprop(&prop);
                }
                // Otherwise, just fetch the string list
                else {
                    char **strlst = NULL;
                    int nstr;
                    if (wid_get_text_prop(ps, wid, pleaf->tgtatom, &strlst,
                                &nstr) && nstr > idx) {
                        tgt_free = mstrcpy(strlst[idx]);
                        tgt = tgt_free;
                    }
                    if (strlst)
                        XFreeStringList(strlst);
                }

                if (tgt) {
                    *perr = false;
                }
                else {
                    return;
                }

                // Actual matching
                switch (pleaf->op) {
                    case C2_L_OEXISTS:
                        *pres = true;
                        break;
                    case C2_L_OEQ:
                        switch (pleaf->match) {
                            case C2_L_MEXACT:
                                if (pleaf->match_ignorecase)
                                    *pres = !strcasecmp(tgt, pleaf->ptnstr);
                                else
                                    *pres = !strcmp(tgt, pleaf->ptnstr);
                                break;
                            case C2_L_MCONTAINS:
                                if (pleaf->match_ignorecase)
                                    *pres = strcasestr(tgt, pleaf->ptnstr);
                                else
                                    *pres = strstr(tgt, pleaf->ptnstr);
                                break;
                            case C2_L_MSTART:
                                if (pleaf->match_ignorecase)
                                    *pres = !strncasecmp(tgt, pleaf->ptnstr,
                                            strlen(pleaf->ptnstr));
                                else
                                    *pres = !strncmp(tgt, pleaf->ptnstr,
                                            strlen(pleaf->ptnstr));
                                break;
                            case C2_L_MWILDCARD:
                                {
                                    int flags = 0;
                                    if (pleaf->match_ignorecase)
                                        flags |= FNM_CASEFOLD;
                                    *pres = !fnmatch(pleaf->ptnstr, tgt, flags);
                                }
                                break;
                            case C2_L_MPCRE:
                                *pres = (pcre_exec(pleaf->regex_pcre,
                                            pleaf->regex_pcre_extra,
                                            tgt, strlen(tgt), 0, 0, NULL, 0) >= 0);
                                break;
                        }
                        break;
                    default:
                        *perr = true;
                        assert(0);
                }

                // Free the string after usage, if necessary
                if (tgt_free) {
                    if (C2_L_TATOM == pleaf->type)
                        cxfree(tgt_free);
                    else
                        free(tgt_free);
                }
            }
            break;
        default:
            assert(0);
            break;
    }
}

/**
 * Match a window against a single window condition.
 *
 * @return true if matched, false otherwise.
 */
static bool c2_match_once(session_t *ps, win *w, const c2_ptr_t cond) {
    bool result = false;
    bool error = true;

    // Handle a branch
    if (cond.isbranch) {
        const c2_b_t *pb = cond.b;

        if (!pb)
            return false;

        error = false;

        switch (pb->op) {
            case C2_B_OAND:
                result = (c2_match_once(ps, w, pb->opr1)
                        && c2_match_once(ps, w, pb->opr2));
                break;
            case C2_B_OOR:
                result = (c2_match_once(ps, w, pb->opr1)
                        || c2_match_once(ps, w, pb->opr2));
                break;
            case C2_B_OXOR:
                result = (c2_match_once(ps, w, pb->opr1)
                        != c2_match_once(ps, w, pb->opr2));
                break;
            default:
                error = true;
                assert(0);
        }

#ifdef DEBUG_WINMATCH
        printf_dbgf("(%#010lx): branch: result = %d, pattern = ", w->id, result);
        c2_dump(cond);
#endif
    }
    // Handle a leaf
    else {
        const c2_l_t *pleaf = cond.l;

        if (!pleaf)
            return false;

        c2_match_once_leaf(ps, w, pleaf, &result, &error);

        // For EXISTS operator, no errors are fatal
        if (C2_L_OEXISTS == pleaf->op && error) {
            result = false;
            error = false;
        }

#ifdef DEBUG_WINMATCH
        printf_dbgf("(%#010lx): leaf: result = %d, error = %d, "
                "client = %#010lx,  pattern = ",
                w->id, result, error, w->client_win);
        c2_dump(cond);
#endif
    }

    // Postprocess the result
    if (error)
        result = false;

    if (cond.isbranch ? cond.b->neg: cond.l->neg)
        result = !result;

    return result;
}

/**
 * Match a window against a condition linked list.
 *
 * @param cache a place to cache the last matched condition
 * @param pdata a place to return the data
 * @return true if matched, false otherwise.
 */
bool c2_matchd(session_t *ps, win *w, const c2_lptr_t *condlst,
        const c2_lptr_t **cache, void **pdata) {
    assert(IsViewable == w->a.map_state);

    // Check if the cached entry matches firstly
    if (cache && *cache && c2_match_once(ps, w, (*cache)->ptr)) {
        if (pdata)
            *pdata = (*cache)->data;
        return true;
    }

    // Then go through the whole linked list
    for (; condlst; condlst = condlst->next) {
        if (c2_match_once(ps, w, condlst->ptr)) {
            if (cache)
                *cache = condlst;
            if (pdata)
                *pdata = condlst->data;
            return true;
        }
    }

    return false;
}
