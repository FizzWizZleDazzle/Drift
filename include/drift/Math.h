#pragma once

#include <cmath>

namespace drift {

// Column-major 3x3 affine matrix for 2D transforms.
// Layout: | m[0] m[3] m[6] |   =   | sx  -sin  tx |
//         | m[1] m[4] m[7] |       | sin  sy   ty |
//         | m[2] m[5] m[8] |       |  0    0    1 |
struct Mat3 {
    float m[9] = {};

    static Mat3 identity() {
        Mat3 r;
        r.m[0] = 1.f; r.m[4] = 1.f; r.m[8] = 1.f;
        return r;
    }

    static Mat3 translate(float tx, float ty) {
        Mat3 r = identity();
        r.m[6] = tx;
        r.m[7] = ty;
        return r;
    }

    static Mat3 rotate(float radians) {
        Mat3 r = identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[0] =  c; r.m[3] = -s;
        r.m[1] =  s; r.m[4] =  c;
        return r;
    }

    static Mat3 scale(float sx, float sy) {
        Mat3 r = identity();
        r.m[0] = sx;
        r.m[4] = sy;
        return r;
    }

    // Orthographic: maps (0..w, 0..h) -> (-1..1, -1..1), Y-down.
    static Mat3 ortho(float w, float h) {
        Mat3 r;
        r.m[0] =  2.f / w;
        r.m[4] = -2.f / h;
        r.m[6] = -1.f;
        r.m[7] =  1.f;
        r.m[8] =  1.f;
        return r;
    }

    Mat3 operator*(const Mat3& b) const {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row) {
                float sum = 0.f;
                for (int k = 0; k < 3; ++k)
                    sum += m[k * 3 + row] * b.m[c * 3 + k];
                r.m[c * 3 + row] = sum;
            }
        return r;
    }

    void transformPoint(float x, float y, float* ox, float* oy) const {
        *ox = m[0] * x + m[3] * y + m[6];
        *oy = m[1] * x + m[4] * y + m[7];
    }

    void transformDir(float x, float y, float* ox, float* oy) const {
        *ox = m[0] * x + m[3] * y;
        *oy = m[1] * x + m[4] * y;
    }

    Mat3 inverse() const {
        float a = m[0], b = m[3];
        float c = m[1], d = m[4];
        float tx = m[6], ty = m[7];

        float det = a * d - b * c;
        if (det > -1e-8f && det < 1e-8f) return identity();

        float inv = 1.f / det;
        Mat3 r;
        r.m[0] =  d * inv;
        r.m[1] = -c * inv;
        r.m[3] = -b * inv;
        r.m[4] =  a * inv;
        r.m[6] = (b * ty - d * tx) * inv;
        r.m[7] = (c * tx - a * ty) * inv;
        r.m[8] = 1.f;
        return r;
    }
};

} // namespace drift
