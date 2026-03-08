#ifndef BYPLANES_HUD_H
#define BYPLANES_HUD_H

#include <memory>
#include "Sprite.h"
#include "TextureAsset.h"
#include "GameConstants.h"

class HUD {
public:
    void init(AAssetManager *assetManager, float aspect);
    void update(float dt);
    void setScores(int p1, int p2);
    void draw(const Shader &shader) const;

private:
    Sprite zeppelin_;
    std::shared_ptr<TextureAsset> digitsBlue_;   // P1 digits (blue)
    std::shared_ptr<TextureAsset> digitsRed_;    // P2 digits (red)
    Sprite digitSprites_[4];  // p1 tens, p1 ones, p2 tens, p2 ones
    float zeppelinX_ = -5.f;
    float zepHalfW_ = 0.f;
    float worldHalfW_ = WORLD_HALF_W;
    int p1Score_ = 0;
    int p2Score_ = 0;
};

#endif //BYPLANES_HUD_H
