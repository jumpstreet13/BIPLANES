#include "GameSession.h"
#include "Utility.h"
#include <cmath>
#include <string>

void GameSession::init(AAssetManager *assetManager, float aspect, GameMode mode, AiDifficulty difficulty) {
    aspect_ = aspect;
    mode_ = mode;
    const float worldHalfW = worldHalfWidthForAspect(aspect);

    auto p1Tex = TextureAsset::loadAsset(assetManager, "plane_p1.png");
    auto p2Tex = TextureAsset::loadAsset(assetManager, "plane_p2.png");
    auto bulletTex = TextureAsset::loadAsset(assetManager, "bullet.png");

    std::shared_ptr<TextureAsset> explFrames[EXPLOSION_FRAMES];
    for (int i = 0; i < EXPLOSION_FRAMES; i++) {
        std::string fname = "explosion_" + std::to_string(i) + ".png";
        explFrames[i] = TextureAsset::loadAsset(assetManager, fname);
    }

    auto smokeTex = TextureAsset::loadAsset(assetManager, "smoke_anim.png");
    auto fireTex  = TextureAsset::loadAsset(assetManager, "fire.png");
    auto sparkTex = TextureAsset::loadAsset(assetManager, "spark.png");

    player_.init(p1Tex, explFrames, smokeTex, fireTex, sparkTex, -3.3f, false, worldHalfW);
    enemy_.init(p2Tex, explFrames, smokeTex, fireTex, sparkTex, 2.8f, true, worldHalfW);

    // Player starts on the ground
    player_.setGrounded();

    playerBullets_.init(bulletTex, worldHalfW);
    enemyBullets_.init(bulletTex, worldHalfW);

    background_.init(assetManager, aspect);
    hud_.init(assetManager, aspect);
    sound_.init(assetManager);
    ai_.setDifficulty(difficulty);

    playerFireCooldown_ = 0.f;
    enemyFireCooldown_ = 0.f;
}

bool GameSession::update(float dt, const TouchState &input) {
    if (playerFireCooldown_ > 0.f) playerFireCooldown_ -= dt;
    if (enemyFireCooldown_ > 0.f) enemyFireCooldown_ -= dt;

    // Update player
    player_.update(dt, input.upButtonHeld, input.downButtonHeld);

    // Player fire (only when airborne)
    if (input.fireTapped && playerFireCooldown_ <= 0.f
        && player_.isAlive && !player_.exploding && !player_.grounded) {
        playerFire();
        playerFireCooldown_ = FIRE_COOLDOWN;
    }

    // AI update
    if (mode_ == GameMode::VsAI) {
        AIOutput aiOut = ai_.update(dt, enemy_, player_, worldHalfWidthForAspect(aspect_));
        enemy_.update(dt, aiOut.thrustUp, aiOut.thrustDown);
        if (aiOut.fire && enemyFireCooldown_ <= 0.f
            && enemy_.isAlive && !enemy_.exploding) {
            enemyFire();
            enemyFireCooldown_ = FIRE_COOLDOWN;
        }
    }

    playerBullets_.updateAll(dt);
    enemyBullets_.updateAll(dt);

    background_.update(dt);

    checkCollisions();

    hud_.setScores(player_.score, enemy_.score);
    hud_.update(dt);

    bool won = (player_.score >= WIN_SCORE || enemy_.score >= WIN_SCORE);
    if (won) sound_.playVictory();
    return won;
}

void GameSession::draw(const Shader &shader) const {
    background_.draw(shader);
    hud_.draw(shader);           // zeppelin behind planes
    player_.draw(shader);
    enemy_.draw(shader);
    playerBullets_.drawAll(shader);
    enemyBullets_.drawAll(shader);
}

int GameSession::getWinner() const {
    if (player_.score >= WIN_SCORE) return 1;
    if (enemy_.score >= WIN_SCORE) return 2;
    return 0;
}

void GameSession::reset() {
    ai_.reset();

    player_.reset(-3.3f, false);
    player_.score = 0;
    player_.setGrounded();  // Player starts on ground

    enemy_.reset(2.8f, true);
    enemy_.score = 0;

    playerFireCooldown_ = 0.f;
    enemyFireCooldown_ = 0.f;
}

void GameSession::checkCollisions() {
    // Player bullets vs enemy
    if (enemy_.isAlive && !enemy_.exploding && !enemy_.isInvulnerable()) {
        for (auto &p : playerBullets_.getProjectiles()) {
            if (!p.active) continue;
            if (Utility::aabbOverlap(p.x, p.y, BULLET_HALF_SIZE, BULLET_HALF_SIZE,
                                     enemy_.x, enemy_.y, PLANE_HALF_W, PLANE_HALF_H)) {
                p.active = false;
                p.sprite.visible = false;
                if (enemy_.hitPlane()) {
                    player_.score++;
                    sound_.playExplosion();
                } else {
                    sound_.playHit();
                }
                break;
            }
        }
    }

    // Enemy bullets vs player
    if (player_.isAlive && !player_.exploding && !player_.isInvulnerable()) {
        for (auto &p : enemyBullets_.getProjectiles()) {
            if (!p.active) continue;
            if (Utility::aabbOverlap(p.x, p.y, BULLET_HALF_SIZE, BULLET_HALF_SIZE,
                                     player_.x, player_.y, PLANE_HALF_W, PLANE_HALF_H)) {
                p.active = false;
                p.sprite.visible = false;
                if (player_.hitPlane()) {
                    enemy_.score++;
                    sound_.playExplosion();
                } else {
                    sound_.playHit();
                }
                break;
            }
        }
    }

    // Barn collision — any plane hitting the barn (AABB)
    constexpr float BARN_HALF_W = 0.45f;
    constexpr float BARN_HALF_H = 0.283f;  // 0.45 * 22/35
    constexpr float BARN_Y = GROUND_Y + BARN_HALF_H;

    if (player_.isAlive && !player_.exploding && !player_.isInvulnerable()) {
        if (Utility::aabbOverlap(player_.x, player_.y, PLANE_HALF_W, PLANE_HALF_H,
                                  HOUSE_X, BARN_Y, BARN_HALF_W, BARN_HALF_H)) {
            player_.triggerExplosion();
            enemy_.score++;
            sound_.playExplosion();
        }
    }
    if (enemy_.isAlive && !enemy_.exploding && !enemy_.isInvulnerable()) {
        if (Utility::aabbOverlap(enemy_.x, enemy_.y, PLANE_HALF_W, PLANE_HALF_H,
                                  HOUSE_X, BARN_Y, BARN_HALF_W, BARN_HALF_H)) {
            enemy_.triggerExplosion();
            player_.score++;
            sound_.playExplosion();
        }
    }

    // Ground collision (airborne planes only)
    if (player_.isAlive && !player_.exploding && !player_.isInvulnerable()
        && !player_.grounded && player_.y <= GROUND_Y) {
        player_.triggerExplosion();
        enemy_.score++;
        sound_.playExplosion();
    }
    if (enemy_.isAlive && !enemy_.exploding && !enemy_.isInvulnerable()
        && enemy_.y <= GROUND_Y) {
        enemy_.triggerExplosion();
        player_.score++;
        sound_.playExplosion();
    }
}

void GameSession::playerFire() {
    float bvx = cosf(player_.angle) * BULLET_SPEED;
    float bvy = sinf(player_.angle) * BULLET_SPEED;
    playerBullets_.spawnDirectional(player_.x, player_.y, bvx, bvy);
    sound_.playShoot();
}

void GameSession::enemyFire() {
    float bvx = cosf(enemy_.angle) * BULLET_SPEED;
    float bvy = sinf(enemy_.angle) * BULLET_SPEED;
    enemyBullets_.spawnDirectional(enemy_.x, enemy_.y, bvx, bvy);
    sound_.playShoot();
}
