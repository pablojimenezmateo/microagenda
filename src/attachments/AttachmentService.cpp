#include "attachments/AttachmentService.h"

#include "platform/PathUtils.h"
#include "platform/DefaultOpener.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace microagenda::attachments {
namespace {

static std::filesystem::path uniqueManagedPath(const std::filesystem::path& desired) {
  if(!std::filesystem::exists(desired)) return desired;
  const auto parent = desired.parent_path();
  const auto stem = desired.stem().string();
  const auto ext = desired.extension().string();
  int suffix = 2;
  while(true) {
    auto candidate = parent / (stem + "-" + std::to_string(suffix++) + ext);
    if(!std::filesystem::exists(candidate)) return candidate;
  }
}

static std::string markdownFor(const std::filesystem::path& libraryRoot, const std::filesystem::path& managed, bool image) {
  const auto relative = managed.lexically_relative(libraryRoot).generic_string();
  const auto label = managed.filename().generic_string();
  return std::string(image ? "![" : "[") + label + "](" + relative + ")";
}

}

AttachmentLink AttachmentService::makeLink(const std::filesystem::path& libraryRoot, const std::string& noteId, const std::filesystem::path& fileName) const {
  const auto desired = libraryRoot / ".microagenda" / "attachments" / noteId / fileName.filename();
  const auto managed = uniqueManagedPath(desired);
  platform::normalizeInsideRoot(libraryRoot, managed);
  const bool image = isSupportedImage(managed);
  return {managed, markdownFor(libraryRoot, managed, image), image};
}

AttachmentLink AttachmentService::attachFile(const std::filesystem::path& libraryRoot, const std::string& noteId, const std::filesystem::path& source) const {
  if(!std::filesystem::is_regular_file(source)) throw std::runtime_error("attachment source is not a regular file");
  auto link = makeLink(libraryRoot, noteId, source.filename());
  std::filesystem::create_directories(link.managedPath.parent_path());
  std::filesystem::copy_file(source, link.managedPath, std::filesystem::copy_options::overwrite_existing);
  return link;
}

AttachmentLink AttachmentService::attachBytes(const std::filesystem::path& libraryRoot, const std::string& noteId, const std::filesystem::path& fileName, const void* data, std::size_t size) const {
  if(!data || size == 0) throw std::runtime_error("attachment data is empty");
  auto link = makeLink(libraryRoot, noteId, fileName);
  std::filesystem::create_directories(link.managedPath.parent_path());
  std::ofstream out(link.managedPath, std::ios::binary);
  if(!out) throw std::runtime_error("failed to create managed attachment");
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  if(!out) throw std::runtime_error("failed to write managed attachment");
  return link;
}

std::filesystem::path AttachmentService::resolveManaged(const std::filesystem::path& libraryRoot, const std::filesystem::path& relative) const {
  return platform::normalizeInsideRoot(libraryRoot, libraryRoot / relative);
}

std::vector<std::string> AttachmentService::openCommand(const std::filesystem::path& libraryRoot, const std::filesystem::path& relative) const {
  return platform::defaultOpenCommand(resolveManaged(libraryRoot, relative));
}

bool AttachmentService::isSupportedImage(const std::filesystem::path& path) const {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif" || ext == ".webp";
}

}
