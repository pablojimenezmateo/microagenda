#include "markdown/MarkdownParser.h"

#include <md4c.h>

extern "C" {
#include <entity.h>
}

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace microagenda::markdown {
namespace {

struct SpanState {
  InlineType type = InlineType::Text;
  std::string target;
  std::string title;
  std::size_t start = 0;
};

struct ListState {
  bool ordered = false;
  bool tight = false;
  unsigned next = 1;
};

struct ParseState {
  Document doc;
  Block current;
  bool inBlock = false;
  std::vector<SpanState> spans;
  int quoteDepth = 0;
  std::vector<ListState> lists;
  bool inTable = false;
  bool inTableHeader = false;
  bool inTableCell = false;
  int admonitionDepth = 0;
  std::string admonitionType;
  TableRow currentRow;
  TableCell currentCell;
  Block tableBlock;
};

static std::string attrToString(const MD_ATTRIBUTE& attr) {
  return attr.text && attr.size ? std::string(attr.text, attr.size) : std::string();
}

static void appendUtf8(std::string& out, unsigned codepoint) {
  if(codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if(codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if(codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if(codepoint <= 0x10ffff) {
    out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
}

static std::string decodeEntity(const char* text, unsigned size) {
  if(!text || size == 0) return {};
  if(size > 3 && text[0] == '&' && text[1] == '#') {
    const bool hex = text[2] == 'x' || text[2] == 'X';
    const int base = hex ? 16 : 10;
    const char* begin = text + (hex ? 3 : 2);
    char* end = nullptr;
    const auto value = std::strtoul(begin, &end, base);
    if(end && end <= text + size && value > 0) {
      std::string out;
      appendUtf8(out, static_cast<unsigned>(value));
      if(!out.empty()) return out;
    }
  }
  if(const ENTITY* entity = entity_lookup(text, size)) {
    std::string out;
    appendUtf8(out, entity->codepoints[0]);
    if(entity->codepoints[1]) appendUtf8(out, entity->codepoints[1]);
    return out;
  }
  return std::string(text, size);
}

static Align mdAlign(MD_ALIGN align) {
  switch(align) {
    case MD_ALIGN_LEFT: return Align::Left;
    case MD_ALIGN_CENTER: return Align::Center;
    case MD_ALIGN_RIGHT: return Align::Right;
    default: return Align::Default;
  }
}

static std::vector<Inline>& inlineTarget(ParseState& state) {
  return state.inTableCell ? state.currentCell.inlines : state.current.inlines;
}

static InlineType activeType(const ParseState& state, MD_TEXTTYPE textType) {
  if(textType == MD_TEXT_CODE) return InlineType::Code;
  for(auto it = state.spans.rbegin(); it != state.spans.rend(); ++it) {
    if(it->type != InlineType::Text) return it->type;
  }
  if(textType == MD_TEXT_HTML) return InlineType::Html;
  return InlineType::Text;
}

static void applyActiveSpan(ParseState& state, Inline& inlineText) {
  for(const auto& span : state.spans) {
    if(span.type == InlineType::Emphasis) inlineText.emphasis = true;
    if(span.type == InlineType::Strong) inlineText.strong = true;
    if(span.type == InlineType::Strikethrough) inlineText.strikethrough = true;
    if(span.type == InlineType::Link) {
      inlineText.target = span.target;
      inlineText.title = span.title;
    }
    if(span.type == InlineType::Image) {
      inlineText.target = span.target;
      inlineText.title = span.title;
    }
  }
}

static void pushCurrentBlock(ParseState& state) {
  state.doc.blocks.push_back(std::move(state.current));
  state.current = {};
  state.inBlock = false;
}

static int enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
  auto& state = *static_cast<ParseState*>(userdata);
  if(type == MD_BLOCK_DOC) return 0;
  if(type == MD_BLOCK_THEAD) {
    state.inTableHeader = true;
    return 0;
  }
  if(type == MD_BLOCK_TBODY) {
    state.inTableHeader = false;
    return 0;
  }
  if(type == MD_BLOCK_TR) {
    state.currentRow = {};
    state.currentRow.header = state.inTableHeader;
    return 0;
  }
  if(type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
    state.currentCell = {};
    state.currentCell.header = type == MD_BLOCK_TH || state.inTableHeader;
    state.currentCell.align = mdAlign(static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align);
    state.inTableCell = true;
    return 0;
  }
  if((type == MD_BLOCK_UL || type == MD_BLOCK_OL) && state.inBlock) pushCurrentBlock(state);
  state.current = {};
  state.inBlock = true;
  state.current.depth = static_cast<int>(state.lists.size()) + state.quoteDepth;
  switch(type) {
    case MD_BLOCK_H:
      state.current.type = BlockType::Heading;
      state.current.level = static_cast<MD_BLOCK_H_DETAIL*>(detail)->level;
      break;
    case MD_BLOCK_UL:
      state.lists.push_back({false, static_cast<MD_BLOCK_UL_DETAIL*>(detail)->is_tight != 0, 1});
      state.inBlock = false;
      break;
    case MD_BLOCK_OL:
      state.lists.push_back({true, static_cast<MD_BLOCK_OL_DETAIL*>(detail)->is_tight != 0, static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start});
      state.inBlock = false;
      break;
    case MD_BLOCK_LI:
      if(!state.lists.empty() && state.lists.back().ordered) {
        state.current.type = BlockType::OrderedItem;
        state.current.orderedNumber = static_cast<int>(state.lists.back().next++);
      } else {
        state.current.type = BlockType::UnorderedItem;
      }
      if(!state.lists.empty()) state.current.tight = state.lists.back().tight;
      if(detail) {
        const auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        state.current.task = li->is_task != 0;
        state.current.taskChecked = li->is_task && (li->task_mark == 'x' || li->task_mark == 'X');
      }
      break;
    case MD_BLOCK_QUOTE:
      ++state.quoteDepth;
      state.inBlock = false;
      break;
    case MD_BLOCK_CODE:
      state.current.type = BlockType::Code;
      if(detail) {
        const auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        state.current.info = attrToString(code->info);
        state.current.language = attrToString(code->lang);
      }
      break;
    case MD_BLOCK_HTML:
      state.current.type = BlockType::Html;
      break;
    case MD_BLOCK_TABLE:
      state.tableBlock = {};
      state.tableBlock.type = BlockType::Table;
      state.tableBlock.depth = state.current.depth;
      state.inTable = true;
      state.inBlock = false;
      break;
    case MD_BLOCK_HR:
      state.current.type = BlockType::HorizontalRule;
      break;
    case MD_BLOCK_FOOTNOTE_DEF:
      state.current.type = BlockType::Footnote;
      if(detail) state.current.footnoteLabel = attrToString(static_cast<MD_BLOCK_FOOTNOTE_DEF_DETAIL*>(detail)->label);
      break;
    case MD_BLOCK_ADMONITION:
      ++state.admonitionDepth;
      state.admonitionType = detail ? attrToString(static_cast<MD_BLOCK_ADMONITION_DETAIL*>(detail)->type) : std::string();
      state.inBlock = false;
      break;
    default:
      if(state.admonitionDepth > 0) {
        state.current.type = BlockType::Admonition;
        state.current.admonitionType = state.admonitionType;
      } else {
        state.current.type = state.quoteDepth > 0 ? BlockType::Quote : BlockType::Paragraph;
      }
      break;
  }
  return 0;
}

static int leaveBlock(MD_BLOCKTYPE type, void*, void* userdata) {
  auto& state = *static_cast<ParseState*>(userdata);
  if(type == MD_BLOCK_DOC) return 0;
  if(type == MD_BLOCK_THEAD || type == MD_BLOCK_TBODY || type == MD_BLOCK_FOOTNOTE_DEF_SECTION) return 0;
  if(type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
    state.currentRow.cells.push_back(std::move(state.currentCell));
    state.currentCell = {};
    state.inTableCell = false;
    return 0;
  }
  if(type == MD_BLOCK_TR) {
    state.tableBlock.tableRows.push_back(std::move(state.currentRow));
    state.currentRow = {};
    return 0;
  }
  if(type == MD_BLOCK_TABLE) {
    state.doc.blocks.push_back(std::move(state.tableBlock));
    state.tableBlock = {};
    state.inTable = false;
    state.inTableHeader = false;
    return 0;
  }
  if(type == MD_BLOCK_UL || type == MD_BLOCK_OL) {
    if(!state.lists.empty()) state.lists.pop_back();
    return 0;
  }
  if(type == MD_BLOCK_QUOTE) {
    state.quoteDepth = std::max(0, state.quoteDepth - 1);
    return 0;
  }
  if(type == MD_BLOCK_ADMONITION) {
    state.admonitionDepth = std::max(0, state.admonitionDepth - 1);
    if(state.admonitionDepth == 0) state.admonitionType.clear();
    return 0;
  }
  if(state.inBlock) pushCurrentBlock(state);
  return 0;
}

static int enterSpan(MD_SPANTYPE type, void* detail, void* userdata) {
  auto& state = *static_cast<ParseState*>(userdata);
  SpanState span;
  span.start = inlineTarget(state).size();
  if(type == MD_SPAN_A) {
    span.type = InlineType::Link;
    span.target = attrToString(static_cast<MD_SPAN_A_DETAIL*>(detail)->href);
    span.title = attrToString(static_cast<MD_SPAN_A_DETAIL*>(detail)->title);
  } else if(type == MD_SPAN_IMG) {
    span.type = InlineType::Image;
    span.target = attrToString(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src);
    span.title = attrToString(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->title);
  } else if(type == MD_SPAN_CODE) {
    span.type = InlineType::Code;
  } else if(type == MD_SPAN_EM) {
    span.type = InlineType::Emphasis;
  } else if(type == MD_SPAN_STRONG) {
    span.type = InlineType::Strong;
  } else if(type == MD_SPAN_DEL) {
    span.type = InlineType::Strikethrough;
  } else if(type == MD_SPAN_FOOTNOTE_REF) {
    Inline inlineItem;
    inlineItem.type = InlineType::FootnoteRef;
    if(detail) {
      const auto* ref = static_cast<MD_SPAN_FOOTNOTE_REF_DETAIL*>(detail);
      inlineItem.text = std::to_string(ref->id);
    }
    inlineTarget(state).push_back(std::move(inlineItem));
    return 0;
  } else {
    span.type = InlineType::Text;
  }
  state.spans.push_back(std::move(span));
  return 0;
}

static int leaveSpan(MD_SPANTYPE, void*, void* userdata) {
  auto& state = *static_cast<ParseState*>(userdata);
  if(state.spans.empty()) return 0;
  const auto span = state.spans.back();
  auto& inlines = inlineTarget(state);
  if((state.inBlock || state.inTableCell) && inlines.size() == span.start &&
    (span.type == InlineType::Image || span.type == InlineType::Link)) {
    Inline inlineItem;
    inlineItem.type = span.type;
    inlineItem.target = span.target;
    inlineItem.title = span.title;
    inlines.push_back(std::move(inlineItem));
  } else if(span.type == InlineType::Link || span.type == InlineType::Image) {
    for(std::size_t i = span.start; i < inlines.size(); ++i) {
      if(inlines[i].target.empty()) inlines[i].target = span.target;
      if(inlines[i].title.empty()) inlines[i].title = span.title;
      if(span.type == InlineType::Link && inlines[i].type == InlineType::Text) inlines[i].type = InlineType::Link;
      if(span.type == InlineType::Image) inlines[i].type = InlineType::Image;
    }
  }
  state.spans.pop_back();
  return 0;
}

static int textCallback(MD_TEXTTYPE type, const char* text, unsigned size, void* userdata) {
  auto& state = *static_cast<ParseState*>(userdata);
  if(!state.inBlock && !state.inTableCell) return 0;
  if((type != MD_TEXT_BR && type != MD_TEXT_SOFTBR) && (!text || size == 0)) return 0;
  Inline inlineText;
  inlineText.type = activeType(state, type);
  if(type == MD_TEXT_BR) inlineText.text = "\n";
  else if(type == MD_TEXT_SOFTBR) inlineText.text = "\n";
  else if(type == MD_TEXT_NULLCHAR) inlineText.text = "\xef\xbf\xbd";
  else if(type == MD_TEXT_ENTITY) inlineText.text = decodeEntity(text, size);
  else inlineText.text.assign(text, size);
  inlineText.hardBreak = type == MD_TEXT_BR;
  applyActiveSpan(state, inlineText);
  inlineTarget(state).push_back(std::move(inlineText));
  return 0;
}

static bool startsWithUrl(std::string_view text, std::size_t pos) {
  return text.substr(pos, 7) == "http://" || text.substr(pos, 8) == "https://";
}

static bool isUrlTerminator(char c) {
  return std::isspace(static_cast<unsigned char>(c)) || c == '<' || c == '>';
}

static bool isTrailingUrlPunctuation(char c) {
  return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '"' || c == '\'';
}

static std::string fileNameFromTarget(std::string_view target) {
  const auto lastSlash = target.find_last_of("/\\");
  const auto begin = lastSlash == std::string_view::npos ? 0 : lastSlash + 1;
  return std::string(target.substr(begin));
}

static bool isAttachmentTarget(std::string_view target) {
  return target.find(".microagenda/attachments/") != std::string_view::npos;
}

static std::vector<Inline> looseAttachmentLinks(const Inline& item) {
  std::vector<Inline> out;
  std::size_t pos = 0;
  while(pos < item.text.size()) {
    const auto labelOpen = item.text.find('[', pos);
    if(labelOpen == std::string::npos) {
      Inline text = item;
      text.text = item.text.substr(pos);
      if(!text.text.empty()) out.push_back(std::move(text));
      break;
    }

    const bool image = labelOpen > 0 && item.text[labelOpen - 1] == '!';
    const auto linkStart = image ? labelOpen - 1 : labelOpen;
    const auto labelClose = item.text.find(']', labelOpen + 1);
    if(labelClose == std::string::npos || labelClose + 1 >= item.text.size() || item.text[labelClose + 1] != '(') {
      Inline text = item;
      text.text = item.text.substr(pos, labelOpen - pos + 1);
      if(!text.text.empty()) out.push_back(std::move(text));
      pos = labelOpen + 1;
      continue;
    }

    const auto targetOpen = labelClose + 2;
    const auto targetClose = item.text.find(')', targetOpen);
    if(targetClose == std::string::npos) {
      Inline text = item;
      text.text = item.text.substr(pos);
      if(!text.text.empty()) out.push_back(std::move(text));
      break;
    }

    const auto target = std::string_view(item.text).substr(targetOpen, targetClose - targetOpen);
    if(!isAttachmentTarget(target)) {
      Inline text = item;
      text.text = item.text.substr(pos, targetClose - pos + 1);
      if(!text.text.empty()) out.push_back(std::move(text));
      pos = targetClose + 1;
      continue;
    }

    if(linkStart > pos) {
      Inline text = item;
      text.text = item.text.substr(pos, linkStart - pos);
      out.push_back(std::move(text));
    }

    Inline link = item;
    link.type = image ? InlineType::Image : InlineType::Link;
    link.text = item.text.substr(labelOpen + 1, labelClose - labelOpen - 1);
    link.target = std::string(target);
    if(link.text.empty()) link.text = fileNameFromTarget(link.target);
    out.push_back(std::move(link));
    pos = targetClose + 1;
  }
  return out;
}

static std::vector<Inline> autolinkTextInline(const Inline& item) {
  std::vector<Inline> out;
  std::size_t pos = 0;
  while(pos < item.text.size()) {
    std::size_t urlStart = std::string::npos;
    for(std::size_t i = pos; i < item.text.size(); ++i) {
      if(startsWithUrl(item.text, i)) {
        urlStart = i;
        break;
      }
    }
    if(urlStart == std::string::npos) {
      Inline text = item;
      text.text = item.text.substr(pos);
      if(!text.text.empty()) out.push_back(std::move(text));
      break;
    }
    if(urlStart > pos) {
      Inline text = item;
      text.text = item.text.substr(pos, urlStart - pos);
      out.push_back(std::move(text));
    }
    std::size_t urlEnd = urlStart;
    while(urlEnd < item.text.size() && !isUrlTerminator(item.text[urlEnd])) ++urlEnd;
    std::size_t trimmedEnd = urlEnd;
    while(trimmedEnd > urlStart && isTrailingUrlPunctuation(item.text[trimmedEnd - 1])) --trimmedEnd;
    if(trimmedEnd == urlStart) {
      Inline text = item;
      text.text = item.text.substr(urlStart, urlEnd - urlStart);
      out.push_back(std::move(text));
    } else {
      Inline link = item;
      link.type = InlineType::Link;
      link.text = item.text.substr(urlStart, trimmedEnd - urlStart);
      link.target = link.text;
      out.push_back(std::move(link));
      if(trimmedEnd < urlEnd) {
        Inline punctuation = item;
        punctuation.text = item.text.substr(trimmedEnd, urlEnd - trimmedEnd);
        out.push_back(std::move(punctuation));
      }
    }
    pos = urlEnd;
  }
  return out;
}

static void autolinkInlineVector(std::vector<Inline>& inlines) {
  std::vector<Inline> rewritten;
  for(const auto& item : inlines) {
    if(item.type == InlineType::Text && item.target.empty()) {
      auto looseLinks = looseAttachmentLinks(item);
      for(auto& looseItem : looseLinks) {
        if(looseItem.type == InlineType::Text && looseItem.target.empty()) {
          auto split = autolinkTextInline(looseItem);
          rewritten.insert(rewritten.end(), std::make_move_iterator(split.begin()), std::make_move_iterator(split.end()));
        } else {
          rewritten.push_back(std::move(looseItem));
        }
      }
    } else {
      rewritten.push_back(item);
    }
  }
  inlines = std::move(rewritten);
}

static void populateEmptyLinkLabels(std::vector<Inline>& inlines) {
  for(auto& item : inlines) {
    if(item.type == InlineType::Link && item.text.empty() && !item.target.empty()) {
      item.text = fileNameFromTarget(item.target);
    }
  }
}

static void autolinkDocument(Document& doc) {
  for(auto& block : doc.blocks) {
    populateEmptyLinkLabels(block.inlines);
    autolinkInlineVector(block.inlines);
    for(auto& row : block.tableRows) {
      for(auto& cell : row.cells) {
        populateEmptyLinkLabels(cell.inlines);
        autolinkInlineVector(cell.inlines);
      }
    }
  }
}

static bool isBlankLine(std::string_view line) {
  return std::all_of(line.begin(), line.end(), [](unsigned char c) {
    return std::isspace(c);
  });
}

struct FenceState {
  char marker = '\0';
  std::size_t length = 0;
};

static std::optional<FenceState> fenceOnLine(std::string_view line) {
  std::size_t pos = 0;
  while(pos < line.size() && pos < 4 && line[pos] == ' ') ++pos;
  if(pos > 3 || pos >= line.size() || (line[pos] != '`' && line[pos] != '~')) return std::nullopt;

  const char marker = line[pos];
  std::size_t end = pos;
  while(end < line.size() && line[end] == marker) ++end;
  const std::size_t length = end - pos;
  if(length < 3) return std::nullopt;
  return FenceState {marker, length};
}

static Document parseMarkdownFragment(std::string_view source) {
  ParseState state;
  MD_PARSER parser {};
  parser.abi_version = 0;
  parser.flags = MD_DIALECT_GITHUB;
  parser.enter_block = enterBlock;
  parser.leave_block = leaveBlock;
  parser.enter_span = enterSpan;
  parser.leave_span = leaveSpan;
  parser.text = textCallback;
  md_parse(source.data(), static_cast<unsigned>(source.size()), &parser, &state);
  return std::move(state.doc);
}

static void appendParsedFragment(Document& doc, std::string_view source) {
  if(source.empty()) return;
  auto parsed = parseMarkdownFragment(source);
  doc.blocks.insert(doc.blocks.end(), std::make_move_iterator(parsed.blocks.begin()), std::make_move_iterator(parsed.blocks.end()));
}

static void appendBlankLines(Document& doc, std::size_t count) {
  for(std::size_t i = 0; i < count; ++i) {
    Block block;
    block.type = BlockType::BlankLine;
    doc.blocks.push_back(std::move(block));
  }
}

static Document parsePreservingBlankLines(std::string_view source) {
  Document doc;
  std::size_t segmentStart = std::string_view::npos;
  std::size_t blankRunStart = std::string_view::npos;
  std::size_t firstBlankEnd = std::string_view::npos;
  std::size_t blankRun = 0;
  std::optional<FenceState> openFence;

  auto flushSegment = [&](std::size_t end) {
    if(segmentStart == std::string_view::npos) return;
    appendParsedFragment(doc, source.substr(segmentStart, end - segmentStart));
    segmentStart = std::string_view::npos;
  };

  auto resetBlankRun = [&]() {
    blankRun = 0;
    blankRunStart = std::string_view::npos;
    firstBlankEnd = std::string_view::npos;
  };

  std::size_t lineStart = 0;
  while(lineStart < source.size()) {
    std::size_t lineEnd = source.find('\n', lineStart);
    if(lineEnd == std::string_view::npos) lineEnd = source.size();
    const std::string_view line = source.substr(lineStart, lineEnd - lineStart);
    const std::size_t nextLineStart = lineEnd < source.size() ? lineEnd + 1 : lineEnd;

    if(!openFence && isBlankLine(line)) {
      if(blankRun == 0) {
        blankRunStart = lineStart;
        firstBlankEnd = nextLineStart;
      }
      ++blankRun;
      lineStart = nextLineStart;
      continue;
    }

    if(blankRun > 0) {
      if(segmentStart == std::string_view::npos) {
        appendBlankLines(doc, blankRun);
      } else if(blankRun > 1) {
        flushSegment(firstBlankEnd);
        appendBlankLines(doc, blankRun - 1);
        segmentStart = lineStart;
      }
      resetBlankRun();
    }

    if(segmentStart == std::string_view::npos) segmentStart = lineStart;

    if(const auto fence = fenceOnLine(line)) {
      if(!openFence) {
        openFence = fence;
      } else if(fence->marker == openFence->marker && fence->length >= openFence->length) {
        openFence.reset();
      }
    }

    lineStart = nextLineStart;
  }

  if(blankRun > 0) {
    flushSegment(blankRunStart);
    appendBlankLines(doc, blankRun);
    resetBlankRun();
  } else {
    flushSegment(source.size());
  }
  return doc;
}

}

Document MarkdownParser::parse(std::string_view source) const {
  auto doc = parsePreservingBlankLines(source);
  autolinkDocument(doc);
  return doc;
}

}
