#include "markdown/RenderModel.h"

namespace microagenda::markdown {

std::string plainText(const Document& document) {
  std::string out;
  std::size_t size = document.blocks.size();
  for(const auto& block : document.blocks) {
    for(const auto& item : block.inlines) size += item.text.size();
    for(const auto& row : block.tableRows) {
      ++size;
      for(const auto& cell : row.cells) {
        ++size;
        for(const auto& item : cell.inlines) size += item.text.size();
      }
    }
  }
  out.reserve(size);
  for(const auto& block : document.blocks) {
    for(const auto& item : block.inlines) {
      out += item.text;
    }
    for(const auto& row : block.tableRows) {
      for(const auto& cell : row.cells) {
        for(const auto& item : cell.inlines) out += item.text;
        out.push_back('\t');
      }
      out.push_back('\n');
    }
    out.push_back('\n');
  }
  return out;
}

}
