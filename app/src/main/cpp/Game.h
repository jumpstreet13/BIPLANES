#ifndef BYPLANES_GAME_H
#define BYPLANES_GAME_H

#include <cstdint>
#include <deque>
#include <memory>

#include "Renderer.h"
#include "InputManager.h"
#include "GameSession.h"
#include "GameState.h"
#include "Sprite.h"
#include "Background.h"
#include "BluetoothBridge.h"

struct android_app;

class Game {
public:
    Game(android_app *pApp);
    ~Game();

    void handleInput();
    void update(float dt);
    void render();
    void onAppBackgrounded();

private:
    void initSession();
    void loadSplashSprites();
    void loadMenuSprites();
    void loadLobbySprites();
    void initBluetooth();
    void initButtonSprite(Sprite &sprite,
                          const std::shared_ptr<TextureAsset> &texture,
                          float x, float y, float width, float height) const;
    bool pointInButton(const Sprite &button, float x, float y) const;
    void drawCenteredText(const Shader &shader, const char *text,
                          float centerX, float y, float charW, float charH,
                          float tintR = 1.f, float tintG = 1.f,
                          float tintB = 1.f, float tintA = 1.f) const;
    void drawText(const Shader &shader, const char *text,
                  float startX, float y, float charW, float charH,
                  float tintR = 1.f, float tintG = 1.f,
                  float tintB = 1.f, float tintA = 1.f) const;
    void drawHintText(const Shader &shader, const char *text,
                      float y, float charW, float charH) const;
    bool isGameplayPaused() const;
    void setPauseFromLocalPlayer(bool paused);
    void handlePauseUiTap(float wx, float wy);
    void processBluetoothControlSignals();
    void leaveMatchToMainMenu(bool notifyRemote);
    void enqueueRemoteBluetoothInput(const BluetoothInputState &input);
    BluetoothInputState consumeRemoteBluetoothInputForStep();
    void startGameplayCountdown();
    void stopGameplayCountdown();
    bool isGameplayCountdownActive() const;

    android_app *app_;
    std::unique_ptr<Renderer> renderer_;
    InputManager inputManager_;
    GameSession session_;
    GameState state_ = GameState::SplashScreen;
    GameMode pendingMode_ = GameMode::VsAI;
    AiDifficulty aiDifficulty_ = AiDifficulty::Medium;
    bool sessionInitialized_ = false;

    int winner_ = 1;
    Sprite gameOverBackdropSprite_;
    Sprite gameOverBannerFrameSprite_;
    Sprite gameOverBannerFillSprite_;
    Sprite gameOverOkFrameSprite_;
    Sprite gameOverOkFillSprite_;
    bool gameOverUiLoaded_ = false;

    Sprite btnUpSprite_;
    Sprite btnDownSprite_;
    Sprite btnFireSprite_;
    Sprite pauseBtnSprite_;
    Sprite pauseDialogBgSprite_;
    Sprite pauseContinueBtnSprite_;
    Sprite pauseEndBtnSprite_;
    Sprite gameplayCountdownBackdropSprite_;
    bool uiSpritesLoaded_ = false;
    bool localPauseActive_ = false;
    bool remotePauseActive_ = false;
    bool opponentLeftDialogActive_ = false;
    bool gameplayCountdownActive_ = false;
    float gameplayCountdownRemaining_ = 0.f;

    // Splash screen
    Sprite splashLogo_;
    bool splashLoaded_ = false;
    float splashTimer_ = 0.f;

    // Main menu
    Background menuBackground_;
    Sprite menuBtn1_;           // button.png for VS COMPUTER
    Sprite menuBtn2_;           // button.png for VS BLUETOOTH
    Sprite aiEasyBtn_;
    Sprite aiMediumBtn_;
    Sprite aiHardBtn_;
    Sprite hintBg_;
    std::shared_ptr<TextureAsset> fontTex_;  // font.png
    bool menuSpritesLoaded_ = false;
    float menuAnimTimer_ = 0.f;

    // Bluetooth lobby
    BluetoothBridge *btBridge_ = nullptr;
    std::deque<BluetoothInputState> remoteBluetoothInputBuffer_;
    BluetoothInputState lastAppliedRemoteBluetoothInput_;
    uint16_t consumedRemoteBluetoothInputSequence_ = 0;
    uint16_t localBluetoothInputSequence_ = 0;
    float bluetoothSimulationAccumulator_ = 0.f;
    bool pendingBluetoothLocalFireTap_ = false;
    JavaVM *javaVM_ = nullptr;
    JNIEnv *jniEnv_ = nullptr;
    Sprite lobbyBtn1_;          // HOST GAME
    Sprite lobbyBtn2_;          // JOIN GAME
    bool lobbySpritesLoaded_ = false;
};

#endif //BYPLANES_GAME_H
