// tests/test_reflected_submessage.cpp
//
// Structs made by define_message (static reflection) must hold enum and
// message fields with the right types, and nested messages must be encoded
// and decoded. This needs a compiler with P2996 reflection. The library
// accepts __cpp_impl_reflection (what GCC 16 defines with -freflection) as
// well as __cpp_reflection, so with -freflection this test runs.

#include <cassert>
#include <cstdio>
#include <type_traits>

#include <ctpb/ctpb.hpp>

#if CTPB_HAS_P2996

inline constexpr ctpb::fixed_string sub_proto = R"(
    syntax = "proto3";
    package reflected;

    enum Color { RED = 0; GREEN = 1; BLUE = 2; }

    message Child { int32 n = 1; }

    message Parent {
      Color color = 1;
      Child child = 2;
      repeated Child kids = 3;
      string note = 4;
    }
)";

// Defining the sub-message first still works. define_message("Parent") alone
// would also define Child (see test_reflected_autodefine.cpp).
consteval { ctpb::define_message<sub_proto, "Child">(); }
consteval { ctpb::define_message<sub_proto, "Parent">(); }

using Child = ctpb::message_t<sub_proto, "Child">;
using Parent = ctpb::message_t<sub_proto, "Parent">;

static_assert(std::is_same_v<decltype(Parent::color), int>);
static_assert(std::is_same_v<decltype(Parent::child), Child>);
static_assert(std::is_same_v<decltype(Parent::kids), std::vector<Child>>);

int main() {
    Parent p;
    p.color = 2;
    p.child.n = 5;
    Child k;
    k.n = 9;
    p.kids.push_back(k);
    p.note = "x";

    const auto wire = ctpb::encode(p);
    // color: 08 02. child: 12 02 08 05. kids: 1A 02 08 09. note: 22 01 78.
    const unsigned char want[] = {0x08, 0x02, 0x12, 0x02, 0x08, 0x05, 0x1A, 0x02, 0x08, 0x09,
                                  0x22, 0x01, 0x78};
    assert(wire.size() == sizeof(want));
    for (std::size_t i = 0; i < sizeof(want); ++i) {
        assert(static_cast<unsigned char>(wire[i]) == want[i]);
    }

    auto back = ctpb::decode<Parent>(wire);
    assert(back.has_value());
    assert(back->color == 2);
    assert(back->child.n == 5);
    assert(back->kids.size() == 1 && back->kids[0].n == 9);
    assert(back->note == "x");

    std::printf("test_reflected_submessage: OK\n");
    return 0;
}

#else

int main() {
    std::printf("test_reflected_submessage: SKIPPED (no P2996 reflection)\n");
    return 0;
}

#endif
