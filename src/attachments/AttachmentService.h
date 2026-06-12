#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace microagenda::attachments {

struct AttachmentLink {
  std::filesystem::path managedPath;
  std::string markdown;
  bool image = false;
};

class AttachmentService {
public:
  AttachmentLink makeLink(const std::filesystem::path& libraryRoot, const std::string& noteId, const std::filesystem::path& fileName) const;
  AttachmentLink attachFile(const std::filesystem::path& libraryRoot, const std::string& noteId, const std::filesystem::path& source) const;
  AttachmentLink attachBytes(const std::filesystem::path& libraryRoot, const std::string& noteId, const std::filesystem::path& fileName, const void* data, std::size_t size) const;
  std::filesystem::path resolveManaged(const std::filesystem::path& libraryRoot, const std::filesystem::path& relative) const;
  std::vector<std::string> openCommand(const std::filesystem::path& libraryRoot, const std::filesystem::path& relative) const;
  bool isSupportedImage(const std::filesystem::path& path) const;
};

}
