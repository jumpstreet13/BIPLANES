#ifndef BIPLANES_PROJECTILE_H
#define BIPLANES_PROJECTILE_H

#include <array>
#include <cstdint>
#include "Sprite.h"
#include "GameConstants.h"

struct Projectile {
    uint16_t id = 0;
    float x = 0.f;
    float y = 0.f;
    float velX = 0.f;
    float velY = 0.f;
    float lifetime = 0.f;
    uint16_t spawnedByInputSequence = 0;
    bool predictedRemoteImpact = false;
    bool active = false;
    Sprite sprite;
};

class ProjectilePool {
public:
    void init(std::shared_ptr<TextureAsset> texture, float worldHalfW);
    void spawn(float x, float y, float velX, uint16_t spawnedByInputSequence = 0, uint16_t projectileId = 0);
    void spawnDirectional(float x, float y, float velX, float velY, uint16_t spawnedByInputSequence = 0, uint16_t projectileId = 0);
    void updateAll(float dt);
    void drawAll(const Shader &shader) const;
    std::array<Projectile, MAX_PROJECTILES> &getProjectiles() { return projectiles_; }
    const std::array<Projectile, MAX_PROJECTILES> &getProjectiles() const { return projectiles_; }

private:
    std::array<Projectile, MAX_PROJECTILES> projectiles_;
    std::shared_ptr<TextureAsset> texture_;
    float worldHalfW_ = WORLD_HALF_W;
};

#endif //BIPLANES_PROJECTILE_H
