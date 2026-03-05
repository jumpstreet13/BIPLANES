#include "AIController.h"
#include "Plane.h"
#include <cmath>

AIOutput AIController::update(float dt, const Plane &aiPlane, const Plane &playerPlane) {
    AIOutput output;

    if (aiPlane.exploding || !aiPlane.isAlive) return output;

    // Calculate desired angle to face the player
    float dx = playerPlane.x - aiPlane.x;
    float dy = playerPlane.y - aiPlane.y;
    float desiredAngle = atan2f(dy, dx);

    // Normalize angle difference to [-PI, PI]
    float diff = desiredAngle - aiPlane.angle;
    while (diff >  (float)M_PI) diff -= 2.f * (float)M_PI;
    while (diff < -(float)M_PI) diff += 2.f * (float)M_PI;

    // Ground avoidance — override if flying toward the ground
    float groundMargin = GROUND_Y + 0.5f;  // start pulling up when close
    if (aiPlane.y < groundMargin && sinf(aiPlane.angle) < 0.f) {
        // Flying downward near ground — pull up hard
        float upAngle = (float)M_PI / 2.f;  // straight up (+Y)
        float diffUp = upAngle - aiPlane.angle;
        while (diffUp >  (float)M_PI) diffUp -= 2.f * (float)M_PI;
        while (diffUp < -(float)M_PI) diffUp += 2.f * (float)M_PI;
        diff = diffUp;
    }

    // Rotate toward target
    float threshold = 0.15f;
    if (diff > threshold) {
        output.thrustUp = true;
    } else if (diff < -threshold) {
        output.thrustDown = true;
    }

    // Fire timer — only fire when roughly facing the player
    fireTimer_ -= dt;
    if (fireTimer_ <= 0.f) {
        float aimDiff = desiredAngle - aiPlane.angle;
        while (aimDiff >  (float)M_PI) aimDiff -= 2.f * (float)M_PI;
        while (aimDiff < -(float)M_PI) aimDiff += 2.f * (float)M_PI;
        if (fabsf(aimDiff) < 0.5f) {
            output.fire = true;
        }
        fireTimer_ = AI_FIRE_INTERVAL;
    }

    return output;
}
