#include "perf/Perf.h"

#include "agenda/AgendaStore.h"
#include "markdown/MarkdownParser.h"

#include <filesystem>
#include <iostream>
#include <sstream>

namespace {

static std::string heavyMarkdown(int seed, int sections) {
  std::ostringstream out;
  out << "# Perf Note " << seed << "\n\n";
  for(int section = 0; section < sections; ++section) {
    out << "## Section " << section << "\n\n";
    out << "This paragraph has searchable agenda text, [a link](https://example.com/" << seed << "/" << section << "), ";
    out << "inline `code`, **strong text**, and attachment syntax.\n\n";
    out << "![image " << section << "](.microagenda/attachments/perf-" << seed << "/image-" << section << ".png)\n\n";
    out << "- completed item " << section << "\n";
    out << "- pending item " << section << "\n\n";
  }
  return out.str();
}

static void printSamples() {
  const auto samples = microagenda::perf::Recorder::instance().snapshot();
  for(const auto& sample : samples) {
    std::cout << sample.name << ": " << sample.micros << "us\n";
  }
}

}

int main() {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-perf-fixture";
  std::filesystem::remove_all(root);
  microagenda::agenda::AgendaStore store;
  store.open(root);

  {
    microagenda::perf::ScopeTimer timer("fixture.agenda.create_1000_entries");
    for(int i = 0; i < 1000; ++i) {
      auto entry = store.createEntry("Person " + std::to_string(i));
      if(!entry) continue;
      store.addAttribute(entry->id, "segment", i % 2 == 0 ? "even" : "odd");
      store.addNote(entry->id, "", heavyMarkdown(i, 2), "2026-06-03", "09:00");
      if(i % 5 == 0) store.addEvent(entry->id, "Follow up", "2026-06-03", "09:00", microagenda::agenda::RepeatRule::Weekly, "Check in");
    }
  }

  {
    microagenda::perf::ScopeTimer timer("fixture.agenda.search");
    (void)store.search("searchable", microagenda::agenda::SearchScope::All);
    (void)store.search("Person 42", microagenda::agenda::SearchScope::Name);
  }

  {
    microagenda::perf::ScopeTimer timer("fixture.agenda.upcoming");
    (void)store.upcoming("2026-06-03", 3);
  }

  {
    microagenda::markdown::MarkdownParser parser;
    microagenda::perf::ScopeTimer timer("fixture.markdown.parse_heavy_document");
    const auto doc = parser.parse(heavyMarkdown(9999, 200));
    std::cout << "heavy_document.blocks: " << doc.blocks.size() << "\n";
  }

  printSamples();
  std::filesystem::remove_all(root);
  return 0;
}
