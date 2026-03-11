#pragma once

#include "BluetoothBridge.h"
#include "GameSession.h"
#include "InputManager.h"

class BluetoothVirtualServer {
public:
    BluetoothVirtualServer(GameSession &session, bool localControlsBlue);

    void reset();
    bool update(float dt, const TouchState &localInput, BluetoothBridge &bridge);
    void forceSendLocalState(BluetoothBridge &bridge);

private:
    void consumeRemoteStates(BluetoothBridge &bridge);
    void maybeSendLocalState(BluetoothBridge &bridge);

    GameSession &session_;
    bool localControlsBlue_ = true;
    float accumulator_ = 0.f;
    float sendAccumulator_ = 0.f;
    float smoothedRemoteStateInterval_ = 1.f / 60.f;
    uint16_t localStateSequence_ = 0;
    uint16_t lastRemoteStateSequence_ = 0;
    bool hasRemoteState_ = false;
};
