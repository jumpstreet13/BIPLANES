#ifndef BYPLANES_SPRITE_H
#define BYPLANES_SPRITE_H

#include <memory>
#include <vector>
#include "Model.h"
#include "Shader.h"
#include "TextureAsset.h"
#include "Utility.h"

class Sprite {
public:
    Sprite() = default;

    void init(std::shared_ptr<TextureAsset> texture, float scaleX = 1.0f, float scaleY = 1.0f);

    void draw(const Shader &shader) const;

    float x = 0.f;
    float y = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float rotation = 0.f;  // in radians
    bool flipX = false;
    bool visible = true;
    float tintR = 1.f;
    float tintG = 1.f;
    float tintB = 1.f;
    float tintA = 1.f;

    // UV sub-region (for sprite sheets / font atlases)
    float uvLeft = 0.f;
    float uvTop = 0.f;
    float uvRight = 1.f;
    float uvBottom = 1.f;

private:
    void buildModel();

    std::shared_ptr<TextureAsset> texture_;
    std::vector<Vertex> vertices_;
    std::vector<Index> indices_;
};

#endif //BYPLANES_SPRITE_H
