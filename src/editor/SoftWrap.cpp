#include "editor/SoftWrap.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <limits>
#include <string_view>

namespace microagenda::editor {
namespace {

bool isWrapSpace(char c) {
  return c != '\n' && std::isspace(static_cast<unsigned char>(c));
}

bool isUtf8Continuation(unsigned char c) {
  return (c & 0xC0) == 0x80;
}

// Ensure `index` sits on a UTF-8 codepoint boundary inside (start, end], never
// splitting a multibyte character. Guarantees forward progress past `start`.
std::size_t snapToCodepoint(std::string_view text, std::size_t index, std::size_t start, std::size_t end) {
  if(index >= end) return end;
  std::size_t snapped = index;
  while(snapped > start && isUtf8Continuation(static_cast<unsigned char>(text[snapped]))) --snapped;
  if(snapped > start) return snapped;
  // A single codepoint wider than the row: advance past the whole codepoint.
  snapped = start + 1;
  while(snapped < end && isUtf8Continuation(static_cast<unsigned char>(text[snapped]))) ++snapped;
  return snapped;
}

int measuredWidth(std::string_view value, const MeasureText& measure) {
  return measure ? measure(value) : static_cast<int>(value.size());
}

void pushRow(std::vector<SoftWrapRow>& rows, std::string_view text, std::size_t start, std::size_t end) {
  rows.push_back({start, end, std::string(text.substr(start, end - start))});
}

void wrapLogicalLine(std::vector<SoftWrapRow>& rows, std::string_view text, std::size_t start, std::size_t end, int width, const MeasureText& measure) {
  if(start == end) {
    pushRow(rows, text, start, end);
    return;
  }
  if(width <= 0 || measuredWidth(text.substr(start, end - start), measure) <= width) {
    pushRow(rows, text, start, end);
    return;
  }

  std::size_t pos = start;
  while(pos < end) {
    if(measuredWidth(text.substr(pos, end - pos), measure) <= width) {
      pushRow(rows, text, pos, end);
      break;
    }

    std::size_t low = pos + 1;
    std::size_t high = end;
    std::size_t fitEnd = pos;
    while(low <= high) {
      const std::size_t mid = low + (high - low) / 2;
      if(measuredWidth(text.substr(pos, mid - pos), measure) <= width) {
        fitEnd = mid;
        low = mid + 1;
      } else {
        if(mid == 0) break;
        high = mid - 1;
      }
    }

    std::size_t breakAfter = std::numeric_limits<std::size_t>::max();
    for(std::size_t i = fitEnd; i > pos; --i) {
      if(isWrapSpace(text[i - 1])) {
        breakAfter = i;
        break;
      }
    }

    std::size_t rowEnd = fitEnd;
    if(breakAfter != std::numeric_limits<std::size_t>::max() && breakAfter > pos) rowEnd = breakAfter;
    if(rowEnd <= pos) rowEnd = std::min(pos + 1, end);
    rowEnd = snapToCodepoint(text, rowEnd, pos, end);
    pushRow(rows, text, pos, rowEnd);
    pos = rowEnd;
  }
}

}

std::vector<SoftWrapRow> softWrap(std::string_view text, int width, const MeasureText& measure) {
  std::vector<SoftWrapRow> rows;
  rows.reserve(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1);
  std::size_t lineStart = 0;
  for(std::size_t i = 0; i < text.size(); ++i) {
    if(text[i] == '\n') {
      wrapLogicalLine(rows, text, lineStart, i, width, measure);
      lineStart = i + 1;
    }
  }
  wrapLogicalLine(rows, text, lineStart, text.size(), width, measure);
  if(rows.empty()) rows.push_back({0, 0, ""});
  return rows;
}

int rowForOffset(const std::vector<SoftWrapRow>& rows, std::size_t offset) {
  if(rows.empty()) return 0;
  const auto found = std::lower_bound(rows.begin(), rows.end(), offset, [](const SoftWrapRow& row, std::size_t value) {
    return row.end < value;
  });
  if(found == rows.end()) return static_cast<int>(rows.size() - 1);
  return static_cast<int>(std::distance(rows.begin(), found));
}

std::size_t offsetForRowX(const SoftWrapRow& row, float x, const MeasureText& measure) {
  if(x <= 0.0f) return row.start;
  if(row.text.empty()) return row.start;

  std::size_t low = 0;
  std::size_t high = row.text.size();
  while(low < high) {
    const std::size_t mid = low + (high - low) / 2;
    const float w = static_cast<float>(measuredWidth(std::string_view(row.text.data(), mid), measure));
    if(w < x) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }

  const std::size_t after = std::min(low, row.text.size());
  const std::size_t before = after == 0 ? 0 : after - 1;
  const float beforeW = static_cast<float>(measuredWidth(std::string_view(row.text.data(), before), measure));
  const float afterW = static_cast<float>(measuredWidth(std::string_view(row.text.data(), after), measure));
  std::size_t best = std::abs(afterW - x) <= std::abs(beforeW - x) ? after : before;
  while(best > 0 && best < row.text.size() &&
        (static_cast<unsigned char>(row.text[best]) & 0xC0) == 0x80) {
    --best;
  }
  return std::min(row.start + best, row.end);
}

}
