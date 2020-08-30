#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void help(FILE* dest, char* name) {
    fprintf(dest, "Usage: %s [options] header\n", name);
    fprintf(dest, "Options:\n");
    fprintf(dest, "  -o     output file location, defaults to stdout\n");
    fprintf(dest, "  -c     generate c source instead of header\n");
    fprintf(dest, "  -h     print help\n");
}

struct arg {
    char type[64];
    char name[64];
};

struct fun {
    char name[64];
    char ret[64];
    size_t numArgs;
    struct arg args[16];
};

enum mode {
    MODE_C,
    MODE_H
};

bool readFunction(const char* buf, struct fun* fun) {
    char argstr[256];
    int matches = sscanf(buf, "%63s %63[a-zA-Z0-9_](%255[^)]);", fun->ret, fun->name, argstr);

    if(matches != 3) {
        printf("Wrongly formatted function \"%s\", ignoring\n", buf);
        return false;
    }

    char* argBegin = argstr;
    char* argEnd = argBegin;
    fun->numArgs = 0;
    while(*argEnd != '\0') {
        if(fun->numArgs >= 16) {
            printf("Too many arguments to intercepted function\n");
            exit(1);
        }
        argEnd = strchrnul(argBegin, ',');
        char* argSep = memrchr(argBegin, ' ', argEnd - argBegin);

        memcpy(&fun->args[fun->numArgs].name,argSep + 1, argEnd - argSep - 1);
        memcpy(&fun->args[fun->numArgs].type, argBegin, argSep - argBegin);
        fun->args[fun->numArgs].name[argEnd - argSep - 1] = '\0';
        fun->args[fun->numArgs].type[argSep - argBegin] = '\0';

        argBegin = argEnd + 1;
        fun->numArgs++;
    }

    return true;
}

void writeFunction(FILE* out, const struct fun* fun, const enum mode mode, bool profile) {
    if (mode == MODE_C) {
        fprintf(out, "%s %sH(", fun->ret, fun->name);
        for(size_t i = 0; i < fun->numArgs; i++) {
            if(i != 0) {
                fprintf(out, ", ");
            }
            fprintf(out, "%s %s", fun->args[i].type, fun->args[i].name);
        }
        fprintf(out, ") {\n");
        if(profile) {
            fprintf(out, "    zone_scope_extra(&ZONE_x_call, \"%s\");\n", fun->name);
        }
        fprintf(out, "    return %s(", fun->name);
        for(size_t i = 0; i < fun->numArgs; i++) {
            if(i != 0) {
                fprintf(out, ", ");
            }
            fprintf(out, "%s", fun->args[i].name);
        }
        fprintf(out, ");\n");
        fprintf(out, "}\n");
    } else {
        fprintf(out, "%s %sH(", fun->ret, fun->name);
        for(size_t i = 0; i < fun->numArgs; i++) {
            if(i != 0) {
                fprintf(out, ", ");
            }
            fprintf(out, "%s %s", fun->args[i].type, fun->args[i].name);
        }
        fprintf(out, ");\n");
    }
}

int main(int argc, char **argv) {
    enum mode mode = MODE_H;
    char* outloc = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "hco:")) != -1) {
        switch (opt) {
            case 'h':
                help(stdout, argv[0]);
                exit(EXIT_SUCCESS);
            case 'c':
                mode = MODE_C;
                break;
            case 'o':
                outloc = optarg;
                break;
            default:
                help(stderr, argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    FILE* out;
    if(outloc == NULL) {
        out = stdout;
    } else {
        out = fopen(outloc, "w");
    }

    char* path = argv[optind];
    FILE* file = fopen(path, "r");
    if(file == NULL) {
        printf("Failed loading file %s\n", path);
        return 1;
    }

    fseek(file, 0, SEEK_SET);

    char* line = NULL;
    size_t line_size = 0;

    size_t read;
    read = getline(&line, &line_size, file);
    if(read <= 0 || strcmp(line, "version 1\n") != 0) {
        printf("No version found at the start of file %s, found %s\n", path, line);
        return 1;
    }

    if(mode == MODE_C) {
        fprintf(out, "#include \"intercept/xorg.h\"\n");
        fprintf(out, "#include \"profiler/zone.h\"\n");
        fprintf(out, "DECLARE_ZONE(x_call);\n");

    }

    while((read = getline(&line, &line_size, file)) != -1) {
        if(read == 0 || line[0] == '#')
            continue;

        // Remove the trailing newlines
        line[strcspn(line, "\r\n")] = '\0';

        char command[64];
        int restStart;
        int matches = sscanf(line, "%63s %n);", command, &restStart);
        char* rest = line + restStart;

        // An EOF from matching means either an error or empty line. We will
        // just swallow those.
        if(matches == EOF)
            continue;

        if(matches != 1) {
            printf("Wrongly formatted line \"%s\", ignoring\n", line);
            continue;
        }

        if(strcmp(command, "include") == 0) {
            if(mode == MODE_H) {
                fprintf(out, "#include %s\n", line + restStart);
            }
            continue;
        } else if(strcmp(command, "noprofile") == 0) {
        } else if(strcmp(command, "fun") == 0) {
            struct fun fun;
            if(!readFunction(rest, &fun)) {
                continue;
            }

            writeFunction(out, &fun, mode, true);
        } else if(strcmp(command, "ffun") == 0) {
            struct fun fun;
            if(!readFunction(rest, &fun)) {
                continue;
            }

            writeFunction(out, &fun, mode, false);
        } else {
            printf("Invalid command %s\n", command);
        }
    }
}
