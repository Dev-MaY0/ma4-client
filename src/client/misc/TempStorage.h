#pragma once

#include <filesystem>

namespace Ma4Temp {
    std::filesystem::path resolvePath(std::filesystem::path const& relative);
    void cleanup();
}
