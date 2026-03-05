#include "Utility.h"
#include "AndroidOut.h"

#include <GLES3/gl3.h>
#include <cmath>

#define CHECK_ERROR(e) case e: aout << "GL Error: "#e << std::endl; break;

bool Utility::checkAndLogGlError(bool alwaysLog) {
    GLenum error = glGetError();
    if (error == GL_NO_ERROR) {
        if (alwaysLog) {
            aout << "No GL error" << std::endl;
        }
        return true;
    } else {
        switch (error) {
            CHECK_ERROR(GL_INVALID_ENUM);
            CHECK_ERROR(GL_INVALID_VALUE);
            CHECK_ERROR(GL_INVALID_OPERATION);
            CHECK_ERROR(GL_INVALID_FRAMEBUFFER_OPERATION);
            CHECK_ERROR(GL_OUT_OF_MEMORY);
            default:
                aout << "Unknown GL error: " << error << std::endl;
        }
        return false;
    }
}

float *
Utility::buildOrthographicMatrix(float *outMatrix, float halfHeight, float aspect, float near,
                                 float far) {
    float halfWidth = halfHeight * aspect;

    // column 1
    outMatrix[0] = 1.f / halfWidth;
    outMatrix[1] = 0.f;
    outMatrix[2] = 0.f;
    outMatrix[3] = 0.f;

    // column 2
    outMatrix[4] = 0.f;
    outMatrix[5] = 1.f / halfHeight;
    outMatrix[6] = 0.f;
    outMatrix[7] = 0.f;

    // column 3
    outMatrix[8] = 0.f;
    outMatrix[9] = 0.f;
    outMatrix[10] = -2.f / (far - near);
    outMatrix[11] = -(far + near) / (far - near);

    // column 4
    outMatrix[12] = 0.f;
    outMatrix[13] = 0.f;
    outMatrix[14] = 0.f;
    outMatrix[15] = 1.f;

    return outMatrix;
}

void Utility::buildTranslationScaleMatrix(float *out16, float x, float y, float scaleX, float scaleY) {
    // Column-major 4x4 matrix: scale then translate
    // column 1
    out16[0] = scaleX;
    out16[1] = 0.f;
    out16[2] = 0.f;
    out16[3] = 0.f;

    // column 2
    out16[4] = 0.f;
    out16[5] = scaleY;
    out16[6] = 0.f;
    out16[7] = 0.f;

    // column 3
    out16[8] = 0.f;
    out16[9] = 0.f;
    out16[10] = 1.f;
    out16[11] = 0.f;

    // column 4
    out16[12] = x;
    out16[13] = y;
    out16[14] = 0.f;
    out16[15] = 1.f;
}

void Utility::buildTRSMatrix(float *out16, float x, float y, float angle, float scaleX, float scaleY) {
    float c = cosf(angle);
    float s = sinf(angle);
    // Column-major 4x4: Translation * Rotation * Scale
    out16[0]  = scaleX * c;    out16[1]  = scaleX * s;    out16[2]  = 0.f; out16[3]  = 0.f;
    out16[4]  = -scaleY * s;   out16[5]  = scaleY * c;    out16[6]  = 0.f; out16[7]  = 0.f;
    out16[8]  = 0.f;            out16[9]  = 0.f;            out16[10] = 1.f; out16[11] = 0.f;
    out16[12] = x;              out16[13] = y;              out16[14] = 0.f; out16[15] = 1.f;
}

bool Utility::aabbOverlap(float ax, float ay, float aw, float ah,
                           float bx, float by, float bw, float bh) {
    return (ax - aw < bx + bw) && (ax + aw > bx - bw) &&
           (ay - ah < by + bh) && (ay + ah > by - bh);
}

void Utility::screenToWorld(float sx, float sy, float screenW, float screenH,
                            float halfH, float aspect, float &wx, float &wy) {
    float halfW = halfH * aspect;
    wx = (sx / screenW - 0.5f) * 2.f * halfW;
    wy = (0.5f - sy / screenH) * 2.f * halfH;
}

float *Utility::buildIdentityMatrix(float *outMatrix) {
    // column 1
    outMatrix[0] = 1.f;
    outMatrix[1] = 0.f;
    outMatrix[2] = 0.f;
    outMatrix[3] = 0.f;

    // column 2
    outMatrix[4] = 0.f;
    outMatrix[5] = 1.f;
    outMatrix[6] = 0.f;
    outMatrix[7] = 0.f;

    // column 3
    outMatrix[8] = 0.f;
    outMatrix[9] = 0.f;
    outMatrix[10] = 1.f;
    outMatrix[11] = 0.f;

    // column 4
    outMatrix[12] = 0.f;
    outMatrix[13] = 0.f;
    outMatrix[14] = 0.f;
    outMatrix[15] = 1.f;

    return outMatrix;
}