#include "TestSupport.h"

#include "platform/PathUtils.h"

#include <filesystem>

MICROAGENDA_TEST(path_utils_sanitizes_file_stem) {
  MICROAGENDA_REQUIRE(microagenda::platform::sanitizeFileStem("Fast Notes!") == "Fast-Notes");
  MICROAGENDA_REQUIRE(microagenda::platform::sanitizeFileStem("...") == "untitled");
}

MICROAGENDA_TEST(path_utils_resolves_runtime_paths) {
  const auto paths = microagenda::platform::resolveRuntimePaths();
  MICROAGENDA_REQUIRE(paths.configDir.string().find("microagenda") != std::string::npos);
  MICROAGENDA_REQUIRE(paths.cacheDir.string().find("microagenda") != std::string::npos);
  MICROAGENDA_REQUIRE(paths.dataDir.string().find("microagenda") != std::string::npos);
}
