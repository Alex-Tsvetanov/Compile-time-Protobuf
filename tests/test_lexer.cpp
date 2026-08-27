// tests/test_lexer.cpp

#include <cassert>
#include <cstdio>
#include <string_view>

#include <ctpb/ctpb.hpp>

int main() {
    using namespace ctpb;

    constexpr std::string_view src = R"(
        syntax = "proto3";
        package test.pkg;
        message Person {
            string name = 1;
            int32 id = 2;
            repeated int64 phone = 3;
            // comment
            /* block comment */
            map<string, int32> attr = 4;
            oneof status {
                bool active = 5;
                int32 code = 6;
            }
            reserved 7, 8 to 15;
            reserved "foo", "bar";
        }
    )";

    lexer lex{src};
    int count = 0;
    while (lex.peek().kind != tok_kind::eof) {
        token t = lex.next();
        count++;
        assert(t.loc.line >= 1);
        assert(t.loc.column >= 1);
    }
    assert(count > 20);

    // Test numbers and hex
    {
        lexer num_lex{"0 42 -17 0x2A 3.1415"};
        token t1 = num_lex.next();
        assert(t1.kind == tok_kind::int_lit && t1.int_value == 0);
        token t2 = num_lex.next();
        assert(t2.kind == tok_kind::int_lit && t2.int_value == 42);
        token t3 = num_lex.next();
        assert(t3.kind == tok_kind::int_lit && t3.int_value == -17);
        token t4 = num_lex.next();
        assert(t4.kind == tok_kind::int_lit && t4.int_value == 42);
        token t5 = num_lex.next();
        assert(t5.kind == tok_kind::float_lit);
    }

    std::printf("test_lexer: OK\n");
    return 0;
}
