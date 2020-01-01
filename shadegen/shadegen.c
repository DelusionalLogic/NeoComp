#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void help(FILE* dest, char* name) {
    fprintf(dest, "Usage: %s [options] [type...]\n", name);
    fprintf(dest, "Options:\n");
    fprintf(dest, "  -o     output file location, defaults to stdout\n");
    fprintf(dest, "  -c     generate c source instead of header\n");
    fprintf(dest, "  -h     print help\n");
}

struct type {
    char name[64];
    char info[64];
    char struc[64];
    char uniforms[10][64];
    int num_uniforms;
};

enum mode {
    MODE_C,
    MODE_H
};

int process_file(FILE* out, char* path, struct type* type) {
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
    if(read <= 0 || strcmp(line, "#version 1\n") != 0) {
        printf("No version found at the start of file %s, found %s\n", path, line);
        return 1;
    }

    while((read = getline(&line, &line_size, file)) != -1) {
        if(read == 0 || line[0] == '#')
            continue;

        // Remove the trailing newlines
        line[strcspn(line, "\r\n")] = '\0';

        char comm[64];
        char arg[64];
        int matches = sscanf(line, "%63s %63s", comm, arg);

        // An EOF from matching means either an error or empty line. We will
        // just swallow those.
        if(matches == EOF)
            continue;

        if(matches != 2) {
            printf("Wrongly formatted line \"%s\", ignoring\n", line);
            continue;
        }

        if(strcmp(comm, "name") == 0) {
            strncpy(type->name, arg, 63);
        }else if(strcmp(comm, "info") == 0) {
            strncpy(type->info, arg, 63);
        }else if(strcmp(comm, "struct") == 0) {
            strncpy(type->struc, arg, 63);
        }else if(strcmp(comm, "uniform") == 0) {
            if(type->num_uniforms == 10) {
                fprintf(stderr, "%s: Max 10 uniforms\n", path);
                exit(EXIT_FAILURE);
            }
            strncpy(type->uniforms[type->num_uniforms], arg, 63);
            type->num_uniforms++;
        }
    }

    return 0;
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

    struct type type[64];
    int num_types = argc - optind;

    for(int i = 0; i < num_types; i++) {
        process_file(out, argv[optind + i], &type[i]);
    }

    if(mode == MODE_H) {
        fprintf(out, "#pragma once\n");
        fprintf(out, "#include <stdio.h>\n");
        fprintf(out, "struct shader_uniform_info {\n");
        fprintf(out, "    char* name;\n");
        fprintf(out, "    size_t offset;\n");
        fprintf(out, "};\n");
        fprintf(out, "\n");
        fprintf(out, "struct shader_type_info {\n");
        fprintf(out, "    char* name;\n");
        fprintf(out, "    size_t size;\n");
        fprintf(out, "    size_t member_count;\n");
        fprintf(out, "    struct shader_uniform_info members[];\n");
        fprintf(out, "};\n");

        for(int i = 0; i < num_types; i++) {
            fprintf(out, "struct %s {\n", type[i].struc);
            for(int j = 0; j < type[i].num_uniforms; j++) {
                fprintf(out, "    struct shader_value* %s;\n", type[i].uniforms[j]);
            }
            fprintf(out, "};\n");

            fprintf(out, "extern struct shader_type_info %s;\n", type[i].info);
        }
    } else if(mode == MODE_C) {
        fprintf(out, "#include <stddef.h>\n");
        fprintf(out, "#include \"shaders/include.h\"\n");
        for(int i = 0; i < num_types; i++) {
            fprintf(out, "struct shader_type_info %s = {\n", type[i].info);
            fprintf(out, "    .name = \"%s\",\n", type[i].name);
            fprintf(out, "    .size = sizeof(struct %s),\n", type[i].struc);
            fprintf(out, "    .member_count = %d,\n", type[i].num_uniforms);
            fprintf(out, "    .members = {\n");
            for(int j = 0; j < type[i].num_uniforms; j++) {
                fprintf(out, "        {\"%s\", offsetof(struct %s, %s)},\n",
                    type[i].uniforms[j],
                    type[i].struc,
                    type[i].uniforms[j]
                );
            }
            fprintf(out, "    }\n");
            fprintf(out, "};\n");
        }
    }
}
