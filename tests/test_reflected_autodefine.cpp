// tests/test_reflected_autodefine.cpp
//
// define_message defines the structs of the message types its fields use, so
// sub-messages no longer have to be defined first. This covers plain, oneof,
// map and optional members, and a message that contains itself through a
// repeated field. A later define_message for a type that is already defined
// does nothing. Needs P2996 reflection.

#include <cstdio>
#include <type_traits>

#include "test_support.hpp"

#if CTPB_HAS_P2996

using test_support::bytes;
using test_support::encode_checked;

inline constexpr ctpb::fixed_string auto_proto = R"(
    syntax = "proto3";
    message Leaf { int32 n = 1; }
    message Child { Leaf leaf = 1; repeated Child kids = 2; }
    message Box {
      oneof v { Leaf a = 1; Child b = 2; }
      map<int32, Leaf> m = 3;
      optional Leaf o = 4;
    }
    message Parent { Child child = 1; Box box = 2; repeated Parent self = 3; }
)";

// Only the outer message is defined by hand.
consteval { ctpb::define_message<auto_proto, "Parent">(); }
// Leaf is already defined as a dependency of Parent: this is a no-op.
consteval { ctpb::define_message<auto_proto, "Leaf">(); }

using Parent = ctpb::message_t<auto_proto, "Parent">;
using Child = ctpb::message_t<auto_proto, "Child">;
using Box = ctpb::message_t<auto_proto, "Box">;
using Leaf = ctpb::message_t<auto_proto, "Leaf">;

static_assert(std::is_same_v<decltype(Parent::child), Child>);
static_assert(std::is_same_v<decltype(Parent::self), std::vector<Parent>>);
static_assert(std::is_same_v<decltype(Child::kids), std::vector<Child>>);
static_assert(std::is_same_v<decltype(Box::o), std::optional<Leaf>>);

int main() {
    Parent p;
    p.child.leaf.n = 5;
    p.box.m[1].n = 2;
    const auto wire = encode_checked(p);
    REQUIRE(wire == bytes({0x0A, 0x04, 0x0A, 0x02, 0x08, 0x05,                        // child.leaf.n = 5
                           0x12, 0x08, 0x1A, 0x06, 0x08, 0x01, 0x12, 0x02, 0x08, 0x02}));  // box.m {1: {n: 2}}

    p.self.push_back(Parent{});
    p.self[0].child.kids.push_back(Child{});
    p.self[0].child.kids[0].leaf.n = 7;
    p.box.v.emplace<2>();
    p.box.o = Leaf{};
    auto back = ctpb::decode<Parent>(encode_checked(p));
    REQUIRE(back.has_value());
    REQUIRE(back->child.leaf.n == 5);
    REQUIRE(back->box.m.size() == 1 && back->box.m.at(1).n == 2);
    REQUIRE(back->box.v.index() == 2);
    REQUIRE(back->box.o.has_value() && back->box.o->n == 0);
    REQUIRE(back->self.size() == 1 && back->self[0].child.kids.size() == 1);
    REQUIRE(back->self[0].child.kids[0].leaf.n == 7);

    std::printf("test_reflected_autodefine: OK\n");
    return 0;
}

#else

int main() {
    std::printf("test_reflected_autodefine: SKIPPED (no P2996 reflection)\n");
    return 0;
}

#endif
