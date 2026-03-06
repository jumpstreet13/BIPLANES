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
    float btnW = 1.8f;
    float btnH = 0.35f;
    menuBtn1_.init(btnTex, btnW, btnH);
    menuBtn1_.x = 0.f;
    menuBtn1_.y = 0.3f;

    menuBtn2_.init(btnTex, btnW, btnH);
    menuBtn2_.x = 0.f;
    menuBtn2_.y = -0.4f;

    // Font texture for button text
    fontTex_ = TextureAsset::loadAsset(am, "font.png");

    menuSpritesLoaded_ = true;
}

void Game::drawText(const Shader &shader, const char *text,
                    float startX, float y, float charW, float charH) const {
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
        glyph.draw(shader);

        cx += charW * 2.f;
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

                // VS COMPUTER button (y: -0.05..0.65)
                if (wx > -1.8f && wx < 1.8f && wy > -0.05f && wy < 0.65f) {
                    pendingMode_ = GameMode::VsAI;
                    state_ = GameState::Playing;
                    sessionInitialized_ = false;
                }
                // VS BLUETOOTH button (y: -0.75..-0.05)
                else if (wx > -1.8f && wx < 1.8f && wy > -0.75f && wy < -0.05f) {
                    pendingMode_ = GameMode::VsBluetooth;
                    state_ = GameState::BluetoothLobby;
                    btHosting_ = false;
                    btJoining_ = false;
                    btStatusTimer_ = 0.f;
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

                if (!btHosting_ && !btJoining_) {
                    // HOST GAME button
                    if (wx > -1.8f && wx < 1.8f && wy > -0.05f && wy < 0.65f) {
                        initBluetooth();
                        if (btBridge_) {
                            btBridge_->startAdvertising();
                            btHosting_ = true;
                            btStatusTimer_ = 0.f;
                            aout << "BluetoothLobby: Hosting..." << std::endl;
                        }
                    }
                    // JOIN GAME button
                    else if (wx > -1.8f && wx < 1.8f && wy > -0.75f && wy < -0.05f) {
                        initBluetooth();
                        if (btBridge_) {
                            btBridge_->startScanning();
                            btJoining_ = true;
                            btStatusTimer_ = 0.f;
                            aout << "BluetoothLobby: Joining..." << std::endl;
                        }
                    }
                } else {
                    // Tap anywhere to cancel and go back
                    if (btBridge_) {
                        btBridge_->disconnect();
                    }
                    btHosting_ = false;
                    btJoining_ = false;
                    state_ = GameState::MainMenu;
                }
            }
            break;
        }
        case GameState::Playing:
            break;
        case GameState::GameOver: {
            if (touch.screenTapped || touch.upButtonHeld || touch.downButtonHeld) {
                // Disconnect Bluetooth if it was a BT game
                if (pendingMode_ == GameMode::VsBluetooth && btBridge_) {
                    btBridge_->disconnect();
                    btHosting_ = false;
                    btJoining_ = false;
                }
                state_ = GameState::MainMenu;
                sessionInitialized_ = false;
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

    if (state_ == GameState::MainMenu) {
        menuAnimTimer_ += dt;
        if (menuSpritesLoaded_) {
            menuBackground_.update(dt);
        }
    }

    if (state_ == GameState::BluetoothLobby) {
        btStatusTimer_ += dt;
        if (!menuSpritesLoaded_ && renderer_->getWidth() > 0) {
            loadMenuSprites();
        }
        if (menuSpritesLoaded_) {
            menuBackground_.update(dt);
        }
        // Check if Bluetooth connected
        if ((btHosting_ || btJoining_) && btBridge_ && btBridge_->isConnected()) {
            aout << "BluetoothLobby: Connected! Starting game." << std::endl;
            state_ = GameState::Playing;
            sessionInitialized_ = false;
        }
    }

    if (state_ == GameState::Playing) {
        if (!sessionInitialized_) {
            initSession();
        }
        const auto &touch = inputManager_.getState();
        bool gameOver = session_.update(dt, touch);
        if (gameOver) {
            state_ = GameState::GameOver;
            winner_ = session_.getWinner();
            aout << "Game Over! Winner: Player " << winner_ << std::endl;
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

            if (!btHosting_ && !btJoining_) {
                // Show HOST GAME / JOIN GAME buttons
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
            } else {
                // Show waiting status
                const char *statusBase = btHosting_ ? "WAITING" : "CONNECTING";
                // Animated dots
                int dots = ((int)(btStatusTimer_ * 2.f)) % 4;
                char statusText[32];
                snprintf(statusText, sizeof(statusText), "%s%.*s", statusBase, dots, "...");

                float tw = strlen(statusText) * charW * 2.f;
                drawText(renderer_->getShader(), statusText,
                         -tw / 2.f + charW, 0.1f, charW, charH);

                // "TAP TO CANCEL" below
                const char *cancel = "TAP TO CANCEL";
                float twc = strlen(cancel) * charW * 2.f;
                drawText(renderer_->getShader(), cancel,
                         -twc / 2.f + charW, -0.4f, charW * 0.7f, charH * 0.7f);
            }
        }
    }

    // Playing or GameOver — draw session
    if (state_ == GameState::Playing || state_ == GameState::GameOver) {
        session_.draw(renderer_->getShader());
    }

    // Playing — draw UP/DOWN/FIRE buttons
    if (state_ == GameState::Playing && uiSpritesLoaded_) {
        btnUpSprite_.draw(renderer_->getShader());
        btnDownSprite_.draw(renderer_->getShader());
        btnFireSprite_.draw(renderer_->getShader());
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
    aout << "BluetoothLobby: Bluetooth initialized" << std::endl;
}

void Game::loadLobbySprites() {
    if (lobbySpritesLoaded_) return;
    auto *am = app_->activity->assetManager;

    auto btnTex = TextureAsset::loadAsset(am, "button.png");
    float btnW = 1.8f;
    float btnH = 0.35f;
    lobbyBtn1_.init(btnTex, btnW, btnH);
    lobbyBtn1_.x = 0.f;
    lobbyBtn1_.y = 0.3f;

    lobbyBtn2_.init(btnTex, btnW, btnH);
    lobbyBtn2_.x = 0.f;
    lobbyBtn2_.y = -0.4f;

    if (!fontTex_) {
        fontTex_ = TextureAsset::loadAsset(am, "font.png");
    }

    lobbySpritesLoaded_ = true;
}

void Game::initSession() {
    auto *assetManager = app_->activity->assetManager;
    float aspect = renderer_->getAspect();
    session_.init(assetManager, aspect);
    session_.reset();
    sessionInitialized_ = true;

    // Load UI button sprites once
    if (!uiSpritesLoaded_) {
        auto btnUpTex   = TextureAsset::loadAsset(assetManager, "btn_up.png");
        auto btnDownTex = TextureAsset::loadAsset(assetManager, "btn_down.png");
        float screenHalfW = WORLD_HALF_W;
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
        uiSpritesLoaded_ = true;
    }

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
