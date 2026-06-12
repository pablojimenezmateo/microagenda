#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace microagenda::agenda {

enum class SearchScope {
  All,
  Name
};

enum class RepeatRule {
  None,
  Daily,
  Weekly,
  Monthly,
  Yearly
};

struct Entry {
  std::string id;
  std::string name;
  std::string description;
  std::string createdAt;
  std::string updatedAt;
};

struct Attribute {
  std::string id;
  std::string entryId;
  std::string key;
  std::string value;
  int position = 0;
};

struct Note {
  std::string id;
  std::string entryId;
  std::string title;
  std::string body;
  std::string date;
  std::string time;
  std::string createdAt;
  std::string updatedAt;
};

struct Event {
  std::string id;
  std::string entryId;
  std::string title;
  std::string description;
  std::string date;
  std::string time;
  RepeatRule repeat = RepeatRule::None;
};

struct UpcomingEvent {
  Event event;
  Entry entry;
  std::string occurrenceDate;
};

enum class SearchTargetKind {
  Entry,
  Attribute,
  Note,
  Event
};

struct SearchPreview {
  std::string label;
  std::string text;
  SearchTargetKind targetKind = SearchTargetKind::Entry;
  std::string targetId;
  int noteIndex = -1;
};

struct SearchResult {
  Entry entry;
  std::string matchText;
  std::vector<SearchPreview> previews;
};

struct LoadedEntry {
  Entry entry;
  std::vector<Attribute> attributes;
  std::vector<Note> notes;
  std::vector<Event> events;
  int noteCount = 0;
};

class AgendaStore {
public:
  bool open(const std::filesystem::path& root);
  const std::filesystem::path& root() const;
  const std::filesystem::path& dbPath() const;

  bool migrate();
  bool rebuildSearchIndex();

  std::optional<Entry> createEntry(std::string name);
  bool renameEntry(std::string_view entryId, std::string name);
  bool updateEntryDescription(std::string_view entryId, std::string description);
  bool deleteEntry(std::string_view entryId);
  std::optional<Entry> entry(std::string_view entryId) const;
  std::vector<Entry> entries() const;
  std::vector<SearchResult> search(std::string_view query, SearchScope scope) const;

  std::optional<LoadedEntry> loadEntry(std::string_view entryId, int notePage = 0, int notesPerPage = 10) const;

  std::optional<Attribute> addAttribute(std::string_view entryId, std::string key, std::string value);
  bool updateAttribute(std::string_view id, std::string key, std::string value);
  bool deleteAttribute(std::string_view id);

  std::optional<Note> addNote(std::string_view entryId, std::string title, std::string body, std::string date, std::string time);
  bool updateNote(std::string_view id, std::string title, std::string body, std::string date, std::string time);
  bool deleteNote(std::string_view id);

  std::optional<Event> addEvent(std::string_view entryId, std::string title, std::string date, std::string time = {}, RepeatRule repeat = RepeatRule::None, std::string description = {});
  bool updateEvent(const Event& event);
  bool deleteEvent(std::string_view id);
  std::vector<UpcomingEvent> upcoming(std::string today, int days = 3) const;

private:
  std::filesystem::path root_;
  std::filesystem::path dbPath_;
};

std::string repeatRuleToString(RepeatRule repeat);
RepeatRule repeatRuleFromString(std::string_view value);
std::string generateId(std::string_view prefix);

}
