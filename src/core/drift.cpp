#include "drift/drift.h"
#include "drift/input.h"

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// Internal global state kept in an anonymous namespace so it has internal
// linkage and cannot collide with anything else.
// ---------------------------------------------------------------------------
namespace {

struct EngineState {
    SDL_Window    *window     = nullptr;
    SDL_GPUDevice *gpu_device = nullptr;

    bool     running     = false;
    bool     initialised = false;

    // Frame timing ----------------------------------------------------------
    uint64_t last_counter   = 0;
    uint64_t perf_frequency = 0;
    float    delta_time     = 0.0f;
    float    fps            = 0.0f;
    float    smoothed_fps   = 0.0f;
    uint64_t frame_count    = 0;
    uint64_t start_counter  = 0;
};

EngineState g_state;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

DriftResult drift_init(const DriftConfig *config)
{
    if (!config) {
        drift_log(DRIFT_LOG_ERROR, "drift_init: config is NULL");
        return DRIFT_ERROR_INVALID_PARAM;
    }

    if (g_state.initialised) {
        drift_log(DRIFT_LOG_WARN, "drift_init: already initialised");
        return DRIFT_OK;
    }

    // Initialise SDL subsystems ---------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        drift_log(DRIFT_LOG_ERROR, "SDL_Init failed: %s", SDL_GetError());
        return DRIFT_ERROR_INIT_FAILED;
    }

    // Create window ---------------------------------------------------------
    const char *title = config->title ? config->title : "Drift Engine";
    int w = config->width  > 0 ? config->width  : 1280;
    int h = config->height > 0 ? config->height : 720;

    SDL_WindowFlags window_flags = 0;
    if (config->fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (config->resizable) {
        window_flags |= SDL_WINDOW_RESIZABLE;
    }

    g_state.window = SDL_CreateWindow(title, w, h, window_flags);
    if (!g_state.window) {
        drift_log(DRIFT_LOG_ERROR, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return DRIFT_ERROR_INIT_FAILED;
    }

    // Create GPU device -----------------------------------------------------
    g_state.gpu_device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
        true,   // debug_mode
        nullptr // preferred driver
    );

    if (!g_state.gpu_device) {
        drift_log(DRIFT_LOG_ERROR, "SDL_CreateGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyWindow(g_state.window);
        g_state.window = nullptr;
        SDL_Quit();
        return DRIFT_ERROR_GPU;
    }

    // Claim the window for the GPU device so we can present to it later -----
    if (!SDL_ClaimWindowForGPUDevice(g_state.gpu_device, g_state.window)) {
        drift_log(DRIFT_LOG_ERROR, "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(g_state.gpu_device);
        g_state.gpu_device = nullptr;
        SDL_DestroyWindow(g_state.window);
        g_state.window = nullptr;
        SDL_Quit();
        return DRIFT_ERROR_GPU;
    }

    // Vsync -----------------------------------------------------------------
    SDL_SetGPUSwapchainParameters(
        g_state.gpu_device,
        g_state.window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        config->vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE
    );

    // Timing state ----------------------------------------------------------
    g_state.perf_frequency = SDL_GetPerformanceFrequency();
    g_state.last_counter   = SDL_GetPerformanceCounter();
    g_state.start_counter  = g_state.last_counter;
    g_state.delta_time     = 0.0f;
    g_state.fps            = 0.0f;
    g_state.smoothed_fps   = 0.0f;
    g_state.frame_count    = 0;

    g_state.running     = false;
    g_state.initialised = true;

    drift_log(DRIFT_LOG_INFO, "Drift engine initialised (%dx%d)", w, h);
    return DRIFT_OK;
}

void drift_shutdown(void)
{
    if (!g_state.initialised) {
        return;
    }

    if (g_state.gpu_device) {
        // Release the window claim before destroying the device
        SDL_ReleaseWindowFromGPUDevice(g_state.gpu_device, g_state.window);
        SDL_DestroyGPUDevice(g_state.gpu_device);
        g_state.gpu_device = nullptr;
    }

    if (g_state.window) {
        SDL_DestroyWindow(g_state.window);
        g_state.window = nullptr;
    }

    SDL_Quit();

    g_state.initialised = false;
    g_state.running     = false;

    drift_log(DRIFT_LOG_INFO, "Drift engine shut down");
}

DriftResult drift_run(DriftUpdateFn update_fn, void *user_data)
{
    if (!g_state.initialised) {
        drift_log(DRIFT_LOG_ERROR, "drift_run: engine not initialised");
        return DRIFT_ERROR_INIT_FAILED;
    }

    g_state.running      = true;
    g_state.last_counter = SDL_GetPerformanceCounter();

    while (g_state.running) {
        // --- Input: snapshot previous frame state --------------------------
        drift_input_update();

        // --- Poll events ---------------------------------------------------
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Forward to input system first.
            drift_input_process_event(&event);

            switch (event.type) {
            case SDL_EVENT_QUIT:
                g_state.running = false;
                break;
            default:
                break;
            }
        }

        if (!g_state.running) {
            break;
        }

        // --- Delta time ----------------------------------------------------
        uint64_t now = SDL_GetPerformanceCounter();
        uint64_t elapsed = now - g_state.last_counter;
        g_state.last_counter = now;

        g_state.delta_time = static_cast<float>(
            static_cast<double>(elapsed) / static_cast<double>(g_state.perf_frequency)
        );

        // Clamp to avoid spiral-of-death after a breakpoint / hitch
        if (g_state.delta_time > 0.25f) {
            g_state.delta_time = 0.25f;
        }

        // Smoothed FPS (exponential moving average)
        float instant_fps = (g_state.delta_time > 0.0f) ? (1.0f / g_state.delta_time) : 0.0f;
        const float smoothing = 0.9f;
        g_state.smoothed_fps = (g_state.smoothed_fps > 0.0f)
            ? g_state.smoothed_fps * smoothing + instant_fps * (1.0f - smoothing)
            : instant_fps;
        g_state.fps = g_state.smoothed_fps;

        // --- User update ---------------------------------------------------
        if (update_fn) {
            update_fn(g_state.delta_time, user_data);
        }

        g_state.frame_count++;
    }

    return DRIFT_OK;
}

// ---------------------------------------------------------------------------
// Timing queries
// ---------------------------------------------------------------------------

float drift_get_delta_time(void)
{
    return g_state.delta_time;
}

float drift_get_fps(void)
{
    return g_state.fps;
}

uint64_t drift_get_frame_count(void)
{
    return g_state.frame_count;
}

double drift_get_time(void)
{
    if (!g_state.initialised || g_state.perf_frequency == 0) {
        return 0.0;
    }
    uint64_t now = SDL_GetPerformanceCounter();
    return static_cast<double>(now - g_state.start_counter) /
           static_cast<double>(g_state.perf_frequency);
}

// ---------------------------------------------------------------------------
// Window helpers
// ---------------------------------------------------------------------------

void drift_get_window_size(int32_t *w, int32_t *h)
{
    if (g_state.window) {
        int ww = 0, hh = 0;
        SDL_GetWindowSize(g_state.window, &ww, &hh);
        if (w) *w = static_cast<int32_t>(ww);
        if (h) *h = static_cast<int32_t>(hh);
    } else {
        if (w) *w = 0;
        if (h) *h = 0;
    }
}

void drift_set_window_title(const char *title)
{
    if (g_state.window && title) {
        SDL_SetWindowTitle(g_state.window, title);
    }
}

// ---------------------------------------------------------------------------
// Quit control
// ---------------------------------------------------------------------------

bool drift_should_quit(void)
{
    return !g_state.running;
}

void drift_request_quit(void)
{
    g_state.running = false;
}

// ---------------------------------------------------------------------------
// Internal getters for other modules (renderer, UI, etc.)
// ---------------------------------------------------------------------------

extern "C" SDL_Window* drift_internal_get_window(void)
{
    return g_state.window;
}

extern "C" SDL_GPUDevice* drift_internal_get_gpu_device(void)
{
    return g_state.gpu_device;
}
