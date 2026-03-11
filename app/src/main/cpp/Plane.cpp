#include "Plane.h"
#include <algorithm>
#include <cmath>

namespace {
float planeHalfExtentX(float angle) {
    return fabsf(cosf(angle)) * PLANE_HALF_W + fabsf(sinf(angle)) * PLANE_HALF_H;
}

void drawExplosionEffect(
    const Shader &shader,
    const std::shared_ptr<TextureAsset> (&explosionFrames)[EXPLOSION_FRAMES],
    float timer,
    float x,
    float y
) {
    if (timer <= 0.f) return;
    int frame = (int)((1.f - timer / EXPLOSION_DURATION) * EXPLOSION_FRAMES);
    frame = std::clamp(frame, 0, EXPLOSION_FRAMES - 1);
    if (!explosionFrames[frame]) return;

    Sprite expl;
    expl.init(explosionFrames[frame], PLANE_HALF_W * 1.8f, PLANE_HALF_H * 1.8f);
    expl.x = x;
    expl.y = y;
    expl.draw(shader);
}
}

// Smooth orientation: no flip, just continuous rotation.
// P1 sprite faces right → rotation = angle.
// P2 sprite faces left  → rotation = angle − π (nose at -X needs inverted rotation).
static void updateSpriteOrientation(Sprite &spr, float angle, bool spriteFacesLeft) {
    spr.flipX = false;
    spr.rotation = spriteFacesLeft ? (angle - (float)M_PI) : angle;
}

static void drawPlaneSprite(
    const Plane &plane,
    const Shader &shader,
    const Sprite &planeSprite,
    float renderX,
    float renderY
) {
    if (plane.exploding) {
        drawExplosionEffect(shader, plane.explosionFrames, plane.explosionTimer, renderX, renderY);
        return;
    }
    if (!plane.isAlive) return;

    // Blink during invulnerability
    if (plane.respawnTimer > 0.f) {
        int blink = (int)(plane.respawnTimer * 10.f);
        if (blink % 2 == 0) return;
    }

    planeSprite.draw(shader);

    if (plane.predictedExplosionTimer > 0.f) {
        drawExplosionEffect(shader, plane.explosionFrames, plane.predictedExplosionTimer, renderX, renderY);
    }

    // Damage effects
    int damage = PLANE_MAX_HP - plane.hp;

    if (damage >= 1 && plane.smokeTex) {
        // Animated smoke from plane center (5-frame sprite sheet, each 13px wide in 65px strip)
        int frame = ((int)(plane.damageEffectTimer * Plane::SMOKE_ANIM_SPEED)) % Plane::SMOKE_FRAMES;
        float uvW = 1.f / (float)Plane::SMOKE_FRAMES;
        Sprite sm;
        sm.init(plane.smokeTex, 0.15f, 0.15f);
        sm.x = renderX;
        sm.y = renderY;
        sm.uvLeft  = frame * uvW;
        sm.uvRight = (frame + 1) * uvW;
        sm.draw(shader);
    }

    if (damage >= 2 && plane.fireTex) {
        // Animated fire from plane center (3-frame sprite sheet)
        int frame = ((int)(plane.damageEffectTimer * Plane::FIRE_ANIM_SPEED)) % Plane::FIRE_FRAMES;
        float uvW = 1.f / (float)Plane::FIRE_FRAMES;
        Sprite fi;
        fi.init(plane.fireTex, 0.15f, 0.15f);
        fi.x = renderX;
        fi.y = renderY;
        fi.uvLeft  = frame * uvW;
        fi.uvRight = (frame + 1) * uvW;
        fi.draw(shader);
    }

    if (plane.impactEffectTimer > 0.f && plane.sparkTex) {
        Sprite spark;
        spark.init(plane.sparkTex, 0.16f, 0.16f);
        spark.x = plane.impactEffectX;
        spark.y = plane.impactEffectY;
        spark.tintA = plane.impactEffectTimer / Plane::IMPACT_EFFECT_DURATION;
        spark.draw(shader);
    }
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
    lastResolvedProjectileId = 0;
    grounded = false;
    groundSpeed = 0.f;
    exploding = false;
    explosionTimer = 0.f;
    respawnTimer = 0.f;
    damageEffectTimer = 0.f;
    predictedExplosionTimer = 0.f;
    impactEffectTimer = 0.f;
    impactEffectX = 0.f;
    impactEffectY = 0.f;
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
    angle = spawnFacingLeft ? (float)M_PI : 0.f;
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
            if (respawnOnGround) {
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

        if (thrustUp)   angle += ROTATION_SPEED * dt;
        if (thrustDown) angle -= ROTATION_SPEED * dt;

        if (spawnFacingLeft) {
            if (angle > (float)M_PI) angle = (float)M_PI;
            if (angle < (float)M_PI * 0.6f) angle = (float)M_PI * 0.6f;
        } else {
            if (angle < 0.f) angle = 0.f;
            if (angle > (float)M_PI * 0.4f) angle = (float)M_PI * 0.4f;
        }

        x += cosf(angle) * groundSpeed * dt;
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
    lastResolvedProjectileId = 0;
    grounded = false;
    groundSpeed = 0.f;
    exploding = false;
    explosionTimer = 0.f;
    damageEffectTimer = 0.f;
    predictedExplosionTimer = 0.f;
    impactEffectTimer = 0.f;
    impactEffectX = 0.f;
    impactEffectY = 0.f;
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
    predictedExplosionTimer = 0.f;
    impactEffectTimer = 0.f;
    respawnTimer = 0.f;
}

void Plane::triggerImpactEffect(float worldX, float worldY) {
    impactEffectTimer = IMPACT_EFFECT_DURATION;
    impactEffectX = worldX;
    impactEffectY = worldY;
}

void Plane::triggerPredictedExplosion() {
    if (exploding) return;
    predictedExplosionTimer = EXPLOSION_DURATION;
}

void Plane::updatePredictedEffects(float dt) {
    if (predictedExplosionTimer > 0.f) {
        predictedExplosionTimer -= dt;
        if (predictedExplosionTimer < 0.f) predictedExplosionTimer = 0.f;
    }
    if (impactEffectTimer > 0.f) {
        impactEffectTimer -= dt;
        if (impactEffectTimer < 0.f) impactEffectTimer = 0.f;
    }
}

void Plane::draw(const Shader& shader) const {
    drawPlaneSprite(*this, shader, sprite, sprite.x, sprite.y);
}

void Plane::drawAt(const Shader &shader, float renderX, float renderY, float renderAngle) const {
    Sprite renderSprite = sprite;
    renderSprite.x = renderX;
    renderSprite.y = renderY;
    updateSpriteOrientation(renderSprite, renderAngle, spawnFacingLeft);
    drawPlaneSprite(*this, shader, renderSprite, renderX, renderY);
}
