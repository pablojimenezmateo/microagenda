#pragma once

#include <filesystem>
#include <string>

namespace microagenda::platform {

struct RuntimePaths {
  std::filesystem::path configDir;
  std::filesystem::path cacheDir;
  std::filesystem::path dataDir;
};

RuntimePaths resolveRuntimePaths();
std::filesystem::path normalizeInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate);
std::string sanitizeFileStem(std::string title);

}
