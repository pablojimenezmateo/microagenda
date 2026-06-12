#include "platform/PathUtils.h"

#include <cstdlib>
#include <stdexcept>

namespace microagenda::platform {

static std::filesystem::path homeDir() {
  if(const char* home = std::getenv("HOME"); home && *home) return home;
  return std::filesystem::current_path();
}

static std::filesystem::path xdgDir(const char* envName, const char* fallback) {
  if(const char* value = std::getenv(envName); value && *value) return value;
  return homeDir() / fallback;
}

RuntimePaths resolveRuntimePaths() {
  return {
    xdgDir("XDG_CONFIG_HOME", ".config") / "microagenda",
    xdgDir("XDG_CACHE_HOME", ".cache") / "microagenda",
    xdgDir("XDG_DATA_HOME", ".local/share") / "microagenda",
  };
}

std::filesystem::path normalizeInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate) {
  const auto rootAbs = std::filesystem::weakly_canonical(root);
  const auto candidateAbs = std::filesystem::weakly_canonical(candidate);
  auto rootIt = rootAbs.begin();
  auto candidateIt = candidateAbs.begin();
  for(; rootIt != rootAbs.end(); ++rootIt, ++candidateIt) {
    if(candidateIt == candidateAbs.end() || *rootIt != *candidateIt) {
      throw std::runtime_error("path escapes library root");
    }
  }
  return candidateAbs;
}

std::string sanitizeFileStem(std::string title) {
  std::string out;
  out.reserve(title.size());
  bool lastDash = false;
  for(char ch : title) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    if(ok) {
      out.push_back(ch);
      lastDash = false;
    } else if(!lastDash) {
      out.push_back('-');
      lastDash = true;
    }
  }
  while(!out.empty() && out.front() == '-') out.erase(out.begin());
  while(!out.empty() && out.back() == '-') out.pop_back();
  return out.empty() ? "untitled" : out;
}

}
