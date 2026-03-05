#include "Background.h"
#include "GameConstants.h"
#include <cmath>

void Background::init(AAssetManager *assetManager, float aspect) {
    aspect_ = aspect;

    // Main background fills the entire world
    auto bgTex = TextureAsset::loadAsset(assetManager, "background.png");
    float fullW = aspect * 2.0f;
    bg_.init(bgTex, fullW, WORLD_HALF_H);
    bg_.x = 0.f;
    bg_.y = 0.f;

    // Cloud 1 (cloud.png) — scrolls right
    auto cloudTex = TextureAsset::loadAsset(assetManager, "cloud.png");
    float cloudScale = 1.2f;
    float cloudAspect = 32.f / 69.f;
    cloud1HalfW_ = cloudScale;
    cloud1_.init(cloudTex, cloudScale, cloudScale * cloudAspect);
    cloud1_.y = 1.0f;
    cloud1X_ = -2.0f;

    // Cloud 2 (cloud_opaque.png) — scrolls left, slightly lower
    auto cloud2Tex = TextureAsset::loadAsset(assetManager, "cloud_opaque.png");
    cloud2HalfW_ = cloudScale;
    cloud2_.init(cloud2Tex, cloudScale, cloudScale * cloudAspect);
    cloud2_.y = 0.3f;
    cloud2X_ = 2.0f;

    // Barn — on the ground at HOUSE_X
    auto barnTex = TextureAsset::loadAsset(assetManager, "barn.png");
    float barnW = 0.45f;
    float barnAspect = 22.f / 35.f;
    float barnH = barnW * barnAspect;
    barn_.init(barnTex, barnW, barnH);
    barn_.x = HOUSE_X;
    barn_.y = GROUND_Y + barnH;
}

void Background::update(float dt) {
    // Cloud 1 moves right
    cloud1X_ += 0.5f * dt;
    if (cloud1X_ > WORLD_HALF_W + cloud1HalfW_)
        cloud1X_ = -WORLD_HALF_W - cloud1HalfW_;
    cloud1_.x = cloud1X_;

    // Cloud 2 moves left
    cloud2X_ -= 0.4f * dt;
    if (cloud2X_ < -WORLD_HALF_W - cloud2HalfW_)
        cloud2X_ = WORLD_HALF_W + cloud2HalfW_;
    cloud2_.x = cloud2X_;
}

void Background::draw(const Shader &shader) const {
    bg_.draw(shader);
    cloud1_.draw(shader);
    cloud2_.draw(shader);
    barn_.draw(shader);
}
