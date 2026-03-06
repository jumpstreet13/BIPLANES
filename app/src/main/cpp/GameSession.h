#ifndef BYPLANES_GAMESESSION_H
#define BYPLANES_GAMESESSION_H

#include "Plane.h"
#include "Projectile.h"
#include "Background.h"
#include "HUD.h"
#include "AIController.h"
#include "GameState.h"
#include "InputManager.h"
#include "SoundEngine.h"

class GameSession {
public:
    void init(AAssetManager *assetManager, float aspect, GameMode mode, AiDifficulty difficulty);
    bool update(float dt, const TouchState &input);
    void draw(const Shader &shader) const;
    int getWinner() const;
    void reset();

private:
    void checkCollisions();
    void playerFire();
    void enemyFire();

    Plane player_;
    Plane enemy_;
    ProjectilePool playerBullets_;
    ProjectilePool enemyBullets_;
    Background background_;
    HUD hud_;
    AIController ai_;
    SoundEngine sound_;
    GameMode mode_ = GameMode::VsAI;
    float aspect_ = 1.f;

    float playerFireCooldown_ = 0.f;
    float enemyFireCooldown_ = 0.f;
};

#endif //BYPLANES_GAMESESSION_H
