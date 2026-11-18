#include "ui.h"
#include "export.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <memory>
#include <vector>
#include <cstdio>

// platform_web.cpp 側で実装するファクトリ関数
namespace seedlens {
Platform make_web_platform();
}

namespace {
SDL_Window* g_window = nullptr;
SDL_GLContext g_gl_context = nullptr;
std::unique_ptr<seedlens::App> g_app;
std::filesystem::path g_settings_path = "/settings.json";

// WebGLのフレームバッファから指定範囲を切り出してPNGダウンロードする
bool capture_map_web(const std::filesystem::path& path, seedlens::PixelRect crop, std::string& error) {
    int win_w = 0, win_h = 0;
    SDL_GL_GetDrawableSize(g_window, &win_w, &win_h);

    crop.left = std::clamp(crop.left, 0, win_w - 1);
    crop.top = std::clamp(crop.top, 0, win_h - 1);
    crop.right = std::clamp(crop.right, crop.left + 1, win_w);
    crop.bottom = std::clamp(crop.bottom, crop.top + 1, win_h);

    int w = crop.right - crop.left;
    int h = crop.bottom - crop.top;
    std::vector<uint32_t> pixels(size_t(w) * h);
    std::vector<uint32_t> row_buf(w);

    // OpenGLは左下原点のため、Y座標を反転して読み取る
    int gl_y = win_h - crop.bottom;
    glReadPixels(crop.left, gl_y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // 上下反転
    for (int y = 0; y < h / 2; ++y) {
        uint32_t* top_row = pixels.data() + size_t(y) * w;
        uint32_t* bot_row = pixels.data() + size_t(h - 1 - y) * w;
        memcpy(row_buf.data(), top_row, size_t(w) * 4);
        memcpy(top_row, bot_row, size_t(w) * 4);
        memcpy(bot_row, row_buf.data(), size_t(w) * 4);
    }

    if (!seedlens::write_png(path, w, h, pixels.data(), error))
        return false;

    // ブラウザへファイルをダウンロードさせるJS呼び出し
    EM_ASM({
        const filename = UTF8ToString($0);
        const data = FS.readFile(filename);
        const blob = new Blob([data], { type: 'image/png' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename.split('/').pop();
        a.click();
        URL.revokeObjectURL(url);
    }, path.c_str());

    return true;
}

void main_loop() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }

    // ブラウザのCanvasサイズに合わせてウィンドウをリサイズ
    double css_w = 0, css_h = 0;
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);
    double dpr = emscripten_get_device_pixel_ratio();
    int target_w = int(css_w);
    int target_h = int(css_h);
    int cur_w = 0, cur_h = 0;
    SDL_GetWindowSize(g_window, &cur_w, &cur_h);
    if (target_w > 0 && target_h > 0 && (target_w != cur_w || target_h != cur_h)) {
        SDL_SetWindowSize(g_window, target_w, target_h);
    }

    float dpi = float(dpr);
    ImGui::GetStyle().FontScaleDpi = dpi;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    g_app->frame(dpi);

    ImGui::Render();
    int fb_w = 0, fb_h = 0;
    SDL_GL_GetDrawableSize(g_window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.96f, 0.97f, 0.98f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (auto export_path = g_app->take_export()) {
        std::string error;
        if (capture_map_web(*export_path, g_app->export_rect(), error)) {
            g_app->notification("保存しました / Saved: " + export_path->filename().string());
        } else {
            g_app->notification(error.empty() ? "PNG export failed." : error);
        }
    }

    SDL_GL_SwapWindow(g_window);
}
} // namespace

int main(int, char**) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    g_window = SDL_CreateWindow("SeedLens", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    g_gl_context = SDL_GL_CreateContext(g_window);
    SDL_GL_MakeCurrent(g_window, g_gl_context);
    SDL_GL_SetSwapInterval(1);

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // assets/ 内の日本語フォントを読み込む（※ファイル名は assets/ 内の実ファイル名に合わせてください）
    ImFontConfig cfg;
    cfg.SizePixels = 15.0f;
    ImFont* regular = io.Fonts->AddFontFromFileTTF("/assets/NotoSansJP-Regular.ttf", 15.0f, &cfg,
                                                   io.Fonts->GetGlyphRangesJapanese());
    if (regular) {
        io.FontDefault = regular;
    }

    ImGui_ImplSDL2_InitForOpenGL(g_window, g_gl_context);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    seedlens::Session session;
    seedlens::load_session(g_settings_path, session);
    g_app = std::make_unique<seedlens::App>(seedlens::make_web_platform(), std::move(session), g_settings_path);

    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}
