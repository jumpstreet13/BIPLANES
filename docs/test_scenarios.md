# ByPlanes -- Test Scenarios & Definition of Done

> Document version: 1.0
> Date: 2026-03-01
> Author: QA Engineer

---

## Test Scenarios

### TS-01: Landscape Orientation Lock

| Field | Value |
|---|---|
| **ID** | TS-01 |
| **Related FR** | FR-08 |
| **Related US** | US-08 |
| **Preconditions** | App is installed on a device with Android API >= 30. Device auto-rotation is enabled in system settings. |
| **Steps** | 1. Launch the app. 2. Observe the screen orientation. 3. Rotate the device to portrait position. 4. Observe the screen orientation. 5. Return to landscape. |
| **Expected** | 1. App launches in landscape orientation. 2. System status bar and navigation bar are hidden (immersive sticky mode). 3. Rotating to portrait does not change the app orientation -- it stays landscape. 4. GL viewport covers the entire screen with no letterboxing. |
| **Pass Criteria** | App remains in landscape at all times. `AndroidManifest.xml` contains `android:screenOrientation="landscape"` or `"sensorLandscape"`. Immersive mode flags are set in the Activity. |

---

### TS-02: Gravity (Plane Falls Without Input)

| Field | Value |
|---|---|
| **ID** | TS-02 |
| **Related FR** | FR-04 |
| **Related US** | US-04 |
| **Preconditions** | VS AI game mode is active. Player is not pressing any buttons. |
| **Steps** | 1. Start a VS AI game. 2. Do not touch the screen. 3. Observe the player's plane over 3 seconds. |
| **Expected** | 1. The player's plane begins to fall under gravity (GRAVITY = 4.0 units/s^2). 2. The plane accelerates downward each frame: `velY += GRAVITY * dt`. 3. Velocity is clamped to MAX_VEL_Y = 5.0. 4. The plane stops at the bottom boundary Y = -Y_CLAMP (-1.7). 5. When the plane hits the boundary, velY resets to 0. |
| **Pass Criteria** | Gravity constant is ~4.0 units/s^2 in code. Plane falls and rests at Y = -1.7. Delta-time is used for frame-rate independence. |

---

### TS-03: UP Button (Upward Thrust)

| Field | Value |
|---|---|
| **ID** | TS-03 |
| **Related FR** | FR-04 |
| **Related US** | US-04 |
| **Preconditions** | VS AI game mode is active. Player's plane is at any Y position below Y_CLAMP. |
| **Steps** | 1. Start a VS AI game. 2. Press and hold the UP button (bottom-left 200x200 px zone). 3. Observe the plane moving upward. 4. Continue holding until the plane reaches the top boundary. |
| **Expected** | 1. Holding UP applies upward thrust (THRUST_UP = 8.0 units/s^2). 2. The plane moves upward, counteracting gravity. 3. The plane position is clamped at Y = +1.7 (Y_CLAMP). 4. When clamped, velY resets to 0. 5. Releasing UP causes the plane to start falling again under gravity. |
| **Pass Criteria** | UP button zone rect(0, 520, 200, 720) is correctly defined. THRUST_UP = 8.0 in code. Y_CLAMP = 1.7 enforced. |

---

### TS-04: Fire (Projectile Spawns and Travels)

| Field | Value |
|---|---|
| **ID** | TS-04 |
| **Related FR** | FR-05 |
| **Related US** | US-05 |
| **Preconditions** | VS AI game mode is active. Player has fewer than 5 active projectiles. |
| **Steps** | 1. Start a VS AI game. 2. Tap anywhere on the screen outside the UP and DOWN button zones. 3. Observe the projectile. 4. Wait 2 seconds without the projectile hitting anything. |
| **Expected** | 1. A projectile sprite spawns at the player's plane position. 2. The projectile moves horizontally toward the opponent at speed = 6.0 units/s. 3. After 2.0 seconds (lifetime), the projectile is destroyed if no collision occurred. 4. If 5 projectiles already exist for the player, additional taps are ignored. |
| **Pass Criteria** | Projectile speed = 6.0 in code. Lifetime = 2.0s. Max 5 simultaneous projectiles per player enforced. Projectile spawns at plane position. |

---

### TS-05: Collision Detection (Hit = +1 Score)

| Field | Value |
|---|---|
| **ID** | TS-05 |
| **Related FR** | FR-05, FR-06 |
| **Related US** | US-05, US-06 |
| **Preconditions** | VS AI game mode is active. Both planes and HUD are visible. |
| **Steps** | 1. Start a VS AI game. 2. Maneuver the player plane to align vertically with the opponent. 3. Fire a projectile. 4. Observe the projectile reaching the opponent plane. |
| **Expected** | 1. When the projectile's AABB overlaps the opponent plane's AABB, a hit is registered. 2. The shooter's score increments by 1 (displayed on HUD immediately). 3. The projectile is destroyed on hit. 4. The opponent plane resets to Y = 0.0. |
| **Pass Criteria** | AABB collision detection is implemented between projectile and opponent plane. Score increments by exactly 1. Opponent resets to Y = 0. HUD updates on the same frame. |

---

### TS-06: AI Chase (Follows Player Y, Shoots Every ~1.8s)

| Field | Value |
|---|---|
| **ID** | TS-06 |
| **Related FR** | FR-09 |
| **Related US** | US-02 |
| **Preconditions** | VS AI game mode is active. |
| **Steps** | 1. Start a VS AI game. 2. Move the player plane to Y = +1.0 (hold UP). 3. Observe the AI plane's vertical movement. 4. Move the player plane to Y = -1.0 (hold DOWN). 5. Observe the AI's response. 6. Count AI projectiles over 10 seconds. |
| **Expected** | 1. AI chases the player's Y position: when abs(AI_Y - player_Y) > 0.1, AI applies thrust toward the player. 2. When the difference is <= 0.1 (dead zone), AI stops chasing (no oscillation). 3. AI vertical speed is capped at max_chase_speed = 2.5 units/s. 4. AI fires a projectile every 1.8 seconds (approximately 5-6 shots in 10 seconds). 5. AI respects the 5 simultaneous projectile limit. |
| **Pass Criteria** | Chase threshold = 0.1 in code. Max chase speed = 2.5. Fire interval = 1.8s. Dead zone prevents oscillation. AI uses same physics model as player. |

---

### TS-07: Score Limit WIN_SCORE=5 -> Game Over

| Field | Value |
|---|---|
| **ID** | TS-07 |
| **Related FR** | FR-07 |
| **Related US** | US-07 |
| **Preconditions** | VS AI game mode is active. One player's score is at 4. |
| **Steps** | 1. Play until one player's score reaches 4. 2. Score the 5th hit. 3. Observe the screen. |
| **Expected** | 1. When either player's score reaches WIN_SCORE = 5, the game session ends immediately. 2. A Game Over overlay is displayed with a semi-transparent black overlay (alpha 0.6). 3. The winner is displayed ("PLAYER 1 WINS" or "PLAYER 2 WINS"). 4. Gameplay input (UP/DOWN/fire) is disabled during Game Over. 5. Tapping the screen returns to the Main Menu. |
| **Pass Criteria** | WIN_SCORE = 5 in code. Game over triggers at exactly 5 points. Overlay with winner text is shown. Input is disabled. Tap returns to menu. |

---

### TS-08: Bluetooth Connection (< 10 Seconds)

| Field | Value |
|---|---|
| **ID** | TS-08 |
| **Related FR** | FR-03, FR-10 |
| **Related US** | US-03 |
| **Preconditions** | Two Android devices with Bluetooth enabled. Both have the app installed. Devices are within Bluetooth range. Required permissions are granted. |
| **Steps** | 1. On device A, launch the app and select "VS BLUETOOTH". 2. Device A enters Host mode (RFCOMM server). 3. On device B, launch the app and select "VS BLUETOOTH". 4. Device B scans and connects to device A. 5. Measure time from scan start to connection established. |
| **Expected** | 1. App requests `BLUETOOTH_CONNECT`, `BLUETOOTH_SCAN`, `BLUETOOTH_ADVERTISE` permissions (Android 12+). 2. Host opens an RFCOMM server socket with fixed SPP UUID. 3. Client scans for nearby Bluetooth devices and connects. 4. Connection is established within 10 seconds. 5. Both devices transition to gameplay simultaneously. 6. 52-byte state packets are sent at 60 Hz over RFCOMM. |
| **Pass Criteria** | Connection completes in < 10 seconds. Correct permissions are requested. RFCOMM with SPP UUID is used. Both devices start gameplay. |

---

### TS-09: Bluetooth Disconnect -> Return to Menu (< 3 Seconds)

| Field | Value |
|---|---|
| **ID** | TS-09 |
| **Related FR** | FR-03, FR-10 |
| **Related US** | US-03 |
| **Preconditions** | Two devices are in an active VS Bluetooth game session. |
| **Steps** | 1. During active Bluetooth gameplay, disable Bluetooth on one device. 2. Observe the other device. 3. Measure the time from disconnect to menu appearance. |
| **Expected** | 1. When no packet is received for > 3 seconds, the connection is considered lost. 2. The remaining device returns to the Main Menu within 3 seconds of the timeout. 3. An error indication or transition is shown (no crash or hang). |
| **Pass Criteria** | Timeout threshold = 3 seconds (no packets received). Return to Main Menu occurs within 3 seconds of timeout detection. No crash or ANR. |

---

### TS-10: Multi-Touch (UP + Fire Simultaneously)

| Field | Value |
|---|---|
| **ID** | TS-10 |
| **Related FR** | FR-04, FR-05 |
| **Related US** | US-04, US-05 |
| **Preconditions** | VS AI game mode is active. |
| **Steps** | 1. Start a VS AI game. 2. Press and hold the UP button with one finger. 3. While holding UP, tap the fire zone (outside button areas) with another finger. 4. Observe both actions. |
| **Expected** | 1. The plane continues to move upward while UP is held. 2. A projectile is fired when the fire zone is tapped. 3. Both actions occur simultaneously without conflict. 4. Each active pointer is evaluated independently against button zones. 5. Touch events use ACTION_DOWN, ACTION_POINTER_DOWN, ACTION_MOVE, ACTION_UP, ACTION_POINTER_UP correctly. |
| **Pass Criteria** | Multi-touch is handled via MotionEvent with pointer index tracking. UP + fire can be performed simultaneously. No input is dropped or misrouted. |

---

### TS-11: Pixel-Art Fidelity (GL_NEAREST, No Blur)

| Field | Value |
|---|---|
| **ID** | TS-11 |
| **Related FR** | FR-06 (HUD), FR-01 (menu) |
| **Related US** | US-06, US-08 |
| **Preconditions** | App is running on any screen. |
| **Steps** | 1. Launch the app and observe the main menu sprites. 2. Start a VS AI game and observe gameplay sprites (planes, bullets, blimp, digits, buttons). 3. Inspect for any blurring or smoothing of pixel art edges. |
| **Expected** | 1. All textures are rendered with crisp pixel edges (no bilinear interpolation). 2. Code uses `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST)` and `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST)` for all textures. 3. Sprites appear blocky/pixelated at large scales, which is the intended aesthetic. |
| **Pass Criteria** | All `glTexParameteri` calls for texture filtering use GL_NEAREST (not GL_LINEAR). Verified in texture loading code. |

---

### TS-12: Performance (60 FPS on minSdk 30)

| Field | Value |
|---|---|
| **ID** | TS-12 |
| **Related FR** | FR-02, FR-04 |
| **Related US** | US-04 |
| **Preconditions** | App is running on a device with Android API 30+. VS AI game mode is active with full gameplay (both planes, projectiles, HUD, background). |
| **Steps** | 1. Start a VS AI game. 2. Play for 30 seconds with active input (moving, shooting). 3. Monitor frame rate using Android GPU profiler or systrace. |
| **Expected** | 1. Frame rate remains stable at ~60 FPS (16.67 ms per frame). 2. No visible frame drops or stuttering during normal gameplay. 3. Delta-time physics ensures consistent behaviour regardless of minor frame variations. 4. No memory leaks causing progressive slowdown. |
| **Pass Criteria** | Average FPS >= 58 during 30 seconds of active gameplay. No single frame exceeds 33 ms (half frame budget). Delta-time is used for physics. Game loop uses display refresh rate timing. |

---

## Definition of Done -- Per User Story

### DoD: US-01 (Main Menu)

- [ ] Main Menu screen renders immediately on app launch
- [ ] Two tappable buttons are visible: "VS AI" and "VS Bluetooth"
- [ ] Tapping "VS AI" transitions to gameplay (VS AI mode)
- [ ] Tapping "VS Bluetooth" transitions to Bluetooth connection flow
- [ ] Menu renders in landscape orientation with pixel-art style
- [ ] All menu sprites use GL_NEAREST filtering
- [ ] TS-01, TS-11 pass

### DoD: US-02 (Play vs Computer)

- [ ] Selecting "VS Computer" starts gameplay immediately (< 500 ms)
- [ ] Player controls left plane, AI controls right plane
- [ ] AI moves and shoots autonomously per FR-09
- [ ] Game ends when WIN_SCORE = 5 is reached
- [ ] Player can use UP/DOWN buttons and fire
- [ ] TS-02, TS-03, TS-04, TS-05, TS-06, TS-07, TS-12 pass

### DoD: US-03 (Play vs Bluetooth Player)

- [ ] Bluetooth connection screen is shown after "VS Bluetooth" tap
- [ ] Host/Client roles are implemented (RFCOMM server/client)
- [ ] Connection established within 10 seconds
- [ ] Both devices transition to gameplay simultaneously
- [ ] Connection loss returns to Main Menu within 3 seconds
- [ ] 52-byte state packets sent at 60 Hz
- [ ] Required Bluetooth permissions are requested
- [ ] TS-08, TS-09 pass

### DoD: US-04 (Plane Physics)

- [ ] Gravity (4.0 units/s^2) causes plane to fall without input
- [ ] UP button (THRUST_UP = 8.0) moves plane upward
- [ ] DOWN button (THRUST_DOWN = 4.0) accelerates downward
- [ ] Velocity clamped to MAX_VEL_Y = 5.0
- [ ] Position clamped to Y_CLAMP = 1.7
- [ ] velY resets to 0 at boundaries
- [ ] Delta-time based physics (frame-rate independent)
- [ ] TS-02, TS-03 pass

### DoD: US-05 (Shooting Projectiles)

- [ ] Tap in fire zone spawns projectile at plane position
- [ ] Projectile speed = 6.0 units/s horizontal
- [ ] Projectile lifetime = 2.0 seconds
- [ ] Max 5 simultaneous projectiles per player
- [ ] AABB collision detection with opponent plane
- [ ] Hit: score +1, projectile destroyed, opponent resets to Y = 0
- [ ] TS-04, TS-05 pass

### DoD: US-06 (In-Game HUD)

- [ ] Blimp sprite at top-center of screen
- [ ] Score digits rendered using digit_0..digit_9 sprites
- [ ] Scores update immediately on hit
- [ ] HUD does not obstruct gameplay
- [ ] Pixel-art font with GL_NEAREST filtering
- [ ] TS-05, TS-11 pass

### DoD: US-07 (Game Over Screen)

- [ ] Game over triggers when score reaches WIN_SCORE = 5
- [ ] Winner text displayed correctly
- [ ] Semi-transparent overlay (alpha 0.6) over frozen gameplay
- [ ] Tap to return to Main Menu
- [ ] Gameplay input disabled during Game Over
- [ ] TS-07 pass

### DoD: US-08 (Landscape & Fullscreen)

- [ ] `android:screenOrientation="landscape"` in AndroidManifest.xml
- [ ] Immersive sticky fullscreen mode active
- [ ] System bars hidden at all times
- [ ] GL viewport covers entire screen (no letterboxing)
- [ ] Immersive mode re-applied on return from background
- [ ] TS-01 pass
