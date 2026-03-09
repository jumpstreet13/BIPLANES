#ifndef BYPLANES_BACKGROUND_H
#define BYPLANES_BACKGROUND_H

#include "Sprite.h"

class Background {
public:
    void init(AAssetManager *assetManager, float aspect);
    void update(float dt);
    void draw(const Shader &shader) const;

private:
    Sprite bg_;         // background.png — full scene
    Sprite cloud1_;     // cloud.png — scrolls right
    Sprite cloud2_;     // cloud_opaque.png — scrolls left, lower
    Sprite barn_;       // barn.png — on the ground
    float cloud1X_ = -2.0f;
    float cloud2X_ = 2.0f;
    float cloud1HalfW_ = 0.f;
    float cloud2HalfW_ = 0.f;
    float aspect_ = 1.f;
};

#endif //BYPLANES_BACKGROUND_H
