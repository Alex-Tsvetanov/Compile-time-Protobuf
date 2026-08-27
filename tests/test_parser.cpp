// tests/test_parser.cpp

#include <cassert>
#include <cstdio>
#include <string_view>

#include <ctpb/ctpb.hpp>

int main() {
    using namespace ctpb;

    constexpr std::string_view proto_src = R"(
        syntax = "proto3";
        package tutorial;

        import public "other.proto";
        option java_package = "com.example.tutorial";

        enum PhoneType {
            MOBILE = 0;
            HOME = 1;
            WORK = 2;
        }

        message Person {
            string name = 1;
            int32 id = 2;
            string email = 3;

            message PhoneNumber {
                string number = 1;
                PhoneType type = 2;
            }

            repeated PhoneNumber phones = 4;
            map<string, string> tags = 5;

            oneof avatar {
                string image_url = 6;
                bytes image_data = 7;
            }

            reserved 8, 9 to 20;
            reserved "foo", "bar";
        }
    )";

    // Compile-time assertion
    constexpr bool valid_proto = []() consteval {
        constexpr std::string_view proto = R"(
            syntax = "proto3";
            package tutorial;
            message Person {
                string name = 1;
                int32 id = 2;
            }
        )";
        parser p{proto};
        ast::file f = p.parse_file();
        return f.messages.size() == 1 && f.messages[0].name == "Person";
    }();
    static_assert(valid_proto, "proto must parse successfully at compile time");

    parser p{proto_src};
    ast::file file = p.parse_file();

    assert(file.syntax.has_value() && *file.syntax == "proto3");
    assert(file.package.has_value() && *file.package == "tutorial");
    assert(file.imports.size() == 1);
    assert(file.imports[0].is_public);
    assert(file.imports[0].path == "other.proto");
    assert(file.options.size() == 1);
    assert(file.top_level_enums.size() == 1);
    assert(file.top_level_enums[0].values.size() == 3);

    assert(file.messages.size() == 1);
    const auto& msg = file.messages[0];
    assert(msg.name == "Person");
    assert(msg.fields.size() == 4);
    assert(msg.nested_messages.size() == 1);
    assert(msg.nested_messages[0].name == "PhoneNumber");
    assert(msg.maps.size() == 1);
    assert(msg.maps[0].name == "tags");
    assert(msg.oneofs.size() == 1);
    assert(msg.oneofs[0].name == "avatar");
    assert(msg.oneofs[0].branches.size() == 2);
    assert(msg.reserved_field_numbers.size() == 2);
    assert(msg.reserved_field_names.size() == 2);
    assert(msg.is_reserved_number(8));
    assert(msg.is_reserved_number(15));
    assert(!msg.is_reserved_number(21));
    assert(msg.is_reserved_name("foo"));
    assert(!msg.is_reserved_name("baz"));

    std::printf("test_parser: OK\n");
    return 0;
}
