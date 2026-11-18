#include <SDL3/SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <filesystem>
#include <string>
#include "../src/mobile_platform.h"

namespace seedlens {
void mobile_set_notice(std::string);

void mobile_initialize(SDL_Window* window) {
    (void)window;
    // ブラウザのリサイズ時にSDLウィンドウサイズを同期
    EM_ASM({
        const resizeCanvas = () => {
            const canvas = document.getElementById('canvas');
            if (canvas) {
                canvas.width = window.innerWidth * window.devicePixelRatio;
                canvas.height = window.innerHeight * window.devicePixelRatio;
            }
        };
        window.addEventListener('resize', resizeCanvas);
        resizeCanvas();
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
            console.error("Export failed:", e);
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
