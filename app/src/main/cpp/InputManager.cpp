#include "InputManager.h"
#include <game-activity/GameActivity.h>

void InputManager::beginFrame() {
    state_.fireTapped = false;
    state_.screenTapped = false;
}

void InputManager::processMotionEvents(android_input_buffer *buf, float screenW, float screenH) {
    if (!buf) return;
    // Guard: don't process events until the renderer has valid dimensions
    if (screenW <= 0 || screenH <= 0) {
        android_app_clear_motion_events(buf);
        return;
    }
    screenW_ = screenW;
    screenH_ = screenH;

    // Button zones: percentage-based so they work on any screen size/DPI.
    // UP   = left 15% width, bottom 40% height
    // DOWN = right 15% width, bottom 25% height
    // FIRE = right 15% width, between 50% and 75% height (above DOWN)
    const float upMaxX   = screenW_ * 0.15f;
    const float downMinX = screenW_ * 0.85f;
    const float btnMinY  = screenH_ * 0.60f;    // UP zone starts here
    const float fireMinY = screenH_ * 0.50f;    // FIRE zone top
    const float fireMaxY = screenH_ * 0.75f;    // FIRE zone bottom / DOWN zone top

    for (auto i = 0; i < buf->motionEventsCount; i++) {
        auto &motionEvent = buf->motionEvents[i];
        auto action = motionEvent.action;
        auto maskedAction = action & AMOTION_EVENT_ACTION_MASK;
        auto pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        switch (maskedAction) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                auto &pointer = motionEvent.pointers[pointerIndex];
                auto x = GameActivityPointerAxes_getX(&pointer);
                auto y = GameActivityPointerAxes_getY(&pointer);
                int id = pointer.id;

                // Record any touch for menu/splash
                state_.screenTapped = true;
                state_.tapX = x;
                state_.tapY = y;

                // UP zone: left side, bottom area
                if (x < upMaxX && y > btnMinY) {
                    upPointerId_ = id;
                    state_.upButtonHeld = true;
                }
                // DOWN zone: right side, very bottom
                else if (x > downMinX && y > fireMaxY) {
                    downPointerId_ = id;
                    state_.downButtonHeld = true;
                }
                // FIRE zone: right side, above DOWN
                else if (x > downMinX && y > fireMinY && y <= fireMaxY) {
                    state_.fireTapped = true;
                }
                break;
            }

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
            case AMOTION_EVENT_ACTION_CANCEL: {
                auto &pointer = motionEvent.pointers[pointerIndex];
                int id = pointer.id;

                if (id == upPointerId_) {
                    upPointerId_ = -1;
                    state_.upButtonHeld = false;
                }
                if (id == downPointerId_) {
                    downPointerId_ = -1;
                    state_.downButtonHeld = false;
                }
                break;
            }

            case AMOTION_EVENT_ACTION_MOVE: {
                // Re-check all active pointers for zone transitions
                for (auto index = 0; index < motionEvent.pointerCount; index++) {
                    auto &pointer = motionEvent.pointers[index];
                    auto x = GameActivityPointerAxes_getX(&pointer);
                    auto y = GameActivityPointerAxes_getY(&pointer);
                    int id = pointer.id;

                    bool inUpZone   = (x < upMaxX   && y > btnMinY);
                    bool inDownZone = (x > downMinX  && y > fireMaxY);

                    if (id == upPointerId_ && !inUpZone) {
                        upPointerId_ = -1;
                        state_.upButtonHeld = false;
                    }
                    if (id == downPointerId_ && !inDownZone) {
                        downPointerId_ = -1;
                        state_.downButtonHeld = false;
                    }
                }
                break;
            }

            default:
                break;
        }
    }
    android_app_clear_motion_events(buf);
}
