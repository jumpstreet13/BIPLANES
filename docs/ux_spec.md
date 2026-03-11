# BIPLANES UX Specification

**Version:** 1.0
**Date:** 2026-03-01
**Author:** UX Designer

---

## 1. Screen Wireframes

Reference resolution: **1280x720** (landscape). All coordinates are in logical pixels.
The game uses OpenGL ES 3.0; coordinates are mapped from screen touch to GL viewport.

### 1.1 Main Menu Screen

```
+----------------------------------------------------------+
|                                                          |
|                  bg_sky.png (full screen)                |
|                                                          |
|               +----------------------------+             |
|               |       B Y P L A N E S      |             |
|               +----------------------------+             |
|                                                          |
|               +----------------------------+             |
|               |     [ VS  AI ]             |             |
|               |     menu_vs_ai.png         |             |
|               +----------------------------+             |
|                       (cx=640, cy=360)                   |
|               +----------------------------+             |
|               |     [ VS BLUETOOTH ]       |             |
|               |     menu_vs_bt.png         |             |
|               +----------------------------+             |
|                       (cx=640, cy=440)                   |
|                                                          |
+----------------------------------------------------------+
```

**Tap zones:**
- "VS AI" button:   rect(512, 328, 768, 392) — 256x64 centered at (640,360)
- "VS BT" button:   rect(512, 408, 768, 472) — 256x64 centered at (640,440)

### 1.2 Gameplay Screen

```
+----------------------------------------------------------+
|  clouds_far (parallax layer 1, scrolling left slowly)    |
|  clouds_near (parallax layer 2, scrolling left faster)   |
|                                                          |
|  P1 -->  [plane_p1]          [plane_p2]  <-- P2         |
|  (x~160, y~360)              (x~1120, y~360)            |
|                                                          |
|          [blimp.png centered top]                        |
|           cx=640, cy=80                                  |
|           digits inside: P1 score | P2 score             |
|                                                          |
|                                                          |
|  +--------+                              +--------+     |
|  |  BTN   |                              |  BTN   |     |
|  |   UP   |                              |  DOWN  |     |
|  | 200x200|                              | 200x200|     |
|  +--------+                              +--------+     |
|  (0,520)-(200,720)                  (1080,520)-(1280,720)|
+----------------------------------------------------------+
```

**Control zones (touch areas):**
- BTN_UP zone:   rect(0, 520, 200, 720)   — bottom-left corner, 200x200 px
- BTN_DOWN zone: rect(1080, 520, 1280, 720) — bottom-right corner, 200x200 px
- SHOOT zone:    everything outside BTN_UP and BTN_DOWN rects

**Multi-touch:** Player can hold UP/DOWN and tap SHOOT simultaneously.

### 1.3 Game Over Screen

```
+----------------------------------------------------------+
|                                                          |
|           (gameplay frozen in background)                |
|                                                          |
|         +----------------------------------+             |
|         |  semi-transparent black overlay  |             |
|         |  alpha = 0.6                     |             |
|         |                                  |             |
|         |   gameover_p1wins.png            |             |
|         |   OR gameover_p2wins.png         |             |
|         |   (cx=640, cy=300, 512x128)      |             |
|         |                                  |             |
|         |   [ TAP TO RESTART ]             |             |
|         |   (cx=640, cy=460)               |             |
|         +----------------------------------+             |
|                                                          |
+----------------------------------------------------------+
```

**Tap zone:** Entire screen = restart (return to Main Menu).

---

## 2. Control Zone Specification

| Zone     | Left | Top | Right | Bottom | Size    | Location            |
|----------|------|-----|-------|--------|---------|---------------------|
| BTN_UP   | 0    | 520 | 200   | 720    | 200x200 | Bottom-left corner  |
| BTN_DOWN | 1080 | 520 | 1280  | 720    | 200x200 | Bottom-right corner |
| SHOOT    | *    | *   | *     | *      | *       | Anywhere else       |

### Touch Priority Rules
1. Check BTN_UP zone first (pointer inside rect).
2. Check BTN_DOWN zone second.
3. If pointer is outside both button zones, treat as SHOOT.
4. Multi-touch: each pointer evaluated independently. Player can move + shoot at same time.

### Coordinate Mapping
- Screen touch coordinates (pixels) are mapped to GL normalized coords:
  - `gl_x = touch_x / screen_width`
  - `gl_y = touch_y / screen_height`
- Button zones are defined in screen pixels at 1280x720 reference. Scale proportionally for other resolutions.

---

## 3. Asset List

All assets are placed in `app/src/main/assets/`.

### 3.1 Backgrounds

| File              | Size (px)  | Description                                        |
|-------------------|------------|----------------------------------------------------|
| `bg_sky.png`      | 512 x 256  | Blue sky gradient, tile-friendly. Top: dark sky (50,150,220), bottom: light (100,200,255). |
| `clouds_far.png`  | 512 x 128  | Distant cloud layer, semi-transparent, slow parallax scroll. |
| `clouds_near.png` | 512 x 128  | Near cloud layer, more opaque, faster parallax scroll. |

### 3.2 Planes

| File           | Size (px) | Description                                         |
|----------------|-----------|-----------------------------------------------------|
| `plane_p1.png` | 64 x 32   | Player 1 plane, faces RIGHT. Yellow body, red wing detail. |
| `plane_p2.png` | 64 x 32   | Player 2 plane, faces LEFT. Blue body, mirrored layout. |

### 3.3 Projectiles & Objects

| File         | Size (px)  | Description                                          |
|--------------|------------|------------------------------------------------------|
| `bullet.png` | 16 x 16   | Grey circle projectile on transparent background.    |
| `blimp.png`  | 256 x 128  | Green oval blimp with white rectangle for score display. Positioned at top-center of screen. |

### 3.4 HUD Digits

| File                    | Size (px) | Description                            |
|-------------------------|-----------|----------------------------------------|
| `digit_0.png` .. `digit_9.png` | 16 x 24 each | White digit on black background. Pixel-art font. Used inside blimp for score display. |

### 3.5 Buttons

| File          | Size (px) | Description                                        |
|---------------|-----------|-----------------------------------------------------|
| `btn_up.png`  | 128 x 128 | Transparent background + white UP arrow. Semi-transparent overlay on gameplay. |
| `btn_down.png`| 128 x 128 | Transparent background + white DOWN arrow.          |

### 3.6 Menu & UI

| File                | Size (px)  | Description                                       |
|---------------------|------------|---------------------------------------------------|
| `menu_vs_ai.png`    | 256 x 64   | "VS AI" button sprite for main menu.             |
| `menu_vs_bt.png`    | 256 x 64   | "VS BLUETOOTH" button sprite for main menu.      |
| `gameover_p1wins.png` | 512 x 128 | "PLAYER 1 WINS" banner for game over screen.    |
| `gameover_p2wins.png` | 512 x 128 | "PLAYER 2 WINS" banner for game over screen.    |

---

## 4. UX Rules

### 4.1 Texture Filtering
- All textures MUST use `GL_NEAREST` for min and mag filter.
- This preserves the pixel-art crisp look. No bilinear filtering.
- `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);`
- `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);`

### 4.2 Multi-Touch
- Game MUST support simultaneous input: hold direction + tap to shoot.
- Use `ACTION_DOWN`, `ACTION_POINTER_DOWN`, `ACTION_MOVE`, `ACTION_UP`, `ACTION_POINTER_UP`.
- Each active pointer is evaluated independently against button zones.

### 4.3 Thumb-Reachable Buttons
- UP and DOWN buttons are in bottom corners — natural thumb positions in landscape grip.
- Button visual (128x128) is centered inside the 200x200 touch zone for generous hit area.
- Touch zone is 56% larger than visual to reduce missed taps.

### 4.4 Screen Orientation
- Game is locked to `landscape` (sensorLandscape for auto-rotation).
- AndroidManifest: `android:screenOrientation="sensorLandscape"`

### 4.5 Parallax Background
- `bg_sky.png` fills the entire viewport (stretched to screen).
- `clouds_far.png` scrolls at 20% of game speed (far layer).
- `clouds_near.png` scrolls at 50% of game speed (near layer).
- Both cloud layers tile horizontally (wrap UV).

### 4.6 Score Display
- Score digits rendered inside `blimp.png` white rectangle area.
- Format: `P1_SCORE : P2_SCORE` (e.g., "3 : 5").
- Each digit is a separate sprite from `digit_0..digit_9.png`.

### 4.7 Game Over Overlay
- Semi-transparent black quad (alpha 0.6) over frozen gameplay.
- Winner banner centered on screen.
- Any tap on screen returns to Main Menu.
