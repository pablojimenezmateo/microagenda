#include "agenda/AgendaStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <system_error>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace microagenda::agenda {
namespace {

static bool exec(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  sqlite3_free(error);
  return rc == SQLITE_OK;
}

static bool tableCount(sqlite3* db, const char* sql, int& out) {
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    ok = sqlite3_step(stmt) == SQLITE_ROW;
    if(ok) out = sqlite3_column_int(stmt, 0);
  }
  if(stmt) sqlite3_finalize(stmt);
  return ok;
}

static bool execAndChanged(sqlite3* db, const char* sql) {
  if(!exec(db, sql)) return false;
  return sqlite3_changes(db) > 0;
}

static void bindText(sqlite3_stmt* stmt, int index, std::string_view value) {
  sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

static std::string columnText(sqlite3_stmt* stmt, int index) {
  const auto* text = sqlite3_column_text(stmt, index);
  return text ? reinterpret_cast<const char*>(text) : std::string();
}

static std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

static std::string trimCopy(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
  if(first >= last) return {};
  return std::string(first, last);
}

static std::vector<std::string> queryTerms(std::string_view query) {
  std::vector<std::string> terms;
  std::string term;
  for(const unsigned char c : query) {
    if(std::isalnum(c)) {
      term.push_back(static_cast<char>(std::tolower(c)));
    } else if(!term.empty()) {
      terms.push_back(term);
      term.clear();
    }
  }
  if(!term.empty()) terms.push_back(term);
  return terms;
}

static bool matchesTerms(std::string_view text, const std::vector<std::string>& terms) {
  if(terms.empty()) return false;
  const auto lowered = lowerCopy(std::string(text));
  return std::any_of(terms.begin(), terms.end(), [&](const std::string& term) { return lowered.find(term) != std::string::npos; });
}

static std::string compactPreview(std::string value, std::size_t limit = 80) {
  value = trimCopy(std::move(value));
  std::replace(value.begin(), value.end(), '\n', ' ');
  if(value.size() <= limit) return value;
  if(limit <= 3) return value.substr(0, limit);
  return value.substr(0, limit - 3) + "...";
}

static std::string nowIso() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
  std::tm tm {};
  localtime_r(&raw, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return out.str();
}

static std::string todayIso() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t raw = std::chrono::system_clock::to_time_t(now);
  std::tm tm {};
  localtime_r(&raw, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d");
  return out.str();
}

static std::string timeFromIso(std::string_view value) {
  if(value.size() >= 16 && value[10] == 'T') return std::string(value.substr(11, 5));
  return {};
}

static std::optional<std::chrono::sys_days> parseDate(std::string_view value) {
  int y = 0;
  unsigned m = 0;
  unsigned d = 0;
  char dash1 = 0;
  char dash2 = 0;
  std::istringstream in {std::string(value)};
  if(!(in >> y >> dash1 >> m >> dash2 >> d) || dash1 != '-' || dash2 != '-') return std::nullopt;
  const std::chrono::year_month_day ymd {std::chrono::year {y}, std::chrono::month {m}, std::chrono::day {d}};
  if(!ymd.ok()) return std::nullopt;
  return std::chrono::sys_days {ymd};
}

static std::string formatDate(std::chrono::sys_days day) {
  const std::chrono::year_month_day ymd {day};
  std::ostringstream out;
  out << static_cast<int>(ymd.year()) << '-'
      << std::setw(2) << std::setfill('0') << static_cast<unsigned>(ymd.month()) << '-'
      << std::setw(2) << std::setfill('0') << static_cast<unsigned>(ymd.day());
  return out.str();
}

static bool sameMonthDay(std::chrono::sys_days start, std::chrono::sys_days target) {
  const std::chrono::year_month_day a {start};
  const std::chrono::year_month_day b {target};
  return a.month() == b.month() && a.day() == b.day();
}

static bool sameDayOfMonth(std::chrono::sys_days start, std::chrono::sys_days target) {
  const std::chrono::year_month_day a {start};
  const std::chrono::year_month_day b {target};
  if(a.day() == b.day()) return true;
  // Clamp to month length: an event on the 29th-31st falls on the last day of
  // shorter months (e.g. a Jan 31 monthly event occurs on Feb 28/29).
  const std::chrono::year_month_day_last lastOfTarget {b.year() / b.month() / std::chrono::last};
  return a.day() > lastOfTarget.day() && b.day() == lastOfTarget.day();
}

static bool occursOn(const Event& event, std::chrono::sys_days target) {
  const auto start = parseDate(event.date);
  if(!start || target < *start) return false;
  const auto delta = target - *start;
  switch(event.repeat) {
    case RepeatRule::None: return target == *start;
    case RepeatRule::Daily: return true;
    case RepeatRule::Weekly: return delta.count() % 7 == 0;
    case RepeatRule::Monthly: return sameDayOfMonth(*start, target);
    case RepeatRule::Yearly: return sameMonthDay(*start, target);
  }
  return false;
}

static bool shouldShowEvent(const Event& event, std::chrono::sys_days today) {
  if(event.repeat != RepeatRule::None) return true;
  const auto date = parseDate(event.date);
  if(!date) return false;
  return *date >= today - std::chrono::days {1};
}

static Entry readEntry(sqlite3_stmt* stmt, int offset = 0) {
  return {
    columnText(stmt, offset + 0),
    columnText(stmt, offset + 1),
    columnText(stmt, offset + 2),
    columnText(stmt, offset + 3),
    columnText(stmt, offset + 4),
  };
}

static Attribute readAttribute(sqlite3_stmt* stmt) {
  return {
    columnText(stmt, 0),
    columnText(stmt, 1),
    columnText(stmt, 2),
    columnText(stmt, 3),
    sqlite3_column_int(stmt, 4),
  };
}

static Note readNote(sqlite3_stmt* stmt) {
  return {
    columnText(stmt, 0),
    columnText(stmt, 1),
    columnText(stmt, 2),
    columnText(stmt, 3),
    columnText(stmt, 4),
    columnText(stmt, 5),
    columnText(stmt, 6),
    columnText(stmt, 7),
  };
}

static Event readEvent(sqlite3_stmt* stmt, int offset = 0) {
  return {
    columnText(stmt, offset + 0),
    columnText(stmt, offset + 1),
    columnText(stmt, offset + 2),
    columnText(stmt, offset + 3),
    columnText(stmt, offset + 4),
    columnText(stmt, offset + 5),
    repeatRuleFromString(columnText(stmt, offset + 6)),
  };
}

static std::optional<Entry> selectEntryBy(sqlite3* db, const char* sql, std::string_view value) {
  sqlite3_stmt* stmt = nullptr;
  std::optional<Entry> out;
  if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, value);
    if(sqlite3_step(stmt) == SQLITE_ROW) out = readEntry(stmt);
  }
  if(stmt) sqlite3_finalize(stmt);
  return out;
}

static std::vector<std::string> noteIdsForEntry(sqlite3* db, std::string_view entryId) {
  std::vector<std::string> out;
  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(db, "SELECT id FROM notes WHERE entry_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, entryId);
    while(sqlite3_step(stmt) == SQLITE_ROW) out.push_back(columnText(stmt, 0));
  }
  if(stmt) sqlite3_finalize(stmt);
  return out;
}

static bool removeNoteAttachments(const std::filesystem::path& root, std::string_view noteId) {
  if(noteId.empty()) return true;
  const auto dir = root / ".microagenda" / "attachments" / std::string(noteId);
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  return !error;
}

static bool removeNoteAttachments(const std::filesystem::path& root, const std::vector<std::string>& noteIds) {
  bool ok = true;
  for(const auto& noteId : noteIds) ok = removeNoteAttachments(root, noteId) && ok;
  return ok;
}

static void pushPreview(std::vector<SearchPreview>& previews, std::string label, std::string text, SearchTargetKind targetKind, std::string targetId, int noteIndex = -1) {
  text = compactPreview(std::move(text));
  if(text.empty()) return;
  previews.push_back({std::move(label), std::move(text), targetKind, std::move(targetId), noteIndex});
}

static void populateSearchPreviews(sqlite3* db, SearchResult& result, std::string_view query, SearchScope scope) {
  const auto terms = queryTerms(query);
  if(terms.empty()) return;
  if(matchesTerms(result.entry.name, terms)) pushPreview(result.previews, "Entry", result.entry.name, SearchTargetKind::Entry, result.entry.id);
  if(matchesTerms(result.entry.description, terms)) pushPreview(result.previews, "Entry description", result.entry.description, SearchTargetKind::Entry, result.entry.id);
  if(scope == SearchScope::Name) return;

  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(db, "SELECT id,entry_id,key,value,position FROM attributes WHERE entry_id=? ORDER BY position;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, result.entry.id);
    while(sqlite3_step(stmt) == SQLITE_ROW) {
      const auto attr = readAttribute(stmt);
      if(matchesTerms(attr.key, terms) || matchesTerms(attr.value, terms)) {
        pushPreview(result.previews, "Attribute " + attr.key, attr.value.empty() ? attr.key : attr.value, SearchTargetKind::Attribute, attr.id);
      }
    }
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;

  if(sqlite3_prepare_v2(db, "SELECT id,entry_id,title,body,date,time,created_at,updated_at FROM notes WHERE entry_id=? ORDER BY date DESC,time DESC,id DESC;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, result.entry.id);
    int noteIndex = 0;
    while(sqlite3_step(stmt) == SQLITE_ROW) {
      const auto note = readNote(stmt);
      if(matchesTerms(note.title, terms)) pushPreview(result.previews, "Note title", note.title, SearchTargetKind::Note, note.id, noteIndex);
      if(matchesTerms(note.date, terms) || matchesTerms(note.time, terms)) {
        pushPreview(result.previews, "Note date", note.date + (note.time.empty() ? "" : " " + note.time), SearchTargetKind::Note, note.id, noteIndex);
      }
      std::istringstream lines {note.body};
      std::string line;
      int lineNumber = 1;
      while(std::getline(lines, line)) {
        if(matchesTerms(line, terms)) pushPreview(result.previews, "Note line " + std::to_string(lineNumber), line, SearchTargetKind::Note, note.id, noteIndex);
        ++lineNumber;
      }
      ++noteIndex;
    }
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;

  const auto today = parseDate(todayIso()).value_or(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
  if(sqlite3_prepare_v2(db, "SELECT id,entry_id,title,description,date,time,repeat_rule FROM events WHERE entry_id=? ORDER BY date,time,title;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, result.entry.id);
    while(sqlite3_step(stmt) == SQLITE_ROW) {
      const auto event = readEvent(stmt);
      if(!shouldShowEvent(event, today)) continue;
      if(matchesTerms(event.title, terms)) pushPreview(result.previews, "Event title", event.title, SearchTargetKind::Event, event.id);
      if(matchesTerms(event.description, terms)) pushPreview(result.previews, "Event description", event.description, SearchTargetKind::Event, event.id);
      if(matchesTerms(event.date, terms) || matchesTerms(event.time, terms)) {
        pushPreview(result.previews, "Event date", event.date + (event.time.empty() ? "" : " " + event.time), SearchTargetKind::Event, event.id);
      }
    }
  }
  if(stmt) sqlite3_finalize(stmt);
}

static bool upsertSearchForEntry(sqlite3* db, std::string_view entryId) {
  sqlite3_stmt* deleteStmt = nullptr;
  sqlite3_stmt* selectStmt = nullptr;
  sqlite3_stmt* insertStmt = nullptr;
  bool ok = true;
  sqlite3_prepare_v2(db, "DELETE FROM entries_fts WHERE id=?;", -1, &deleteStmt, nullptr);
  sqlite3_prepare_v2(db,
    "SELECT e.name,"
    " e.description,"
    " COALESCE(group_concat(DISTINCT a.key || ' ' || a.value), ''),"
    " COALESCE(group_concat(DISTINCT n.title || ' ' || n.body || ' ' || n.date || ' ' || n.time), ''),"
    " COALESCE(group_concat(DISTINCT ev.title || ' ' || ev.description), '')"
    " FROM entries e"
    " LEFT JOIN attributes a ON a.entry_id=e.id"
    " LEFT JOIN notes n ON n.entry_id=e.id"
    " LEFT JOIN events ev ON ev.entry_id=e.id"
    " WHERE e.id=? GROUP BY e.id;",
    -1, &selectStmt, nullptr);
  sqlite3_prepare_v2(db, "INSERT INTO entries_fts(id,name,body) VALUES(?,?,?);", -1, &insertStmt, nullptr);
  ok = deleteStmt && selectStmt && insertStmt;
  if(ok) {
    bindText(deleteStmt, 1, entryId);
    ok = sqlite3_step(deleteStmt) == SQLITE_DONE;
  }
  if(ok) {
    bindText(selectStmt, 1, entryId);
    if(sqlite3_step(selectStmt) == SQLITE_ROW) {
      const std::string name = columnText(selectStmt, 0);
      const std::string body = columnText(selectStmt, 1) + "\n" + columnText(selectStmt, 2) + "\n" + columnText(selectStmt, 3) + "\n" + columnText(selectStmt, 4);
      bindText(insertStmt, 1, entryId);
      bindText(insertStmt, 2, name);
      bindText(insertStmt, 3, body);
      ok = sqlite3_step(insertStmt) == SQLITE_DONE;
    }
  }
  if(deleteStmt) sqlite3_finalize(deleteStmt);
  if(selectStmt) sqlite3_finalize(selectStmt);
  if(insertStmt) sqlite3_finalize(insertStmt);
  return ok;
}

static bool refreshEntry(sqlite3* db, std::string_view entryId) {
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "UPDATE entries SET updated_at=? WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    const auto now = nowIso();
    bindText(stmt, 1, now);
    bindText(stmt, 2, entryId);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  }
  if(stmt) sqlite3_finalize(stmt);
  return ok && upsertSearchForEntry(db, entryId);
}

static int nextPosition(sqlite3* db, std::string_view entryId) {
  sqlite3_stmt* stmt = nullptr;
  int out = 0;
  if(sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(position), -1) + 1 FROM attributes WHERE entry_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, entryId);
    if(sqlite3_step(stmt) == SQLITE_ROW) out = sqlite3_column_int(stmt, 0);
  }
  if(stmt) sqlite3_finalize(stmt);
  return out;
}

}

std::string repeatRuleToString(RepeatRule repeat) {
  switch(repeat) {
    case RepeatRule::Daily: return "daily";
    case RepeatRule::Weekly: return "weekly";
    case RepeatRule::Monthly: return "monthly";
    case RepeatRule::Yearly: return "yearly";
    case RepeatRule::None: return "none";
  }
  return "none";
}

RepeatRule repeatRuleFromString(std::string_view value) {
  if(value == "daily") return RepeatRule::Daily;
  if(value == "weekly") return RepeatRule::Weekly;
  if(value == "monthly") return RepeatRule::Monthly;
  if(value == "yearly") return RepeatRule::Yearly;
  return RepeatRule::None;
}

std::string generateId(std::string_view prefix) {
  // Wall-clock time keeps ids roughly sortable; a process-unique random salt
  // prevents collisions between separate runs (steady_clock resets per process).
  static const unsigned long long salt = std::random_device {}();
  static unsigned long long counter = 0;
  const auto now = std::chrono::system_clock::now().time_since_epoch().count();
  std::ostringstream out;
  out << prefix << std::hex << now << "-" << salt << "-" << counter++;
  return out.str();
}

bool AgendaStore::open(const std::filesystem::path& root) {
  root_ = root;
  std::filesystem::create_directories(root_ / ".microagenda");
  dbPath_ = root_ / ".microagenda" / "agenda.sqlite";
  return migrate();
}

const std::filesystem::path& AgendaStore::root() const {
  return root_;
}

const std::filesystem::path& AgendaStore::dbPath() const {
  return dbPath_;
}

bool AgendaStore::migrate() {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  const bool ok =
    exec(db, "PRAGMA journal_mode=WAL;") &&
    exec(db, "PRAGMA synchronous=FULL;") &&
    exec(db, "PRAGMA foreign_keys=ON;") &&
    exec(db, "CREATE TABLE IF NOT EXISTS entries(id TEXT PRIMARY KEY, name TEXT NOT NULL UNIQUE, description TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL, updated_at TEXT NOT NULL);") &&
    exec(db, "CREATE TABLE IF NOT EXISTS attributes(id TEXT PRIMARY KEY, entry_id TEXT NOT NULL REFERENCES entries(id) ON DELETE CASCADE, key TEXT NOT NULL, value TEXT NOT NULL, position INTEGER NOT NULL);") &&
    exec(db, "CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY, entry_id TEXT NOT NULL REFERENCES entries(id) ON DELETE CASCADE, title TEXT NOT NULL DEFAULT '', body TEXT NOT NULL, date TEXT NOT NULL, time TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL, updated_at TEXT NOT NULL);") &&
    exec(db, "CREATE TABLE IF NOT EXISTS events(id TEXT PRIMARY KEY, entry_id TEXT NOT NULL REFERENCES entries(id) ON DELETE CASCADE, title TEXT NOT NULL, description TEXT NOT NULL, date TEXT NOT NULL, time TEXT, repeat_rule TEXT NOT NULL DEFAULT 'none');") &&
    exec(db, "CREATE INDEX IF NOT EXISTS attributes_entry_position ON attributes(entry_id, position);") &&
    exec(db, "CREATE INDEX IF NOT EXISTS notes_entry_created ON notes(entry_id, created_at DESC);") &&
    exec(db, "CREATE INDEX IF NOT EXISTS events_date ON events(date);") &&
    exec(db, "CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts USING fts5(id UNINDEXED, name, body);");
  bool needsSearchRebuild = false;
  if(ok) {
    needsSearchRebuild = exec(db, "ALTER TABLE entries ADD COLUMN description TEXT NOT NULL DEFAULT '';") || needsSearchRebuild;
    needsSearchRebuild = exec(db, "ALTER TABLE notes ADD COLUMN title TEXT NOT NULL DEFAULT '';") || needsSearchRebuild;
    needsSearchRebuild = exec(db, "ALTER TABLE notes ADD COLUMN date TEXT NOT NULL DEFAULT '';") || needsSearchRebuild;
    needsSearchRebuild = exec(db, "ALTER TABLE notes ADD COLUMN time TEXT NOT NULL DEFAULT '';") || needsSearchRebuild;
    needsSearchRebuild = execAndChanged(db, "UPDATE notes SET date=substr(created_at,1,10) WHERE date='';") || needsSearchRebuild;
    needsSearchRebuild = execAndChanged(db, "UPDATE notes SET time=substr(created_at,12,5) WHERE time='';") || needsSearchRebuild;
    int entryCount = 0;
    int ftsCount = 0;
    if(tableCount(db, "SELECT COUNT(*) FROM entries;", entryCount) && tableCount(db, "SELECT COUNT(*) FROM entries_fts;", ftsCount) && entryCount != ftsCount) {
      needsSearchRebuild = true;
    }
  }
  sqlite3_close(db);
  return ok && (!needsSearchRebuild || rebuildSearchIndex());
}

bool AgendaStore::rebuildSearchIndex() {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  bool ok = exec(db, "BEGIN IMMEDIATE; DELETE FROM entries_fts;");
  sqlite3_stmt* stmt = nullptr;
  if(ok && sqlite3_prepare_v2(db, "SELECT id FROM entries ORDER BY name COLLATE NOCASE;", -1, &stmt, nullptr) == SQLITE_OK) {
    while(sqlite3_step(stmt) == SQLITE_ROW) {
      ok = upsertSearchForEntry(db, columnText(stmt, 0));
      if(!ok) break;
    }
  }
  if(stmt) sqlite3_finalize(stmt);
  ok = ok && exec(db, "COMMIT;");
  if(!ok) exec(db, "ROLLBACK;");
  sqlite3_close(db);
  return ok;
}

std::optional<Entry> AgendaStore::createEntry(std::string name) {
  if(name.empty()) name = "Untitled";
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return std::nullopt;
  const auto id = generateId("entry-");
  const auto now = nowIso();
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "INSERT INTO entries(id,name,description,created_at,updated_at) VALUES(?,?,?,?,?);", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    bindText(stmt, 2, name);
    bindText(stmt, 3, "");
    bindText(stmt, 4, now);
    bindText(stmt, 5, now);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = upsertSearchForEntry(db, id);
  sqlite3_close(db);
  if(!ok) return std::nullopt;
  return Entry {id, name, "", now, now};
}

bool AgendaStore::renameEntry(std::string_view entryId, std::string name) {
  if(name.empty()) return false;
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "UPDATE entries SET name=?, updated_at=? WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    const auto now = nowIso();
    bindText(stmt, 1, name);
    bindText(stmt, 2, now);
    bindText(stmt, 3, entryId);
    ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = upsertSearchForEntry(db, entryId);
  sqlite3_close(db);
  return ok;
}

bool AgendaStore::updateEntryDescription(std::string_view entryId, std::string description) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "UPDATE entries SET description=?, updated_at=? WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, description);
    bindText(stmt, 2, nowIso());
    bindText(stmt, 3, entryId);
    ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = upsertSearchForEntry(db, entryId);
  sqlite3_close(db);
  return ok;
}

bool AgendaStore::deleteEntry(std::string_view entryId) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  exec(db, "PRAGMA foreign_keys=ON;");
  const auto noteIds = noteIdsForEntry(db, entryId);
  const auto deleteByEntry = [&](const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK;
    if(ok) {
      bindText(stmt, 1, entryId);
      ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if(stmt) sqlite3_finalize(stmt);
    return ok;
  };
  sqlite3_stmt* stmt = nullptr;
  bool ok = exec(db, "BEGIN IMMEDIATE;") &&
    deleteByEntry("DELETE FROM entries_fts WHERE id=?;") &&
    deleteByEntry("DELETE FROM attributes WHERE entry_id=?;") &&
    deleteByEntry("DELETE FROM notes WHERE entry_id=?;") &&
    deleteByEntry("DELETE FROM events WHERE entry_id=?;") &&
    sqlite3_prepare_v2(db, "DELETE FROM entries WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, entryId);
    ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0;
  }
  if(stmt) sqlite3_finalize(stmt);
  ok = ok && exec(db, "COMMIT;");
  if(!ok) exec(db, "ROLLBACK;");
  sqlite3_close(db);
  return ok && removeNoteAttachments(root_, noteIds);
}

std::optional<Entry> AgendaStore::entry(std::string_view entryId) const {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return std::nullopt;
  auto out = selectEntryBy(db, "SELECT id,name,description,created_at,updated_at FROM entries WHERE id=?;", entryId);
  sqlite3_close(db);
  return out;
}

std::vector<Entry> AgendaStore::entries() const {
  std::vector<Entry> out;
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return out;
  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(db, "SELECT id,name,description,created_at,updated_at FROM entries ORDER BY name COLLATE NOCASE;", -1, &stmt, nullptr) == SQLITE_OK) {
    while(sqlite3_step(stmt) == SQLITE_ROW) out.push_back(readEntry(stmt));
  }
  if(stmt) sqlite3_finalize(stmt);
  sqlite3_close(db);
  return out;
}

std::vector<SearchResult> AgendaStore::search(std::string_view query, SearchScope scope) const {
  std::vector<SearchResult> out;
  if(query.empty()) {
    for(const auto& item : entries()) out.push_back({item, {}, {}});
    return out;
  }
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return out;
  sqlite3_stmt* stmt = nullptr;
  const char* ftsSql = scope == SearchScope::Name
    ? "SELECT e.id,e.name,e.description,e.created_at,e.updated_at,'' FROM entries_fts f JOIN entries e ON e.id=f.id WHERE f.name MATCH ? ORDER BY e.name COLLATE NOCASE LIMIT 300;"
    : "SELECT e.id,e.name,e.description,e.created_at,e.updated_at,snippet(entries_fts,2,'','', '...', 12) FROM entries_fts f JOIN entries e ON e.id=f.id WHERE entries_fts MATCH ? ORDER BY rank LIMIT 300;";
  if(sqlite3_prepare_v2(db, ftsSql, -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, query);
    while(sqlite3_step(stmt) == SQLITE_ROW) out.push_back({readEntry(stmt), columnText(stmt, 5), {}});
  }
  if(stmt) sqlite3_finalize(stmt);
  if(out.empty()) {
    const auto like = "%" + lowerCopy(std::string(query)) + "%";
    const char* likeSql = scope == SearchScope::Name
      ? "SELECT id,name,description,created_at,updated_at,'' FROM entries WHERE lower(name) LIKE ? ORDER BY name COLLATE NOCASE LIMIT 300;"
      : "SELECT e.id,e.name,e.description,e.created_at,e.updated_at,'' FROM entries e LEFT JOIN entries_fts f ON f.id=e.id WHERE lower(e.name) LIKE ? OR lower(e.description) LIKE ? OR lower(f.body) LIKE ? ORDER BY e.name COLLATE NOCASE LIMIT 300;";
    if(sqlite3_prepare_v2(db, likeSql, -1, &stmt, nullptr) == SQLITE_OK) {
      bindText(stmt, 1, like);
      if(scope == SearchScope::All) {
        bindText(stmt, 2, like);
        bindText(stmt, 3, like);
      }
      while(sqlite3_step(stmt) == SQLITE_ROW) out.push_back({readEntry(stmt), columnText(stmt, 5), {}});
    }
    if(stmt) sqlite3_finalize(stmt);
  }
  std::vector<SearchResult> expanded;
  for(auto& result : out) {
    populateSearchPreviews(db, result, query, scope);
    if(result.previews.empty() && scope == SearchScope::Name) {
      expanded.push_back(std::move(result));
      continue;
    }
    if(result.previews.empty()) continue;
    for(auto& preview : result.previews) {
      expanded.push_back({result.entry, {}, {std::move(preview)}});
    }
  }
  out = std::move(expanded);
  sqlite3_close(db);
  return out;
}

std::optional<LoadedEntry> AgendaStore::loadEntry(std::string_view entryId, int notePage, int notesPerPage) const {
  auto selected = entry(entryId);
  if(!selected) return std::nullopt;
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return std::nullopt;
  LoadedEntry out;
  out.entry = *selected;
  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(db, "SELECT id,entry_id,key,value,position FROM attributes WHERE entry_id=? ORDER BY position,id;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, entryId);
    while(sqlite3_step(stmt) == SQLITE_ROW) out.attributes.push_back(readAttribute(stmt));
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;
  if(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM notes WHERE entry_id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, entryId);
    if(sqlite3_step(stmt) == SQLITE_ROW) out.noteCount = sqlite3_column_int(stmt, 0);
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;
  if(sqlite3_prepare_v2(db, "SELECT id,entry_id,title,body,date,time,created_at,updated_at FROM notes WHERE entry_id=? ORDER BY date DESC,time DESC,id DESC LIMIT ? OFFSET ?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, entryId);
    sqlite3_bind_int(stmt, 2, notesPerPage);
    sqlite3_bind_int(stmt, 3, std::max(0, notePage) * notesPerPage);
    while(sqlite3_step(stmt) == SQLITE_ROW) out.notes.push_back(readNote(stmt));
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;
  const auto today = parseDate(todayIso()).value_or(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
  if(sqlite3_prepare_v2(db, "SELECT id,entry_id,title,description,date,COALESCE(time,''),repeat_rule FROM events WHERE entry_id=? ORDER BY date,time,title;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, entryId);
    while(sqlite3_step(stmt) == SQLITE_ROW) {
      auto event = readEvent(stmt);
      if(shouldShowEvent(event, today)) out.events.push_back(std::move(event));
    }
  }
  if(stmt) sqlite3_finalize(stmt);
  sqlite3_close(db);
  return out;
}

std::optional<Attribute> AgendaStore::addAttribute(std::string_view entryId, std::string key, std::string value) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return std::nullopt;
  const auto id = generateId("attr-");
  const int position = nextPosition(db, entryId);
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "INSERT INTO attributes(id,entry_id,key,value,position) VALUES(?,?,?,?,?);", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    bindText(stmt, 2, entryId);
    bindText(stmt, 3, key);
    bindText(stmt, 4, value);
    sqlite3_bind_int(stmt, 5, position);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, entryId);
  sqlite3_close(db);
  if(!ok) return std::nullopt;
  return Attribute {id, std::string(entryId), key, value, position};
}

bool AgendaStore::updateAttribute(std::string_view id, std::string key, std::string value) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  std::string entryId;
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "SELECT entry_id FROM attributes WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    ok = sqlite3_step(stmt) == SQLITE_ROW;
    if(ok) entryId = columnText(stmt, 0);
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;
  if(ok && sqlite3_prepare_v2(db, "UPDATE attributes SET key=?, value=? WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, key);
    bindText(stmt, 2, value);
    bindText(stmt, 3, id);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  } else {
    ok = false;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, entryId);
  sqlite3_close(db);
  return ok;
}

bool AgendaStore::deleteAttribute(std::string_view id) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  std::string entryId;
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "SELECT entry_id FROM attributes WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    ok = sqlite3_step(stmt) == SQLITE_ROW;
    if(ok) entryId = columnText(stmt, 0);
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;
  if(ok && sqlite3_prepare_v2(db, "DELETE FROM attributes WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, id);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  } else {
    ok = false;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, entryId);
  sqlite3_close(db);
  return ok;
}

std::optional<Note> AgendaStore::addNote(std::string_view entryId, std::string title, std::string body, std::string date, std::string time) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return std::nullopt;
  const auto id = generateId("note-");
  const auto now = nowIso();
  if(date.empty()) date = now.substr(0, 10);
  if(time.empty()) time = timeFromIso(now);
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "INSERT INTO notes(id,entry_id,title,body,date,time,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?);", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    bindText(stmt, 2, entryId);
    bindText(stmt, 3, title);
    bindText(stmt, 4, body);
    bindText(stmt, 5, date);
    bindText(stmt, 6, time);
    bindText(stmt, 7, now);
    bindText(stmt, 8, now);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, entryId);
  sqlite3_close(db);
  if(!ok) return std::nullopt;
  return Note {id, std::string(entryId), title, body, date, time, now, now};
}

bool AgendaStore::updateNote(std::string_view id, std::string title, std::string body, std::string date, std::string time) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  std::string entryId;
  std::string createdAt;
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "SELECT entry_id,created_at FROM notes WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    ok = sqlite3_step(stmt) == SQLITE_ROW;
    if(ok) {
      entryId = columnText(stmt, 0);
      createdAt = columnText(stmt, 1);
    }
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;
  if(date.empty()) date = createdAt.size() >= 10 ? createdAt.substr(0, 10) : todayIso();
  if(time.empty()) time = timeFromIso(createdAt);
  if(ok && sqlite3_prepare_v2(db, "UPDATE notes SET title=?, body=?, date=?, time=?, updated_at=? WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, title);
    bindText(stmt, 2, body);
    bindText(stmt, 3, date);
    bindText(stmt, 4, time);
    bindText(stmt, 5, nowIso());
    bindText(stmt, 6, id);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  } else {
    ok = false;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, entryId);
  sqlite3_close(db);
  return ok;
}

bool AgendaStore::deleteNote(std::string_view id) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  std::string entryId;
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "SELECT entry_id FROM notes WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    ok = sqlite3_step(stmt) == SQLITE_ROW;
    if(ok) entryId = columnText(stmt, 0);
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;
  if(ok && sqlite3_prepare_v2(db, "DELETE FROM notes WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, id);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  } else {
    ok = false;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, entryId);
  sqlite3_close(db);
  return ok && removeNoteAttachments(root_, id);
}

std::optional<Event> AgendaStore::addEvent(std::string_view entryId, std::string title, std::string date, std::string time, RepeatRule repeat, std::string description) {
  if(title.empty() || !parseDate(date)) return std::nullopt;
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return std::nullopt;
  const auto id = generateId("event-");
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "INSERT INTO events(id,entry_id,title,description,date,time,repeat_rule) VALUES(?,?,?,?,?,?,?);", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    bindText(stmt, 2, entryId);
    bindText(stmt, 3, title);
    bindText(stmt, 4, description);
    bindText(stmt, 5, date);
    bindText(stmt, 6, time);
    bindText(stmt, 7, repeatRuleToString(repeat));
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, entryId);
  sqlite3_close(db);
  if(!ok) return std::nullopt;
  return Event {id, std::string(entryId), title, description, date, time, repeat};
}

bool AgendaStore::updateEvent(const Event& event) {
  if(event.title.empty() || !parseDate(event.date)) return false;
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "UPDATE events SET title=?,description=?,date=?,time=?,repeat_rule=? WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, event.title);
    bindText(stmt, 2, event.description);
    bindText(stmt, 3, event.date);
    bindText(stmt, 4, event.time);
    bindText(stmt, 5, repeatRuleToString(event.repeat));
    bindText(stmt, 6, event.id);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, event.entryId);
  sqlite3_close(db);
  return ok;
}

bool AgendaStore::deleteEvent(std::string_view id) {
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return false;
  std::string entryId;
  sqlite3_stmt* stmt = nullptr;
  bool ok = sqlite3_prepare_v2(db, "SELECT entry_id FROM events WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK;
  if(ok) {
    bindText(stmt, 1, id);
    ok = sqlite3_step(stmt) == SQLITE_ROW;
    if(ok) entryId = columnText(stmt, 0);
  }
  if(stmt) sqlite3_finalize(stmt);
  stmt = nullptr;
  if(ok && sqlite3_prepare_v2(db, "DELETE FROM events WHERE id=?;", -1, &stmt, nullptr) == SQLITE_OK) {
    bindText(stmt, 1, id);
    ok = sqlite3_step(stmt) == SQLITE_DONE;
  } else {
    ok = false;
  }
  if(stmt) sqlite3_finalize(stmt);
  if(ok) ok = refreshEntry(db, entryId);
  sqlite3_close(db);
  return ok;
}

std::vector<UpcomingEvent> AgendaStore::upcoming(std::string today, int days) const {
  std::vector<UpcomingEvent> out;
  const auto start = parseDate(today);
  if(!start || days <= 0) return out;
  sqlite3* db = nullptr;
  if(sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return out;
  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(db,
    "SELECT ev.id,ev.entry_id,ev.title,ev.description,ev.date,COALESCE(ev.time,''),ev.repeat_rule,e.id,e.name,e.description,e.created_at,e.updated_at "
    "FROM events ev JOIN entries e ON e.id=ev.entry_id;",
    -1, &stmt, nullptr) == SQLITE_OK) {
    while(sqlite3_step(stmt) == SQLITE_ROW) {
      const auto event = readEvent(stmt, 0);
      const auto entry = readEntry(stmt, 7);
      for(int i = 0; i < days; ++i) {
        const auto target = *start + std::chrono::days {i};
        if(occursOn(event, target)) out.push_back({event, entry, formatDate(target)});
      }
    }
  }
  if(stmt) sqlite3_finalize(stmt);
  sqlite3_close(db);
  std::sort(out.begin(), out.end(), [](const UpcomingEvent& a, const UpcomingEvent& b) {
    return std::tie(a.occurrenceDate, a.event.time, a.event.title, a.entry.name) < std::tie(b.occurrenceDate, b.event.time, b.event.title, b.entry.name);
  });
  return out;
}

}
