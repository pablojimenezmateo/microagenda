#include "md4c.h"

#include <ctype.h>
#include <string.h>

static int emit_inline(const char* text, unsigned size, const MD_PARSER* parser, void* userdata);

static int emit_text(const MD_PARSER* parser, MD_TEXTTYPE type, const char* text, unsigned size, void* userdata) {
  if(!parser->text || size == 0) return 0;
  return parser->text(type, text, size, userdata);
}

static int emit_block_line(MD_BLOCKTYPE type, void* detail, const char* text, unsigned size, const MD_PARSER* parser, void* userdata) {
  if(parser->enter_block && parser->enter_block(type, detail, userdata)) return -1;
  if(type == MD_BLOCK_CODE) {
    if(emit_text(parser, MD_TEXT_CODE, text, size, userdata)) return -1;
  } else {
    if(emit_inline(text, size, parser, userdata)) return -1;
  }
  if(parser->leave_block && parser->leave_block(type, detail, userdata)) return -1;
  return 0;
}

static unsigned trim_left(const char* text, unsigned size) {
  unsigned i = 0;
  while(i < size && (text[i] == ' ' || text[i] == '\t')) ++i;
  return i;
}

static unsigned trim_right(const char* text, unsigned size) {
  while(size > 0 && (text[size - 1] == ' ' || text[size - 1] == '\t' || text[size - 1] == '\r')) --size;
  return size;
}

static int is_heading(const char* text, unsigned size, unsigned* level, unsigned* offset) {
  unsigned i = trim_left(text, size);
  unsigned count = 0;
  while(i + count < size && text[i + count] == '#') ++count;
  if(count == 0 || count > 6 || i + count >= size || text[i + count] != ' ') return 0;
  *level = count;
  *offset = i + count + 1;
  return 1;
}

static int is_list_item(const char* text, unsigned size, unsigned* offset, int* ordered) {
  unsigned i = trim_left(text, size);
  if(i + 2 <= size && (text[i] == '-' || text[i] == '*') && text[i + 1] == ' ') {
    *offset = i + 2;
    *ordered = 0;
    return 1;
  }
  unsigned j = i;
  while(j < size && isdigit((unsigned char)text[j])) ++j;
  if(j > i && j + 1 < size && text[j] == '.' && text[j + 1] == ' ') {
    *offset = j + 2;
    *ordered = 1;
    return 1;
  }
  return 0;
}

static int emit_inline(const char* text, unsigned size, const MD_PARSER* parser, void* userdata) {
  unsigned i = 0;
  while(i < size) {
    if(text[i] == '!' && i + 3 < size && text[i + 1] == '[') {
      const char* alt_start = text + i + 2;
      const char* alt_end = memchr(alt_start, ']', size - i - 2);
      if(alt_end && alt_end + 1 < text + size && alt_end[1] == '(') {
        const char* src_start = alt_end + 2;
        const char* src_end = memchr(src_start, ')', (text + size) - src_start);
        if(src_end) {
          MD_SPAN_IMG_DETAIL detail;
          detail.src.text = src_start;
          detail.src.size = (unsigned)(src_end - src_start);
          detail.title.text = 0;
          detail.title.size = 0;
          if(parser->enter_span && parser->enter_span(MD_SPAN_IMG, &detail, userdata)) return -1;
          if(emit_text(parser, MD_TEXT_NORMAL, alt_start, (unsigned)(alt_end - alt_start), userdata)) return -1;
          if(parser->leave_span && parser->leave_span(MD_SPAN_IMG, &detail, userdata)) return -1;
          i = (unsigned)((src_end + 1) - text);
          continue;
        }
      }
    }
    if(text[i] == '[') {
      const char* label_start = text + i + 1;
      const char* label_end = memchr(label_start, ']', size - i - 1);
      if(label_end && label_end + 1 < text + size && label_end[1] == '(') {
        const char* href_start = label_end + 2;
        const char* href_end = memchr(href_start, ')', (text + size) - href_start);
        if(href_end) {
          MD_SPAN_A_DETAIL detail;
          detail.href.text = href_start;
          detail.href.size = (unsigned)(href_end - href_start);
          detail.title.text = 0;
          detail.title.size = 0;
          if(parser->enter_span && parser->enter_span(MD_SPAN_A, &detail, userdata)) return -1;
          if(emit_text(parser, MD_TEXT_NORMAL, label_start, (unsigned)(label_end - label_start), userdata)) return -1;
          if(parser->leave_span && parser->leave_span(MD_SPAN_A, &detail, userdata)) return -1;
          i = (unsigned)((href_end + 1) - text);
          continue;
        }
      }
    }
    const char* next = text + i + 1;
    while(next < text + size && *next != '[' && !(*next == '!' && next + 1 < text + size && next[1] == '[')) ++next;
    if(emit_text(parser, MD_TEXT_NORMAL, text + i, (unsigned)(next - (text + i)), userdata)) return -1;
    i = (unsigned)(next - text);
  }
  return 0;
}

int md_parse(const char* text, unsigned size, const MD_PARSER* parser, void* userdata) {
  if(!text || !parser) return -1;
  if(parser->enter_block && parser->enter_block(MD_BLOCK_DOC, 0, userdata)) return -1;

  unsigned pos = 0;
  int in_fence = 0;
  while(pos < size) {
    unsigned line_start = pos;
    while(pos < size && text[pos] != '\n') ++pos;
    unsigned line_size = trim_right(text + line_start, pos - line_start);
    if(pos < size && text[pos] == '\n') ++pos;

    unsigned left = trim_left(text + line_start, line_size);
    const char* line = text + line_start + left;
    unsigned n = line_size >= left ? line_size - left : 0;
    if(n == 0) continue;

    if(n >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') {
      in_fence = !in_fence;
      continue;
    }
    if(in_fence) {
      if(emit_block_line(MD_BLOCK_CODE, 0, line, n, parser, userdata)) return -1;
      continue;
    }

    unsigned level = 0;
    unsigned offset = 0;
    if(is_heading(text + line_start, line_size, &level, &offset)) {
      MD_BLOCK_H_DETAIL detail;
      detail.level = level;
      if(parser->enter_block && parser->enter_block(MD_BLOCK_H, &detail, userdata)) return -1;
      if(emit_inline(text + line_start + offset, line_size - offset, parser, userdata)) return -1;
      if(parser->leave_block && parser->leave_block(MD_BLOCK_H, &detail, userdata)) return -1;
      continue;
    }
    if(line[0] == '>') {
      unsigned quote_offset = n > 1 && line[1] == ' ' ? 2 : 1;
      if(emit_block_line(MD_BLOCK_QUOTE, 0, line + quote_offset, n - quote_offset, parser, userdata)) return -1;
      continue;
    }
    int ordered = 0;
    if(is_list_item(text + line_start, line_size, &offset, &ordered)) {
      MD_BLOCKTYPE list_type = ordered ? MD_BLOCK_OL : MD_BLOCK_UL;
      if(parser->enter_block && parser->enter_block(list_type, 0, userdata)) return -1;
      if(emit_block_line(MD_BLOCK_LI, 0, text + line_start + offset, line_size - offset, parser, userdata)) return -1;
      if(parser->leave_block && parser->leave_block(list_type, 0, userdata)) return -1;
      continue;
    }
    if(emit_block_line(MD_BLOCK_P, 0, line, n, parser, userdata)) return -1;
  }

  if(parser->leave_block && parser->leave_block(MD_BLOCK_DOC, 0, userdata)) return -1;
  return 0;
}
