#include "GameSession.h"
#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float CLIENT_PLANE_SMOOTHING = 14.f;
constexpr float CLIENT_PROJECTILE_SMOOTHING = 18.f;
constexpr float CLIENT_REMOTE_EXTRAPOLATION_MAX = 0.08f;
constexpr float CLIENT_SNAPSHOT_BUFFER_MIN = 0.06f;
constexpr float CLIENT_SNAPSHOT_BUFFER_MAX = 0.20f;
constexpr float CLIENT_LOCAL_RENDER_ERROR_DECAY = 12.f;
constexpr float CLIENT_LOCAL_RENDER_ERROR_MAX_X = 0.85f;
constexpr float CLIENT_LOCAL_RENDER_ERROR_MAX_Y = 0.55f;
constexpr float CLIENT_LOCAL_RENDER_ERROR_MAX_ANGLE = 0.7f;
constexpr float CLIENT_LOCAL_PROJECTILE_CORRECTION_MAX_X = 0.9f;
constexpr float CLIENT_LOCAL_PROJECTILE_CORRECTION_MAX_Y = 0.6f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MAX_X = 0.55f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MAX_Y = 0.35f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MAX_ANGLE = 0.45f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MIN_ALPHA = 0.18f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MAX_ALPHA = 0.55f;
constexpr size_t CLIENT_SNAPSHOT_HISTORY_LIMIT = 32;
constexpr float PROJECTILE_MUZZLE_OFFSET = PLANE_HALF_W + BULLET_HALF_SIZE * 1.4f;

float normalizeAngle(float angle) {
    while (angle > static_cast<float>(M_PI)) angle -= 2.f * static_cast<float>(M_PI);
    while (angle < -static_cast<float>(M_PI)) angle += 2.f * static_cast<float>(M_PI);
    return angle;
}

float shortestAngleDelta(float from, float to) {
    return normalizeAngle(to - from);
}

float shortestWrappedDx(float from, float to, float worldHalfW) {
    float dx = to - from;
    while (dx > worldHalfW) dx -= 2.f * worldHalfW;
    while (dx < -worldHalfW) dx += 2.f * worldHalfW;
    return dx;
}

bool projectileHitsPlane(const Projectile &projectile, const Plane &plane) {
    return Utility::aabbOverlap(
        projectile.x,
        projectile.y,
        BULLET_HALF_SIZE,
        BULLET_HALF_SIZE,
        plane.sprite.x,
        plane.sprite.y,
        PLANE_HALF_W,
        PLANE_HALF_H
    );
}

bool planeHitsHouseOrGround(const Plane &plane) {
    constexpr float BARN_HALF_W = 0.45f;
    constexpr float BARN_HALF_H = 0.283f;
    constexpr float BARN_Y = GROUND_Y + BARN_HALF_H;
    return Utility::aabbOverlap(
               plane.sprite.x,
               plane.sprite.y,
               PLANE_HALF_W,
               PLANE_HALF_H,
               HOUSE_X,
               BARN_Y,
               BARN_HALF_W,
               BARN_HALF_H
           )
           || (!plane.grounded && plane.sprite.y <= GROUND_Y);
}

float smoothingAlpha(float speed, float dt) {
    if (speed <= 0.f) return 0.f;
    return 1.f - expf(-speed * dt);
}

float lerpFloat(float from, float to, float alpha) {
    return from + (to - from) * alpha;
}

bool isSequenceNewer(uint16_t candidate, uint16_t reference) {
    return static_cast<int16_t>(candidate - reference) > 0;
}

void spawnProjectileFromPlane(
    ProjectilePool &pool,
    const Plane &plane,
    float worldHalfW,
    uint16_t spawnedByInputSequence
) {
    const float bulletVelX = cosf(plane.angle) * BULLET_SPEED;
    const float bulletVelY = sinf(plane.angle) * BULLET_SPEED;
    const float spawnX = Utility::wrapWorldX(
        plane.x + cosf(plane.angle) * PROJECTILE_MUZZLE_OFFSET,
        worldHalfW,
        BULLET_HALF_SIZE
    );
    const float spawnY = plane.y + sinf(plane.angle) * PROJECTILE_MUZZLE_OFFSET;
    pool.spawnDirectional(spawnX, spawnY, bulletVelX, bulletVelY, spawnedByInputSequence);
}

BluetoothPlaneState capturePlaneState(const Plane &plane) {
    BluetoothPlaneState state;
    state.x = plane.x;
    state.y = plane.y;
    state.angle = plane.angle;
    state.groundSpeed = plane.groundSpeed;
    state.respawnTimer = plane.respawnTimer;
    state.explosionTimer = plane.explosionTimer;
    state.hp = static_cast<uint8_t>(std::clamp(plane.hp, 0, 255));
    state.score = static_cast<uint8_t>(std::clamp(plane.score, 0, 255));
    state.isAlive = plane.isAlive;
    state.grounded = plane.grounded;
    state.exploding = plane.exploding;
    return state;
}

void syncPlaneSprite(Plane &plane) {
    plane.sprite.x = plane.x;
    plane.sprite.y = plane.y;
    plane.sprite.flipX = false;
    plane.sprite.rotation = plane.spawnFacingLeft
                            ? (plane.angle - static_cast<float>(M_PI))
                            : plane.angle;
}

bool planeHasAuthoritativeStateChange(const Plane &plane, const BluetoothPlaneState &state) {
    return plane.exploding != state.exploding
           || plane.grounded != state.grounded
           || plane.isAlive != state.isAlive
           || plane.hp != state.hp
           || plane.score != state.score;
}

bool planeStatesDifferSignificantly(const BluetoothPlaneState &from, const BluetoothPlaneState &to) {
    return from.exploding != to.exploding
           || from.grounded != to.grounded
           || from.isAlive != to.isAlive
           || from.hp != to.hp
           || from.score != to.score;
}

bool shouldSnapPlaneToState(const Plane &plane, const BluetoothPlaneState &state, float worldHalfW) {
    if (planeHasAuthoritativeStateChange(plane, state)) {
        return true;
    }

    return fabsf(shortestWrappedDx(plane.x, state.x, worldHalfW)) > 1.6f
           || fabsf(plane.y - state.y) > 0.8f;
}

BluetoothPlaneState extrapolatePlaneState(const BluetoothPlaneState &state, float dt, float worldHalfW) {
    if (dt <= 0.f || !state.isAlive || state.exploding) {
        return state;
    }

    BluetoothPlaneState result = state;
    const float speed = state.grounded ? state.groundSpeed : PLANE_SPEED;
    result.x = Utility::wrapWorldX(
        state.x + cosf(state.angle) * speed * dt,
        worldHalfW,
        PLANE_HALF_W
    );
    result.y = state.grounded
               ? (GROUND_Y + PLANE_HALF_H + 0.02f)
               : std::min(state.y + sinf(state.angle) * speed * dt, WORLD_HALF_H - PLANE_HALF_H);
    return result;
}

BluetoothPlaneState interpolatePlaneState(
    const BluetoothPlaneState &from,
    const BluetoothPlaneState &to,
    float alpha,
    float worldHalfW
) {
    if (alpha <= 0.f) return from;
    if (alpha >= 1.f) return to;
    if (planeStatesDifferSignificantly(from, to)) {
        return alpha < 0.5f ? from : to;
    }

    BluetoothPlaneState result = from;
    result.x = Utility::wrapWorldX(
        from.x + shortestWrappedDx(from.x, to.x, worldHalfW) * alpha,
        worldHalfW,
        PLANE_HALF_W
    );
    result.y = lerpFloat(from.y, to.y, alpha);
    result.angle = normalizeAngle(from.angle + shortestAngleDelta(from.angle, to.angle) * alpha);
    result.groundSpeed = lerpFloat(from.groundSpeed, to.groundSpeed, alpha);
    result.respawnTimer = lerpFloat(from.respawnTimer, to.respawnTimer, alpha);
    result.explosionTimer = lerpFloat(from.explosionTimer, to.explosionTimer, alpha);
    result.hp = alpha < 0.5f ? from.hp : to.hp;
    result.score = alpha < 0.5f ? from.score : to.score;
    result.isAlive = alpha < 0.5f ? from.isAlive : to.isAlive;
    result.grounded = alpha < 0.5f ? from.grounded : to.grounded;
    result.exploding = alpha < 0.5f ? from.exploding : to.exploding;
    return result;
}

BluetoothProjectileState interpolateProjectileState(
    const BluetoothProjectileState &from,
    const BluetoothProjectileState &to,
    float alpha,
    float worldHalfW
) {
    if (alpha <= 0.f) return from;
    if (alpha >= 1.f) return to;

    BluetoothProjectileState result{};
    result.count = std::max(from.count, to.count);
    for (int i = 0; i < result.count; ++i) {
        const bool hasFrom = i < from.count;
        const bool hasTo = i < to.count;
        if (hasFrom && hasTo) {
            result.x[i] = Utility::wrapWorldX(
                from.x[i] + shortestWrappedDx(from.x[i], to.x[i], worldHalfW) * alpha,
                worldHalfW,
                BULLET_HALF_SIZE
            );
            result.y[i] = lerpFloat(from.y[i], to.y[i], alpha);
        } else if (hasTo) {
            result.x[i] = to.x[i];
            result.y[i] = to.y[i];
        } else if (hasFrom) {
            result.x[i] = from.x[i];
            result.y[i] = from.y[i];
        }
    }
    return result;
}
}

void GameSession::init(AAssetManager *assetManager, float aspect, GameMode mode, AiDifficulty difficulty, bool localControlsBlue) {
    aspect_ = aspect;
    mode_ = mode;
    localControlsBlue_ = localControlsBlue;
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

    player_.respawnOnGround = true;
    enemy_.respawnOnGround = (mode_ == GameMode::VsBluetooth);
    player_.setGrounded();
    if (enemy_.respawnOnGround) {
        enemy_.setGrounded();
    }

    playerBullets_.init(bulletTex, worldHalfW);
    enemyBullets_.init(bulletTex, worldHalfW);

    background_.init(assetManager, aspect);
    hud_.init(assetManager, aspect);
    sound_.init(assetManager);
    ai_.setDifficulty(difficulty);

    playerFireCooldown_ = 0.f;
    enemyFireCooldown_ = 0.f;
    clientHasTargetState_ = false;
    clientStateInitialized_ = false;
    pendingLocalInputs_.clear();
    snapshotBuffer_.clear();
    localPlaneRenderSmoothing_ = {};
    clientClock_ = 0.f;
    hostToLocalClockOffset_ = 0.f;
    hasHostClockOffset_ = false;
    smoothedRtt_ = 0.08f;
    smoothedSnapshotInterval_ = 1.f / 60.f;
    snapshotJitter_ = 0.01f;
    simulationTime_ = 0.f;
}

bool GameSession::update(float dt, const TouchState &input) {
    simulationTime_ += dt;
    if (playerFireCooldown_ > 0.f) playerFireCooldown_ -= dt;
    if (enemyFireCooldown_ > 0.f) enemyFireCooldown_ -= dt;

    player_.update(dt, input.upButtonHeld, input.downButtonHeld);

    if (input.fireTapped && playerFireCooldown_ <= 0.f
        && player_.isAlive && !player_.exploding && !player_.grounded) {
        playerFire();
        playerFireCooldown_ = FIRE_COOLDOWN;
    }

    if (mode_ == GameMode::VsAI) {
        AIOutput aiOut = ai_.update(dt, enemy_, player_, worldHalfWidthForAspect(aspect_));
        enemy_.update(dt, aiOut.thrustUp, aiOut.thrustDown);
        if (aiOut.fire && enemyFireCooldown_ <= 0.f
            && enemy_.isAlive && !enemy_.exploding && !enemy_.grounded) {
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

    return getWinner() != 0;
}

bool GameSession::updateBluetoothHost(float dt, const TouchState &localInput, const BluetoothInputState &remoteInput) {
    simulationTime_ += dt;
    if (playerFireCooldown_ > 0.f) playerFireCooldown_ -= dt;
    if (enemyFireCooldown_ > 0.f) enemyFireCooldown_ -= dt;

    const TouchState blueInput = localControlsBlue_
                                 ? makeHumanTouchStateForPlane(player_, localInput.upButtonHeld, localInput.downButtonHeld, localInput.fireTapped)
                                 : makeHumanTouchStateForPlane(player_, remoteInput.upButtonHeld, remoteInput.downButtonHeld, remoteInput.fireTapped);
    const TouchState redInput = localControlsBlue_
                                ? makeHumanTouchStateForPlane(enemy_, remoteInput.upButtonHeld, remoteInput.downButtonHeld, remoteInput.fireTapped)
                                : makeHumanTouchStateForPlane(enemy_, localInput.upButtonHeld, localInput.downButtonHeld, localInput.fireTapped);

    player_.update(dt, blueInput.upButtonHeld, blueInput.downButtonHeld);
    enemy_.update(dt, redInput.upButtonHeld, redInput.downButtonHeld);

    if (blueInput.fireTapped && playerFireCooldown_ <= 0.f
        && player_.isAlive && !player_.exploding && !player_.grounded) {
        playerFire();
        playerFireCooldown_ = FIRE_COOLDOWN;
    }

    if (redInput.fireTapped && enemyFireCooldown_ <= 0.f
        && enemy_.isAlive && !enemy_.exploding && !enemy_.grounded) {
        enemyFire();
        enemyFireCooldown_ = FIRE_COOLDOWN;
    }

    playerBullets_.updateAll(dt);
    enemyBullets_.updateAll(dt);
    background_.update(dt);

    checkCollisions();

    hud_.setScores(player_.score, enemy_.score);
    hud_.update(dt);

    return getWinner() != 0;
}

void GameSession::updateBluetoothClient(float dt, const TouchState &localInput, uint16_t localInputSequence) {
    simulationTime_ += dt;
    background_.update(dt);
    clientClock_ += dt;
    player_.updatePredictedEffects(dt);
    enemy_.updatePredictedEffects(dt);
    if (playerFireCooldown_ > 0.f) playerFireCooldown_ -= dt;
    if (enemyFireCooldown_ > 0.f) enemyFireCooldown_ -= dt;

    if (localControlsBlue_) {
        const TouchState predictedInput = makeHumanTouchStateForPlane(
            player_, localInput.upButtonHeld, localInput.downButtonHeld, localInput.fireTapped);
        recordPredictedLocalInput(localInputSequence, dt, predictedInput);
        player_.update(dt, predictedInput.upButtonHeld, predictedInput.downButtonHeld);
        if (predictedInput.fireTapped && playerFireCooldown_ <= 0.f
            && player_.isAlive && !player_.exploding && !player_.grounded) {
            playerFire(localInputSequence);
            playerFireCooldown_ = FIRE_COOLDOWN;
        }
        playerBullets_.updateAll(dt);
    } else {
        const TouchState predictedInput = makeHumanTouchStateForPlane(
            enemy_, localInput.upButtonHeld, localInput.downButtonHeld, localInput.fireTapped);
        recordPredictedLocalInput(localInputSequence, dt, predictedInput);
        enemy_.update(dt, predictedInput.upButtonHeld, predictedInput.downButtonHeld);
        if (predictedInput.fireTapped && enemyFireCooldown_ <= 0.f
            && enemy_.isAlive && !enemy_.exploding && !enemy_.grounded) {
            enemyFire(localInputSequence);
            enemyFireCooldown_ = FIRE_COOLDOWN;
        }
        enemyBullets_.updateAll(dt);
    }

    if (clientHasTargetState_) {
        BluetoothMatchState bufferedState = clientTargetState_;
        sampleBufferedSnapshot(bufferedState);
        if (localControlsBlue_) {
            applyPlaneState(
                enemy_,
                bufferedState.redPlane,
                dt,
                true,
                CLIENT_PLANE_SMOOTHING
            );
            applyProjectiles(enemyBullets_, bufferedState.redProjectiles, dt, true);
            reconcileLocalProjectiles(
                playerBullets_,
                clientTargetState_.blueProjectiles,
                dt,
                clientTargetState_.acknowledgedInputSequence,
                player_
            );
        } else {
            applyPlaneState(
                player_,
                bufferedState.bluePlane,
                dt,
                true,
                CLIENT_PLANE_SMOOTHING
            );
            applyProjectiles(playerBullets_, bufferedState.blueProjectiles, dt, true);
            reconcileLocalProjectiles(
                enemyBullets_,
                clientTargetState_.redProjectiles,
                dt,
                clientTargetState_.acknowledgedInputSequence,
                enemy_
            );
        }
        hud_.setScores(player_.score, enemy_.score);
    }
    predictBluetoothClientEffects();
    if (localControlsBlue_) {
        applyLocalPlaneRenderSmoothing(player_, dt);
    } else {
        applyLocalPlaneRenderSmoothing(enemy_, dt);
    }
    hud_.update(dt);
    player_.damageEffectTimer += dt;
    enemy_.damageEffectTimer += dt;
}

void GameSession::draw(const Shader &shader) const {
    background_.draw(shader);
    hud_.draw(shader);
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
    player_.respawnOnGround = true;
    player_.setGrounded();

    enemy_.reset(2.8f, true);
    enemy_.score = 0;
    enemy_.respawnOnGround = (mode_ == GameMode::VsBluetooth);
    if (enemy_.respawnOnGround) {
        enemy_.setGrounded();
    }

    for (auto &projectile : playerBullets_.getProjectiles()) {
        projectile.active = false;
        projectile.velX = 0.f;
        projectile.velY = 0.f;
        projectile.lifetime = 0.f;
        projectile.spawnedByInputSequence = 0;
        projectile.sprite.visible = false;
    }
    for (auto &projectile : enemyBullets_.getProjectiles()) {
        projectile.active = false;
        projectile.velX = 0.f;
        projectile.velY = 0.f;
        projectile.lifetime = 0.f;
        projectile.spawnedByInputSequence = 0;
        projectile.sprite.visible = false;
    }

    playerFireCooldown_ = 0.f;
    enemyFireCooldown_ = 0.f;
    hud_.setScores(player_.score, enemy_.score);
    clientHasTargetState_ = false;
    clientStateInitialized_ = false;
    pendingLocalInputs_.clear();
    snapshotBuffer_.clear();
    localPlaneRenderSmoothing_ = {};
    clientClock_ = 0.f;
    hostToLocalClockOffset_ = 0.f;
    hasHostClockOffset_ = false;
    smoothedRtt_ = 0.08f;
    smoothedSnapshotInterval_ = 1.f / 60.f;
    snapshotJitter_ = 0.01f;
    simulationTime_ = 0.f;
}

BluetoothMatchState GameSession::buildBluetoothMatchState(uint16_t acknowledgedInputSequence) const {
    BluetoothMatchState state;
    state.acknowledgedInputSequence = acknowledgedInputSequence;
    state.hostTimestamp = simulationTime_;
    state.bluePlane = capturePlaneState(player_);
    state.redPlane = capturePlaneState(enemy_);
    captureProjectiles(playerBullets_, state.blueProjectiles);
    captureProjectiles(enemyBullets_, state.redProjectiles);
    return state;
}

void GameSession::applyBluetoothMatchState(const BluetoothMatchState &state) {
    clientTargetState_ = state;
    clientHasTargetState_ = true;
    pushBufferedSnapshot(state);
    if (!clientStateInitialized_) {
        applyPlaneState(player_, state.bluePlane, 0.f, false, CLIENT_PLANE_SMOOTHING);
        applyPlaneState(enemy_, state.redPlane, 0.f, false, CLIENT_PLANE_SMOOTHING);
        applyProjectiles(playerBullets_, state.blueProjectiles, 0.f, false);
        applyProjectiles(enemyBullets_, state.redProjectiles, 0.f, false);
        hud_.setScores(player_.score, enemy_.score);
        clientStateInitialized_ = true;
        pendingLocalInputs_.clear();
        localPlaneRenderSmoothing_ = {};
        return;
    }

    if (localControlsBlue_) {
        reconcilePredictedLocalPlane(player_, state.bluePlane, state.acknowledgedInputSequence);
        applyLocalPlaneRenderSmoothing(player_, 0.f);
    } else {
        reconcilePredictedLocalPlane(enemy_, state.redPlane, state.acknowledgedInputSequence);
        applyLocalPlaneRenderSmoothing(enemy_, 0.f);
    }
}

TouchState GameSession::makePlaneTouchState(bool upHeld, bool downHeld, bool fireTapped) {
    TouchState input;
    input.upButtonHeld = upHeld;
    input.downButtonHeld = downHeld;
    input.fireTapped = fireTapped;
    return input;
}

TouchState GameSession::makeHumanTouchStateForPlane(const Plane &plane, bool upHeld, bool downHeld, bool fireTapped) {
    if (!plane.spawnFacingLeft) {
        return makePlaneTouchState(upHeld, downHeld, fireTapped);
    }
    return makePlaneTouchState(downHeld, upHeld, fireTapped);
}

void GameSession::applyPlaneState(Plane &plane, const BluetoothPlaneState &state, float dt, bool smooth, float smoothingSpeed) {
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    if (!smooth || shouldSnapPlaneToState(plane, state, worldHalfW)) {
        plane.x = state.x;
        plane.y = state.y;
        plane.angle = state.angle;
        plane.groundSpeed = state.groundSpeed;
    } else {
        const float alpha = smoothingAlpha(smoothingSpeed, dt);
        plane.x = Utility::wrapWorldX(
            plane.x + shortestWrappedDx(plane.x, state.x, worldHalfW) * alpha,
            worldHalfW,
            PLANE_HALF_W
        );
        plane.y += (state.y - plane.y) * alpha;
        plane.angle = normalizeAngle(plane.angle + shortestAngleDelta(plane.angle, state.angle) * alpha);
        plane.groundSpeed += (state.groundSpeed - plane.groundSpeed) * alpha;
    }
    plane.respawnTimer = state.respawnTimer;
    plane.explosionTimer = state.explosionTimer;
    plane.hp = state.hp;
    plane.score = state.score;
    plane.isAlive = state.isAlive;
    plane.grounded = state.grounded;
    plane.exploding = state.exploding;
    if (plane.exploding) {
        plane.predictedExplosionTimer = 0.f;
        plane.impactEffectTimer = 0.f;
    }
    syncPlaneSprite(plane);
}

void GameSession::applyLocalPlaneRenderSmoothing(Plane &plane, float dt) {
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const float alpha = smoothingAlpha(CLIENT_LOCAL_RENDER_ERROR_DECAY, dt);
    localPlaneRenderSmoothing_.xError += (0.f - localPlaneRenderSmoothing_.xError) * alpha;
    localPlaneRenderSmoothing_.yError += (0.f - localPlaneRenderSmoothing_.yError) * alpha;
    localPlaneRenderSmoothing_.angleError += (0.f - localPlaneRenderSmoothing_.angleError) * alpha;

    const float renderAngle = normalizeAngle(plane.angle + localPlaneRenderSmoothing_.angleError);
    plane.sprite.x = Utility::wrapWorldX(
        plane.x + localPlaneRenderSmoothing_.xError,
        worldHalfW,
        PLANE_HALF_W
    );
    plane.sprite.y = plane.y + localPlaneRenderSmoothing_.yError;
    plane.sprite.flipX = false;
    plane.sprite.rotation = plane.spawnFacingLeft
                            ? (renderAngle - static_cast<float>(M_PI))
                            : renderAngle;
}

void GameSession::recordPredictedLocalInput(uint16_t sequence, float dt, const TouchState &predictedInput) {
    if (!pendingLocalInputs_.empty() && pendingLocalInputs_.back().sequence == sequence) {
        pendingLocalInputs_.back().dt += dt;
        pendingLocalInputs_.back().upButtonHeld = predictedInput.upButtonHeld;
        pendingLocalInputs_.back().downButtonHeld = predictedInput.downButtonHeld;
        return;
    }

    pendingLocalInputs_.push_back(PredictedInputFrame{
        sequence,
        clientClock_,
        dt,
        predictedInput.upButtonHeld,
        predictedInput.downButtonHeld
    });
    while (pendingLocalInputs_.size() > 180) {
        pendingLocalInputs_.pop_front();
    }
}

void GameSession::reconcilePredictedLocalPlane(
    Plane &plane,
    const BluetoothPlaneState &state,
    uint16_t acknowledgedInputSequence
) {
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const float previousX = plane.x;
    const float previousY = plane.y;
    const float previousAngle = plane.angle;
    const float previousGroundSpeed = plane.groundSpeed;
    const bool shouldSmoothCorrection = !planeHasAuthoritativeStateChange(plane, state);

    float newestAcknowledgedSentAt = -1.f;
    while (!pendingLocalInputs_.empty()
        && !isSequenceNewer(pendingLocalInputs_.front().sequence, acknowledgedInputSequence)) {
        newestAcknowledgedSentAt = pendingLocalInputs_.front().sentAt;
        pendingLocalInputs_.pop_front();
    }

    if (newestAcknowledgedSentAt >= 0.f) {
        const float rttSample = std::max(0.f, clientClock_ - newestAcknowledgedSentAt);
        smoothedRtt_ = lerpFloat(smoothedRtt_, rttSample, 0.12f);
    }

    const float preservedDamageEffectTimer = plane.damageEffectTimer;
    applyPlaneState(plane, state, 0.f, false, CLIENT_PLANE_SMOOTHING);
    for (const auto &inputFrame : pendingLocalInputs_) {
        plane.update(inputFrame.dt, inputFrame.upButtonHeld, inputFrame.downButtonHeld);
    }
    plane.damageEffectTimer = preservedDamageEffectTimer;

    if (shouldSmoothCorrection) {
        const float dx = fabsf(shortestWrappedDx(previousX, plane.x, worldHalfW));
        const float dy = fabsf(plane.y - previousY);
        const float da = fabsf(shortestAngleDelta(previousAngle, plane.angle));
        if (dx <= CLIENT_LOCAL_SOFT_RECONCILE_MAX_X
            && dy <= CLIENT_LOCAL_SOFT_RECONCILE_MAX_Y
            && da <= CLIENT_LOCAL_SOFT_RECONCILE_MAX_ANGLE) {
            const float normalizedMagnitude = std::max({
                dx / CLIENT_LOCAL_SOFT_RECONCILE_MAX_X,
                dy / CLIENT_LOCAL_SOFT_RECONCILE_MAX_Y,
                da / CLIENT_LOCAL_SOFT_RECONCILE_MAX_ANGLE
            });
            const float correctionAlpha = lerpFloat(
                CLIENT_LOCAL_SOFT_RECONCILE_MIN_ALPHA,
                CLIENT_LOCAL_SOFT_RECONCILE_MAX_ALPHA,
                normalizedMagnitude
            );
            plane.x = Utility::wrapWorldX(
                previousX + shortestWrappedDx(previousX, plane.x, worldHalfW) * correctionAlpha,
                worldHalfW,
                PLANE_HALF_W
            );
            plane.y = previousY + (plane.y - previousY) * correctionAlpha;
            plane.angle = normalizeAngle(
                previousAngle + shortestAngleDelta(previousAngle, plane.angle) * correctionAlpha
            );
            plane.groundSpeed = previousGroundSpeed + (plane.groundSpeed - previousGroundSpeed) * correctionAlpha;
        }
    }

    if (shouldSmoothCorrection) {
        localPlaneRenderSmoothing_.xError = std::clamp(
            localPlaneRenderSmoothing_.xError + shortestWrappedDx(plane.x, previousX, worldHalfW),
            -CLIENT_LOCAL_RENDER_ERROR_MAX_X,
            CLIENT_LOCAL_RENDER_ERROR_MAX_X
        );
        localPlaneRenderSmoothing_.yError = std::clamp(
            localPlaneRenderSmoothing_.yError + (previousY - plane.y),
            -CLIENT_LOCAL_RENDER_ERROR_MAX_Y,
            CLIENT_LOCAL_RENDER_ERROR_MAX_Y
        );
        localPlaneRenderSmoothing_.angleError = std::clamp(
            normalizeAngle(localPlaneRenderSmoothing_.angleError + shortestAngleDelta(plane.angle, previousAngle)),
            -CLIENT_LOCAL_RENDER_ERROR_MAX_ANGLE,
            CLIENT_LOCAL_RENDER_ERROR_MAX_ANGLE
        );
    } else {
        localPlaneRenderSmoothing_ = {};
    }

    syncPlaneSprite(plane);
}

void GameSession::predictBluetoothClientEffects() {
    Plane &localPlane = localControlsBlue_ ? player_ : enemy_;
    Plane &remotePlane = localControlsBlue_ ? enemy_ : player_;
    ProjectilePool &localBullets = localControlsBlue_ ? playerBullets_ : enemyBullets_;

    if (localPlane.isAlive
        && !localPlane.exploding
        && !localPlane.isInvulnerable()
        && !localPlane.hasPredictedExplosion()
        && planeHitsHouseOrGround(localPlane)) {
        localPlane.triggerPredictedExplosion();
        sound_.playExplosion();
    }

    if (remotePlane.isAlive
        && !remotePlane.exploding
        && !remotePlane.isInvulnerable()
        && !remotePlane.hasPredictedExplosion()
        && planeHitsHouseOrGround(remotePlane)) {
        remotePlane.triggerPredictedExplosion();
        sound_.playExplosion();
    }

    if (!remotePlane.isAlive || remotePlane.exploding || remotePlane.isInvulnerable()) {
        return;
    }

    for (auto &projectile : localBullets.getProjectiles()) {
        if (!projectile.active) continue;
        if (!projectileHitsPlane(projectile, remotePlane)) continue;

        projectile.active = false;
        projectile.sprite.visible = false;
        projectile.velX = 0.f;
        projectile.velY = 0.f;
        projectile.lifetime = 0.f;
        projectile.spawnedByInputSequence = 0;
        remotePlane.triggerImpactEffect(projectile.x, projectile.y);
        if (remotePlane.hp <= 1 && !remotePlane.hasPredictedExplosion()) {
            remotePlane.triggerPredictedExplosion();
            sound_.playExplosion();
        } else {
            sound_.playHit();
        }
        break;
    }
}

void GameSession::pushBufferedSnapshot(const BluetoothMatchState &state) {
    const float offsetSample = std::max(0.f, clientClock_ - state.hostTimestamp);
    if (!hasHostClockOffset_) {
        hostToLocalClockOffset_ = offsetSample;
        hasHostClockOffset_ = true;
    } else if (offsetSample < hostToLocalClockOffset_) {
        hostToLocalClockOffset_ = lerpFloat(hostToLocalClockOffset_, offsetSample, 0.35f);
    } else {
        hostToLocalClockOffset_ = lerpFloat(hostToLocalClockOffset_, offsetSample, 0.08f);
    }

    if (!snapshotBuffer_.empty()
        && state.hostTimestamp <= snapshotBuffer_.back().state.hostTimestamp) {
        snapshotBuffer_.back() = BufferedMatchSnapshot{state, clientClock_};
        return;
    }

    if (!snapshotBuffer_.empty()) {
        const float hostInterval = std::max(
            0.0001f,
            state.hostTimestamp - snapshotBuffer_.back().state.hostTimestamp
        );
        const float arrivalInterval = std::max(0.f, clientClock_ - snapshotBuffer_.back().receivedAt);
        smoothedSnapshotInterval_ = lerpFloat(smoothedSnapshotInterval_, hostInterval, 0.18f);
        snapshotJitter_ = lerpFloat(snapshotJitter_, fabsf(arrivalInterval - hostInterval), 0.18f);
    }

    snapshotBuffer_.push_back(BufferedMatchSnapshot{state, clientClock_});
    while (snapshotBuffer_.size() > CLIENT_SNAPSHOT_HISTORY_LIMIT) {
        snapshotBuffer_.pop_front();
    }
}

float GameSession::currentInterpolationDelay() const {
    const float delay = smoothedSnapshotInterval_ * 2.0f
                        + snapshotJitter_ * 2.0f
                        + smoothedRtt_ * 0.25f;
    return std::clamp(delay, CLIENT_SNAPSHOT_BUFFER_MIN, CLIENT_SNAPSHOT_BUFFER_MAX);
}

bool GameSession::sampleBufferedSnapshot(BluetoothMatchState &outState) const {
    if (snapshotBuffer_.empty()) {
        return false;
    }

    if (snapshotBuffer_.size() == 1) {
        outState = snapshotBuffer_.front().state;
        const float worldHalfW = worldHalfWidthForAspect(aspect_);
        const float estimatedHostNow = hasHostClockOffset_
                                       ? (clientClock_ - hostToLocalClockOffset_)
                                       : snapshotBuffer_.front().state.hostTimestamp;
        const float extrapolationDt = std::min(
            std::max(0.f, estimatedHostNow - snapshotBuffer_.front().state.hostTimestamp),
            CLIENT_REMOTE_EXTRAPOLATION_MAX
        );
        outState.bluePlane = extrapolatePlaneState(outState.bluePlane, extrapolationDt, worldHalfW);
        outState.redPlane = extrapolatePlaneState(outState.redPlane, extrapolationDt, worldHalfW);
        return true;
    }

    const float estimatedHostNow = hasHostClockOffset_
                                   ? (clientClock_ - hostToLocalClockOffset_)
                                   : snapshotBuffer_.back().state.hostTimestamp;
    const float renderTime = estimatedHostNow - currentInterpolationDelay();
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const BufferedMatchSnapshot *older = &snapshotBuffer_.front();
    const BufferedMatchSnapshot *newer = &snapshotBuffer_.back();

    for (size_t i = 1; i < snapshotBuffer_.size(); ++i) {
        if (snapshotBuffer_[i].state.hostTimestamp >= renderTime) {
            older = &snapshotBuffer_[i - 1];
            newer = &snapshotBuffer_[i];
            break;
        }
    }

    if (renderTime >= snapshotBuffer_.back().state.hostTimestamp) {
        outState = snapshotBuffer_.back().state;
        const float extrapolationDt = std::min(
            renderTime - snapshotBuffer_.back().state.hostTimestamp,
            CLIENT_REMOTE_EXTRAPOLATION_MAX
        );
        outState.bluePlane = extrapolatePlaneState(outState.bluePlane, extrapolationDt, worldHalfW);
        outState.redPlane = extrapolatePlaneState(outState.redPlane, extrapolationDt, worldHalfW);
        return true;
    }

    if (renderTime <= snapshotBuffer_.front().state.hostTimestamp) {
        outState = snapshotBuffer_.front().state;
        return true;
    }

    const float span = std::max(0.0001f, newer->state.hostTimestamp - older->state.hostTimestamp);
    const float alpha = std::clamp((renderTime - older->state.hostTimestamp) / span, 0.f, 1.f);
    outState = newer->state;
    outState.bluePlane = interpolatePlaneState(older->state.bluePlane, newer->state.bluePlane, alpha, worldHalfW);
    outState.redPlane = interpolatePlaneState(older->state.redPlane, newer->state.redPlane, alpha, worldHalfW);
    outState.blueProjectiles = interpolateProjectileState(
        older->state.blueProjectiles,
        newer->state.blueProjectiles,
        alpha,
        worldHalfW
    );
    outState.redProjectiles = interpolateProjectileState(
        older->state.redProjectiles,
        newer->state.redProjectiles,
        alpha,
        worldHalfW
    );
    return true;
}

void GameSession::captureProjectiles(const ProjectilePool &pool, BluetoothProjectileState &outState) {
    outState.count = 0;
    const auto &projectiles = pool.getProjectiles();
    for (const auto &projectile : projectiles) {
        if (!projectile.active || outState.count >= MAX_PROJECTILES) continue;
        outState.x[outState.count] = projectile.x;
        outState.y[outState.count] = projectile.y;
        outState.count++;
    }
}

void GameSession::applyProjectiles(ProjectilePool &pool, const BluetoothProjectileState &state, float dt, bool smooth) {
    auto &projectiles = pool.getProjectiles();
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const float alpha = smoothingAlpha(CLIENT_PROJECTILE_SMOOTHING, dt);
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        const bool active = i < state.count;
        const bool wasActive = projectiles[i].active;
        projectiles[i].active = active;
        projectiles[i].sprite.visible = active;
        if (!active) continue;
        if (!smooth || !wasActive
            || fabsf(shortestWrappedDx(projectiles[i].x, state.x[i], worldHalfW)) > 1.2f
            || fabsf(projectiles[i].y - state.y[i]) > 0.8f) {
            projectiles[i].x = state.x[i];
            projectiles[i].y = state.y[i];
        } else {
            projectiles[i].x = Utility::wrapWorldX(
                projectiles[i].x + shortestWrappedDx(projectiles[i].x, state.x[i], worldHalfW) * alpha,
                worldHalfW,
                BULLET_HALF_SIZE
            );
            projectiles[i].y += (state.y[i] - projectiles[i].y) * alpha;
        }
        projectiles[i].sprite.x = projectiles[i].x;
        projectiles[i].sprite.y = projectiles[i].y;
    }
}

void GameSession::reconcileLocalProjectiles(
    ProjectilePool &pool,
    const BluetoothProjectileState &state,
    float dt,
    uint16_t acknowledgedInputSequence,
    const Plane &firingPlane
) {
    auto &projectiles = pool.getProjectiles();
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const float alpha = smoothingAlpha(CLIENT_PROJECTILE_SMOOTHING, dt);

    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        Projectile &projectile = projectiles[i];
        const bool authoritativeActive = i < state.count;
        const bool wasActive = projectile.active;
        const bool awaitingShotAck = projectile.spawnedByInputSequence != 0
                                     && isSequenceNewer(projectile.spawnedByInputSequence, acknowledgedInputSequence);

        if (!authoritativeActive) {
            if (!awaitingShotAck) {
                projectile.active = false;
                projectile.velX = 0.f;
                projectile.velY = 0.f;
                projectile.lifetime = 0.f;
                projectile.spawnedByInputSequence = 0;
                projectile.sprite.visible = false;
            }
            continue;
        }

        projectile.active = true;
        projectile.sprite.visible = true;
        if (fabsf(projectile.velX) < 0.001f && fabsf(projectile.velY) < 0.001f) {
            projectile.velX = cosf(firingPlane.angle) * BULLET_SPEED;
            projectile.velY = sinf(firingPlane.angle) * BULLET_SPEED;
        }

        const float dx = shortestWrappedDx(projectile.x, state.x[i], worldHalfW);
        const float dy = state.y[i] - projectile.y;
        const float alongVelocity = dx * projectile.velX + dy * projectile.velY;
        const bool authoritativeBehindTrajectory = alongVelocity < 0.f;

        if (!wasActive) {
            projectile.x = state.x[i];
            projectile.y = state.y[i];
            projectile.lifetime = BULLET_LIFETIME;
        } else if (authoritativeBehindTrajectory) {
            // Keep locally predicted projectile ahead of the delayed authoritative snapshot.
        } else if (fabsf(dx) > CLIENT_LOCAL_PROJECTILE_CORRECTION_MAX_X
            || fabsf(dy) > CLIENT_LOCAL_PROJECTILE_CORRECTION_MAX_Y) {
            projectile.x = state.x[i];
            projectile.y = state.y[i];
        } else {
            projectile.x = Utility::wrapWorldX(
                projectile.x + dx * alpha,
                worldHalfW,
                BULLET_HALF_SIZE
            );
            projectile.y += dy * alpha;
        }

        projectile.sprite.x = projectile.x;
        projectile.sprite.y = projectile.y;
        if (!awaitingShotAck) {
            projectile.spawnedByInputSequence = 0;
        }
    }
}

void GameSession::checkCollisions() {
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

    constexpr float BARN_HALF_W = 0.45f;
    constexpr float BARN_HALF_H = 0.283f;
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

    if (player_.isAlive && !player_.exploding && !player_.isInvulnerable()
        && !player_.grounded && player_.y <= GROUND_Y) {
        player_.triggerExplosion();
        enemy_.score++;
        sound_.playExplosion();
    }
    if (enemy_.isAlive && !enemy_.exploding && !enemy_.isInvulnerable()
        && !enemy_.grounded && enemy_.y <= GROUND_Y) {
        enemy_.triggerExplosion();
        player_.score++;
        sound_.playExplosion();
    }
}

void GameSession::playerFire(uint16_t spawnedByInputSequence) {
    spawnProjectileFromPlane(
        playerBullets_,
        player_,
        worldHalfWidthForAspect(aspect_),
        spawnedByInputSequence
    );
    sound_.playShoot();
}

void GameSession::enemyFire(uint16_t spawnedByInputSequence) {
    spawnProjectileFromPlane(
        enemyBullets_,
        enemy_,
        worldHalfWidthForAspect(aspect_),
        spawnedByInputSequence
    );
    sound_.playShoot();
}
