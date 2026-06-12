#pragma once

#include "agenda/AgendaStore.h"
#include "ui/ShellModel.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace microagenda::ui {

struct UiSelection {
  std::string entryId;
  std::string search;
  agenda::SearchScope searchScope = agenda::SearchScope::All;
  int notePage = 0;
};

class AppState {
public:
  bool openOrCreateAgenda(const std::filesystem::path& root);
  bool hasAgenda() const;
  const std::filesystem::path& agendaRoot() const;
  const ShellModel& shell() const;
  ShellModel& shell();
  const UiSelection& selection() const;

  void selectEntry(std::string entryId);
  void setSearch(std::string query, agenda::SearchScope scope);
  void setNotePage(int page);

  std::vector<agenda::SearchResult> currentEntries() const;
  std::vector<agenda::UpcomingEvent> upcoming(std::string today) const;
  std::optional<agenda::LoadedEntry> selectedEntry(int notesPerPage = 10) const;

  std::optional<agenda::Entry> createEntry(const std::string& name);
  bool renameSelectedEntry(const std::string& name);
  bool updateSelectedEntryDescription(std::string description);
  bool deleteSelectedEntry();

  std::optional<agenda::Attribute> addAttribute(std::string key, std::string value);
  bool updateAttribute(std::string id, std::string key, std::string value);
  bool deleteAttribute(std::string id);

  std::optional<agenda::Note> addNote(std::string title, std::string body, std::string date, std::string time);
  bool updateNote(std::string id, std::string title, std::string body, std::string date, std::string time);
  bool deleteNote(std::string id);

  std::optional<agenda::Event> addEvent(std::string title, std::string date, std::string time, agenda::RepeatRule repeat, std::string description);
  bool updateEvent(const agenda::Event& event);
  bool deleteEvent(std::string id);

  bool saveUiState(const std::filesystem::path& path) const;
  bool loadUiState(const std::filesystem::path& path);

private:
  ShellModel shell_;
  UiSelection selection_;
  std::optional<agenda::AgendaStore> store_;
};

}
