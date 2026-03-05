#include "Projectile.h"

void ProjectilePool::init(std::shared_ptr<TextureAsset> texture) {
    texture_ = std::move(texture);
    for (auto &p : projectiles_) {
        p.active = false;
        p.sprite.init(texture_, 0.05f, 0.05f);
        p.sprite.visible = false;
    }
}

void ProjectilePool::spawn(float x, float y, float velX) {
    for (auto &p : projectiles_) {
        if (!p.active) {
            p.x = x;
            p.y = y;
            p.velX = velX;
            p.lifetime = BULLET_LIFETIME;
            p.active = true;
            p.sprite.visible = true;
            return;
        }
    }
}

void ProjectilePool::spawnDirectional(float x, float y, float velX, float velY) {
    for (auto &p : projectiles_) {
        if (!p.active) {
            p.x = x; p.y = y;
            p.velX = velX; p.velY = velY;
            p.lifetime = BULLET_LIFETIME;
            p.active = true;
            p.sprite.visible = true;
            return;
        }
    }
}

void ProjectilePool::updateAll(float dt) {
    for (auto &p : projectiles_) {
        if (!p.active) continue;

        p.x += p.velX * dt;
        p.y += p.velY * dt;
        p.lifetime -= dt;

        // Wrap left/right like planes
        if (p.x >  WORLD_HALF_W) p.x -= 2.f * WORLD_HALF_W;
        if (p.x < -WORLD_HALF_W) p.x += 2.f * WORLD_HALF_W;

        // Bullet hits ground — disappear
        if (p.y <= GROUND_Y) {
            p.active = false;
            p.sprite.visible = false;
            continue;
        }

        if (p.lifetime <= 0.f) {
            p.active = false;
            p.sprite.visible = false;
        }

        p.sprite.x = p.x;
        p.sprite.y = p.y;
    }
}

void ProjectilePool::drawAll(const Shader &shader) const {
    for (const auto &p : projectiles_) {
        if (p.active) {
            p.sprite.draw(shader);
        }
    }
}
