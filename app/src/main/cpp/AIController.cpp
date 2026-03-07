#include "AIController.h"
#include "Plane.h"

#include <cmath>

namespace {
struct AIProfile {
    float turnThreshold;
    float fireInterval;
    float fireAimTolerance;
    float leadTime;
    float minCruiseMargin;
    float recoverMargin;
    float houseSafeMargin;
    float houseDangerRadius;
    float lookAheadTime;
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
            return AIProfile{0.22f, 2.10f, 0.20f, 0.00f, 0.62f, 0.85f, 0.92f, 1.10f, 0.55f};
        case AiDifficulty::Medium:
            return AIProfile{0.14f, 1.45f, 0.34f, 0.22f, 0.72f, 0.95f, 1.00f, 1.25f, 0.62f};
        case AiDifficulty::Hard:
            return AIProfile{0.07f, 0.95f, 0.48f, 0.45f, 0.82f, 1.05f, 1.08f, 1.40f, 0.70f};
    }
    return AIProfile{0.14f, 1.45f, 0.34f, 0.22f, 0.72f, 0.95f, 1.00f, 1.25f, 0.62f};
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
    const float minTargetY = GROUND_Y + profile.minCruiseMargin;
    const float maxTargetY = WORLD_HALF_H - PLANE_HALF_H;
    if (targetY < minTargetY) targetY = minTargetY;
    if (targetY > maxTargetY) targetY = maxTargetY;

    const float dx = shortestWorldDx(aiPlane.x, targetX);
    const float dy = targetY - aiPlane.y;
    const float desiredAngle = atan2f(dy, dx);
    float diff = normalizeAngle(desiredAngle - aiPlane.angle);

    const float recoveryY = GROUND_Y + profile.recoverMargin;
    const float predictedY = aiPlane.y + sinf(aiPlane.angle) * PLANE_SPEED * profile.lookAheadTime;
    const float predictedX = wrapWorldX(aiPlane.x + cosf(aiPlane.angle) * PLANE_SPEED * profile.lookAheadTime);
    const float houseSafeY = GROUND_Y + profile.houseSafeMargin;
    const float houseDxNow = fabsf(shortestWorldDx(aiPlane.x, HOUSE_X));
    const float houseDxSoon = fabsf(shortestWorldDx(predictedX, HOUSE_X));

    const bool needsGroundRecovery =
            (aiPlane.y < recoveryY && sinf(aiPlane.angle) < 0.20f)
            || (predictedY < recoveryY);
    const bool nearHouseAtLowAltitude =
            (houseDxNow < profile.houseDangerRadius || houseDxSoon < profile.houseDangerRadius)
            && (aiPlane.y < houseSafeY || predictedY < houseSafeY);

    if (needsGroundRecovery || nearHouseAtLowAltitude) {
        const float escapeAngle = (cosf(aiPlane.angle) < 0.f)
                                  ? ((float)M_PI * 0.82f)
                                  : ((float)M_PI * 0.28f);
        diff = normalizeAngle(escapeAngle - aiPlane.angle);
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
