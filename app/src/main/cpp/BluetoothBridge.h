#pragma once
#include <jni.h>
#include <mutex>
#include <vector>
#include <utility>
#include <cstdint>

// Forward declare ProjectilePool to avoid circular includes
// We'll pass projectile data as raw arrays
struct BluetoothState {
    float planeY     = 0.f;
    float planeVelY  = 0.f;
    uint8_t projCount = 0;
    float projX[5]   = {};
    float projY[5]   = {};
};

class BluetoothBridge {
public:
    BluetoothBridge(JNIEnv* env, jobject activity);
    ~BluetoothBridge();

    // Send our plane state to the remote player (called every frame in BT mode)
    void sendState(float planeY, float planeVelY,
                   const float* projX, const float* projY, int projCount);

    // Poll latest received state from remote player (non-blocking)
    // Returns true if new data is available
    bool pollReceivedState(BluetoothState& outState);

    bool isConnected() const;
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
    jmethodID  isConnectedMethod_;
    jmethodID  startAdvMethod_;
    jmethodID  startScanMethod_;
    jmethodID  disconnectMethod_;

    mutable std::mutex mutex_;
    BluetoothState     latestState_;
    bool               hasNewState_ = false;
};
