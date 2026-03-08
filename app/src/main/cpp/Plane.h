#ifndef BYPLANES_PLANE_H
#define BYPLANES_PLANE_H

#include <memory>
#include <cmath>
#include "Sprite.h"
#include "GameConstants.h"

struct Plane {
    float x = 0.f;
    float y = 0.f;
    float angle = 0.f;    // heading in radians (0 = right, pi = left)
    bool isAlive = true;
    int score = 0;
    float spawnX = 0.f;
    bool spawnFacingLeft = false;
    bool respawnOnGround = false;
    float worldHalfW = WORLD_HALF_W;

    Sprite sprite;

    // Hit points (3-hit system)
    int hp = PLANE_MAX_HP;

    // Takeoff
    bool grounded = false;
    float groundSpeed = 0.f;

    // Explosion animation
    bool exploding = false;
    float explosionTimer = 0.f;
    std::shared_ptr<TextureAsset> explosionFrames[EXPLOSION_FRAMES];

    // Damage effect textures
    std::shared_ptr<TextureAsset> smokeTex;  // smoke_anim.png: 5 frames, 13x13 each
    std::shared_ptr<TextureAsset> fireTex;   // fire.png: 3 frames, 13x13 each
    std::shared_ptr<TextureAsset> sparkTex;
    float damageEffectTimer = 0.f;
    float predictedExplosionTimer = 0.f;
    float impactEffectTimer = 0.f;
    float impactEffectX = 0.f;
    float impactEffectY = 0.f;
    static constexpr int SMOKE_FRAMES = 5;
    static constexpr int FIRE_FRAMES = 3;
    static constexpr float SMOKE_ANIM_SPEED = 6.f;
    static constexpr float FIRE_ANIM_SPEED = 8.f;
    static constexpr float IMPACT_EFFECT_DURATION = 0.16f;

    // Respawn invulnerability
    float respawnTimer = 0.f;
    bool isInvulnerable() const { return respawnTimer > 0.f; }

    void init(std::shared_ptr<TextureAsset> texture,
              std::shared_ptr<TextureAsset> exFrames[EXPLOSION_FRAMES],
              std::shared_ptr<TextureAsset> smoke,
              std::shared_ptr<TextureAsset> fire,
              std::shared_ptr<TextureAsset> spark,
              float startX, bool facingLeft, float worldHalfW);
    void update(float dt, bool thrustUp, bool thrustDown);
    void reset(float startX, bool facingLeft);
    bool hitPlane();
    void triggerExplosion();
    void triggerImpactEffect(float worldX, float worldY);
    void triggerPredictedExplosion();
    void updatePredictedEffects(float dt);
    bool hasPredictedExplosion() const { return predictedExplosionTimer > 0.f; }
    void draw(const Shader& shader) const;

    // Set up for ground start (player only)
    void setGrounded();
};

#endif
