// Motion blur implementation based on code by CrackedMatter
// Original source: https://github.com/CrackedMatter/mcpelauncher-motion-blur
// GitHub: https://github.com/CrackedMatter

#include "motionblur.hpp"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cmath>


static constexpr const char* vertexShaderSource =  R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
}
)";

static constexpr const char* fragmentShaderSource =  R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uCurrentFrame;
uniform sampler2D uPreviousFrame;
uniform float uBlendFactor;
uniform float uOpacity;
void main() {
    vec4 currentColor  = texture2D(uCurrentFrame, vTexCoord);
    vec4 previousColor = texture2D(uPreviousFrame, vTexCoord);
    float adaptiveFactor = uBlendFactor * (1.0 - 0.5 * exp2(-10.0 * distance(currentColor, previousColor)));
    vec4 blurred = mix(currentColor, previousColor, adaptiveFactor);
    gl_FragColor = mix(currentColor, blurred, uOpacity);
}
)";

// Internal render scale for the blur pass. 1.0 = full resolution (expensive,
// causes the GPU stall/stutter). Lowering this cuts the cost of the
// glCopyTexSubImage2D call (and the shader pass) roughly quadratically,
// since it copies/samples far fewer pixels, while the final quad is still
// drawn stretched across the whole screen so it looks the same visually.
static constexpr float kRenderScale = 0.5f;


MotionBlurModule::MotionBlurModule()
    : Module("Motion Blur", "Adds a smooth motion blur effect when rotating the camera.") {}

void MotionBlurModule::onInit() {
}

void MotionBlurModule::onEnable() { m_hasPreviousFrame = false; }

void MotionBlurModule::onDisable() { m_hasPreviousFrame = false; }

void MotionBlurModule::initGL() {
    if (m_glInitialized) return;

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vertexShader);
    glAttachShader(m_shaderProgram, fragmentShader);
    glLinkProgram(m_shaderProgram);

    m_positionLocation      = glGetAttribLocation(m_shaderProgram, "aPosition");
    m_texCoordLocation      = glGetAttribLocation(m_shaderProgram, "aTexCoord");
    m_currentFrameLocation  = glGetUniformLocation(m_shaderProgram, "uCurrentFrame");
    m_previousFrameLocation = glGetUniformLocation(m_shaderProgram, "uPreviousFrame");
    m_blendFactorLocation   = glGetUniformLocation(m_shaderProgram, "uBlendFactor");
    m_opacityLocation       = glGetUniformLocation(m_shaderProgram, "uOpacity");

    glGenTextures(1, &m_currentFrameTexture);
    glBindTexture(GL_TEXTURE_2D, m_currentFrameTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(1, &m_previousFrameTexture);
    glBindTexture(GL_TEXTURE_2D, m_previousFrameTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLfloat vertices[] = {
        -1.0f, 1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
        1.0f,  -1.0f, 1.0f, 0.0f,
        1.0f,  1.0f,  1.0f, 1.0f,
    };

    GLushort indices[] = {
        0, 1, 2,
        0, 2, 3,
    };

    glGenBuffers(1, &m_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &m_indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    m_glInitialized = true;
}

void MotionBlurModule::allocateTextures(int w, int h) {
    if (w == m_texWidth && h == m_texHeight) return;
    m_texWidth = w;
    m_texHeight = h;
    m_hasPreviousFrame = false;

    glBindTexture(GL_TEXTURE_2D, m_currentFrameTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, m_previousFrameTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

void MotionBlurModule::onFrame() {
    initGL();
    if (!m_glInitialized) return;

    EGLDisplay display = eglGetCurrentDisplay();
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) return;

    EGLint fullWidth, fullHeight;
    eglQuerySurface(display, surface, EGL_WIDTH, &fullWidth);
    eglQuerySurface(display, surface, EGL_HEIGHT, &fullHeight);
    if (fullWidth <= 0 || fullHeight <= 0) return;

    // Render/copy at a reduced internal resolution to cut the cost of the
    // per-frame framebuffer copy - this is the main stutter fix.
    int width  = static_cast<int>(fullWidth  * kRenderScale);
    int height = static_cast<int>(fullHeight * kRenderScale);
    if (width < 1) width
