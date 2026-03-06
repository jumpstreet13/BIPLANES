#include "Renderer.h"

#include <android/native_window.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"

#define PRINT_GL_STRING(s) {aout << #s": "<< glGetString(s) << std::endl;}

#define CORNFLOWER_BLUE 100 / 255.f, 149 / 255.f, 237 / 255.f, 1

// Vertex shader
static const char *vertex = R"vertex(#version 300 es
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

out vec2 fragUV;

uniform mat4 uProjection;
uniform mat4 uModel;

void main() {
    fragUV = inUV;
    gl_Position = uProjection * uModel * vec4(inPosition, 1.0);
}
)vertex";

// Fragment shader
static const char *fragment = R"fragment(#version 300 es
precision mediump float;

in vec2 fragUV;

uniform sampler2D uTexture;

out vec4 outColor;

void main() {
    outColor = texture(uTexture, fragUV);
}
)fragment";

static constexpr float kProjectionHalfHeight = 2.f;
static constexpr float kProjectionNearPlane = -1.f;
static constexpr float kProjectionFarPlane = 1.f;

Renderer::~Renderer() {
    // Release GL objects while the context is still current.
    shader_.reset();

    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
}

void Renderer::beginFrame() {
    updateRenderArea();

    if (shaderNeedsNewProjectionMatrix_) {
        float projectionMatrix[16] = {0};
        Utility::buildOrthographicMatrix(
                projectionMatrix,
                kProjectionHalfHeight,
                getAspect(),
                kProjectionNearPlane,
                kProjectionFarPlane);
        shader_->setProjectionMatrix(projectionMatrix);
        shaderNeedsNewProjectionMatrix_ = false;
    }

    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::endFrame() {
    auto swapResult = eglSwapBuffers(display_, surface_);
    assert(swapResult == EGL_TRUE);
}

void Renderer::initRenderer() {
    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);

    EGLint numConfigs;
    eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);

    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);

    auto config = *std::find_if(
            supportedConfigs.get(),
            supportedConfigs.get() + numConfigs,
            [&display](const EGLConfig &config) {
                EGLint red, green, blue, depth;
                if (eglGetConfigAttrib(display, config, EGL_RED_SIZE, &red)
                    && eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &green)
                    && eglGetConfigAttrib(display, config, EGL_BLUE_SIZE, &blue)
                    && eglGetConfigAttrib(display, config, EGL_DEPTH_SIZE, &depth)) {
                    return red == 8 && green == 8 && blue == 8 && depth == 24;
                }
                return false;
            });

    EGLint format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    nativeFormat_ = format;
    ANativeWindow_setBuffersGeometry(app_->window, 0, 0, format);

    aout << "Renderer::initRenderer: window="
         << ANativeWindow_getWidth(app_->window) << "x"
         << ANativeWindow_getHeight(app_->window)
         << " format=" << format << std::endl;

    EGLSurface surface = eglCreateWindowSurface(display, config, app_->window, nullptr);

    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);

    auto madeCurrent = eglMakeCurrent(display, surface, surface, context);
    assert(madeCurrent);

    display_ = display;
    surface_ = surface;
    context_ = context;
    config_ = config;

    width_ = -1;
    height_ = -1;

    PRINT_GL_STRING(GL_VENDOR);
    PRINT_GL_STRING(GL_RENDERER);
    PRINT_GL_STRING(GL_VERSION);

    shader_ = std::unique_ptr<Shader>(
            Shader::loadShader(vertex, fragment, "inPosition", "inUV", "uProjection"));
    assert(shader_);

    shader_->activate();

    glClearColor(CORNFLOWER_BLUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::updateRenderArea() {
    EGLint surfW, surfH;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &surfW);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &surfH);

    // Check if native window resized (e.g. system bars hidden, cutout mode changed)
    if (app_->window) {
        int32_t winW = ANativeWindow_getWidth(app_->window);
        int32_t winH = ANativeWindow_getHeight(app_->window);

        if (winW > 0 && winH > 0 && (winW != surfW || winH != surfH)) {
            aout << "Renderer: resize detected window=" << winW << "x" << winH
                 << " surface=" << surfW << "x" << surfH << std::endl;
            // Recreate EGL surface to match new window size
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroySurface(display_, surface_);
            ANativeWindow_setBuffersGeometry(app_->window, 0, 0, nativeFormat_);
            surface_ = eglCreateWindowSurface(display_, config_, app_->window, nullptr);
            eglMakeCurrent(display_, surface_, surface_, context_);
            eglQuerySurface(display_, surface_, EGL_WIDTH, &surfW);
            eglQuerySurface(display_, surface_, EGL_HEIGHT, &surfH);
        }
    }

    if (surfW != width_ || surfH != height_) {
        width_ = surfW;
        height_ = surfH;
        viewportX_ = 0;
        viewportY_ = 0;
        viewportWidth_ = width_;
        viewportHeight_ = height_;

        aout << "Renderer: surface=" << width_ << "x" << height_
             << " viewport=" << viewportWidth_ << "x" << viewportHeight_
             << " offset=" << viewportX_ << "," << viewportY_ << std::endl;

        glViewport(viewportX_, viewportY_, viewportWidth_, viewportHeight_);
        shaderNeedsNewProjectionMatrix_ = true;
    }
}
