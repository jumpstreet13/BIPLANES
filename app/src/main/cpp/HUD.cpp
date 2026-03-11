#include "HUD.h"
#include "GameConstants.h"

static constexpr float ZEPPELIN_SPEED = 0.6f;

void HUD::init(AAssetManager *assetManager, float aspect) {
    worldHalfW_ = worldHalfWidthForAspect(aspect);

    // Zeppelin sprite
    auto zepTex = TextureAsset::loadAsset(assetManager, "zeppelin.png");
    float zepW = 1.0f;
    float zepAspect = 27.f / 51.f;
    float zepH = zepW * zepAspect;
    zepHalfW_ = zepW;
    zeppelin_.init(zepTex, zepW, zepH);
    zeppelin_.y = 1.3f;
    zeppelinX_ = -worldHalfW_ - zepHalfW_;

    // Digit textures: blue for P1, red for P2 (200x40, 10 digits of 20x40 each)
    digitsBlue_ = TextureAsset::loadAsset(assetManager, "digits_blue.png");
    digitsRed_  = TextureAsset::loadAsset(assetManager, "digits_red.png");

    float digitW = 0.14f;
    float digitH = 0.26f;
    // P1 digits (blue)
    digitSprites_[0].init(digitsBlue_, digitW, digitH);
    digitSprites_[1].init(digitsBlue_, digitW, digitH);
    // P2 digits (red)
    digitSprites_[2].init(digitsRed_, digitW, digitH);
    digitSprites_[3].init(digitsRed_, digitW, digitH);
}

void HUD::update(float dt) {
    zeppelinX_ += ZEPPELIN_SPEED * dt;
    if (zeppelinX_ > worldHalfW_ + zepHalfW_)
        zeppelinX_ = -worldHalfW_ - zepHalfW_;
    zeppelin_.x = zeppelinX_;

    // Position digits on the zeppelin white panel
    float zx = zeppelinX_;
    float zy = zeppelin_.y + 0.01f;
    float digitGap = 0.22f;    // spacing between digit centers within a pair
    float blueOff  = 0.18f;   // blue group center offset left of zeppelin center
    float redOff   = 0.38f;   // red group center offset right of zeppelin center

    int p1t = p1Score_ / 10;
    int p1o = p1Score_ % 10;
    int p2t = p2Score_ / 10;
    int p2o = p2Score_ % 10;

    // Each digit in the 200x40 strip is 20px wide → UV 0.1 per digit
    // P1 score (left pair, blue)
    digitSprites_[0].x = zx - blueOff - digitGap * 0.5f;
    digitSprites_[0].y = zy;
    digitSprites_[0].uvLeft = p1t * 0.1f;
    digitSprites_[0].uvRight = (p1t + 1) * 0.1f;

    digitSprites_[1].x = zx - blueOff + digitGap * 0.5f;
    digitSprites_[1].y = zy;
    digitSprites_[1].uvLeft = p1o * 0.1f;
    digitSprites_[1].uvRight = (p1o + 1) * 0.1f;

    // P2 score (right pair, red)
    digitSprites_[2].x = zx + redOff - digitGap * 0.5f;
    digitSprites_[2].y = zy;
    digitSprites_[2].uvLeft = p2t * 0.1f;
    digitSprites_[2].uvRight = (p2t + 1) * 0.1f;

    digitSprites_[3].x = zx + redOff + digitGap * 0.5f;
    digitSprites_[3].y = zy;
    digitSprites_[3].uvLeft = p2o * 0.1f;
    digitSprites_[3].uvRight = (p2o + 1) * 0.1f;
}

void HUD::setScores(int p1, int p2) {
    p1Score_ = p1;
    p2Score_ = p2;
}

void HUD::draw(const Shader &shader) const {
    zeppelin_.draw(shader);
    for (int i = 0; i < 4; i++) {
        digitSprites_[i].draw(shader);
    }
}
