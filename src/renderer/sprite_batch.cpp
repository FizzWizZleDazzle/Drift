// =============================================================================
// Drift 2D Game Engine - Sprite Batch Helpers
// =============================================================================
// Auxiliary math utilities used by the renderer's sprite-batching pipeline.
// The core batching logic (vertex building, sorting, draw-call emission) lives
// in renderer.cpp.  This file exposes standalone 3x3 affine-matrix helpers
// that can also be used by other subsystems (e.g. UI, particles).
// =============================================================================

#include <drift/renderer.h>

#include <cmath>

// =============================================================================
// 3x3 column-major affine matrix helpers for 2D transforms
// =============================================================================
// Layout (column-major, like OpenGL):
//   | m[0]  m[3]  m[6] |       | sx   -sin  tx |
//   | m[1]  m[4]  m[7] |  -->  | sin   sy   ty |
//   | m[2]  m[5]  m[8] |       |  0     0    1 |
//
// These are free functions in the drift::math namespace so that any
// translation unit can use them without pulling in renderer internals.
// =============================================================================

namespace drift {
namespace math {

struct Mat3 {
    float m[9];
};

Mat3 mat3_identity() {
    Mat3 r{};
    r.m[0] = 1.0f;
    r.m[4] = 1.0f;
    r.m[8] = 1.0f;
    return r;
}

Mat3 mat3_multiply(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k) {
                sum += a.m[k * 3 + row] * b.m[col * 3 + k];
            }
            r.m[col * 3 + row] = sum;
        }
    }
    return r;
}

Mat3 mat3_translate(float tx, float ty) {
    Mat3 r = mat3_identity();
    r.m[6] = tx;
    r.m[7] = ty;
    return r;
}

Mat3 mat3_rotate(float radians) {
    Mat3 r = mat3_identity();
    float c = cosf(radians);
    float s = sinf(radians);
    r.m[0] =  c;
    r.m[1] =  s;
    r.m[3] = -s;
    r.m[4] =  c;
    return r;
}

Mat3 mat3_scale(float sx, float sy) {
    Mat3 r = mat3_identity();
    r.m[0] = sx;
    r.m[4] = sy;
    return r;
}

// Build an orthographic projection that maps pixel coordinates
// (0..width, 0..height) into normalised device coordinates (-1..1).
// Y is flipped so that the origin is at the top-left corner (screen
// convention).
Mat3 mat3_ortho(float width, float height) {
    Mat3 r{};
    // Zero-initialise, then fill the non-zero elements.
    r.m[0] =  2.0f / width;
    r.m[4] = -2.0f / height;   // flip Y
    r.m[6] = -1.0f;
    r.m[7] =  1.0f;
    r.m[8] =  1.0f;
    return r;
}

// Transform a 2D point by the matrix.  The implicit w component is 1.
void mat3_transform_point(const Mat3& m, float x, float y,
                          float* out_x, float* out_y) {
    *out_x = m.m[0] * x + m.m[3] * y + m.m[6];
    *out_y = m.m[1] * x + m.m[4] * y + m.m[7];
}

// Transform a 2D direction (ignores translation).
void mat3_transform_dir(const Mat3& m, float x, float y,
                        float* out_x, float* out_y) {
    *out_x = m.m[0] * x + m.m[3] * y;
    *out_y = m.m[1] * x + m.m[4] * y;
}

// Compute the inverse of an affine 3x3 matrix.  Returns identity if the
// matrix is singular (determinant ~ 0).
Mat3 mat3_inverse(const Mat3& m) {
    // For an affine matrix the bottom row is (0, 0, 1), so we only need
    // to invert the upper-left 2x2 and adjust the translation.
    float a = m.m[0], b = m.m[3];
    float c = m.m[1], d = m.m[4];
    float tx = m.m[6], ty = m.m[7];

    float det = a * d - b * c;
    if (det > -1e-8f && det < 1e-8f) {
        return mat3_identity();
    }

    float inv_det = 1.0f / det;

    Mat3 r{};
    r.m[0] =  d * inv_det;
    r.m[1] = -c * inv_det;
    r.m[3] = -b * inv_det;
    r.m[4] =  a * inv_det;
    r.m[6] = (b * ty - d * tx) * inv_det;
    r.m[7] = (c * tx - a * ty) * inv_det;
    r.m[8] = 1.0f;

    return r;
}

} // namespace math
} // namespace drift
