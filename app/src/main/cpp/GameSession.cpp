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
constexpr float REMOTE_OWNED_EXTRAPOLATION_MAX = 0.22f;
constexpr float REMOTE_OWNED_SNAPSHOT_BUFFER_MIN = 0.0f;
constexpr float REMOTE_OWNED_SNAPSHOT_BUFFER_MAX = 0.035f;
constexpr float CLIENT_LOCAL_RENDER_ERROR_DECAY = 12.f;
constexpr float CLIENT_LOCAL_RENDER_ERROR_MAX_X = 0.85f;
constexpr float CLIENT_LOCAL_RENDER_ERROR_MAX_Y = 0.55f;
constexpr float CLIENT_LOCAL_RENDER_ERROR_MAX_ANGLE = 0.7f;
constexpr float CLIENT_LOCAL_PROJECTILE_CORRECTION_MAX_X = 0.9f;
constexpr float CLIENT_LOCAL_PROJECTILE_CORRECTION_MAX_Y = 0.6f;
constexpr float CLIENT_LOCAL_RECONCILE_DEAD_ZONE_X = 0.05f;
constexpr float CLIENT_LOCAL_RECONCILE_DEAD_ZONE_Y = 0.08f;
constexpr float CLIENT_LOCAL_RECONCILE_DEAD_ZONE_ANGLE = 0.07f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MAX_X = 0.55f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MAX_Y = 0.35f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MAX_ANGLE = 0.45f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MIN_ALPHA = 0.18f;
constexpr float CLIENT_LOCAL_SOFT_RECONCILE_MAX_ALPHA = 0.55f;
constexpr float CLIENT_LOCAL_RECONCILE_LARGE_ALPHA = 0.72f;
constexpr size_t CLIENT_SNAPSHOT_HISTORY_LIMIT = 32;
constexpr size_t ROLLBACK_HISTORY_LIMIT = 240;
constexpr uint8_t REMOTE_PREDICTION_HOLD_FRAMES = 3;
constexpr float PROJECTILE_MUZZLE_OFFSET = PLANE_HALF_W + BULLET_HALF_SIZE * 1.4f;
constexpr int BLUETOOTH_REMOTE_RENDER_DELAY_STEPS = 3;
constexpr float BLUETOOTH_REMOTE_RENDER_SNAP_X = 0.9f;
constexpr float BLUETOOTH_REMOTE_RENDER_SNAP_Y = 0.6f;
constexpr size_t BLUETOOTH_REMOTE_RENDER_HISTORY_LIMIT = 8;
constexpr float BLUETOOTH_REMOTE_RENDER_FOLLOW_SPEED = 5.f;
constexpr float BLUETOOTH_REMOTE_BULLET_RENDER_OFFSET_FADE = 0.35f;
constexpr float BLUETOOTH_REMOTE_BULLET_FIRST_SEEN_FORWARD = BULLET_SPEED * (1.5f / 60.f);

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

bool bluetoothInputsEqual(const BluetoothInputState &lhs, const BluetoothInputState &rhs) {
    return lhs.upButtonHeld == rhs.upButtonHeld
           && lhs.downButtonHeld == rhs.downButtonHeld
           && lhs.fireTapped == rhs.fireTapped
           && lhs.sequence == rhs.sequence;
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

bool planeHasAuthoritativeEventChange(const Plane &plane, const BluetoothPlaneState &state) {
    return plane.exploding != state.exploding
           || plane.isAlive != state.isAlive
           || plane.hp != state.hp
           || plane.score != state.score
           || (plane.respawnTimer <= 0.f && state.respawnTimer > 0.f);
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
    rollbackFrames_.clear();
    pendingRemoteInputs_.clear();
    lastConfirmedRemoteInput_ = {};
    hasLastConfirmedRemoteInput_ = false;
    predictedRemoteInputAge_ = 0;
    hasPendingRollbackCorrection_ = false;
    pendingRollbackSequence_ = 0;
    hasRemoteOwnedState_ = false;
    remoteOwnedState_ = {};
    remoteOwnedSnapshotBuffer_.clear();
    clientHasTargetState_ = false;
    clientStateInitialized_ = false;
    pendingLocalInputs_.clear();
    snapshotBuffer_.clear();
    localPlaneRenderSmoothing_ = {};
    clientClock_ = 0.f;
    hostToLocalClockOffset_ = 0.f;
    hasHostClockOffset_ = false;
    remoteOwnedClockOffset_ = 0.f;
    hasRemoteOwnedClockOffset_ = false;
    smoothedRtt_ = 0.08f;
    smoothedSnapshotInterval_ = 1.f / 60.f;
    snapshotJitter_ = 0.01f;
    remoteOwnedSnapshotInterval_ = 1.f / 60.f;
    remoteOwnedSnapshotJitter_ = 0.01f;
    simulationTime_ = 0.f;
    bluetoothRemoteRenderAlpha_ = 1.f;
    resetRemotePlaneRenderState();
    resetRemoteBulletRenderState();
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

bool GameSession::updateBluetoothHost(float dt, const TouchState &localInput) {
    simulationTime_ += dt;
    if (playerFireCooldown_ > 0.f) playerFireCooldown_ -= dt;
    if (enemyFireCooldown_ > 0.f) enemyFireCooldown_ -= dt;

    const TouchState blueInput = makeHumanTouchStateForPlane(
        player_,
        localInput.upButtonHeld,
        localInput.downButtonHeld,
        localInput.fireTapped
    );

    player_.update(dt, blueInput.upButtonHeld, blueInput.downButtonHeld);
    enemy_.update(dt, false, false);

    if (blueInput.fireTapped && playerFireCooldown_ <= 0.f
        && player_.isAlive && !player_.exploding && !player_.grounded) {
        playerFire();
        playerFireCooldown_ = FIRE_COOLDOWN;
    }

    if (hasRemoteOwnedState_
        && enemy_.isAlive
        && !enemy_.exploding
        && enemy_.respawnTimer <= 0.f) {
        BluetoothMatchState bufferedRemoteState = remoteOwnedState_;
        sampleRemoteOwnedSnapshot(bufferedRemoteState);
        applyOwnedRemotePlaneState(
            enemy_,
            bufferedRemoteState.redPlane,
            dt,
            false,
            CLIENT_PLANE_SMOOTHING
        );
        applyProjectiles(enemyBullets_, bufferedRemoteState.redProjectiles, dt, true);
    }

    playerBullets_.updateAll(dt);
    background_.update(dt);

    checkCollisions();

    hud_.setScores(player_.score, enemy_.score);
    hud_.update(dt);

    return getWinner() != 0;
}

void GameSession::updateBluetoothClient(float dt, const TouchState &localInput) {
    simulationTime_ += dt;
    background_.update(dt);
    clientClock_ += dt;
    player_.updatePredictedEffects(dt);
    enemy_.updatePredictedEffects(dt);
    if (playerFireCooldown_ > 0.f) playerFireCooldown_ -= dt;
    if (enemyFireCooldown_ > 0.f) enemyFireCooldown_ -= dt;

    const TouchState localOwnedInput = makeHumanTouchStateForPlane(
        enemy_, localInput.upButtonHeld, localInput.downButtonHeld, localInput.fireTapped);
    enemy_.update(dt, localOwnedInput.upButtonHeld, localOwnedInput.downButtonHeld);
    if (localOwnedInput.fireTapped && enemyFireCooldown_ <= 0.f
        && enemy_.isAlive && !enemy_.exploding && !enemy_.grounded) {
        enemyFire();
        enemyFireCooldown_ = FIRE_COOLDOWN;
    }
    enemyBullets_.updateAll(dt);

    if (clientHasTargetState_) {
        BluetoothMatchState bufferedState = clientTargetState_;
        sampleBufferedSnapshot(bufferedState);
        applyPlaneState(
            player_,
            bufferedState.bluePlane,
            dt,
            true,
            CLIENT_PLANE_SMOOTHING
        );
        applyProjectiles(playerBullets_, bufferedState.blueProjectiles, dt, true);
        if (planeHasAuthoritativeEventChange(enemy_, clientTargetState_.redPlane)) {
            applyPlaneState(enemy_, clientTargetState_.redPlane, 0.f, false, CLIENT_PLANE_SMOOTHING);
            applyProjectiles(enemyBullets_, clientTargetState_.redProjectiles, 0.f, false);
        } else {
            applyAuthoritativePlaneFields(enemy_, clientTargetState_.redPlane);
        }
        hud_.setScores(player_.score, enemy_.score);
    }
    predictBluetoothClientEffects();
    hud_.update(dt);
    player_.damageEffectTimer += dt;
    enemy_.damageEffectTimer += dt;
}

bool GameSession::updateBluetoothRollback(float dt, const BluetoothInputState &localInput) {
    processBluetoothRollbackCorrections();

    BluetoothInputState remoteInput{};
    const bool remoteConfirmed = consumeQueuedRemoteInput(localInput.sequence, remoteInput);
    if (remoteConfirmed) {
        predictedRemoteInputAge_ = 0;
        lastConfirmedRemoteInput_ = remoteInput;
        lastConfirmedRemoteInput_.fireTapped = false;
        hasLastConfirmedRemoteInput_ = true;
    } else {
        remoteInput = hasLastConfirmedRemoteInput_ ? lastConfirmedRemoteInput_ : BluetoothInputState{};
        remoteInput.sequence = localInput.sequence;
        remoteInput.fireTapped = false;
        if (predictedRemoteInputAge_ >= REMOTE_PREDICTION_HOLD_FRAMES) {
            remoteInput.upButtonHeld = false;
            remoteInput.downButtonHeld = false;
        }
        predictedRemoteInputAge_ = static_cast<uint8_t>(std::min<int>(predictedRemoteInputAge_ + 1, 255));
    }

    rollbackFrames_.push_back(RollbackFrame{
        localInput.sequence,
        dt,
        captureSimulationSnapshot(),
        localInput,
        remoteInput,
        remoteConfirmed
    });

    simulateBluetoothRollbackFrame(dt, localInput, remoteInput, true);
    advanceRemotePlaneRenderSample();

    trimRollbackHistory();
    background_.update(dt);
    hud_.setScores(player_.score, enemy_.score);
    hud_.update(dt);
    return getWinner() != 0;
}

void GameSession::queueBluetoothRemoteInput(const BluetoothInputState &input) {
    for (auto &frame : rollbackFrames_) {
        if (frame.sequence != input.sequence) {
            continue;
        }

        BluetoothInputState resolved = input;
        resolved.fireTapped = frame.remoteInput.fireTapped || input.fireTapped;
        const bool changed = !bluetoothInputsEqual(frame.remoteInput, resolved);
        frame.remoteInput = resolved;
        frame.remoteConfirmed = true;

        if (changed) {
            if (!hasPendingRollbackCorrection_
                || isSequenceNewer(pendingRollbackSequence_, input.sequence)) {
                pendingRollbackSequence_ = input.sequence;
                hasPendingRollbackCorrection_ = true;
            }
        }
        return;
    }

    for (auto it = pendingRemoteInputs_.begin(); it != pendingRemoteInputs_.end(); ++it) {
        if (it->sequence == input.sequence) {
            it->upButtonHeld = input.upButtonHeld;
            it->downButtonHeld = input.downButtonHeld;
            it->fireTapped = it->fireTapped || input.fireTapped;
            return;
        }
        if (isSequenceNewer(it->sequence, input.sequence)) {
            pendingRemoteInputs_.insert(it, input);
            return;
        }
    }

    pendingRemoteInputs_.push_back(input);
    while (pendingRemoteInputs_.size() > ROLLBACK_HISTORY_LIMIT) {
        pendingRemoteInputs_.pop_front();
    }
}

void GameSession::processBluetoothRollbackCorrections() {
    if (!hasPendingRollbackCorrection_) {
        return;
    }

    rebuildRollbackFrom(pendingRollbackSequence_);
    hasPendingRollbackCorrection_ = false;
    advanceRemotePlaneRenderSample();
}

void GameSession::draw(const Shader &shader) const {
    background_.draw(shader);
    hud_.draw(shader);
    if (mode_ == GameMode::VsBluetooth && !localControlsBlue_) {
        drawRemotePlane(shader, player_);
    } else {
        player_.draw(shader);
    }
    if (mode_ == GameMode::VsBluetooth && localControlsBlue_) {
        drawRemotePlane(shader, enemy_);
    } else {
        enemy_.draw(shader);
    }

    float remoteBulletOffsetX = 0.f;
    float remoteBulletOffsetY = 0.f;
    const bool hasRemoteBulletOffset = mode_ == GameMode::VsBluetooth
                                       && computeRemotePlaneRenderOffset(
                                           remoteBulletOffsetX,
                                           remoteBulletOffsetY
                                       );

    if (mode_ == GameMode::VsBluetooth && !localControlsBlue_ && hasRemoteBulletOffset) {
        drawProjectilePoolWithOffset(
            shader,
            playerBullets_,
            playerBulletRenderVisibleAges_,
            playerBulletRenderOriginX_,
            playerBulletRenderOriginY_,
            remoteBulletOffsetX,
            remoteBulletOffsetY,
            BLUETOOTH_REMOTE_BULLET_RENDER_OFFSET_FADE
        );
    } else {
        playerBullets_.drawAll(shader);
    }

    if (mode_ == GameMode::VsBluetooth && localControlsBlue_ && hasRemoteBulletOffset) {
        drawProjectilePoolWithOffset(
            shader,
            enemyBullets_,
            enemyBulletRenderVisibleAges_,
            enemyBulletRenderOriginX_,
            enemyBulletRenderOriginY_,
            remoteBulletOffsetX,
            remoteBulletOffsetY,
            BLUETOOTH_REMOTE_BULLET_RENDER_OFFSET_FADE
        );
    } else {
        enemyBullets_.drawAll(shader);
    }
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
    rollbackFrames_.clear();
    pendingRemoteInputs_.clear();
    lastConfirmedRemoteInput_ = {};
    hasLastConfirmedRemoteInput_ = false;
    predictedRemoteInputAge_ = 0;
    hasPendingRollbackCorrection_ = false;
    pendingRollbackSequence_ = 0;
    hasRemoteOwnedState_ = false;
    remoteOwnedState_ = {};
    remoteOwnedSnapshotBuffer_.clear();
    clientHasTargetState_ = false;
    clientStateInitialized_ = false;
    pendingLocalInputs_.clear();
    snapshotBuffer_.clear();
    localPlaneRenderSmoothing_ = {};
    clientClock_ = 0.f;
    hostToLocalClockOffset_ = 0.f;
    hasHostClockOffset_ = false;
    remoteOwnedClockOffset_ = 0.f;
    hasRemoteOwnedClockOffset_ = false;
    smoothedRtt_ = 0.08f;
    smoothedSnapshotInterval_ = 1.f / 60.f;
    snapshotJitter_ = 0.01f;
    remoteOwnedSnapshotInterval_ = 1.f / 60.f;
    remoteOwnedSnapshotJitter_ = 0.01f;
    simulationTime_ = 0.f;
    bluetoothRemoteRenderAlpha_ = 1.f;
    resetRemotePlaneRenderState();
    resetRemoteBulletRenderState();
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
        applyProjectiles(playerBullets_, state.blueProjectiles, 0.f, false);
        if (!localControlsBlue_) {
            applyPlaneState(enemy_, state.redPlane, 0.f, false, CLIENT_PLANE_SMOOTHING);
            applyProjectiles(enemyBullets_, state.redProjectiles, 0.f, false);
        }
        hud_.setScores(player_.score, enemy_.score);
        clientStateInitialized_ = true;
        pendingLocalInputs_.clear();
        localPlaneRenderSmoothing_ = {};
        return;
    }

}

void GameSession::applyBluetoothRemoteOwnedState(const BluetoothMatchState &state) {
    remoteOwnedState_ = state;
    hasRemoteOwnedState_ = true;
    pushRemoteOwnedSnapshot(state);
}

void GameSession::setBluetoothRemoteRenderAlpha(float alpha) {
    bluetoothRemoteRenderAlpha_ = std::clamp(alpha, 0.f, 1.f);
}

void GameSession::updateBluetoothRemoteRender(float dt) {
    if (mode_ != GameMode::VsBluetooth) {
        return;
    }

    RemotePlaneRenderSample targetSample{};
    if (!sampleRemotePlaneRenderTarget(targetSample)) {
        renderedRemotePlaneRenderSample_ = {};
        return;
    }

    if (!renderedRemotePlaneRenderSample_.valid) {
        renderedRemotePlaneRenderSample_ = targetSample;
        return;
    }

    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const float dx = shortestWrappedDx(
        renderedRemotePlaneRenderSample_.x,
        targetSample.x,
        worldHalfW
    );
    const float dy = targetSample.y - renderedRemotePlaneRenderSample_.y;
    const bool shouldSnap = renderedRemotePlaneRenderSample_.grounded != targetSample.grounded
                            || renderedRemotePlaneRenderSample_.isAlive != targetSample.isAlive
                            || renderedRemotePlaneRenderSample_.exploding != targetSample.exploding
                            || fabsf(dx) > BLUETOOTH_REMOTE_RENDER_SNAP_X
                            || fabsf(dy) > BLUETOOTH_REMOTE_RENDER_SNAP_Y;
    if (shouldSnap) {
        renderedRemotePlaneRenderSample_ = targetSample;
        return;
    }

    const float alpha = smoothingAlpha(BLUETOOTH_REMOTE_RENDER_FOLLOW_SPEED, dt);
    renderedRemotePlaneRenderSample_.x = Utility::wrapWorldX(
        renderedRemotePlaneRenderSample_.x + dx * alpha,
        worldHalfW,
        PLANE_HALF_W
    );
    renderedRemotePlaneRenderSample_.y += dy * alpha;
    renderedRemotePlaneRenderSample_.angle = normalizeAngle(
        renderedRemotePlaneRenderSample_.angle
        + shortestAngleDelta(
            renderedRemotePlaneRenderSample_.angle,
            targetSample.angle
        ) * alpha
    );
    renderedRemotePlaneRenderSample_.grounded = targetSample.grounded;
    renderedRemotePlaneRenderSample_.isAlive = targetSample.isAlive;
    renderedRemotePlaneRenderSample_.exploding = targetSample.exploding;
    renderedRemotePlaneRenderSample_.valid = true;

    const float renderedMuzzleX = Utility::wrapWorldX(
        renderedRemotePlaneRenderSample_.x
        + cosf(renderedRemotePlaneRenderSample_.angle) * PROJECTILE_MUZZLE_OFFSET,
        worldHalfW,
        BULLET_HALF_SIZE
    );
    const float renderedMuzzleY =
        renderedRemotePlaneRenderSample_.y
        + sinf(renderedRemotePlaneRenderSample_.angle) * PROJECTILE_MUZZLE_OFFSET;

    updateBulletRenderVisibleAges(
        playerBullets_,
        playerBulletRenderVisibleAges_,
        playerBulletRenderWasActive_,
        playerBulletRenderOriginX_,
        playerBulletRenderOriginY_,
        renderedRemotePlaneRenderSample_.angle,
        renderedMuzzleX,
        renderedMuzzleY,
        dt
    );
    updateBulletRenderVisibleAges(
        enemyBullets_,
        enemyBulletRenderVisibleAges_,
        enemyBulletRenderWasActive_,
        enemyBulletRenderOriginX_,
        enemyBulletRenderOriginY_,
        renderedRemotePlaneRenderSample_.angle,
        renderedMuzzleX,
        renderedMuzzleY,
        dt
    );
}

GameSession::RemotePlaneRenderSample GameSession::captureRemotePlaneRenderSample() const {
    const Plane &remotePlane = localControlsBlue_ ? enemy_ : player_;
    return RemotePlaneRenderSample{
        remotePlane.x,
        remotePlane.y,
        remotePlane.angle,
        remotePlane.grounded,
        remotePlane.isAlive,
        remotePlane.exploding,
        true
    };
}

bool GameSession::sampleRemotePlaneRenderTarget(RemotePlaneRenderSample &outSample) const {
    if (remotePlaneRenderHistory_.empty()) {
        return false;
    }

    if (remotePlaneRenderHistory_.size() == 1) {
        outSample = remotePlaneRenderHistory_.back();
        return true;
    }

    const size_t newestIndex = remotePlaneRenderHistory_.size() - 1;
    const size_t delaySteps = std::min<size_t>(BLUETOOTH_REMOTE_RENDER_DELAY_STEPS, newestIndex);
    const size_t toIndex = newestIndex - delaySteps;
    const size_t fromIndex = toIndex > 0 ? toIndex - 1 : toIndex;
    const RemotePlaneRenderSample &fromSample = remotePlaneRenderHistory_[fromIndex];
    const RemotePlaneRenderSample &toSample = remotePlaneRenderHistory_[toIndex];

    if (fromIndex == toIndex) {
        outSample = toSample;
        return true;
    }

    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const float alpha = std::clamp(bluetoothRemoteRenderAlpha_, 0.f, 1.f);
    outSample = toSample;
    outSample.x = Utility::wrapWorldX(
        fromSample.x + shortestWrappedDx(fromSample.x, toSample.x, worldHalfW) * alpha,
        worldHalfW,
        PLANE_HALF_W
    );
    outSample.y = lerpFloat(fromSample.y, toSample.y, alpha);
    outSample.angle = normalizeAngle(
        fromSample.angle + shortestAngleDelta(fromSample.angle, toSample.angle) * alpha
    );
    outSample.valid = true;
    return true;
}

void GameSession::resetRemotePlaneRenderState() {
    remotePlaneRenderHistory_.clear();
    renderedRemotePlaneRenderSample_ = {};
    if (mode_ != GameMode::VsBluetooth) {
        return;
    }

    const RemotePlaneRenderSample sample = captureRemotePlaneRenderSample();
    remotePlaneRenderHistory_.push_back(sample);
    renderedRemotePlaneRenderSample_ = sample;
}

void GameSession::advanceRemotePlaneRenderSample() {
    if (mode_ != GameMode::VsBluetooth) {
        return;
    }

    const RemotePlaneRenderSample sample = captureRemotePlaneRenderSample();
    if (remotePlaneRenderHistory_.empty()) {
        remotePlaneRenderHistory_.push_back(sample);
        return;
    }

    remotePlaneRenderHistory_.push_back(sample);
    while (remotePlaneRenderHistory_.size() > BLUETOOTH_REMOTE_RENDER_HISTORY_LIMIT) {
        remotePlaneRenderHistory_.pop_front();
    }
}

void GameSession::drawRemotePlane(const Shader &shader, const Plane &plane) const {
    if (!renderedRemotePlaneRenderSample_.valid) {
        plane.draw(shader);
        return;
    }

    plane.drawAt(
        shader,
        renderedRemotePlaneRenderSample_.x,
        renderedRemotePlaneRenderSample_.y,
        renderedRemotePlaneRenderSample_.angle
    );
}

bool GameSession::computeRemotePlaneRenderOffset(float &outOffsetX, float &outOffsetY) const {
    if (mode_ != GameMode::VsBluetooth || !renderedRemotePlaneRenderSample_.valid) {
        return false;
    }

    const Plane &remotePlane = localControlsBlue_ ? enemy_ : player_;
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const float actualMuzzleX = Utility::wrapWorldX(
        remotePlane.x + cosf(remotePlane.angle) * PROJECTILE_MUZZLE_OFFSET,
        worldHalfW,
        BULLET_HALF_SIZE
    );
    const float actualMuzzleY = remotePlane.y + sinf(remotePlane.angle) * PROJECTILE_MUZZLE_OFFSET;
    const float renderedMuzzleX = Utility::wrapWorldX(
        renderedRemotePlaneRenderSample_.x
        + cosf(renderedRemotePlaneRenderSample_.angle) * PROJECTILE_MUZZLE_OFFSET,
        worldHalfW,
        BULLET_HALF_SIZE
    );
    const float renderedMuzzleY =
        renderedRemotePlaneRenderSample_.y
        + sinf(renderedRemotePlaneRenderSample_.angle) * PROJECTILE_MUZZLE_OFFSET;

    outOffsetX = shortestWrappedDx(actualMuzzleX, renderedMuzzleX, worldHalfW);
    outOffsetY = renderedMuzzleY - actualMuzzleY;
    return true;
}

void GameSession::resetRemoteBulletRenderState() {
    playerBulletRenderVisibleAges_.fill(0.f);
    enemyBulletRenderVisibleAges_.fill(0.f);
    playerBulletRenderWasActive_.fill(false);
    enemyBulletRenderWasActive_.fill(false);
    playerBulletRenderOriginX_.fill(0.f);
    playerBulletRenderOriginY_.fill(0.f);
    enemyBulletRenderOriginX_.fill(0.f);
    enemyBulletRenderOriginY_.fill(0.f);
}

void GameSession::updateBulletRenderVisibleAges(
    const ProjectilePool &pool,
    std::array<float, MAX_PROJECTILES> &visibleAges,
    std::array<bool, MAX_PROJECTILES> &wasActive,
    std::array<float, MAX_PROJECTILES> &originX,
    std::array<float, MAX_PROJECTILES> &originY,
    float renderedAngle,
    float renderedMuzzleX,
    float renderedMuzzleY,
    float dt
) {
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const auto &projectiles = pool.getProjectiles();
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (!projectiles[i].active) {
            visibleAges[i] = 0.f;
            wasActive[i] = false;
            continue;
        }

        if (!wasActive[i]) {
            visibleAges[i] = 0.f;
            wasActive[i] = true;
            originX[i] = Utility::wrapWorldX(
                renderedMuzzleX + cosf(renderedAngle) * BLUETOOTH_REMOTE_BULLET_FIRST_SEEN_FORWARD,
                worldHalfW,
                BULLET_HALF_SIZE
            );
            originY[i] = renderedMuzzleY + sinf(renderedAngle) * BLUETOOTH_REMOTE_BULLET_FIRST_SEEN_FORWARD;
            continue;
        }

        visibleAges[i] = std::min(visibleAges[i] + dt, BULLET_LIFETIME);
    }
}

void GameSession::drawProjectilePoolWithOffset(
    const Shader &shader,
    const ProjectilePool &pool,
    const std::array<float, MAX_PROJECTILES> &visibleAges,
    const std::array<float, MAX_PROJECTILES> &originX,
    const std::array<float, MAX_PROJECTILES> &originY,
    float offsetX,
    float offsetY,
    float fadeDuration
) const {
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const float safeFadeDuration = std::max(0.0001f, fadeDuration);
    int projectileIndex = 0;
    for (const auto &projectile : pool.getProjectiles()) {
        if (!projectile.active) {
            projectileIndex++;
            continue;
        }

        const float visibleAge = std::clamp(visibleAges[projectileIndex], 0.f, BULLET_LIFETIME);
        const float progress = std::clamp(visibleAge / safeFadeDuration, 0.f, 1.f);
        const float offsetAlpha = 1.f - progress;
        Sprite renderSprite = projectile.sprite;
        const float targetX = Utility::wrapWorldX(
            projectile.x + offsetX * offsetAlpha,
            worldHalfW,
            BULLET_HALF_SIZE
        );
        const float targetY = projectile.y + offsetY * offsetAlpha;
        renderSprite.x = Utility::wrapWorldX(
            originX[projectileIndex]
            + shortestWrappedDx(originX[projectileIndex], targetX, worldHalfW) * progress,
            worldHalfW,
            BULLET_HALF_SIZE
        );
        renderSprite.y = originY[projectileIndex] + (targetY - originY[projectileIndex]) * progress;
        renderSprite.draw(shader);
        projectileIndex++;
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

void GameSession::simulateBluetoothRollbackFrame(
    float dt,
    const BluetoothInputState &localInput,
    const BluetoothInputState &remoteInput,
    bool emitAudio
) {
    simulationTime_ += dt;
    if (playerFireCooldown_ > 0.f) playerFireCooldown_ -= dt;
    if (enemyFireCooldown_ > 0.f) enemyFireCooldown_ -= dt;

    const BluetoothInputState &blueRawInput = localControlsBlue_ ? localInput : remoteInput;
    const BluetoothInputState &redRawInput = localControlsBlue_ ? remoteInput : localInput;
    const TouchState blueInput = makeHumanTouchStateForPlane(
        player_,
        blueRawInput.upButtonHeld,
        blueRawInput.downButtonHeld,
        blueRawInput.fireTapped
    );
    const TouchState redInput = makeHumanTouchStateForPlane(
        enemy_,
        redRawInput.upButtonHeld,
        redRawInput.downButtonHeld,
        redRawInput.fireTapped
    );

    player_.update(dt, blueInput.upButtonHeld, blueInput.downButtonHeld);
    enemy_.update(dt, redInput.upButtonHeld, redInput.downButtonHeld);

    if (blueInput.fireTapped && playerFireCooldown_ <= 0.f
        && player_.isAlive && !player_.exploding && !player_.grounded) {
        playerFire(blueRawInput.sequence, emitAudio);
        playerFireCooldown_ = FIRE_COOLDOWN;
    }

    if (redInput.fireTapped && enemyFireCooldown_ <= 0.f
        && enemy_.isAlive && !enemy_.exploding && !enemy_.grounded) {
        enemyFire(redRawInput.sequence, emitAudio);
        enemyFireCooldown_ = FIRE_COOLDOWN;
    }

    playerBullets_.updateAll(dt);
    enemyBullets_.updateAll(dt);
    checkCollisions(emitAudio);
}

GameSession::SimulationSnapshot GameSession::captureSimulationSnapshot() const {
    SimulationSnapshot snapshot;
    snapshot.playerFireCooldown = playerFireCooldown_;
    snapshot.enemyFireCooldown = enemyFireCooldown_;
    snapshot.simulationTime = simulationTime_;

    snapshot.bluePlane.x = player_.x;
    snapshot.bluePlane.y = player_.y;
    snapshot.bluePlane.angle = player_.angle;
    snapshot.bluePlane.groundSpeed = player_.groundSpeed;
    snapshot.bluePlane.respawnTimer = player_.respawnTimer;
    snapshot.bluePlane.explosionTimer = player_.explosionTimer;
    snapshot.bluePlane.damageEffectTimer = player_.damageEffectTimer;
    snapshot.bluePlane.predictedExplosionTimer = player_.predictedExplosionTimer;
    snapshot.bluePlane.impactEffectTimer = player_.impactEffectTimer;
    snapshot.bluePlane.impactEffectX = player_.impactEffectX;
    snapshot.bluePlane.impactEffectY = player_.impactEffectY;
    snapshot.bluePlane.hp = player_.hp;
    snapshot.bluePlane.score = player_.score;
    snapshot.bluePlane.isAlive = player_.isAlive;
    snapshot.bluePlane.grounded = player_.grounded;
    snapshot.bluePlane.exploding = player_.exploding;

    snapshot.redPlane.x = enemy_.x;
    snapshot.redPlane.y = enemy_.y;
    snapshot.redPlane.angle = enemy_.angle;
    snapshot.redPlane.groundSpeed = enemy_.groundSpeed;
    snapshot.redPlane.respawnTimer = enemy_.respawnTimer;
    snapshot.redPlane.explosionTimer = enemy_.explosionTimer;
    snapshot.redPlane.damageEffectTimer = enemy_.damageEffectTimer;
    snapshot.redPlane.predictedExplosionTimer = enemy_.predictedExplosionTimer;
    snapshot.redPlane.impactEffectTimer = enemy_.impactEffectTimer;
    snapshot.redPlane.impactEffectX = enemy_.impactEffectX;
    snapshot.redPlane.impactEffectY = enemy_.impactEffectY;
    snapshot.redPlane.hp = enemy_.hp;
    snapshot.redPlane.score = enemy_.score;
    snapshot.redPlane.isAlive = enemy_.isAlive;
    snapshot.redPlane.grounded = enemy_.grounded;
    snapshot.redPlane.exploding = enemy_.exploding;

    const auto &blueProjectiles = playerBullets_.getProjectiles();
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        snapshot.blueProjectiles[i].x = blueProjectiles[i].x;
        snapshot.blueProjectiles[i].y = blueProjectiles[i].y;
        snapshot.blueProjectiles[i].velX = blueProjectiles[i].velX;
        snapshot.blueProjectiles[i].velY = blueProjectiles[i].velY;
        snapshot.blueProjectiles[i].lifetime = blueProjectiles[i].lifetime;
        snapshot.blueProjectiles[i].spawnedByInputSequence = blueProjectiles[i].spawnedByInputSequence;
        snapshot.blueProjectiles[i].active = blueProjectiles[i].active;
    }

    const auto &redProjectiles = enemyBullets_.getProjectiles();
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        snapshot.redProjectiles[i].x = redProjectiles[i].x;
        snapshot.redProjectiles[i].y = redProjectiles[i].y;
        snapshot.redProjectiles[i].velX = redProjectiles[i].velX;
        snapshot.redProjectiles[i].velY = redProjectiles[i].velY;
        snapshot.redProjectiles[i].lifetime = redProjectiles[i].lifetime;
        snapshot.redProjectiles[i].spawnedByInputSequence = redProjectiles[i].spawnedByInputSequence;
        snapshot.redProjectiles[i].active = redProjectiles[i].active;
    }

    return snapshot;
}

void GameSession::restoreSimulationSnapshot(const SimulationSnapshot &snapshot) {
    playerFireCooldown_ = snapshot.playerFireCooldown;
    enemyFireCooldown_ = snapshot.enemyFireCooldown;
    simulationTime_ = snapshot.simulationTime;

    player_.x = snapshot.bluePlane.x;
    player_.y = snapshot.bluePlane.y;
    player_.angle = snapshot.bluePlane.angle;
    player_.groundSpeed = snapshot.bluePlane.groundSpeed;
    player_.respawnTimer = snapshot.bluePlane.respawnTimer;
    player_.explosionTimer = snapshot.bluePlane.explosionTimer;
    player_.damageEffectTimer = snapshot.bluePlane.damageEffectTimer;
    player_.predictedExplosionTimer = snapshot.bluePlane.predictedExplosionTimer;
    player_.impactEffectTimer = snapshot.bluePlane.impactEffectTimer;
    player_.impactEffectX = snapshot.bluePlane.impactEffectX;
    player_.impactEffectY = snapshot.bluePlane.impactEffectY;
    player_.hp = snapshot.bluePlane.hp;
    player_.score = snapshot.bluePlane.score;
    player_.isAlive = snapshot.bluePlane.isAlive;
    player_.grounded = snapshot.bluePlane.grounded;
    player_.exploding = snapshot.bluePlane.exploding;
    syncPlaneSprite(player_);

    enemy_.x = snapshot.redPlane.x;
    enemy_.y = snapshot.redPlane.y;
    enemy_.angle = snapshot.redPlane.angle;
    enemy_.groundSpeed = snapshot.redPlane.groundSpeed;
    enemy_.respawnTimer = snapshot.redPlane.respawnTimer;
    enemy_.explosionTimer = snapshot.redPlane.explosionTimer;
    enemy_.damageEffectTimer = snapshot.redPlane.damageEffectTimer;
    enemy_.predictedExplosionTimer = snapshot.redPlane.predictedExplosionTimer;
    enemy_.impactEffectTimer = snapshot.redPlane.impactEffectTimer;
    enemy_.impactEffectX = snapshot.redPlane.impactEffectX;
    enemy_.impactEffectY = snapshot.redPlane.impactEffectY;
    enemy_.hp = snapshot.redPlane.hp;
    enemy_.score = snapshot.redPlane.score;
    enemy_.isAlive = snapshot.redPlane.isAlive;
    enemy_.grounded = snapshot.redPlane.grounded;
    enemy_.exploding = snapshot.redPlane.exploding;
    syncPlaneSprite(enemy_);

    auto &blueProjectiles = playerBullets_.getProjectiles();
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        blueProjectiles[i].x = snapshot.blueProjectiles[i].x;
        blueProjectiles[i].y = snapshot.blueProjectiles[i].y;
        blueProjectiles[i].velX = snapshot.blueProjectiles[i].velX;
        blueProjectiles[i].velY = snapshot.blueProjectiles[i].velY;
        blueProjectiles[i].lifetime = snapshot.blueProjectiles[i].lifetime;
        blueProjectiles[i].spawnedByInputSequence = snapshot.blueProjectiles[i].spawnedByInputSequence;
        blueProjectiles[i].active = snapshot.blueProjectiles[i].active;
        blueProjectiles[i].sprite.visible = snapshot.blueProjectiles[i].active;
        blueProjectiles[i].sprite.x = snapshot.blueProjectiles[i].x;
        blueProjectiles[i].sprite.y = snapshot.blueProjectiles[i].y;
    }

    auto &redProjectiles = enemyBullets_.getProjectiles();
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        redProjectiles[i].x = snapshot.redProjectiles[i].x;
        redProjectiles[i].y = snapshot.redProjectiles[i].y;
        redProjectiles[i].velX = snapshot.redProjectiles[i].velX;
        redProjectiles[i].velY = snapshot.redProjectiles[i].velY;
        redProjectiles[i].lifetime = snapshot.redProjectiles[i].lifetime;
        redProjectiles[i].spawnedByInputSequence = snapshot.redProjectiles[i].spawnedByInputSequence;
        redProjectiles[i].active = snapshot.redProjectiles[i].active;
        redProjectiles[i].sprite.visible = snapshot.redProjectiles[i].active;
        redProjectiles[i].sprite.x = snapshot.redProjectiles[i].x;
        redProjectiles[i].sprite.y = snapshot.redProjectiles[i].y;
    }

    hud_.setScores(player_.score, enemy_.score);
}

bool GameSession::consumeQueuedRemoteInput(uint16_t sequence, BluetoothInputState &outInput) {
    for (auto it = pendingRemoteInputs_.begin(); it != pendingRemoteInputs_.end(); ++it) {
        if (it->sequence == sequence) {
            outInput = *it;
            pendingRemoteInputs_.erase(it);
            return true;
        }
        if (isSequenceNewer(it->sequence, sequence)) {
            break;
        }
    }
    return false;
}

void GameSession::rebuildRollbackFrom(uint16_t sequence) {
    auto rollbackStart = rollbackFrames_.end();
    for (auto it = rollbackFrames_.begin(); it != rollbackFrames_.end(); ++it) {
        if (it->sequence == sequence) {
            rollbackStart = it;
            break;
        }
    }
    if (rollbackStart == rollbackFrames_.end()) {
        return;
    }

    restoreSimulationSnapshot(rollbackStart->snapshot);
    for (auto it = rollbackStart; it != rollbackFrames_.end(); ++it) {
        it->snapshot = captureSimulationSnapshot();
        simulateBluetoothRollbackFrame(it->dt, it->localInput, it->remoteInput, false);
    }

    lastConfirmedRemoteInput_ = {};
    hasLastConfirmedRemoteInput_ = false;
    predictedRemoteInputAge_ = 0;
    for (auto it = rollbackFrames_.rbegin(); it != rollbackFrames_.rend(); ++it) {
        if (it->remoteConfirmed) {
            lastConfirmedRemoteInput_ = it->remoteInput;
            lastConfirmedRemoteInput_.fireTapped = false;
            hasLastConfirmedRemoteInput_ = true;
            break;
        }
        predictedRemoteInputAge_ = static_cast<uint8_t>(std::min<int>(predictedRemoteInputAge_ + 1, 255));
    }

    hud_.setScores(player_.score, enemy_.score);
}

void GameSession::trimRollbackHistory() {
    while (rollbackFrames_.size() > ROLLBACK_HISTORY_LIMIT) {
        rollbackFrames_.pop_front();
    }
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

void GameSession::applyOwnedRemotePlaneState(
    Plane &plane,
    const BluetoothPlaneState &state,
    float dt,
    bool smooth,
    float smoothingSpeed
) {
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const bool shouldSnap = plane.grounded != state.grounded
                            || fabsf(shortestWrappedDx(plane.x, state.x, worldHalfW)) > 1.6f
                            || fabsf(plane.y - state.y) > 0.8f;
    if (!smooth || shouldSnap) {
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
    plane.grounded = state.grounded;
    syncPlaneSprite(plane);
}

void GameSession::applyAuthoritativePlaneFields(Plane &plane, const BluetoothPlaneState &state) {
    plane.respawnTimer = state.respawnTimer;
    plane.explosionTimer = state.explosionTimer;
    plane.hp = state.hp;
    plane.score = state.score;
    plane.isAlive = state.isAlive;
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
    Plane reconciledPlane = plane;
    applyPlaneState(reconciledPlane, state, 0.f, false, CLIENT_PLANE_SMOOTHING);
    for (const auto &inputFrame : pendingLocalInputs_) {
        reconciledPlane.update(inputFrame.dt, inputFrame.upButtonHeld, inputFrame.downButtonHeld);
    }
    reconciledPlane.damageEffectTimer = preservedDamageEffectTimer;

    if (!shouldSmoothCorrection) {
        plane = reconciledPlane;
        localPlaneRenderSmoothing_ = {};
        syncPlaneSprite(plane);
        return;
    }

    const float dx = fabsf(shortestWrappedDx(previousX, reconciledPlane.x, worldHalfW));
    const float dy = fabsf(reconciledPlane.y - previousY);
    const float da = fabsf(shortestAngleDelta(previousAngle, reconciledPlane.angle));
    if (dx <= CLIENT_LOCAL_RECONCILE_DEAD_ZONE_X
        && dy <= CLIENT_LOCAL_RECONCILE_DEAD_ZONE_Y
        && da <= CLIENT_LOCAL_RECONCILE_DEAD_ZONE_ANGLE) {
        localPlaneRenderSmoothing_ = {};
        syncPlaneSprite(plane);
        return;
    }

    float correctionAlpha = CLIENT_LOCAL_RECONCILE_LARGE_ALPHA;
    if (dx <= CLIENT_LOCAL_SOFT_RECONCILE_MAX_X
        && dy <= CLIENT_LOCAL_SOFT_RECONCILE_MAX_Y
        && da <= CLIENT_LOCAL_SOFT_RECONCILE_MAX_ANGLE) {
        const float normalizedMagnitude = std::max({
            dx / CLIENT_LOCAL_SOFT_RECONCILE_MAX_X,
            dy / CLIENT_LOCAL_SOFT_RECONCILE_MAX_Y,
            da / CLIENT_LOCAL_SOFT_RECONCILE_MAX_ANGLE
        });
        correctionAlpha = lerpFloat(
            CLIENT_LOCAL_SOFT_RECONCILE_MIN_ALPHA,
            CLIENT_LOCAL_SOFT_RECONCILE_MAX_ALPHA,
            normalizedMagnitude
        );
    }

    plane.x = Utility::wrapWorldX(
        previousX + shortestWrappedDx(previousX, reconciledPlane.x, worldHalfW) * correctionAlpha,
        worldHalfW,
        PLANE_HALF_W
    );
    plane.y = previousY + (reconciledPlane.y - previousY) * correctionAlpha;
    plane.angle = normalizeAngle(
        previousAngle + shortestAngleDelta(previousAngle, reconciledPlane.angle) * correctionAlpha
    );
    plane.groundSpeed = previousGroundSpeed
                        + (reconciledPlane.groundSpeed - previousGroundSpeed) * correctionAlpha;
    plane.respawnTimer = reconciledPlane.respawnTimer;
    plane.explosionTimer = reconciledPlane.explosionTimer;
    plane.hp = reconciledPlane.hp;
    plane.score = reconciledPlane.score;
    plane.isAlive = reconciledPlane.isAlive;
    plane.grounded = reconciledPlane.grounded;
    plane.exploding = reconciledPlane.exploding;
    plane.damageEffectTimer = preservedDamageEffectTimer;
    localPlaneRenderSmoothing_ = {};
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

float GameSession::currentRemoteOwnedInterpolationDelay() const {
    const float delay = remoteOwnedSnapshotInterval_ * 0.35f
                        + remoteOwnedSnapshotJitter_ * 0.15f;
    return std::clamp(delay, REMOTE_OWNED_SNAPSHOT_BUFFER_MIN, REMOTE_OWNED_SNAPSHOT_BUFFER_MAX);
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

void GameSession::pushRemoteOwnedSnapshot(const BluetoothMatchState &state) {
    const float offsetSample = std::max(0.f, simulationTime_ - state.hostTimestamp);
    if (!hasRemoteOwnedClockOffset_) {
        remoteOwnedClockOffset_ = offsetSample;
        hasRemoteOwnedClockOffset_ = true;
    } else if (offsetSample < remoteOwnedClockOffset_) {
        remoteOwnedClockOffset_ = lerpFloat(remoteOwnedClockOffset_, offsetSample, 0.35f);
    } else {
        remoteOwnedClockOffset_ = lerpFloat(remoteOwnedClockOffset_, offsetSample, 0.08f);
    }

    if (!remoteOwnedSnapshotBuffer_.empty()
        && state.hostTimestamp <= remoteOwnedSnapshotBuffer_.back().state.hostTimestamp) {
        remoteOwnedSnapshotBuffer_.back() = BufferedMatchSnapshot{state, simulationTime_};
        return;
    }

    if (!remoteOwnedSnapshotBuffer_.empty()) {
        const float remoteInterval = std::max(
            0.0001f,
            state.hostTimestamp - remoteOwnedSnapshotBuffer_.back().state.hostTimestamp
        );
        const float arrivalInterval = std::max(
            0.f,
            simulationTime_ - remoteOwnedSnapshotBuffer_.back().receivedAt
        );
        remoteOwnedSnapshotInterval_ = lerpFloat(remoteOwnedSnapshotInterval_, remoteInterval, 0.18f);
        remoteOwnedSnapshotJitter_ = lerpFloat(
            remoteOwnedSnapshotJitter_,
            fabsf(arrivalInterval - remoteInterval),
            0.18f
        );
    }

    remoteOwnedSnapshotBuffer_.push_back(BufferedMatchSnapshot{state, simulationTime_});
    while (remoteOwnedSnapshotBuffer_.size() > CLIENT_SNAPSHOT_HISTORY_LIMIT) {
        remoteOwnedSnapshotBuffer_.pop_front();
    }
}

bool GameSession::sampleRemoteOwnedSnapshot(BluetoothMatchState &outState) const {
    if (remoteOwnedSnapshotBuffer_.empty()) {
        return false;
    }

    if (remoteOwnedSnapshotBuffer_.size() == 1) {
        outState = remoteOwnedSnapshotBuffer_.front().state;
        const float worldHalfW = worldHalfWidthForAspect(aspect_);
        const float estimatedRemoteNow = hasRemoteOwnedClockOffset_
                                         ? (simulationTime_ - remoteOwnedClockOffset_)
                                         : remoteOwnedSnapshotBuffer_.front().state.hostTimestamp;
        const float extrapolationDt = std::min(
            std::max(0.f, estimatedRemoteNow - remoteOwnedSnapshotBuffer_.front().state.hostTimestamp),
            REMOTE_OWNED_EXTRAPOLATION_MAX
        );
        outState.redPlane = extrapolatePlaneState(outState.redPlane, extrapolationDt, worldHalfW);
        return true;
    }

    const float estimatedRemoteNow = hasRemoteOwnedClockOffset_
                                     ? (simulationTime_ - remoteOwnedClockOffset_)
                                     : remoteOwnedSnapshotBuffer_.back().state.hostTimestamp;
    const float renderTime = estimatedRemoteNow - currentRemoteOwnedInterpolationDelay();
    const float worldHalfW = worldHalfWidthForAspect(aspect_);
    const BufferedMatchSnapshot *older = &remoteOwnedSnapshotBuffer_.front();
    const BufferedMatchSnapshot *newer = &remoteOwnedSnapshotBuffer_.back();

    for (size_t i = 1; i < remoteOwnedSnapshotBuffer_.size(); ++i) {
        if (remoteOwnedSnapshotBuffer_[i].state.hostTimestamp >= renderTime) {
            older = &remoteOwnedSnapshotBuffer_[i - 1];
            newer = &remoteOwnedSnapshotBuffer_[i];
            break;
        }
    }

    if (renderTime >= remoteOwnedSnapshotBuffer_.back().state.hostTimestamp) {
        outState = remoteOwnedSnapshotBuffer_.back().state;
        const float extrapolationDt = std::min(
            renderTime - remoteOwnedSnapshotBuffer_.back().state.hostTimestamp,
            REMOTE_OWNED_EXTRAPOLATION_MAX
        );
        outState.redPlane = extrapolatePlaneState(outState.redPlane, extrapolationDt, worldHalfW);
        return true;
    }

    if (renderTime <= remoteOwnedSnapshotBuffer_.front().state.hostTimestamp) {
        outState = remoteOwnedSnapshotBuffer_.front().state;
        return true;
    }

    const float span = std::max(0.0001f, newer->state.hostTimestamp - older->state.hostTimestamp);
    const float alpha = std::clamp((renderTime - older->state.hostTimestamp) / span, 0.f, 1.f);
    outState = newer->state;
    outState.redPlane = interpolatePlaneState(older->state.redPlane, newer->state.redPlane, alpha, worldHalfW);
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

void GameSession::checkCollisions(bool emitAudio) {
    if (enemy_.isAlive && !enemy_.exploding && !enemy_.isInvulnerable()) {
        for (auto &p : playerBullets_.getProjectiles()) {
            if (!p.active) continue;
            if (Utility::aabbOverlap(p.x, p.y, BULLET_HALF_SIZE, BULLET_HALF_SIZE,
                                     enemy_.x, enemy_.y, PLANE_HALF_W, PLANE_HALF_H)) {
                p.active = false;
                p.sprite.visible = false;
                if (enemy_.hitPlane()) {
                    player_.score++;
                    if (emitAudio) {
                        sound_.playExplosion();
                    }
                } else {
                    if (emitAudio) {
                        sound_.playHit();
                    }
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
                    if (emitAudio) {
                        sound_.playExplosion();
                    }
                } else {
                    if (emitAudio) {
                        sound_.playHit();
                    }
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
            if (emitAudio) {
                sound_.playExplosion();
            }
        }
    }
    if (enemy_.isAlive && !enemy_.exploding && !enemy_.isInvulnerable()) {
        if (Utility::aabbOverlap(enemy_.x, enemy_.y, PLANE_HALF_W, PLANE_HALF_H,
                                 HOUSE_X, BARN_Y, BARN_HALF_W, BARN_HALF_H)) {
            enemy_.triggerExplosion();
            player_.score++;
            if (emitAudio) {
                sound_.playExplosion();
            }
        }
    }

    if (player_.isAlive && !player_.exploding && !player_.isInvulnerable()
        && !player_.grounded && player_.y <= GROUND_Y) {
        player_.triggerExplosion();
        enemy_.score++;
        if (emitAudio) {
            sound_.playExplosion();
        }
    }
    if (enemy_.isAlive && !enemy_.exploding && !enemy_.isInvulnerable()
        && !enemy_.grounded && enemy_.y <= GROUND_Y) {
        enemy_.triggerExplosion();
        player_.score++;
        if (emitAudio) {
            sound_.playExplosion();
        }
    }
}

void GameSession::playerFire(uint16_t spawnedByInputSequence, bool emitAudio) {
    spawnProjectileFromPlane(
        playerBullets_,
        player_,
        worldHalfWidthForAspect(aspect_),
        spawnedByInputSequence
    );
    if (emitAudio) {
        sound_.playShoot();
    }
}

void GameSession::enemyFire(uint16_t spawnedByInputSequence, bool emitAudio) {
    spawnProjectileFromPlane(
        enemyBullets_,
        enemy_,
        worldHalfWidthForAspect(aspect_),
        spawnedByInputSequence
    );
    if (emitAudio) {
        sound_.playShoot();
    }
}
