#ifndef BYPLANES_GAME_H
#define BYPLANES_GAME_H

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

private:
    void initSession();
    void loadSplashSprites();
    void loadMenuSprites();
    void loadLobbySprites();
    void initBluetooth();
    void drawText(const Shader &shader, const char *text,
                  float startX, float y, float charW, float charH) const;

    android_app *app_;
    Renderer *renderer_ = nullptr;
    InputManager inputManager_;
    GameSession session_;
    GameState state_ = GameState::SplashScreen;
    GameMode pendingMode_ = GameMode::VsAI;
    bool sessionInitialized_ = false;

    int winner_ = 1;
    Sprite gameoverP1Sprite_;
    Sprite gameoverP2Sprite_;
    bool gameoverSpritesLoaded_ = false;

    Sprite btnUpSprite_;
    Sprite btnDownSprite_;
    Sprite btnFireSprite_;
    bool uiSpritesLoaded_ = false;

    // Splash screen
    Sprite splashLogo_;
    bool splashLoaded_ = false;
    float splashTimer_ = 0.f;

    // Main menu
    Background menuBackground_;
    Sprite menuBtn1_;           // button.png for VS COMPUTER
    Sprite menuBtn2_;           // button.png for VS BLUETOOTH
    std::shared_ptr<TextureAsset> fontTex_;  // font.png
    bool menuSpritesLoaded_ = false;
    float menuAnimTimer_ = 0.f;

    // Bluetooth lobby
    BluetoothBridge *btBridge_ = nullptr;
    JavaVM *javaVM_ = nullptr;
    JNIEnv *jniEnv_ = nullptr;
    Sprite lobbyBtn1_;          // HOST GAME
    Sprite lobbyBtn2_;          // JOIN GAME
    bool lobbySpritesLoaded_ = false;
    bool btHosting_ = false;
    bool btJoining_ = false;
    float btStatusTimer_ = 0.f;   // for "..." animation
};

#endif //BYPLANES_GAME_H
