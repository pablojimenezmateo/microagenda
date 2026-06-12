#include "ui/AppState.h"

#include "platform/DurableFile.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

namespace microagenda::ui {

bool AppState::openOrCreateAgenda(const std::filesystem::path& root) {
  store_.emplace();
  return store_->open(root);
}

bool AppState::hasAgenda() const {
  return store_.has_value();
}

const std::filesystem::path& AppState::agendaRoot() const {
  static const std::filesystem::path empty;
  return store_ ? store_->root() : empty;
}

const ShellModel& AppState::shell() const {
  return shell_;
}

ShellModel& AppState::shell() {
  return shell_;
}

const UiSelection& AppState::selection() const {
  return selection_;
}

void AppState::selectEntry(std::string entryId) {
  selection_.entryId = std::move(entryId);
  selection_.notePage = 0;
  shell_.paneMode = PaneMode::Viewer;
}

void AppState::setSearch(std::string query, agenda::SearchScope scope) {
  selection_.search = std::move(query);
  selection_.searchScope = scope;
}

void AppState::setNotePage(int page) {
  selection_.notePage = std::max(0, page);
}

std::vector<agenda::SearchResult> AppState::currentEntries() const {
  if(!store_) return {};
  return store_->search(selection_.search, selection_.searchScope);
}

std::vector<agenda::UpcomingEvent> AppState::upcoming(std::string today) const {
  if(!store_) return {};
  return store_->upcoming(std::move(today), 3);
}

std::optional<agenda::LoadedEntry> AppState::selectedEntry(int notesPerPage) const {
  if(!store_ || selection_.entryId.empty()) return std::nullopt;
  return store_->loadEntry(selection_.entryId, selection_.notePage, notesPerPage);
}

std::optional<agenda::Entry> AppState::createEntry(const std::string& name) {
  if(!store_) return std::nullopt;
  auto entry = store_->createEntry(name);
  if(entry) selectEntry(entry->id);
  return entry;
}

bool AppState::renameSelectedEntry(const std::string& name) {
  return store_ && !selection_.entryId.empty() && store_->renameEntry(selection_.entryId, name);
}

bool AppState::updateSelectedEntryDescription(std::string description) {
  return store_ && !selection_.entryId.empty() && store_->updateEntryDescription(selection_.entryId, std::move(description));
}

bool AppState::deleteSelectedEntry() {
  if(!store_ || selection_.entryId.empty()) return false;
  const bool ok = store_->deleteEntry(selection_.entryId);
  if(ok) {
    selection_.entryId.clear();
    selection_.notePage = 0;
  }
  return ok;
}

std::optional<agenda::Attribute> AppState::addAttribute(std::string key, std::string value) {
  if(!store_ || selection_.entryId.empty()) return std::nullopt;
  return store_->addAttribute(selection_.entryId, std::move(key), std::move(value));
}

bool AppState::updateAttribute(std::string id, std::string key, std::string value) {
  return store_ && store_->updateAttribute(std::move(id), std::move(key), std::move(value));
}

bool AppState::deleteAttribute(std::string id) {
  return store_ && store_->deleteAttribute(std::move(id));
}

std::optional<agenda::Note> AppState::addNote(std::string title, std::string body, std::string date, std::string time) {
  if(!store_ || selection_.entryId.empty()) return std::nullopt;
  selection_.notePage = 0;
  return store_->addNote(selection_.entryId, std::move(title), std::move(body), std::move(date), std::move(time));
}

bool AppState::updateNote(std::string id, std::string title, std::string body, std::string date, std::string time) {
  return store_ && store_->updateNote(std::move(id), std::move(title), std::move(body), std::move(date), std::move(time));
}

bool AppState::deleteNote(std::string id) {
  return store_ && store_->deleteNote(std::move(id));
}

std::optional<agenda::Event> AppState::addEvent(std::string title, std::string date, std::string time, agenda::RepeatRule repeat, std::string description) {
  if(!store_ || selection_.entryId.empty()) return std::nullopt;
  return store_->addEvent(selection_.entryId, std::move(title), std::move(date), std::move(time), repeat, std::move(description));
}

bool AppState::updateEvent(const agenda::Event& event) {
  return store_ && store_->updateEvent(event);
}

bool AppState::deleteEvent(std::string id) {
  return store_ && store_->deleteEvent(std::move(id));
}

bool AppState::saveUiState(const std::filesystem::path& path) const {
  std::ostringstream out;
  out << "pane=" << static_cast<int>(shell_.paneMode) << "\n";
  out << "sidebar=" << shell_.sidebarWidth << "\n";
  out << "entry=" << selection_.entryId << "\n";
  out << "search=" << selection_.search << "\n";
  out << "search_scope=" << static_cast<int>(selection_.searchScope) << "\n";
  out << "note_page=" << selection_.notePage << "\n";
  return platform::writeFileDurably(path, out.str());
}

bool AppState::loadUiState(const std::filesystem::path& path) {
  std::ifstream in(path);
  if(!in) return false;
  const auto parseInt = [](const std::string& value, int fallback) {
    int result = fallback;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, result);
    if(ec != std::errc {} || ptr != last) return fallback;
    return result;
  };
  std::string line;
  while(std::getline(in, line)) {
    const auto eq = line.find('=');
    if(eq == std::string::npos) continue;
    const auto key = line.substr(0, eq);
    const auto value = line.substr(eq + 1);
    if(key == "pane") {
      const int mode = parseInt(value, static_cast<int>(shell_.paneMode));
      if(mode >= static_cast<int>(PaneMode::Editor) && mode <= static_cast<int>(PaneMode::Split)) {
        shell_.paneMode = static_cast<PaneMode>(mode);
      }
    }
    else if(key == "sidebar") shell_.sidebarWidth = parseInt(value, shell_.sidebarWidth);
    else if(key == "entry") selection_.entryId = value;
    else if(key == "search") selection_.search = value;
    else if(key == "search_scope") {
      const int scope = parseInt(value, static_cast<int>(selection_.searchScope));
      if(scope >= static_cast<int>(agenda::SearchScope::All) && scope <= static_cast<int>(agenda::SearchScope::Name)) {
        selection_.searchScope = static_cast<agenda::SearchScope>(scope);
      }
    }
    else if(key == "note_page") selection_.notePage = std::max(0, parseInt(value, selection_.notePage));
  }
  return true;
}

}
