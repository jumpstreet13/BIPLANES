#ifndef BIPLANES_AICONTROLLER_H
#define BIPLANES_AICONTROLLER_H

#include "GameConstants.h"
#include "GameState.h"

struct Plane;

struct AIOutput {
    bool thrustUp = false;
    bool thrustDown = false;
    bool fire = false;
};

class AIController {
public:
    void setDifficulty(AiDifficulty difficulty);
    void reset();
    AIOutput update(float dt, const Plane &aiPlane, const Plane &playerPlane, float worldHalfW);

private:
    AiDifficulty difficulty_ = AiDifficulty::Medium;
    float fireTimer_ = AI_FIRE_INTERVAL;
};

#endif //BIPLANES_AICONTROLLER_H
