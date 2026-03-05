#ifndef BYPLANES_INPUTMANAGER_H
#define BYPLANES_INPUTMANAGER_H

#include <game-activity/native_app_glue/android_native_app_glue.h>

struct TouchState {
    bool upButtonHeld = false;
    bool downButtonHeld = false;
    bool fireTapped = false;      // fire button zone only (gameplay)
    bool screenTapped = false;    // any touch (menu/splash/gameover)
    float tapX = 0;
    float tapY = 0;
};

class InputManager {
public:
    void beginFrame();
    void processMotionEvents(android_input_buffer *buf, float screenW, float screenH);
    const TouchState &getState() const { return state_; }

private:
    TouchState state_;
    int upPointerId_ = -1;
    int downPointerId_ = -1;
    float screenW_ = 0;
    float screenH_ = 0;
};

#endif //BYPLANES_INPUTMANAGER_H
