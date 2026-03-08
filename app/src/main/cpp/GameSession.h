#ifndef BYPLANES_GAMESESSION_H
#define BYPLANES_GAMESESSION_H

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
    bool updateBluetoothHost(float dt, const TouchState &localInput, const BluetoothInputState &remoteInput);
    void updateBluetoothClient(float dt, const TouchState &localInput, uint16_t localInputSequence);
    void draw(const Shader &shader) const;
    int getWinner() const;
    void reset();
    BluetoothMatchState buildBluetoothMatchState(uint16_t acknowledgedInputSequence) const;
    void applyBluetoothMatchState(const BluetoothMatchState &state);

private:
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

    void checkCollisions();
    void playerFire(uint16_t spawnedByInputSequence = 0);
    void enemyFire(uint16_t spawnedByInputSequence = 0);
    static TouchState makePlaneTouchState(bool upHeld, bool downHeld, bool fireTapped);
    static TouchState makeHumanTouchStateForPlane(const Plane &plane, bool upHeld, bool downHeld, bool fireTapped);
    void applyPlaneState(Plane &plane, const BluetoothPlaneState &state, float dt, bool smooth, float smoothingSpeed);
    void applyLocalPlaneRenderSmoothing(Plane &plane, float dt);
    void recordPredictedLocalInput(uint16_t sequence, float dt, const TouchState &predictedInput);
    void reconcilePredictedLocalPlane(Plane &plane, const BluetoothPlaneState &state, uint16_t acknowledgedInputSequence);
    void predictBluetoothClientEffects();
    void reconcileLocalProjectiles(
        ProjectilePool &pool,
        const BluetoothProjectileState &state,
        float dt,
        uint16_t acknowledgedInputSequence,
        const Plane &firingPlane
    );
    void pushBufferedSnapshot(const BluetoothMatchState &state);
    bool sampleBufferedSnapshot(BluetoothMatchState &outState) const;
    float currentInterpolationDelay() const;
    static void captureProjectiles(const ProjectilePool &pool, BluetoothProjectileState &outState);
    void applyProjectiles(ProjectilePool &pool, const BluetoothProjectileState &state, float dt, bool smooth);

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
    bool clientHasTargetState_ = false;
    bool clientStateInitialized_ = false;
    BluetoothMatchState clientTargetState_{};
    std::deque<PredictedInputFrame> pendingLocalInputs_;
    std::deque<BufferedMatchSnapshot> snapshotBuffer_;
    PlaneRenderSmoothingState localPlaneRenderSmoothing_{};
    float clientClock_ = 0.f;
    float hostToLocalClockOffset_ = 0.f;
    bool hasHostClockOffset_ = false;
    float smoothedRtt_ = 0.08f;
    float smoothedSnapshotInterval_ = 1.f / 60.f;
    float snapshotJitter_ = 0.01f;
    float simulationTime_ = 0.f;
    float aspect_ = 1.f;

    float playerFireCooldown_ = 0.f;
    float enemyFireCooldown_ = 0.f;
};

#endif //BYPLANES_GAMESESSION_H
