// tests/test_keyword_fields.cpp
//
// A field may be named like a keyword. The parser must read it as a name and
// must always make progress, even on a token it does not understand.

#include <cassert>
#include <cstdio>
#include <string_view>

#include <ctpb/ctpb.hpp>

using namespace ctpb;

// Checked at compile time. If the parser looped, this would not compile.
static constexpr bool keyword_names_parse() {
    parser p{R"(
        syntax = "proto3";
        message M {
            string max = 1;
            int32 to = 2;
            bool public = 3;
            string map = 4;
            repeated string package = 5;
            int64 optional = 6;
        }
    )"};
    ast::file f = p.parse_file();
    if (f.messages.size() != 1) return false;
    const auto& fl = f.messages[0].fields;
    return fl.size() == 6 && fl[0].name == "max" && fl[0].number == 1 &&
           fl[1].name == "to" && fl[1].number == 2 &&
           fl[2].name == "public" && fl[2].number == 3 &&
           fl[3].name == "map" && fl[3].number == 4 &&
           fl[4].name == "package" && fl[4].number == 5 &&
           fl[5].name == "optional" && fl[5].number == 6;
}
static_assert(keyword_names_parse(), "keywords must be accepted as field names");

// A stray token in a message body must be skipped, not spin forever.
static constexpr bool stray_token_terminates() {
    parser p{R"(
        message M {
            string a = 1;
            42
            string b = 2;
        }
    )"};
    ast::file f = p.parse_file();
    return f.messages.size() == 1;
}
static_assert(stray_token_terminates(), "parser must make progress on a stray token");

int main() {
    assert(keyword_names_parse());
    assert(stray_token_terminates());
    std::printf("test_keyword_fields: OK\n");
    return 0;
}
