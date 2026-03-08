#include "Plane.h"
#include <algorithm>
#include <cmath>

namespace {
float planeHalfExtentX(float angle) {
    return fabsf(cosf(angle)) * PLANE_HALF_W + fabsf(sinf(angle)) * PLANE_HALF_H;
}
}

// Smooth orientation: no flip, just continuous rotation.
// P1 sprite faces right → rotation = angle.
// P2 sprite faces left  → rotation = angle − π (nose at -X needs inverted rotation).
static void updateSpriteOrientation(Sprite &spr, float angle, bool spriteFacesLeft) {
    spr.flipX = false;
    spr.rotation = spriteFacesLeft ? (angle - (float)M_PI) : angle;
}

void Plane::init(std::shared_ptr<TextureAsset> texture,
                 std::shared_ptr<TextureAsset> exFrames[EXPLOSION_FRAMES],
                 std::shared_ptr<TextureAsset> smoke,
                 std::shared_ptr<TextureAsset> fire,
                 std::shared_ptr<TextureAsset> spark,
                 float startX, bool facingLeft, float worldHalfW) {
    spawnX = startX;
    spawnFacingLeft = facingLeft;
    this->worldHalfW = worldHalfW;
    x = startX;
    y = 0.f;
    angle = facingLeft ? (float)M_PI : 0.f;
    isAlive = true;
    score = 0;
    hp = PLANE_MAX_HP;
    grounded = false;
    groundSpeed = 0.f;
    exploding = false;
    explosionTimer = 0.f;
    respawnTimer = 0.f;
    damageEffectTimer = 0.f;
    smokeTex = std::move(smoke);
    fireTex = std::move(fire);
    sparkTex = std::move(spark);
    sprite.init(std::move(texture), PLANE_HALF_W, PLANE_HALF_H);
    updateSpriteOrientation(sprite, angle, facingLeft);
    for (int i = 0; i < EXPLOSION_FRAMES; i++) {
        explosionFrames[i] = exFrames ? exFrames[i] : nullptr;
    }
}

void Plane::setGrounded() {
    grounded = true;
    groundSpeed = 0.f;
    y = GROUND_Y + PLANE_HALF_H + 0.02f;
    angle = 0.f;  // facing right
    sprite.x = x;
    sprite.y = y;
    updateSpriteOrientation(sprite, angle, spawnFacingLeft);
}

void Plane::update(float dt, bool thrustUp, bool thrustDown) {
    if (exploding) {
        explosionTimer -= dt;
        if (explosionTimer <= 0.f) {
            reset(spawnX, spawnFacingLeft);
            respawnTimer = RESPAWN_INVULN;
            // Player respawns on ground
            if (!spawnFacingLeft) {
                setGrounded();
                x = spawnX;
            }
        }
        return;
    }
    if (!isAlive) return;

    // Tick down invulnerability
    if (respawnTimer > 0.f) {
        respawnTimer -= dt;
        if (respawnTimer < 0.f) respawnTimer = 0.f;
    }

    damageEffectTimer += dt;

    // === Grounded / takeoff physics ===
    if (grounded) {
        groundSpeed += TAKEOFF_ACCEL * dt;
        if (groundSpeed > PLANE_SPEED) groundSpeed = PLANE_SPEED;

        // Either button angles nose UP (positive angle = up)
        if (thrustUp || thrustDown) angle += ROTATION_SPEED * dt;
        if (angle < 0.f) angle = 0.f;
        if (angle > (float)M_PI * 0.4f) angle = (float)M_PI * 0.4f;

        x += groundSpeed * dt;
        x = Utility::wrapWorldX(x, worldHalfW, planeHalfExtentX(angle));

        if (sinf(angle) > TAKEOFF_LIFTOFF && groundSpeed > PLANE_SPEED * 0.5f) {
            grounded = false;
        } else {
            y = GROUND_Y + PLANE_HALF_H + 0.02f;
        }

        sprite.x = x;
        sprite.y = y;
        updateSpriteOrientation(sprite, angle, spawnFacingLeft);
        return;
    }

    // === Normal flight physics ===
    if (thrustUp)   angle += ROTATION_SPEED * dt;
    if (thrustDown) angle -= ROTATION_SPEED * dt;

    // Normalize angle to [-π, π]
    while (angle > (float)M_PI) angle -= 2.f * (float)M_PI;
    while (angle < -(float)M_PI) angle += 2.f * (float)M_PI;

    x += cosf(angle) * PLANE_SPEED * dt;
    y += sinf(angle) * PLANE_SPEED * dt;

    // Clamp at ceiling
    if (y > WORLD_HALF_H - PLANE_HALF_H) {
        y = WORLD_HALF_H - PLANE_HALF_H;
    }

    // Wrap left/right
    x = Utility::wrapWorldX(x, worldHalfW, planeHalfExtentX(angle));

    sprite.x = x;
    sprite.y = y;
    updateSpriteOrientation(sprite, angle, spawnFacingLeft);
}

void Plane::reset(float startX, bool facingLeft) {
    spawnX = startX;
    spawnFacingLeft = facingLeft;
    x = startX;
    y = 0.f;
    angle = facingLeft ? (float)M_PI : 0.f;
    isAlive = true;
    hp = PLANE_MAX_HP;
    grounded = false;
    groundSpeed = 0.f;
    exploding = false;
    explosionTimer = 0.f;
    damageEffectTimer = 0.f;
    sprite.x = x;
    sprite.y = y;
    updateSpriteOrientation(sprite, angle, facingLeft);
}

bool Plane::hitPlane() {
    hp--;
    if (hp <= 0) {
        triggerExplosion();
        return true;
    }
    return false;
}

void Plane::triggerExplosion() {
    isAlive = false;
    exploding = true;
    grounded = false;
    hp = 0;
    explosionTimer = EXPLOSION_DURATION;
    respawnTimer = 0.f;
}

void Plane::draw(const Shader& shader) const {
    if (exploding) {
        int frame = (int)((1.f - explosionTimer / EXPLOSION_DURATION) * EXPLOSION_FRAMES);
        frame = std::min(frame, EXPLOSION_FRAMES - 1);
        if (explosionFrames[frame]) {
            Sprite expl;
            expl.init(explosionFrames[frame], PLANE_HALF_W * 1.8f, PLANE_HALF_H * 1.8f);
            expl.x = x;
            expl.y = y;
            expl.draw(shader);
        }
        return;
    }
    if (!isAlive) return;

    // Blink during invulnerability
    if (respawnTimer > 0.f) {
        int blink = (int)(respawnTimer * 10.f);
        if (blink % 2 == 0) return;
    }

    sprite.draw(shader);

    // Damage effects
    int damage = PLANE_MAX_HP - hp;

    if (damage >= 1 && smokeTex) {
        // Animated smoke from plane center (5-frame sprite sheet, each 13px wide in 65px strip)
        int frame = ((int)(damageEffectTimer * SMOKE_ANIM_SPEED)) % SMOKE_FRAMES;
        float uvW = 1.f / (float)SMOKE_FRAMES; // 0.2 per frame
        Sprite sm;
        sm.init(smokeTex, 0.15f, 0.15f);
        sm.x = x;
        sm.y = y;
        sm.uvLeft  = frame * uvW;
        sm.uvRight = (frame + 1) * uvW;
        sm.draw(shader);
    }

    if (damage >= 2 && fireTex) {
        // Animated fire from plane center (3-frame sprite sheet)
        int frame = ((int)(damageEffectTimer * FIRE_ANIM_SPEED)) % FIRE_FRAMES;
        float uvW = 1.f / (float)FIRE_FRAMES;
        Sprite fi;
        fi.init(fireTex, 0.15f, 0.15f);
        fi.x = x;
        fi.y = y;
        fi.uvLeft  = frame * uvW;
        fi.uvRight = (frame + 1) * uvW;
        fi.draw(shader);
    }
}
