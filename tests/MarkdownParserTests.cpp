#include "TestSupport.h"

#include "markdown/MarkdownParser.h"
#include "markdown/RenderModel.h"

#include <md4c-html.h>

#include <string>

using microagenda::markdown::BlockType;
using microagenda::markdown::InlineType;
using microagenda::markdown::MarkdownParser;

namespace {

static void appendHtml(const MD_CHAR* text, MD_SIZE size, void* userdata) {
  auto& out = *static_cast<std::string*>(userdata);
  out.append(text, size);
}

static std::string md4cHtml(const std::string& markdown) {
  std::string out;
  md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()), appendHtml, &out, MD_DIALECT_GITHUB, 0);
  return out;
}

}

MICROAGENDA_TEST(markdown_parser_parses_basic_blocks) {
  const auto doc = MarkdownParser().parse("# Title\n\n- item\n\n> quote\n\n```mermaid\ngraph TD\n```\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 4);
  MICROAGENDA_REQUIRE(doc.blocks[0].type == BlockType::Heading);
  MICROAGENDA_REQUIRE(doc.blocks[0].level == 1);
  MICROAGENDA_REQUIRE(doc.blocks[1].type == BlockType::UnorderedItem);
  MICROAGENDA_REQUIRE(doc.blocks[2].type == BlockType::Quote);
  MICROAGENDA_REQUIRE(doc.blocks[3].type == BlockType::Code);
}

MICROAGENDA_TEST(markdown_parser_keeps_links_and_images) {
  const auto doc = MarkdownParser().parse("Read [doc](file.pdf) and see ![alt](image.png)\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 1);
  bool sawLink = false;
  bool sawImage = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawLink = sawLink || (item.type == InlineType::Link && item.target == "file.pdf");
    sawImage = sawImage || (item.type == InlineType::Image && item.target == "image.png");
  }
  MICROAGENDA_REQUIRE(sawLink);
  MICROAGENDA_REQUIRE(sawImage);
}

MICROAGENDA_TEST(markdown_parser_renders_single_newlines_as_line_breaks) {
  const auto doc = MarkdownParser().parse("first line\nsecond line\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 1);
  bool sawBreak = false;
  std::string combined;
  for(const auto& item : doc.blocks[0].inlines) {
    combined += item.text;
    sawBreak = sawBreak || item.text == "\n";
  }
  MICROAGENDA_REQUIRE(sawBreak);
  MICROAGENDA_REQUIRE(combined == "first line\nsecond line");
}

MICROAGENDA_TEST(markdown_parser_preserves_additional_empty_lines_as_blank_blocks) {
  const auto doc = MarkdownParser().parse("first\n\n\n\nsecond\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 4);
  MICROAGENDA_REQUIRE(doc.blocks[0].type == BlockType::Paragraph);
  MICROAGENDA_REQUIRE(doc.blocks[1].type == BlockType::BlankLine);
  MICROAGENDA_REQUIRE(doc.blocks[2].type == BlockType::BlankLine);
  MICROAGENDA_REQUIRE(doc.blocks[3].type == BlockType::Paragraph);
}

MICROAGENDA_TEST(markdown_parser_keeps_empty_lines_inside_fenced_code) {
  const auto doc = MarkdownParser().parse("```text\nfirst\n\nsecond\n```\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 1);
  MICROAGENDA_REQUIRE(doc.blocks[0].type == BlockType::Code);
  MICROAGENDA_REQUIRE(microagenda::markdown::plainText(doc).find("first\n\nsecond") != std::string::npos);
}

MICROAGENDA_TEST(markdown_parser_keeps_empty_links_and_images) {
  const auto doc = MarkdownParser().parse("![](.microagenda/attachments/note/clipboard.png)\n[](.microagenda/attachments/note/doc.pdf)\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 1);
  bool sawImage = false;
  bool sawLink = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawImage = sawImage || (item.type == InlineType::Image && item.target == ".microagenda/attachments/note/clipboard.png");
    sawLink = sawLink || (item.type == InlineType::Link && item.text == "doc.pdf" && item.target == ".microagenda/attachments/note/doc.pdf");
  }
  MICROAGENDA_REQUIRE(sawImage);
  MICROAGENDA_REQUIRE(sawLink);
}

MICROAGENDA_TEST(markdown_parser_tolerates_attachment_links_with_spaces) {
  const auto target = std::string(".microagenda/attachments/n25083524798053-0/From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf");
  const auto label = std::string("From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf");
  const auto doc = MarkdownParser().parse("[" + label + "](" + target + ")\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 1);
  bool sawLink = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawLink = sawLink || (item.type == InlineType::Link && item.text == label && item.target == target);
  }
  MICROAGENDA_REQUIRE(sawLink);
}

MICROAGENDA_TEST(markdown_parser_autolinks_raw_urls) {
  const auto url = std::string("https://ptvgroup-my.sharepoint.com/:b:/p/lena_wostracky/IQD_Wqa8-9fCRZ6zBN9QL");
  const auto doc = MarkdownParser().parse("Open " + url + ".\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 1);
  bool sawLink = false;
  bool sawTrailingPeriod = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawLink = sawLink || (item.type == InlineType::Link && item.text == url && item.target == url);
    sawTrailingPeriod = sawTrailingPeriod || (item.type == InlineType::Text && item.text == ".");
  }
  MICROAGENDA_REQUIRE(sawLink);
  MICROAGENDA_REQUIRE(sawTrailingPeriod);
}

MICROAGENDA_TEST(markdown_parser_keeps_ordered_numbers_and_rules) {
  const auto doc = MarkdownParser().parse("3. Bird\n1. McHale\n8. Parish\n\n---\n\nUse the `printf()` function and *emphasis*.\n");
  MICROAGENDA_REQUIRE(doc.blocks.size() == 5);
  MICROAGENDA_REQUIRE(doc.blocks[0].type == BlockType::OrderedItem);
  MICROAGENDA_REQUIRE(doc.blocks[0].orderedNumber == 3);
  MICROAGENDA_REQUIRE(doc.blocks[1].orderedNumber == 4);
  MICROAGENDA_REQUIRE(doc.blocks[2].orderedNumber == 5);
  MICROAGENDA_REQUIRE(doc.blocks[3].type == BlockType::HorizontalRule);
  bool sawCode = false;
  bool sawEmphasis = false;
  for(const auto& item : doc.blocks[4].inlines) {
    sawCode = sawCode || item.type == InlineType::Code;
    sawEmphasis = sawEmphasis || item.type == InlineType::Emphasis;
  }
  MICROAGENDA_REQUIRE(sawCode);
  MICROAGENDA_REQUIRE(sawEmphasis);
}

MICROAGENDA_TEST(markdown_parser_keeps_gfm_tables) {
  const std::string source = "| Left | Center | Right |\n|:-----|:------:|------:|\n| a | b | c |\n";
  const auto doc = MarkdownParser().parse(source);
  MICROAGENDA_REQUIRE(doc.blocks.size() == 1);
  MICROAGENDA_REQUIRE(doc.blocks[0].type == BlockType::Table);
  MICROAGENDA_REQUIRE(doc.blocks[0].tableRows.size() == 2);
  MICROAGENDA_REQUIRE(doc.blocks[0].tableRows[0].header);
  MICROAGENDA_REQUIRE(doc.blocks[0].tableRows[0].cells.size() == 3);
  MICROAGENDA_REQUIRE(doc.blocks[0].tableRows[0].cells[0].align == microagenda::markdown::Align::Left);
  MICROAGENDA_REQUIRE(doc.blocks[0].tableRows[0].cells[1].align == microagenda::markdown::Align::Center);
  MICROAGENDA_REQUIRE(doc.blocks[0].tableRows[0].cells[2].align == microagenda::markdown::Align::Right);
  const auto html = md4cHtml(source);
  MICROAGENDA_REQUIRE(html.find("<table>") != std::string::npos);
  MICROAGENDA_REQUIRE(html.find("<th align=\"center\">") != std::string::npos);
  MICROAGENDA_REQUIRE(microagenda::markdown::plainText(doc).find("Center") != std::string::npos);
}

MICROAGENDA_TEST(markdown_parser_keeps_gfm_task_lists_and_strike_links) {
  const std::string source = "- [x] done with ~~strike~~ and [link](https://example.com)\n- [ ] todo\n";
  const auto doc = MarkdownParser().parse(source);
  MICROAGENDA_REQUIRE(doc.blocks.size() == 2);
  MICROAGENDA_REQUIRE(doc.blocks[0].type == BlockType::UnorderedItem);
  MICROAGENDA_REQUIRE(doc.blocks[0].task);
  MICROAGENDA_REQUIRE(doc.blocks[0].taskChecked);
  MICROAGENDA_REQUIRE(doc.blocks[1].task);
  MICROAGENDA_REQUIRE(!doc.blocks[1].taskChecked);
  bool sawStrike = false;
  bool sawLink = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawStrike = sawStrike || item.strikethrough || item.type == InlineType::Strikethrough;
    sawLink = sawLink || (item.type == InlineType::Link && item.target == "https://example.com");
  }
  MICROAGENDA_REQUIRE(sawStrike);
  MICROAGENDA_REQUIRE(sawLink);
  const auto html = md4cHtml(source);
  MICROAGENDA_REQUIRE(html.find("type=\"checkbox\"") != std::string::npos);
  MICROAGENDA_REQUIRE(html.find("<del>strike</del>") != std::string::npos);
}

MICROAGENDA_TEST(markdown_parser_keeps_nested_list_parent_items) {
  const std::string source = "- Parent item\n  - Child item\n    - Grandchild item\n- Second parent item\n\n1. Plan\n   - Research\n   - Implement\n2. Verify\n   1. Build\n   2. Test\n";
  const auto doc = MarkdownParser().parse(source);
  const auto plain = microagenda::markdown::plainText(doc);
  MICROAGENDA_REQUIRE(plain.find("Parent item") != std::string::npos);
  MICROAGENDA_REQUIRE(plain.find("Child item") != std::string::npos);
  MICROAGENDA_REQUIRE(plain.find("Grandchild item") != std::string::npos);
  MICROAGENDA_REQUIRE(plain.find("Plan") != std::string::npos);
  MICROAGENDA_REQUIRE(plain.find("Research") != std::string::npos);
  MICROAGENDA_REQUIRE(plain.find("Build") != std::string::npos);
}

MICROAGENDA_TEST(markdown_parser_keeps_footnotes_and_admonitions) {
  const std::string source = "Read this[^a].\n\n[^a]: Footnote body.\n\n> [!NOTE]\n> Pay attention.\n";
  const auto doc = MarkdownParser().parse(source);
  bool sawRef = false;
  bool sawFootnote = false;
  bool sawAdmonition = false;
  for(const auto& block : doc.blocks) {
    sawFootnote = sawFootnote || (block.type == BlockType::Footnote && block.footnoteLabel == "a");
    sawAdmonition = sawAdmonition || (block.type == BlockType::Admonition && block.admonitionType == "note");
    for(const auto& item : block.inlines) sawRef = sawRef || item.type == InlineType::FootnoteRef;
  }
  MICROAGENDA_REQUIRE(sawRef);
  MICROAGENDA_REQUIRE(sawFootnote);
  MICROAGENDA_REQUIRE(sawAdmonition);
  const auto html = md4cHtml(source);
  MICROAGENDA_REQUIRE(html.find("footnote") != std::string::npos);
  MICROAGENDA_REQUIRE(html.find("admonition") != std::string::npos);
}

MICROAGENDA_TEST(markdown_parser_decodes_entities_like_md4c_html) {
  const std::string source = "Fish &amp; chips &#169;\n";
  const auto doc = MarkdownParser().parse(source);
  MICROAGENDA_REQUIRE(doc.blocks.size() == 1);
  MICROAGENDA_REQUIRE(microagenda::markdown::plainText(doc).find("Fish & chips") != std::string::npos);
  MICROAGENDA_REQUIRE(microagenda::markdown::plainText(doc).find("©") != std::string::npos);
  const auto html = md4cHtml(source);
  MICROAGENDA_REQUIRE(html.find("Fish &amp; chips ©") != std::string::npos);
}
