#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <memory>

#include "Shader.h"

struct android_app;

class Renderer {
public:
    inline Renderer(android_app *pApp) :
            app_(pApp),
            display_(EGL_NO_DISPLAY),
            surface_(EGL_NO_SURFACE),
            context_(EGL_NO_CONTEXT),
            width_(0),
            height_(0),
            viewportX_(0),
            viewportY_(0),
            viewportWidth_(0),
            viewportHeight_(0),
            shaderNeedsNewProjectionMatrix_(true) {
        initRenderer();
    }

    virtual ~Renderer();

    void beginFrame();
    void endFrame();

    const Shader &getShader() const { return *shader_; }
    EGLint getWidth() const { return viewportWidth_; }
    EGLint getHeight() const { return viewportHeight_; }
    EGLint getOffsetX() const { return viewportX_; }
    EGLint getOffsetY() const { return viewportY_; }
    float getAspect() const {
        return viewportHeight_ > 0
               ? static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_)
               : 16.f / 9.f;
    }

private:
    void initRenderer();
    void updateRenderArea();

    android_app *app_;
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    EGLConfig config_;
    EGLint nativeFormat_ = 0;
    EGLint width_;
    EGLint height_;
    EGLint viewportX_;
    EGLint viewportY_;
    EGLint viewportWidth_;
    EGLint viewportHeight_;

    bool shaderNeedsNewProjectionMatrix_;

    std::unique_ptr<Shader> shader_;
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H
