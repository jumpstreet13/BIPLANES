#include "BluetoothBridge.h"
#include "AndroidOut.h"
#include <cstring>
#include <algorithm>

BluetoothBridge* BluetoothBridge::instance_ = nullptr;

// Packet layout (49 bytes max):
// [0-3]   float planeY
// [4-7]   float planeVelY
// [8]     uint8 projCount
// [9-28]  float projX[5]
// [29-48] float projY[5]
static constexpr int PACKET_SIZE = 49;

BluetoothBridge::BluetoothBridge(JNIEnv* env, jobject activity)
    : env_(env) {

    jclass btClass = env->FindClass("com/abocha/byplanes/BluetoothManager");
    if (!btClass) {
        aout << "BluetoothBridge: BluetoothManager class not found" << std::endl;
        btManager_ = nullptr;
        return;
    }

    jmethodID ctor = env->GetMethodID(btClass, "<init>",
        "(Lcom/google/androidgamesdk/GameActivity;)V");
    jobject localRef = env->NewObject(btClass, ctor, activity);
    btManager_ = env->NewGlobalRef(localRef);
    env->DeleteLocalRef(localRef);

    sendMethod_        = env->GetMethodID(btClass, "sendPacket", "([B)V");
    isConnectedMethod_ = env->GetMethodID(btClass, "isConnected", "()Z");
    startAdvMethod_    = env->GetMethodID(btClass, "startAdvertising", "()V");
    startScanMethod_   = env->GetMethodID(btClass, "startScanning", "()V");
    disconnectMethod_  = env->GetMethodID(btClass, "disconnect", "()V");

    instance_ = this;
    aout << "BluetoothBridge: initialized" << std::endl;
}

BluetoothBridge::~BluetoothBridge() {
    if (btManager_) {
        env_->DeleteGlobalRef(btManager_);
    }
    instance_ = nullptr;
}

void BluetoothBridge::sendState(float planeY, float planeVelY,
                                const float* projX, const float* projY, int projCount) {
    if (!btManager_ || !sendMethod_) return;

    uint8_t packet[PACKET_SIZE] = {};
    memcpy(packet + 0, &planeY,    4);
    memcpy(packet + 4, &planeVelY, 4);
    packet[8] = static_cast<uint8_t>(std::min(projCount, 5));
    for (int i = 0; i < packet[8]; ++i) {
        memcpy(packet + 9  + i * 4, &projX[i], 4);
        memcpy(packet + 29 + i * 4, &projY[i], 4);
    }

    jbyteArray arr = env_->NewByteArray(PACKET_SIZE);
    env_->SetByteArrayRegion(arr, 0, PACKET_SIZE, reinterpret_cast<const jbyte*>(packet));
    env_->CallVoidMethod(btManager_, sendMethod_, arr);
    env_->DeleteLocalRef(arr);
}

bool BluetoothBridge::pollReceivedState(BluetoothState& outState) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasNewState_) return false;
    outState = latestState_;
    hasNewState_ = false;
    return true;
}

bool BluetoothBridge::isConnected() const {
    if (!btManager_ || !isConnectedMethod_) return false;
    return env_->CallBooleanMethod(btManager_, isConnectedMethod_) == JNI_TRUE;
}

void BluetoothBridge::startAdvertising() {
    if (btManager_ && startAdvMethod_)
        env_->CallVoidMethod(btManager_, startAdvMethod_);
}

void BluetoothBridge::startScanning() {
    if (btManager_ && startScanMethod_)
        env_->CallVoidMethod(btManager_, startScanMethod_);
}

void BluetoothBridge::disconnect() {
    if (btManager_ && disconnectMethod_)
        env_->CallVoidMethod(btManager_, disconnectMethod_);
}

void BluetoothBridge::onPacketReceived(const uint8_t* data, int len) {
    if (len < 9) return;
    BluetoothState state;
    memcpy(&state.planeY,    data + 0, 4);
    memcpy(&state.planeVelY, data + 4, 4);
    state.projCount = data[8];
    if (state.projCount > 5) state.projCount = 5;
    for (int i = 0; i < state.projCount; ++i) {
        if (9 + i * 4 + 4 <= len)  memcpy(&state.projX[i], data + 9  + i * 4, 4);
        if (29 + i * 4 + 4 <= len) memcpy(&state.projY[i], data + 29 + i * 4, 4);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    latestState_ = state;
    hasNewState_ = true;
}

// JNI callback invoked from Kotlin receive thread
extern "C" JNIEXPORT void JNICALL
Java_com_abocha_byplanes_BluetoothManager_onPacketReceived(
        JNIEnv* env, jobject /*obj*/, jbyteArray data, jint len) {
    if (!BluetoothBridge::instance_) return;
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    BluetoothBridge::instance_->onPacketReceived(
        reinterpret_cast<const uint8_t*>(bytes), static_cast<int>(len));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}
