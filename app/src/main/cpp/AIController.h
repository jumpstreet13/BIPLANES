#ifndef BYPLANES_AICONTROLLER_H
#define BYPLANES_AICONTROLLER_H

#include "GameConstants.h"

struct Plane;

struct AIOutput {
    bool thrustUp = false;
    bool thrustDown = false;
    bool fire = false;
};

class AIController {
public:
    AIOutput update(float dt, const Plane &aiPlane, const Plane &playerPlane);

private:
    float fireTimer_ = AI_FIRE_INTERVAL;
};

#endif //BYPLANES_AICONTROLLER_H
