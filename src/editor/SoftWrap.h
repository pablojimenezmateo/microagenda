#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace microagenda::editor {

struct SoftWrapRow {
  std::size_t start = 0;
  std::size_t end = 0;
  std::string text;
};

using MeasureText = std::function<int(std::string_view)>;

std::vector<SoftWrapRow> softWrap(std::string_view text, int width, const MeasureText& measure);
int rowForOffset(const std::vector<SoftWrapRow>& rows, std::size_t offset);
std::size_t offsetForRowX(const SoftWrapRow& row, float x, const MeasureText& measure);

}
