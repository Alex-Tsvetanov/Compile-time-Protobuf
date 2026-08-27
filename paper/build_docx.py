"""
Fill the TELECOM 2026 IEEE Word template with the content of paper/main.pdf.

The template supplies every style (margins, columns, fonts) and auto-numbers
headings, references and table captions, so this script must NOT type numbers
like "I." or "TABLE I" or "[1]" - it only supplies text and the style name.

Text is written with `backticks` marking inline code spans, which are re-run
in Courier New. Numbers are transcribed verbatim from the LaTeX sources; none
are recomputed here.
"""
import copy
import docx
from docx.shared import Pt, Inches, RGBColor
from docx.oxml.ns import qn
from docx.oxml import OxmlElement
from docx.enum.text import WD_ALIGN_PARAGRAPH

TEMPLATE = "template.docx"
OUTPUT = "Tsvetanov-ctpb-TELECOM2026.docx"

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
    t.autofit = True
    if widths:
        t.autofit = False
        for j, wdt in enumerate(widths):
            for row in t.rows:
                row.cells[j].width = wdt
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
add_p("Zero-Codegen Protocol Buffers via C++26 Static Reflection "
      "and Compile-Time Metaprogramming", style="paper title")

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
    "Serialization frameworks like Protocol Buffers rely on external code "
    "generators (protoc) to translate schema definitions into intermediate C++ "
    "files, introducing build-system complexity, toolchain version skew, and "
    "compilation bloat. We present ctpb, a header-only C++26 library that "
    "eliminates external code generation entirely by performing compile-time IDL "
    "parsing and type synthesis using C++26 static reflection (P2996) and "
    "resource inclusion (P1967 #embed). A three-stage compile-time pipeline "
    "tokenizes .proto schemas into an Abstract Syntax Tree during constant "
    "evaluation and materializes them as first-class C++ structs with optimized "
    "wire visitors. Benchmarks show schema parsing under 11.4 microseconds and "
    "wire throughput of 1,396.0 MB/s encoding and 350.2 MB/s decoding, speedups "
    "of 2.81 times and 1.71 times over Google libprotobuf that persist against "
    "the leaner libprotobuf-lite baseline, with bidirectional wire-format "
    "fidelity verified against the reference implementation."
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
p.add_run("Protocol Buffers, C++26, static reflection, #embed, "
          "compile-time metaprogramming, zero-codegen serialization")

# ============================================================= I. INTRODUCTION
add_p("Introduction", style="heading 1")
add_p(
    "Interface Definition Languages such as Google Protocol Buffers [1], "
    "FlatBuffers [2], and Cap'n Proto [3] underpin distributed systems, "
    "microservice architectures, and high-performance computing, offering "
    "cross-language interoperability, compact wire encodings, and compatibility "
    "guarantees across schema versions.", style="Body Text")
add_p(
    "Protocol Buffers, however, depend on an external code generator. Building a "
    "C++ project that uses Protobuf requires the build system to invoke `protoc` "
    "and produce intermediate `.pb.h` and `.pb.cc` files. This imposes four "
    "costs: intricate build rules and dependency graphs to track generated "
    "artifacts; version skew between the runtime `libprotobuf` and the build-time "
    "`protoc`, a frequent source of ABI mismatches; codebase bloat, since large "
    "systems generate hundreds of thousands of lines that slow incremental "
    "builds; and the loss of direct language integration, as schemas remain "
    "external build targets rather than first-class compile-time entities.",
    style="Body Text")
add_p(
    "The upcoming ISO C++26 standard removes the need for this arrangement. "
    "P1967 `#embed` [4] provides zero-overhead compile-time file inclusion, and "
    "P2996 Static Reflection [5] provides programmatic type synthesis through "
    "splicing.", style="Body Text")
add_p(
    "We present `ctpb` (Compile-Time Protobuf) [6], a header-only C++26 library "
    "that eliminates `protoc` entirely: it embeds `.proto` schemas into the "
    "translation unit, runs a fully `constexpr` recursive-descent parser inside "
    "the compiler front-end, and synthesizes type-safe structs, accessors, and "
    "wire-format routines through static reflection. Our contributions are a "
    "three-stage compile-time architecture that turns raw IDL text into "
    "first-class C++ types with no intermediate disk I/O; a zero-allocation "
    "`constexpr` proto3 parser handling nested messages, enums, maps, oneofs, and "
    "reserved ranges; the application of P2996 to inject member fields and wire "
    "visitors programmatically; and an evaluation establishing sub-11.4 "
    "microsecond schema ingestion and encoding throughput above 1,300 MB/s, "
    "verified for wire-format fidelity against the reference implementation.",
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
    "Two ISO C++26 proposals make compile-time IDL ingestion possible. P1967 [4] "
    "adds the `#embed` directive, exposing an external file as a contiguous "
    "compile-time byte span without the external build scripts (`xxd -i` and "
    "similar) previously required. P2996 [5] adds static reflection around the "
    "opaque `std::meta::info` type, supporting introspection, programmatic "
    "generation of member descriptors (`define_aggregate()`, "
    "`data_member_spec()`), and splicing of reflected entities into concrete "
    "declarations. Combined with `constexpr` evaluation, the two turn the "
    "compiler front-end into a programmable meta-compiler capable of digesting "
    "an external domain-specific language and emitting native types.",
    style="Body Text")

# ============================================================ III. ARCHITECTURE
add_p("Architecture and Compile-Time Pipeline", style="heading 1")
add_p(
    "`ctpb` is organized as a three-stage pipeline executing entirely within the "
    "compiler front-end prior to code emission.", style="Body Text")
add_p(
    "Stage 1, source embedding. The user supplies a `.proto` path as a template "
    "argument. Under C++26 this is loaded via `#embed` into a contiguous "
    "immutable byte buffer, mapped to a zero-copy `std::string_view`.",
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
    "struct whose members match each field, together with `encode()` and "
    "`decode()` visitors, directly into the active translation unit.",
    style="Body Text")
add_p("The resulting public interface requires no macros and no build step:",
      style="Body Text")
add_code([
    '#include <ctpb/ctpb.hpp>',
    '',
    '// Compile-time schema synthesis',
    'consteval { ctpb::define_message<"person.proto",',
    '                                 "tutorial.Person">(); }',
    'using Person = ctpb::message_t<"person.proto",',
    '                               "tutorial.Person">;',
    '',
    'Person p{};',
    'p.name = "Alex I. Tsvetanov";',
    'p.id   = 42;',
    '',
    'std::vector<std::byte> wire = ctpb::encode(p);',
    'auto decoded = ctpb::decode<Person>(wire);',
])
doc.add_paragraph()
add_p("Every field offset, wire tag, and type check is resolved at compile time "
      "with zero runtime dispatch.", style="Body Text")

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
    '  constexpr auto count = Schema::field_count();',
    '  std::vector<std::meta::info> members;',
    '',
    '  auto append_member = [&]<std::size_t I>() {',
    '    constexpr auto info = Schema::get_field(I);',
    '    std::meta::info type_info =',
    '        resolve_type_info(info.type);',
    '    if (info.is_repeated) {',
    '      type_info = std::meta::substitute(',
    '          ^^std::vector, {type_info});',
    '    }',
    '    members.push_back(std::meta::data_member_spec(',
    '        type_info, {.name = info.name}));',
    '  };',
    '',
    '  [&]<std::size_t... Is>(std::index_sequence<Is...>) {',
    '    (append_member.template operator()<Is>(), ...);',
    '  }(std::make_index_sequence<count>{});',
    '',
    '  return std::meta::define_aggregate(',
    '      incomplete_type, members);',
    '}',
])
doc.add_paragraph()
add_p("Type Mapping and Generated Interface", style="heading 2")
add_p(
    "The proto3-to-C++ mapping is strict and deterministic: the sized integer "
    "types map to the corresponding `std::intN_t` and `std::uintN_t` (with `sint` "
    "and `sfixed` variants sharing the signed representation and differing only "
    "in wire encoding), `float`, `double` and `bool` map to their C++ "
    "equivalents, `string` to `std::string`, `bytes` to "
    "`std::vector<std::byte>`, `repeated T` to `std::vector<T>`, `map<K,V>` to "
    "`std::unordered_map<K,V>`, and `oneof` to `std::variant`.",
    style="Body Text")
add_p(
    "Beyond member generation, `ctpb` supplies direct dot-syntax field access "
    "(`p.name`), non-intrusive `ctpb::encode` and `ctpb::decode` visitors that "
    "traverse the member descriptors reflectively, and defaulted member-wise "
    "comparison operators.", style="Body Text")

# ======================================================== VI. WIRE FORMAT ENGINE
add_p("Wire Format Engine", style="heading 1")
add_p("Encoding", style="heading 2")
add_p(
    "The serialization engine in `ctpb::wire` manipulates the byte stream "
    "directly, without intermediate allocation. Encoding proceeds in two passes: "
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
    "remaining bytes before yielding a subspan. Failures are reported through a "
    "typed `decode_error` rather than an exception, so decoding remains usable in "
    "`noexcept` contexts.", style="Body Text")
add_p(
    "Unknown field numbers are not an error. The `skip_field` method uses the "
    "wire type alone to advance past an unrecognized integer, fixed-width block, "
    "or length-delimited payload, preserving the proto3 forward-compatibility "
    "guarantee when a peer sends a newer schema version.", style="Body Text")

# ======================================================== VII. EVALUATION
add_p("Experimental Evaluation", style="heading 1")
add_p(
    "All benchmarks ran inside the project's own `sleepingeight/clang-cpp-refl` "
    "Docker image on an AMD Ryzen 5 3600 (6 cores, x86_64, Ubuntu 24.04), "
    "compiled with Clang 21.0 (P2996 branch) under "
    "`-std=c++26 -freflection -O3` and linked against Protobuf 3.21.12. The full "
    "pipeline is reproducible from the repository Dockerfile [6], and the raw "
    "per-trial output behind every figure below is published with it.",
    style="Body Text")

add_p("Compile-Time Schema Parsing", style="heading 2")
add_p(
    "We measured the `constexpr` parser on three schemas scaled by field count "
    "alone (5, 25, and 100 `int32` fields), each over 20,000 iterations after a "
    "2,000-iteration warmup, across seven trials (Table I, median plus or minus "
    "sample standard deviation).", style="Body Text")
add_table(
    "Compile-Time Parser Throughput and Latency",
    ["Schema Size", "Fields", "Latency / Parse", "Throughput"],
    [
        ["Small", "5", "0.86 ± 0.11 µs", "1,164,144 parses/s"],
        ["Medium", "25", "3.04 ± 0.23 µs", "329,164 parses/s"],
        ["Large", "100", "11.36 ± 0.73 µs", "88,013 parses/s"],
    ])
add_p(
    "Invoking `protoc` in the same environment on a 3-field schema takes 7 ms end "
    "to end (median of seven trials) once process creation, disk I/O, and code "
    "generation are counted. `ctpb` ingests equivalent schemas in 0.86 to 11.36 "
    "microseconds, a reduction of roughly 620 to 8,150 times, and does so without "
    "emitting files that a build system must subsequently track.",
    style="Body Text")

add_p("Runtime Serialization Throughput", style="heading 2")
add_p(
    "We evaluated encoding and decoding on a representative 3-field `Person` "
    "message (47-byte wire payload) against the official Google `libprotobuf`, "
    "over seven trials of one million iterations each with a 20,000-iteration "
    "warmup. Latency is reported as median plus or minus sample standard "
    "deviation; throughput and speedup derive from the median.", style="Body Text")
add_table(
    "Runtime Performance: ctpb vs. libprotobuf",
    ["Framework / Operation", "Latency (ns)", "Throughput", "Data Rate", "Speedup"],
    [
        ["ctpb Encode", "32.1 ± 1.0", "31.15 MOps/s", "1,396.0 MB/s", "2.81x"],
        ["Google Protobuf Encode", "90.3 ± 3.4", "11.08 MOps/s", "496.4 MB/s", "1.00x"],
        ["ctpb Decode", "128.0 ± 5.9", "7.81 MOps/s", "350.2 MB/s", "1.71x"],
        ["Google Protobuf Decode", "218.6 ± 8.4", "4.58 MOps/s", "205.0 MB/s", "1.00x"],
    ],
    bold_first_col_rows=(0, 2),
    widths=[Inches(1.02), Inches(0.62), Inches(0.68), Inches(0.66), Inches(0.40)])
add_p(
    "Encoding gains 2.81 times from compile-time field-size evaluation, exact "
    "single-buffer pre-allocation via `byte_size()`, and raw-pointer writes free "
    "of reallocation and virtual dispatch. Decoding gains 1.71 times from inlined "
    "unrolled field loops and direct member assignment, avoiding the arena and "
    "descriptor machinery of the reference implementation.", style="Body Text")
add_p(
    "Because full `libprotobuf` carries descriptor pools, reflection registries, "
    "and arenas that `ctpb` does not implement, part of that margin measures "
    "absent features rather than the compile-time approach itself. We therefore "
    "repeated the comparison against `libprotobuf-lite` "
    "(`optimize_for = LITE_RUNTIME`), which strips them, on the same machine with "
    "the same script and iteration count. The margin does not shrink: 3.06 times "
    "encoding (32.7 ± 2.1 ns versus 99.9 ± 12.4 ns) and 1.69 times "
    "decoding (132.6 ± 5.5 ns versus 223.7 ± 10.0 ns). The speedup is "
    "therefore not an artifact of comparing against a heavier reference "
    "implementation.", style="Body Text")

add_p("Binary Footprint", style="heading 2")
add_table(
    "Binary Footprint and Code Generation",
    ["Metric", "ctpb", "protoc (Dyn)", "protoc (Static)"],
    [
        ["Generated Source Lines", "0", "754", "754"],
        ["Object Files (.o)", "21.9 KB", "31.3 KB", "31.3 KB"],
        ["Stripped Executable", "18.3 KB", "26.9 KB", "1,718.1 KB"],
        ["Text Section (.text)", "9.8 KB", "16.0 KB", "1,691.4 KB"],
        ["External Shared Libraries", "0", "libprotobuf.so", "0"],
    ])
add_p(
    "For a single 3-field message `protoc` emits 754 lines across `.pb.h` and "
    "`.pb.cc`, whereas `ctpb` emits no files at all (Table III). In fully static "
    "deployments the `ctpb` stripped executable is 18.3 KB against 1.72 MB, a "
    "93.7 times reduction, because `libprotobuf` statically bundles descriptor "
    "pools, reflection registries, mutex tables, and arena allocators while "
    "`ctpb` compiles to exact inlined field loops. The dynamic `ctpb` build links "
    "only `libc` and `libstdc++`, removing `libprotobuf.so` version skew from "
    "deployment.", style="Body Text")

add_p("Reference Interoperability and gRPC Parity", style="heading 2")
add_p(
    "Correctness was established by dual-direction interoperability testing "
    "against `libprotobuf` 3.21.12 and the `protoc` code generator [1]. Messages "
    "serialized by the Google `SerializeToString()` decode through "
    "`ctpb::decode<T>()` with full fidelity across scalars, Zigzag integers, "
    "strings, enums, nested messages, `bytes` fields, and packed repeated "
    "scalars; in the reverse direction, buffers produced by `ctpb::encode()` "
    "parse cleanly through the Google `ParseFromArray()`, with all wire tags, "
    "varints, and length delimiters conforming to reference parser expectations. "
    "Packed repeated fields and `bytes` are covered explicitly because both are "
    "encoded differently from the singular scalar path and are the cases where an "
    "independent implementation most easily diverges.", style="Body Text")
add_p(
    "Transport parity was verified against gRPC [7], whose HTTP/2 framing "
    "prefixes each message with a one-byte compression flag and a four-byte "
    "big-endian length. Specializing `grpc::SerializationTraits<T>` over "
    "`ctpb::encode` and `ctpb::decode` is sufficient to carry `ctpb` types across "
    "standard gRPC channels, and automated tests confirm that framed messages are "
    "processed correctly by unmodified gRPC server handlers.", style="Body Text")

# ============================================================ VIII. RELATED WORK
add_p("Related Work", style="heading 1")
add_p(
    "Compile-time metaprogramming in ISO C++ has progressed from template tricks "
    "and macro preprocessors to standardized `constexpr` facilities [8], "
    "culminating in P2996 [5] and P1967 [4]. CTRE [9] established the precedent "
    "that an entire domain-specific grammar, in that case regular expressions, "
    "can be tokenized and compiled to typed C++ artifacts inside `constexpr` "
    "evaluation rather than through an external tool; `ctpb` applies the same "
    "principle to the substantially larger proto3 grammar and adds type synthesis "
    "rather than pattern matching. Boost.PFR [10] predates P2996 and offers "
    "structural reflection over existing aggregates, sufficient for serializing a "
    "struct that already exists but unable to synthesize members from an external "
    "schema.", style="Body Text")
add_p(
    "Among serialization libraries, reflect-cpp [11] provides C++20 "
    "reflection-based serialization but derives schemas from existing C++ structs "
    "rather than compiling external IDL, and so lacks proto3 wire tags, packed "
    "encoding, and reserved-range validation. FlatBuffers [2] and Cap'n Proto [3] "
    "target zero-copy in-memory layout but ship standalone compiler binaries "
    "analogous to `protoc`. The lightweight protobuf runtimes reduce overhead "
    "without removing the generation step: `libprotobuf-lite` omits runtime "
    "reflection descriptors, nanopb [12] targets embedded systems through its own "
    "`protoc` plugin, protozero [13] relies on hand-written accessors, and upb "
    "[14], the Google small-footprint C core, still consumes `protoc`-generated "
    "descriptors. `ctpb` differs from all of these in eliminating the generation "
    "step outright while retaining proto3 as the schema authority.",
    style="Body Text")

# ========================================================== IX. LIMITATIONS
add_p("Limitations and Future Work", style="heading 1")
add_p(
    "Three limitations bound the present work. P2996 static reflection is "
    "available only in experimental Clang and GCC branches pending final ISO "
    "C++26 ratification, so deployment currently depends on a non-release "
    "toolchain. The initial release covers wire serialization only; gRPC service "
    "stubs are deliberately out of scope, although the transport traits above "
    "show that `ctpb` types integrate with existing gRPC channels. Finally, "
    "monolithic schemas containing hundreds of messages demand correspondingly "
    "generous compiler constant-evaluation memory limits, since the entire AST is "
    "materialized during translation. Future work targets the Well-Known Types "
    "(`Timestamp`, `Duration`), compile-time JSON mapping, and asynchronous stub "
    "synthesis.", style="Body Text")

# ============================================================ X. CONCLUSION
add_p("Conclusion", style="heading 1")
add_p(
    "We presented `ctpb`, a header-only C++26 library that performs Protocol "
    "Buffers serialization without an external code generator. By uniting P1967 "
    "`#embed`, `constexpr` recursive-descent parsing, and P2996 static "
    "reflection, `ctpb` compiles `.proto` schemas directly into type-safe C++ "
    "structures inside the compiler front-end. Measured against the Google "
    "reference implementation it delivers a 93.7 times reduction in standalone "
    "binary footprint, sub-11.4 microsecond schema ingestion, and wire throughput "
    "of 1,396 MB/s encoding (2.81 times) and 350 MB/s decoding (1.71 times), "
    "margins that persist against the leaner `libprotobuf-lite` baseline, with "
    "bidirectional wire-format fidelity verified against `libprotobuf` itself. "
    "Static reflection is a viable and, on this evidence, preferable foundation "
    "for interface definition compilation in modern systems programming.",
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
    'J. Meneide, "#embed - a resource inclusion mechanism for C and C++ (ISO/IEC '
    'JTC1/SC22/WG21 P1967R14)." '
    'https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p1967r14.html, '
    '2024. ISO WG21 Standard Proposal.',
    'B. Revzin, W. Childers, P. Dimov, A. Sutton, F. Vali, and D. Vandevoorde, '
    '"Reflection for C++26 (ISO/IEC JTC1/SC22/WG21 P2996R8)." '
    'https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2996r8.html, '
    '2024. ISO WG21 Standard Proposal.',
    'A. I. Tsvetanov, "ctpb: Compile-time protobuf library via C++26 static '
    'reflection." https://github.com/Alex-Tsvetanov/Compile-time-Protobuf, 2026. '
    'Open Source Publication Repository.',
    'gRPC Authors, "grpc: A high performance, open source universal RPC framework '
    'and wire protocol specification." '
    'https://grpc.io/docs/what-is-grpc/core-concepts/, 2025. Cloud Native '
    'Computing Foundation.',
    'D. Vandevoorde, W. Childers, A. Sutton, and F. Vali, "Scalable reflection in '
    'C++ (ISO/IEC JTC1/SC22/WG21 P1240R2)." '
    'https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1240r2.pdf, 2020. '
    'ISO WG21 Proposal.',
    'H. Dusíková, "CTRE: Compile time regular expression for C++." '
    'https://github.com/hanickadot/compile-time-regular-expressions, 2020. '
    'Accessed: 2026-08-27.',
    'A. Polukhin, "Boost.PFR: Basic reflection library." '
    'https://www.boost.org/doc/libs/release/doc/html/boost_pfr.html, 2019. '
    'Accessed: 2026-08-27.',
    'G. Vormayr, "reflect-cpp: Fast C++20 reflection and serialization library." '
    'https://github.com/getml/reflect-cpp, 2024. Accessed: 2026-08-20.',
    'P. Aimonen, "nanopb: Protocol buffers with small code size." '
    'https://github.com/nanopb/nanopb, 2011. Accessed: 2026-08-27.',
    'D. Springmeyer and K. Käfer, "protozero: Minimalist protocol buffer '
    'decoder and encoder in C++." https://github.com/mapbox/protozero, 2016. '
    'Accessed: 2026-08-27.',
    'Google LLC, "upb: A small protobuf implementation in C." '
    'https://github.com/protocolbuffers/protobuf/tree/main/upb, 2018. Accessed: '
    '2026-08-27.',
]
for r in REFS:
    add_p(r, style="references")

doc.save(OUTPUT)
print("wrote", OUTPUT)
