#include "vmath.h"

#include <string.h>

double lerp(double a, double b, double f) {
    return a + f * (b - a);
}

#define DEFINE_VEC_OPS(n)                                                    \
void vec##n##_add(Vector##n * r, const Vector##n * const a)                  \
{                                                                            \
    for(int i = 0; i < n; i++)                                               \
        r->m[i] = r->m[i] + a->m[i];                                         \
}                                                                            \
void vec##n##_sub(Vector##n * r, const Vector##n * const a)                  \
{                                                                            \
    for(int i = 0; i < n; i++)                                               \
        r->m[i] = r->m[i] - a->m[i];                                         \
}                                                                            \
void vec##n##_scale(Vector##n * const v, float const s)                      \
{                                                                            \
    for(int i = 0; i < n; i++)                                               \
        v->m[i] = v->m[i] * s;                                               \
}                                                                            \
float vec##n##_mul_inner(Vector##n * const a, const Vector##n * const b)     \
{                                                                            \
    float p = 0.;                                                            \
    for(int i = 0; i < n; i++)                                               \
        p += b->m[i] * a->m[i];                                              \
    return p;                                                                \
}                                                                            \
void vec##n##_idiv(Vector##n * const a, float x) {                           \
    for(int i = 0; i < n; i++)                                               \
        a->m[i] /= x;                                                        \
}                                                                            \
void vec##n##_div(Vector##n * const a, const Vector##n * const b) {          \
    for(int i = 0; i < n; i++)                                               \
        a->m[i] = a->m[i] / b->m[i];                                         \
}                                                                            \
void vec##n##_imul(Vector##n * const a, float x) {                           \
    for(int i = 0; i < n; i++)                                               \
        a->m[i] *= x;                                                        \
}                                                                            \
void vec##n##_mul(Vector##n * const a, const Vector##n * const b) {          \
    for(int i = 0; i < n; i++)                                               \
        a->m[i] = a->m[i] * b->m[i];                                         \
}                                                                            \
float vec##n##_len(const Vector##n * const v)                                \
{                                                                            \
    Vector##n c = *v;                                                        \
    return sqrtf(vec##n##_mul_inner(&c,v));                                  \
}                                                                            \
void vec##n##_norm(Vector##n * const v)                                      \
{                                                                            \
    float k = 1.0 / vec##n##_len(v);                                         \
    vec##n##_scale(v, k);                                                    \
}                                                                            \
void vec##n##_min(Vector##n * a, const Vector##n * b)                        \
{                                                                            \
    for(int i = 0; i < n; i++)                                               \
        a->m[i] = a->m[i] < b->m[i] ? a->m[i] : b->m[i];                     \
}                                                                            \
void vec##n##_max(Vector##n * a, const Vector##n * b)                        \
{                                                                            \
    for(int i = 0; i < n; i++)                                               \
        a->m[i] = a->m[i]>b->m[i] ? a->m[i] : b->m[i];                       \
}                                                                            \
void vec##n##_clamp(Vector##n * a, const Vector##n * b, const Vector##n * c) \
{                                                                            \
    vec##n##_max(a, b);                                                      \
    vec##n##_min(a, c);                                                      \
}                                                                            \
bool vec##n##_eq(const Vector##n * a, const Vector##n * b)                   \
{                                                                            \
    for(int i = 0; i < n; i++) {                                             \
        if(a->m[i] != b->m[i])                                               \
            return false;                                                    \
    }                                                                        \
    return true;                                                             \
}

#define DEFINE_VEC_CONST(n, k)                                          \
Vector##n vec##n##_from_vec##k (const Vector##k * const v, ...) {       \
    va_list args;                                                       \
    va_start(args, v);                                                  \
                                                                        \
    Vector##n r;                                                        \
    for(size_t i = 0; i < k; i++) {                                     \
        r.m[i] = v->m[i];                                               \
    }                                                                   \
    for(size_t i = k; i < n; i++) {                                     \
        r.m[i] = (float)va_arg(args, double);                           \
    }                                                                   \
    return r;                                                           \
}

DEFINE_VEC_OPS(2)
DEFINE_VEC_OPS(3)
DEFINE_VEC_OPS(4)
DEFINE_VEC_CONST(4, 2)
DEFINE_VEC_CONST(3, 2)
DEFINE_VEC_CONST(4, 3)

#undef DEFINE_VEC_CONST
#undef DEFINE_VEC_OPS

static const double PI = 3.14159265358979323846;

Matrix mat4_multiply(const Matrix* m1, const Matrix* m2) {
    Matrix out;;
    unsigned int row, column, row_offset;

    for (row = 0, row_offset = row * 4; row < 4; ++row, row_offset = row * 4)
        for (column = 0; column < 4; ++column)
            out.m[row_offset + column] =
                (m2->m[row_offset + 0] * m1->m[column + 0]) +
                (m2->m[row_offset + 1] * m1->m[column + 4]) +
                (m2->m[row_offset + 2] * m1->m[column + 8]) +
                (m2->m[row_offset + 3] * m1->m[column + 12]);

    return out;
}

Vector4 mat4_vec4_mul(const Matrix* m, const Vector4* v) {
    Vector4 out;
    for(int i = 0; i < 4; ++i) {
        out.m[i] =
            (v->m[0] * m->m[i + 0]) +
            (v->m[1] * m->m[i + 4]) +
            (v->m[2] * m->m[i + 8]) +
            (v->m[3] * m->m[i + 12]);
    }

    return out;
}

void vec4_normalize(Vector4* v) {
    float sqr = v->m[0] * v->m[0] + v->m[1] * v->m[1] + v->m[2] * v->m[2];
    if(sqr == 1 || sqr == 0)
        return;
    float invrt = 1.f/sqrt(sqr);
    v->m[0] *= invrt;
    v->m[1] *= invrt;
    v->m[2] *= invrt;
}

float vec4_dot(const Vector4* v1, const Vector4* v2) {
    return v1->m[0] * v2->m[0] + v1->m[1] * v2->m[1] + v1->m[2] * v2->m[2]
        + v1->m[3] * v2->m[3];
}

Vector4 vec4_cross(const Vector4* v1, const Vector4* v2) {
    Vector4 out = {{0}};
    out.m[0] = v1->m[1]*v2->m[2] - v1->m[2]*v2->m[1];
    out.m[1] = v1->m[2]*v2->m[0] - v1->m[0]*v2->m[2];
    out.m[2] = v1->m[0]*v2->m[1] - v1->m[1]*v2->m[0];
    return out;
}
void mat4_rotateX(Matrix* const m, float angle) {
    Matrix rotation = IDENTITY_MATRIX;
    float sine = (float)sin(angle);
    float cosine = (float)cos(angle);

    rotation.m[5] = cosine;
    rotation.m[6] = -sine;
    rotation.m[9] = sine;
    rotation.m[10] = cosine;

    memcpy(m->m, mat4_multiply(m, &rotation).m, sizeof(m->m));
}
void mat4_rotateY(Matrix* const m, float angle) {
    Matrix rotation = IDENTITY_MATRIX;
    float sine = (float)sin(angle);
    float cosine = (float)cos(angle);

    rotation.m[0] = cosine;
    rotation.m[8] = sine;
    rotation.m[2] = -sine;
    rotation.m[10] = cosine;

    memcpy(m->m, mat4_multiply(m, &rotation).m, sizeof(m->m));
}
void mat4_rotateZ(Matrix* const m, float angle) {
    Matrix rotation = IDENTITY_MATRIX;
    float sine = (float)sin(angle);
    float cosine = (float)cos(angle);

    rotation.m[0] = cosine;
    rotation.m[1] = -sine;
    rotation.m[4] = sine;
    rotation.m[5] = cosine;

    memcpy(m->m, mat4_multiply(m, &rotation).m, sizeof(m->m));
}
void mat4_scale(Matrix* const m, float x, float y, float z) {
    Matrix scale = IDENTITY_MATRIX;

    scale.m[0] = x;
    scale.m[5] = y;
    scale.m[10] = z;

    memcpy(m->m, mat4_multiply(m, &scale).m, sizeof(m->m));
}
void mat4_translate(Matrix* const m, float x, float y, float z) {
    Matrix translation = IDENTITY_MATRIX;

    translation.m[12] = x;
    translation.m[13] = y;
    translation.m[14] = z;

    memcpy(m->m, mat4_multiply(m, &translation).m, sizeof(m->m));
}

Matrix perspective(float fovy, float aspect_ratio, float near_plane, float far_plane) {
    Matrix out = { { 0 } };

    const float
        y_scale = (float)(1/cos(fovy * PI / 360)),
                x_scale = y_scale / aspect_ratio,
                frustum_length = far_plane - near_plane;

    out.m[0] = x_scale;
    out.m[5] = y_scale;
    out.m[10] = -((far_plane + near_plane) / frustum_length);
    out.m[11] = -1;
    out.m[14] = -((2 * near_plane * far_plane) / frustum_length);

    return out;
}

Matrix mat4_orthogonal(float left, float right, float bottom, float top, float near, float far) {
    Matrix out = IDENTITY_MATRIX;
    out.m[0] = 2 / (right - left);
    out.m[5] = 2 / (top - bottom);
    out.m[10] = 2 / (far - near);

    out.m[12] = -((right + left) / (right - left));
    out.m[13] = -((top + bottom) / (top - bottom));
    out.m[14] = -((far + near) / (far - near));

    out.m[15] = 1;

    return out;
}

Matrix lookAt(Vector4 pos, Vector4 dir) {
    Vector4 f = dir;
    vec4_normalize(&f);
    Vector4 u = {{0, 1, 0, 0}};
    Vector4 s = vec4_cross(&f, &u);
    vec4_normalize(&s);
    u = vec4_cross(&s, &f);

    Matrix out = IDENTITY_MATRIX;
    out.m[0] = s.x;
    out.m[4] = s.y;
    out.m[8] = s.z;

    out.m[1] = u.x;
    out.m[5] = u.y;
    out.m[9] = u.z;

    out.m[2] = -f.x;
    out.m[6] = -f.y;
    out.m[10] = -f.z;

    out.m[12] = -vec4_dot(&s, &pos);
    out.m[13] = -vec4_dot(&u, &pos);
    out.m[14] =  vec4_dot(&f, &pos);
    return out;
}
