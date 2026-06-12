#include "TestSupport.h"

#include "ui/AppState.h"

#include <filesystem>

MICROAGENDA_TEST(app_state_opens_filters_and_persists_agenda_state) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-app-state-test";
  std::filesystem::remove_all(root);

  microagenda::ui::AppState state;
  MICROAGENDA_REQUIRE(state.openOrCreateAgenda(root));
  auto entry = state.createEntry("Alice");
  MICROAGENDA_REQUIRE(entry.has_value());
  MICROAGENDA_REQUIRE(state.addAttribute("role", "advisor").has_value());
  state.setSearch("advisor", microagenda::agenda::SearchScope::All);
  MICROAGENDA_REQUIRE(state.currentEntries().size() == 1);
  state.setSearch("advisor", microagenda::agenda::SearchScope::Name);
  MICROAGENDA_REQUIRE(state.currentEntries().empty());
  state.setSearch("", microagenda::agenda::SearchScope::All);
  state.setNotePage(2);

  const auto path = root / ".microagenda" / "ui.state";
  MICROAGENDA_REQUIRE(state.saveUiState(path));
  microagenda::ui::AppState loaded;
  MICROAGENDA_REQUIRE(loaded.loadUiState(path));
  MICROAGENDA_REQUIRE(loaded.selection().entryId == entry->id);
  MICROAGENDA_REQUIRE(loaded.selection().notePage == 2);

  std::filesystem::remove_all(root);
}
