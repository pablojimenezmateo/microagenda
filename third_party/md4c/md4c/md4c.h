#ifndef MD4C_H
#define MD4C_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MD_BLOCKTYPE {
  MD_BLOCK_DOC = 0,
  MD_BLOCK_P,
  MD_BLOCK_H,
  MD_BLOCK_UL,
  MD_BLOCK_OL,
  MD_BLOCK_LI,
  MD_BLOCK_QUOTE,
  MD_BLOCK_CODE,
  MD_BLOCK_TABLE
} MD_BLOCKTYPE;

typedef enum MD_SPANTYPE {
  MD_SPAN_EM = 0,
  MD_SPAN_STRONG,
  MD_SPAN_A,
  MD_SPAN_IMG,
  MD_SPAN_CODE
} MD_SPANTYPE;

typedef enum MD_TEXTTYPE {
  MD_TEXT_NORMAL = 0,
  MD_TEXT_NULLCHAR,
  MD_TEXT_BR,
  MD_TEXT_SOFTBR,
  MD_TEXT_ENTITY,
  MD_TEXT_CODE,
  MD_TEXT_HTML
} MD_TEXTTYPE;

typedef struct MD_ATTRIBUTE {
  const char* text;
  unsigned size;
} MD_ATTRIBUTE;

typedef struct MD_BLOCK_H_DETAIL {
  unsigned level;
} MD_BLOCK_H_DETAIL;

typedef struct MD_BLOCK_CODE_DETAIL {
  MD_ATTRIBUTE info;
  MD_ATTRIBUTE lang;
  MD_ATTRIBUTE fence_char;
} MD_BLOCK_CODE_DETAIL;

typedef struct MD_SPAN_A_DETAIL {
  MD_ATTRIBUTE href;
  MD_ATTRIBUTE title;
} MD_SPAN_A_DETAIL;

typedef struct MD_SPAN_IMG_DETAIL {
  MD_ATTRIBUTE src;
  MD_ATTRIBUTE title;
} MD_SPAN_IMG_DETAIL;

typedef int (*MD_BLOCK_FUNC)(MD_BLOCKTYPE type, void* detail, void* userdata);
typedef int (*MD_SPAN_FUNC)(MD_SPANTYPE type, void* detail, void* userdata);
typedef int (*MD_TEXT_FUNC)(MD_TEXTTYPE type, const char* text, unsigned size, void* userdata);

typedef struct MD_PARSER {
  unsigned abi_version;
  unsigned flags;
  MD_BLOCK_FUNC enter_block;
  MD_BLOCK_FUNC leave_block;
  MD_SPAN_FUNC enter_span;
  MD_SPAN_FUNC leave_span;
  MD_TEXT_FUNC text;
  void (*debug_log)(const char* msg, void* userdata);
  void (*syntax)(void);
} MD_PARSER;

#define MD_DIALECT_COMMONMARK 0u
#define MD_DIALECT_GITHUB 0u

int md_parse(const char* text, unsigned size, const MD_PARSER* parser, void* userdata);

#ifdef __cplusplus
}
#endif

#endif
