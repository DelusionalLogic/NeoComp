#include "face.h"

#include <string.h>
#include <assert.h>

void face_init(struct face* asset, size_t vertex_count) {
    vector_init(&asset->vertex_buffer, sizeof(float), vertex_count * 3);
    vector_init(&asset->uv_buffer, sizeof(float), vertex_count * 2);
}

struct face* face_load_file(const char* path) {
    FILE* file = fopen(path, "r");
    if(file == NULL) {
        printf("Failed loading face file %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_SET);

    struct face* face = malloc(sizeof(struct face));

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
            vector_init(&face->vertex_buffer, sizeof(float), count * 3);
            float* cursor = vector_reserve(&face->vertex_buffer, count * 3);
            size_t lines = 0;
            while((read = getline(&line, &line_size, file)) != -1) {
                if(line[0] == '#')
                    continue;

                // Remove the trailing newlines
                line[strcspn(line, "\r\n")] = '\0';

                float x;
                float y;
                float z;
                matches = sscanf(line, "v %f %f %f", &x, &y, &z);

                // An EOF from matching means either an error or empty line. We will
                // just stop on those.
                if(matches == EOF)
                    break;

                if(matches != 3) {
                    printf("Wrongly formatted line \"%s\", ignoring\n", line);
                    continue;
                }

                if(lines >= count) {
                    printf("Too many pos vertecies expected %zu\n", count);
                    continue;
                }

                cursor[0] = x;
                cursor[1] = y;
                cursor[2] = z;

                cursor += 3;
                lines++;
            }
        } else if(strcmp(type, "vtc") == 0) {
            vector_init(&face->uv_buffer, sizeof(float), count * 2);
            float* cursor = vector_reserve(&face->uv_buffer, count * 2);
            size_t lines = 0;
            while((read = getline(&line, &line_size, file)) != -1) {
                if(line[0] == '#')
                    continue;

                // Remove the trailing newlines
                line[strcspn(line, "\r\n")] = '\0';

                float u;
                float v;
                int matches = sscanf(line, "vt %f %f", &u, &v);

                // An EOF from matching means either an error or empty line. We will
                // just stop on those.
                if(matches == EOF)
                    break;

                if(matches != 2) {
                    printf("Wrongly formatted line \"%s\", ignoring\n", line);
                    continue;
                }

                if(lines >= count) {
                    printf("Too many uv vertecies expected %zu\n", count);
                    continue;
                }

                cursor[0] = u;
                cursor[1] = v;

                cursor += 2;
                lines++;
            }
        } else {
            printf("Unknown directive \"%s\" in face file %s, ignoring\n", line, path);
        }
    }

    free(line);
    fclose(file);

    face_upload(face);

    return face;
}

void face_init_rects(struct face* asset, Vector* rects) {

    face_init(asset, vector_size(rects) * 6);

    float* vertex = vector_reserve(&asset->vertex_buffer, vector_size(rects) * 6 * 3);
    float* uv = vector_reserve(&asset->uv_buffer, vector_size(rects) * 6 * 2);

    size_t index;
    struct Rect* rect = vector_getFirst(rects, &index);
    while(rect != NULL) {
        // A single rect line in the vertex buffer is 3 * 6
        float* vertex_rect = &vertex[index * 3 * 6];
        // A single rect line in the uv buffer is 2 * 6
        float* uv_rect = &uv[index * 2 * 6];

        int vec_cnt = 0;
        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect->pos.x;
            vertex_vec[1] = rect->pos.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect->pos.x;
            uv_vec[1] = rect->pos.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect->pos.x;
            vertex_vec[1] = rect->pos.y - rect->size.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect->pos.x;
            uv_vec[1] = rect->pos.y - rect->size.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect->pos.x + rect->size.x;
            vertex_vec[1] = rect->pos.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect->pos.x + rect->size.x;
            uv_vec[1] = rect->pos.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect->pos.x + rect->size.x;
            vertex_vec[1] = rect->pos.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect->pos.x + rect->size.x;
            uv_vec[1] = rect->pos.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect->pos.x;
            vertex_vec[1] = rect->pos.y - rect->size.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect->pos.x;
            uv_vec[1] = rect->pos.y - rect->size.y;
        }

        {
            float* vertex_vec = &vertex_rect[vec_cnt * 3];
            float* uv_vec = &uv_rect[vec_cnt * 2];
            vec_cnt++;
            vertex_vec[0] = rect->pos.x + rect->size.x;
            vertex_vec[1] = rect->pos.y - rect->size.y;
            vertex_vec[2] = 0;

            uv_vec[0] = rect->pos.x + rect->size.x;
            uv_vec[1] = rect->pos.y - rect->size.y;
        }
        rect = vector_getNext(rects, &index);
    }
}

void face_upload(struct face* asset) {
    glGenBuffers(1, &asset->vertex);
    glGenBuffers(1, &asset->uv);

    glBindBuffer(GL_ARRAY_BUFFER, asset->vertex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * asset->vertex_buffer.size, asset->vertex_buffer.data, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, asset->uv);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * asset->uv_buffer.size, asset->uv_buffer.data, GL_STATIC_DRAW);
}

void face_unload_file(struct face* asset) {
    glDeleteBuffers(1, &asset->vertex);
    glDeleteBuffers(1, &asset->uv);

    vector_kill(&asset->vertex_buffer);
    vector_kill(&asset->uv_buffer);
}
