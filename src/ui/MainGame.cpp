#include "stdafx.h"
#include "ui/MainGame.h"

#include <thread>

#if defined(VORB_IMPL_UI_SDL)
#if defined(OS_WINDOWS)
#include <SDL/SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#define MS_TIME (SDL_GetTicks())
#elif defined(VORB_IMPL_UI_GLFW)
#include <GLFW/glfw3.h>
#define MS_TIME ((ui32)(glfwGetTime() * 1000.0))
#elif defined(VORB_IMPL_UI_SFML)
#include <SFML/System/Clock.hpp>
// TODO: This is pretty effing hacky...
static ui32 getCurrentTime() {
    static sf::Clock clock;
    static ui32 lastMS = 0;

    lastMS = (ui32)clock.getElapsedTime().asMilliseconds();
    return lastMS;
}
#define MS_TIME getCurrentTime()
#endif

#include "../ImplGraphicsH.inl"

#include "graphics/GLStates.h"
#include "ui/IGameScreen.h"
#include "ui/InputDispatcher.h"
#include "graphics/GraphicsDevice.h"
#include "ui/ScreenList.h"
#include "utils.h"
#include "Timing.h"
#include "InputDispatcherEventCatcher.h"

vui::MainGame::MainGame() : _fps(0) {
}
vui::MainGame::~MainGame() {
    // Empty
}

bool vui::MainGame::initSystems() {
    // Create The Window
    if (!_window.init()) return false;

    // Initialize input
    vui::InputDispatcher::onQuit += makeDelegate(*this, &MainGame::onQuit);

    // Get The Machine's Graphics Capabilities
    _gDevice = new vg::GraphicsDevice(_window.getHandle());
    _gDevice->refreshInformation();

#if defined(VORB_IMPL_GRAPHICS_OPENGL)
    // TODO: Replace With BlendState
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set A Default OpenGL State
    vg::DepthState::FULL.set();
    vg::RasterizerState::CULL_CLOCKWISE.set();
#elif defined(VORB_IMPL_GRAPHICS_D3D)

#endif

    return true;
}

void vui::MainGame::run() {
#if defined(VORB_IMPL_UI_SDL)
    // Initialize everything except SDL audio and SDL haptic feedback.
    SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK);

    // Make sure we are using hardware acceleration
#if defined(VORB_IMPL_GRAPHICS_OPENGL)
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
#elif defined(VORB_IMPL_GRAPHICS_D3D)
    // Empty
#endif

#elif defined(VORB_IMPL_UI_GLFW)
    glfwInit();
#elif defined(VORB_IMPL_UI_SFML)
    // Nothing to do...
#endif

    // For counting the fps
    FpsCounter fpsCounter;

    // Game Loop
    if (init()) {
        _isRunning = true;
        while (_isRunning) {
            // Start the FPS counter
            fpsCounter.beginFrame();
            // Refresh Time Information
            refreshElapsedTime();

            // Main Game Logic
            checkInput();
            if (!_isRunning) break;
            onUpdateFrame();
#if defined(VORB_IMPL_GRAPHICS_OPENGL)
            onRenderFrame();
#elif defined(VORB_IMPL_GRAPHICS_D3D)
            VG_DX_DEVICE(_window.getContext())->BeginScene();
            onRenderFrame();
            VG_DX_DEVICE(_window.getContext())->EndScene();
#endif
            // Swap buffers
            ui32 curMS = MS_TIME;
            _window.sync(curMS - _lastMS);

            // Get the FPS
            _fps = fpsCounter.endFrame();
        }
    }


#if defined(VORB_IMPL_UI_SDL)
    SDL_Quit();
#elif defined(VORB_IMPL_UI_GLFW)
    glfwTerminate();
#elif defined(VORB_IMPL_UI_SFML)
    // Don't have to do anything
#endif
}
void vui::MainGame::exitGame() {
    if (_screen) {
        _screen->onExit(_lastTime);
    }
    if (_screenList) {
        _screenList->destroy(_lastTime);
    }
    vui::InputDispatcher::onQuit -= makeDelegate(*this, &MainGame::onQuit);
    _window.dispose();
    _isRunning = false;
}

bool vui::MainGame::init() {
    // This Is Vital
    if (!initSystems()) return false;
    _window.setTitle(nullptr);

    // Initialize Logic And Screens
    _screenList = new ScreenList(this);
    onInit();
    addScreens();

    // Try To Get A Screen
    _screen = _screenList->getCurrent();
    if (_screen == nullptr) {
        exitGame();
        return false;
    }

    // Run The First Game Screen
    _screen->setRunning();
    _lastTime = {};
    _curTime = {};
    _screen->onEntry(_lastTime);
    _lastMS = MS_TIME;

    return true;
}
void vui::MainGame::refreshElapsedTime() {
    ui32 ct = MS_TIME;
    f64 et = (ct - _lastMS) / 1000.0;
    _lastMS = ct;

    _lastTime = _curTime;
    _curTime.elapsed = et;
    _curTime.total += et;
}
void vui::MainGame::checkInput() {
    if (m_signalQuit) {
        m_signalQuit = false;
        exitGame();
    }
}

void vui::MainGame::onUpdateFrame() {
    if (_screen != nullptr) {
        switch (_screen->getState()) {
        case ScreenState::RUNNING:
            _screen->update(_curTime);
            break;
        case ScreenState::CHANGE_NEXT:
            _screen->onExit(_curTime);
            _screen = _screenList->moveNext();
            if (_screen != nullptr) {
                _screen->setRunning();
                _screen->onEntry(_curTime);
            }
            break;
        case ScreenState::CHANGE_PREVIOUS:
            _screen->onExit(_curTime);
            _screen = _screenList->movePrevious();
            if (_screen != nullptr) {
                _screen->setRunning();
                _screen->onEntry(_curTime);
            }
            break;
        case ScreenState::EXIT_APPLICATION:
            exitGame();
            return;
        default:
            return;
        }
    } else {
        exitGame();
        return;
    }
}

void vui::MainGame::onRenderFrame() {
#if defined(VORB_IMPL_GRAPHICS_OPENGL)
    // TODO: Investigate Removing This
    glViewport(0, 0, _window.getWidth(), _window.getHeight());
#elif defined(VORB_IMPL_GRAPHICS_D3D)
    {
#if defined(VORB_DX_9)
        D3DVIEWPORT9 vp;
        vp.X = 0;
        vp.Y = 0;
        vp.Width = _window.getWidth();
        vp.Height = _window.getHeight();
        vp.MinZ = 0.0f;
        vp.MaxZ = 1.0f;
        VG_DX_DEVICE(_window.getContext())->SetViewport(&vp);
#endif
    }
#endif
    if (_screen != nullptr && _screen->getState() == ScreenState::RUNNING) {
        _screen->draw(_curTime);
    }
}

void vui::MainGame::onQuit(Sender) {
    m_signalQuit = true;
}
