#include "diagnostics/StructurePreviewTrace.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include "fmt/chrono.h"
#include <fstream>
#include <mutex>
namespace lholo::diagnostics {
void traceStructurePreview(std::string_view message) noexcept {
    try {
        static std::mutex mutex;
        std::lock_guard lock(mutex);
        auto const directory = LHolo::getInstance().getSelf().getModDir() / "logs";
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / "structure-preview.log",std::ios::binary | std::ios::app);
        output << fmt::format("[{:%Y-%m-%d %H:%M:%S}] {}\n",fmt::localtime(std::time(nullptr)),message);
        output.flush();
    } catch (...) { /* Diagnostics must not replace the original failure. */ }
}
}
