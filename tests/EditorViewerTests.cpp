#include "TestSupport.h"

#include "editor/MarkdownEditor.h"
#include "editor/SoftWrap.h"
#include "markdown/MarkdownParser.h"
#include "ui/ShellModel.h"

#include <filesystem>
#include <fstream>
#include <sstream>

MICROAGENDA_TEST(editor_tracks_dirty_state) {
  microagenda::editor::MarkdownEditor editor;
  editor.setText("hello");
  MICROAGENDA_REQUIRE(!editor.dirty());
  editor.insert(" world");
  MICROAGENDA_REQUIRE(editor.dirty());
  MICROAGENDA_REQUIRE(editor.text() == "hello world");
  MICROAGENDA_REQUIRE(editor.undo());
  MICROAGENDA_REQUIRE(editor.text() == "hello");
  MICROAGENDA_REQUIRE(editor.redo());
  MICROAGENDA_REQUIRE(editor.text() == "hello world");
  editor.markSaved();
  MICROAGENDA_REQUIRE(!editor.dirty());
}

MICROAGENDA_TEST(editor_moves_cursor_and_deletes_forward) {
  microagenda::editor::MarkdownEditor editor;
  editor.setText("one\ntwo\nthree");
  editor.moveCursor(5);
  editor.moveLineDown();
  MICROAGENDA_REQUIRE(editor.cursor() == 9);
  editor.moveLineUp();
  MICROAGENDA_REQUIRE(editor.cursor() == 5);
  editor.moveLeft();
  editor.eraseNext();
  MICROAGENDA_REQUIRE(editor.text() == "one\nwo\nthree");
}

MICROAGENDA_TEST(editor_moves_and_erases_by_utf8_codepoints) {
  microagenda::editor::MarkdownEditor editor;
  editor.setText("caf\xC3\xA9");  // "café", the 'é' is two bytes
  MICROAGENDA_REQUIRE(editor.cursor() == 5);
  editor.moveLeft();
  MICROAGENDA_REQUIRE(editor.cursor() == 3);  // landed on the codepoint start, not mid-byte
  editor.moveRight();
  MICROAGENDA_REQUIRE(editor.cursor() == 5);
  editor.erasePrevious();
  MICROAGENDA_REQUIRE(editor.text() == "caf");  // whole 'é' removed, no dangling byte
  editor.setText("\xC3\xA9xy");  // "éxy"
  editor.moveCursor(0);
  editor.eraseNext();
  MICROAGENDA_REQUIRE(editor.text() == "xy");  // whole leading 'é' removed
}

MICROAGENDA_TEST(editor_moves_to_line_boundaries) {
  microagenda::editor::MarkdownEditor editor;
  editor.setText("one\ntwo three\nfour");
  editor.moveCursor(8);
  editor.moveLineStart();
  MICROAGENDA_REQUIRE(editor.cursor() == 4);
  editor.moveLineEnd();
  MICROAGENDA_REQUIRE(editor.cursor() == 13);
  editor.moveCursor(8);
  editor.moveLineStart(true);
  MICROAGENDA_REQUIRE(editor.selectedText() == "two ");
}

MICROAGENDA_TEST(editor_selects_replaces_and_erases_ranges) {
  microagenda::editor::MarkdownEditor editor;
  editor.setText("alpha beta");
  editor.selectRange(6, 10);
  MICROAGENDA_REQUIRE(editor.hasSelection());
  MICROAGENDA_REQUIRE(editor.selectedText() == "beta");
  editor.insert("gamma");
  MICROAGENDA_REQUIRE(editor.text() == "alpha gamma");
  editor.selectAll();
  MICROAGENDA_REQUIRE(editor.selectedText() == "alpha gamma");
  editor.eraseSelection();
  MICROAGENDA_REQUIRE(editor.text().empty());
}

MICROAGENDA_TEST(editor_ignores_empty_insert_without_selection) {
  microagenda::editor::MarkdownEditor editor;
  editor.setText("alpha");
  editor.insert("");
  MICROAGENDA_REQUIRE(!editor.dirty());
  MICROAGENDA_REQUIRE(!editor.undo());
  MICROAGENDA_REQUIRE(editor.text() == "alpha");
}

MICROAGENDA_TEST(editor_caps_undo_history) {
  microagenda::editor::MarkdownEditor editor;
  editor.setText("");
  for(int i = 0; i < 105; ++i) {
    editor.insert("x");
  }
  int undoCount = 0;
  while(editor.undo()) {
    ++undoCount;
  }
  MICROAGENDA_REQUIRE(undoCount == 100);
  MICROAGENDA_REQUIRE(editor.text().size() == 5);
}

MICROAGENDA_TEST(editor_soft_wraps_by_words_without_changing_source_offsets) {
  const std::string source = "alpha beta gamma";
  const auto rows = microagenda::editor::softWrap(source, 11, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICROAGENDA_REQUIRE(rows.size() == 2);
  MICROAGENDA_REQUIRE(rows[0].text == "alpha beta ");
  MICROAGENDA_REQUIRE(rows[0].start == 0);
  MICROAGENDA_REQUIRE(rows[0].end == 11);
  MICROAGENDA_REQUIRE(rows[1].text == "gamma");
  MICROAGENDA_REQUIRE(rows[1].start == 11);
  MICROAGENDA_REQUIRE(rows[1].end == source.size());
}

MICROAGENDA_TEST(editor_soft_wrap_keeps_remaining_words_together_when_they_fit) {
  const std::string source = "alpha beta gamma delta";
  const auto rows = microagenda::editor::softWrap(source, 11, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICROAGENDA_REQUIRE(rows.size() == 2);
  MICROAGENDA_REQUIRE(rows[0].text == "alpha beta ");
  MICROAGENDA_REQUIRE(rows[0].start == 0);
  MICROAGENDA_REQUIRE(rows[0].end == 11);
  MICROAGENDA_REQUIRE(rows[1].text == "gamma delta");
  MICROAGENDA_REQUIRE(rows[1].start == 11);
  MICROAGENDA_REQUIRE(rows[1].end == source.size());
}

MICROAGENDA_TEST(editor_soft_wrap_preserves_hard_newlines) {
  const std::string source = "one two\nthree";
  const auto rows = microagenda::editor::softWrap(source, 20, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICROAGENDA_REQUIRE(rows.size() == 2);
  MICROAGENDA_REQUIRE(rows[0].text == "one two");
  MICROAGENDA_REQUIRE(rows[0].start == 0);
  MICROAGENDA_REQUIRE(rows[0].end == 7);
  MICROAGENDA_REQUIRE(rows[1].text == "three");
  MICROAGENDA_REQUIRE(rows[1].start == 8);
  MICROAGENDA_REQUIRE(rows[1].end == source.size());
}

MICROAGENDA_TEST(editor_soft_wrap_splits_oversized_words) {
  const std::string source = "abcdefgh";
  const auto rows = microagenda::editor::softWrap(source, 3, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICROAGENDA_REQUIRE(rows.size() == 3);
  MICROAGENDA_REQUIRE(rows[0].text == "abc");
  MICROAGENDA_REQUIRE(rows[1].text == "def");
  MICROAGENDA_REQUIRE(rows[2].text == "gh");
}

MICROAGENDA_TEST(editor_soft_wrap_keeps_utf8_codepoints_intact) {
  const std::string source = "a\xC3\xA9\xC3\xA9";  // "aéé", each 'é' is two bytes
  const auto rows = microagenda::editor::softWrap(source, 2, [](std::string_view value) {
    return static_cast<int>(value.size());
  });
  MICROAGENDA_REQUIRE(rows.size() == 3);
  MICROAGENDA_REQUIRE(rows[0].text == "a");
  MICROAGENDA_REQUIRE(rows[1].text == "\xC3\xA9");
  MICROAGENDA_REQUIRE(rows[2].text == "\xC3\xA9");
}

MICROAGENDA_TEST(editor_soft_wrap_maps_offsets_and_hit_testing) {
  const std::string source = "alpha beta gamma";
  const auto measure = [](std::string_view value) {
    return static_cast<int>(value.size());
  };
  const auto rows = microagenda::editor::softWrap(source, 11, measure);
  MICROAGENDA_REQUIRE(microagenda::editor::rowForOffset(rows, 0) == 0);
  MICROAGENDA_REQUIRE(microagenda::editor::rowForOffset(rows, 12) == 1);
  MICROAGENDA_REQUIRE(microagenda::editor::offsetForRowX(rows[1], 2.0f, measure) == 13);
}

MICROAGENDA_TEST(markdown_parser_covers_syntax_reference_blocks) {
  const std::filesystem::path syntaxFixture {"docs/markdown-elements.md"};
  std::ifstream in {syntaxFixture};
  MICROAGENDA_REQUIRE(static_cast<bool>(in));
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const auto doc = microagenda::markdown::MarkdownParser().parse(buffer.str());
  int headings = 0;
  int orderedItems = 0;
  int unorderedItems = 0;
  int quotes = 0;
  int codeBlocks = 0;
  int links = 0;
  for(const auto& block : doc.blocks) {
    if(block.type == microagenda::markdown::BlockType::Heading) ++headings;
    if(block.type == microagenda::markdown::BlockType::OrderedItem) ++orderedItems;
    if(block.type == microagenda::markdown::BlockType::UnorderedItem) ++unorderedItems;
    if(block.type == microagenda::markdown::BlockType::Quote) ++quotes;
    if(block.type == microagenda::markdown::BlockType::Code) ++codeBlocks;
    for(const auto& inlineItem : block.inlines) {
      if(inlineItem.type == microagenda::markdown::InlineType::Link) ++links;
    }
    for(const auto& row : block.tableRows) {
      for(const auto& cell : row.cells) {
        for(const auto& inlineItem : cell.inlines) {
          if(inlineItem.type == microagenda::markdown::InlineType::Link) ++links;
        }
      }
    }
  }
  MICROAGENDA_REQUIRE(headings >= 8);
  MICROAGENDA_REQUIRE(orderedItems >= 3);
  MICROAGENDA_REQUIRE(unorderedItems >= 3);
  MICROAGENDA_REQUIRE(quotes >= 2);
  MICROAGENDA_REQUIRE(codeBlocks >= 2);
  MICROAGENDA_REQUIRE(links >= 8);
}
