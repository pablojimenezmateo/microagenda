#include "TestSupport.h"

#include "agenda/AgendaStore.h"

#include <sqlite3.h>

#include <filesystem>
#include <fstream>

MICROAGENDA_TEST(agenda_store_crud_searches_and_paginates) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-store-test";
  std::filesystem::remove_all(root);

  microagenda::agenda::AgendaStore store;
  MICROAGENDA_REQUIRE(store.open(root));
  auto entry = store.createEntry("Alice");
  MICROAGENDA_REQUIRE(entry.has_value());
  MICROAGENDA_REQUIRE(std::filesystem::exists(root / ".microagenda" / "agenda.sqlite"));

  auto attr = store.addAttribute(entry->id, "role", "advisor");
  MICROAGENDA_REQUIRE(attr.has_value());
  MICROAGENDA_REQUIRE(store.updateAttribute(attr->id, "role", "mentor"));
  MICROAGENDA_REQUIRE(store.updateEntryDescription(entry->id, "entry description has overview"));
  MICROAGENDA_REQUIRE(store.addEvent(entry->id, "Planning", "2999-06-04", "", microagenda::agenda::RepeatRule::None, "budget review").has_value());
  MICROAGENDA_REQUIRE(store.addEvent(entry->id, "Past hidden", "2000-01-01", "", microagenda::agenda::RepeatRule::None, "ancient hiddenword").has_value());

  for(int i = 0; i < 12; ++i) {
    MICROAGENDA_REQUIRE(store.addNote(entry->id, "", "note body " + std::to_string(i), "2026-06-04", "10:00").has_value());
  }
  auto firstPage = store.loadEntry(entry->id, 0, 10);
  MICROAGENDA_REQUIRE(firstPage.has_value());
  MICROAGENDA_REQUIRE(firstPage->notes.size() == 10);
  MICROAGENDA_REQUIRE(firstPage->noteCount == 12);
  MICROAGENDA_REQUIRE(firstPage->events.size() == 1);
  MICROAGENDA_REQUIRE(firstPage->entry.description == "entry description has overview");
  auto secondPage = store.loadEntry(entry->id, 1, 10);
  MICROAGENDA_REQUIRE(secondPage.has_value());
  MICROAGENDA_REQUIRE(secondPage->notes.size() == 2);
  auto note = store.addNote(entry->id, "Needle title", "needleword appears here", "2026-06-05", "11:30");
  MICROAGENDA_REQUIRE(note.has_value());
  MICROAGENDA_REQUIRE(note->title == "Needle title");
  MICROAGENDA_REQUIRE(note->date == "2026-06-05");
  MICROAGENDA_REQUIRE(note->time == "11:30");
  MICROAGENDA_REQUIRE(store.updateNote(note->id, "Updated title", "needleword appears here", "2026-06-06", "12:45"));
  MICROAGENDA_REQUIRE(store.updateNote(note->id, "Title survives empty date", "needleword appears here", "", ""));
  auto reloaded = store.loadEntry(entry->id, 0, 10);
  MICROAGENDA_REQUIRE(reloaded.has_value());
  MICROAGENDA_REQUIRE(!reloaded->notes.empty());
  MICROAGENDA_REQUIRE(reloaded->notes.front().title == "Title survives empty date");

  auto mentorResults = store.search("mentor", microagenda::agenda::SearchScope::All);
  MICROAGENDA_REQUIRE(mentorResults.size() == 1);
  MICROAGENDA_REQUIRE(!mentorResults.front().previews.empty());
  MICROAGENDA_REQUIRE(mentorResults.front().previews.front().label == "Attribute role");
  MICROAGENDA_REQUIRE(mentorResults.front().previews.front().text == "mentor");
  MICROAGENDA_REQUIRE(store.search("mentor", microagenda::agenda::SearchScope::Name).empty());
  MICROAGENDA_REQUIRE(store.search("Alice", microagenda::agenda::SearchScope::Name).size() == 1);
  auto descriptionResults = store.search("overview", microagenda::agenda::SearchScope::All);
  MICROAGENDA_REQUIRE(descriptionResults.size() == 1);
  MICROAGENDA_REQUIRE(descriptionResults.front().previews.front().label == "Entry description");
  auto noteResults = store.search("needleword", microagenda::agenda::SearchScope::All);
  MICROAGENDA_REQUIRE(noteResults.size() == 1);
  MICROAGENDA_REQUIRE(!noteResults.front().previews.empty());
  MICROAGENDA_REQUIRE(noteResults.front().previews.front().label == "Note line 1");
  MICROAGENDA_REQUIRE(noteResults.front().previews.front().targetKind == microagenda::agenda::SearchTargetKind::Note);
  auto eventResults = store.search("budget", microagenda::agenda::SearchScope::All);
  MICROAGENDA_REQUIRE(eventResults.size() == 1);
  MICROAGENDA_REQUIRE(!eventResults.front().previews.empty());
  MICROAGENDA_REQUIRE(eventResults.front().previews.front().label == "Event description");
  MICROAGENDA_REQUIRE(store.search("hiddenword", microagenda::agenda::SearchScope::All).empty());

  std::filesystem::remove_all(root);
}

MICROAGENDA_TEST(agenda_store_expands_upcoming_repeating_events) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-upcoming-test";
  std::filesystem::remove_all(root);

  microagenda::agenda::AgendaStore store;
  MICROAGENDA_REQUIRE(store.open(root));
  auto entry = store.createEntry("Calendar");
  MICROAGENDA_REQUIRE(entry.has_value());
  MICROAGENDA_REQUIRE(store.addEvent(entry->id, "Daily", "2026-06-01", "09:00", microagenda::agenda::RepeatRule::Daily, "standup").has_value());
  MICROAGENDA_REQUIRE(store.addEvent(entry->id, "Weekly", "2026-05-27", "", microagenda::agenda::RepeatRule::Weekly, "").has_value());
  MICROAGENDA_REQUIRE(store.addEvent(entry->id, "Future", "2026-06-08", "", microagenda::agenda::RepeatRule::None, "").has_value());

  const auto upcoming = store.upcoming("2026-06-03", 3);
  MICROAGENDA_REQUIRE(upcoming.size() == 4);
  MICROAGENDA_REQUIRE(upcoming[0].occurrenceDate == "2026-06-03");
  MICROAGENDA_REQUIRE(upcoming[0].event.title == "Weekly");
  MICROAGENDA_REQUIRE(upcoming[1].event.title == "Daily");
  MICROAGENDA_REQUIRE(upcoming[2].occurrenceDate == "2026-06-04");
  MICROAGENDA_REQUIRE(upcoming[3].occurrenceDate == "2026-06-05");

  std::filesystem::remove_all(root);
}

MICROAGENDA_TEST(agenda_store_deletes_note_attachments) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-note-attachment-delete-test";
  std::filesystem::remove_all(root);

  microagenda::agenda::AgendaStore store;
  MICROAGENDA_REQUIRE(store.open(root));
  auto entry = store.createEntry("Attachments");
  MICROAGENDA_REQUIRE(entry.has_value());
  auto note = store.addNote(entry->id, "", "body", "2026-06-04", "10:00");
  MICROAGENDA_REQUIRE(note.has_value());
  const auto attachmentDir = root / ".microagenda" / "attachments" / note->id;
  std::filesystem::create_directories(attachmentDir);
  std::ofstream(attachmentDir / "clipboard.png") << "image";
  MICROAGENDA_REQUIRE(std::filesystem::exists(attachmentDir / "clipboard.png"));

  MICROAGENDA_REQUIRE(store.deleteNote(note->id));
  MICROAGENDA_REQUIRE(!std::filesystem::exists(attachmentDir));
  const auto loaded = store.loadEntry(entry->id, 0, 10);
  MICROAGENDA_REQUIRE(loaded.has_value());
  MICROAGENDA_REQUIRE(loaded->notes.empty());

  std::filesystem::remove_all(root);
}

MICROAGENDA_TEST(agenda_store_deletes_entry_children_and_note_attachments) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-entry-attachment-delete-test";
  std::filesystem::remove_all(root);

  microagenda::agenda::AgendaStore store;
  MICROAGENDA_REQUIRE(store.open(root));
  auto entry = store.createEntry("Entry cleanup");
  MICROAGENDA_REQUIRE(entry.has_value());
  MICROAGENDA_REQUIRE(store.addAttribute(entry->id, "key", "value").has_value());
  MICROAGENDA_REQUIRE(store.addEvent(entry->id, "Event", "2999-06-04").has_value());
  auto first = store.addNote(entry->id, "", "first", "2026-06-04", "10:00");
  auto second = store.addNote(entry->id, "", "second", "2026-06-04", "11:00");
  MICROAGENDA_REQUIRE(first.has_value());
  MICROAGENDA_REQUIRE(second.has_value());
  const auto firstDir = root / ".microagenda" / "attachments" / first->id;
  const auto secondDir = root / ".microagenda" / "attachments" / second->id;
  std::filesystem::create_directories(firstDir);
  std::filesystem::create_directories(secondDir);
  std::ofstream(firstDir / "first.png") << "image";
  std::ofstream(secondDir / "second.png") << "image";

  MICROAGENDA_REQUIRE(store.deleteEntry(entry->id));
  MICROAGENDA_REQUIRE(!store.entry(entry->id).has_value());
  MICROAGENDA_REQUIRE(!std::filesystem::exists(firstDir));
  MICROAGENDA_REQUIRE(!std::filesystem::exists(secondDir));
  MICROAGENDA_REQUIRE(store.search("Entry cleanup", microagenda::agenda::SearchScope::Name).empty());

  std::filesystem::remove_all(root);
}


MICROAGENDA_TEST(agenda_store_uses_durable_sqlite_pragmas) {
  const auto root = std::filesystem::temp_directory_path() / "microagenda-durability-test";
  std::filesystem::remove_all(root);

  microagenda::agenda::AgendaStore store;
  MICROAGENDA_REQUIRE(store.open(root));

  sqlite3* db = nullptr;
  MICROAGENDA_REQUIRE(sqlite3_open(store.dbPath().c_str(), &db) == SQLITE_OK);
  sqlite3_stmt* stmt = nullptr;
  MICROAGENDA_REQUIRE(sqlite3_prepare_v2(db, "PRAGMA synchronous;", -1, &stmt, nullptr) == SQLITE_OK);
  MICROAGENDA_REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
  MICROAGENDA_REQUIRE(sqlite3_column_int(stmt, 0) == 2);
  sqlite3_finalize(stmt);
  stmt = nullptr;
  MICROAGENDA_REQUIRE(sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, nullptr) == SQLITE_OK);
  MICROAGENDA_REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
  const auto* mode = sqlite3_column_text(stmt, 0);
  MICROAGENDA_REQUIRE(mode && std::string(reinterpret_cast<const char*>(mode)) == "wal");
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  std::filesystem::remove_all(root);
}

