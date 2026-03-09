#include "BluetoothBridge.h"
#include "AndroidOut.h"

#include <algorithm>
#include <cstring>

BluetoothBridge* BluetoothBridge::instance_ = nullptr;

namespace {
constexpr uint8_t MATCH_STATE_PACKET_TYPE = 0;
constexpr uint8_t INPUT_PACKET_TYPE = 4;
constexpr uint8_t PLANE_FLAG_ALIVE = 1 << 0;
constexpr uint8_t PLANE_FLAG_GROUNDED = 1 << 1;
constexpr uint8_t PLANE_FLAG_EXPLODING = 1 << 2;
constexpr uint8_t INPUT_FLAG_UP = 1 << 0;
constexpr uint8_t INPUT_FLAG_DOWN = 1 << 1;
constexpr uint8_t INPUT_FLAG_FIRE = 1 << 2;

constexpr int PLANE_PACKET_SIZE = sizeof(float) * 6 + 3 + sizeof(uint16_t);
constexpr int PROJECTILE_PACKET_SIZE = 1 + sizeof(uint16_t) * MAX_PROJECTILES + sizeof(float) * MAX_PROJECTILES * 2;
constexpr int MATCH_PACKET_SIZE =
    1 + 1 + sizeof(uint16_t) + sizeof(float) + PLANE_PACKET_SIZE * 2 + PROJECTILE_PACKET_SIZE * 2;
constexpr int INPUT_PACKET_SIZE = 1 + 1 + sizeof(uint16_t);

bool clearJniException(JNIEnv* env, const char* context) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    aout << "BluetoothBridge: JNI exception in " << context << std::endl;
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

jclass loadBluetoothManagerClass(JNIEnv* env, jobject activity) {
    jclass activityClass = env->GetObjectClass(activity);
    if (!activityClass) {
        clearJniException(env, "GetObjectClass(activity)");
        return nullptr;
    }

    jmethodID getClassLoaderMethod = env->GetMethodID(
            activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getClassLoaderMethod || clearJniException(env, "GameActivity.getClassLoader")) {
        env->DeleteLocalRef(activityClass);
        return nullptr;
    }

    jobject classLoader = env->CallObjectMethod(activity, getClassLoaderMethod);
    env->DeleteLocalRef(activityClass);
    if (!classLoader || clearJniException(env, "CallObjectMethod(getClassLoader)")) {
        return nullptr;
    }

    jclass classLoaderClass = env->GetObjectClass(classLoader);
    if (!classLoaderClass) {
        env->DeleteLocalRef(classLoader);
        clearJniException(env, "GetObjectClass(classLoader)");
        return nullptr;
    }

    jmethodID loadClassMethod = env->GetMethodID(
            classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClassMethod || clearJniException(env, "ClassLoader.loadClass")) {
        env->DeleteLocalRef(classLoaderClass);
        env->DeleteLocalRef(classLoader);
        return nullptr;
    }

    jstring className = env->NewStringUTF("com.abocha.byplanes.BluetoothManager");
    jobject classObject = env->CallObjectMethod(classLoader, loadClassMethod, className);
    env->DeleteLocalRef(className);
    env->DeleteLocalRef(classLoaderClass);
    env->DeleteLocalRef(classLoader);
    if (!classObject || clearJniException(env, "loadClass(BluetoothManager)")) {
        return nullptr;
    }

    return static_cast<jclass>(classObject);
}

void writeFloat(uint8_t*& dst, float value) {
    memcpy(dst, &value, sizeof(float));
    dst += sizeof(float);
}

void writeUint16(uint8_t*& dst, uint16_t value) {
    *dst++ = static_cast<uint8_t>((value >> 8) & 0xFF);
    *dst++ = static_cast<uint8_t>(value & 0xFF);
}

float readFloat(const uint8_t*& src) {
    float value = 0.f;
    memcpy(&value, src, sizeof(float));
    src += sizeof(float);
    return value;
}

uint16_t readUint16(const uint8_t*& src) {
    const uint16_t value = (static_cast<uint16_t>(src[0]) << 8)
                           | static_cast<uint16_t>(src[1]);
    src += sizeof(uint16_t);
    return value;
}

uint8_t encodePlaneFlags(const BluetoothPlaneState& plane) {
    uint8_t flags = 0;
    if (plane.isAlive) flags |= PLANE_FLAG_ALIVE;
    if (plane.grounded) flags |= PLANE_FLAG_GROUNDED;
    if (plane.exploding) flags |= PLANE_FLAG_EXPLODING;
    return flags;
}

void writePlaneState(uint8_t*& dst, const BluetoothPlaneState& plane) {
    writeFloat(dst, plane.x);
    writeFloat(dst, plane.y);
    writeFloat(dst, plane.angle);
    writeFloat(dst, plane.groundSpeed);
    writeFloat(dst, plane.respawnTimer);
    writeFloat(dst, plane.explosionTimer);
    *dst++ = plane.hp;
    *dst++ = plane.score;
    *dst++ = encodePlaneFlags(plane);
    writeUint16(dst, plane.lastResolvedProjectileId);
}

BluetoothPlaneState readPlaneState(const uint8_t*& src) {
    BluetoothPlaneState plane;
    plane.x = readFloat(src);
    plane.y = readFloat(src);
    plane.angle = readFloat(src);
    plane.groundSpeed = readFloat(src);
    plane.respawnTimer = readFloat(src);
    plane.explosionTimer = readFloat(src);
    plane.hp = *src++;
    plane.score = *src++;
    const uint8_t flags = *src++;
    plane.lastResolvedProjectileId = readUint16(src);
    plane.isAlive = (flags & PLANE_FLAG_ALIVE) != 0;
    plane.grounded = (flags & PLANE_FLAG_GROUNDED) != 0;
    plane.exploding = (flags & PLANE_FLAG_EXPLODING) != 0;
    return plane;
}

void writeProjectileState(uint8_t*& dst, const BluetoothProjectileState& projectileState) {
    const uint8_t count = std::min<int>(projectileState.count, MAX_PROJECTILES);
    *dst++ = count;
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        writeUint16(dst, i < count ? projectileState.id[i] : 0);
    }
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        writeFloat(dst, i < count ? projectileState.x[i] : 0.f);
    }
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        writeFloat(dst, i < count ? projectileState.y[i] : 0.f);
    }
}

BluetoothProjectileState readProjectileState(const uint8_t*& src) {
    BluetoothProjectileState projectileState;
    projectileState.count = std::min<int>(*src++, MAX_PROJECTILES);
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        projectileState.id[i] = readUint16(src);
    }
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        projectileState.x[i] = readFloat(src);
    }
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        projectileState.y[i] = readFloat(src);
    }
    return projectileState;
}
}

BluetoothBridge::BluetoothBridge(JNIEnv* env, jobject activity)
    : env_(env) {

    jclass btClass = loadBluetoothManagerClass(env, activity);
    if (!btClass) {
        aout << "BluetoothBridge: BluetoothManager class not found" << std::endl;
        btManager_ = nullptr;
        return;
    }

    jmethodID ctor = env->GetMethodID(btClass, "<init>",
        "(Lcom/google/androidgamesdk/GameActivity;)V");
    if (!ctor || clearJniException(env, "BluetoothManager.<init>")) {
        env->DeleteLocalRef(btClass);
        aout << "BluetoothBridge: BluetoothManager constructor lookup failed" << std::endl;
        btManager_ = nullptr;
        return;
    }

    jobject localRef = env->NewObject(btClass, ctor, activity);
    if (!localRef || clearJniException(env, "NewObject(BluetoothManager)")) {
        env->DeleteLocalRef(btClass);
        aout << "BluetoothBridge: BluetoothManager instantiation failed" << std::endl;
        btManager_ = nullptr;
        return;
    }

    btManager_ = env->NewGlobalRef(localRef);
    env->DeleteLocalRef(localRef);

    sendMethod_        = env->GetMethodID(btClass, "sendPacket", "([B)V");
    sendAndDisconnectMethod_ = env->GetMethodID(btClass, "sendPacketAndDisconnect", "([B)V");
    isConnectedMethod_ = env->GetMethodID(btClass, "isConnected", "()Z");
    isHostMethod_      = env->GetMethodID(btClass, "isHostRole", "()Z");
    showRoleToastMethod_ = env->GetMethodID(btClass, "showCurrentRoleToast", "()V");
    dismissDialogsMethod_ = env->GetMethodID(btClass, "dismissConnectionDialogs", "()V");
    startAdvMethod_    = env->GetMethodID(btClass, "startAdvertising", "()V");
    startScanMethod_   = env->GetMethodID(btClass, "startScanning", "()V");
    disconnectMethod_  = env->GetMethodID(btClass, "disconnect", "()V");
    env->DeleteLocalRef(btClass);

    if (clearJniException(env, "BluetoothManager method lookup")) {
        if (btManager_) {
            env->DeleteGlobalRef(btManager_);
            btManager_ = nullptr;
        }
        return;
    }

    initialized_ = btManager_ && sendMethod_ && sendAndDisconnectMethod_ && isConnectedMethod_ &&
                   isHostMethod_ && showRoleToastMethod_ && dismissDialogsMethod_ &&
                   startAdvMethod_ && startScanMethod_ &&
                   disconnectMethod_;
    if (!initialized_) {
        aout << "BluetoothBridge: BluetoothManager method lookup returned null" << std::endl;
        if (btManager_) {
            env->DeleteGlobalRef(btManager_);
            btManager_ = nullptr;
        }
        return;
    }

    instance_ = this;
    aout << "BluetoothBridge: initialized" << std::endl;
}

BluetoothBridge::~BluetoothBridge() {
    if (btManager_) {
        env_->DeleteGlobalRef(btManager_);
    }
    instance_ = nullptr;
}

void BluetoothBridge::sendMatchState(const BluetoothMatchState& state) {
    if (!btManager_ || !sendMethod_) return;

    uint8_t packet[MATCH_PACKET_SIZE] = {};
    uint8_t* dst = packet;
    *dst++ = MATCH_STATE_PACKET_TYPE;
    *dst++ = static_cast<uint8_t>(state.authority);
    writeUint16(dst, state.stateSequence);
    writeFloat(dst, state.simulationTimestamp);
    writePlaneState(dst, state.bluePlane);
    writePlaneState(dst, state.redPlane);
    writeProjectileState(dst, state.blueProjectiles);
    writeProjectileState(dst, state.redProjectiles);

    jbyteArray arr = env_->NewByteArray(MATCH_PACKET_SIZE);
    env_->SetByteArrayRegion(arr, 0, MATCH_PACKET_SIZE, reinterpret_cast<const jbyte*>(packet));
    env_->CallVoidMethod(btManager_, sendMethod_, arr);
    env_->DeleteLocalRef(arr);
}

void BluetoothBridge::sendInputState(const BluetoothInputState& input) {
    if (!btManager_ || !sendMethod_) return;

    uint8_t packet[INPUT_PACKET_SIZE] = {};
    packet[0] = INPUT_PACKET_TYPE;
    if (input.upButtonHeld) packet[1] |= INPUT_FLAG_UP;
    if (input.downButtonHeld) packet[1] |= INPUT_FLAG_DOWN;
    if (input.fireTapped) packet[1] |= INPUT_FLAG_FIRE;
    uint8_t* dst = packet + 2;
    writeUint16(dst, input.sequence);

    jbyteArray arr = env_->NewByteArray(INPUT_PACKET_SIZE);
    env_->SetByteArrayRegion(arr, 0, INPUT_PACKET_SIZE, reinterpret_cast<const jbyte*>(packet));
    env_->CallVoidMethod(btManager_, sendMethod_, arr);
    env_->DeleteLocalRef(arr);
}

bool BluetoothBridge::pollReceivedMatchState(BluetoothMatchState& outState) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingMatchStates_.empty()) return false;
    outState = pendingMatchStates_.front();
    pendingMatchStates_.pop_front();
    return true;
}

bool BluetoothBridge::pollReceivedInputState(BluetoothInputState& outInput) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingInputStates_.empty()) return false;
    outInput = pendingInputStates_.front();
    pendingInputStates_.pop_front();
    return true;
}

bool BluetoothBridge::pollControlSignal(ControlSignal& outSignal) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasNewControlSignal_) return false;
    outSignal = latestControlSignal_;
    hasNewControlSignal_ = false;
    return true;
}

void BluetoothBridge::sendControlSignal(ControlSignal signal) {
    if (!btManager_ || !sendMethod_ || signal == ControlSignal::None) return;
    uint8_t packet[1] = { static_cast<uint8_t>(signal) };

    jbyteArray arr = env_->NewByteArray(1);
    env_->SetByteArrayRegion(arr, 0, 1, reinterpret_cast<const jbyte*>(packet));
    env_->CallVoidMethod(btManager_, sendMethod_, arr);
    env_->DeleteLocalRef(arr);
}

void BluetoothBridge::sendControlSignalAndDisconnect(ControlSignal signal) {
    if (signal == ControlSignal::None) return;
    if (!btManager_ || !sendAndDisconnectMethod_) {
        sendControlSignal(signal);
        disconnect();
        return;
    }

    uint8_t packet[1] = { static_cast<uint8_t>(signal) };
    jbyteArray arr = env_->NewByteArray(1);
    env_->SetByteArrayRegion(arr, 0, 1, reinterpret_cast<const jbyte*>(packet));
    env_->CallVoidMethod(btManager_, sendAndDisconnectMethod_, arr);
    env_->DeleteLocalRef(arr);
}

bool BluetoothBridge::isConnected() const {
    if (!initialized_ || !btManager_ || !isConnectedMethod_) return false;
    return env_->CallBooleanMethod(btManager_, isConnectedMethod_) == JNI_TRUE;
}

bool BluetoothBridge::isReady() const {
    return initialized_;
}

bool BluetoothBridge::isHostRole() const {
    if (!initialized_ || !btManager_ || !isHostMethod_) return false;
    return env_->CallBooleanMethod(btManager_, isHostMethod_) == JNI_TRUE;
}

void BluetoothBridge::showCurrentRoleToast() {
    if (initialized_ && btManager_ && showRoleToastMethod_) {
        env_->CallVoidMethod(btManager_, showRoleToastMethod_);
    }
}

void BluetoothBridge::dismissConnectionDialogs() {
    if (initialized_ && btManager_ && dismissDialogsMethod_) {
        env_->CallVoidMethod(btManager_, dismissDialogsMethod_);
    }
}

void BluetoothBridge::startAdvertising() {
    if (initialized_ && btManager_ && startAdvMethod_) {
        env_->CallVoidMethod(btManager_, startAdvMethod_);
    }
}

void BluetoothBridge::startScanning() {
    if (initialized_ && btManager_ && startScanMethod_) {
        env_->CallVoidMethod(btManager_, startScanMethod_);
    }
}

void BluetoothBridge::disconnect() {
    clearPendingGameState();
    if (initialized_ && btManager_ && disconnectMethod_) {
        env_->CallVoidMethod(btManager_, disconnectMethod_);
    }
}

void BluetoothBridge::clearPendingGameState() {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingMatchStates_.clear();
    pendingInputStates_.clear();
    latestControlSignal_ = ControlSignal::None;
    hasNewControlSignal_ = false;
}

void BluetoothBridge::onPacketReceived(const uint8_t* data, int len) {
    if (len < 1) return;
    const uint8_t packetType = data[0];

    if (packetType == static_cast<uint8_t>(ControlSignal::Pause)
        || packetType == static_cast<uint8_t>(ControlSignal::Resume)
        || packetType == static_cast<uint8_t>(ControlSignal::EndMatch)) {
        std::lock_guard<std::mutex> lock(mutex_);
        latestControlSignal_ = static_cast<ControlSignal>(packetType);
        hasNewControlSignal_ = true;
        return;
    }

    if (packetType == INPUT_PACKET_TYPE) {
        if (len < INPUT_PACKET_SIZE) return;
        const uint8_t flags = data[1];
        const uint8_t* src = data + 2;
        BluetoothInputState inputState;
        inputState.upButtonHeld = (flags & INPUT_FLAG_UP) != 0;
        inputState.downButtonHeld = (flags & INPUT_FLAG_DOWN) != 0;
        inputState.fireTapped = (flags & INPUT_FLAG_FIRE) != 0;
        inputState.sequence = readUint16(src);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pendingInputStates_.empty()
            && pendingInputStates_.back().sequence == inputState.sequence) {
            pendingInputStates_.back().upButtonHeld = inputState.upButtonHeld;
            pendingInputStates_.back().downButtonHeld = inputState.downButtonHeld;
            pendingInputStates_.back().fireTapped = pendingInputStates_.back().fireTapped || inputState.fireTapped;
        } else {
            pendingInputStates_.push_back(inputState);
        }
        return;
    }

    if (packetType != MATCH_STATE_PACKET_TYPE || len < MATCH_PACKET_SIZE) return;

    const uint8_t* src = data + 1;
    BluetoothMatchState state;
    state.authority = static_cast<BluetoothAuthority>(*src++);
    state.stateSequence = readUint16(src);
    state.simulationTimestamp = readFloat(src);
    state.bluePlane = readPlaneState(src);
    state.redPlane = readPlaneState(src);
    state.blueProjectiles = readProjectileState(src);
    state.redProjectiles = readProjectileState(src);

    std::lock_guard<std::mutex> lock(mutex_);
    pendingMatchStates_.push_back(state);
    while (pendingMatchStates_.size() > 96) {
        pendingMatchStates_.pop_front();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_abocha_byplanes_BluetoothManager_onPacketReceived(
        JNIEnv* env, jobject /*obj*/, jbyteArray data, jint len) {
    if (!BluetoothBridge::instance_) return;
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    BluetoothBridge::instance_->onPacketReceived(
        reinterpret_cast<const uint8_t*>(bytes), static_cast<int>(len));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}
