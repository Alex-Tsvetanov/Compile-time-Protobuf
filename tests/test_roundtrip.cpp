// tests/test_roundtrip.cpp

#include <cassert>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string sample_proto = R"(
    syntax = "proto3";
    package tutorial;

    message Person {
      string name = 1;
      int32 id = 2;
      string email = 3;
      repeated int64 phone_numbers = 4;
    }
)";

#if CTPB_HAS_P2996
// Zero-Macro Pure C++26 Static Reflection Message Definition:
consteval { ctpb::define_message<sample_proto, "Person">(); }
using PersonMessage = ctpb::message_t<sample_proto, "Person">;
#else
// C++20 Fallback for standard compilers without P2996:
using PersonSchema = ctpb::parse<sample_proto, "Person">;
struct PersonMessage : ctpb::reflect_base<PersonSchema, PersonMessage> {
    std::string   name;
    std::int32_t  id = 0;
    std::string   email;
    std::vector<std::int64_t> phone_numbers;

    auto reflect_members() { return std::tie(name, id, email, phone_numbers); }
};
#endif

int main() {
    // Direct dot-syntax access
    PersonMessage orig;
    orig.name = "Alex Tsvetanov";
    orig.id = 1337;
    orig.email = "atsvetanov@tu-sofia.bg";
    orig.phone_numbers.push_back(123456789);
    orig.phone_numbers.push_back(987654321);
    orig.phone_numbers.push_back(555444333);

    std::vector<std::byte> wire_bytes = ctpb::encode(orig);
    assert(!wire_bytes.empty());

    auto decoded = ctpb::decode<PersonMessage>(wire_bytes);
    assert(decoded.has_value());
    assert(decoded->name == "Alex Tsvetanov");
    assert(decoded->id == 1337);
    assert(decoded->email == "atsvetanov@tu-sofia.bg");
    
    assert(decoded->phone_numbers.size() == 3);
    assert(decoded->phone_numbers[0] == 123456789);
    assert(decoded->phone_numbers[1] == 987654321);
    assert(decoded->phone_numbers[2] == 555444333);

    std::printf("test_roundtrip: OK\n");
    return 0;
}
