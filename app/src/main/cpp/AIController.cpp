#include "AIController.h"
#include "Plane.h"

#include <cmath>

namespace {
struct AIProfile {
    float turnThreshold;
    float fireInterval;
    float fireAimTolerance;
    float leadTime;
    float groundMargin;
};

float normalizeAngle(float angle) {
    while (angle >  (float)M_PI) angle -= 2.f * (float)M_PI;
    while (angle < -(float)M_PI) angle += 2.f * (float)M_PI;
    return angle;
}

float wrapWorldX(float x) {
    while (x >  WORLD_HALF_W) x -= 2.f * WORLD_HALF_W;
    while (x < -WORLD_HALF_W) x += 2.f * WORLD_HALF_W;
    return x;
}

float shortestWorldDx(float fromX, float toX) {
    float dx = toX - fromX;
    while (dx >  WORLD_HALF_W) dx -= 2.f * WORLD_HALF_W;
    while (dx < -WORLD_HALF_W) dx += 2.f * WORLD_HALF_W;
    return dx;
}

AIProfile getProfile(AiDifficulty difficulty) {
    switch (difficulty) {
        case AiDifficulty::Easy:
            return AIProfile{0.22f, 2.10f, 0.20f, 0.00f, 0.35f};
        case AiDifficulty::Medium:
            return AIProfile{0.14f, 1.45f, 0.34f, 0.22f, 0.50f};
        case AiDifficulty::Hard:
            return AIProfile{0.07f, 0.95f, 0.48f, 0.45f, 0.75f};
    }
    return AIProfile{0.14f, 1.45f, 0.34f, 0.22f, 0.50f};
}
}

void AIController::setDifficulty(AiDifficulty difficulty) {
    difficulty_ = difficulty;
    reset();
}

void AIController::reset() {
    fireTimer_ = getProfile(difficulty_).fireInterval;
}

AIOutput AIController::update(float dt, const Plane &aiPlane, const Plane &playerPlane) {
    AIOutput output;
    const AIProfile profile = getProfile(difficulty_);

    if (aiPlane.exploding || !aiPlane.isAlive) return output;

    float playerVx = 0.f;
    float playerVy = 0.f;
    if (playerPlane.isAlive && !playerPlane.exploding) {
        if (playerPlane.grounded) {
            playerVx = playerPlane.groundSpeed;
        } else {
            playerVx = cosf(playerPlane.angle) * PLANE_SPEED;
            playerVy = sinf(playerPlane.angle) * PLANE_SPEED;
        }
    }

    float targetX = wrapWorldX(playerPlane.x + playerVx * profile.leadTime);
    float targetY = playerPlane.y + playerVy * profile.leadTime;
    const float minTargetY = GROUND_Y + PLANE_HALF_H;
    const float maxTargetY = WORLD_HALF_H - PLANE_HALF_H;
    if (targetY < minTargetY) targetY = minTargetY;
    if (targetY > maxTargetY) targetY = maxTargetY;

    const float dx = shortestWorldDx(aiPlane.x, targetX);
    const float dy = targetY - aiPlane.y;
    const float desiredAngle = atan2f(dy, dx);
    float diff = normalizeAngle(desiredAngle - aiPlane.angle);

    // Pull out of the ground earlier on higher difficulties so the AI survives longer.
    const float groundMargin = GROUND_Y + profile.groundMargin;
    if (aiPlane.y < groundMargin && sinf(aiPlane.angle) < 0.f) {
        diff = normalizeAngle((float)M_PI / 2.f - aiPlane.angle);
    }

    if (diff > profile.turnThreshold) {
        output.thrustUp = true;
    } else if (diff < -profile.turnThreshold) {
        output.thrustDown = true;
    }

    fireTimer_ -= dt;
    if (fireTimer_ <= 0.f) {
        const float aimDiff = normalizeAngle(desiredAngle - aiPlane.angle);
        if (fabsf(aimDiff) < profile.fireAimTolerance) {
            output.fire = true;
        }
        fireTimer_ = profile.fireInterval;
    }

    return output;
}
