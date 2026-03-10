#include "Renderer.h"

#include <android/native_window.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"

#define PRINT_GL_STRING(s) { \
    const GLubyte* value = glGetString(s); \
    aout << #s ": " << (value ? reinterpret_cast<const char*>(value) : "(null)") << std::endl; \
}

#define CORNFLOWER_BLUE 100 / 255.f, 149 / 255.f, 237 / 255.f, 1

// Vertex shader
static const char *vertexEs3 = R"vertex(#version 300 es
in vec3 inPosition;
in vec2 inUV;

out vec2 fragUV;

uniform mat4 uProjection;
uniform mat4 uModel;

void main() {
    fragUV = inUV;
    gl_Position = uProjection * uModel * vec4(inPosition, 1.0);
}
)vertex";

// Fragment shader
static const char *fragmentEs3 = R"fragment(#version 300 es
precision mediump float;

in vec2 fragUV;

uniform sampler2D uTexture;
uniform vec4 uTint;

out vec4 outColor;

void main() {
    outColor = texture(uTexture, fragUV) * uTint;
}
)fragment";

static const char *vertexEs2 = R"vertex(#version 100
attribute vec3 inPosition;
attribute vec2 inUV;

varying vec2 fragUV;

uniform mat4 uProjection;
uniform mat4 uModel;

void main() {
    fragUV = inUV;
    gl_Position = uProjection * uModel * vec4(inPosition, 1.0);
}
)vertex";

static const char *fragmentEs2 = R"fragment(#version 100
precision mediump float;

varying vec2 fragUV;

uniform sampler2D uTexture;
uniform vec4 uTint;

void main() {
    gl_FragColor = texture2D(uTexture, fragUV) * uTint;
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
    if (!isReady()) {
        if (app_ && app_->window) {
            initRenderer();
        }
        if (!isReady()) {
            return;
        }
    }

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
    if (!isReady()) {
        return;
    }
    auto swapResult = eglSwapBuffers(display_, surface_);
    assert(swapResult == EGL_TRUE);
}

void Renderer::initRenderer() {
    if (!app_ || !app_->window || isReady()) {
        return;
    }

    struct RendererInitAttempt {
        EGLint clientVersion;
        EGLint renderableType;
        const char* vertexShader;
        const char* fragmentShader;
        const char* label;
    };

    const RendererInitAttempt attempts[] = {
        {3, EGL_OPENGL_ES3_BIT, vertexEs3, fragmentEs3, "OpenGL ES 3"},
        {2, EGL_OPENGL_ES2_BIT, vertexEs2, fragmentEs2, "OpenGL ES 2"}
    };

    for (const auto& attempt : attempts) {
        constexpr EGLint colorAttribTail[] = {
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_BLUE_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_RED_SIZE, 8,
                EGL_NONE
        };
        const EGLint attribs[] = {
                EGL_RENDERABLE_TYPE, attempt.renderableType,
                colorAttribTail[0], colorAttribTail[1],
                colorAttribTail[2], colorAttribTail[3],
                colorAttribTail[4], colorAttribTail[5],
                colorAttribTail[6], colorAttribTail[7],
                colorAttribTail[8]
        };

        auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY) {
            aout << "Renderer::initRenderer: eglGetDisplay failed for " << attempt.label << std::endl;
            continue;
        }
        if (eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
            aout << "Renderer::initRenderer: eglInitialize failed for " << attempt.label << std::endl;
            continue;
        }

        EGLint numConfigs = 0;
        if (eglChooseConfig(display, attribs, nullptr, 0, &numConfigs) != EGL_TRUE || numConfigs <= 0) {
            aout << "Renderer::initRenderer: no matching EGL configs for " << attempt.label << std::endl;
            eglTerminate(display);
            continue;
        }

        std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
        if (eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs) != EGL_TRUE
            || numConfigs <= 0) {
            aout << "Renderer::initRenderer: failed to enumerate EGL configs for " << attempt.label << std::endl;
            eglTerminate(display);
            continue;
        }

        EGLConfig config = supportedConfigs[0];
        for (int i = 0; i < numConfigs; ++i) {
            EGLint red = 0;
            EGLint green = 0;
            EGLint blue = 0;
            if (eglGetConfigAttrib(display, supportedConfigs[i], EGL_RED_SIZE, &red)
                && eglGetConfigAttrib(display, supportedConfigs[i], EGL_GREEN_SIZE, &green)
                && eglGetConfigAttrib(display, supportedConfigs[i], EGL_BLUE_SIZE, &blue)
                && red == 8 && green == 8 && blue == 8) {
                config = supportedConfigs[i];
                break;
            }
        }

        EGLint format = 0;
        if (eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format) != EGL_TRUE) {
            aout << "Renderer::initRenderer: failed to get visual format for " << attempt.label << std::endl;
            eglTerminate(display);
            continue;
        }
        ANativeWindow_setBuffersGeometry(app_->window, 0, 0, format);

        EGLSurface surface = eglCreateWindowSurface(display, config, app_->window, nullptr);
        if (surface == EGL_NO_SURFACE) {
            aout << "Renderer::initRenderer: eglCreateWindowSurface failed for " << attempt.label << std::endl;
            eglTerminate(display);
            continue;
        }

        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, attempt.clientVersion, EGL_NONE};
        EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);
        if (context == EGL_NO_CONTEXT) {
            aout << "Renderer::initRenderer: eglCreateContext failed for " << attempt.label << std::endl;
            eglDestroySurface(display, surface);
            eglTerminate(display);
            continue;
        }

        if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
            aout << "Renderer::initRenderer: eglMakeCurrent failed for " << attempt.label << std::endl;
            eglDestroyContext(display, context);
            eglDestroySurface(display, surface);
            eglTerminate(display);
            continue;
        }

        auto shader = std::unique_ptr<Shader>(
                Shader::loadShader(
                    attempt.vertexShader,
                    attempt.fragmentShader,
                    "inPosition",
                    "inUV",
                    "uProjection"));
        if (!shader) {
            aout << "Renderer::initRenderer: shader creation failed for " << attempt.label << std::endl;
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
            eglDestroySurface(display, surface);
            eglTerminate(display);
            continue;
        }

        display_ = display;
        surface_ = surface;
        context_ = context;
        config_ = config;
        nativeFormat_ = format;
        lastWindow_ = app_->window;
        width_ = -1;
        height_ = -1;
        shader_ = std::move(shader);

        aout << "Renderer::initRenderer: using " << attempt.label
             << " window=" << ANativeWindow_getWidth(app_->window) << "x"
             << ANativeWindow_getHeight(app_->window)
             << " format=" << format << std::endl;
        PRINT_GL_STRING(GL_VENDOR);
        PRINT_GL_STRING(GL_RENDERER);
        PRINT_GL_STRING(GL_VERSION);

        shader_->activate();
        shader_->setTintColor(1.f, 1.f, 1.f, 1.f);

        glClearColor(CORNFLOWER_BLUE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return;
    }

    aout << "Renderer::initRenderer: all GL initialization attempts failed" << std::endl;
}

void Renderer::updateRenderArea() {
    if (!app_->window) {
        return;
    }

    if (app_->window != lastWindow_) {
        aout << "Renderer: window changed, recreating EGL surface" << std::endl;
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
        }
        ANativeWindow_setBuffersGeometry(app_->window, 0, 0, nativeFormat_);
        surface_ = eglCreateWindowSurface(display_, config_, app_->window, nullptr);
        if (surface_ == EGL_NO_SURFACE) {
            aout << "Renderer: failed to recreate EGL surface for new window" << std::endl;
            return;
        }
        auto madeCurrent = eglMakeCurrent(display_, surface_, surface_, context_);
        if (madeCurrent != EGL_TRUE) {
            aout << "Renderer: eglMakeCurrent failed after window change" << std::endl;
            return;
        }
        lastWindow_ = app_->window;
        width_ = -1;
        height_ = -1;
        shaderNeedsNewProjectionMatrix_ = true;
    }

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
            if (surface_ == EGL_NO_SURFACE) {
                aout << "Renderer: failed to recreate EGL surface on resize" << std::endl;
                return;
            }
            if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
                aout << "Renderer: eglMakeCurrent failed after resize" << std::endl;
                return;
            }
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
