# Functional Requirements — BIPLANES

> Document version: 1.0
> Date: 2026-03-01
> Author: Analyst

---

## FR-01: Main Menu

| Field | Value |
|---|---|
| **ID** | FR-01 |
| **Source** | US-01 |
| **Description** | At launch the game displays a Main Menu screen with two tappable regions: **"VS COMPUTER"** (upper half of the screen) and **"VS BLUETOOTH"** (lower half of the screen). Each region occupies ~50 % of the viewport height. Tapping a region transitions to the corresponding game mode. The menu is rendered using the same OpenGL ES 3.0 pipeline as gameplay (orthographic projection, pixel-art sprites with GL_NEAREST filtering). |
| **Acceptance Criteria** | 1. Two visually distinct regions are rendered on screen. 2. Tap inside the upper region starts VS AI mode (-> FR-02). 3. Tap inside the lower region starts VS Bluetooth mode (-> FR-03). 4. No other interactive elements exist on the menu screen. 5. Menu renders in landscape orientation matching FR-08. |
| **Dependencies** | FR-08 (landscape lock must be active before menu renders) |

---

## FR-02: VS AI Mode -- Immediate Start

| Field | Value |
|---|---|
| **ID** | FR-02 |
| **Source** | US-02 |
| **Description** | When the player selects "VS COMPUTER" from the main menu, a game session starts immediately. The player controls the **left plane**, the AI controls the **right plane**. Both planes spawn at their default Y positions (Y = 0.0 in world space, centered vertically). The game loop runs at the display refresh rate (target 60 FPS) with delta-time physics. The session continues until one plane reaches WIN_SCORE hits (FR-07). |
| **Acceptance Criteria** | 1. Transition from menu to gameplay takes < 500 ms (no loading screen required). 2. Player plane appears on the left side; AI plane on the right side. 3. Physics, input, rendering, and AI systems are all active on the first frame. 4. Score for both players starts at 0. 5. AI behaviour conforms to FR-09. |
| **Dependencies** | FR-01, FR-04, FR-05, FR-06, FR-09 |

---

## FR-03: VS Bluetooth Mode -- RFCOMM Scan / Advertise

| Field | Value |
|---|---|
| **ID** | FR-03 |
| **Source** | US-03 |
| **Description** | When the player selects "VS BLUETOOTH", the app enters Bluetooth Classic pairing flow. One device acts as **Host** (RFCOMM server / advertiser) and the other as **Client** (scanner / connector). The app uses Android Bluetooth Classic RFCOMM with a fixed SPP UUID. Once two devices are connected, a game session starts identically to FR-02 but with the remote player replacing the AI. During gameplay, each device sends a 52-byte state packet at 60 Hz (FR-10). |
| **Acceptance Criteria** | 1. App requests `BLUETOOTH_CONNECT`, `BLUETOOTH_SCAN`, and `BLUETOOTH_ADVERTISE` runtime permissions (Android 12+). 2. Host device opens an RFCOMM server socket and waits for connection. 3. Client device scans for nearby Bluetooth devices and connects to the Host. 4. Connection is established within 10 seconds on a local network. 5. Once connected, both devices transition to gameplay simultaneously. 6. If connection is lost during gameplay, both devices return to Main Menu. |
| **Dependencies** | FR-01, FR-10 |

---

## FR-04: Plane Physics

| Field | Value |
|---|---|
| **ID** | FR-04 |
| **Source** | US-04 |
| **Description** | Each plane is subject to continuous downward gravity and player-controlled thrust. Physics constants (world units per second): **GRAVITY = 4.0** (downward acceleration applied every frame), **THRUST_UP = 8.0** (upward acceleration while UP button held), **THRUST_DOWN = 4.0** (downward acceleration added while DOWN button held), **MAX_VEL_Y = 5.0** (velocity clamped to [-MAX_VEL_Y, +MAX_VEL_Y]), **Y_CLAMP = 1.7** (plane position clamped to [-Y_CLAMP, +Y_CLAMP] in world space). Physics update uses delta-time: `velY += acceleration * dt; velY = clamp(velY, -MAX_VEL_Y, MAX_VEL_Y); posY += velY * dt; posY = clamp(posY, -Y_CLAMP, Y_CLAMP)`. When posY hits a clamp boundary, velY is reset to 0. |
| **Acceptance Criteria** | 1. With no input, the plane falls under gravity and comes to rest at Y = -Y_CLAMP. 2. Holding UP button counteracts gravity and moves the plane upward. 3. Holding DOWN button accelerates downward faster than gravity alone. 4. Vertical velocity never exceeds MAX_VEL_Y in either direction. 5. Plane position never exceeds Y_CLAMP boundaries. 6. Physics is frame-rate independent (delta-time based). |
| **Dependencies** | None |

---

## FR-05: Projectile System

| Field | Value |
|---|---|
| **ID** | FR-05 |
| **Source** | US-05 |
| **Description** | The player fires a projectile by tapping anywhere in the **fire zone** (the screen area excluding the UP button region in the bottom-left 200x200 px and the DOWN button region in the bottom-right 200x200 px). Projectile parameters: **speed = 6.0** world units/second (horizontal, towards opponent), **lifetime = 2.0** seconds (projectile is destroyed after this duration), **max simultaneous projectiles = 5** per player. Projectiles spawn at the plane's current position and travel horizontally. A hit is registered when a projectile's bounding box overlaps the opponent plane's bounding box (AABB collision). On hit: the projectile is destroyed, the shooter's score increments by 1, and the opponent plane resets to Y = 0.0. |
| **Acceptance Criteria** | 1. Tapping fire zone spawns a projectile at the player's plane position. 2. Projectile moves horizontally at 6.0 world units/second toward the opponent. 3. Projectile is destroyed after 2.0 seconds if no collision occurs. 4. No more than 5 projectiles per player exist simultaneously; excess taps are ignored. 5. AABB collision detection registers hits correctly. 6. On hit: score increments, projectile destroyed, opponent plane resets to Y = 0. |
| **Dependencies** | FR-04 (plane position needed for spawn point and collision) |

---

## FR-06: HUD (Heads-Up Display)

| Field | Value |
|---|---|
| **ID** | FR-06 |
| **Source** | US-06 |
| **Description** | During gameplay, a HUD is rendered on top of the game scene. The HUD displays: a **blimp sprite** centered at the top of the screen, and the current **score** for each player rendered using digit sprites (0-9). The left player's score appears to the left of the blimp, the right player's score to the right. Scores update immediately on hit events. All HUD elements use pixel-art sprites with GL_NEAREST filtering. |
| **Acceptance Criteria** | 1. Blimp sprite is visible at the top-center of the screen during gameplay. 2. Left score displays correctly for the player (or host in Bluetooth mode). 3. Right score displays correctly for the AI (or client in Bluetooth mode). 4. Scores update within the same frame as the hit event. 5. HUD elements do not interfere with gameplay input (rendered as overlay). |
| **Dependencies** | FR-05 (score changes triggered by hits) |

---

## FR-07: Game Over Screen

| Field | Value |
|---|---|
| **ID** | FR-07 |
| **Source** | US-07 |
| **Description** | When either player's score reaches **WIN_SCORE = 5**, the game session ends. A Game Over overlay is displayed showing the winner ("LEFT WINS" or "RIGHT WINS") and two tappable buttons: **"Play Again"** (restarts the same game mode with scores reset to 0) and **"Main Menu"** (returns to the main menu screen FR-01). In Bluetooth mode, "Play Again" requires both devices to acknowledge; if the Bluetooth connection is lost, only "Main Menu" is available. |
| **Acceptance Criteria** | 1. Game ends immediately when a player's score reaches 5. 2. Winner text is displayed correctly. 3. "Play Again" restarts the session in the same mode with scores at 0. 4. "Main Menu" transitions back to FR-01. 5. During Game Over, gameplay input (UP/DOWN/fire) is disabled. 6. In Bluetooth mode, if connection is lost, only "Main Menu" is shown. |
| **Dependencies** | FR-05 (score tracking), FR-01 (menu transition) |

---

## FR-08: Landscape Lock and Immersive Fullscreen

| Field | Value |
|---|---|
| **ID** | FR-08 |
| **Source** | US-08 |
| **Description** | The application is locked to **landscape orientation** via `android:screenOrientation="landscape"` in AndroidManifest.xml. The app runs in **immersive sticky fullscreen** mode, hiding the status bar and navigation bar. The system UI auto-hides after any transient appearance. The GL viewport matches the full display resolution. |
| **Acceptance Criteria** | 1. App always displays in landscape, regardless of device rotation. 2. Status bar and navigation bar are hidden at all times. 3. System UI reappears only on swipe from edge and auto-hides after ~3 seconds. 4. GL viewport covers the entire screen (no letterboxing). 5. `AndroidManifest.xml` contains `android:screenOrientation="landscape"`. |
| **Dependencies** | None |

---

## FR-09: AI Controller

| Field | Value |
|---|---|
| **ID** | FR-09 |
| **Source** | US-02 |
| **Description** | In VS AI mode, the opponent plane is controlled by an AI controller. Behaviour: the AI **chases the player's Y position** -- if the difference between AI Y and player Y exceeds a **threshold of 0.1** world units, the AI applies thrust in the direction of the player. The AI's effective chase speed is capped at **max_chase_speed = 2.5** world units/second. The AI fires a projectile at a fixed **interval of 1.8 seconds** (subject to the same max 5 projectile limit as the player per FR-05). The AI uses the same physics model as the player (FR-04). |
| **Acceptance Criteria** | 1. AI moves toward the player's Y position when distance > 0.1. 2. AI does not oscillate when within the 0.1 threshold (dead zone). 3. AI vertical movement speed does not exceed 2.5 world units/second. 4. AI fires a projectile every 1.8 seconds. 5. AI respects the 5 simultaneous projectile limit. 6. AI plane obeys the same physics constants as the player plane. |
| **Dependencies** | FR-04 (physics model), FR-05 (projectile system) |

---

## FR-10: Bluetooth Network Packet

| Field | Value |
|---|---|
| **ID** | FR-10 |
| **Source** | US-03 |
| **Description** | In VS Bluetooth mode, each device sends a **52-byte binary packet** over the RFCOMM channel at **60 Hz** (once per frame). Packet layout (little-endian): `[float32 planeY (4B), float32 planeVelY (4B), uint8 projectileCount (1B), float32 projX[5] (20B), float32 projY[5] (20B), padding (3B)]` = **52 bytes total**. The receiving device uses this data to update the remote plane's position and render remote projectiles. Packets are fire-and-forget (UDP-style over RFCOMM); no acknowledgment is required. If no packet is received for > 3 seconds, the connection is considered lost and both devices return to Main Menu. |
| **Acceptance Criteria** | 1. Packet size is exactly 52 bytes. 2. Packet is sent once per frame (~60 Hz). 3. Packet layout matches the specified format (planeY, planeVelY, count, projX[5], projY[5], padding). 4. Receiving device correctly parses and applies remote state. 5. Connection timeout after 3 seconds of no packets triggers return to Main Menu. 6. Byte order is little-endian. |
| **Dependencies** | FR-03 (Bluetooth connection must be established) |

---

## Dependency Matrix

| FR | Depends On |
|---|---|
| FR-01 | FR-08 |
| FR-02 | FR-01, FR-04, FR-05, FR-06, FR-09 |
| FR-03 | FR-01, FR-10 |
| FR-04 | -- |
| FR-05 | FR-04 |
| FR-06 | FR-05 |
| FR-07 | FR-05, FR-01 |
| FR-08 | -- |
| FR-09 | FR-04, FR-05 |
| FR-10 | FR-03 |

---

## Development Task Decomposition

| Task ID | Description | Depends On | Deliverable |
|---|---|---|---|
| T-1.1 | Add `uModel` uniform to vertex shader; pass model matrix from C++ | -- | Modified `shader.vert`, updated `Renderer` uniform upload |
| T-1.2 | Implement delta-time calculation in game loop (`std::chrono`) | -- | `getDeltaTime()` utility in game loop |
| T-1.3 | Create utility helpers: `clamp()`, `lerp()`, AABB struct | -- | `GameUtils.h` |
| T-1.4 | Define `GameState` enum (MENU, PLAYING, GAME_OVER) and `GameConstants` struct | -- | `GameState.h`, `GameConstants.h` |
| T-1.5 | Implement `InputManager` -- multi-touch routing for UP/DOWN/FIRE zones | T-1.4 | `InputManager.h/.cpp` |
| T-1.6 | Create `Sprite` class (textured quad, position, scale, model matrix) | T-1.1 | `Sprite.h/.cpp` |
| T-1.7 | Create game entities: `Plane`, `Projectile`, `HUD`, `Background` | T-1.3, T-1.6 | Entity header/source files |
| T-1.8 | Implement `AIController` (chase logic, fire timer) | T-1.5, T-1.7 | `AIController.h/.cpp` |
| T-1.9 | Implement `GameSession` (score tracking, collision, win condition, state transitions) | T-1.7, T-1.8 | `GameSession.h/.cpp` |
| T-1.10 | Refactor `Renderer` and create `Game` class (main loop orchestrator, menu/play/gameover states) | T-1.5, T-1.9 | `Game.h/.cpp`, refactored `Renderer` |
| T-1.11 | Implement Bluetooth multiplayer: `BluetoothBridge.cpp` (JNI) + `BluetoothManager.kt` (Kotlin RFCOMM) | T-1.9 | `BluetoothBridge.h/.cpp`, `BluetoothManager.kt` |
| T-2.1 | Update `AndroidManifest.xml` (landscape, permissions) and `CMakeLists.txt` (new source files) | T-1.10, T-1.11 | Updated config files |

### Task Dependency Graph

```
T-1.1 ─────────────────┐
T-1.2 (independent)     │
T-1.3 ──────────┐       │
T-1.4 ──┐       │       │
        ▼       ▼       ▼
      T-1.5   T-1.6   T-1.6
        │       │       │
        │       ▼       │
        │     T-1.7 ◄───┘
        │       │
        ▼       ▼
      T-1.8 ◄─ T-1.7
        │       │
        ▼       ▼
      T-1.9 ◄──┘
        │
        ▼
      T-1.10 ◄── T-1.5
        │
        ├───────┐
        ▼       ▼
      T-1.11  T-2.1 ◄── T-1.11
```

### Critical Path

**T-1.1 -> T-1.6 -> T-1.7 -> T-1.9 -> T-1.10 -> T-2.1**

Tasks T-1.2, T-1.3, T-1.4 can be developed in parallel as foundation work. T-1.11 (Bluetooth) can be developed in parallel with T-1.10 once T-1.9 is complete.
