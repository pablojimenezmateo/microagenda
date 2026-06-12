#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace microagenda::platform {

std::vector<std::string> defaultOpenCommand(const std::filesystem::path& path);

}
