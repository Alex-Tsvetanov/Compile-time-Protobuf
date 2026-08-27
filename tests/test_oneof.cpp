// tests/test_oneof.cpp

#include <cassert>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#include <ctpb/ctpb.hpp>

struct OneofMessage {
    std::variant<std::monostate, std::string, std::int32_t, std::vector<std::byte>> body;

    void encode_into(std::vector<std::byte>& out) const {
        if (std::holds_alternative<std::string>(body)) {
            ctpb::wire::write_tag(out, 1, ctpb::wire::wire_type::len);
            ctpb::wire::write_string(out, std::get<std::string>(body));
        } else if (std::holds_alternative<std::int32_t>(body)) {
            ctpb::wire::write_tag(out, 2, ctpb::wire::wire_type::varint);
            ctpb::wire::write_varint(out, static_cast<std::uint64_t>(std::get<std::int32_t>(body)));
        } else if (std::holds_alternative<std::vector<std::byte>>(body)) {
            ctpb::wire::write_tag(out, 3, ctpb::wire::wire_type::len);
            ctpb::wire::write_length_delimited(out, std::get<std::vector<std::byte>>(body));
        }
    }

    void decode_from(std::span<const std::byte> bytes, ctpb::wire::decode_error& err) {
        ctpb::wire::reader r{bytes};
        while (!r.empty()) {
            std::int64_t num = 0;
            ctpb::wire::wire_type wt = ctpb::wire::wire_type::varint;
            if (!r.read_tag(num, wt)) { err.err = ctpb::wire::decode_error::bad_varint; return; }
            if (num == 1 && wt == ctpb::wire::wire_type::len) {
                std::string_view sv;
                if (!r.read_string(sv)) { err.err = ctpb::wire::decode_error::truncated; return; }
                body = std::string(sv);
            } else if (num == 2 && wt == ctpb::wire::wire_type::varint) {
                std::uint64_t v = 0;
                if (!r.read_varint(v)) { err.err = ctpb::wire::decode_error::bad_varint; return; }
                body = static_cast<std::int32_t>(v);
            } else if (num == 3 && wt == ctpb::wire::wire_type::len) {
                std::span<const std::byte> b;
                if (!r.read_length_delimited(b)) { err.err = ctpb::wire::decode_error::truncated; return; }
                body = std::vector<std::byte>(b.begin(), b.end());
            } else {
                r.skip_field(wt);
            }
        }
    }
};

int main() {
    using namespace ctpb;

    constexpr std::string_view oneof_proto = R"(
        syntax = "proto3";
        package oneof_test;

        message Request {
          oneof body {
            string text = 1;
            int32 code = 2;
            bytes blob = 3;
          }
        }
    )";

    constexpr bool valid_oneof = []() consteval {
        constexpr std::string_view proto = R"(
            syntax = "proto3";
            package oneof_test;

            message Request {
              oneof body {
                string text = 1;
                int32 code = 2;
                bytes blob = 3;
              }
            }
        )";
        parser p{proto};
        ast::file f = p.parse_file();
        if (f.messages.empty()) return false;
        if (f.messages[0].oneofs.empty()) return false;
        return f.messages[0].oneofs[0].branches.size() == 3;
    }();
    static_assert(valid_oneof, "oneof schema must be validated at compile time");

    parser p{oneof_proto};
    auto schema = p.parse_file();
    assert(schema.messages.size() == 1);
    assert(schema.messages[0].oneofs.size() == 1);
    const auto& ob = schema.messages[0].oneofs[0];
    assert(ob.name == "body");
    assert(ob.branches.size() == 3);

    // Test serialization of string branch
    {
        OneofMessage msg;
        msg.body = std::string("test string payload");
        auto bytes = ctpb::encode(msg);
        assert(!bytes.empty());

        auto dec = ctpb::decode<OneofMessage>(bytes);
        assert(dec.has_value());
        assert(std::holds_alternative<std::string>(dec->body));
        assert(std::get<std::string>(dec->body) == "test string payload");
    }

    // Test serialization of int32 branch
    {
        OneofMessage msg;
        msg.body = static_cast<std::int32_t>(1337);
        auto bytes = ctpb::encode(msg);
        assert(!bytes.empty());

        auto dec = ctpb::decode<OneofMessage>(bytes);
        assert(dec.has_value());
        assert(std::holds_alternative<std::int32_t>(dec->body));
        assert(std::get<std::int32_t>(dec->body) == 1337);
    }

    std::printf("test_oneof: OK\n");
    return 0;
}
