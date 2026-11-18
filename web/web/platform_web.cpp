#include <SDL3/SDL.h>
#include <emscripten.h>
#include <filesystem>
#include <string>
#include "../src/mobile_platform.h"

namespace seedlens {
void mobile_set_notice(std::string);

static SDL_Window* g_web_window = nullptr;

extern "C" EMSCRIPTEN_KEEPALIVE void seedlens_on_web_resize(int width, int height) {
    if (g_web_window && width > 0 && height > 0) {
        SDL_SetWindowSize(g_web_window, width, height);
    }
}

void mobile_initialize(SDL_Window* window) {
    g_web_window = window;
    EM_ASM({
        const syncSize = () => {
            const w = window.innerWidth;
            const h = window.innerHeight;
            const canvas = document.getElementById('canvas');
            if (canvas) {
                canvas.style.width = w + 'px';
                canvas.style.height = h + 'px';
            }
            if (typeof _seedlens_on_web_resize === 'function') {
                _seedlens_on_web_resize(w, h);
            }
        };
        window.addEventListener('resize', syncSize);
        syncSize();
    });
}

void mobile_export_file(const std::filesystem::path& path, const char* mime) {
    std::string filepath = path.string();
    std::string mimetype = (mime && *mime) ? mime : "application/octet-stream";

    bool ok = EM_ASM_INT({
        try {
            const path = UTF8ToString($0);
            const mime = UTF8ToString($1);
            const data = FS.readFile(path);
            const blob = new Blob([data], { type: mime });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = path.split('/').pop();
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            setTimeout(() => URL.revokeObjectURL(url), 1000);
            return 1;
        } catch (e) {
            console.error("SeedLens export error:", e);
            return 0;
        }
    }, filepath.c_str(), mimetype.c_str()) != 0;

    if (ok) {
        mobile_set_notice("ダウンロードしました / Downloaded: " + path.filename().string());
    } else {
        mobile_set_notice("保存できませんでした / Could not save");
    }
}
} // namespace seedlens
