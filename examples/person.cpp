// examples/person.cpp
// Minimal user code demonstration for ctpb.
//
// Demonstrates compile-time schema parsing, message reflection, and direct dot-member wire serialization.

#include <cassert>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include <ctpb/ctpb.hpp>

// 1. Embedded Proto Source (or included via P1967 #embed "addressbook.proto")
inline constexpr ctpb::fixed_string addressbook_proto = R"(
    syntax = "proto3";
    package tutorial;

    message Person {
      string name = 1;
      int32 id = 2;
      string email = 3;
    }
)";

// 2. Schema Binding & Message Type Definition
#if CTPB_HAS_P2996
// Zero-Macro Pure C++26 Static Reflection Message Definition:
consteval { ctpb::define_message<addressbook_proto, "tutorial.Person">(); }
using Person = ctpb::message_t<addressbook_proto, "tutorial.Person">;
#else
// C++20 Fallback for standard compilers without P2996:
using PersonSchema = ctpb::parse<addressbook_proto, "tutorial.Person">;
struct Person : ctpb::reflect_base<PersonSchema, Person> {
    std::string  name;
    std::int32_t id = 0;
    std::string  email;

    auto reflect_members() { return std::tie(name, id, email); }
};
#endif

int main() {
    // Direct dot-syntax access
    Person p;
    p.name = "Alex Tsvetanov";
    p.id = 42;
    p.email = "atsvetanov@tu-sofia.bg";

    // Serialize using compile-time reflection
    std::vector<std::byte> bytes = ctpb::encode(p);
    std::printf("Encoded Person to %zu wire bytes\n", bytes.size());

    // Deserialize using compile-time reflection
    auto back = ctpb::decode<Person>(bytes);
    if (back) {
        std::printf("Successfully decoded Person via compile-time reflection:\n");
        std::printf("  name  : %s\n", back->name.c_str());
        std::printf("  id    : %d\n", back->id);
        std::printf("  email : %s\n", back->email.c_str());
    }

    return 0;
}
