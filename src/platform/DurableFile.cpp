#include "platform/DurableFile.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <string>
#include <system_error>

namespace microagenda::platform {
namespace {

static bool fsyncDirectory(const std::filesystem::path& dir) {
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if(fd < 0) return false;
  const bool ok = ::fsync(fd) == 0;
  ::close(fd);
  return ok;
}

static bool writeAll(int fd, std::string_view contents) {
  const char* data = contents.data();
  std::size_t remaining = contents.size();
  while(remaining > 0) {
    const ssize_t written = ::write(fd, data, remaining);
    if(written < 0) {
      if(errno == EINTR) continue;
      return false;
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
  return true;
}

}

bool writeFileDurably(const std::filesystem::path& path, std::string_view contents) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if(error) return false;

  const auto temp = path.parent_path() / (path.filename().string() + ".tmp-" + std::to_string(::getpid()));
  const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if(fd < 0) return false;

  bool ok = writeAll(fd, contents) && ::fsync(fd) == 0;
  if(::close(fd) != 0) ok = false;
  if(!ok) {
    std::filesystem::remove(temp, error);
    return false;
  }

  std::filesystem::rename(temp, path, error);
  if(error) {
    std::filesystem::remove(temp, error);
    return false;
  }
  return fsyncDirectory(path.parent_path());
}

bool removeFileDurably(const std::filesystem::path& path) {
  if(!std::filesystem::exists(path)) return true;
  std::error_code error;
  std::filesystem::remove(path, error);
  if(error) return false;
  return fsyncDirectory(path.parent_path());
}

}
