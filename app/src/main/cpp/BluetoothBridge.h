#pragma once
#include <jni.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include "GameConstants.h"

enum class BluetoothAuthority : uint8_t {
    Blue = 0,
    Red = 1
};

struct BluetoothInputState {
    bool upButtonHeld = false;
    bool downButtonHeld = false;
    bool fireTapped = false;
    uint16_t sequence = 0;
};

struct BluetoothPlaneState {
    float x = 0.f;
    float y = 0.f;
    float angle = 0.f;
    float groundSpeed = 0.f;
    float respawnTimer = 0.f;
    float explosionTimer = 0.f;
    uint8_t hp = PLANE_MAX_HP;
    uint8_t score = 0;
    uint16_t lastResolvedProjectileId = 0;
    bool isAlive = true;
    bool grounded = false;
    bool exploding = false;
};

struct BluetoothProjectileState {
    uint8_t count = 0;
    uint16_t id[MAX_PROJECTILES] = {};
    float x[MAX_PROJECTILES] = {};
    float y[MAX_PROJECTILES] = {};
};

struct BluetoothMatchState {
    BluetoothAuthority authority = BluetoothAuthority::Blue;
    uint16_t stateSequence = 0;
    float simulationTimestamp = 0.f;
    BluetoothPlaneState bluePlane;
    BluetoothPlaneState redPlane;
    BluetoothProjectileState blueProjectiles;
    BluetoothProjectileState redProjectiles;
};

class BluetoothBridge {
public:
    enum class ControlSignal : uint8_t {
        None = 0,
        Pause = 1,
        Resume = 2,
        EndMatch = 3
    };

    BluetoothBridge(JNIEnv* env, jobject activity);
    ~BluetoothBridge();

    void sendMatchState(const BluetoothMatchState& state);
    void sendInputState(const BluetoothInputState& input);
    bool pollReceivedMatchState(BluetoothMatchState& outState);
    bool pollReceivedInputState(BluetoothInputState& outInput);
    bool pollControlSignal(ControlSignal& outSignal);
    void sendControlSignal(ControlSignal signal);
    void sendControlSignalAndDisconnect(ControlSignal signal);

    bool isConnected() const;
    bool isReady() const;
    bool isHostRole() const;
    void showCurrentRoleToast();
    void dismissConnectionDialogs();
    void startAdvertising();
    void startScanning();
    void disconnect();
    void clearPendingGameState();

    // Called from JNI callback -- thread safe
    void onPacketReceived(const uint8_t* data, int len);

    static BluetoothBridge* instance_; // for JNI callback routing

private:
    JNIEnv*    env_;
    jobject    btManager_;      // global ref to BluetoothManager Kotlin object
    jmethodID  sendMethod_;
    jmethodID  sendAndDisconnectMethod_;
    jmethodID  isConnectedMethod_;
    jmethodID  isHostMethod_;
    jmethodID  showRoleToastMethod_;
    jmethodID  dismissDialogsMethod_;
    jmethodID  startAdvMethod_;
    jmethodID  startScanMethod_;
    jmethodID  disconnectMethod_;
    bool       initialized_ = false;

    mutable std::mutex mutex_;
    std::deque<BluetoothMatchState> pendingMatchStates_;
    std::deque<BluetoothInputState> pendingInputStates_;
    ControlSignal      latestControlSignal_ = ControlSignal::None;
    bool               hasNewControlSignal_ = false;
};
