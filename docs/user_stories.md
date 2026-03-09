# ByPlanes — User Stories

## US-01: Main Menu

- **ID**: US-01
- **Title**: Main Menu with Game Mode Selection
- **Story**: As a player, I want to see a main menu when I launch the game, so that I can choose between playing against the computer or against another player via Bluetooth.
- **Acceptance Criteria**:
  1. The main menu is displayed immediately after the app launches (no splash delay beyond loading).
  2. Two clearly labeled buttons are present: "vs Computer" and "vs Bluetooth".
  3. Tapping "vs Computer" transitions the player directly into the gameplay screen (US-02).
  4. Tapping "vs Bluetooth" transitions to the Bluetooth connection screen (US-03).
  5. The menu is rendered in landscape orientation and uses the pixel-art visual style consistent with the rest of the game.
- **Priority**: Must Have

---

## US-02: Play vs Computer

- **ID**: US-02
- **Title**: Single-Player Mode vs AI Opponent
- **Story**: As a player, I want to start a game against a computer-controlled opponent, so that I can play and enjoy the game at any time without needing another person.
- **Acceptance Criteria**:
  1. Selecting "vs Computer" from the main menu starts the game immediately with one player-controlled plane and one AI-controlled plane.
  2. The AI opponent moves and shoots autonomously with basic pursue/evade behavior.
  3. The game ends when one plane is destroyed; the result screen (US-07) is then displayed.
  4. The player can control their plane using UP and DOWN buttons (US-04) and shoot by tapping (US-05).
- **Priority**: Must Have

---

## US-03: Play vs Bluetooth Player

- **ID**: US-03
- **Title**: Multiplayer Mode via Bluetooth
- **Story**: As a player, I want to connect to another device via Bluetooth and play against a real opponent, so that I can have a competitive experience with a friend nearby.
- **Acceptance Criteria**:
  1. The Bluetooth connection screen shows a list of nearby discoverable devices or a "Host / Join" option.
  2. One device acts as the host (server) and the other as the client; roles are clearly indicated on screen.
  3. A successful connection transitions both devices into the gameplay screen simultaneously.
  4. If the connection fails or is lost, an error message is displayed and the player is returned to the main menu.
  5. Input and game state are synchronized between both devices with acceptable latency for real-time gameplay.
- **Priority**: Should Have

---

## US-04: Plane Physics (Gravity and Thrust)

- **ID**: US-04
- **Title**: Plane Movement with Gravity and Thrust Controls
- **Story**: As a player, I want my plane to be affected by gravity and respond to UP/DOWN button presses, so that the flying feels physical and engaging.
- **Acceptance Criteria**:
  1. The plane continuously falls due to gravity when no button is pressed.
  2. Pressing the UP button (bottom-left of the screen) applies upward thrust, causing the plane to climb.
  3. Pressing the DOWN button (bottom-right of the screen) applies downward thrust, causing the plane to descend faster.
  4. The plane cannot move beyond the top or bottom boundaries of the screen.
  5. Movement feels smooth at the target frame rate (60 FPS) with no visible jitter.
- **Priority**: Must Have

---

## US-05: Shooting Projectiles

- **ID**: US-05
- **Title**: Fire Projectiles by Tapping the Screen
- **Story**: As a player, I want to shoot projectiles by tapping the screen, so that I can attack the opponent's plane and try to win.
- **Acceptance Criteria**:
  1. Tapping anywhere on the screen (outside the UP/DOWN button areas) fires a projectile from the front of the player's plane.
  2. Projectiles travel horizontally across the screen at a constant speed.
  3. A projectile that hits the opponent's plane registers as a hit and increments the player's score (US-06).
  4. Projectiles that leave the screen boundaries are destroyed and freed from memory.
  5. A maximum of 5 projectiles per player can exist on screen simultaneously; firing is blocked until an existing projectile is destroyed (hit or off-screen).
- **Priority**: Must Have

---

## US-06: In-Game HUD with Score

- **ID**: US-06
- **Title**: Heads-Up Display Showing Current Score
- **Story**: As a player, I want to see the current score on screen during gameplay, so that I always know the state of the match.
- **Acceptance Criteria**:
  1. The HUD displays both players' scores (e.g., "Player: 3 — Enemy: 1") at the top of the screen.
  2. The score updates immediately when a hit is registered.
  3. The HUD does not obstruct critical gameplay areas (planes, projectiles).
  4. Text is rendered in a pixel-art font consistent with the game's visual style.
- **Priority**: Must Have

---

## US-07: Game Over Screen

- **ID**: US-07
- **Title**: Game Over Screen Displaying the Winner
- **Story**: As a player, I want to see a Game Over screen that shows who won, so that the match has a clear conclusion.
- **Acceptance Criteria**:
  1. The Game Over screen is displayed when one player reaches the winning score or the opponent's plane is destroyed.
  2. The screen clearly shows the winner ("You Win!" / "You Lose!" or player names in Bluetooth mode).
  3. The final score for both players is displayed.
  4. A "Play Again" button restarts the same game mode; a "Menu" button returns to the main menu (US-01).
- **Priority**: Must Have

---

## US-08: Landscape Orientation and Fullscreen Immersive Mode

- **ID**: US-08
- **Title**: Forced Landscape with Fullscreen Immersive Display
- **Story**: As a player, I want the game to run in landscape orientation and fullscreen immersive mode, so that I have the maximum screen space and an uninterrupted experience.
- **Acceptance Criteria**:
  1. The app forces landscape orientation on launch and does not rotate to portrait.
  2. System bars (status bar, navigation bar) are hidden using Android's immersive sticky mode.
  3. The game surface occupies the entire screen including any notch/cutout area.
  4. If the player leaves the app and returns, immersive mode is re-applied automatically.
- **Priority**: Must Have
