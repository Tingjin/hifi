//
//  main.cpp
//  tests/render-utils/src
//
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "TextRenderer.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif

#include <mutex>

#include <QWindow>
#include <QtGlobal>
#include <QFile>
#include <QTime>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QResizeEvent>
#include <QLoggingCategory>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QApplication>
#include <QOpenGLDebugLogger>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>
#include <PathUtils.h>
#include <QDir>


#include "gpu/Batch.h"
#include "gpu/Context.h"

#include "../model/Skybox_vert.h"
#include "..//model/Skybox_frag.h"

#include "simple_vert.h"
#include "simple_frag.h"
#include "simple_textured_frag.h"

#include "deferred_light_vert.h"
#include "deferred_light_limited_vert.h"

#include "directional_light_frag.h"
#include "directional_light_shadow_map_frag.h"
#include "directional_light_cascaded_shadow_map_frag.h"

#include "directional_ambient_light_frag.h"
#include "directional_ambient_light_shadow_map_frag.h"
#include "directional_ambient_light_cascaded_shadow_map_frag.h"

#include "directional_skybox_light_frag.h"
#include "directional_skybox_light_shadow_map_frag.h"
#include "directional_skybox_light_cascaded_shadow_map_frag.h"

#include "point_light_frag.h"
#include "spot_light_frag.h"


class RateCounter {
    std::vector<float> times;
    QElapsedTimer timer;
public:
    RateCounter() {
        timer.start();
    }

    void reset() {
        times.clear();
    }

    unsigned int count() const {
        return times.size() - 1;
    }

    float elapsed() const {
        if (times.size() < 1) {
            return 0.0f;
        }
        float elapsed = *times.rbegin() - *times.begin();
        return elapsed;
    }

    void increment() {
        times.push_back(timer.elapsed() / 1000.0f);
    }

    float rate() const {
        if (elapsed() == 0.0f) {
            return NAN;
        }
        return (float) count() / elapsed();
    }
};


const QString& getQmlDir() {
    static QString dir;
    if (dir.isEmpty()) {
        QDir path(__FILE__);
        path.cdUp();
        dir = path.cleanPath(path.absoluteFilePath("../../../interface/resources/qml/")) + "/";
        qDebug() << "Qml Path: " << dir;
    }
    return dir;
}

// Create a simple OpenGL window that renders text in various ways
class QTestWindow : public QWindow {
    Q_OBJECT

    QOpenGLContext* _context{ nullptr };
    QSize _size;
    TextRenderer* _textRenderer[4];
    RateCounter fps;

protected:
    void renderText();

private:
    void resizeWindow(const QSize& size) {
        _size = size;
    }

public:
    QTestWindow() {
        setSurfaceType(QSurface::OpenGLSurface);

        QSurfaceFormat format;
        // Qt Quick may need a depth and stencil buffer. Always make sure these are available.
        format.setDepthBufferSize(16);
        format.setStencilBufferSize(8);
        format.setVersion(4, 1);
        format.setProfile(QSurfaceFormat::OpenGLContextProfile::CoreProfile);
        format.setOption(QSurfaceFormat::DebugContext);

        setFormat(format);

        _context = new QOpenGLContext;
        _context->setFormat(format);
        _context->create();

        show();
        makeCurrent();

#ifdef WIN32
        glewExperimental = true;
        GLenum err = glewInit();
        if (GLEW_OK != err) {
            /* Problem: glewInit failed, something is seriously wrong. */
            const GLubyte * errStr = glewGetErrorString(err);
            qDebug("Error: %s\n", errStr);
        }
        qDebug("Status: Using GLEW %s\n", glewGetString(GLEW_VERSION));

        if (wglewGetExtension("WGL_EXT_swap_control")) {
            int swapInterval = wglGetSwapIntervalEXT();
            qDebug("V-Sync is %s\n", (swapInterval > 0 ? "ON" : "OFF"));
        }
        glGetError();
#endif

        {
            QOpenGLDebugLogger* logger = new QOpenGLDebugLogger(this);
            logger->initialize(); // initializes in the current context, i.e. ctx
            logger->enableMessages();
            connect(logger, &QOpenGLDebugLogger::messageLogged, this, [&](const QOpenGLDebugMessage & debugMessage) {
                qDebug() << debugMessage;
            });
            logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
        }
        qDebug() << (const char*)glGetString(GL_VERSION);


//        _textRenderer[0] = TextRenderer::getInstance(SANS_FONT_FAMILY, 12, false);
//        _textRenderer[1] = TextRenderer::getInstance(SERIF_FONT_FAMILY, 12, false,
//            TextRenderer::SHADOW_EFFECT);
//        _textRenderer[2] = TextRenderer::getInstance(MONO_FONT_FAMILY, 48, -1,
//            false, TextRenderer::OUTLINE_EFFECT);
//        _textRenderer[3] = TextRenderer::getInstance(INCONSOLATA_FONT_FAMILY, 24);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.2f, 0.2f, 0.2f, 1);
        glDisable(GL_DEPTH_TEST);

        makeCurrent();

//        setFramePosition(QPoint(-1000, 0));
        resize(QSize(800, 600));
    }

    virtual ~QTestWindow() {
    }

    void draw();
    void makeCurrent() {
        _context->makeCurrent(this);
    }

protected:

    void resizeEvent(QResizeEvent* ev) override {
        resizeWindow(ev->size());
    }
};

#ifndef SERIF_FONT_FAMILY
#define SERIF_FONT_FAMILY "Times New Roman"
#endif

static const wchar_t* EXAMPLE_TEXT = L"Hello";
//static const wchar_t* EXAMPLE_TEXT = L"\xC1y Hello 1.0\ny\xC1 line 2\n\xC1y";
static const glm::uvec2 QUAD_OFFSET(10, 10);

static const glm::vec3 COLORS[4] = { { 1.0, 1.0, 1.0 }, { 0.5, 1.0, 0.5 }, {
        1.0, 0.5, 0.5 }, { 0.5, 0.5, 1.0 } };

void QTestWindow::renderText() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, _size.width(), _size.height(), 0, 1, -1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    const glm::uvec2 size = glm::uvec2(_size.width() / 2, _size.height() / 2);

    const glm::uvec2 offsets[4] = {
        { QUAD_OFFSET.x, QUAD_OFFSET.y },
        { size.x + QUAD_OFFSET.x, QUAD_OFFSET.y },
        { size.x + QUAD_OFFSET.x, size.y + QUAD_OFFSET.y },
        { QUAD_OFFSET.x, size.y + QUAD_OFFSET.y },
    };	

    QString str = QString::fromWCharArray(EXAMPLE_TEXT);
    for (int i = 0; i < 4; ++i) {
        glm::vec2 bounds = _textRenderer[i]->computeExtent(str);
        glPushMatrix();
        {
            glTranslatef(offsets[i].x, offsets[i].y, 0);
            glColor3f(0, 0, 0);
            glBegin(GL_QUADS);
            {
                glVertex2f(0, 0);
                glVertex2f(0, bounds.y);
                glVertex2f(bounds.x, bounds.y);
                glVertex2f(bounds.x, 0);
            }
            glEnd();
        }
        glPopMatrix();
        const int testCount = 100;
        for (int j = 0; j < testCount; ++j) {
            // Draw backgrounds around where the text will appear
            // Draw the text itself
            _textRenderer[i]->draw(offsets[i].x, offsets[i].y, str.toLocal8Bit().constData(),
                glm::vec4(COLORS[i], 1.0f));
        }
    }
}

void testShaderBuild(const char* vs_src, const char * fs_src) {
    auto vs = gpu::ShaderPointer(gpu::Shader::createVertex(std::string(vs_src)));
    auto fs = gpu::ShaderPointer(gpu::Shader::createPixel(std::string(fs_src)));
    auto pr = gpu::ShaderPointer(gpu::Shader::createProgram(vs, fs));
    gpu::Shader::makeProgram(*pr);
}

void QTestWindow::draw() {
    if (!isVisible()) {
        return;
    }

    makeCurrent();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, _size.width() * devicePixelRatio(), _size.height() * devicePixelRatio());

    static std::once_flag once;
    std::call_once(once, [&]{
        testShaderBuild(Skybox_vert, Skybox_frag);
        testShaderBuild(simple_vert, simple_frag);
        testShaderBuild(simple_vert, simple_textured_frag);
        testShaderBuild(deferred_light_vert, directional_light_frag);
        testShaderBuild(deferred_light_vert, directional_light_shadow_map_frag);
        testShaderBuild(deferred_light_vert, directional_light_cascaded_shadow_map_frag);
        testShaderBuild(deferred_light_vert, directional_ambient_light_frag);
        testShaderBuild(deferred_light_vert, directional_ambient_light_shadow_map_frag);
        testShaderBuild(deferred_light_vert, directional_ambient_light_cascaded_shadow_map_frag);
        testShaderBuild(deferred_light_vert, directional_skybox_light_frag);
        testShaderBuild(deferred_light_vert, directional_skybox_light_shadow_map_frag);
        testShaderBuild(deferred_light_vert, directional_skybox_light_cascaded_shadow_map_frag);
        testShaderBuild(deferred_light_limited_vert, point_light_frag);
        testShaderBuild(deferred_light_limited_vert, spot_light_frag);

   });
    //    renderText();

    _context->swapBuffers(this);
    glFinish();

    fps.increment();
    if (fps.elapsed() >= 2.0f) {
        qDebug() << "FPS: " << fps.rate();
        fps.reset();
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (!message.isEmpty()) {
#ifdef Q_OS_WIN
        OutputDebugStringA(message.toLocal8Bit().constData());
        OutputDebugStringA("\n");
#else 
        std::cout << message.toLocal8Bit().constData() << std::endl;
#endif
    }
}


const char * LOG_FILTER_RULES = R"V0G0N(
hifi.gpu=true
)V0G0N";

int main(int argc, char** argv) {    
    QGuiApplication app(argc, argv);
    qInstallMessageHandler(messageHandler);
    QLoggingCategory::setFilterRules(LOG_FILTER_RULES);
    QTestWindow window;
    QTimer timer;
    timer.setInterval(1);
    app.connect(&timer, &QTimer::timeout, &app, [&] {
        window.draw();
    });
    timer.start();
    app.exec();
    return 0;
}

#include "main.moc"
