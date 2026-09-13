"""
Fill the TELECOM 2026 IEEE Word template with the content of paper/main.pdf.

The template supplies every style (margins, columns, fonts) and auto-numbers
headings, references and table captions, so this script must NOT type numbers
like "I." or "TABLE I" or "[1]" - it only supplies text and the style name.

Text is written with `backticks` marking inline code spans, which are re-run
in Courier New. Numbers are transcribed verbatim from the LaTeX sources; none
are recomputed here.
"""
import os
import copy
import docx
from docx.shared import Pt, Inches, RGBColor
from docx.oxml.ns import qn
from docx.oxml import OxmlElement
from docx.enum.text import WD_ALIGN_PARAGRAPH

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "template.docx")
OUTPUT = os.path.join(HERE, "Tsvetanov-ctpb-TELECOM2026.docx")

doc = docx.Document(TEMPLATE)
body = doc.element.body

# ---------------------------------------------------------------- clear body
# Keep only the trailing sectPr (page size, margins, 2-column body setup).
final_sectPr = body.find(qn("w:sectPr"))
for child in list(body):
    if child is not final_sectPr:
        body.remove(child)


def set_cols(sectPr, num):
    cols = sectPr.find(qn("w:cols"))
    if cols is None:
        cols = OxmlElement("w:cols")
        sectPr.append(cols)
    cols.set(qn("w:num"), str(num))
    cols.set(qn("w:space"), "360")


set_cols(final_sectPr, 2)  # body is two-column


def add_p(text, style=None, size=None, align=None, space_after=None):
    """Add a paragraph; `backtick` spans become Courier New runs."""
    p = doc.add_paragraph()
    if style:
        p.style = doc.styles[style]
    for i, part in enumerate(text.split("`")):
        if not part:
            continue
        run = p.add_run(part)
        if i % 2 == 1:
            run.font.name = "Courier New"
            rpr = run._element.get_or_add_rPr()
            rf = rpr.find(qn("w:rFonts"))
            if rf is None:
                rf = OxmlElement("w:rFonts")
                rpr.append(rf)
            for a in ("w:ascii", "w:hAnsi", "w:cs"):
                rf.set(qn(a), "Courier New")
            if size is None:
                run.font.size = Pt(8.5)
        if size:
            run.font.size = Pt(size)
    if align is not None:
        p.alignment = align
    if space_after is not None:
        p.paragraph_format.space_after = Pt(space_after)
    return p


def set_run_font(run, name):
    rpr = run._element.get_or_add_rPr()
    rf = rpr.find(qn("w:rFonts"))
    if rf is None:
        rf = OxmlElement("w:rFonts")
        rpr.append(rf)
    for a in ("w:ascii", "w:hAnsi", "w:cs"):
        rf.set(qn(a), name)


def add_equation(text):
    """The template's `equation` style is set in Symbol font, which renders
    Latin letters as Greek. Force a text font on the run."""
    p = doc.add_paragraph()
    p.style = doc.styles["equation"]
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run(text)
    set_run_font(run, "Times New Roman")
    run.font.size = Pt(10)
    return p


def add_heading_unnumbered(text):
    """IEEE convention: Acknowledgment and References carry no section number."""
    p = add_p(text, style="heading 1")
    pPr = p._element.get_or_add_pPr()
    numPr = OxmlElement("w:numPr")
    ilvl = OxmlElement("w:ilvl")
    ilvl.set(qn("w:val"), "0")
    numId = OxmlElement("w:numId")
    numId.set(qn("w:val"), "0")
    numPr.append(ilvl)
    numPr.append(numId)
    pPr.append(numPr)
    return p


def keep_together(table, caption_p):
    """Stop Word splitting a small table across the column boundary."""
    caption_p.paragraph_format.keep_with_next = True
    rows = list(table.rows)
    for ri, row in enumerate(rows):
        trPr = row._tr.get_or_add_trPr()
        trPr.append(OxmlElement("w:cantSplit"))
        if ri < len(rows) - 1:
            for cell in row.cells:
                for p in cell.paragraphs:
                    p.paragraph_format.keep_with_next = True


# Colours transcribed from the lstlisting style in main.tex so the Word
# listings match the LaTeX ones: keywordstyle=magenta, commentstyle=codegreen
# rgb(0,0.6,0), stringstyle=codepurple rgb(0.58,0,0.82), numberstyle=codegray
# rgb(0.5,0.5,0.5), backgroundcolor=backcolour rgb(0.95,0.95,0.92).
COL_KW = RGBColor(0xFF, 0x00, 0xFF)
COL_COMMENT = RGBColor(0x00, 0x99, 0x00)
COL_STRING = RGBColor(0x94, 0x00, 0xD1)
COL_LINENO = RGBColor(0x80, 0x80, 0x80)
CODE_BG = "F2F2EB"

# The ModernC++ keyword set defined in main.tex.
CPP_KEYWORDS = set("""
alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto
bitand bitor bool break case catch char char8_t char16_t char32_t class compl
concept const consteval constexpr constinit const_cast continue contract_assert
co_await co_return co_yield decltype default delete do double dynamic_cast else
enum explicit export extern false float for friend goto if inline int long
mutable namespace new noexcept not not_eq nullptr operator or or_eq private
protected public reflexpr register reinterpret_cast requires return short signed
sizeof static static_assert static_cast struct switch synchronized template this
thread_local throw true try typedef typeid typename union unsigned using virtual
void volatile wchar_t while xor xor_eq
""".split())


def tokenize_cpp(line):
    """Split a C++ line into (text, kind) with kind in plain/kw/str/com."""
    out, buf, i, n = [], "", 0, len(line)

    def flush():
        nonlocal buf
        if buf:
            out.append((buf, "plain"))
            buf = ""

    while i < n:
        if line.startswith("//", i):          # line comment runs to EOL
            flush()
            out.append((line[i:], "com"))
            return out
        if line[i] == '"':                     # string literal
            flush()
            j = i + 1
            while j < n and line[j] != '"':
                j += 2 if line[j] == "\\" else 1
            out.append((line[i:j + 1], "str"))
            i = j + 1
            continue
        if line[i].isalpha() or line[i] == "_":
            j = i
            while j < n and (line[j].isalnum() or line[j] == "_"):
                j += 1
            word = line[i:j]
            if word in CPP_KEYWORDS:
                flush()
                out.append((word, "kw"))
            else:
                buf += word
            i = j
            continue
        buf += line[i]
        i += 1
    flush()
    return out


def add_code(lines):
    """Syntax-highlighted monospace block with gray line numbers and the
    same light background the LaTeX listings use."""
    colours = {"kw": COL_KW, "str": COL_STRING, "com": COL_COMMENT}
    width = len(str(len(lines)))
    for idx, ln in enumerate(lines, start=1):
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.LEFT
        pf = p.paragraph_format
        pf.space_before = Pt(0)
        pf.space_after = Pt(0)
        pf.line_spacing = 1.0
        # paragraph background, matching backgroundcolor=backcolour
        shd = OxmlElement("w:shd")
        shd.set(qn("w:val"), "clear")
        shd.set(qn("w:fill"), CODE_BG)
        p._element.get_or_add_pPr().append(shd)

        def emit(text, colour=None, size=7):
            run = p.add_run(text)
            set_run_font(run, "Courier New")
            run.font.size = Pt(size)
            if colour is not None:
                run.font.color.rgb = colour
            return run

        emit(str(idx).rjust(width) + "  ", COL_LINENO, size=5.5)
        if not ln:
            emit(" ")
            continue
        for text, kind in tokenize_cpp(ln):
            emit(text, colours.get(kind))


def set_borders(table):
    tblPr = table._tbl.tblPr
    borders = OxmlElement("w:tblBorders")
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        e = OxmlElement("w:" + edge)
        e.set(qn("w:val"), "single")
        e.set(qn("w:sz"), "4")
        e.set(qn("w:color"), "000000")
        borders.append(e)
    tblPr.append(borders)


def add_table(caption, headers, rows, bold_first_col_rows=(), widths=None):
    cap = add_p(caption, style="table head")
    t = doc.add_table(rows=1 + len(rows), cols=len(headers))
    set_borders(t)
    if widths is None:
        widths = [Inches(3.38 / len(headers)) for _ in headers]
    t.autofit = False
    grid_cols = t._tbl.tblGrid.gridCol_lst
    for j, wdt in enumerate(widths):
        grid_cols[j].set(qn("w:w"), str(wdt.twips))
        for row in t.rows:
            row.cells[j].width = wdt
    tbl_width = t._tbl.tblPr.find(qn("w:tblW"))
    tbl_width.set(qn("w:w"), str(sum(wdt.twips for wdt in widths)))
    tbl_width.set(qn("w:type"), "dxa")
    for j, h in enumerate(headers):
        cell = t.cell(0, j)
        cell.text = ""
        p = cell.paragraphs[0]
        p.style = doc.styles["table col head"]
        r = p.add_run(h)
        r.bold = True
        r.font.size = Pt(7)
    for i, row in enumerate(rows):
        for j, val in enumerate(row):
            cell = t.cell(i + 1, j)
            cell.text = ""
            p = cell.paragraphs[0]
            p.style = doc.styles["table copy"]
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER if j else WD_ALIGN_PARAGRAPH.LEFT
            r = p.add_run(val)
            r.font.size = Pt(7)
            if i in bold_first_col_rows:
                r.bold = True
    keep_together(t, cap)
    doc.add_paragraph()
    return t


# ============================================================ TITLE (1 column)
add_p("Zero-Codegen Protocol Buffers with C++26 Static Reflection", style="paper title")

add_p("Alex I. Tsvetanov", style="Author")
add_p("Faculty of Computer Systems and Technologies", style="Affiliation")
add_p("Technical University of Sofia", style="Affiliation")
add_p("Sofia, Bulgaria", style="Affiliation")
last_title_p = add_p("atsvetanov@tu-sofia.bg", style="Affiliation")

# Section break: everything above is single-column, everything below is two.
sectPr_title = copy.deepcopy(final_sectPr)
typ = sectPr_title.find(qn("w:type"))
if typ is None:
    typ = OxmlElement("w:type")
    sectPr_title.insert(0, typ)
typ.set(qn("w:val"), "continuous")
set_cols(sectPr_title, 1)
last_title_p._element.get_or_add_pPr().append(sectPr_title)

# ========================================================== ABSTRACT (2 col)
abstract = (
    "Serialization frameworks such as Protocol Buffers typically rely on external "
    "code generators (protoc) to translate schemas into C++ files, which adds "
    "build rules, version coupling, and generated source. This paper presents "
    "ctpb, a header-only C++26 library that uses static reflection to parse "
    ".proto schemas and synthesize C++ types during "
    "compilation. The library carries the schema in the translation unit as a "
    "compile-time string, performs "
    "a constexpr recursive-descent parse into an Abstract Syntax Tree, and "
    "materializes type-safe fields and wire-format visitors without generated "
    ".pb.h or .pb.cc files. On one host, a synthetic 100-field schema is "
    "parsed in 7.487 microseconds (range 7.442 to 7.515 microseconds). For a "
    "47-byte, three-field Person workload, ctpb encodes 4.80x faster than "
    "libprotobuf 3.21.12, while decoding leads by 1.26x to 1.46x and includes "
    "proto3 UTF-8 validation. A stripped sample executable is 18,792 bytes versus "
    "1,759,368 bytes for the generated-code static-runtime comparison. Removing the "
    "generator relocates its cost into the front end rather than eliminating it: the "
    "same schema at 100 fields builds 2.87x more slowly than the generated path. "
    "Wire-format fidelity is verified bidirectionally against the reference "
    "implementation."
)
p = doc.add_paragraph()
p.style = doc.styles["Abstract"]
r = p.add_run("Abstract—")
r.bold = True
r.italic = True
p.add_run(abstract)

p = doc.add_paragraph()
p.style = doc.styles["Keywords"]
r = p.add_run("Keywords—")
r.italic = True
p.add_run("Protocol Buffers, C++26, static reflection, "
          "compile-time metaprogramming, zero-codegen serialization")

# ============================================================= I. INTRODUCTION
add_p("Introduction", style="heading 1")
add_p(
    "Distributed systems and microservice architectures rely on Interface "
    "Definition Languages such as Google Protocol Buffers [1], FlatBuffers [2] "
    "and Cap'n Proto [3]. One schema definition serves several languages, the "
    "encoding it produces is compact on the wire, and a message can gain fields "
    "without breaking readers built against an earlier version of the schema.",
    style="Body Text")
add_p(
    "Protocol Buffers, however, depend on an external code generator. Building a "
    "C++ project that uses Protobuf requires the build system to invoke `protoc` "
    "and produce intermediate `.pb.h` and `.pb.cc` files. This imposes four "
    "costs: custom build rules and dependency graphs to track generated "
    "artifacts; version skew between the runtime `libprotobuf` and the build-time "
    "`protoc`, a frequent source of ABI mismatches; codebase bloat, since large "
    "systems generate hundreds of thousands of lines that slow incremental "
    "builds; and the loss of direct language integration, as schemas remain "
    "external build targets rather than first-class compile-time entities.",
    style="Body Text")
add_p(
    "C++26 makes a different arrangement possible in compilers that implement "
    "it. P2996 Static Reflection [5] provides programmatic type synthesis "
    "through splicing, and P1967 `#embed` [4] is the standardized way to bring "
    "an external file into a translation unit without generating C++ source with "
    "tools such as `xxd -i`. `ctpb` is built on the former; the evaluated "
    "implementation receives the schema text directly rather than through "
    "`#embed`.",
    style="Body Text")
add_p(
    "We present `ctpb` (Compile-Time Protobuf) [6], a header-only C++26 library "
    "that carries `.proto` schemas in the translation unit as compile-time "
    "strings, runs a `constexpr` "
    "recursive-descent parser in the compiler front-end, and synthesizes type-safe "
    "structs and wire-format routines through static reflection. The contribution "
    "is a three-stage architecture that turns raw IDL text into first-class C++ "
    "types without generated intermediate files. Its parser handles nested "
    "messages, enums, maps, oneofs, and reserved ranges, while P2996 injects "
    "fields and makes wire visitors available programmatically. The evaluation "
    "reports a 7.487 microsecond parse for a synthetic 100-field schema and a "
    "4.80x encoding speedup over libprotobuf 3.21.12 on a 47-byte Person workload, "
    "with bidirectional wire-format tests against the reference implementation, and "
    "it measures the build cost the removed generator is replaced by.",
    style="Body Text")

# ================================================================ II. BACKGROUND
add_p("Background", style="heading 1")
add_p("Protocol Buffers Wire Format", style="heading 2")
add_p(
    "Protocol Buffers define a compact binary format in which each field is "
    "prefixed by a key encoded as a variable-length integer (varint):",
    style="Body Text")
add_equation("Key = (Field Number << 3) | Wire Type")
add_p(
    "Four wire types remain live in proto3: varint (`int32`, `int64`, `uint32`, "
    "`uint64`, `bool`, enums), 64-bit and 32-bit fixed (`fixed64`/`double` and "
    "`fixed32`/`float`), and length-delimited (`string`, `bytes`, submessages, "
    "and packed repeated scalars). Signed integers (`sint32`, `sint64`) apply "
    "Zigzag encoding, which maps a signed value to an unsigned one by shifting "
    "left once and XOR-ing with the arithmetic right shift by the width minus "
    "one, so that small negative values do not occupy the full varint width.",
    style="Body Text")

add_p("C++26 Language Facilities", style="heading 2")
add_p(
    "Two C++26 proposals bear on compile-time IDL ingestion. "
    "P1967 [4] adds the `#embed` directive, which expands an external resource "
    "into values that can initialize compile-time storage without a generated "
    "source file. P2996 [5] adds static reflection around the opaque "
    "`std::meta::info` type, supporting introspection, programmatic generation "
    "of member descriptors (`define_aggregate()`, `data_member_spec()`), and "
    "splicing of reflected entities into concrete declarations. Together with "
    "`constexpr` evaluation, these facilities let the implementation parse an "
    "external domain-specific language during compilation and construct native "
    "types.", style="Body Text")

# ============================================================ III. ARCHITECTURE
add_p("Architecture and Compile-Time Pipeline", style="heading 1")
add_p(
    "`ctpb` is organized as a three-stage pipeline executing entirely within the "
    "compiler front-end prior to code emission.", style="Body Text")
add_p(
    "Stage 1, source ingestion. The schema text is supplied as a non-type "
    "template argument and held in immutable compile-time storage, mapped to a "
    "zero-copy `std::string_view`. `#embed` is the standardized route from a "
    "`.proto` file to that storage without a generated source file; the "
    "evaluated implementation takes the text itself, so the file-reading step is "
    "not exercised here.",
    style="Body Text")
add_p(
    "Stage 2, constexpr lexing and AST construction. The `ctpb::parser` is "
    "instantiated inside a `consteval` context, tokenizing the grammar on demand "
    "without backtracking and emitting an immutable AST (`ctpb::ast::file`) "
    "holding messages, fields, enums, oneofs, maps, reserved regions, and "
    "options.", style="Body Text")
add_p(
    "Stage 3, reflection splicing and type synthesis. `define_message` looks up "
    "the requested entity in the AST and, via P2996 `define_aggregate`, injects a "
    "struct whose members match each field directly into the active translation "
    "unit. Generic `ctpb::encode` and `ctpb::decode` visitors use the resulting "
    "metadata.", style="Body Text")
add_p("The resulting public interface requires no macros and no build step:",
      style="Body Text")
add_code([
    '#include <ctpb/ctpb.hpp>',
    '',
    'inline constexpr ctpb::fixed_string person_proto = R"(',
    '  syntax = "proto3"; package tutorial;',
    '  message Person { string name = 1; int32 id = 2; }',
    ')";',
    '',
    '// Compile-time schema synthesis',
    'consteval { ctpb::define_message<person_proto, "Person">(); }',
    'using Person = ctpb::message_t<person_proto, "Person">;',
    '',
    'Person p{};',
    'p.name = "Alex I. Tsvetanov";',
    'p.id   = 42;',
    '',
    'std::vector<std::byte> wire = ctpb::encode(p);',
    'auto decoded = ctpb::decode<Person>(wire); // std::expected<Person, ctpb::wire::decode_error>',
])
doc.add_paragraph()
add_p("Field offsets, wire tags, and type checks are resolved at compile time; "
      "runtime operations use the generated metadata rather than a reflection "
      "registry.", style="Body Text")

# ======================================================= IV. LEXING AND PARSING
add_p("Constexpr Lexing and Parsing", style="heading 1")
add_p("Zero-Copy Tokenizer", style="heading 2")
add_p(
    "The `ctpb::lexer` processes raw IDL text during constant evaluation. Unlike "
    "runtime tokenizers that allocate storage for lexemes, every identifier, "
    "keyword, and literal is retained as a `std::string_view` referencing the "
    "embedded source, so tokenization performs zero dynamic allocations. The "
    "lexer recognizes the proto3 keyword set (`syntax`, `package`, `import`, "
    "`message`, `enum`, `oneof`, `map`, `reserved`), integer and floating-point "
    "literals including hexadecimal, single- and multi-line comments, and "
    "punctuation, in linear time.", style="Body Text")
add_p("Recursive-Descent Parser", style="heading 2")
add_p(
    "The proto3 grammar is formalized into approximately fifty LL(1) productions, "
    "and the parser builds the structural AST in a single pass without "
    "backtracking. Message parsing dispatches on the leading token of each body "
    "element, recursing for nested messages and delegating to dedicated "
    "productions for enums, oneofs, maps, and reserved ranges; anything else is "
    "parsed as a field declaration.", style="Body Text")
add_p("Compile-Time Diagnostics", style="heading 2")
add_p(
    "When a syntax violation, invalid field number, or reserved-field conflict is "
    "detected during `consteval` evaluation, `ctpb` raises an exception carrying "
    "the source line and column. Under ISO C++ constant-evaluation rules, "
    "throwing inside a `consteval` context terminates compilation, so the schema "
    "error surfaces as a localized compiler diagnostic rather than a runtime "
    "failure.", style="Body Text")

# ======================================================= V. STATIC REFLECTION
add_p("Static Reflection and Type Generation", style="heading 1")
add_p("P2996 Reflection Pipeline", style="heading 2")
add_p(
    "Once the AST is parsed, `ctpb` maps the schema onto a native aggregate using "
    "`std::meta::info` reflection tokens and compile-time member descriptors. "
    "This is the step that replaces `protoc`: the struct is not written to disk "
    "and re-read, but constructed directly in the translation unit.",
    style="Body Text")
add_code([
    'template <fixed_string ProtoSource,',
    '          fixed_string MessageName>',
    'consteval auto complete_message_struct(',
    '        std::meta::info incomplete_type) {',
    '  using Schema = parse<ProtoSource, MessageName>;',
    '  std::vector<std::meta::info> members;',
    '',
    '  for (const field_info& info :',
    '           Schema::declared_fields()) {',
    '    std::meta::info type_info = ^^int;',
    '    if (info.type == "string")',
    '      type_info = ^^std::string;',
    '    else if (info.type == "uint32")',
    '      type_info = ^^unsigned int;',
    '    else if (info.type == "int64")',
    '      type_info = ^^long long;',
    '    else if (info.type == "double")',
    '      type_info = ^^double;',
    '    else if (info.type == "bool")',
    '      type_info = ^^bool;',
    '    else if (info.type == "bytes")',
    '      type_info = ^^std::vector<std::byte>;',
    '    // ... remaining scalar types elided; an enum',
    '    // field keeps the default `int`, since proto3',
    '    // encodes enums as varints.',
    '',
    '    if (info.is_repeated)',
    '      type_info = std::meta::substitute(',
    '          ^^std::vector, {type_info});',
    '',
    '    members.push_back(std::meta::data_member_spec(',
    '        type_info, {.name = info.name}));',
    '  }',
    '',
    '  return std::meta::define_aggregate(',
    '      incomplete_type, members);',
    '}',
])
doc.add_paragraph()
add_p("Type Mapping and Generated Interface", style="heading 2")
add_p(
    "The synthesized members follow a fixed mapping: the sized integer "
    "types map to the corresponding C++ integer types (with `sint` "
    "and `sfixed` variants sharing the signed representation and differing only "
    "in wire encoding), `float`, `double` and `bool` map to their C++ "
    "equivalents, `string` to `std::string`, `bytes` to "
    "`std::vector<std::byte>`, and `repeated T` to `std::vector<T>`. The mapping "
    "is partial rather than complete: `map` and `oneof` members are parsed into "
    "the AST but are not yet synthesized, and a field whose type is an enum or a "
    "nested message is emitted as `int` rather than as a distinct type.",
    style="Body Text")
add_p(
    "Beyond member generation, `ctpb` supplies direct dot-syntax field access "
    "(`p.name`) and non-intrusive `ctpb::encode` and `ctpb::decode` visitors that "
    "traverse the member descriptors reflectively.", style="Body Text")

# ======================================================== VI. WIRE FORMAT ENGINE
add_p("Wire Format Engine", style="heading 1")
add_p("Encoding", style="heading 2")
add_p(
    "The serialization engine in `ctpb::wire` manipulates the byte stream "
    "directly, without intermediate buffers. Encoding proceeds in two passes: "
    "`byte_size()` computes the exact wire length from compile-time field "
    "metadata, the output buffer is allocated once, and the fields are then "
    "written through a raw pointer with no reallocation or virtual dispatch. "
    "Varints emit seven payload bits per byte with the high bit signalling "
    "continuation; signed integers are Zigzag-converted with single-instruction "
    "shifts and XORs; and repeated numeric scalars, which proto3 packs by "
    "default, have their payload size computed inline and are written as one "
    "contiguous length-delimited record under a single tag.", style="Body Text")
add_p("Deserialization and Error Recovery", style="heading 2")
add_p(
    "The `ctpb::wire::reader` operates on a non-owning "
    "`std::span<const std::byte>` and bounds-checks every read: varint decoding "
    "refuses to advance past the buffer end or beyond the ten-byte maximum width, "
    "and length-delimited reads validate the declared length against the "
    "remaining bytes before yielding a subspan. `ctpb::decode` returns failures "
    "as `std::expected<T, decode_error>` rather than throwing, so decoding remains usable in "
    "`noexcept` contexts.", style="Body Text")
add_p(
    "Unknown field numbers are tolerated during decoding. The `skip_field` method uses the "
    "wire type alone to advance past an unrecognized integer, fixed-width block, "
    "or length-delimited payload, allowing an older `ctpb` reader to consume a "
    "message containing newer fields. Skipped fields are not retained for "
    "re-serialization.", style="Body Text")

# ======================================================== VII. EVALUATION
add_p("Experimental Evaluation", style="heading 1")
add_p(
    "All results in this section come from one sitting on 13 September "
    "2026: an AMD Ryzen 7 5800H with 16 logical CPUs running Linux 7.2.3. "
    "Every CPU ran at the `performance` scaling governor for the duration, as "
    "the run's own environment capture records. No frequency samples "
    "accompany this sitting's record. The timed work ran in a container image "
    "built from the project's own recipe [6], using the experimental Clang 21 "
    "branch that implements P2996, `-std=c++26 -freflection -O3`, and "
    "Protobuf 3.21.12.",
    style="Body Text")
add_p(
    "The reflection implementation is the one measured: the P2996 feature gate is "
    "verified open in the measured configuration, and the same command with "
    "`-stdlib=libstdc++` closes it, because <meta> ships only with libc++. Trial 0 is "
    "excluded as a warm-up; all tables report the median and full range of the "
    "six retained trials. The results describe this host and these workloads, "
    "not performance across machines. Raw trial lines, the environment record, "
    "and the scripts that produced them are published with the artifact [6].",
    style="Body Text")

add_p("Compile-Time Schema Parsing", style="heading 2")
add_p(
    "We measured the `constexpr` parser on three schemas scaled by field count "
    "alone (5, 25, and 100 `int32` fields), each over 20,000 iterations after a "
    "2,000-iteration warm-up, across seven trials (Table I, median and full range "
    "of six retained trials). The parser is compiled as ordinary optimized code and "
    "timed at run time, so these figures bound the work the front end does per schema "
    "rather than measuring it inside constant evaluation, where the same code runs on "
    "the compiler's interpreter.", style="Body Text")
add_table(
    "Parser Latency, Compiled as Ordinary Code, Median and Range of Six Trials",
    ["Schema", "Fields", "Latency / Parse", "Throughput"],
    [
        ["Small", "5", "0.522 [0.515, 0.527] µs", "1,917,546 parses/s"],
        ["Medium", "25", "2.013 [1.985, 2.048] µs", "496,894 parses/s"],
        ["Large", "100", "7.487 [7.442, 7.515] µs", "133,556 parses/s"],
    ],
    widths=[Inches(0.58), Inches(0.42), Inches(1.35), Inches(1.03)])
add_p(
    "A 100-field schema is ingested in 7.487 microseconds. This table is unaffected "
    "by the feature gate: a parser-only translation unit never instantiates the "
    "synthesis code, and the same probe compiled with the gate open and forced shut "
    "produces byte-identical text.",
    style="Body Text")
add_p(
    "The ceiling on schema size is a property of the compiler's evaluation budget "
    "rather than of the schema language. Bisecting `define_message` at Clang's "
    "default `constexpr` step limit synthesizes a largest single message of 317 "
    "fields and fails at 318. The diagnostic locates the exhaustion in the "
    "destructor of the std::vector of member descriptors that synthesis builds, not "
    "in parsing or in `define_aggregate`, so the limit is reached by bookkeeping the "
    "compiler performs on the descriptor list. Raising the budget to 100,000,000 "
    "steps compiles 318, 400, 800, 1,600 and 3,200 fields, the largest attempted, in "
    "19.8 s; no ceiling was found. Any \"supports N fields\" claim is therefore a "
    "statement about a compiler flag, and `ctpb` sets none.", style="Body Text")

add_p("Build Cost", style="heading 2")
add_p(
    "`ctpb` moves schema processing from a generator invocation into the compilation "
    "of the translation unit that uses the schema, so a comparison that measures only "
    "run time omits the side where the cost was moved to. We therefore timed the whole "
    "path from a .proto file to a linked program on both sides: for `ctpb`, compiling "
    "one translation unit that ingests the schema; for the generated path, running "
    "`protoc`, compiling the generated .pb.cc, compiling an equivalent user translation "
    "unit, and linking, all counted.", style="Body Text")
add_table(
    "Build Cost from .proto to Linked Program, Median [Min, Max] of Six Trials, Seconds",
    ["Fields", "ctpb", "Generated", "Ratio"],
    [
        ["5", "2.09 [2.08, 2.10]", "2.49 [2.47, 2.50]", "0.84x"],
        ["25", "2.95 [2.90, 2.99]", "2.63 [2.60, 2.66]", "1.12x"],
        ["100", "9.05 [8.59, 9.14]", "3.15 [3.12, 3.21]", "2.87x"],
    ],
    widths=[Inches(0.52), Inches(1.03), Inches(1.03), Inches(0.80)])
add_p(
    "The ordering reverses with schema size. At 5 fields `ctpb` builds faster than the "
    "generated path, by 25 fields it is already slower, and at 100 fields it costs "
    "2.87x as much. The two sides also scale differently rather than differing by a "
    "constant: `protoc` itself never exceeds 0.015 s and the link 0.058 s, so the "
    "generated path's growth is almost entirely the cost of compiling more generated "
    "code, which grows mildly, while `ctpb`'s growth is constant evaluation of a longer "
    "schema, which does not. Removing the generator does not remove the work; it "
    "relocates it into the front end, where at this scale it is more expensive.",
    style="Body Text")

add_p("Runtime Serialization Throughput", style="heading 2")
add_p(
    "We evaluated encoding and decoding on a 3-field `Person` message with a "
    "47-byte wire payload against full `libprotobuf`. The two arms are separate "
    "programs, because they cannot share one: <meta> ships only with libc++, while "
    "the reference library is built against libstdc++ and does not link against "
    "libc++. The timed loop is one header compiled into both arms rather than two "
    "loops intended to match, both arms are held behind a compiler barrier, both "
    "encode into a preallocated buffer and decode into a reused target, and both "
    "assert a correct round trip before timing. The arms are alternated across five "
    "repetitions of twenty rounds each and the table reports the median and range "
    "over all 100 rounds. The two arms are verified to produce identical wire bytes; "
    "the comparison is void otherwise.", style="Body Text")
add_table(
    "Runtime Latency, Median [Min, Max] over 100 Rounds, 47-Byte Payload",
    ["Operation", "ctpb (ns)", "libprotobuf (ns)", "Ratio"],
    [
        ["Encode", "7.70 [7.10, 9.11]", "36.97 [33.57, 41.37]", "4.80x"],
        ["Decode", "50.38 [45.33, 52.77]", "70.39 [63.85, 77.37]", "1.26-1.46x"],
    ],
    widths=[Inches(0.72), Inches(1.00), Inches(1.05), Inches(0.70)])
add_p(
    "For this workload `ctpb` encodes 4.80x faster. Field sizes are known to the compiler, "
    "so the encoder allocates one exactly sized buffer through `byte_size()` and "
    "writes fields through raw pointers without reallocation or virtual dispatch. "
    "These results are limited to the message shape and allocation policy "
    "exercised here.", style="Body Text")
add_p(
    "Decoding is the narrower and the more fragile result, and is quoted as a range "
    "because five repetitions do not support a point estimate: the per-repetition "
    "ratio spans 1.26x to 1.46x. All reported `ctpb` measurements include UTF-8 "
    "validation for `string` fields. Two properties of the comparison bound how far "
    "it generalizes. The arms link different C++ runtimes, libc++ and libstdc++, "
    "because neither builds the other way. More importantly the decode margin is "
    "specific to the 3.21.12 baseline: a single unpaired run of `libprotobuf` 36.0.0, "
    "built alone on the host toolchain because no `ctpb` arm exists there, decoded "
    "the same message in 48.9 ns against the 50.4 ns measured here for `ctpb`. That "
    "run shares neither compiler nor container with the table and establishes nothing "
    "on its own, but it is sufficient to say that the decoding result should not be "
    "read as a margin over Protocol Buffers in general. The encoding margin is not at "
    "risk from the same comparison, which encoded more slowly rather than faster.",
    style="Body Text")

add_p("Binary Footprint", style="heading 2")
add_table(
    "Binary Footprint and Generated Source, Exact Bytes",
    ["Metric", "ctpb", "Generated (dyn.)", "Generated (static)"],
    [
        ["Generated Source Lines", "0", "754", "754"],
        ["Stripped Executable", "18,792 B", "27,504 B", "1,759,368 B"],
        ["Code (.text)", "7,027 B", "4,728 B", "1,323,755 B"],
        ["Protobuf Runtime Dependency", "none", "libprotobuf.so", "none"],
    ],
    widths=[Inches(1.30), Inches(0.45), Inches(0.75), Inches(0.88)])
add_p(
    "For a single 3-field message, `protoc` emits 754 lines across `.pb.h` and "
    "`.pb.cc`, whereas `ctpb` emits no generated files. The generated application "
    "linked dynamically is 1.46x larger as a file and the statically linked one "
    "93.62x larger.", style="Body Text")
add_p(
    "The two rows point in opposite directions and the second is the more "
    "informative. Measured as exact .text, `ctpb`'s own code is 1.49x larger than "
    "the dynamically linked generated program's, because that program's encoder and "
    "decoder are not in it: they are in libprotobuf.so, which the file metric does "
    "not count. The static column is the same library moved inside the binary, at "
    "188x `ctpb`'s code. What the comparison shows is therefore not that `ctpb` "
    "generates less code, but that it carries its serialization logic in the "
    "executable at a size comparable to a small dynamic stub and two orders of "
    "magnitude below the bundled runtime. All file sizes exclude dynamically linked "
    "system libraries; the `ctpb` sample has no Protocol Buffers runtime dependency "
    "but still links the system C++ runtime. ELF page alignment gives the "
    "stripped-file metric a resolution floor of about 4 KB, so the file rows are "
    "coarse for small changes.", style="Body Text")

add_p("Wire Interoperability and Message Framing", style="heading 2")
add_p(
    "Correctness was assessed through dual-direction interoperability testing "
    "against `libprotobuf` 3.21.12 and the `protoc` code generator [1]. Messages "
    "serialized by the Google `SerializeToString()` decode through "
    "`ctpb::decode<T>()` for the exercised cases across scalars, Zigzag integers, "
    "strings, enums, nested messages, `bytes` fields, and packed repeated "
    "scalars; in the reverse direction, buffers produced by `ctpb::encode()` "
    "parse through the Google `ParseFromArray()`, with wire tags, varints, and "
    "length delimiters accepted by the reference parser. "
    "Packed repeated fields and `bytes` are covered explicitly because both are "
    "encoded differently from the singular scalar path and are the cases where an "
    "independent implementation most easily diverges.", style="Body Text")
add_p(
    "The tests also cover the message framing gRPC uses on the wire [7], a "
    "one-byte compression flag followed by a four-byte big-endian length. The "
    "test builds that framing around a `ctpb`-encoded payload, strips it again, "
    "and parses the result with `libprotobuf`. No gRPC library, channel, or "
    "server handler takes part, so what is shown is payload and framing "
    "fidelity; integration with a gRPC runtime, and generation of service stubs, "
    "remain untested.", style="Body Text")

# ============================================================ VIII. RELATED WORK
add_p("Related Work", style="heading 1")
add_p(
    "Prior C++ work has moved from template techniques to `constexpr` evaluation "
    "and reflection; P2996 [5] supplies the type-synthesis facilities used here, "
    "and P1967 [4] the standardized resource inclusion. CTRE [8] illustrates compile-time "
    "processing of a domain-specific grammar; `ctpb` applies the same broad idea "
    "to proto3 parsing and schema-driven types. In serialization, reflect-cpp [9] "
    "derives schemas from existing C++ structs, whereas FlatBuffers [2] and Cap'n "
    "Proto [3] retain standalone compilers. `ctpb` instead treats proto3 as the "
    "external schema authority while avoiding generated C++ source.",
    style="Body Text")

# ========================================================== IX. LIMITATIONS
add_p("Limitations and Future Work", style="heading 1")
add_p(
    "The present work has five limitations. First, the evaluated implementation "
    "requires an experimental Clang P2996 branch, so deployment depends on the "
    "compiler revision recorded with the benchmark artifact. Second, the initial "
    "release covers wire serialization only: gRPC service stubs are out of "
    "scope, and integration with a gRPC runtime is untested, the evaluation "
    "above covering the framing alone. Third, constant evaluation is bounded "
    "first by the compiler's step limit rather than by memory: a single message of "
    "317 `int32` fields is synthesized under Clang's default budget, while 318 "
    "fail. The bound is a flag rather than a wall, since raising the budget compiles "
    "3,200 fields without reaching a ceiling, but `ctpb` does not raise the default. "
    "Fourth, the decoding margin is specific to the `libprotobuf` 3.21.12 baseline; "
    "an unpaired run of 36.0.0 decoded the same message faster than the figure "
    "measured here for `ctpb`, so decoding is not claimed as a lead over Protocol "
    "Buffers in general. Finally, the performance data comes from one host and "
    "a limited set of synthetic schemas and one 47-byte `Person` message. Future "
    "work targets the Well-Known Types (`Timestamp`, `Duration`), compile-time JSON "
    "mapping, broader workload coverage, and asynchronous stub synthesis.",
    style="Body Text")

# ============================================================ X. CONCLUSION
add_p("Conclusion", style="heading 1")
add_p(
    "We presented `ctpb`, a header-only C++26 library that performs Protocol "
    "Buffers serialization without generated C++ source. By combining "
    "`constexpr` recursive-descent parsing with P2996 static reflection, "
    "`ctpb` compiles `.proto` schemas directly into type-safe C++ structures in "
    "the compiler front-end. On one host, a 100-field synthetic schema is "
    "parsed in 7.487 microseconds, and the tested 47-byte `Person` workload "
    "encodes 4.80x faster than `libprotobuf` 3.21.12 while including UTF-8 "
    "validation, and the exercised cases pass bidirectional wire-format "
    "interoperability tests. The static-runtime generated comparison is 93.62x "
    "larger as a file than the `ctpb` sample, while the dynamic comparison is 1.46x "
    "larger; measured as .text, however, `ctpb` carries more code than the dynamic "
    "stub and far less than the bundled runtime. The removed generator's cost "
    "reappears in the front end, where at 100 fields it exceeds it. "
    "These results show that static reflection can remove generated Protocol "
    "Buffers source for the evaluated schemas and toolchain while retaining tested "
    "wire-format interoperability.",
    style="Body Text")

# ========================================================== ACKNOWLEDGMENT
add_heading_unnumbered("Acknowledgment")
add_p("The author would like to thank the scientific research sector of the "
      "Technical University of Sofia for financial support for publishing the "
      "present paper.", style="Body Text")

# ============================================================== REFERENCES
add_heading_unnumbered("References")
REFS = [
    'Google LLC, "Protocol buffers: Language guide (proto3) and encoding '
    'specification." https://protobuf.dev/programming-guides/proto3/, 2025. '
    'Accessed: 2026-08-20.',
    'W. van Oortmerssen, "FlatBuffers: Memory efficient serialization library." '
    'https://google.github.io/flatbuffers/, 2014. Google Open Source.',
    'K. Varda, "Cap\'n proto: Insanely fast data interchange format and RPC '
    'system." https://capnproto.org/, 2013. Sandstorm.io.',
    'J. Meneide, "#embed - a scannable, tooling-friendly binary resource inclusion mechanism for C and C++ (ISO/IEC '
    'JTC1/SC22/WG21 P1967R14)." '
    'https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p1967r14.html, '
    '2025. ISO WG21 Standard Proposal.',
    'W. Childers, P. Dimov, D. Katz, B. Revzin, A. Sutton, F. Vali, and D. Vandevoorde, '
    '"Reflection for C++26 (ISO/IEC JTC1/SC22/WG21 P2996R13)." '
    'https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html, '
    '2025. ISO WG21 Standard Proposal.',
    'A. I. Tsvetanov, "ctpb: Compile-time protobuf library via C++26 static '
    'reflection." https://github.com/Alex-Tsvetanov/Compile-time-Protobuf, 2026. '
    'Open Source Publication Repository.',
    'gRPC Authors, "grpc: A high performance, open source universal RPC framework '
    'and wire protocol specification." '
    'https://grpc.io/docs/what-is-grpc/core-concepts/, 2025. Cloud Native '
    'Computing Foundation.',
    'H. Dusíková, "CTRE: Compile time regular expression for C++." '
    'https://github.com/hanickadot/compile-time-regular-expressions, 2020. '
    'Accessed: 2026-08-27.',
    'G. Vormayr, "reflect-cpp: Fast C++20 reflection and serialization library." '
    'https://github.com/getml/reflect-cpp, 2024. Accessed: 2026-08-20.',
]
for r in REFS:
    add_p(r, style="references")

doc.save(OUTPUT)
print("wrote", OUTPUT)
