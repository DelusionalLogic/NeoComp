#pragma once

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

double lerp(double a, double b, double f);

typedef struct Matrix {
    float m[16];
} Matrix;

static const Matrix IDENTITY_MATRIX = {{
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
}};

void mat4_rotateX(Matrix* const m, float angle);
void mat4_rotateY(Matrix* const m, float angle);
void mat4_rotateZ(Matrix* const m, float angle);
void mat4_scale(Matrix* const m, float x, float y, float z);
void mat4_translate(Matrix* const m, float x, float y, float z);

Matrix mat4_multiply(const Matrix* m1, const Matrix* m2);

Matrix perspective(float fovy, float aspect_ratio, float near_plane, float far_plane);
Matrix mat4_orthogonal(float left, float right, float bottom, float top, float near, float far);

// Vector operations {{{
#define DEFINE_VEC_OPS(n)                                                     \
void vec##n##_add(Vector##n * r, const Vector##n * const a);                  \
void vec##n##_sub(Vector##n * r, const Vector##n * const a);                  \
void vec##n##_scale(Vector##n * const v, float const s);                      \
float vec##n##_mul_inner(Vector##n * const a, const Vector##n * const b);     \
void vec##n##_idiv(Vector##n * a, float x);                                   \
void vec##n##_div(Vector##n * const a, const Vector##n * const b);            \
void vec##n##_imul(Vector##n * a, float x);                                   \
void vec##n##_mul(Vector##n * const a, const Vector##n * const b);            \
float vec##n##_len(const Vector##n * const v);                                \
void vec##n##_norm(Vector##n * const v);                                      \
void vec##n##_min(Vector##n * a, const Vector##n * b);                        \
void vec##n##_max(Vector##n * a, const Vector##n * b);                        \
void vec##n##_clamp(Vector##n * a, const Vector##n * b, const Vector##n * c); \
bool vec##n##_eq(const Vector##n * a, const Vector##n * b);                   \

#define DEFINE_VEC_CONST(n, k) \
    Vector##n vec##n##_from_vec##k (const Vector##k * const v, ...);
// }}}

typedef union Vector4 {
    float m[4];
    struct {
        float x, y, z, w;
    };
} Vector4;

static const Vector4 X_AXIS = {{1, 0, 0, 0}};
static const Vector4 Y_AXIS = {{0, 1, 0, 0}};
static const Vector4 Z_AXIS = {{0, 0, 1, 0}};
static const Vector4 INV_X_AXIS = {{-1, 0, 0, 0}};
static const Vector4 INV_Y_AXIS = {{0, -1, 0, 0}};
static const Vector4 INV_Z_AXIS = {{0, 0, -1, 0}};

void vec4_normalize(Vector4* v);
float vec4_dot(const Vector4* v1, const Vector4* v2);
Vector4 vec4_cross(const Vector4* v1, const Vector4* v2);

DEFINE_VEC_OPS(4)
Vector4 mat4_vec4_mul(const Matrix* m, const Vector4* v);

Matrix lookAt(Vector4 pos, Vector4 dir);

typedef union Vector3 {
    float m[3];
    struct {
        float x, y, z;
    };
} Vector3;

static const Vector3 VEC3_ZERO = {{0, 0, 0}};
static const Vector3 VEC3_UNIT = {{1, 1, 1}};
static const Vector3 VEC3_Z    = {{0, 0, 1}};

DEFINE_VEC_OPS(3)
DEFINE_VEC_CONST(4, 3)

typedef union Vector2 {
    float m[2];
    struct {
        float x, y;
    };
    struct {
        float u, v;
    };
} Vector2;

static const Vector2 VEC2_ZERO = {{0, 0}};
static const Vector2 VEC2_UNIT = {{1, 1}};

DEFINE_VEC_OPS(2)
DEFINE_VEC_CONST(3, 2)
DEFINE_VEC_CONST(4, 2)

#undef DEFINE_VEC_CONST
#undef DEFINE_VEC_OPS
