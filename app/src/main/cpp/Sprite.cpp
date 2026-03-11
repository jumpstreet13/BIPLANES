#include "Sprite.h"

void Sprite::init(std::shared_ptr<TextureAsset> texture, float sx, float sy) {
    texture_ = std::move(texture);
    scaleX = sx;
    scaleY = sy;
    buildModel();
}

void Sprite::buildModel() {
    // Standard quad vertices: {(1,1,0),(-1,1,0),(-1,-1,0),(1,-1,0)} with UV
    vertices_ = {
            Vertex(Vector3{1, 1, 0}, Vector2{1, 0}),   // 0: top-right
            Vertex(Vector3{-1, 1, 0}, Vector2{0, 0}),   // 1: top-left
            Vertex(Vector3{-1, -1, 0}, Vector2{0, 1}),  // 2: bottom-left
            Vertex(Vector3{1, -1, 0}, Vector2{1, 1})    // 3: bottom-right
    };
    indices_ = {0, 1, 2, 0, 2, 3};
}

void Sprite::draw(const Shader &shader) const {
    if (!visible || !texture_) return;
    const GLint positionAttribute = shader.getPositionAttribute();
    const GLint uvAttribute = shader.getUvAttribute();

    // Build model matrix with translation and scale
    float modelMatrix[16];
    Utility::buildTRSMatrix(modelMatrix, x, y, rotation, scaleX, scaleY);
    shader.setModelMatrix(modelMatrix);
    shader.setTintColor(tintR, tintG, tintB, tintA);

    // Apply UV region and flipX
    std::vector<Vertex> verts = vertices_;
    float uL = flipX ? uvRight : uvLeft;
    float uR = flipX ? uvLeft : uvRight;
    verts[0].uv = Vector2{uR, uvTop};     // top-right
    verts[1].uv = Vector2{uL, uvTop};     // top-left
    verts[2].uv = Vector2{uL, uvBottom};  // bottom-left
    verts[3].uv = Vector2{uR, uvBottom};  // bottom-right

    // Setup vertex attributes
    glVertexAttribPointer(
            positionAttribute, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), verts.data());
    glEnableVertexAttribArray(positionAttribute);

    glVertexAttribPointer(
            uvAttribute, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            ((uint8_t *) verts.data()) + sizeof(Vector3));
    glEnableVertexAttribArray(uvAttribute);

    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_->getTextureID());

    // Draw
    glDrawElements(GL_TRIANGLES, indices_.size(), GL_UNSIGNED_SHORT, indices_.data());

    glDisableVertexAttribArray(uvAttribute);
    glDisableVertexAttribArray(positionAttribute);
}
