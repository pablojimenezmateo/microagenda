#include "TestSupport.h"

#include "attachments/AttachmentService.h"

#include <filesystem>
#include <fstream>

MICROAGENDA_TEST(attachment_service_detects_supported_images) {
  microagenda::attachments::AttachmentService service;
  MICROAGENDA_REQUIRE(service.isSupportedImage("photo.PNG"));
  MICROAGENDA_REQUIRE(!service.isSupportedImage("document.pdf"));
}

MICROAGENDA_TEST(attachment_service_copies_and_links_files) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-attachment-test";
  const auto sourceDir = std::filesystem::temp_directory_path() / "microagenda-attachment-source";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(sourceDir);
  std::filesystem::create_directories(sourceDir);
  const auto source = sourceDir / "image.png";
  {
    std::ofstream out(source);
    out << "png";
  }
  microagenda::attachments::AttachmentService service;
  const auto link = service.attachFile(root, "note-1", source);
  MICROAGENDA_REQUIRE(link.image);
  MICROAGENDA_REQUIRE(link.markdown.find("![image.png](") == 0);
  MICROAGENDA_REQUIRE(std::filesystem::exists(link.managedPath));
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(sourceDir);
}

MICROAGENDA_TEST(attachment_service_labels_non_image_links_with_file_name) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-attachment-label-test";
  const auto sourceDir = std::filesystem::temp_directory_path() / "microagenda-attachment-label-source";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(sourceDir);
  std::filesystem::create_directories(sourceDir);
  const auto source = sourceDir / "From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf";
  {
    std::ofstream out(source);
    out << "pdf";
  }
  microagenda::attachments::AttachmentService service;
  const auto link = service.attachFile(root, "note-1", source);
  MICROAGENDA_REQUIRE(!link.image);
  MICROAGENDA_REQUIRE(link.markdown.find("[From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf](") == 0);
  MICROAGENDA_REQUIRE(std::filesystem::exists(link.managedPath));
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(sourceDir);
}

MICROAGENDA_TEST(attachment_service_writes_clipboard_bytes_with_unique_names) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-attach-bytes-test";
  std::filesystem::remove_all(root);
  const char first[] = "first";
  const char second[] = "second";

  microagenda::attachments::AttachmentService service;
  const auto one = service.attachBytes(root, "note-1", "clipboard.png", first, sizeof(first) - 1);
  const auto two = service.attachBytes(root, "note-1", "clipboard.png", second, sizeof(second) - 1);

  MICROAGENDA_REQUIRE(one.image);
  MICROAGENDA_REQUIRE(two.image);
  MICROAGENDA_REQUIRE(one.managedPath != two.managedPath);
  MICROAGENDA_REQUIRE(std::filesystem::exists(one.managedPath));
  MICROAGENDA_REQUIRE(std::filesystem::exists(two.managedPath));
  MICROAGENDA_REQUIRE(two.markdown.find("clipboard-2.png") != std::string::npos);
  std::filesystem::remove_all(root);
}

MICROAGENDA_TEST(attachment_service_builds_default_open_command) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-open-test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / ".microagenda" / "attachments" / "note");
  const auto file = root / ".microagenda" / "attachments" / "note" / "doc.pdf";
  {
    std::ofstream out(file);
    out << "pdf";
  }
  microagenda::attachments::AttachmentService service;
  const auto command = service.openCommand(root, ".microagenda/attachments/note/doc.pdf");
  MICROAGENDA_REQUIRE(command.size() == 2);
  MICROAGENDA_REQUIRE(command[0] == "xdg-open");
  std::filesystem::remove_all(root);
}

MICROAGENDA_TEST(attachment_service_rejects_path_traversal) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-attachment-boundary";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  microagenda::attachments::AttachmentService service;
  bool rejected = false;
  try {
    (void)service.resolveManaged(root, "../outside.pdf");
  } catch(...) {
    rejected = true;
  }
  MICROAGENDA_REQUIRE(rejected);
  std::filesystem::remove_all(root);
}
