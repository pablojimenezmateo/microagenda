#include "platform/DefaultOpener.h"

namespace microagenda::platform {

std::vector<std::string> defaultOpenCommand(const std::filesystem::path& path) {
  return {"xdg-open", path.string()};
}

}
