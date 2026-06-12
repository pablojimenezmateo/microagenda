#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace microagenda::editor {

class MarkdownEditor {
public:
  void setText(std::string text);
  void insert(std::string_view text);
  void erasePrevious();
  void eraseNext();
  void moveCursor(std::size_t cursor);
  void selectRange(std::size_t anchor, std::size_t cursor);
  void selectAll();
  void clearSelection();
  bool hasSelection() const;
  std::size_t selectionStart() const;
  std::size_t selectionEnd() const;
  std::string selectedText() const;
  void eraseSelection();
  void moveLeft();
  void moveRight();
  void moveLineUp();
  void moveLineDown();
  void moveLineStart(bool keepSelection = false);
  void moveLineEnd(bool keepSelection = false);
  bool undo();
  bool redo();
  const std::string& text() const;
  std::size_t cursor() const;
  bool dirty() const;
  void markDirty();
  void markSaved();

private:
  void snapshot();

  std::string text_;
  std::size_t cursor_ = 0;
  std::size_t selectionAnchor_ = 0;
  bool selecting_ = false;
  bool dirty_ = false;
  std::vector<std::string> undo_;
  std::vector<std::string> redo_;
};

}
