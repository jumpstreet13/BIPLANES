#include "Game.h"
#include "GameConstants.h"
#include "Utility.h"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <game-activity/GameActivity.h>
#include "AndroidOut.h"
#include <cmath>
#include <cstring>

// font.png layout: 19 columns x 5 rows, 8x8 cells, ASCII 32-126
static constexpr int FONT_COLS = 19;
static constexpr int FONT_ROWS = 5;
static constexpr float FONT_TEX_W = 152.f;
static constexpr float FONT_TEX_H = 40.f;
static constexpr float FONT_CELL = 8.f;

static constexpr float SPLASH_DURATION = 2.5f;

Game::Game(android_app *pApp) : app_(pApp) {
    renderer_ = std::make_unique<Renderer>(pApp);
}

Game::~Game() {
    delete btBridge_;
    if (jniEnv_ && javaVM_) {
        javaVM_->DetachCurrentThread();
    }
}

void Game::initButtonSprite(Sprite &sprite,
                            const std::shared_ptr<TextureAsset> &texture,
                            float x, float y, float width, float height) const {
    sprite.init(texture, width, height);
    sprite.x = x;
    sprite.y = y;
}

bool Game::pointInButton(const Sprite &button, float x, float y) const {
    return x >= button.x - button.scaleX && x <= button.x + button.scaleX
           && y >= button.y - button.scaleY && y <= button.y + button.scaleY;
}

void Game::loadSplashSprites() {
    if (splashLoaded_) return;
    auto *am = app_->activity->assetManager;

    auto logoTex = TextureAsset::loadAsset(am, "screen_logo.png");
    splashLogo_.init(logoTex, 1.8f, 1.8f);
    splashLogo_.x = 0.f;
    splashLogo_.y = 0.f;
    splashLoaded_ = true;
}

void Game::loadMenuSprites() {
    if (menuSpritesLoaded_) return;
    auto *am = app_->activity->assetManager;
    float aspect = renderer_->getAspect();

    // Menu background — same as game map
    menuBackground_.init(am, aspect);

    // Button sprites (button.png: 255x12, elongated bar)
    auto btnTex = TextureAsset::loadAsset(am, "button.png");
    initButtonSprite(menuBtn1_, btnTex, 0.f, 0.55f, 1.8f, 0.35f);
    initButtonSprite(menuBtn2_, btnTex, 0.f, -0.55f, 1.8f, 0.35f);
    initButtonSprite(aiEasyBtn_, btnTex, 0.f, 0.72f, 1.8f, 0.28f);
    initButtonSprite(aiMediumBtn_, btnTex, 0.f, 0.00f, 1.8f, 0.28f);
    initButtonSprite(aiHardBtn_, btnTex, 0.f, -0.72f, 1.8f, 0.28f);
    hintBg_.init(btnTex, 1.0f, 0.2f);
    hintBg_.x = 0.f;
    hintBg_.y = -1.35f;
    hintBg_.tintR = 0.f;
    hintBg_.tintG = 0.f;
    hintBg_.tintB = 0.f;
    hintBg_.tintA = 0.45f;

    // Font texture for button text
    fontTex_ = TextureAsset::loadAsset(am, "font.png");

    menuSpritesLoaded_ = true;
}

void Game::drawText(const Shader &shader, const char *text,
                    float startX, float y, float charW, float charH,
                    float tintR, float tintG, float tintB, float tintA) const {
    if (!fontTex_) return;
    int len = (int)strlen(text);
    float cx = startX;
    for (int i = 0; i < len; i++) {
        int ch = (unsigned char)text[i];
        if (ch < 32 || ch > 126) {
            cx += charW * 2.f;
            continue;
        }
        int pos = ch - 32;
        int col = pos % FONT_COLS;
        int row = pos / FONT_COLS;

        Sprite glyph;
        glyph.init(fontTex_, charW, charH);
        glyph.x = cx;
        glyph.y = y;
        glyph.uvLeft   = (col * FONT_CELL) / FONT_TEX_W;
        glyph.uvRight  = ((col + 1) * FONT_CELL) / FONT_TEX_W;
        glyph.uvTop    = (row * FONT_CELL) / FONT_TEX_H;
        glyph.uvBottom = ((row + 1) * FONT_CELL) / FONT_TEX_H;
        glyph.tintR = tintR;
        glyph.tintG = tintG;
        glyph.tintB = tintB;
        glyph.tintA = tintA;
        glyph.draw(shader);

        cx += charW * 2.f;
    }
}

void Game::drawHintText(const Shader &shader, const char *text,
                        float y, float charW, float charH) const {
    int len = (int)strlen(text);
    float textW = len * charW * 2.f;
    float startX = -textW / 2.f + charW;

    Sprite bg = hintBg_;
    bg.y = y;
    bg.scaleX = textW * 0.5f + charW * 0.55f;
    bg.scaleY = charH + charH * 0.55f;
    bg.draw(shader);

    float shadowOffsetX = charW * 0.18f;
    float shadowOffsetY = charH * 0.18f;
    drawText(shader, text, startX + shadowOffsetX, y - shadowOffsetY, charW, charH,
             0.f, 0.f, 0.f, 0.85f);
    drawText(shader, text, startX, y, charW, charH);
}

void Game::onAppBackgrounded() {
    if (state_ == GameState::Playing) {
        setPauseFromLocalPlayer(true);
    }
}

bool Game::isGameplayPaused() const {
    return localPauseActive_ || remotePauseActive_;
}

void Game::setPauseFromLocalPlayer(bool paused) {
    if (localPauseActive_ == paused) return;
    localPauseActive_ = paused;

    if (pendingMode_ == GameMode::VsBluetooth
        && btBridge_
        && btBridge_->isConnected()) {
        btBridge_->sendControlSignal(paused
                                     ? BluetoothBridge::ControlSignal::Pause
                                     : BluetoothBridge::ControlSignal::Resume);
    }
}

void Game::leaveMatchToMainMenu(bool notifyRemote) {
    if (notifyRemote
        && pendingMode_ == GameMode::VsBluetooth
        && btBridge_
        && btBridge_->isConnected()) {
        btBridge_->sendControlSignal(BluetoothBridge::ControlSignal::EndMatch);
    }

    if (pendingMode_ == GameMode::VsBluetooth && btBridge_) {
        btBridge_->disconnect();
    }

    localPauseActive_ = false;
    remotePauseActive_ = false;
    state_ = GameState::MainMenu;
    sessionInitialized_ = false;
}

void Game::processBluetoothControlSignals() {
    if (pendingMode_ != GameMode::VsBluetooth || !btBridge_) return;

    BluetoothBridge::ControlSignal signal = BluetoothBridge::ControlSignal::None;
    while (btBridge_->pollControlSignal(signal)) {
        switch (signal) {
            case BluetoothBridge::ControlSignal::Pause:
                remotePauseActive_ = true;
                break;
            case BluetoothBridge::ControlSignal::Resume:
                remotePauseActive_ = false;
                break;
            case BluetoothBridge::ControlSignal::EndMatch:
                aout << "BluetoothLobby: Opponent ended the match" << std::endl;
                leaveMatchToMainMenu(false);
                return;
            case BluetoothBridge::ControlSignal::None:
                break;
        }
    }
}

void Game::handlePauseUiTap(float wx, float wy) {
    if (!isGameplayPaused()) {
        if (pointInButton(pauseBtnSprite_, wx, wy)) {
            setPauseFromLocalPlayer(true);
        }
        return;
    }

    if (remotePauseActive_ && !localPauseActive_) {
        return;
    }

    if (pointInButton(pauseContinueBtnSprite_, wx, wy)) {
        setPauseFromLocalPlayer(false);
    } else if (pointInButton(pauseEndBtnSprite_, wx, wy)) {
        leaveMatchToMainMenu(true);
    }
}

void Game::handleInput() {
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) return;

    inputManager_.beginFrame();
    inputManager_.processMotionEvents(inputBuffer,
                                      (float) renderer_->getOffsetX(),
                                      (float) renderer_->getOffsetY(),
                                      (float) renderer_->getWidth(),
                                      (float) renderer_->getHeight());

    android_app_clear_key_events(inputBuffer);

    const auto &touch = inputManager_.getState();

    switch (state_) {
        case GameState::SplashScreen: {
            // Tap to skip splash
            if (touch.screenTapped || touch.upButtonHeld || touch.downButtonHeld) {
                state_ = GameState::MainMenu;
            }
            break;
        }
        case GameState::MainMenu: {
            if (touch.screenTapped || touch.upButtonHeld || touch.downButtonHeld) {
                float screenW = (float) renderer_->getWidth();
                float screenH = (float) renderer_->getHeight();
                float aspect = renderer_->getAspect();

                // Use raw touch coords
                float rawX = touch.screenTapped ? touch.tapX : 0.f;
                float rawY = touch.screenTapped ? touch.tapY : 0.f;

                // For button presses, use center of the respective zone
                if (!touch.screenTapped && touch.upButtonHeld) {
                    rawX = screenW * 0.125f;
                    rawY = screenH * 0.8f;
                }
                if (!touch.screenTapped && touch.downButtonHeld) {
                    rawX = screenW * 0.875f;
                    rawY = screenH * 0.8f;
                }

                float wx, wy;
                Utility::screenToWorld(rawX, rawY, screenW, screenH, 2.0f, aspect, wx, wy);

                if (pointInButton(menuBtn1_, wx, wy)) {
                    pendingMode_ = GameMode::VsAI;
                    state_ = GameState::AiDifficultyMenu;
                    sessionInitialized_ = false;
                }
                else if (pointInButton(menuBtn2_, wx, wy)) {
                    pendingMode_ = GameMode::VsBluetooth;
                    state_ = GameState::BluetoothLobby;
                }
            }
            break;
        }
        case GameState::AiDifficultyMenu: {
            if (touch.screenTapped) {
                float screenW = (float) renderer_->getWidth();
                float screenH = (float) renderer_->getHeight();
                float aspect = renderer_->getAspect();

                float wx, wy;
                Utility::screenToWorld(touch.tapX, touch.tapY, screenW, screenH, 2.0f, aspect, wx, wy);

                if (pointInButton(aiEasyBtn_, wx, wy)) {
                    aiDifficulty_ = AiDifficulty::Easy;
                    state_ = GameState::Playing;
                    sessionInitialized_ = false;
                } else if (pointInButton(aiMediumBtn_, wx, wy)) {
                    aiDifficulty_ = AiDifficulty::Medium;
                    state_ = GameState::Playing;
                    sessionInitialized_ = false;
                } else if (pointInButton(aiHardBtn_, wx, wy)) {
                    aiDifficulty_ = AiDifficulty::Hard;
                    state_ = GameState::Playing;
                    sessionInitialized_ = false;
                } else {
                    state_ = GameState::MainMenu;
                }
            }
            break;
        }
        case GameState::BluetoothLobby: {
            if (touch.screenTapped) {
                float screenW = (float) renderer_->getWidth();
                float screenH = (float) renderer_->getHeight();
                float aspect = renderer_->getAspect();

                float wx, wy;
                Utility::screenToWorld(touch.tapX, touch.tapY, screenW, screenH, 2.0f, aspect, wx, wy);

                if (pointInButton(lobbyBtn1_, wx, wy)) {
                    initBluetooth();
                    if (btBridge_) {
                        btBridge_->startAdvertising();
                        aout << "BluetoothLobby: Host flow opened" << std::endl;
                    }
                } else if (pointInButton(lobbyBtn2_, wx, wy)) {
                    initBluetooth();
                    if (btBridge_) {
                        btBridge_->startScanning();
                        aout << "BluetoothLobby: Join flow opened" << std::endl;
                    }
                } else {
                    if (btBridge_) {
                        btBridge_->disconnect();
                    }
                    state_ = GameState::MainMenu;
                }
            }
            break;
        }
        case GameState::Playing: {
            if (touch.screenTapped) {
                float screenW = (float) renderer_->getWidth();
                float screenH = (float) renderer_->getHeight();
                float aspect = renderer_->getAspect();
                float wx, wy;
                Utility::screenToWorld(touch.tapX, touch.tapY, screenW, screenH, 2.0f, aspect, wx, wy);
                handlePauseUiTap(wx, wy);
            }
            break;
        }
        case GameState::GameOver: {
            if (touch.screenTapped || touch.upButtonHeld || touch.downButtonHeld) {
                leaveMatchToMainMenu(false);
            }
            break;
        }
    }
}

void Game::update(float dt) {
    if (state_ == GameState::SplashScreen) {
        splashTimer_ += dt;
        if (splashTimer_ >= SPLASH_DURATION) {
            state_ = GameState::MainMenu;
        }
    }

    if (state_ == GameState::MainMenu || state_ == GameState::AiDifficultyMenu) {
        menuAnimTimer_ += dt;
        if (menuSpritesLoaded_) {
            menuBackground_.update(dt);
        }
    }

    if (state_ == GameState::BluetoothLobby) {
        if (!menuSpritesLoaded_ && renderer_->getWidth() > 0) {
            loadMenuSprites();
        }
        if (menuSpritesLoaded_) {
            menuBackground_.update(dt);
        }
        // Check if Bluetooth connected
        if (btBridge_ && btBridge_->isConnected()) {
            aout << "BluetoothLobby: Connected! Starting game." << std::endl;
            state_ = GameState::Playing;
            sessionInitialized_ = false;
        }
    }

    if (state_ == GameState::Playing) {
        if (!sessionInitialized_) {
            initSession();
        }
        processBluetoothControlSignals();
        if (state_ != GameState::Playing) {
            return;
        }
        if (!isGameplayPaused()) {
            const auto &touch = inputManager_.getState();
            bool gameOver = session_.update(dt, touch);
            if (gameOver) {
                state_ = GameState::GameOver;
                winner_ = session_.getWinner();
                localPauseActive_ = false;
                remotePauseActive_ = false;
                aout << "Game Over! Winner: Player " << winner_ << std::endl;
            }
        }
    }
}

void Game::render() {
    renderer_->beginFrame();

    // Splash Screen
    if (state_ == GameState::SplashScreen) {
        if (!splashLoaded_ && renderer_->getWidth() > 0) {
            loadSplashSprites();
        }
        if (splashLoaded_) {
            splashLogo_.draw(renderer_->getShader());
        }
    }

    // Main Menu
    if (state_ == GameState::MainMenu) {
        if (!menuSpritesLoaded_ && renderer_->getWidth() > 0) {
            loadMenuSprites();
        }
        if (menuSpritesLoaded_) {
            menuBackground_.draw(renderer_->getShader());

            // Draw buttons
            menuBtn1_.draw(renderer_->getShader());
            menuBtn2_.draw(renderer_->getShader());

            // Draw text on buttons
            float charW = 0.08f;
            float charH = 0.10f;

            // "VS COMPUTER" — 11 chars, total width = 11 * charW * 2 = 1.76
            const char *text1 = "VS COMPUTER";
            float tw1 = strlen(text1) * charW * 2.f;
            drawText(renderer_->getShader(), text1,
                     menuBtn1_.x - tw1 / 2.f + charW, menuBtn1_.y, charW, charH);

            // "VS BLUETOOTH" — 12 chars
            const char *text2 = "VS BLUETOOTH";
            float tw2 = strlen(text2) * charW * 2.f;
            drawText(renderer_->getShader(), text2,
                     menuBtn2_.x - tw2 / 2.f + charW, menuBtn2_.y, charW, charH);
        }
    }

    if (state_ == GameState::AiDifficultyMenu) {
        if (!menuSpritesLoaded_ && renderer_->getWidth() > 0) {
            loadMenuSprites();
        }
        if (menuSpritesLoaded_) {
            menuBackground_.draw(renderer_->getShader());

            aiEasyBtn_.draw(renderer_->getShader());
            aiMediumBtn_.draw(renderer_->getShader());
            aiHardBtn_.draw(renderer_->getShader());

            float titleCharW = 0.08f;
            float titleCharH = 0.10f;
            const char *title = "CHOOSE DIFFICULTY";
            float titleW = strlen(title) * titleCharW * 2.f;
            drawText(renderer_->getShader(), title,
                     -titleW / 2.f + titleCharW, 1.25f, titleCharW, titleCharH);

            float charW = 0.075f;
            float charH = 0.095f;

            const char *easy = "EASY";
            float easyW = strlen(easy) * charW * 2.f;
            drawText(renderer_->getShader(), easy,
                     aiEasyBtn_.x - easyW / 2.f + charW, aiEasyBtn_.y, charW, charH);

            const char *medium = "MEDIUM";
            float mediumW = strlen(medium) * charW * 2.f;
            drawText(renderer_->getShader(), medium,
                     aiMediumBtn_.x - mediumW / 2.f + charW, aiMediumBtn_.y, charW, charH);

            const char *hard = "HARD";
            float hardW = strlen(hard) * charW * 2.f;
            drawText(renderer_->getShader(), hard,
                     aiHardBtn_.x - hardW / 2.f + charW, aiHardBtn_.y, charW, charH);

            const char *hint = "TAP OUTSIDE TO GO BACK";
            drawHintText(renderer_->getShader(), hint, -1.35f, charW * 0.65f, charH * 0.65f);
        }
    }

    // Bluetooth Lobby
    if (state_ == GameState::BluetoothLobby) {
        if (!lobbySpritesLoaded_ && renderer_->getWidth() > 0) {
            loadLobbySprites();
        }
        if (lobbySpritesLoaded_) {
            // Reuse menu background
            if (menuSpritesLoaded_) {
                menuBackground_.draw(renderer_->getShader());
            }

            float charW = 0.08f;
            float charH = 0.10f;
            lobbyBtn1_.draw(renderer_->getShader());
            lobbyBtn2_.draw(renderer_->getShader());

            const char *text1 = "HOST GAME";
            float tw1 = strlen(text1) * charW * 2.f;
            drawText(renderer_->getShader(), text1,
                     lobbyBtn1_.x - tw1 / 2.f + charW, lobbyBtn1_.y, charW, charH);

            const char *text2 = "JOIN GAME";
            float tw2 = strlen(text2) * charW * 2.f;
            drawText(renderer_->getShader(), text2,
                     lobbyBtn2_.x - tw2 / 2.f + charW, lobbyBtn2_.y, charW, charH);

            const char *hint = "TAP OUTSIDE TO GO BACK";
            drawHintText(renderer_->getShader(), hint, -1.35f, charW * 0.65f, charH * 0.65f);
        }
    }

    // Playing or GameOver — draw session
    if (state_ == GameState::Playing || state_ == GameState::GameOver) {
        session_.draw(renderer_->getShader());
    }

    // Playing — draw UP/DOWN/FIRE buttons
    if (state_ == GameState::Playing && uiSpritesLoaded_) {
        if (!isGameplayPaused()) {
            btnUpSprite_.draw(renderer_->getShader());
            btnDownSprite_.draw(renderer_->getShader());
            btnFireSprite_.draw(renderer_->getShader());
            pauseBtnSprite_.draw(renderer_->getShader());
            drawText(renderer_->getShader(), "PAUSE",
                     pauseBtnSprite_.x - 0.20f, pauseBtnSprite_.y, 0.036f, 0.048f);
        } else {
            pauseDialogBgSprite_.draw(renderer_->getShader());

            if (remotePauseActive_ && !localPauseActive_) {
                drawText(renderer_->getShader(), "OPPONENT PAUSED",
                         -0.88f, 0.20f, 0.07f, 0.09f);
                drawText(renderer_->getShader(), "WAIT FOR RESUME",
                         -0.88f, -0.22f, 0.07f, 0.09f);
            } else {
                pauseContinueBtnSprite_.draw(renderer_->getShader());
                pauseEndBtnSprite_.draw(renderer_->getShader());

                drawText(renderer_->getShader(), "MATCH PAUSED",
                         -0.67f, 0.78f, 0.07f, 0.09f);
                drawText(renderer_->getShader(), "CONTINUE",
                         -0.52f, pauseContinueBtnSprite_.y, 0.07f, 0.09f);
                drawText(renderer_->getShader(), "END MATCH",
                         -0.61f, pauseEndBtnSprite_.y, 0.07f, 0.09f);
            }
        }
    }

    // Game Over banner
    if (state_ == GameState::GameOver && gameoverSpritesLoaded_) {
        if (winner_ == 1) {
            gameoverP1Sprite_.draw(renderer_->getShader());
        } else {
            gameoverP2Sprite_.draw(renderer_->getShader());
        }
    }

    renderer_->endFrame();
}

void Game::initBluetooth() {
    if (btBridge_) return;  // already initialized

    // Attach native thread to JVM to get JNIEnv
    javaVM_ = app_->activity->vm;
    JNIEnv *env = nullptr;
    jint res = javaVM_->AttachCurrentThread(&env, nullptr);
    if (res != JNI_OK || !env) {
        aout << "BluetoothLobby: Failed to attach JNI thread" << std::endl;
        return;
    }
    jniEnv_ = env;
    btBridge_ = new BluetoothBridge(jniEnv_, app_->activity->javaGameActivity);
    if (!btBridge_->isReady()) {
        delete btBridge_;
        btBridge_ = nullptr;
        aout << "BluetoothLobby: Bluetooth initialization failed" << std::endl;
        return;
    }
    aout << "BluetoothLobby: Bluetooth initialized" << std::endl;
}

void Game::loadLobbySprites() {
    if (lobbySpritesLoaded_) return;
    auto *am = app_->activity->assetManager;

    auto btnTex = TextureAsset::loadAsset(am, "button.png");
    initButtonSprite(lobbyBtn1_, btnTex, 0.f, 0.55f, 1.8f, 0.35f);
    initButtonSprite(lobbyBtn2_, btnTex, 0.f, -0.55f, 1.8f, 0.35f);

    if (!fontTex_) {
        fontTex_ = TextureAsset::loadAsset(am, "font.png");
    }

    lobbySpritesLoaded_ = true;
}

void Game::initSession() {
    auto *assetManager = app_->activity->assetManager;
    float aspect = renderer_->getAspect();
    float screenHalfW = 2.0f * aspect;
    session_.init(assetManager, aspect, pendingMode_, aiDifficulty_);
    session_.reset();
    sessionInitialized_ = true;
    localPauseActive_ = false;
    remotePauseActive_ = false;

    if (!fontTex_) {
        fontTex_ = TextureAsset::loadAsset(assetManager, "font.png");
    }

    // Load UI button sprites once
    if (!uiSpritesLoaded_) {
        auto btnUpTex   = TextureAsset::loadAsset(assetManager, "btn_up.png");
        auto btnDownTex = TextureAsset::loadAsset(assetManager, "btn_down.png");
        btnUpSprite_.init(btnUpTex, 0.35f, 0.35f);
        btnUpSprite_.x = -screenHalfW + 0.45f;
        btnUpSprite_.y = -1.55f;
        btnDownSprite_.init(btnDownTex, 0.35f, 0.35f);
        btnDownSprite_.x = screenHalfW - 0.45f;
        btnDownSprite_.y = -1.55f;

        auto fireBtnTex = TextureAsset::loadAsset(assetManager, "btn_fire.png");
        btnFireSprite_.init(fireBtnTex, 0.35f, 0.35f);
        btnFireSprite_.x = screenHalfW - 0.45f;
        btnFireSprite_.y = -1.55f + 0.65f;  // above DOWN button with gap

        auto menuBtnTex = TextureAsset::loadAsset(assetManager, "button.png");
        pauseBtnSprite_.init(menuBtnTex, 0.42f, 0.16f);
        pauseBtnSprite_.y = 1.55f;

        pauseDialogBgSprite_.init(menuBtnTex, 1.95f, 1.1f);
        pauseDialogBgSprite_.x = 0.f;
        pauseDialogBgSprite_.y = 0.f;
        pauseDialogBgSprite_.tintR = 0.f;
        pauseDialogBgSprite_.tintG = 0.f;
        pauseDialogBgSprite_.tintB = 0.f;
        pauseDialogBgSprite_.tintA = 0.78f;

        pauseContinueBtnSprite_.init(menuBtnTex, 1.25f, 0.24f);
        pauseContinueBtnSprite_.x = 0.f;
        pauseContinueBtnSprite_.y = 0.20f;

        pauseEndBtnSprite_.init(menuBtnTex, 1.25f, 0.24f);
        pauseEndBtnSprite_.x = 0.f;
        pauseEndBtnSprite_.y = -0.38f;

        uiSpritesLoaded_ = true;
    }
    pauseBtnSprite_.x = screenHalfW - 0.48f;
    pauseBtnSprite_.y = 1.55f;

    // Load Game Over sprites once
    if (!gameoverSpritesLoaded_) {
        auto goP1Tex = TextureAsset::loadAsset(assetManager, "gameover_p1wins.png");
        gameoverP1Sprite_.init(goP1Tex, 2.0f, 0.5f);
        gameoverP1Sprite_.x = 0.0f;
        gameoverP1Sprite_.y = 0.3f;

        auto goP2Tex = TextureAsset::loadAsset(assetManager, "gameover_p2wins.png");
        gameoverP2Sprite_.init(goP2Tex, 2.0f, 0.5f);
        gameoverP2Sprite_.x = 0.0f;
        gameoverP2Sprite_.y = 0.3f;

        gameoverSpritesLoaded_ = true;
    }
}
