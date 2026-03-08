#pragma once
#include <jni.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include "GameConstants.h"

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
    bool isAlive = true;
    bool grounded = false;
    bool exploding = false;
};

struct BluetoothProjectileState {
    uint8_t count = 0;
    float x[MAX_PROJECTILES] = {};
    float y[MAX_PROJECTILES] = {};
};

struct BluetoothMatchState {
    uint16_t acknowledgedInputSequence = 0;
    float hostTimestamp = 0.f;
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
    BluetoothMatchState latestMatchState_;
    bool                hasNewMatchState_ = false;
    std::deque<BluetoothInputState> pendingInputStates_;
    ControlSignal      latestControlSignal_ = ControlSignal::None;
    bool               hasNewControlSignal_ = false;
};
