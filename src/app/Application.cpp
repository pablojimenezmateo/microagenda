#include "app/Application.h"

#include "attachments/AttachmentService.h"
#include "agenda/AgendaStore.h"
#include "editor/MarkdownEditor.h"
#include "editor/SoftWrap.h"
#include "markdown/MarkdownParser.h"
#include "platform/DurableFile.h"
#include "ui/AppState.h"
#include "ui/ShellModel.h"

#include <SDL3/SDL.h>
#if MICROAGENDA_HAS_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#if MICROAGENDA_HAS_SDL3_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace microagenda::app {
namespace {

constexpr int kNotesPerPage = 10;
constexpr SDL_Color kText {238, 241, 244, 255};
constexpr SDL_Color kMuted {157, 165, 174, 255};
constexpr SDL_Color kDim {97, 106, 117, 255};
constexpr SDL_Color kAccent {114, 230, 198, 255};
constexpr SDL_Color kAccentDim {50, 124, 111, 255};
constexpr SDL_Color kWarn {239, 145, 104, 255};
constexpr SDL_Color kAppBg {9, 11, 14, 255};
constexpr SDL_Color kSidebarBg {16, 18, 23, 255};
constexpr SDL_Color kContentBg {12, 14, 18, 255};
constexpr SDL_Color kSurface {22, 25, 31, 255};
constexpr SDL_Color kSurfaceElevated {28, 32, 39, 255};
constexpr SDL_Color kHairline {30, 34, 41, 255};
constexpr SDL_Color kDivider {43, 48, 57, 255};
constexpr SDL_Color kInputBg {11, 13, 17, 255};
constexpr SDL_Color kHoverBg {32, 38, 47, 255};
constexpr SDL_Color kSelectedBg {31, 46, 50, 255};
constexpr float kIconButtonSize = 16.0f;

enum class FocusArea {
  Entries,
  Search,
  Content,
  EntryName,
  AttributeKey,
  AttributeValue,
  EntryDescription,
  NoteTitle,
  NoteDate,
  NoteTime,
  NoteEditor,
  EventTitle,
  EventDate,
  EventTime,
  EventDescription
};

enum class EditKind {
  None,
  EntryName,
  Attribute,
  EntryDescription,
  Note,
  Event
};

struct Rect {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;
};

static bool contains(Rect rect, float x, float y) {
  return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

static SDL_FRect sdlRect(Rect rect) {
  return {rect.x, rect.y, rect.w, rect.h};
}

class ClipGuard {
public:
  ClipGuard(SDL_Renderer* renderer, Rect rect) : renderer_(renderer) {
    hadPrevious_ = SDL_RenderClipEnabled(renderer_);
    if(hadPrevious_) SDL_GetRenderClipRect(renderer_, &previous_);
    clip_ = {
      static_cast<int>(std::floor(rect.x)),
      static_cast<int>(std::floor(rect.y)),
      static_cast<int>(std::ceil(rect.w)),
      static_cast<int>(std::ceil(rect.h)),
    };
    SDL_SetRenderClipRect(renderer_, &clip_);
  }

  ~ClipGuard() {
    SDL_SetRenderClipRect(renderer_, hadPrevious_ ? &previous_ : nullptr);
  }

private:
  SDL_Renderer* renderer_ = nullptr;
  SDL_Rect previous_ {};
  SDL_Rect clip_ {};
  bool hadPrevious_ = false;
};

static void fill(SDL_Renderer* renderer, Rect rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  auto out = sdlRect(rect);
  SDL_RenderFillRect(renderer, &out);
}

static void stroke(SDL_Renderer* renderer, Rect rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  auto out = sdlRect(rect);
  SDL_RenderRect(renderer, &out);
}

static void line(SDL_Renderer* renderer, float x1, float x2, float y, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderLine(renderer, x1, y, x2, y);
}

static void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color bg = kSurface, SDL_Color border = kHairline) {
  fill(renderer, rect, bg);
  stroke(renderer, rect, border);
}

static int maxScrollFor(float contentHeight, float viewportHeight) {
  return std::max(0, static_cast<int>(std::ceil(contentHeight - viewportHeight)));
}

static void drawVerticalScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0 || viewport.h <= 0) return;
  Rect track {viewport.x + viewport.w - 7.0f, viewport.y + 9.0f, 3.0f, std::max(24.0f, viewport.h - 18.0f)};
  const float visibleRatio = std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), 0.08f, 1.0f);
  const float thumbH = std::max(22.0f, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  Rect thumb {track.x - 1.0f, track.y + (track.h - thumbH) * t, 5.0f, thumbH};
  fill(renderer, track, SDL_Color {32, 37, 45, 210});
  fill(renderer, thumb, kAccentDim);
  stroke(renderer, thumb, SDL_Color {89, 154, 142, 180});
}

class TextRenderer {
public:
  explicit TextRenderer(SDL_Renderer* renderer) : renderer_(renderer) {
#if MICROAGENDA_HAS_SDL3_TTF
    ttfReady_ = TTF_Init();
    if(ttfReady_) {
      regular_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15.0f);
      mono_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 15.0f);
      heading_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 21.0f);
      bold_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15.0f);
      italic_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf", 15.0f);
      boldItalic_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf", 15.0f);
      if(!regular_) regular_ = mono_;
      if(!mono_) mono_ = regular_;
      if(!heading_) heading_ = regular_;
      if(!bold_) bold_ = regular_;
      if(!italic_) italic_ = regular_;
      if(!boldItalic_) boldItalic_ = bold_;
    }
#endif
  }

  ~TextRenderer() {
    clear();
#if MICROAGENDA_HAS_SDL3_TTF
    if(boldItalic_ && boldItalic_ != bold_ && boldItalic_ != italic_ && boldItalic_ != regular_ && boldItalic_ != mono_ && boldItalic_ != heading_) TTF_CloseFont(boldItalic_);
    if(italic_ && italic_ != regular_ && italic_ != mono_ && italic_ != heading_) TTF_CloseFont(italic_);
    if(bold_ && bold_ != regular_ && bold_ != mono_ && bold_ != heading_) TTF_CloseFont(bold_);
    if(heading_ && heading_ != regular_ && heading_ != mono_) TTF_CloseFont(heading_);
    if(mono_ && mono_ != regular_) TTF_CloseFont(mono_);
    if(regular_) TTF_CloseFont(regular_);
    if(ttfReady_) TTF_Quit();
#endif
  }

  int lineHeight(bool heading = false) const {
#if MICROAGENDA_HAS_SDL3_TTF
    TTF_Font* font = heading ? heading_ : regular_;
    if(font) return TTF_GetFontHeight(font) + 2;
#else
    (void)heading;
#endif
    return 16;
  }

  int width(std::string_view text, bool heading = false, bool mono = false, bool strong = false, bool emphasis = false) const {
    if(text.empty()) return 0;
#if MICROAGENDA_HAS_SDL3_TTF
    TTF_Font* font = fontFor(heading, mono, strong, emphasis);
    if(ttfReady_ && font) {
      int w = 0;
      int h = 0;
      if(TTF_GetStringSize(font, text.data(), text.size(), &w, &h)) return w;
    }
#else
    (void)heading;
    (void)mono;
    (void)strong;
    (void)emphasis;
#endif
    return static_cast<int>(text.size() * 8);
  }

  int height(std::string_view text, bool heading = false, bool mono = false, bool bold = false) const {
    if(text.empty()) return 0;
#if MICROAGENDA_HAS_SDL3_TTF
    TTF_Font* font = heading ? heading_ : mono ? mono_ : bold ? bold_ : regular_;
    if(ttfReady_ && font) {
      int w = 0;
      int h = 0;
      if(TTF_GetStringSize(font, text.data(), text.size(), &w, &h)) return h;
    }
#else
    (void)heading;
    (void)mono;
    (void)bold;
#endif
    return lineHeight(heading);
  }

  void clear() {
    for(auto& [_, texture] : cache_) SDL_DestroyTexture(texture.texture);
    cache_.clear();
  }

  void draw(std::string_view text, float x, float y, SDL_Color color = kText, bool heading = false, bool mono = false, bool strong = false, bool emphasis = false) {
    if(text.empty()) return;
    const std::string value(text);
    x = std::round(x);
    y = std::round(y);
#if MICROAGENDA_HAS_SDL3_TTF
    TTF_Font* font = fontFor(heading, mono, strong, emphasis);
    if(ttfReady_ && font) {
      if(cache_.size() > 4096) clear();
      const auto key = std::to_string(color.r) + ":" + std::to_string(color.g) + ":" + std::to_string(color.b) + ":" +
        (heading ? "h:" : mono ? "m:" : strong && emphasis ? "bi:" : strong ? "b:" : emphasis ? "i:" : "r:") + value;
      auto found = cache_.find(key);
      if(found == cache_.end()) {
        SDL_Surface* surface = TTF_RenderText_Blended(font, value.c_str(), value.size(), color);
        if(!surface) return;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        CachedText cached {texture, surface->w, surface->h};
        SDL_DestroySurface(surface);
        if(!texture) return;
        found = cache_.emplace(key, cached).first;
      }
      SDL_FRect dst {x, y, static_cast<float>(found->second.w), static_cast<float>(found->second.h)};
      SDL_RenderTexture(renderer_, found->second.texture, nullptr, &dst);
      return;
    }
#else
    (void)heading;
    (void)mono;
    (void)strong;
    (void)emphasis;
#endif
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer_, x, y, value.c_str());
  }

private:
  struct CachedText {
    SDL_Texture* texture = nullptr;
    int w = 0;
    int h = 0;
  };

  SDL_Renderer* renderer_ = nullptr;
  std::map<std::string, CachedText> cache_;
#if MICROAGENDA_HAS_SDL3_TTF
  TTF_Font* fontFor(bool heading, bool mono, bool strong, bool emphasis) const {
    if(heading) return heading_;
    if(mono) return mono_;
    if(strong && emphasis) return boldItalic_;
    if(strong) return bold_;
    if(emphasis) return italic_;
    return regular_;
  }

  bool ttfReady_ = false;
  TTF_Font* regular_ = nullptr;
  TTF_Font* mono_ = nullptr;
  TTF_Font* heading_ = nullptr;
  TTF_Font* bold_ = nullptr;
  TTF_Font* italic_ = nullptr;
  TTF_Font* boldItalic_ = nullptr;
#endif
};

class ImageCache {
public:
  explicit ImageCache(SDL_Renderer* renderer) : renderer_(renderer) {}

  ~ImageCache() {
    clear();
  }

  void clear() {
    for(auto& [_, image] : cache_) SDL_DestroyTexture(image.texture);
    cache_.clear();
  }

  SDL_Texture* load(const std::filesystem::path& path, float& width, float& height) {
#if MICROAGENDA_HAS_SDL3_IMAGE
    const auto key = path.string();
    auto found = cache_.find(key);
    if(found == cache_.end()) {
      SDL_Texture* texture = IMG_LoadTexture(renderer_, key.c_str());
      if(!texture) return nullptr;
      CachedImage image {texture, 0.0f, 0.0f};
      SDL_GetTextureSize(texture, &image.w, &image.h);
      if(cache_.size() > 256) clear();
      found = cache_.emplace(key, image).first;
    }
    width = found->second.w;
    height = found->second.h;
    return found->second.texture;
#else
    (void)path;
    (void)width;
    (void)height;
    return nullptr;
#endif
  }

private:
  struct CachedImage {
    SDL_Texture* texture = nullptr;
    float w = 0;
    float h = 0;
  };

  SDL_Renderer* renderer_ = nullptr;
  std::map<std::string, CachedImage> cache_;
};

struct ButtonRegion {
  Rect rect;
  std::string id;
};

struct LinkRegion {
  Rect rect;
  std::string target;
};

struct NoteRegion {
  Rect rect;
  std::string id;
};

struct FieldRegion {
  Rect rect;
  FocusArea focus;
};

struct EntryRegion {
  Rect rect;
  std::string id;
};

struct SearchResultRegion {
  Rect rect;
  agenda::SearchResult result;
};

struct PopupMenuState {
  bool open = false;
  float x = 0;
  float y = 0;
  std::string targetId;
};

struct UiRuntime {
  ui::AppState state;
  markdown::MarkdownParser parser;
  editor::MarkdownEditor editor;
  std::string editorTargetId;
  std::string cachedEditorRowsSource;
  int cachedEditorRowsWidth = -1;
  std::vector<editor::SoftWrapRow> cachedEditorRows;
  FocusArea focus = FocusArea::Entries;
  EditKind editKind = EditKind::None;
  bool creatingEntry = false;
  std::string searchDraft;
  agenda::SearchScope searchScope = agenda::SearchScope::All;
  std::string draftA;
  std::string draftB;
  std::string draftC;
  agenda::RepeatRule draftRepeat = agenda::RepeatRule::None;
  std::string status;
  bool activeEditDirty = false;
  bool recoveredEdit = false;
  Uint64 lastEdit = 0;
  Uint64 lastAutosaveAttempt = 0;
  int leftScroll = 0;
  int contentScroll = 0;
  int leftMaxScroll = 0;
  int contentMaxScroll = 0;
  int noteEditorScroll = 0;
  bool revealNoteEditorCursor = true;
  Rect searchRect;
  Rect searchScopeToggle;
  Rect entryHeaderRect;
  Rect entryDescriptionRect;
  std::vector<ButtonRegion> buttons;
  std::vector<FieldRegion> fields;
  std::vector<EntryRegion> attributes;
  std::vector<EntryRegion> events;
  std::vector<EntryRegion> entries;
  std::vector<EntryRegion> upcoming;
  std::vector<NoteRegion> notes;
  PopupMenuState noteMenu;
  PopupMenuState entryMenu;
  PopupMenuState attributeMenu;
  PopupMenuState eventMenu;
  std::vector<SearchResultRegion> searchResults;
  agenda::SearchTargetKind flashKind = agenda::SearchTargetKind::Entry;
  std::string flashTargetId;
  uint64_t flashStartedAt = 0;
  std::string selectedSearchTargetId;
  bool selectingEditorText = false;
  std::size_t editorSelectionAnchor = 0;
};

static void clampScrolls(UiRuntime& ui) {
  ui.leftScroll = std::clamp(ui.leftScroll, 0, ui.leftMaxScroll);
  ui.contentScroll = std::clamp(ui.contentScroll, 0, ui.contentMaxScroll);
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

static std::string currentTimeIso() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t raw = std::chrono::system_clock::to_time_t(now);
  std::tm tm {};
  localtime_r(&raw, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%H:%M");
  return out.str();
}

static bool isEditorFocus(FocusArea focus) {
  return focus == FocusArea::NoteEditor || focus == FocusArea::EventDescription || focus == FocusArea::EntryDescription;
}

static std::string ellipsize(std::string value, std::size_t limit) {
  if(value.size() <= limit) return value;
  if(limit <= 3) return value.substr(0, limit);
  return value.substr(0, limit - 3) + "...";
}

static std::string ellipsizeToWidth(TextRenderer& text, std::string value, int width, bool heading = false, bool mono = false) {
  if(text.width(value, heading, mono) <= width) return value;
  while(!value.empty() && text.width(value + "...", heading, mono) > width) value.pop_back();
  return value + "...";
}

static std::string searchPreviewText(const agenda::SearchResult& result) {
  if(!result.previews.empty()) return result.previews.front().label + ": " + result.previews.front().text;
  return result.matchText;
}

static const agenda::SearchPreview* searchPreview(const agenda::SearchResult& result) {
  return result.previews.empty() ? nullptr : &result.previews.front();
}

static std::string searchResultKey(const agenda::SearchResult& result) {
  if(const auto* preview = searchPreview(result)) {
    return std::to_string(static_cast<int>(preview->targetKind)) + ":" + preview->targetId + ":" + preview->label + ":" + preview->text;
  }
  return "entry:" + result.entry.id;
}

static bool flashVisible(const UiRuntime& ui, agenda::SearchTargetKind kind, std::string_view id) {
  if(ui.flashTargetId.empty() || ui.flashKind != kind || ui.flashTargetId != id) return false;
  const uint64_t elapsed = SDL_GetTicks() - ui.flashStartedAt;
  if(elapsed >= 1200) return false;
  return (elapsed / 200) % 2 == 0;
}

static bool flashAnimating(const UiRuntime& ui) {
  return !ui.flashTargetId.empty() && SDL_GetTicks() - ui.flashStartedAt < 1200;
}

static bool isRemoteTarget(std::string_view target) {
  return target.starts_with("http://") || target.starts_with("https://");
}

static std::string fileNameForMime(std::string_view mime) {
  if(mime == "image/png") return "clipboard.png";
  if(mime == "image/jpeg" || mime == "image/jpg") return "clipboard.jpg";
  if(mime == "image/bmp") return "clipboard.bmp";
  if(mime == "image/webp") return "clipboard.webp";
  return "clipboard-image";
}

static std::vector<std::string> wrapText(TextRenderer& text, std::string_view input, int width, bool heading = false, bool mono = false) {
  std::vector<std::string> out;
  if(width <= 0) {
    out.emplace_back(input);
    return out;
  }
  std::istringstream logicalLines {std::string(input)};
  std::string logicalLine;
  while(std::getline(logicalLines, logicalLine)) {
    if(logicalLine.empty()) {
      out.emplace_back();
      continue;
    }
    std::string lineText;
    std::istringstream words {logicalLine};
    std::string word;
    while(words >> word) {
      const auto candidate = lineText.empty() ? word : lineText + " " + word;
      if(!lineText.empty() && text.width(candidate, heading, mono) > width) {
        out.push_back(lineText);
        lineText = word;
        while(text.width(lineText, heading, mono) > width && lineText.size() > 1) {
          std::string chunk = lineText;
          while(chunk.size() > 1 && text.width(chunk, heading, mono) > width) chunk.pop_back();
          out.push_back(chunk);
          lineText.erase(0, chunk.size());
        }
      } else {
        lineText = candidate;
      }
    }
    out.push_back(lineText);
  }
  if(out.empty()) out.emplace_back();
  return out;
}

static std::vector<std::string> splitLines(std::string_view input) {
  std::vector<std::string> out;
  std::istringstream stream {std::string(input)};
  std::string lineText;
  while(std::getline(stream, lineText)) out.push_back(lineText);
  if(input.empty() || (!input.empty() && input.back() == '\n')) out.emplace_back();
  return out;
}

static std::string blockText(const markdown::Block& block) {
  std::string out;
  for(const auto& inlineItem : block.inlines) {
    if(inlineItem.type == markdown::InlineType::Image) {
      continue;
    } else if(inlineItem.type == markdown::InlineType::Link) {
      out += inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
    } else if(inlineItem.type == markdown::InlineType::Code) {
      out += block.type == markdown::BlockType::Code ? inlineItem.text : "`" + inlineItem.text + "`";
    } else if(inlineItem.type == markdown::InlineType::FootnoteRef) {
      out += "[" + inlineItem.text + "]";
    } else {
      out += inlineItem.text;
    }
  }
  return out;
}

static std::vector<std::string> codeBlockLines(const markdown::Block& block) {
  auto lines = splitLines(blockText(block));
  if(lines.size() > 1 && lines.back().empty()) lines.pop_back();
  if(lines.empty()) lines.emplace_back();
  return lines;
}

static std::vector<markdown::Inline> blockImages(const markdown::Block& block) {
  std::vector<markdown::Inline> out;
  for(const auto& inlineItem : block.inlines) {
    if(inlineItem.type == markdown::InlineType::Image) out.push_back(inlineItem);
  }
  return out;
}

struct InlineRun {
  std::string text;
  std::string target;
  SDL_Color color = kText;
  bool mono = false;
  bool strong = false;
  bool emphasis = false;
  bool strikethrough = false;
};

static std::vector<InlineRun> inlineRuns(const std::vector<markdown::Inline>& inlines, SDL_Color baseColor = kText) {
  std::vector<InlineRun> runs;
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    InlineRun run;
    run.text = inlineItem.text;
    run.color = baseColor;
    run.strong = inlineItem.strong;
    run.emphasis = inlineItem.emphasis;
    run.strikethrough = inlineItem.strikethrough;
    if(inlineItem.type == markdown::InlineType::Link) {
      run.text = inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
      run.target = inlineItem.target;
      run.color = kAccent;
    } else if(inlineItem.type == markdown::InlineType::Code) {
      run.mono = true;
      run.color = kWarn;
    } else if(inlineItem.type == markdown::InlineType::Emphasis) {
      run.emphasis = true;
    } else if(inlineItem.type == markdown::InlineType::Strong) {
      run.strong = true;
    } else if(inlineItem.type == markdown::InlineType::Strikethrough) {
      run.strikethrough = true;
    } else if(inlineItem.type == markdown::InlineType::Html) {
      run.color = kDim;
    } else if(inlineItem.type == markdown::InlineType::FootnoteRef) {
      run.text = "[" + inlineItem.text + "]";
      run.target = "#fn-" + inlineItem.text;
      run.color = kAccent;
    }
    if(!run.text.empty()) runs.push_back(std::move(run));
  }
  return runs;
}

static std::vector<InlineRun> inlineRuns(const markdown::Block& block, SDL_Color baseColor = kText) {
  return inlineRuns(block.inlines, baseColor);
}

static std::vector<InlineRun> splitRunWords(const InlineRun& run) {
  std::vector<InlineRun> out;
  std::string token;
  auto flush = [&]() {
    if(token.empty()) return;
    InlineRun item = run;
    item.text = token;
    out.push_back(std::move(item));
    token.clear();
  };
  for(char c : run.text) {
    if(c == '\n') {
      flush();
      InlineRun item = run;
      item.text = "\n";
      out.push_back(std::move(item));
    } else if(std::isspace(static_cast<unsigned char>(c))) {
      flush();
    } else {
      token.push_back(c);
    }
  }
  flush();
  return out;
}

static int measureInlineLines(TextRenderer& text, const std::vector<InlineRun>& runs, int maxWidth, bool heading = false) {
  int lines = 1;
  int x = 0;
  for(const auto& run : runs) {
    for(const auto& word : splitRunWords(run)) {
      if(word.text == "\n") {
        ++lines;
        x = 0;
        continue;
      }
      const int wordW = text.width(word.text, heading, word.mono, word.strong, word.emphasis);
      const int spaceW = x == 0 ? 0 : text.width(" ", heading, word.mono, word.strong, word.emphasis);
      if(x > 0 && x + spaceW + wordW > maxWidth) {
        ++lines;
        x = wordW;
      } else {
        x += spaceW + wordW;
      }
    }
  }
  return std::max(1, lines);
}

static float drawInlineRuns(SDL_Renderer* renderer, TextRenderer& text, std::vector<LinkRegion>* links, const std::vector<InlineRun>& runs, float x, float y, int maxWidth, int lineStep, bool heading = false) {
  float cursorX = x;
  float cursorY = y;
  for(const auto& run : runs) {
    for(const auto& word : splitRunWords(run)) {
      if(word.text == "\n") {
        cursorX = x;
        cursorY += static_cast<float>(lineStep);
        continue;
      }
      const int wordW = text.width(word.text, heading, word.mono, word.strong, word.emphasis);
      const int spaceW = cursorX == x ? 0 : text.width(" ", heading, word.mono, word.strong, word.emphasis);
      if(cursorX > x && cursorX + static_cast<float>(spaceW + wordW) > x + static_cast<float>(maxWidth)) {
        cursorX = x;
        cursorY += static_cast<float>(lineStep);
      } else if(cursorX > x) {
        cursorX += static_cast<float>(spaceW);
      }
      text.draw(word.text, cursorX, cursorY, word.color, heading, word.mono, word.strong, word.emphasis);
      if(word.strikethrough) {
        const float lineY = cursorY + static_cast<float>(text.lineHeight(heading)) * 0.55f;
        line(renderer, cursorX, cursorX + static_cast<float>(wordW), lineY, word.color);
      }
      if(!word.target.empty()) {
        if(links) links->push_back({{cursorX, cursorY, static_cast<float>(wordW), static_cast<float>(text.lineHeight(heading))}, word.target});
        line(renderer, cursorX, cursorX + static_cast<float>(wordW), cursorY + static_cast<float>(text.lineHeight(heading) - 2), kAccentDim);
      }
      cursorX += static_cast<float>(wordW);
    }
  }
  return cursorY;
}

static std::string inlinePlainText(const std::vector<markdown::Inline>& inlines) {
  std::string out;
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    if(inlineItem.type == markdown::InlineType::Link) out += inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
    else if(inlineItem.type == markdown::InlineType::FootnoteRef) out += "[" + inlineItem.text + "]";
    else out += inlineItem.text;
  }
  return out;
}

static float listMarkerWidth(const markdown::Block& block) {
  if(block.type == markdown::BlockType::OrderedItem) return 26.0f;
  if(block.type == markdown::BlockType::UnorderedItem) return 18.0f;
  return 0.0f;
}

static float blockBottomSpacing(const markdown::Block& block, bool heading, bool html) {
  if(block.type == markdown::BlockType::BlankLine) return 0.0f;
  if(block.type == markdown::BlockType::OrderedItem || block.type == markdown::BlockType::UnorderedItem) return 2.0f;
  return heading ? 14.0f : html ? 8.0f : 10.0f;
}

static float admonitionLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Admonition) return 0.0f;
  const auto label = block.admonitionType.empty() ? "note" : block.admonitionType;
  return static_cast<float>(text.width(label, false, false, true)) + 18.0f;
}

static float footnoteLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Footnote) return 0.0f;
  const auto label = "[" + (block.footnoteLabel.empty() ? std::string("*") : block.footnoteLabel) + "]";
  return static_cast<float>(text.width(label)) + 12.0f;
}

static std::string imagePlaceholder(const markdown::Inline& image, const UiRuntime& ui, attachments::AttachmentService& attachmentService) {
  if(isRemoteTarget(image.target) || !ui.state.hasAgenda()) return "[remote image skipped: " + image.target + "]";
  try {
    const auto path = attachmentService.resolveManaged(ui.state.agendaRoot(), image.target);
    if(!attachmentService.isSupportedImage(path)) return "[image link: " + image.target + "]";
  } catch(const std::exception&) {
    return "[unsafe image path]";
  }
  return "[image unavailable: " + image.target + "]";
}

static float imagePlaceholderHeight(TextRenderer& text, std::string_view placeholder, float width) {
  const auto lines = wrapText(text, placeholder, static_cast<int>(width), false, true);
  return static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight() + 2) + 8);
}

static float tableHeight(TextRenderer& text, const markdown::Block& block, float width) {
  const float rowPadY = 8.0f;
  int cols = 0;
  for(const auto& row : block.tableRows) cols = std::max(cols, static_cast<int>(row.cells.size()));
  cols = std::max(1, cols);
  const float cellW = std::max(48.0f, (width - static_cast<float>(cols + 1)) / static_cast<float>(cols));
  float h = 0.0f;
  for(const auto& row : block.tableRows) {
    int rowLines = 1;
    for(const auto& cell : row.cells) rowLines = std::max(rowLines, measureInlineLines(text, inlineRuns(cell.inlines), static_cast<int>(cellW - 14.0f)));
    h += static_cast<float>(rowLines * (text.lineHeight() + 2)) + rowPadY * 2.0f;
  }
  return h + 10.0f;
}

static void drawTable(SDL_Renderer* renderer, TextRenderer& text, std::vector<LinkRegion>& links, const markdown::Block& block, Rect rect) {
  int cols = 0;
  for(const auto& row : block.tableRows) cols = std::max(cols, static_cast<int>(row.cells.size()));
  if(cols <= 0) return;
  const float cellW = std::max(48.0f, (rect.w - static_cast<float>(cols + 1)) / static_cast<float>(cols));
  float y = rect.y;
  for(const auto& row : block.tableRows) {
    int rowLines = 1;
    for(const auto& cell : row.cells) rowLines = std::max(rowLines, measureInlineLines(text, inlineRuns(cell.inlines), static_cast<int>(cellW - 14.0f)));
    const float rowH = static_cast<float>(rowLines * (text.lineHeight() + 2)) + 16.0f;
    float x = rect.x;
    for(int i = 0; i < cols; ++i) {
      const markdown::TableCell* cell = i < static_cast<int>(row.cells.size()) ? &row.cells[static_cast<std::size_t>(i)] : nullptr;
      Rect cellRect {x, y, cellW, rowH};
      fill(renderer, cellRect, row.header ? SDL_Color {24, 30, 37, 255} : SDL_Color {16, 20, 25, 255});
      stroke(renderer, cellRect, kDivider);
      if(cell) {
        auto runs = inlineRuns(cell->inlines, row.header ? kText : kMuted);
        const auto cellText = inlinePlainText(cell->inlines);
        float textX = x + 7.0f;
        if(cell->align == markdown::Align::Right) textX = std::max(textX, x + cellW - 7.0f - static_cast<float>(text.width(cellText)));
        else if(cell->align == markdown::Align::Center) textX = std::max(textX, x + (cellW - static_cast<float>(text.width(cellText))) / 2.0f);
        drawInlineRuns(renderer, text, &links, runs, textX, y + 8.0f, static_cast<int>(cellW - 14.0f), text.lineHeight() + 2, false);
      }
      x += cellW;
    }
    y += rowH;
  }
}

static float measureMarkdown(TextRenderer& text, ImageCache& images, const UiRuntime& ui, const std::string& body, float width) {
  markdown::MarkdownParser parser;
  const auto doc = parser.parse(body);
  attachments::AttachmentService attachmentService;
  if(doc.blocks.empty()) return static_cast<float>(text.lineHeight());
  float total = 0.0f;
  for(const auto& block : doc.blocks) {
    const bool heading = block.type == markdown::BlockType::Heading;
    const bool code = block.type == markdown::BlockType::Code;
    const bool rule = block.type == markdown::BlockType::HorizontalRule;
    const bool table = block.type == markdown::BlockType::Table;
    const bool html = block.type == markdown::BlockType::Html;
    const bool blankLine = block.type == markdown::BlockType::BlankLine;
    const float indentW = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
    const float markerW = listMarkerWidth(block);
    const float quoteW = block.type == markdown::BlockType::Quote ? 16.0f : 0.0f;
    const int lineHeight = text.lineHeight(heading);
    if(blankLine) {
      total += static_cast<float>(text.lineHeight());
    } else if(rule) {
      total += 22.0f;
    } else if(table) {
      total += tableHeight(text, block, width - indentW) + 12.0f;
    } else if(code) {
      const auto lines = codeBlockLines(block);
      total += static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight(false) + 2)) + 18.0f;
    } else {
      const auto value = blockText(block);
      if(!value.empty()) {
        const auto runs = inlineRuns(block, heading ? kText : kText);
        const int lineStep = lineHeight + (heading ? 4 : 0);
        const float chromeW = markerW + quoteW + indentW + admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
        total += static_cast<float>(measureInlineLines(text, runs, static_cast<int>(width - chromeW), heading) * lineStep) + blockBottomSpacing(block, heading, html);
      }
    }
    for(const auto& image : blockImages(block)) {
      float imageW = 0;
      float imageH = 0;
      float renderedH = imagePlaceholderHeight(text, imagePlaceholder(image, ui, attachmentService), width);
      if(!isRemoteTarget(image.target) && ui.state.hasAgenda()) {
        try {
          const auto path = attachmentService.resolveManaged(ui.state.agendaRoot(), image.target);
          SDL_Texture* texture = images.load(path, imageW, imageH);
          if(texture && imageW > 0 && imageH > 0) {
            const float maxW = std::max(40.0f, std::min(width, 720.0f));
            const float scale = std::min(1.0f, maxW / imageW);
            renderedH = imageH * scale + 14.0f;
          }
        } catch(const std::exception&) {
        }
      }
      total += renderedH;
    }
  }
  return total;
}

static std::string scopeLabel(agenda::SearchScope scope) {
  return scope == agenda::SearchScope::All ? "A" : "N";
}

static agenda::SearchScope nextScope(agenda::SearchScope scope) {
  return scope == agenda::SearchScope::All ? agenda::SearchScope::Name : agenda::SearchScope::All;
}

static agenda::RepeatRule nextRepeat(agenda::RepeatRule repeat) {
  switch(repeat) {
    case agenda::RepeatRule::None: return agenda::RepeatRule::Daily;
    case agenda::RepeatRule::Daily: return agenda::RepeatRule::Weekly;
    case agenda::RepeatRule::Weekly: return agenda::RepeatRule::Monthly;
    case agenda::RepeatRule::Monthly: return agenda::RepeatRule::Yearly;
    case agenda::RepeatRule::Yearly: return agenda::RepeatRule::None;
  }
  return agenda::RepeatRule::None;
}

static void syncSearch(UiRuntime& ui) {
  ui.state.setSearch(ui.searchDraft, ui.searchScope);
}

static std::filesystem::path uiStatePath(const std::filesystem::path& root) {
  return root / ".microagenda" / "ui.state";
}

static std::string editKindName(EditKind kind) {
  switch(kind) {
    case EditKind::None: return "none";
    case EditKind::EntryName: return "entry_name";
    case EditKind::Attribute: return "attribute";
    case EditKind::EntryDescription: return "entry_description";
    case EditKind::Note: return "note";
    case EditKind::Event: return "event";
  }
  return "none";
}

static EditKind editKindFromName(const std::string& value) {
  if(value == "entry_name") return EditKind::EntryName;
  if(value == "attribute") return EditKind::Attribute;
  if(value == "entry_description") return EditKind::EntryDescription;
  if(value == "note") return EditKind::Note;
  if(value == "event") return EditKind::Event;
  return EditKind::None;
}

static std::filesystem::path activeEditRecoveryPath(const std::filesystem::path& root) {
  return root / ".microagenda" / "recovery" / "active-edit";
}

static bool writeActiveEditRecovery(UiRuntime& ui) {
  if(!ui.state.hasAgenda() || ui.editKind == EditKind::None) return true;
  std::ostringstream out;
  out << "kind " << std::quoted(editKindName(ui.editKind)) << "\n";
  out << "focus " << static_cast<int>(ui.focus) << "\n";
  out << "creating " << ui.creatingEntry << "\n";
  out << "entry " << std::quoted(ui.state.selection().entryId) << "\n";
  out << "target " << std::quoted(ui.editorTargetId) << "\n";
  out << "a " << std::quoted(ui.draftA) << "\n";
  out << "b " << std::quoted(ui.draftB) << "\n";
  out << "c " << std::quoted(ui.draftC) << "\n";
  out << "repeat " << std::quoted(agenda::repeatRuleToString(ui.draftRepeat)) << "\n";
  out << "body " << std::quoted(ui.editor.text()) << "\n";
  return platform::writeFileDurably(activeEditRecoveryPath(ui.state.agendaRoot()), out.str());
}

static bool clearActiveEditRecovery(UiRuntime& ui) {
  if(!ui.state.hasAgenda()) return true;
  return platform::removeFileDurably(activeEditRecoveryPath(ui.state.agendaRoot()));
}

static void markDraftChanged(UiRuntime& ui) {
  if(ui.editKind == EditKind::None) return;
  ui.activeEditDirty = true;
  ui.lastEdit = SDL_GetTicks();
  if(!writeActiveEditRecovery(ui)) ui.status = "Recovery save failed";
}

static bool restoreActiveEditRecovery(UiRuntime& ui) {
  const auto path = activeEditRecoveryPath(ui.state.agendaRoot());
  if(!std::filesystem::exists(path)) return false;
  std::ifstream in(path);
  if(!in) return false;
  std::string key;
  std::string value;
  while(in >> key) {
    if(key == "focus") {
      int focus = 0;
      in >> focus;
      ui.focus = static_cast<FocusArea>(focus);
    } else if(key == "creating") {
      in >> ui.creatingEntry;
    } else {
      in >> std::quoted(value);
      if(key == "kind") ui.editKind = editKindFromName(value);
      else if(key == "entry") ui.state.selectEntry(value);
      else if(key == "target") ui.editorTargetId = value;
      else if(key == "a") ui.draftA = value;
      else if(key == "b") ui.draftB = value;
      else if(key == "c") ui.draftC = value;
      else if(key == "repeat") ui.draftRepeat = agenda::repeatRuleFromString(value);
      else if(key == "body") ui.editor.setText(value);
    }
  }
  if(ui.editKind == EditKind::None) return false;
  ui.editor.markDirty();
  ui.activeEditDirty = true;
  ui.recoveredEdit = true;
  ui.state.shell().paneMode = ui::PaneMode::Editor;
  ui.status = "Recovered unsaved edit";
  return true;
}

static void beginEntryCreate(UiRuntime& ui) {
  ui.editKind = EditKind::EntryName;
  ui.creatingEntry = true;
  ui.focus = FocusArea::EntryName;
  ui.draftA.clear();
  ui.status = "Enter to create entry";
}

static void beginEntryRename(UiRuntime& ui, const agenda::LoadedEntry& entry) {
  ui.editKind = EditKind::EntryName;
  ui.creatingEntry = false;
  ui.focus = FocusArea::EntryName;
  ui.draftA = entry.entry.name;
  ui.status = "Enter to rename entry";
}

static void beginEntryDescriptionEdit(UiRuntime& ui, const agenda::LoadedEntry& entry) {
  ui.editKind = EditKind::EntryDescription;
  ui.focus = FocusArea::EntryDescription;
  ui.editorTargetId = entry.entry.id;
  ui.editor.setText(entry.entry.description);
  ui.noteEditorScroll = 0;
  ui.revealNoteEditorCursor = true;
  ui.state.shell().paneMode = ui::PaneMode::Editor;
  ui.status = "Editing entry description";
}

static void beginAttributeCreate(UiRuntime& ui) {
  ui.editKind = EditKind::Attribute;
  ui.focus = FocusArea::AttributeKey;
  ui.draftA.clear();
  ui.draftB.clear();
  ui.editorTargetId.clear();
  ui.status = "Edit attribute key/value";
}

static void beginAttributeEdit(UiRuntime& ui, const agenda::Attribute& attr) {
  ui.editKind = EditKind::Attribute;
  ui.focus = FocusArea::AttributeValue;
  ui.editorTargetId = attr.id;
  ui.draftA = attr.key;
  ui.draftB = attr.value;
  ui.status = "Edit attribute key/value";
}

static void beginNoteCreate(UiRuntime& ui) {
  ui.editKind = EditKind::Note;
  ui.focus = FocusArea::NoteTitle;
  ui.editorTargetId.clear();
  ui.draftA.clear();
  ui.draftB = todayIso();
  ui.draftC = currentTimeIso();
  ui.editor.setText("");
  ui.noteEditorScroll = 0;
  ui.revealNoteEditorCursor = true;
  ui.state.shell().paneMode = ui::PaneMode::Editor;
  ui.status = "Enter note, Ctrl+S to save";
}

static void beginNoteEdit(UiRuntime& ui, const agenda::Note& note) {
  ui.editKind = EditKind::Note;
  ui.focus = FocusArea::NoteEditor;
  ui.editorTargetId = note.id;
  ui.draftA = note.title;
  ui.draftB = note.date.empty() ? (note.createdAt.size() >= 10 ? note.createdAt.substr(0, 10) : todayIso()) : note.date;
  ui.draftC = note.time.empty() ? (note.createdAt.size() >= 16 ? note.createdAt.substr(11, 5) : currentTimeIso()) : note.time;
  ui.editor.setText(note.body);
  ui.noteEditorScroll = 0;
  ui.revealNoteEditorCursor = true;
  ui.state.shell().paneMode = ui::PaneMode::Editor;
  ui.status = "Editing note";
}

static void beginEventCreate(UiRuntime& ui) {
  ui.editKind = EditKind::Event;
  ui.focus = FocusArea::EventTitle;
  ui.editorTargetId.clear();
  ui.draftA.clear();
  ui.draftB = todayIso();
  ui.draftC.clear();
  ui.editor.setText("");
  ui.draftRepeat = agenda::RepeatRule::None;
  ui.status = "Edit event fields";
}

static void beginEventEdit(UiRuntime& ui, const agenda::Event& event) {
  ui.editKind = EditKind::Event;
  ui.focus = FocusArea::EventTitle;
  ui.editorTargetId = event.id;
  ui.draftA = event.title;
  ui.draftB = event.date;
  ui.draftC = event.time;
  ui.editor.setText(event.description);
  ui.draftRepeat = event.repeat;
  ui.status = "Editing event";
}


static bool canAutosaveActiveEdit(const UiRuntime& ui) {
  return ui.editKind == EditKind::EntryDescription || ui.editKind == EditKind::Note || ui.editKind == EditKind::Event;
}

static bool saveDraft(UiRuntime& ui, bool closeAfter = true, bool quiet = false) {
  bool saved = false;
  switch(ui.editKind) {
    case EditKind::EntryName:
      if(ui.creatingEntry) {
        auto created = ui.state.createEntry(ui.draftA);
        saved = created.has_value();
        if(!quiet) ui.status = saved ? "Created entry" : "Create failed";
      } else {
        saved = ui.state.renameSelectedEntry(ui.draftA);
        if(!quiet) ui.status = saved ? "Renamed entry" : "Rename failed";
      }
      break;
    case EditKind::Attribute:
      if(ui.editorTargetId.empty()) {
        auto attr = ui.state.addAttribute(ui.draftA, ui.draftB);
        saved = attr.has_value();
        if(saved) ui.editorTargetId = attr->id;
        if(!quiet) ui.status = saved ? "Added attribute" : "Add attribute failed";
      } else {
        saved = ui.state.updateAttribute(ui.editorTargetId, ui.draftA, ui.draftB);
        if(!quiet) ui.status = saved ? "Saved attribute" : "Save attribute failed";
      }
      break;
    case EditKind::EntryDescription:
      saved = ui.state.updateSelectedEntryDescription(ui.editor.text());
      if(saved) ui.editor.markSaved();
      if(!quiet) ui.status = saved ? "Saved description" : "Save description failed";
      break;
    case EditKind::Note:
      if(ui.editorTargetId.empty()) {
        if(ui.draftA.empty() && ui.editor.text().empty()) return false;
        auto note = ui.state.addNote(ui.draftA, ui.editor.text(), ui.draftB, ui.draftC);
        saved = note.has_value();
        if(saved) ui.editorTargetId = note->id;
        if(!quiet) ui.status = saved ? "Added note" : "Add note failed";
      } else {
        saved = ui.state.updateNote(ui.editorTargetId, ui.draftA, ui.editor.text(), ui.draftB, ui.draftC);
        if(!quiet) ui.status = saved ? "Saved note" : "Save note failed";
      }
      if(saved) ui.editor.markSaved();
      break;
    case EditKind::Event:
      if(ui.editorTargetId.empty()) {
        auto event = ui.state.addEvent(ui.draftA, ui.draftB, ui.draftC, ui.draftRepeat, ui.editor.text());
        saved = event.has_value();
        if(saved) ui.editorTargetId = event->id;
        if(!quiet) ui.status = saved ? "Added event" : "Add event failed";
      } else {
        agenda::Event event {ui.editorTargetId, ui.state.selection().entryId, ui.draftA, ui.editor.text(), ui.draftB, ui.draftC, ui.draftRepeat};
        saved = ui.state.updateEvent(event);
        if(!quiet) ui.status = saved ? "Saved event" : "Save event failed";
      }
      if(saved) ui.editor.markSaved();
      break;
    case EditKind::None:
      return false;
  }
  if(!saved) return false;
  ui.activeEditDirty = false;
  ui.recoveredEdit = false;
  clearActiveEditRecovery(ui);
  if(closeAfter) {
    ui.editKind = EditKind::None;
    ui.creatingEntry = false;
    ui.activeEditDirty = false;
    clearActiveEditRecovery(ui);
    ui.focus = FocusArea::Content;
    ui.state.shell().paneMode = ui::PaneMode::Viewer;
  }
  return true;
}

static void insertAttachmentMarkdown(UiRuntime& ui, const attachments::AttachmentLink& link) {
  if(ui.editor.text().empty() || ui.editor.text().back() == '\n') ui.editor.insert(link.markdown + "\n");
  else ui.editor.insert("\n" + link.markdown + "\n");
  markDraftChanged(ui);
}

static bool ensurePersistedNoteForAttachment(UiRuntime& ui) {
  if(ui.editKind != EditKind::Note || ui.state.selection().entryId.empty()) {
    ui.status = "Select or edit a note before attaching";
    return false;
  }
  if(!ui.editorTargetId.empty()) return true;
  auto created = ui.state.addNote(ui.draftA, ui.editor.text(), ui.draftB, ui.draftC);
  if(!created) {
    ui.status = "Create note before attaching failed";
    return false;
  }
  ui.editorTargetId = created->id;
  ui.editor.markSaved();
  ui.activeEditDirty = false;
  clearActiveEditRecovery(ui);
  return true;
}

static bool setClipboardText(std::string_view value) {
  const std::string text {value};
  const bool clipboardOk = SDL_SetClipboardText(text.c_str());
  SDL_SetPrimarySelectionText(text.c_str());
  return clipboardOk;
}

static bool publishEditorPrimarySelection(UiRuntime& ui) {
  if(isEditorFocus(ui.focus) && ui.editor.hasSelection()) {
    const auto selected = ui.editor.selectedText();
    return SDL_SetPrimarySelectionText(selected.c_str());
  }
  return false;
}

static bool pasteClipboardImage(UiRuntime& ui) {
  static constexpr const char* kImageMimes[] = {"image/png", "image/jpeg", "image/jpg", "image/bmp", "image/webp"};
  const char* mime = nullptr;
  for(const char* candidate : kImageMimes) {
    if(SDL_HasClipboardData(candidate)) {
      mime = candidate;
      break;
    }
  }
  if(!mime) return false;
  if(!ensurePersistedNoteForAttachment(ui)) return true;

  size_t size = 0;
  void* data = SDL_GetClipboardData(mime, &size);
  if(!data || size == 0) {
    if(data) SDL_free(data);
    ui.status = "Clipboard image is empty";
    return true;
  }

  attachments::AttachmentService service;
  try {
    const auto link = service.attachBytes(ui.state.agendaRoot(), ui.editorTargetId, fileNameForMime(mime), data, size);
    SDL_free(data);
    insertAttachmentMarkdown(ui, link);
    ui.status = "Pasted image attachment";
    return true;
  } catch(const std::exception& error) {
    SDL_free(data);
    ui.status = "Paste image failed: " + std::string(error.what());
    return true;
  }
}

static bool pasteClipboardText(UiRuntime& ui) {
  if(!SDL_HasClipboardText()) return false;
  char* raw = SDL_GetClipboardText();
  if(!raw) return false;
  ui.editor.insert(raw);
  if(isEditorFocus(ui.focus)) ui.revealNoteEditorCursor = true;
  markDraftChanged(ui);
  SDL_free(raw);
  return true;
}

static bool pastePrimarySelectionText(UiRuntime& ui) {
  if(!SDL_HasPrimarySelectionText()) return false;
  char* raw = SDL_GetPrimarySelectionText();
  if(!raw) return false;
  ui.editor.insert(raw);
  if(isEditorFocus(ui.focus)) ui.revealNoteEditorCursor = true;
  markDraftChanged(ui);
  SDL_free(raw);
  return true;
}

static std::string* focusedDraft(UiRuntime& ui) {
  switch(ui.focus) {
    case FocusArea::Search: return &ui.searchDraft;
    case FocusArea::EntryName:
    case FocusArea::AttributeKey:
    case FocusArea::NoteTitle:
    case FocusArea::EventTitle:
      return &ui.draftA;
    case FocusArea::AttributeValue:
    case FocusArea::NoteDate:
    case FocusArea::EventDate:
      return &ui.draftB;
    case FocusArea::NoteTime:
    case FocusArea::EventTime:
      return &ui.draftC;
    default:
      return nullptr;
  }
}

static void handleText(UiRuntime& ui, const char* raw) {
  if(!raw) return;
  if(auto* draft = focusedDraft(ui)) {
    *draft += raw;
    if(ui.focus == FocusArea::Search) syncSearch(ui);
    else markDraftChanged(ui);
  } else if(isEditorFocus(ui.focus)) {
    ui.editor.insert(raw);
    ui.revealNoteEditorCursor = true;
    markDraftChanged(ui);
  }
}

static void cyclePane(UiRuntime& ui) {
  switch(ui.state.shell().paneMode) {
    case ui::PaneMode::Editor: ui.state.shell().paneMode = ui::PaneMode::Viewer; break;
    case ui::PaneMode::Viewer: ui.state.shell().paneMode = ui::PaneMode::Split; break;
    case ui::PaneMode::Split: ui.state.shell().paneMode = ui::PaneMode::Editor; break;
  }
}

static std::optional<Rect> activeEditorRect(const UiRuntime& ui) {
  if(!isEditorFocus(ui.focus)) return std::nullopt;
  for(const auto& field : ui.fields) {
    if(field.focus == ui.focus) return field.rect;
  }
  return std::nullopt;
}

static void moveEditorVisualLine(TextRenderer& text, UiRuntime& ui, Rect rect, int delta);

static void handleKey(TextRenderer& text, UiRuntime& ui, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mod) {
  const SDL_Keymod currentMod = SDL_GetModState();
  const bool ctrl = ((mod | currentMod) & SDL_KMOD_CTRL) != 0;
  const bool shift = ((mod | currentMod) & SDL_KMOD_SHIFT) != 0;
  const auto shortcut = [&](SDL_Keycode keycode, SDL_Scancode code) {
    return ctrl && (key == keycode || scancode == code);
  };

  if(shortcut(SDLK_N, SDL_SCANCODE_N)) {
    if(shift) beginNoteCreate(ui);
    else beginEntryCreate(ui);
  } else if(shortcut(SDLK_S, SDL_SCANCODE_S)) {
    saveDraft(ui);
  } else if(shortcut(SDLK_L, SDL_SCANCODE_L)) {
    cyclePane(ui);
  } else if(shortcut(SDLK_F, SDL_SCANCODE_F)) {
    ui.focus = FocusArea::Search;
    ui.status = "Search entries";
  } else if(shortcut(SDLK_A, SDL_SCANCODE_A)) {
    if(isEditorFocus(ui.focus)) {
      ui.editor.selectAll();
      publishEditorPrimarySelection(ui);
      ui.revealNoteEditorCursor = true;
    }
  } else if(shortcut(SDLK_C, SDL_SCANCODE_C)) {
    if(isEditorFocus(ui.focus) && ui.editor.hasSelection()) {
      setClipboardText(ui.editor.selectedText());
      ui.status = "Copied selection";
    }
  } else if(shortcut(SDLK_X, SDL_SCANCODE_X)) {
    if(isEditorFocus(ui.focus) && ui.editor.hasSelection()) {
      setClipboardText(ui.editor.selectedText());
      ui.editor.eraseSelection();
      ui.revealNoteEditorCursor = true;
      markDraftChanged(ui);
      ui.status = "Cut selection";
    }
  } else if(shortcut(SDLK_V, SDL_SCANCODE_V)) {
    if(isEditorFocus(ui.focus)) {
      // Decide by what is actually on the clipboard: image data wins (image
      // copies often also expose an incidental text/plain target), otherwise
      // paste text. Shift forces plain text even when an image is present.
      if(shift) {
        if(!pasteClipboardText(ui)) pasteClipboardImage(ui);
      } else {
        if(!pasteClipboardImage(ui)) pasteClipboardText(ui);
      }
    } else if(auto* draft = focusedDraft(ui); draft && SDL_HasClipboardText()) {
      char* raw = SDL_GetClipboardText();
      if(raw) {
        *draft += raw;
        SDL_free(raw);
        if(ui.focus == FocusArea::Search) syncSearch(ui);
      }
    }
  } else if(shortcut(SDLK_Z, SDL_SCANCODE_Z)) {
    if(isEditorFocus(ui.focus)) {
      if(ui.editor.undo()) markDraftChanged(ui);
      ui.revealNoteEditorCursor = true;
    }
  } else if(shortcut(SDLK_Y, SDL_SCANCODE_Y)) {
    if(isEditorFocus(ui.focus)) {
      if(ui.editor.redo()) markDraftChanged(ui);
      ui.revealNoteEditorCursor = true;
    }
  } else if(key == SDLK_ESCAPE) {
    if(ui.focus == FocusArea::Search || !ui.searchDraft.empty()) {
      ui.searchDraft.clear();
      ui.selectedSearchTargetId.clear();
      syncSearch(ui);
      ui.focus = FocusArea::Content;
      ui.status = "Cleared search";
      return;
    }
    ui.noteMenu.open = false;
    ui.entryMenu.open = false;
    ui.attributeMenu.open = false;
    ui.eventMenu.open = false;
    ui.editKind = EditKind::None;
    ui.creatingEntry = false;
    ui.activeEditDirty = false;
    clearActiveEditRecovery(ui);
    ui.focus = FocusArea::Content;
    ui.state.shell().paneMode = ui::PaneMode::Viewer;
    ui.status = "Cancelled";
  } else if(key == SDLK_RETURN) {
    if(ui.focus == FocusArea::Search) ui.focus = FocusArea::Entries;
    else if(isEditorFocus(ui.focus)) {
      ui.editor.insert("\n");
      ui.revealNoteEditorCursor = true;
      markDraftChanged(ui);
    }
    else if(ui.focus == FocusArea::AttributeKey) ui.focus = FocusArea::AttributeValue;
    else if(ui.focus == FocusArea::AttributeValue) saveDraft(ui);
    else if(ui.focus == FocusArea::EntryName) saveDraft(ui);
    else if(ui.focus == FocusArea::NoteTitle) ui.focus = FocusArea::NoteDate;
    else if(ui.focus == FocusArea::NoteDate) ui.focus = FocusArea::NoteTime;
    else if(ui.focus == FocusArea::NoteTime) ui.focus = FocusArea::NoteEditor;
    else if(ui.focus == FocusArea::EventTitle) ui.focus = FocusArea::EventDate;
    else if(ui.focus == FocusArea::EventDate) ui.focus = FocusArea::EventTime;
    else if(ui.focus == FocusArea::EventTime) ui.focus = FocusArea::EventDescription;
  } else if(key == SDLK_TAB) {
    if(isEditorFocus(ui.focus)) {
      ui.editor.insert("  ");
      ui.revealNoteEditorCursor = true;
      markDraftChanged(ui);
    }
    else if(ui.focus == FocusArea::NoteTitle) ui.focus = FocusArea::NoteDate;
    else if(ui.focus == FocusArea::NoteDate) ui.focus = FocusArea::NoteTime;
    else if(ui.focus == FocusArea::NoteTime) ui.focus = FocusArea::NoteEditor;
    else if(ui.focus == FocusArea::EventTitle) ui.focus = FocusArea::EventDate;
    else if(ui.focus == FocusArea::EventDate) ui.focus = FocusArea::EventTime;
    else if(ui.focus == FocusArea::EventTime) ui.focus = FocusArea::EventDescription;
    else if(ui.focus == FocusArea::EventDescription) ui.focus = FocusArea::EventTitle;
    else if(ui.focus == FocusArea::AttributeKey) ui.focus = FocusArea::AttributeValue;
    else if(ui.focus == FocusArea::AttributeValue) ui.focus = FocusArea::AttributeKey;
  } else if(key == SDLK_BACKSPACE) {
    if(auto* draft = focusedDraft(ui)) {
      if(!draft->empty()) { draft->pop_back(); if(ui.focus != FocusArea::Search) markDraftChanged(ui); }
      if(ui.focus == FocusArea::Search) syncSearch(ui);
    } else if(isEditorFocus(ui.focus)) {
      ui.editor.erasePrevious();
      ui.revealNoteEditorCursor = true;
      markDraftChanged(ui);
    }
  } else if(key == SDLK_DELETE) {
    if(isEditorFocus(ui.focus)) {
      ui.editor.eraseNext();
      ui.revealNoteEditorCursor = true;
      markDraftChanged(ui);
    }
  } else if(key == SDLK_LEFT) {
    if(isEditorFocus(ui.focus)) {
      ui.editor.moveLeft();
      ui.revealNoteEditorCursor = true;
    }
  } else if(key == SDLK_RIGHT) {
    if(isEditorFocus(ui.focus)) {
      ui.editor.moveRight();
      ui.revealNoteEditorCursor = true;
    }
  } else if(key == SDLK_UP) {
    if(isEditorFocus(ui.focus)) {
      if(auto rect = activeEditorRect(ui)) moveEditorVisualLine(text, ui, *rect, -1);
      else ui.editor.moveLineUp();
      ui.revealNoteEditorCursor = true;
    }
    else ui.leftScroll = std::max(0, ui.leftScroll - 24);
  } else if(key == SDLK_DOWN) {
    if(isEditorFocus(ui.focus)) {
      if(auto rect = activeEditorRect(ui)) moveEditorVisualLine(text, ui, *rect, 1);
      else ui.editor.moveLineDown();
      ui.revealNoteEditorCursor = true;
    }
    else ui.leftScroll = std::min(ui.leftMaxScroll, ui.leftScroll + 24);
  } else if(key == SDLK_HOME) {
    if(isEditorFocus(ui.focus)) {
      ui.editor.moveLineStart(shift);
      publishEditorPrimarySelection(ui);
      ui.revealNoteEditorCursor = true;
    }
  } else if(key == SDLK_END) {
    if(isEditorFocus(ui.focus)) {
      ui.editor.moveLineEnd(shift);
      publishEditorPrimarySelection(ui);
      ui.revealNoteEditorCursor = true;
    }
  } else if(key == SDLK_PAGEUP) {
    ui.contentScroll = std::max(0, ui.contentScroll - 320);
  } else if(key == SDLK_PAGEDOWN) {
    ui.contentScroll = std::min(ui.contentMaxScroll, ui.contentScroll + 320);
  }
}

static void drawButton(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect, std::string id, std::string_view label) {
  drawSurface(renderer, rect, kSurface, kHairline);
  text.draw(label, rect.x + 10, rect.y + 6, kText);
  ui.buttons.push_back({rect, std::move(id)});
}

static void drawIconButton(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect, std::string id, std::string_view label) {
  drawSurface(renderer, rect, kSurface, kHairline);
  const int glyphWidth = text.width(label, false, false, true);
  const int glyphHeight = text.height(label, false, false, true);
  const float glyphX = rect.x + std::floor((rect.w - static_cast<float>(glyphWidth)) / 2.0f);
  const float glyphY = rect.y + std::floor((rect.h - static_cast<float>(glyphHeight)) / 2.0f);
  text.draw(label, glyphX, glyphY, kText, false, false, true);
  ui.buttons.push_back({rect, std::move(id)});
}

static void drawSelection(SDL_Renderer* renderer, Rect rect, bool selected) {
  if(selected) {
    fill(renderer, rect, kSelectedBg);
    fill(renderer, {rect.x, rect.y, 3, rect.h}, kAccent);
    stroke(renderer, rect, kAccentDim);
  } else {
    line(renderer, rect.x + 8, rect.x + rect.w - 8, rect.y + rect.h, kHairline);
  }
}

static Rect noteMenuRect(int width, int height, const PopupMenuState& menu) {
  const float bottom = static_cast<float>(height) - 28.0f;
  return {
    std::min(menu.x, static_cast<float>(width) - 170.0f),
    std::min(menu.y, bottom - 100.0f),
    160,
    90,
  };
}

static Rect entryMenuRect(int width, int height, const PopupMenuState& menu) {
  const float bottom = static_cast<float>(height) - 28.0f;
  return {
    std::min(menu.x, static_cast<float>(width) - 160.0f),
    std::min(menu.y, bottom - 100.0f),
    150,
    90,
  };
}

static Rect rowActionMenuRect(int width, int height, const PopupMenuState& menu) {
  const float bottom = static_cast<float>(height) - 28.0f;
  return {
    std::min(menu.x, static_cast<float>(width) - 140.0f),
    std::min(menu.y, bottom - 70.0f),
    130,
    60,
  };
}

static void drawMarkdown(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, const std::string& body, Rect rect, float& y) {
  markdown::MarkdownParser parser;
  const auto doc = parser.parse(body);
  attachments::AttachmentService attachmentService;
  std::vector<LinkRegion> links;
  if(doc.blocks.empty()) {
    text.draw("Empty note", rect.x, y, kDim);
    y += static_cast<float>(text.lineHeight());
    return;
  }
  const float contentLeft = rect.x;
  const float contentWidth = std::max(80.0f, rect.w);
  int orderedIndex = 1;
  for(const auto& block : doc.blocks) {
    const bool heading = block.type == markdown::BlockType::Heading;
    const bool code = block.type == markdown::BlockType::Code;
    const bool ordered = block.type == markdown::BlockType::OrderedItem;
    const bool unordered = block.type == markdown::BlockType::UnorderedItem;
    const bool quote = block.type == markdown::BlockType::Quote;
    const bool rule = block.type == markdown::BlockType::HorizontalRule;
    const bool table = block.type == markdown::BlockType::Table;
    const bool admonition = block.type == markdown::BlockType::Admonition;
    const bool footnote = block.type == markdown::BlockType::Footnote;
    const bool blankLine = block.type == markdown::BlockType::BlankLine;
    if(!ordered) orderedIndex = 1;
    const float indentW = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
    const float markerW = listMarkerWidth(block);
    const float quoteW = quote ? 16.0f : 0.0f;
    const float extraW = admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
    const float textX = contentLeft + indentW + markerW + quoteW + extraW;
    const int lineHeight = text.lineHeight(heading);
    if(blankLine) {
      y += static_cast<float>(text.lineHeight());
    } else if(rule) {
      line(renderer, contentLeft + indentW, contentLeft + contentWidth, y + 8.0f, kDivider);
      line(renderer, contentLeft + indentW, contentLeft + contentWidth, y + 9.0f, kHairline);
      y += 22.0f;
    } else if(table) {
      const float blockH = tableHeight(text, block, contentWidth - indentW);
      drawTable(renderer, text, links, block, {contentLeft + indentW, y, contentWidth - indentW, blockH});
      y += blockH + 12.0f;
    } else if(code) {
      const auto lines = codeBlockLines(block);
      const float blockH = static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight(false) + 2)) + 10.0f;
      Rect codeRect {contentLeft + indentW, y - 6.0f, contentWidth - indentW, blockH};
      drawSurface(renderer, codeRect, SDL_Color {20, 24, 30, 255}, kHairline);
      float codeY = y;
      for(const auto& codeLine : lines) {
        text.draw(ellipsizeToWidth(text, codeLine, static_cast<int>(codeRect.w - 20.0f), false, true), codeRect.x + 10.0f, codeY, kWarn, false, true);
        codeY += static_cast<float>(text.lineHeight(false) + 2);
      }
      y += blockH + 8.0f;
    } else {
      const auto value = blockText(block);
      if(!value.empty()) {
        const auto runs = inlineRuns(block, heading ? kText : kText);
        const int lineStep = lineHeight + (heading ? 4 : 0);
        const float blockWidth = contentWidth - indentW - markerW - quoteW - extraW;
        const float blockH = static_cast<float>(measureInlineLines(text, runs, static_cast<int>(blockWidth), heading) * lineStep);
        if(quote) fill(renderer, {contentLeft + indentW, y - 2.0f, 3.0f, blockH + 2.0f}, kAccentDim);
        if(admonition) {
          Rect callout {contentLeft + indentW, y - 7.0f, contentWidth - indentW, blockH + 12.0f};
          drawSurface(renderer, callout, SDL_Color {18, 28, 29, 255}, kAccentDim);
          const auto label = block.admonitionType.empty() ? "note" : block.admonitionType;
          text.draw(label, callout.x + 10.0f, y, kAccent, false, false, true);
        }
        if(footnote) {
          const auto label = block.footnoteLabel.empty() ? "*" : block.footnoteLabel;
          text.draw("[" + label + "]", contentLeft + indentW, y, kAccent);
        }
        if(ordered) {
          const int number = block.orderedNumber > 0 ? block.orderedNumber : orderedIndex;
          text.draw(std::to_string(number) + ".", contentLeft + indentW, y, kMuted);
        } else if(unordered) {
          if(block.task) {
            Rect box {contentLeft + indentW, y + 3.0f, 12.0f, 12.0f};
            stroke(renderer, box, block.taskChecked ? kAccent : kMuted);
            if(block.taskChecked) {
              line(renderer, box.x + 2.0f, box.x + 5.0f, box.y + 7.0f, kAccent);
              line(renderer, box.x + 5.0f, box.x + 10.0f, box.y + 3.0f, kAccent);
            }
          } else {
            text.draw("-", contentLeft + indentW, y, kMuted);
          }
        }
        drawInlineRuns(renderer, text, &links, runs, textX, y, static_cast<int>(blockWidth), lineStep, heading);
        y += blockH + blockBottomSpacing(block, heading, false);
      }
      for(const auto& image : blockImages(block)) {
        float imageW = 0.0f;
        float imageH = 0.0f;
        SDL_Texture* texture = nullptr;
        std::string placeholder = imagePlaceholder(image, ui, attachmentService);
        if(!isRemoteTarget(image.target) && ui.state.hasAgenda()) {
          try {
            const auto path = attachmentService.resolveManaged(ui.state.agendaRoot(), image.target);
            if(attachmentService.isSupportedImage(path)) texture = images.load(path, imageW, imageH);
          } catch(const std::exception&) {
          }
        }
        if(texture && imageW > 0.0f && imageH > 0.0f) {
          const float maxW = std::max(40.0f, std::min(contentWidth, 720.0f));
          const float scale = std::min(1.0f, maxW / imageW);
          SDL_FRect dst {contentLeft, std::round(y), std::round(imageW * scale), std::round(imageH * scale)};
          SDL_RenderTexture(renderer, texture, nullptr, &dst);
          links.push_back({{dst.x, dst.y, dst.w, dst.h}, image.target});
          y += dst.h + 14.0f;
        } else {
          const auto lines = wrapText(text, placeholder, static_cast<int>(contentWidth), false, true);
          float placeholderY = y;
          const bool clickablePlaceholder = isRemoteTarget(image.target);
          for(const auto& lineText : lines) {
            const auto lineW = static_cast<float>(text.width(lineText, false, true));
            text.draw(lineText, contentLeft, placeholderY, clickablePlaceholder ? kAccent : kDim, false, true);
            if(clickablePlaceholder && lineW > 0.0f) {
              links.push_back({{contentLeft, placeholderY, lineW, static_cast<float>(text.lineHeight())}, image.target});
              line(renderer, contentLeft, contentLeft + lineW, placeholderY + static_cast<float>(text.lineHeight() - 2), kAccentDim);
            }
            placeholderY += static_cast<float>(text.lineHeight() + 2);
          }
          y = placeholderY + 8.0f;
        }
      }
    }
    if(ordered) ++orderedIndex;
  }
}

static editor::MeasureText editorMeasure(TextRenderer& text) {
  return [&text](std::string_view value) {
    return text.width(value, false, true);
  };
}

static Rect noteEditorWritingRect(Rect rect) {
  return {rect.x + 8.0f, rect.y + 8.0f, rect.w - 16.0f, rect.h - 16.0f};
}

static const std::vector<editor::SoftWrapRow>& editorRows(TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect writing = noteEditorWritingRect(rect);
  const int wrapWidth = static_cast<int>(std::max(1.0f, writing.w));
  const auto& source = ui.editor.text();
  if(ui.cachedEditorRowsWidth != wrapWidth || ui.cachedEditorRowsSource != source) {
    ui.cachedEditorRowsWidth = wrapWidth;
    ui.cachedEditorRowsSource = source;
    ui.cachedEditorRows = editor::softWrap(ui.cachedEditorRowsSource, wrapWidth, editorMeasure(text));
  }
  return ui.cachedEditorRows;
}

static std::size_t editorIndexAtPoint(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  const int lineHeight = text.lineHeight();
  const Rect writing = noteEditorWritingRect(rect);
  const int visibleLine = std::max(0, static_cast<int>((y - writing.y) / static_cast<float>(lineHeight)));
  const auto& rows = editorRows(text, ui, rect);
  const int rowIndex = std::clamp(ui.noteEditorScroll + visibleLine, 0, std::max(0, static_cast<int>(rows.size()) - 1));
  return editor::offsetForRowX(rows[static_cast<std::size_t>(rowIndex)], x - writing.x, editorMeasure(text));
}

static void placeEditorCursor(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  ui.editor.moveCursor(editorIndexAtPoint(text, ui, rect, x, y));
  ui.revealNoteEditorCursor = true;
}

static void selectWordAtCursor(UiRuntime& ui) {
  const auto& value = ui.editor.text();
  std::size_t cursor = std::min(ui.editor.cursor(), value.size());
  if(cursor > 0 && (cursor == value.size() || !std::isalnum(static_cast<unsigned char>(value[cursor])))) --cursor;
  std::size_t start = cursor;
  std::size_t end = cursor;
  while(start > 0 && (std::isalnum(static_cast<unsigned char>(value[start - 1])) || value[start - 1] == '_')) --start;
  while(end < value.size() && (std::isalnum(static_cast<unsigned char>(value[end])) || value[end] == '_')) ++end;
  ui.editor.selectRange(start, end);
}

static void selectLineAtCursor(UiRuntime& ui) {
  const auto& value = ui.editor.text();
  const auto cursor = std::min(ui.editor.cursor(), value.size());
  const auto lineStart = value.rfind('\n', cursor == 0 ? 0 : cursor - 1);
  const auto lineEnd = value.find('\n', cursor);
  const std::size_t start = lineStart == std::string::npos ? 0 : lineStart + 1;
  const std::size_t end = lineEnd == std::string::npos ? value.size() : lineEnd;
  ui.editor.selectRange(start, end);
}

static int noteEditorMaxScroll(TextRenderer& text, UiRuntime& ui, Rect rect) {
  const Rect writing = noteEditorWritingRect(rect);
  const int maxLines = std::max(1, static_cast<int>(writing.h / static_cast<float>(text.lineHeight())));
  return std::max(0, static_cast<int>(editorRows(text, ui, rect).size()) - maxLines);
}

static void moveEditorVisualLine(TextRenderer& text, UiRuntime& ui, Rect rect, int delta) {
  const auto& rows = editorRows(text, ui, rect);
  if(rows.empty()) return;
  const int currentRow = editor::rowForOffset(rows, ui.editor.cursor());
  const auto& current = rows[static_cast<std::size_t>(currentRow)];
  const auto cursorInRow = ui.editor.cursor() <= current.end ? ui.editor.cursor() - current.start : current.text.size();
  const float targetX = static_cast<float>(text.width(std::string_view(current.text.data(), std::min(cursorInRow, current.text.size())), false, true));
  const int targetRow = std::clamp(currentRow + delta, 0, static_cast<int>(rows.size()) - 1);
  ui.editor.moveCursor(editor::offsetForRowX(rows[static_cast<std::size_t>(targetRow)], targetX, editorMeasure(text)));
  ui.revealNoteEditorCursor = true;
}

static void drawNoteEditor(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  drawSurface(renderer, rect, kInputBg, isEditorFocus(ui.focus) ? kAccentDim : kHairline);
  const Rect writing = noteEditorWritingRect(rect);
  const int lineHeight = text.lineHeight();
  const int maxLines = std::max(1, static_cast<int>(writing.h / static_cast<float>(lineHeight)));
  const auto& rows = editorRows(text, ui, rect);
  const int cursorRow = editor::rowForOffset(rows, ui.editor.cursor());
  if(ui.revealNoteEditorCursor) {
    if(cursorRow < ui.noteEditorScroll) ui.noteEditorScroll = cursorRow;
    if(cursorRow >= ui.noteEditorScroll + maxLines) ui.noteEditorScroll = cursorRow - maxLines + 1;
  }
  const int maxScroll = noteEditorMaxScroll(text, ui, rect);
  ui.noteEditorScroll = std::clamp(ui.noteEditorScroll, 0, maxScroll);
  ui.revealNoteEditorCursor = false;
  {
    ClipGuard clip(renderer, writing);
    float lineY = writing.y;
    for(int i = ui.noteEditorScroll; i < static_cast<int>(rows.size()) && lineY < writing.y + writing.h; ++i) {
      const auto& row = rows[static_cast<std::size_t>(i)];
      const auto& lineText = row.text;
      if(ui.editor.hasSelection()) {
        const auto selStart = std::max(ui.editor.selectionStart(), row.start);
        const auto selEnd = std::min(ui.editor.selectionEnd(), row.end);
        if(selStart < selEnd) {
          const auto before = std::string_view(lineText.data(), selStart - row.start);
          const auto selected = std::string_view(lineText.data() + (selStart - row.start), selEnd - selStart);
          const float sx = writing.x + static_cast<float>(text.width(before, false, true));
          const float sw = static_cast<float>(text.width(selected, false, true));
          fill(renderer, {sx, lineY - 1.0f, std::min(sw, writing.x + writing.w - sx), static_cast<float>(lineHeight)}, SDL_Color {48, 82, 95, 210});
        }
      }
      text.draw(lineText.empty() ? " " : lineText, writing.x, lineY, kText, false, true);
      lineY += static_cast<float>(lineHeight);
    }
    if(isEditorFocus(ui.focus) && cursorRow >= ui.noteEditorScroll && cursorRow < ui.noteEditorScroll + maxLines) {
      std::string prefix;
      if(cursorRow >= 0 && cursorRow < static_cast<int>(rows.size())) {
        const auto& row = rows[static_cast<std::size_t>(cursorRow)];
        const auto cursorInRow = ui.editor.cursor() <= row.end ? ui.editor.cursor() - row.start : row.text.size();
        prefix = row.text.substr(0, std::min<std::size_t>(row.text.size(), cursorInRow));
      }
      const float cursorX = writing.x + static_cast<float>(text.width(prefix, false, true));
      const float cursorY = writing.y + static_cast<float>((cursorRow - ui.noteEditorScroll) * lineHeight);
      fill(renderer, {std::min(cursorX, writing.x + writing.w - 2.0f), cursorY, 2.0f, static_cast<float>(lineHeight - 2)}, kAccent);
    }
    if(ui.editor.text().empty()) text.draw("Start typing...", writing.x, writing.y, kDim);
  }
  drawVerticalScrollbar(renderer, writing, ui.noteEditorScroll, maxScroll);
}

static void drawLeft(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, kSidebarBg);
  ClipGuard clip(renderer, rect);
  ui.entries.clear();
  ui.searchResults.clear();
  ui.upcoming.clear();

  Rect search {rect.x + 12, rect.y + 12, rect.w - 24, 34};
  ui.searchRect = search;
  drawSurface(renderer, search, kInputBg, ui.focus == FocusArea::Search ? kAccentDim : kHairline);
  ui.searchScopeToggle = {search.x + search.w - 34, search.y + 5, 24, 24};
  text.draw("Find", search.x + 10, search.y + 8, ui.focus == FocusArea::Search ? kAccent : kDim);
  text.draw(ellipsizeToWidth(text, ui.searchDraft.empty() ? "Search entries" : ui.searchDraft, static_cast<int>(search.w - 92)), search.x + 52, search.y + 8, ui.searchDraft.empty() ? kDim : kText);
  drawSurface(renderer, ui.searchScopeToggle, kSurface, ui.focus == FocusArea::Search ? kAccentDim : kHairline);
  text.draw(scopeLabel(ui.searchScope), ui.searchScopeToggle.x + 8, ui.searchScopeToggle.y + 5, kAccent);

  const Rect scrollViewport {rect.x, rect.y + 64, rect.w, std::max(0.0f, rect.h - 64.0f)};
  float y = rect.y + 64 - static_cast<float>(ui.leftScroll);
  if(!ui.searchDraft.empty()) {
    text.draw("Results", rect.x + 16, y, kText, false, false, true);
    y += 28;
    const auto results = ui.state.currentEntries();
    if(results.empty()) {
      text.draw("No results", rect.x + 16, y, kDim);
      ui.leftMaxScroll = maxScrollFor(y + 28.0f + static_cast<float>(ui.leftScroll) - scrollViewport.y, scrollViewport.h);
      ui.leftScroll = std::clamp(ui.leftScroll, 0, ui.leftMaxScroll);
      drawVerticalScrollbar(renderer, scrollViewport, ui.leftScroll, ui.leftMaxScroll);
      return;
    }
    for(const auto& result : results) {
      Rect row {rect.x + 10, y - 6, rect.w - 20, 42};
      const bool selected = searchResultKey(result) == ui.selectedSearchTargetId;
      drawSelection(renderer, row, selected);
      text.draw(ellipsizeToWidth(text, result.entry.name, static_cast<int>(row.w - 22)), rect.x + 20, y, selected ? kText : kMuted);
      const auto preview = searchPreviewText(result);
      if(!preview.empty()) text.draw(ellipsizeToWidth(text, preview, static_cast<int>(row.w - 22)), rect.x + 20, y + 20, kDim);
      ui.searchResults.push_back({row, result});
      y += 44;
    }
    ui.leftMaxScroll = maxScrollFor(y + static_cast<float>(ui.leftScroll) - scrollViewport.y, scrollViewport.h);
    ui.leftScroll = std::clamp(ui.leftScroll, 0, ui.leftMaxScroll);
    drawVerticalScrollbar(renderer, scrollViewport, ui.leftScroll, ui.leftMaxScroll);
    return;
  }

  text.draw("Upcoming", rect.x + 16, y, kText, false, false, true);
  y += 26;
  const auto upcoming = ui.state.upcoming(todayIso());
  if(upcoming.empty()) {
    text.draw("No events in the next 3 days", rect.x + 16, y, kDim);
    y += 28;
  } else {
    for(const auto& item : upcoming) {
      Rect row {rect.x + 10, y - 6, rect.w - 20, 46};
      const bool selected = item.entry.id == ui.state.selection().entryId;
      drawSelection(renderer, row, selected);
      text.draw(ellipsizeToWidth(text, item.event.title, static_cast<int>(row.w - 22)), rect.x + 20, y, selected ? kText : kMuted);
      text.draw(item.occurrenceDate + (item.event.time.empty() ? "" : " " + item.event.time) + " · " + item.entry.name, rect.x + 20, y + 20, kDim);
      ui.upcoming.push_back({row, item.entry.id});
      y += 48;
    }
  }

  y += 12;
  text.draw("Entries", rect.x + 16, y, kText, false, false, true);
  y += 28;
  const auto entries = ui.state.currentEntries();
  if(entries.empty()) {
    text.draw("No entries", rect.x + 16, y, kDim);
  }
  for(const auto& result : entries) {
    Rect row {rect.x + 10, y - 6, rect.w - 20, 42};
    const bool selected = result.entry.id == ui.state.selection().entryId;
    drawSelection(renderer, row, selected);
    text.draw(ellipsizeToWidth(text, result.entry.name, static_cast<int>(row.w - 22)), rect.x + 20, y, selected ? kText : kMuted);
    const auto preview = searchPreviewText(result);
    if(!preview.empty()) text.draw(ellipsizeToWidth(text, preview, static_cast<int>(row.w - 22)), rect.x + 20, y + 20, kDim);
    ui.entries.push_back({row, result.entry.id});
    y += 44;
  }
  ui.leftMaxScroll = maxScrollFor(y + static_cast<float>(ui.leftScroll) - scrollViewport.y, scrollViewport.h);
  ui.leftScroll = std::clamp(ui.leftScroll, 0, ui.leftMaxScroll);
  drawVerticalScrollbar(renderer, scrollViewport, ui.leftScroll, ui.leftMaxScroll);
}

static void drawContent(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, kContentBg);
  ClipGuard clip(renderer, rect);
  ui.buttons.clear();
  ui.fields.clear();
  ui.attributes.clear();
  ui.events.clear();
  ui.notes.clear();
  ui.entryHeaderRect = {};
  ui.entryDescriptionRect = {};
  float y = rect.y + 20 - static_cast<float>(ui.contentScroll);
  const float x = rect.x + 24;
  const float w = rect.w - 48;

  if(!ui.state.hasAgenda()) {
    text.draw("Open an agenda with --agenda <path>", x, y, kText, true);
    ui.contentMaxScroll = 0;
    ui.contentScroll = 0;
    return;
  }
  auto loaded = ui.state.selectedEntry(kNotesPerPage);
  if(!loaded) {
    text.draw("No entry selected", x, y, kText, true);
    y += 38;
    drawButton(renderer, text, ui, {x, y, 120, 32}, "new-entry", "New entry");
    ui.contentMaxScroll = maxScrollFor(y + 40.0f + static_cast<float>(ui.contentScroll) - rect.y, rect.h);
    ui.contentScroll = std::clamp(ui.contentScroll, 0, ui.contentMaxScroll);
    drawVerticalScrollbar(renderer, rect, ui.contentScroll, ui.contentMaxScroll);
    return;
  }

  ui.entryHeaderRect = {x, y - 8, w, 38};
  text.draw(loaded->entry.name, x, y, kText, true, false, true);
  y += 48;

  text.draw("Description", x, y, kText, false, false, true);
  y += 32;
  if(ui.editKind == EditKind::EntryDescription) {
    Rect editorSurface {x, y, w, 190};
    ui.fields.push_back({editorSurface, FocusArea::EntryDescription});
    drawNoteEditor(renderer, text, ui, editorSurface);
    ui.entryDescriptionRect = editorSurface;
    y += editorSurface.h + 14.0f;
  } else if(loaded->entry.description.empty()) {
    text.draw("No description", x, y, kDim);
    ui.entryDescriptionRect = {x, y - 5, w, 28};
    y += 28;
  } else {
    const float descHeight = std::max(42.0f, measureMarkdown(text, images, ui, loaded->entry.description, w - 20));
    Rect card {x, y, w, descHeight + 20.0f};
    const bool flashing = flashVisible(ui, agenda::SearchTargetKind::Entry, loaded->entry.id);
    fill(renderer, card, flashing ? kSelectedBg : kSurface);
    stroke(renderer, card, flashing ? kAccent : kHairline);
    float descY = y + 10;
    drawMarkdown(renderer, text, images, ui, loaded->entry.description, {x + 10, descY, w - 20, descHeight}, descY);
    ui.entryDescriptionRect = card;
    y += card.h + 12.0f;
  }

  y += 18;
  text.draw("Attributes", x, y, kText, false, false, true);
  drawIconButton(renderer, text, ui, {x + 120, y + 1, kIconButtonSize, kIconButtonSize}, "add-attr", "+");
  y += 32;
  if(ui.editKind == EditKind::Attribute) {
    ui.fields.push_back({{x, y - 4, w, 22}, FocusArea::AttributeKey});
    text.draw("Key", x, y, ui.focus == FocusArea::AttributeKey ? kAccent : kDim);
    text.draw(ui.draftA, x + 70, y, kText);
    y += 24;
    ui.fields.push_back({{x, y - 4, w, 22}, FocusArea::AttributeValue});
    text.draw("Value", x, y, ui.focus == FocusArea::AttributeValue ? kAccent : kDim);
    text.draw(ui.draftB, x + 70, y, kText);
    y += 32;
  }
  if(loaded->attributes.empty()) {
    text.draw("No attributes", x, y, kDim);
    y += 26;
  } else {
    for(const auto& attr : loaded->attributes) {
      Rect row {x, y - 5, w, 30};
      const bool flashing = flashVisible(ui, agenda::SearchTargetKind::Attribute, attr.id);
      fill(renderer, row, flashing ? kSelectedBg : kSurface);
      stroke(renderer, row, flashing ? kAccent : kHairline);
      text.draw(ellipsizeToWidth(text, attr.key, 130), x + 10, y, kAccent);
      text.draw(ellipsizeToWidth(text, attr.value, static_cast<int>(w - 180)), x + 160, y, kText);
      ui.attributes.push_back({row, attr.id});
      y += 34;
    }
  }

  y += 18;
  text.draw("Notes", x, y, kText, false, false, true);
  drawIconButton(renderer, text, ui, {x + 80, y + 1, kIconButtonSize, kIconButtonSize}, "add-note", "+");
  const int pages = std::max(1, (loaded->noteCount + kNotesPerPage - 1) / kNotesPerPage);
  text.draw("Page " + std::to_string(ui.state.selection().notePage + 1) + "/" + std::to_string(pages), x + 126, y, kDim);
  drawIconButton(renderer, text, ui, {x + 228, y + 1, kIconButtonSize, kIconButtonSize}, "prev-notes", "<");
  drawIconButton(renderer, text, ui, {x + 252, y + 1, kIconButtonSize, kIconButtonSize}, "next-notes", ">");
  y += 34;

  if(ui.editKind == EditKind::Note && ui.editorTargetId.empty()) {
    drawSurface(renderer, {x, y, w, 260}, kInputBg, kAccentDim);
    float fieldY = y + 10;
    ui.fields.push_back({{x + 10, fieldY - 4, w - 20, 22}, FocusArea::NoteTitle});
    text.draw("Title", x + 10, fieldY, ui.focus == FocusArea::NoteTitle ? kAccent : kDim);
    text.draw(ui.draftA.empty() ? "optional" : ui.draftA, x + 84, fieldY, ui.draftA.empty() ? kDim : kText);
    fieldY += 24;
    ui.fields.push_back({{x + 10, fieldY - 4, 200, 22}, FocusArea::NoteDate});
    text.draw("Date", x + 10, fieldY, ui.focus == FocusArea::NoteDate ? kAccent : kDim);
    text.draw(ui.draftB, x + 84, fieldY, kText);
    ui.fields.push_back({{x + 230, fieldY - 4, 160, 22}, FocusArea::NoteTime});
    text.draw("Time", x + 230, fieldY, ui.focus == FocusArea::NoteTime ? kAccent : kDim);
    text.draw(ui.draftC.empty() ? "optional" : ui.draftC, x + 280, fieldY, ui.draftC.empty() ? kDim : kText);
    fieldY += 30;
    Rect editorSurface {x + 10, fieldY, w - 20, 190};
    ui.fields.push_back({editorSurface, FocusArea::NoteEditor});
    drawNoteEditor(renderer, text, ui, editorSurface);
    y += 274.0f;
  }
  if(loaded->notes.empty()) {
    text.draw("No notes on this page", x, y, kDim);
    y += 28;
  } else {
    for(const auto& note : loaded->notes) {
      const bool editingThisNote = ui.editKind == EditKind::Note && ui.editorTargetId == note.id && ui.state.shell().paneMode != ui::PaneMode::Viewer;
      const float bodyHeight = editingThisNote ? 190.0f : std::max(42.0f, measureMarkdown(text, images, ui, note.body, w - 20));
      const float metaHeight = editingThisNote ? 72.0f : (note.title.empty() ? 30.0f : 54.0f);
      Rect card {x, y, w, bodyHeight + metaHeight + 20.0f};
      const bool flashing = flashVisible(ui, agenda::SearchTargetKind::Note, note.id);
      fill(renderer, card, flashing ? kSelectedBg : kSurface);
      stroke(renderer, card, flashing ? kAccent : kHairline);
      if(editingThisNote) {
        float fieldY = y + 10;
        ui.fields.push_back({{x + 10, fieldY - 4, w - 20, 22}, FocusArea::NoteTitle});
        text.draw("Title", x + 10, fieldY, ui.focus == FocusArea::NoteTitle ? kAccent : kDim);
        text.draw(ui.draftA.empty() ? "optional" : ui.draftA, x + 84, fieldY, ui.draftA.empty() ? kDim : kText);
        fieldY += 24;
        ui.fields.push_back({{x + 10, fieldY - 4, 200, 22}, FocusArea::NoteDate});
        text.draw("Date", x + 10, fieldY, ui.focus == FocusArea::NoteDate ? kAccent : kDim);
        text.draw(ui.draftB, x + 84, fieldY, kText);
        ui.fields.push_back({{x + 230, fieldY - 4, 160, 22}, FocusArea::NoteTime});
        text.draw("Time", x + 230, fieldY, ui.focus == FocusArea::NoteTime ? kAccent : kDim);
        text.draw(ui.draftC.empty() ? "optional" : ui.draftC, x + 280, fieldY, ui.draftC.empty() ? kDim : kText);
      } else {
        if(!note.title.empty()) text.draw(note.title, x + 10, y + 8, kText, false, false, true);
        text.draw(note.date + (note.time.empty() ? "" : " " + note.time), x + 10, y + (note.title.empty() ? 8 : 32), kDim);
      }
      float bodyY = y + metaHeight;
      if(editingThisNote) {
        Rect noteEditor {x + 10, bodyY, w - 20, bodyHeight};
        ui.fields.push_back({noteEditor, FocusArea::NoteEditor});
        drawNoteEditor(renderer, text, ui, noteEditor);
      } else {
        drawMarkdown(renderer, text, images, ui, note.body, {x + 10, bodyY, w - 20, bodyHeight}, bodyY);
      }
      ui.notes.push_back({card, note.id});
      y += card.h + 12.0f;
    }
  }

  y += 18;
  text.draw("Events", x, y, kText, false, false, true);
  drawIconButton(renderer, text, ui, {x + 80, y + 1, kIconButtonSize, kIconButtonSize}, "add-event", "+");
  y += 34;
  if(ui.editKind == EditKind::Event) {
    drawSurface(renderer, {x, y, w, 220}, kInputBg, kAccentDim);
    float fieldY = y + 10;
    ui.fields.push_back({{x + 10, fieldY - 4, w - 20, 22}, FocusArea::EventTitle});
    text.draw("Title", x + 10, fieldY, ui.focus == FocusArea::EventTitle ? kAccent : kDim);
    text.draw(ui.draftA, x + 84, fieldY, kText);
    fieldY += 24;
    ui.fields.push_back({{x + 10, fieldY - 4, 200, 22}, FocusArea::EventDate});
    text.draw("Date", x + 10, fieldY, ui.focus == FocusArea::EventDate ? kAccent : kDim);
    text.draw(ui.draftB, x + 84, fieldY, kText);
    ui.fields.push_back({{x + 230, fieldY - 4, 160, 22}, FocusArea::EventTime});
    text.draw("Time", x + 230, fieldY, ui.focus == FocusArea::EventTime ? kAccent : kDim);
    text.draw(ui.draftC.empty() ? "optional" : ui.draftC, x + 280, fieldY, ui.draftC.empty() ? kDim : kText);
    fieldY += 24;
    text.draw("Repeat", x + 10, fieldY, kDim);
    text.draw(agenda::repeatRuleToString(ui.draftRepeat), x + 84, fieldY, kAccent);
    drawButton(renderer, text, ui, {x + 180, fieldY - 4, 62, 26}, "repeat", "Cycle");
    fieldY += 30;
    ui.fields.push_back({{x + 10, fieldY - 4, w - 20, 128}, FocusArea::EventDescription});
    text.draw("Description", x + 10, fieldY, ui.focus == FocusArea::EventDescription ? kAccent : kDim);
    fieldY += 24;
    drawNoteEditor(renderer, text, ui, {x + 10, fieldY, w - 20, 96});
    y += 234;
  }
  if(loaded->events.empty()) {
    text.draw("No events", x, y, kDim);
    y += 28;
  } else {
    for(const auto& event : loaded->events) {
      Rect card {x, y, w, 92};
      const bool flashing = flashVisible(ui, agenda::SearchTargetKind::Event, event.id);
      fill(renderer, card, flashing ? kSelectedBg : kSurfaceElevated);
      stroke(renderer, card, flashing ? kAccent : kHairline);
      text.draw(event.title, x + 10, y + 8, kText, false, false, true);
      text.draw(event.date + (event.time.empty() ? "" : " " + event.time) + " · " + agenda::repeatRuleToString(event.repeat), x + 10, y + 30, kAccent);
      text.draw(ellipsizeToWidth(text, event.description, static_cast<int>(w - 20)), x + 10, y + 54, kMuted);
      ui.events.push_back({card, event.id});
      y += 104;
    }
  }
  ui.contentMaxScroll = maxScrollFor(y + 20.0f + static_cast<float>(ui.contentScroll) - rect.y, rect.h);
  ui.contentScroll = std::clamp(ui.contentScroll, 0, ui.contentMaxScroll);
  drawVerticalScrollbar(renderer, rect, ui.contentScroll, ui.contentMaxScroll);
}

static void drawNoteMenu(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, int width, int height) {
  if(!ui.noteMenu.open) return;
  const Rect menu = noteMenuRect(width, height, ui.noteMenu);
  drawSurface(renderer, menu, kSurfaceElevated, kHairline);
  text.draw("New note", menu.x + 12, menu.y + 12, kText);
  text.draw("Edit", menu.x + 12, menu.y + 42, kText);
  text.draw("Delete", menu.x + 12, menu.y + 72, kWarn);
}

static void drawEntryMenu(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, int width, int height) {
  if(!ui.entryMenu.open) return;
  const Rect menu = entryMenuRect(width, height, ui.entryMenu);
  drawSurface(renderer, menu, kSurfaceElevated, kHairline);
  text.draw("New entry", menu.x + 12, menu.y + 12, kText);
  text.draw("Rename", menu.x + 12, menu.y + 42, kText);
  text.draw("Delete", menu.x + 12, menu.y + 72, kWarn);
}

static void drawRowActionMenu(SDL_Renderer* renderer, TextRenderer& text, const PopupMenuState& state, int width, int height) {
  if(!state.open) return;
  const Rect menu = rowActionMenuRect(width, height, state);
  drawSurface(renderer, menu, kSurfaceElevated, kHairline);
  text.draw("Edit", menu.x + 12, menu.y + 12, kText);
  text.draw("Delete", menu.x + 12, menu.y + 42, kWarn);
}

static void drawStatus(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, SDL_Color {12, 14, 18, 255});
  line(renderer, rect.x, rect.x + rect.w, rect.y, kHairline);
  std::string help = "Ctrl+N entry   Ctrl+Shift+N note   Ctrl+F search   Ctrl+S save edit   Ctrl+L note mode";
  if(ui.focus == FocusArea::Search) help = "Search: " + ui.searchDraft + "   scope " + scopeLabel(ui.searchScope);
  if(ui.editKind == EditKind::EntryName) help = std::string(ui.creatingEntry ? "New entry: " : "Rename entry: ") + ui.draftA + "   Enter/Ctrl+S saves";
  if(ui.editKind == EditKind::EntryDescription) help = "Description: Ctrl+S saves, Ctrl+L cycles mode";
  if(ui.editKind == EditKind::Event) help = "Event: Enter/Tab moves fields, Ctrl+S saves";
  if(ui.editKind == EditKind::Note) help = "Note: Ctrl+S saves, Ctrl+L cycles mode";
  if(ui.noteMenu.open || ui.entryMenu.open || ui.attributeMenu.open || ui.eventMenu.open) help = "Context menu: click an action or click outside to dismiss";
  fill(renderer, {rect.x + 12, rect.y + 7, 6, 6}, ui.activeEditDirty ? kWarn : kAccent);
  text.draw(ellipsize(help, 110), rect.x + 26, rect.y + 6, kMuted);
  if(!ui.status.empty()) text.draw(ellipsize(ui.status, 60), rect.x + rect.w - 360, rect.y + 6, kAccent);
}

static void drawApp(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, int width, int height) {
  clampScrolls(ui);
  SDL_SetRenderDrawColor(renderer, kAppBg.r, kAppBg.g, kAppBg.b, kAppBg.a);
  SDL_RenderClear(renderer);
  const float statusH = 28.0f;
  const float leftW = std::clamp(static_cast<float>(ui.state.shell().sidebarWidth), 260.0f, 380.0f);
  Rect left {0, 0, leftW, static_cast<float>(height) - statusH};
  Rect content {leftW, 0, static_cast<float>(width) - leftW, static_cast<float>(height) - statusH};
  Rect status {0, static_cast<float>(height) - statusH, static_cast<float>(width), statusH};
  drawLeft(renderer, text, ui, left);
  fill(renderer, {left.x + left.w, 0, 1, left.h}, kHairline);
  drawContent(renderer, text, images, ui, content);
  drawStatus(renderer, text, ui, status);
  drawNoteMenu(renderer, text, ui, width, height);
  drawEntryMenu(renderer, text, ui, width, height);
  drawRowActionMenu(renderer, text, ui.attributeMenu, width, height);
  drawRowActionMenu(renderer, text, ui.eventMenu, width, height);
  SDL_RenderPresent(renderer);
}

static void performButton(UiRuntime& ui, const std::string& id) {
  auto loaded = ui.state.selectedEntry(kNotesPerPage);
  if(id == "new-entry") beginEntryCreate(ui);
  else if(id == "rename-entry" && loaded) beginEntryRename(ui, *loaded);
  else if(id == "delete-entry") {
    ui.status = ui.state.deleteSelectedEntry() ? "Deleted entry" : "Delete failed";
  } else if(id == "add-attr") beginAttributeCreate(ui);
  else if(id == "add-note") beginNoteCreate(ui);
  else if(id == "add-event") beginEventCreate(ui);
  else if(id == "prev-notes") ui.state.setNotePage(ui.state.selection().notePage - 1);
  else if(id == "next-notes" && loaded) {
    const int pages = std::max(1, (loaded->noteCount + kNotesPerPage - 1) / kNotesPerPage);
    ui.state.setNotePage(std::min(pages - 1, ui.state.selection().notePage + 1));
  } else if(id == "repeat") ui.draftRepeat = nextRepeat(ui.draftRepeat);
  else if(id.starts_with("edit-attr:") && loaded) {
    const auto attrId = id.substr(10);
    auto found = std::find_if(loaded->attributes.begin(), loaded->attributes.end(), [&](const agenda::Attribute& attr) { return attr.id == attrId; });
    if(found != loaded->attributes.end()) beginAttributeEdit(ui, *found);
  } else if(id.starts_with("del-attr:")) {
    ui.status = ui.state.deleteAttribute(id.substr(9)) ? "Deleted attribute" : "Delete failed";
  } else if(id.starts_with("edit-note:") && loaded) {
    const auto noteId = id.substr(10);
    auto found = std::find_if(loaded->notes.begin(), loaded->notes.end(), [&](const agenda::Note& note) { return note.id == noteId; });
    if(found != loaded->notes.end()) beginNoteEdit(ui, *found);
  } else if(id.starts_with("del-note:")) {
    ui.status = ui.state.deleteNote(id.substr(9)) ? "Deleted note" : "Delete failed";
  } else if(id.starts_with("edit-event:") && loaded) {
    const auto eventId = id.substr(11);
    auto found = std::find_if(loaded->events.begin(), loaded->events.end(), [&](const agenda::Event& event) { return event.id == eventId; });
    if(found != loaded->events.end()) beginEventEdit(ui, *found);
  } else if(id.starts_with("del-event:")) {
    ui.status = ui.state.deleteEvent(id.substr(10)) ? "Deleted event" : "Delete failed";
  }
}

static void activateSearchResult(UiRuntime& ui, const agenda::SearchResult& result) {
  ui.state.selectEntry(result.entry.id);
  ui.selectedSearchTargetId = searchResultKey(result);
  if(const auto* preview = searchPreview(result)) {
    if(preview->targetKind == agenda::SearchTargetKind::Note && preview->noteIndex >= 0) {
      ui.state.setNotePage(preview->noteIndex / kNotesPerPage);
    }
    ui.flashKind = preview->targetKind;
    ui.flashTargetId = preview->targetId;
    ui.flashStartedAt = SDL_GetTicks();
  } else {
    ui.flashKind = agenda::SearchTargetKind::Entry;
    ui.flashTargetId = result.entry.id;
    ui.flashStartedAt = SDL_GetTicks();
  }
  ui.contentScroll = 0;
  ui.focus = FocusArea::Content;
}

static void handleMouse(TextRenderer& text, UiRuntime& ui, float x, float y, uint8_t button, uint8_t clicks, int width, int height) {
  if(ui.noteMenu.open) {
    const Rect menu = noteMenuRect(width, height, ui.noteMenu);
    if(button == SDL_BUTTON_LEFT && contains(menu, x, y)) {
      if(y < menu.y + 30) beginNoteCreate(ui);
      else if(y < menu.y + 60) {
        auto loaded = ui.state.selectedEntry(kNotesPerPage);
        if(loaded) {
          auto found = std::find_if(loaded->notes.begin(), loaded->notes.end(), [&](const agenda::Note& note) { return note.id == ui.noteMenu.targetId; });
          if(found != loaded->notes.end()) beginNoteEdit(ui, *found);
        }
      } else {
        ui.status = ui.state.deleteNote(ui.noteMenu.targetId) ? "Deleted note" : "Delete failed";
      }
      ui.noteMenu.open = false;
      return;
    }
    ui.noteMenu.open = false;
  }

  if(ui.entryMenu.open) {
    const Rect menu = entryMenuRect(width, height, ui.entryMenu);
    if(button == SDL_BUTTON_LEFT && contains(menu, x, y)) {
      if(y < menu.y + 30) beginEntryCreate(ui);
      else if(y < menu.y + 60) {
        auto loaded = ui.state.selectedEntry(kNotesPerPage);
        if(loaded) beginEntryRename(ui, *loaded);
      } else {
        ui.status = ui.state.deleteSelectedEntry() ? "Deleted entry" : "Delete failed";
      }
      ui.entryMenu.open = false;
      return;
    }
    ui.entryMenu.open = false;
  }

  if(ui.attributeMenu.open) {
    const Rect menu = rowActionMenuRect(width, height, ui.attributeMenu);
    if(button == SDL_BUTTON_LEFT && contains(menu, x, y)) {
      auto loaded = ui.state.selectedEntry(kNotesPerPage);
      if(y < menu.y + 30) {
        if(loaded) {
          auto found = std::find_if(loaded->attributes.begin(), loaded->attributes.end(), [&](const agenda::Attribute& attr) { return attr.id == ui.attributeMenu.targetId; });
          if(found != loaded->attributes.end()) beginAttributeEdit(ui, *found);
        }
      } else {
        ui.status = ui.state.deleteAttribute(ui.attributeMenu.targetId) ? "Deleted attribute" : "Delete failed";
      }
      ui.attributeMenu.open = false;
      return;
    }
    ui.attributeMenu.open = false;
  }

  if(ui.eventMenu.open) {
    const Rect menu = rowActionMenuRect(width, height, ui.eventMenu);
    if(button == SDL_BUTTON_LEFT && contains(menu, x, y)) {
      auto loaded = ui.state.selectedEntry(kNotesPerPage);
      if(y < menu.y + 30) {
        if(loaded) {
          auto found = std::find_if(loaded->events.begin(), loaded->events.end(), [&](const agenda::Event& event) { return event.id == ui.eventMenu.targetId; });
          if(found != loaded->events.end()) beginEventEdit(ui, *found);
        }
      } else {
        ui.status = ui.state.deleteEvent(ui.eventMenu.targetId) ? "Deleted event" : "Delete failed";
      }
      ui.eventMenu.open = false;
      return;
    }
    ui.eventMenu.open = false;
  }

  if(button == SDL_BUTTON_LEFT && contains(ui.searchScopeToggle, x, y)) {
    ui.searchScope = nextScope(ui.searchScope);
    syncSearch(ui);
    ui.focus = FocusArea::Search;
    return;
  }
  if(button == SDL_BUTTON_LEFT && contains(ui.searchRect, x, y)) {
    ui.focus = FocusArea::Search;
    return;
  }
  for(const auto& item : ui.buttons) {
    if(button == SDL_BUTTON_LEFT && contains(item.rect, x, y)) {
      performButton(ui, item.id);
      return;
    }
  }
  for(const auto& field : ui.fields) {
    if((button == SDL_BUTTON_LEFT || button == SDL_BUTTON_MIDDLE) && contains(field.rect, x, y)) {
      ui.focus = field.focus;
      if(isEditorFocus(field.focus)) {
        ui.state.shell().paneMode = ui::PaneMode::Editor;
        placeEditorCursor(text, ui, field.rect, x, y);
        if(button == SDL_BUTTON_MIDDLE) {
          ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
          return;
        }
        ui.selectingEditorText = true;
        ui.editorSelectionAnchor = ui.editor.cursor();
        if(clicks == 2) {
          selectWordAtCursor(ui);
          ui.editorSelectionAnchor = ui.editor.selectionStart();
          publishEditorPrimarySelection(ui);
        } else if(clicks >= 3) {
          selectLineAtCursor(ui);
          ui.editorSelectionAnchor = ui.editor.selectionStart();
          publishEditorPrimarySelection(ui);
        }
      }
      return;
    }
  }
  if(button == SDL_BUTTON_RIGHT && contains(ui.entryHeaderRect, x, y)) {
    ui.entryMenu.open = true;
    ui.entryMenu.x = x;
    ui.entryMenu.y = y;
    return;
  }
  if(contains(ui.entryDescriptionRect, x, y)) {
    if(button == SDL_BUTTON_LEFT && clicks >= 2) {
      auto loaded = ui.state.selectedEntry(kNotesPerPage);
      if(loaded) beginEntryDescriptionEdit(ui, *loaded);
      return;
    }
  }
  for(const auto& event : ui.upcoming) {
    if(contains(event.rect, x, y)) {
      ui.state.selectEntry(event.id);
      ui.contentScroll = 0;
      ui.focus = FocusArea::Content;
      return;
    }
  }
  for(const auto& result : ui.searchResults) {
    if(contains(result.rect, x, y)) {
      activateSearchResult(ui, result.result);
      return;
    }
  }
  for(const auto& entry : ui.entries) {
    if(contains(entry.rect, x, y)) {
      if(button == SDL_BUTTON_RIGHT) {
        ui.state.selectEntry(entry.id);
        ui.entryMenu.open = true;
        ui.entryMenu.x = x;
        ui.entryMenu.y = y;
        ui.focus = FocusArea::Content;
      } else {
        ui.state.selectEntry(entry.id);
        ui.contentScroll = 0;
        ui.focus = FocusArea::Content;
      }
      return;
    }
  }
  for(const auto& attr : ui.attributes) {
    if(contains(attr.rect, x, y)) {
      if(button == SDL_BUTTON_LEFT && clicks >= 2) {
        auto loaded = ui.state.selectedEntry(kNotesPerPage);
        if(loaded) {
          auto found = std::find_if(loaded->attributes.begin(), loaded->attributes.end(), [&](const agenda::Attribute& item) { return item.id == attr.id; });
          if(found != loaded->attributes.end()) beginAttributeEdit(ui, *found);
        }
      } else if(button == SDL_BUTTON_RIGHT) {
        ui.attributeMenu.open = true;
        ui.attributeMenu.x = x;
        ui.attributeMenu.y = y;
        ui.attributeMenu.targetId = attr.id;
      }
      ui.focus = FocusArea::Content;
      return;
    }
  }
  for(const auto& event : ui.events) {
    if(contains(event.rect, x, y)) {
      if(button == SDL_BUTTON_LEFT && clicks >= 2) {
        auto loaded = ui.state.selectedEntry(kNotesPerPage);
        if(loaded) {
          auto found = std::find_if(loaded->events.begin(), loaded->events.end(), [&](const agenda::Event& item) { return item.id == event.id; });
          if(found != loaded->events.end()) beginEventEdit(ui, *found);
        }
      } else if(button == SDL_BUTTON_RIGHT) {
        ui.eventMenu.open = true;
        ui.eventMenu.x = x;
        ui.eventMenu.y = y;
        ui.eventMenu.targetId = event.id;
      }
      ui.focus = FocusArea::Content;
      return;
    }
  }
  for(const auto& note : ui.notes) {
    if(contains(note.rect, x, y)) {
      if(button == SDL_BUTTON_LEFT && clicks >= 2) {
        auto loaded = ui.state.selectedEntry(kNotesPerPage);
        if(loaded) {
          auto found = std::find_if(loaded->notes.begin(), loaded->notes.end(), [&](const agenda::Note& item) { return item.id == note.id; });
          if(found != loaded->notes.end()) beginNoteEdit(ui, *found);
        }
      } else if(button == SDL_BUTTON_RIGHT) {
        ui.noteMenu.open = true;
        ui.noteMenu.x = x;
        ui.noteMenu.y = y;
        ui.noteMenu.targetId = note.id;
      }
      ui.focus = FocusArea::Content;
      return;
    }
  }
  ui.focus = FocusArea::Content;
}

static void handleMouseMotion(TextRenderer& text, UiRuntime& ui, float x, float y) {
  if(!ui.selectingEditorText) return;
  const auto rect = activeEditorRect(ui);
  if(!rect) return;
  const auto cursor = editorIndexAtPoint(text, ui, *rect, x, y);
  ui.editor.selectRange(ui.editorSelectionAnchor, cursor);
  ui.revealNoteEditorCursor = true;
}

static void handleMouseUp(UiRuntime& ui, uint8_t button) {
  if(button != SDL_BUTTON_LEFT) return;
  if(ui.selectingEditorText) publishEditorPrimarySelection(ui);
  ui.selectingEditorText = false;
}

}

ApplicationOptions parseArgs(int argc, char** argv) {
  ApplicationOptions options;
  for(int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if((arg == "--agenda" || arg == "--library") && i + 1 < argc) {
      options.agendaRoot = argv[++i];
    } else if(arg == "--headless") {
      options.headless = true;
    }
  }
  if(options.agendaRoot.empty()) {
    if(const char* home = std::getenv("HOME")) options.agendaRoot = std::filesystem::path(home) / ".local" / "share" / "microagenda";
    else options.agendaRoot = std::filesystem::current_path() / "microagenda-data";
  }
  return options;
}

int run(const ApplicationOptions& options) {
  UiRuntime ui;
  if(!ui.state.openOrCreateAgenda(options.agendaRoot)) {
    std::cerr << "failed to open agenda at " << options.agendaRoot << "\n";
    return 1;
  }
  ui.state.loadUiState(uiStatePath(options.agendaRoot));
  ui.searchDraft = ui.state.selection().search;
  ui.searchScope = ui.state.selection().searchScope;
  const bool restoredEdit = restoreActiveEditRecovery(ui);
  if(ui.state.selection().entryId.empty()) {
    const auto entries = ui.state.currentEntries();
    if(!entries.empty()) ui.state.selectEntry(entries.front().entry.id);
  }
  if(restoredEdit) ui.status = "Recovered unsaved edit";
  if(options.headless) return 0;

  if(!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("microagenda", 1180, 760, SDL_WINDOW_RESIZABLE);
  if(!window) {
    std::cerr << "window failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if(!renderer) {
    std::cerr << "renderer failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  TextRenderer text(renderer);
  ImageCache images(renderer);
  SDL_StartTextInput(window);

  auto autosaveWaitMs = [&]() -> int {
    if(!ui.state.hasAgenda() || ui.editKind == EditKind::None || !ui.activeEditDirty || !canAutosaveActiveEdit(ui)) return -1;
    const Uint64 now = SDL_GetTicks();
    const Uint64 next = std::max(ui.lastEdit + 1201, ui.lastAutosaveAttempt + 1001);
    if(now >= next) return 0;
    return std::clamp(static_cast<int>(next - now), 1, 1200);
  };

  bool running = true;
  bool needsDraw = true;
  while(running) {
    SDL_Event event;
    if(needsDraw) {
      int width = 0;
      int height = 0;
      SDL_GetWindowSize(window, &width, &height);
      drawApp(renderer, text, images, ui, width, height);
      needsDraw = false;
    }
    const int waitMs = autosaveWaitMs();
    const int eventWaitMs = flashAnimating(ui) ? 16 : waitMs;
    const bool hasEvent = eventWaitMs >= 0 ? SDL_WaitEventTimeout(&event, eventWaitMs) : SDL_WaitEvent(&event);
    if(!hasEvent) {
      const Uint64 now = SDL_GetTicks();
      if(ui.state.hasAgenda() && ui.editKind != EditKind::None && ui.activeEditDirty && canAutosaveActiveEdit(ui) && now - ui.lastEdit > 1200 && now - ui.lastAutosaveAttempt > 1000) {
        ui.lastAutosaveAttempt = now;
        if(saveDraft(ui, false, true)) ui.status = "All changes saved";
      }
      needsDraw = true;
      continue;
    }
    do {
      needsDraw = true;
      if(event.type == SDL_EVENT_QUIT) running = false;
      else if(event.type == SDL_EVENT_TEXT_INPUT) handleText(ui, event.text.text);
      else if(event.type == SDL_EVENT_KEY_DOWN) handleKey(text, ui, event.key.key, event.key.scancode, event.key.mod);
      else if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE)) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        handleMouse(text, ui, event.button.x, event.button.y, event.button.button, event.button.clicks, width, height);
      }
      else if(event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        handleMouseUp(ui, event.button.button);
      }
      else if(event.type == SDL_EVENT_MOUSE_MOTION) {
        handleMouseMotion(text, ui, event.motion.x, event.motion.y);
      }
      else if(event.type == SDL_EVENT_MOUSE_WHEEL) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        const float leftW = std::clamp(static_cast<float>(ui.state.shell().sidebarWidth), 260.0f, 380.0f);
        bool scrolledEditor = false;
        if(isEditorFocus(ui.focus)) {
          for(const auto& field : ui.fields) {
            if(isEditorFocus(field.focus) && contains(field.rect, event.wheel.mouse_x, event.wheel.mouse_y)) {
              ui.noteEditorScroll = std::clamp(ui.noteEditorScroll - static_cast<int>(event.wheel.y * 3), 0, noteEditorMaxScroll(text, ui, field.rect));
              ui.revealNoteEditorCursor = false;
              scrolledEditor = true;
              break;
            }
          }
        }
        if(!scrolledEditor) {
          if(event.wheel.mouse_x < leftW) ui.leftScroll = std::clamp(ui.leftScroll - static_cast<int>(event.wheel.y * 48), 0, ui.leftMaxScroll);
          else ui.contentScroll = std::clamp(ui.contentScroll - static_cast<int>(event.wheel.y * 64), 0, ui.contentMaxScroll);
        }
      }
    } while(SDL_PollEvent(&event));
    const Uint64 now = SDL_GetTicks();
    if(ui.state.hasAgenda() && ui.editKind != EditKind::None && ui.activeEditDirty && canAutosaveActiveEdit(ui) && now - ui.lastEdit > 1200 && now - ui.lastAutosaveAttempt > 1000) {
      ui.lastAutosaveAttempt = now;
      if(saveDraft(ui, false, true)) ui.status = "All changes saved";
      needsDraw = true;
    }
  }

  if(ui.state.hasAgenda() && ui.editKind != EditKind::None && ui.activeEditDirty && canAutosaveActiveEdit(ui)) (void)saveDraft(ui, false, true);
  ui.state.saveUiState(uiStatePath(options.agendaRoot));
  SDL_StopTextInput(window);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}
