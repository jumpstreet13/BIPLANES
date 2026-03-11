#include "BluetoothVirtualServer.h"

#include <algorithm>

namespace {
constexpr float BLUETOOTH_SERVER_STEP = 1.f / 60.f;
constexpr int BLUETOOTH_SERVER_MAX_STEPS_PER_FRAME = 4;
constexpr float BLUETOOTH_SERVER_MAX_SEND_INTERVAL = 1.f / 18.f;

bool isSequenceNewer(uint16_t candidate, uint16_t reference) {
    return static_cast<int16_t>(candidate - reference) > 0;
}

float lerpFloat(float from, float to, float alpha) {
    return from + (to - from) * alpha;
}
}

BluetoothVirtualServer::BluetoothVirtualServer(GameSession &session, bool localControlsBlue)
    : session_(session),
      localControlsBlue_(localControlsBlue) {
}

void BluetoothVirtualServer::reset() {
    accumulator_ = 0.f;
    sendAccumulator_ = BLUETOOTH_SERVER_MAX_SEND_INTERVAL;
    smoothedRemoteStateInterval_ = BLUETOOTH_SERVER_STEP;
    localStateSequence_ = 0;
    lastRemoteStateSequence_ = 0;
    hasRemoteState_ = false;
}

void BluetoothVirtualServer::consumeRemoteStates(BluetoothBridge &bridge) {
    BluetoothMatchState state;
    while (bridge.pollReceivedMatchState(state)) {
        const BluetoothAuthority expectedAuthority = localControlsBlue_
                                                     ? BluetoothAuthority::Red
                                                     : BluetoothAuthority::Blue;
        if (state.authority != expectedAuthority) {
            continue;
        }

        if (hasRemoteState_) {
            if (state.stateSequence == lastRemoteStateSequence_) {
                continue;
            }
            if (!isSequenceNewer(state.stateSequence, lastRemoteStateSequence_)) {
                continue;
            }
        }

        if (hasRemoteState_) {
            const uint16_t remoteDelta = static_cast<uint16_t>(state.stateSequence - lastRemoteStateSequence_);
            const float remoteInterval = std::max(
                BLUETOOTH_SERVER_STEP,
                BLUETOOTH_SERVER_STEP * static_cast<float>(std::max<int>(remoteDelta, 1))
            );
            smoothedRemoteStateInterval_ = lerpFloat(
                smoothedRemoteStateInterval_,
                remoteInterval,
                0.18f
            );
        }

        lastRemoteStateSequence_ = state.stateSequence;
        hasRemoteState_ = true;
        if (localControlsBlue_) {
            session_.applyBluetoothRemoteOwnedState(state);
        } else {
            session_.applyBluetoothMatchState(state);
        }
    }
}

void BluetoothVirtualServer::maybeSendLocalState(BluetoothBridge &bridge) {
    if (!bridge.isConnected() || localStateSequence_ == 0) {
        return;
    }

    const float targetSendInterval = std::clamp(
        std::max(BLUETOOTH_SERVER_STEP, smoothedRemoteStateInterval_),
        BLUETOOTH_SERVER_STEP,
        BLUETOOTH_SERVER_MAX_SEND_INTERVAL
    );
    if (sendAccumulator_ < targetSendInterval) {
        return;
    }

    bridge.sendMatchState(session_.buildBluetoothMatchState(localStateSequence_));
    sendAccumulator_ = 0.f;
}

void BluetoothVirtualServer::forceSendLocalState(BluetoothBridge &bridge) {
    if (!bridge.isConnected()) {
        return;
    }

    if (localStateSequence_ == 0) {
        localStateSequence_ = 1;
    }

    bridge.sendMatchState(session_.buildBluetoothMatchState(localStateSequence_));
    sendAccumulator_ = 0.f;
}

bool BluetoothVirtualServer::update(float dt, const TouchState &localInput, BluetoothBridge &bridge) {
    consumeRemoteStates(bridge);

    accumulator_ = std::min(
        accumulator_ + dt,
        BLUETOOTH_SERVER_STEP * static_cast<float>(BLUETOOTH_SERVER_MAX_STEPS_PER_FRAME)
    );
    sendAccumulator_ += dt;

    bool gameOver = false;
    while (accumulator_ >= BLUETOOTH_SERVER_STEP) {
        if (localControlsBlue_) {
            gameOver = session_.updateBluetoothHost(BLUETOOTH_SERVER_STEP, localInput) || gameOver;
        } else {
            session_.updateBluetoothClient(BLUETOOTH_SERVER_STEP, localInput);
            gameOver = session_.getWinner() != 0 || gameOver;
        }

        localStateSequence_ = static_cast<uint16_t>(localStateSequence_ + 1);
        accumulator_ -= BLUETOOTH_SERVER_STEP;
        maybeSendLocalState(bridge);
        consumeRemoteStates(bridge);
        if (gameOver) {
            break;
        }
    }

    session_.setBluetoothRemoteRenderAlpha(accumulator_ / BLUETOOTH_SERVER_STEP);
    session_.updateBluetoothRemoteRender(dt);
    maybeSendLocalState(bridge);
    return gameOver || session_.getWinner() != 0;
}
