# Markdown Elements Fixture

This file is both documentation for microagenda' Markdown rendering scope and a
manual regression fixture for the preview renderer. Open it as a note and verify
that each section below renders cleanly, wraps without overflow, scrolls
correctly, and preserves clickable link behavior.

## Table Of Contents

- [Paragraphs And Line Breaks](#paragraphs-and-line-breaks)
- [Inline Formatting](#inline-formatting)
- [Links And Images](#links-and-images)
- [Lists](#lists)
- [Blockquotes And Admonitions](#blockquotes-and-admonitions)
- [Code](#code)
- [Tables](#tables)
- [Footnotes](#footnotes)
- [Horizontal Rules](#horizontal-rules)
- [Raw HTML And Entities](#raw-html-and-entities)

## Paragraphs And Line Breaks

This is a normal paragraph with enough text to wrap across multiple lines in a
narrow preview pane. The renderer should keep the text crisp, readable, and
inside the viewer bounds.

This paragraph contains a hard line break after this sentence.  
This line should begin directly below it.

Soft line breaks in Markdown
should render as normal spaces inside the paragraph.

## Inline Formatting

Plain text can be mixed with *emphasis*, **strong text**, and
***strong emphasis***.

Inline code such as `printf("%s", value)` should use a monospace style.

GFM strikethrough should render as ~~deleted text~~.

Escaped Markdown punctuation should remain visible: \*not emphasis\*,
\[not a link\], and \`not code\`.

## Links And Images

This is an external URL link: [microagenda project](https://example.com).

This is an intra-note heading link: [jump to Tables](#tables).

This is a same-note style anchor link: [jump to Footnotes](markdown-elements.md#footnotes).

Autolinks should render as links when supported by md4c GFM:
<https://example.com/autolink>.

This is a local file link intended to exercise safe local opening:
[local attachment placeholder](.microagenda/attachments/example/document.pdf).

This is an image attachment placeholder:

![Example local image](.microagenda/attachments/example/image.png)

Remote images should not be downloaded by the renderer:

![Remote image](https://example.com/image.png)

## Lists

Unordered lists:

- Red
- Green
- Blue

Nested unordered lists:

- Parent item
  - Child item
    - Grandchild item
- Second parent item

Ordered lists:

1. Bird
2. McHale
3. Parish

Ordered lists with a custom start:

4. Fourth
5. Fifth
6. Sixth

Nested mixed lists:

1. Plan
   - Research
   - Implement
2. Verify
   1. Build
   2. Test

Task lists:

- [x] Parse GFM task list syntax
- [ ] Toggle interaction is not implemented; this is render-only
- [x] Keep checkbox alignment stable

## Blockquotes And Admonitions

> This is a blockquote. It should show quoted content distinctly while still
> wrapping cleanly across multiple lines.

Nested blockquotes:

> Outer quote
>
> > Inner quote

GFM admonitions use blockquote syntax:

> [!NOTE]
> Notes should render as native callouts.

> [!TIP]
> Tips use the same structure with a different title.

> [!IMPORTANT]
> Important callouts should stay readable.

> [!WARNING]
> Warning callouts should not break layout.

> [!CAUTION]
> Caution callouts should not overflow.

## Code

Inline code was covered above. This is an indented code block:

    tell application "Foo"
      beep
    end tell

This is a fenced code block with an info string:

```cpp
#include <iostream>

int main() {
  std::cout << "hello from microagenda\n";
  return 0;
}
```

This fenced block contains long lines that should be clipped or wrapped safely by
the viewer instead of overflowing into another pane:

```text
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
```

## Tables

Simple table:

| Feature | Status | Notes |
| --- | --- | --- |
| Paragraphs | Done | Wrapped text |
| Lists | Done | Ordered, unordered, task |
| Tables | Done | Header and body rows |

Aligned table:

| Left | Center | Right |
|:-----|:------:|------:|
| alpha | beta | gamma |
| short | medium content | 12345 |

Wide table cells should wrap or clip inside the viewer:

| Column | Long content |
| --- | --- |
| A | This sentence is intentionally long so that the table renderer has to measure the column and keep the content inside the available width. |
| B | Links in tables should be clickable: [example](https://example.com/table-link). |

## Footnotes

This sentence has a footnote reference.[^first]

This sentence has a second footnote reference.[^second]

[^first]: This is the first footnote definition. It should render in the
    generated footnote section and be reachable from the reference.

[^second]: This is the second footnote definition with `inline code` and
    **strong text**.

## Horizontal Rules

Text before the rule.

---

Text after the rule.

## Raw HTML And Entities

Raw inline HTML should be displayed safely rather than executed:
<span class="raw-html">inline html</span>.

Raw HTML blocks should be displayed safely:

<div class="raw-html-block">
  This is raw HTML source.
</div>

Entities should decode like md4c HTML output: Fish &amp; chips, 2 &lt; 3,
and copyright &#169;.

Null characters are not included in this fixture, but the parser maps them to
the Unicode replacement character.
