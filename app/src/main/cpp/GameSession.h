#ifndef BIPLANES_GAMESESSION_H
#define BIPLANES_GAMESESSION_H

#include <array>
#include <deque>

#include "Plane.h"
#include "Projectile.h"
#include "Background.h"
#include "HUD.h"
#include "AIController.h"
#include "BluetoothBridge.h"
#include "GameState.h"
#include "InputManager.h"
#include "SoundEngine.h"

class GameSession {
public:
    void init(AAssetManager *assetManager, float aspect, GameMode mode, AiDifficulty difficulty, bool localControlsBlue);
    bool update(float dt, const TouchState &input);
    bool updateBluetoothHost(float dt, const TouchState &localInput);
    void updateBluetoothClient(float dt, const TouchState &localInput);
    bool updateBluetoothRollback(float dt, const BluetoothInputState &localInput);
    void queueBluetoothRemoteInput(const BluetoothInputState &input);
    void processBluetoothRollbackCorrections();
    void draw(const Shader &shader) const;
    int getWinner() const;
    void reset();
    BluetoothMatchState buildBluetoothMatchState(uint16_t stateSequence) const;
    void applyBluetoothMatchState(const BluetoothMatchState &state);
    void applyBluetoothRemoteOwnedState(const BluetoothMatchState &state);
    void setBluetoothRemoteRenderAlpha(float alpha);
    void updateBluetoothRemoteRender(float dt);

private:
    struct PlaneSimulationState {
        float x = 0.f;
        float y = 0.f;
        float angle = 0.f;
        float groundSpeed = 0.f;
        float respawnTimer = 0.f;
        float explosionTimer = 0.f;
        float damageEffectTimer = 0.f;
        float predictedExplosionTimer = 0.f;
        float impactEffectTimer = 0.f;
        float impactEffectX = 0.f;
        float impactEffectY = 0.f;
        int hp = PLANE_MAX_HP;
        int score = 0;
        uint16_t lastResolvedProjectileId = 0;
        bool isAlive = true;
        bool grounded = false;
        bool exploding = false;
    };

    struct ProjectileSimulationState {
        uint16_t id = 0;
        float x = 0.f;
        float y = 0.f;
        float velX = 0.f;
        float velY = 0.f;
        float lifetime = 0.f;
        uint16_t spawnedByInputSequence = 0;
        bool predictedRemoteImpact = false;
        bool active = false;
    };

    struct SimulationSnapshot {
        PlaneSimulationState bluePlane{};
        PlaneSimulationState redPlane{};
        std::array<ProjectileSimulationState, MAX_PROJECTILES> blueProjectiles{};
        std::array<ProjectileSimulationState, MAX_PROJECTILES> redProjectiles{};
        float playerFireCooldown = 0.f;
        float enemyFireCooldown = 0.f;
        float simulationTime = 0.f;
    };

    struct RollbackFrame {
        uint16_t sequence = 0;
        float dt = 0.f;
        SimulationSnapshot snapshot{};
        BluetoothInputState localInput{};
        BluetoothInputState remoteInput{};
        bool remoteConfirmed = false;
    };

    struct PredictedInputFrame {
        uint16_t sequence = 0;
        float sentAt = 0.f;
        float dt = 0.f;
        bool upButtonHeld = false;
        bool downButtonHeld = false;
    };

    struct BufferedMatchSnapshot {
        BluetoothMatchState state{};
        float receivedAt = 0.f;
    };

    struct PlaneRenderSmoothingState {
        float xError = 0.f;
        float yError = 0.f;
        float angleError = 0.f;
    };

    struct RemotePlaneRenderSample {
        float x = 0.f;
        float y = 0.f;
        float angle = 0.f;
        bool grounded = false;
        bool isAlive = true;
        bool exploding = false;
        bool valid = false;
    };

    void checkCollisions(bool emitAudio = true);
    void playerFire(uint16_t spawnedByInputSequence = 0, bool emitAudio = true);
    void enemyFire(uint16_t spawnedByInputSequence = 0, bool emitAudio = true);
    static TouchState makePlaneTouchState(bool upHeld, bool downHeld, bool fireTapped);
    static TouchState makeHumanTouchStateForPlane(const Plane &plane, bool upHeld, bool downHeld, bool fireTapped);
    void simulateBluetoothRollbackFrame(
        float dt,
        const BluetoothInputState &localInput,
        const BluetoothInputState &remoteInput,
        bool emitAudio
    );
    SimulationSnapshot captureSimulationSnapshot() const;
    void restoreSimulationSnapshot(const SimulationSnapshot &snapshot);
    bool consumeQueuedRemoteInput(uint16_t sequence, BluetoothInputState &outInput);
    void rebuildRollbackFrom(uint16_t sequence);
    void trimRollbackHistory();
    void applyPlaneState(Plane &plane, const BluetoothPlaneState &state, float dt, bool smooth, float smoothingSpeed);
    void applyOwnedRemotePlaneState(Plane &plane, const BluetoothPlaneState &state, float dt, bool smooth, float smoothingSpeed);
    static void applyAuthoritativePlaneFields(Plane &plane, const BluetoothPlaneState &state);
    void applyAuthoritativeHitConfirmation(
        Plane &plane,
        const BluetoothPlaneState &state,
        ProjectilePool &ownedProjectiles
    );
    void applyLocalPlaneRenderSmoothing(Plane &plane, float dt);
    void recordPredictedLocalInput(uint16_t sequence, float dt, const TouchState &predictedInput);
    void reconcilePredictedLocalPlane(
        Plane &plane,
        const BluetoothPlaneState &state,
        uint16_t acknowledgedInputSequence
    );
    void predictBluetoothClientEffects();
    void checkBluetoothLocalAuthoritativeCollisions(bool emitAudio = true);
    static void deactivateProjectile(Projectile &projectile);
    static void consumeProjectileById(ProjectilePool &pool, uint16_t projectileId);
    void rememberConsumedRemoteProjectile(uint16_t projectileId);
    bool isRemoteProjectileConsumed(uint16_t projectileId) const;
    void reconcileLocalProjectiles(
        ProjectilePool &pool,
        const BluetoothProjectileState &state,
        float dt,
        uint16_t acknowledgedInputSequence,
        const Plane &firingPlane
    );
    RemotePlaneRenderSample captureRemotePlaneRenderSample() const;
    bool sampleRemotePlaneRenderTarget(RemotePlaneRenderSample &outSample) const;
    void resetRemotePlaneRenderState();
    void advanceRemotePlaneRenderSample();
    void drawRemotePlane(const Shader &shader, const Plane &plane) const;
    bool computeRemotePlaneRenderOffset(float &outOffsetX, float &outOffsetY) const;
    void resetRemoteBulletRenderState();
    void updateBulletRenderVisibleAges(
        const ProjectilePool &pool,
        std::array<float, MAX_PROJECTILES> &visibleAges,
        std::array<bool, MAX_PROJECTILES> &wasActive,
        std::array<uint16_t, MAX_PROJECTILES> &trackedIds,
        std::array<float, MAX_PROJECTILES> &originX,
        std::array<float, MAX_PROJECTILES> &originY,
        float renderedAngle,
        float renderedMuzzleX,
        float renderedMuzzleY,
        float dt
    );
    void drawProjectilePoolWithOffset(
        const Shader &shader,
        const ProjectilePool &pool,
        const std::array<float, MAX_PROJECTILES> &visibleAges,
        const std::array<float, MAX_PROJECTILES> &originX,
        const std::array<float, MAX_PROJECTILES> &originY,
        float offsetX,
        float offsetY,
        float fadeDuration
    ) const;
    void pushBufferedSnapshot(const BluetoothMatchState &state);
    bool sampleBufferedSnapshot(BluetoothMatchState &outState) const;
    float currentInterpolationDelay() const;
    void pushRemoteOwnedSnapshot(const BluetoothMatchState &state);
    bool sampleRemoteOwnedSnapshot(BluetoothMatchState &outState) const;
    float currentRemoteOwnedInterpolationDelay() const;
    static void captureProjectiles(const ProjectilePool &pool, BluetoothProjectileState &outState);
    void applyProjectiles(
        ProjectilePool &pool,
        const BluetoothProjectileState &state,
        float dt,
        bool smooth,
        bool ignoreConsumedProjectiles = false,
        bool emitAudio = false
    );

    Plane player_;
    Plane enemy_;
    ProjectilePool playerBullets_;
    ProjectilePool enemyBullets_;
    Background background_;
    HUD hud_;
    AIController ai_;
    SoundEngine sound_;
    GameMode mode_ = GameMode::VsAI;
    bool localControlsBlue_ = true;
    std::deque<RollbackFrame> rollbackFrames_;
    std::deque<BluetoothInputState> pendingRemoteInputs_;
    BluetoothInputState lastConfirmedRemoteInput_{};
    bool hasLastConfirmedRemoteInput_ = false;
    uint8_t predictedRemoteInputAge_ = 0;
    bool hasPendingRollbackCorrection_ = false;
    uint16_t pendingRollbackSequence_ = 0;
    bool hasRemoteOwnedState_ = false;
    BluetoothMatchState remoteOwnedState_{};
    std::deque<BufferedMatchSnapshot> remoteOwnedSnapshotBuffer_;
    bool clientHasTargetState_ = false;
    bool clientStateInitialized_ = false;
    BluetoothMatchState clientTargetState_{};
    std::deque<PredictedInputFrame> pendingLocalInputs_;
    std::deque<BufferedMatchSnapshot> snapshotBuffer_;
    PlaneRenderSmoothingState localPlaneRenderSmoothing_{};
    std::deque<RemotePlaneRenderSample> remotePlaneRenderHistory_;
    RemotePlaneRenderSample renderedRemotePlaneRenderSample_{};
    std::array<float, MAX_PROJECTILES> playerBulletRenderVisibleAges_{};
    std::array<float, MAX_PROJECTILES> enemyBulletRenderVisibleAges_{};
    std::array<bool, MAX_PROJECTILES> playerBulletRenderWasActive_{};
    std::array<bool, MAX_PROJECTILES> enemyBulletRenderWasActive_{};
    std::array<uint16_t, MAX_PROJECTILES> playerBulletRenderIds_{};
    std::array<uint16_t, MAX_PROJECTILES> enemyBulletRenderIds_{};
    std::array<float, MAX_PROJECTILES> playerBulletRenderOriginX_{};
    std::array<float, MAX_PROJECTILES> playerBulletRenderOriginY_{};
    std::array<float, MAX_PROJECTILES> enemyBulletRenderOriginX_{};
    std::array<float, MAX_PROJECTILES> enemyBulletRenderOriginY_{};
    float clientClock_ = 0.f;
    float hostToLocalClockOffset_ = 0.f;
    bool hasHostClockOffset_ = false;
    float remoteOwnedClockOffset_ = 0.f;
    bool hasRemoteOwnedClockOffset_ = false;
    float smoothedRtt_ = 0.08f;
    float smoothedSnapshotInterval_ = 1.f / 60.f;
    float snapshotJitter_ = 0.01f;
    float remoteOwnedSnapshotInterval_ = 1.f / 60.f;
    float remoteOwnedSnapshotJitter_ = 0.01f;
    float simulationTime_ = 0.f;
    float aspect_ = 1.f;
    float bluetoothRemoteRenderAlpha_ = 1.f;
    uint16_t nextProjectileId_ = 0;

    float playerFireCooldown_ = 0.f;
    float enemyFireCooldown_ = 0.f;
    std::deque<uint16_t> consumedRemoteProjectileIds_;
};

#endif //BIPLANES_GAMESESSION_H
