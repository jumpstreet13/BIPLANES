#ifndef BYPLANES_GAMECONSTANTS_H
#define BYPLANES_GAMECONSTANTS_H

constexpr float PLANE_SPEED    = 2.5f;   // constant speed, world units/s
constexpr float ROTATION_SPEED = 2.8f;   // rotation speed, radians/s
constexpr float WORLD_HALF_W   = 3.56f;  // world half-width
constexpr float WORLD_HALF_H   = 2.0f;   // world half-height
constexpr float PLANE_HALF_W   = 0.3f;
constexpr float PLANE_HALF_H   = 0.2f;
constexpr float GROUND_Y       = -1.65f; // ground collision level (top of grass in background.png)
constexpr float BULLET_SPEED   = 5.0f;
constexpr float BULLET_LIFETIME= 2.0f;
constexpr float BULLET_HALF_SIZE = 0.05f;
constexpr float FIRE_COOLDOWN  = 0.8f;   // seconds between shots
constexpr int   PLANE_MAX_HP   = 3;      // hits before explosion
constexpr float AI_FIRE_INTERVAL = 1.8f;
constexpr float AI_CHASE_SPEED   = 2.5f;
constexpr int   WIN_SCORE      = 5;
constexpr int   MAX_PROJECTILES = 10;
constexpr int   EXPLOSION_FRAMES = 7;
constexpr float EXPLOSION_DURATION = 0.8f;
constexpr float RESPAWN_INVULN   = 1.5f;  // seconds of invulnerability after respawn
constexpr float TAKEOFF_ACCEL   = 2.0f;  // ground acceleration (world units/s^2)
constexpr float TAKEOFF_LIFTOFF = 0.3f;  // sin(angle) threshold to lift off
constexpr float HOUSE_X         = 0.0f;  // house X position for collision
constexpr float BARREL_ROLL_PITCH = 1.2f;    // ~69° from horizontal triggers roll
constexpr float BARREL_ROLL_DURATION = 0.5f; // seconds for full 360° roll

inline float worldHalfWidthForAspect(float aspect) {
    return WORLD_HALF_H * aspect;
}

#endif
