#include "editor/MarkdownEditor.h"

#include <algorithm>

namespace microagenda::editor {
namespace {

constexpr std::size_t kMaxUndoSnapshots = 100;

bool isUtf8Continuation(unsigned char c) {
  return (c & 0xC0) == 0x80;
}

// Start offset of the UTF-8 codepoint immediately before `pos`.
std::size_t previousCodepoint(const std::string& text, std::size_t pos) {
  if(pos == 0) return 0;
  std::size_t i = pos - 1;
  while(i > 0 && isUtf8Continuation(static_cast<unsigned char>(text[i]))) --i;
  return i;
}

// Start offset of the UTF-8 codepoint immediately after `pos`.
std::size_t nextCodepoint(const std::string& text, std::size_t pos) {
  if(pos >= text.size()) return text.size();
  std::size_t i = pos + 1;
  while(i < text.size() && isUtf8Continuation(static_cast<unsigned char>(text[i]))) ++i;
  return i;
}

}

void MarkdownEditor::setText(std::string text) {
  text_ = std::move(text);
  cursor_ = text_.size();
  selectionAnchor_ = cursor_;
  selecting_ = false;
  dirty_ = false;
  undo_.clear();
  redo_.clear();
}

void MarkdownEditor::insert(std::string_view text) {
  if(text.empty() && !hasSelection()) return;
  snapshot();
  if(hasSelection()) {
    const auto start = selectionStart();
    text_.erase(start, selectionEnd() - start);
    cursor_ = start;
    clearSelection();
  }
  text_.insert(cursor_, text);
  cursor_ += text.size();
  dirty_ = true;
}

void MarkdownEditor::erasePrevious() {
  if(hasSelection()) {
    eraseSelection();
    return;
  }
  if(cursor_ == 0) return;
  snapshot();
  const auto previous = previousCodepoint(text_, cursor_);
  text_.erase(previous, cursor_ - previous);
  cursor_ = previous;
  dirty_ = true;
}

void MarkdownEditor::eraseNext() {
  if(hasSelection()) {
    eraseSelection();
    return;
  }
  if(cursor_ >= text_.size()) return;
  snapshot();
  text_.erase(cursor_, nextCodepoint(text_, cursor_) - cursor_);
  dirty_ = true;
}

void MarkdownEditor::moveCursor(std::size_t cursor) {
  cursor_ = std::min(cursor, text_.size());
  clearSelection();
}

void MarkdownEditor::selectRange(std::size_t anchor, std::size_t cursor) {
  selectionAnchor_ = std::min(anchor, text_.size());
  cursor_ = std::min(cursor, text_.size());
  selecting_ = selectionAnchor_ != cursor_;
}

void MarkdownEditor::selectAll() {
  selectionAnchor_ = 0;
  cursor_ = text_.size();
  selecting_ = !text_.empty();
}

void MarkdownEditor::clearSelection() {
  selecting_ = false;
  selectionAnchor_ = cursor_;
}

bool MarkdownEditor::hasSelection() const {
  return selecting_ && selectionAnchor_ != cursor_;
}

std::size_t MarkdownEditor::selectionStart() const {
  return std::min(selectionAnchor_, cursor_);
}

std::size_t MarkdownEditor::selectionEnd() const {
  return std::max(selectionAnchor_, cursor_);
}

std::string MarkdownEditor::selectedText() const {
  if(!hasSelection()) return "";
  return text_.substr(selectionStart(), selectionEnd() - selectionStart());
}

void MarkdownEditor::eraseSelection() {
  if(!hasSelection()) return;
  snapshot();
  const auto start = selectionStart();
  text_.erase(start, selectionEnd() - start);
  cursor_ = start;
  clearSelection();
  dirty_ = true;
}

void MarkdownEditor::moveLeft() {
  clearSelection();
  cursor_ = previousCodepoint(text_, cursor_);
}

void MarkdownEditor::moveRight() {
  clearSelection();
  cursor_ = nextCodepoint(text_, cursor_);
}

void MarkdownEditor::moveLineUp() {
  clearSelection();
  const auto lineStart = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
  if(lineStart == std::string::npos) {
    cursor_ = 0;
    return;
  }
  const auto previousEnd = lineStart;
  const auto previousStart = text_.rfind('\n', previousEnd == 0 ? 0 : previousEnd - 1);
  const auto column = cursor_ - lineStart - 1;
  const auto targetStart = previousStart == std::string::npos ? 0 : previousStart + 1;
  const auto targetLength = previousEnd - targetStart;
  cursor_ = targetStart + std::min(column, targetLength);
}

void MarkdownEditor::moveLineDown() {
  clearSelection();
  const auto lineStart = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
  const auto column = cursor_ - (lineStart == std::string::npos ? 0 : lineStart + 1);
  const auto currentEnd = text_.find('\n', cursor_);
  if(currentEnd == std::string::npos) {
    cursor_ = text_.size();
    return;
  }
  const auto nextStart = currentEnd + 1;
  const auto nextEnd = text_.find('\n', nextStart);
  const auto targetEnd = nextEnd == std::string::npos ? text_.size() : nextEnd;
  cursor_ = nextStart + std::min(column, targetEnd - nextStart);
}

void MarkdownEditor::moveLineStart(bool keepSelection) {
  const auto anchor = keepSelection ? selectionAnchor_ : cursor_;
  const auto lineStart = text_.rfind('\n', cursor_ == 0 ? 0 : cursor_ - 1);
  cursor_ = lineStart == std::string::npos ? 0 : lineStart + 1;
  if(keepSelection) selectRange(anchor, cursor_);
  else clearSelection();
}

void MarkdownEditor::moveLineEnd(bool keepSelection) {
  const auto anchor = keepSelection ? selectionAnchor_ : cursor_;
  const auto lineEnd = text_.find('\n', cursor_);
  cursor_ = lineEnd == std::string::npos ? text_.size() : lineEnd;
  if(keepSelection) selectRange(anchor, cursor_);
  else clearSelection();
}

bool MarkdownEditor::undo() {
  if(undo_.empty()) return false;
  redo_.push_back(text_);
  text_ = undo_.back();
  undo_.pop_back();
  cursor_ = std::min(cursor_, text_.size());
  dirty_ = true;
  return true;
}

bool MarkdownEditor::redo() {
  if(redo_.empty()) return false;
  undo_.push_back(text_);
  text_ = redo_.back();
  redo_.pop_back();
  cursor_ = std::min(cursor_, text_.size());
  dirty_ = true;
  return true;
}

const std::string& MarkdownEditor::text() const {
  return text_;
}

std::size_t MarkdownEditor::cursor() const {
  return cursor_;
}

bool MarkdownEditor::dirty() const {
  return dirty_;
}

void MarkdownEditor::markDirty() {
  dirty_ = true;
}

void MarkdownEditor::markSaved() {
  dirty_ = false;
}

void MarkdownEditor::snapshot() {
  if(!undo_.empty() && undo_.back() == text_) return;
  undo_.push_back(text_);
  if(undo_.size() > kMaxUndoSnapshots) undo_.erase(undo_.begin());
  redo_.clear();
}

}
