#include "Utils/Config.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>

// =====================================================================
// OW::Config -- Save / Load stubs
//
// These functions provide the persistence skeleton.  The actual
// serialisation format (INI / JSON / binary) should be chosen to
// match the needs of the project.
//
// The mutex is locked during read/write to prevent race conditions
// from the render and logic threads.
// =====================================================================

namespace OW { namespace Config {

    void SaveConfig(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);

        // --- TODO: implement actual serialisation ---
        // Example INI-style:
        //
        //   std::ofstream ofs(path);
        //   if (!ofs.is_open()) return;
        //   ofs << "[Aimbot]\n";
        //   ofs << "Fov=" << Fov << "\n";
        //   ofs << "Smooth=" << Smooth << "\n";
        //   ofs << "...\n";
        //   ofs.close();

        // For now, just create / touch the file as a placeholder
        std::ofstream ofs(path, std::ios::app);
        if (ofs.is_open())
            ofs.close();
    }

    void LoadConfig(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);

        // --- TODO: implement actual deserialisation ---
        // Example INI-style:
        //
        //   std::ifstream ifs(path);
        //   if (!ifs.is_open()) return;
        //   std::string line;
        //   while (std::getline(ifs, line)) {
        //       // parse key=value ...
        //   }

        // Placeholder -- nothing loaded
        (void)path;
    }

}} // namespace OW::Config
