#include "face.h"

#include <string.h>

struct face* face_load_file(const char* path) {
    FILE* file = fopen(path, "r");
    if(file == NULL) {
        printf("Failed loading face file %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_SET);

    struct face* face = malloc(sizeof(struct face));
    face->vertex_buffer_data = NULL;
    face->uv_buffer_data = NULL;

    char* line = NULL;
    size_t line_size = 0;

    size_t read;
    read = getline(&line, &line_size, file);
    if(read <= 0 || strcmp(line, "#version 1\n") != 0) {
        printf("No version found at the start of file %s, found %s\n", path, line);
        return NULL;
    }

    while((read = getline(&line, &line_size, file)) != -1) {
        if(read == 0 || line[0] == '#')
            continue;

        // Remove the trailing newlines
        line[strcspn(line, "\r\n")] = '\0';

        char type[64];
        size_t count;
        int matches = sscanf(line, "%63s %ld", type, &count);

        // An EOF from matching means either an error or empty line. We will
        // just swallow those.
        if(matches == EOF)
            continue;

        if(matches != 2) {
            printf("Wrongly formatted line \"%s\", ignoring\n", line);
            continue;
        }

        if(strcmp(type, "vc") == 0) {
            face->vertex_buffer_size = count;
            face->vertex_buffer_data = malloc(sizeof(float) * count * 3);
            float* cursor = face->vertex_buffer_data;
            while((read = getline(&line, &line_size, file)) != -1) {
                if(line[0] == '#')
                    continue;

                float x;
                float y;
                float z;

                // Remove the trailing newlines
                line[strcspn(line, "\r\n")] = '\0';
                matches = sscanf(line, "v %f %f %f", &x, &y, &z);

                // An EOF from matching means either an error or empty line. We will
                // just stop on those.
                if(matches == EOF)
                    break;

                if(matches != 3) {
                    printf("Wrongly formatted line \"%s\", ignoring\n", line);
                    continue;
                }

                cursor[0] = x;
                cursor[1] = y;
                cursor[2] = z;

                cursor += 3;
            }
        } else if(strcmp(type, "vtc") == 0) {
            face->uv_buffer_size = count;
            face->uv_buffer_data = malloc(sizeof(float) * count * 2);
            float* cursor = face->uv_buffer_data;
            while((read = getline(&line, &line_size, file)) != -1) {
                if(line[0] == '#')
                    continue;

                float u;
                float v;

                // Remove the trailing newlines
                line[strcspn(line, "\r\n")] = '\0';
                int matches = sscanf(line, "vt %f %f", &u, &v);

                // An EOF from matching means either an error or empty line. We will
                // just stop on those.
                if(matches == EOF)
                    break;

                if(matches != 2) {
                    printf("Wrongly formatted line \"%s\", ignoring\n", line);
                    continue;
                }

                cursor[0] = u;
                cursor[1] = v;

                cursor += 2;
            }
        } else {
            printf("Unknown directive \"%s\" in face file %s, ignoring\n", line, path);
        }
    }

    free(line);
    fclose(file);

    glGenBuffers(1, &face->vertex);
    glGenBuffers(1, &face->uv);

    glBindBuffer(GL_ARRAY_BUFFER, face->vertex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * face->vertex_buffer_size * 3, face->vertex_buffer_data, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, face->uv);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * face->uv_buffer_size * 2, face->uv_buffer_data, GL_STATIC_DRAW);

    return face;
}

void face_unload_file(struct face* asset) {
    glDeleteBuffers(1, &asset->vertex);
    glDeleteBuffers(1, &asset->uv);

    free(asset->vertex_buffer_data);
    free(asset->uv_buffer_data);
}
