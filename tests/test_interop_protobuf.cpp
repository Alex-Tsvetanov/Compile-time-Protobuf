// tests/test_interop_protobuf.cpp
// Automated interoperability test between ctpb and Google's official libprotobuf / protoc.
// Also validates standard gRPC HTTP/2 5-byte message framing round-trip.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <ctpb/ctpb.hpp>
#include "addressbook.pb.h"
#include "presence.pb.h"
#include "test_support.hpp"

// Proto schema embedded and parsed at compile-time
inline constexpr ctpb::fixed_string addressbook_proto = R"(
syntax = "proto3";
package tutorial;

message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;

  enum PhoneType {
    MOBILE = 0;
    HOME = 1;
    WORK = 2;
  }

  message PhoneNumber {
    string number = 1;
    PhoneType type = 2;
  }

  repeated PhoneNumber phones = 4;
  bytes avatar = 5;
  repeated int32 lucky_numbers = 6;
}
)";

// Compile-time schema AST bindings
using PhoneNumberSchema = ctpb::parse<addressbook_proto, "tutorial.Person.PhoneNumber">;
using PersonSchema      = ctpb::parse<addressbook_proto, "tutorial.Person">;

struct CtpbPhoneNumber : ctpb::reflect_base<PhoneNumberSchema, CtpbPhoneNumber> {
    std::string number;
    std::int32_t type = 0;

    CtpbPhoneNumber() = default;
    CtpbPhoneNumber(std::string num, std::int32_t t) : number(std::move(num)), type(t) {}

    auto reflect_members() { return std::tie(number, type); }
};

struct CtpbPerson : ctpb::reflect_base<PersonSchema, CtpbPerson> {
    std::string name;
    std::int32_t id = 0;
    std::string email;
    std::vector<CtpbPhoneNumber> phones;
    std::vector<std::byte> avatar;
    std::vector<std::int32_t> lucky_numbers;

    auto reflect_members() { return std::tie(name, id, email, phones, avatar, lucky_numbers); }
};

// Unlike assert, this check stays when NDEBUG is defined. Use it for results of
// calls the test depends on, so a Release build cannot skip the call itself.
inline void require(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "test_interop_protobuf: FAILED: %s\n", what);
        std::abort();
    }
}

// gRPC HTTP/2 5-byte framing helpers:
// [1 byte compression flag (0x00 = uncompressed)] [4 bytes big-endian length] [payload]
inline std::vector<std::byte> grpc_frame(std::span<const std::byte> payload) {
    std::vector<std::byte> frame;
    frame.reserve(5 + payload.size());
    frame.push_back(static_cast<std::byte>(0x00)); // uncompressed
    std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    frame.push_back(static_cast<std::byte>((len >> 24) & 0xFF));
    frame.push_back(static_cast<std::byte>((len >> 16) & 0xFF));
    frame.push_back(static_cast<std::byte>((len >> 8) & 0xFF));
    frame.push_back(static_cast<std::byte>(len & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

inline std::span<const std::byte> grpc_unframe(std::span<const std::byte> frame) {
    assert(frame.size() >= 5);
    std::uint8_t compressed = static_cast<std::uint8_t>(frame[0]);
    assert(compressed == 0);
    std::uint32_t len = (static_cast<std::uint32_t>(frame[1]) << 24) |
                        (static_cast<std::uint32_t>(frame[2]) << 16) |
                        (static_cast<std::uint32_t>(frame[3]) << 8) |
                        static_cast<std::uint32_t>(frame[4]);
    assert(frame.size() == 5 + len);
    return frame.subspan(5, len);
}

// -------------------------------------------------------------
// proto3 optional, oneof and map against libprotobuf (presence.proto).
// The schema text is the same file protoc compiles, read with #embed.
// -------------------------------------------------------------
inline constexpr char presence_proto_text[] = {
#embed "protos/presence.proto"
    , '\0'};
inline constexpr ctpb::fixed_string presence_proto{presence_proto_text};

namespace presence_test {

using test_support::at;
using test_support::encode_checked;

// Member indices, in declaration order.
enum : std::size_t { P_COUNT, P_TITLE, P_FLAG, P_KIND, P_DELTA, P_RATIO, P_ITEM, P_PLAIN };
enum : std::size_t { C_ID, C_BODY };
enum : std::size_t { V_NONE, V_TEXT, V_CODE, V_DELTA, V_ITEM, V_BLOB };
enum : std::size_t { M_COUNTS, M_NAMES, M_ITEMS, M_KINDS, M_BLOBS };

inline std::vector<std::byte> to_bytes(const std::string& s) {
    auto view = std::as_bytes(std::span(s.data(), s.size()));
    return {view.begin(), view.end()};
}

inline std::string serialize(const google::protobuf::Message& m) {
    std::string out;
    const bool ok = m.SerializeToString(&out);
    require(ok, "SerializeToString (presence)");
    return out;
}

template <typename G>
G parse_google(const std::vector<std::byte>& wire) {
    G g;
    const bool ok = g.ParseFromArray(wire.data(), static_cast<int>(wire.size()));
    require(ok, "ParseFromArray (presence)");
    return g;
}

template <typename Presence, typename Item>
void check_optional() {
    // Every optional set to its zero value: libprotobuf writes each one, and so
    // must ctpb, byte for byte.
    interop::Presence g;
    g.set_count(0);
    g.set_title("");
    g.set_flag(false);
    g.set_kind(interop::KIND_ZERO);
    g.set_delta(0);
    g.set_ratio(0.0);
    g.mutable_item();
    const std::string google_zero = serialize(g);

    Presence c{};
    at<P_COUNT>(c) = 0;
    at<P_TITLE>(c) = std::string{};
    at<P_FLAG>(c) = false;
    at<P_KIND>(c) = 0;
    at<P_DELTA>(c) = 0;
    at<P_RATIO>(c) = 0.0;
    at<P_ITEM>(c) = Item{};
    require(encode_checked(c) == to_bytes(google_zero), "optional zero values: same bytes as libprotobuf");

    auto back = ctpb::decode<Presence>(to_bytes(google_zero));
    require(back.has_value(), "decode libprotobuf optional zero values");
    require(at<P_COUNT>(*back) == 0 && at<P_TITLE>(*back) == std::string{} && at<P_FLAG>(*back) == false,
            "optional zero values engaged after decode");
    require(at<P_KIND>(*back) == 0 && at<P_DELTA>(*back) == 0 && at<P_RATIO>(*back) == 0.0,
            "optional zero values engaged after decode");
    require(at<P_ITEM>(*back).has_value(), "optional empty message engaged after decode");

    // Nothing set: both sides write nothing, and nothing is engaged.
    require(serialize(interop::Presence{}).empty(), "libprotobuf writes nothing for unset optionals");
    back = ctpb::decode<Presence>(std::vector<std::byte>{});
    require(back.has_value() && !at<P_COUNT>(*back).has_value() && !at<P_ITEM>(*back).has_value(),
            "no optional engaged for empty input");

    // ctpb encodes, libprotobuf reads presence and values.
    Presence d{};
    at<P_COUNT>(d) = -5;
    at<P_TITLE>(d) = std::string("t");
    at<P_KIND>(d) = 2;
    at<P_DELTA>(d) = -7;
    at<P_RATIO>(d) = 0.5;
    Item item{};
    at<0>(item) = 3;
    at<P_ITEM>(d) = item;
    at<P_PLAIN>(d) = 4;
    const auto wire = encode_checked(d);
    const auto gd = parse_google<interop::Presence>(wire);
    require(gd.has_count() && gd.count() == -5 && gd.has_title() && gd.title() == "t", "optional values in libprotobuf");
    require(!gd.has_flag(), "unset optional stays unset in libprotobuf");
    require(gd.has_kind() && gd.kind() == interop::KIND_TWO && gd.has_delta() && gd.delta() == -7,
            "optional values in libprotobuf");
    require(gd.has_ratio() && gd.ratio() == 0.5 && gd.has_item() && gd.item().n() == 3 && gd.plain() == 4,
            "optional values in libprotobuf");
    require(serialize(gd) == std::string(reinterpret_cast<const char*>(wire.data()), wire.size()),
            "optional values: same bytes as libprotobuf");
}

template <typename Choice, typename Item>
void check_oneof() {
    // Each alternative set to a zero or empty value: byte equal with libprotobuf.
    interop::Choice g;
    Choice c{};

    g.set_text("");
    at<C_BODY>(c).template emplace<V_TEXT>();
    require(encode_checked(c) == to_bytes(serialize(g)), "oneof text: same bytes as libprotobuf");

    g.set_code(0);
    at<C_BODY>(c).template emplace<V_CODE>(0);
    require(encode_checked(c) == to_bytes(serialize(g)), "oneof code: same bytes as libprotobuf");

    g.set_delta(-2);
    at<C_BODY>(c).template emplace<V_DELTA>(-2);
    require(encode_checked(c) == to_bytes(serialize(g)), "oneof delta: same bytes as libprotobuf");

    g.mutable_item();
    at<C_BODY>(c).template emplace<V_ITEM>();
    require(encode_checked(c) == to_bytes(serialize(g)), "oneof item: same bytes as libprotobuf");

    g.set_blob(std::string("\x00\x01", 2));
    g.set_id(9);
    at<C_BODY>(c).template emplace<V_BLOB>(std::vector<std::byte>{std::byte{0x00}, std::byte{0x01}});
    at<C_ID>(c) = 9;
    require(encode_checked(c) == to_bytes(serialize(g)), "oneof blob: same bytes as libprotobuf");

    // libprotobuf reads what ctpb wrote.
    at<C_BODY>(c).template emplace<V_DELTA>(0);
    auto gd = parse_google<interop::Choice>(encode_checked(c));
    require(gd.body_case() == interop::Choice::kDelta && gd.delta() == 0, "oneof delta 0 read by libprotobuf");

    // Last alternative wins: two messages back to back merge, on both sides.
    interop::Choice first;
    first.set_code(5);
    interop::Choice second;
    second.set_text("x");
    const std::string joined = serialize(first) + serialize(second);
    interop::Choice gj;
    require(gj.ParseFromString(joined), "ParseFromString (oneof joined)");
    auto cj = ctpb::decode<Choice>(to_bytes(joined));
    require(cj.has_value() && gj.body_case() == interop::Choice::kText, "oneof: last wins");
    require(at<C_BODY>(*cj).index() == V_TEXT && std::get<V_TEXT>(at<C_BODY>(*cj)) == "x", "oneof: last wins");
}

template <typename Maps, typename Item>
void check_maps() {
    // One entry per map, with default keys and values: libprotobuf writes key
    // and value too, so the bytes match.
    interop::Maps g;
    (*g.mutable_counts())[""] = 0;
    (*g.mutable_names())[0] = "";
    (*g.mutable_items())[0];
    (*g.mutable_kinds())[false] = interop::KIND_ZERO;
    (*g.mutable_blobs())[0] = "";
    Maps c{};
    at<M_COUNTS>(c)[""] = 0;
    at<M_NAMES>(c)[0] = "";
    at<M_ITEMS>(c)[0] = Item{};
    at<M_KINDS>(c)[false] = 0;
    at<M_BLOBS>(c)[0] = {};
    require(encode_checked(c) == to_bytes(serialize(g)), "map default entries: same bytes as libprotobuf");

    // Non-default single entries.
    interop::Maps g1;
    (*g1.mutable_names())[-3] = "neg";
    Item item{};
    at<0>(item) = 4;
    at<1>(item) = "four";
    (*g1.mutable_items())[7].set_n(4);
    (*g1.mutable_items())[7].set_label("four");
    (*g1.mutable_kinds())[true] = interop::KIND_TWO;
    (*g1.mutable_blobs())[0xFFFFFFFFFFull] = std::string("\xAB", 1);
    Maps c1{};
    at<M_NAMES>(c1)[-3] = "neg";
    at<M_ITEMS>(c1)[7] = item;
    at<M_KINDS>(c1)[true] = 2;
    at<M_BLOBS>(c1)[0xFFFFFFFFFFull] = {std::byte{0xAB}};
    require(encode_checked(c1) == to_bytes(serialize(g1)), "map entries: same bytes as libprotobuf");

    // Several entries: libprotobuf's order is not fixed, so compare contents
    // in both directions.
    interop::Maps g2;
    for (int i = 0; i < 5; ++i) {
        (*g2.mutable_counts())["k" + std::to_string(i)] = i * 10;
        (*g2.mutable_names())[-i] = std::string(static_cast<std::size_t>(i), 'z');
    }
    auto c2 = ctpb::decode<Maps>(to_bytes(serialize(g2)));
    require(c2.has_value() && at<M_COUNTS>(*c2).size() == 5 && at<M_NAMES>(*c2).size() == 5, "maps from libprotobuf");
    for (int i = 0; i < 5; ++i) {
        require(at<M_COUNTS>(*c2).at("k" + std::to_string(i)) == i * 10, "map value from libprotobuf");
        require(at<M_NAMES>(*c2).at(-i) == std::string(static_cast<std::size_t>(i), 'z'), "map value from libprotobuf");
    }
    const auto g3 = parse_google<interop::Maps>(encode_checked(*c2));
    require(g3.counts_size() == 5 && g3.names_size() == 5, "maps read by libprotobuf");
    for (int i = 0; i < 5; ++i) {
        require(g3.counts().at("k" + std::to_string(i)) == i * 10, "map value read by libprotobuf");
        require(g3.names().at(-i) == std::string(static_cast<std::size_t>(i), 'z'), "map value read by libprotobuf");
    }

    // A repeated key: the last entry wins on both sides.
    interop::Maps a;
    (*a.mutable_counts())["dup"] = 1;
    interop::Maps b;
    (*b.mutable_counts())["dup"] = 2;
    const std::string joined = serialize(a) + serialize(b);
    interop::Maps gj;
    require(gj.ParseFromString(joined), "ParseFromString (map joined)");
    auto cj = ctpb::decode<Maps>(to_bytes(joined));
    require(gj.counts().at("dup") == 2, "libprotobuf: last map entry wins");
    require(cj.has_value() && at<M_COUNTS>(*cj).size() == 1 && at<M_COUNTS>(*cj).at("dup") == 2,
            "ctpb: last map entry wins");
}

template <typename Presence, typename Choice, typename Maps, typename Item>
void check_all() {
    check_optional<Presence, Item>();
    check_oneof<Choice, Item>();
    check_maps<Maps, Item>();
}

}  // namespace presence_test

#if CTPB_HAS_P2996
consteval { ctpb::define_message<presence_proto, "Item">(); }
consteval { ctpb::define_message<presence_proto, "Presence">(); }
consteval { ctpb::define_message<presence_proto, "Choice">(); }
consteval { ctpb::define_message<presence_proto, "Maps">(); }
#endif

int main() {
    // -------------------------------------------------------------
    // Test 1: Google Protoc encodes -> ctpb decodes
    // -------------------------------------------------------------
    {
        tutorial::Person google_person;
        google_person.set_name("Nikola Tesla");
        google_person.set_id(1856);
        google_person.set_email("tesla@wardenclyffe.org");

        auto* ph1 = google_person.add_phones();
        ph1->set_number("+1-555-0100");
        ph1->set_type(tutorial::Person::MOBILE);

        auto* ph2 = google_person.add_phones();
        ph2->set_number("+1-555-0199");
        ph2->set_type(tutorial::Person::WORK);

        // A `bytes` field (non-UTF8 payload) and a packed `repeated int32` field:
        // proto3's default wire encoding for both, and the two shapes a wire-format
        // review (26 Aug 2026) found ctpb's reflection dispatch mis-encoded.
        google_person.set_avatar(std::string("\x00\x01\xDE\xAD\xBE\xEF", 6));
        for (int n : {3, 1, 4, 1, 5, 9, 2, 6}) google_person.add_lucky_numbers(n);

        std::string serialized;
        const bool serialized_ok = google_person.SerializeToString(&serialized);
        require(serialized_ok, "SerializeToString (Test 1)");

        auto span_bytes = std::as_bytes(std::span(serialized.data(), serialized.size()));
        auto ctpb_decoded = ctpb::decode<CtpbPerson>(span_bytes);
        assert(ctpb_decoded.has_value());

        // Direct dot syntax:
        assert(ctpb_decoded->name == "Nikola Tesla");
        assert(ctpb_decoded->id == 1856);
        assert(ctpb_decoded->email == "tesla@wardenclyffe.org");
        assert(ctpb_decoded->phones.size() == 2);
        assert(ctpb_decoded->phones[0].number == "+1-555-0100");
        assert(ctpb_decoded->phones[0].type == 0);
        assert(ctpb_decoded->phones[1].number == "+1-555-0199");
        assert(ctpb_decoded->phones[1].type == 2);

        std::vector<std::byte> expected_avatar = {
            std::byte{0x00}, std::byte{0x01}, std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}
        };
        assert(ctpb_decoded->avatar == expected_avatar);
        assert((ctpb_decoded->lucky_numbers == std::vector<std::int32_t>{3, 1, 4, 1, 5, 9, 2, 6}));
        std::printf("[PASS] Test 1: Google protoc -> ctpb decode validated (incl. bytes + packed repeated).\n");
    }

    // -------------------------------------------------------------
    // Test 2: ctpb encodes -> Google Protoc decodes
    // -------------------------------------------------------------
    {
        CtpbPerson ctpb_p;
        ctpb_p.name = "Ada Lovelace";
        ctpb_p.id = 1815;
        ctpb_p.email = "ada@analyticalengine.org";
        ctpb_p.phones.push_back({"+44-20-7946-0919", 1}); // HOME
        ctpb_p.avatar = {std::byte{0xCA}, std::byte{0xFE}, std::byte{0x00}, std::byte{0xBA}, std::byte{0xBE}};
        ctpb_p.lucky_numbers = {7, 11, 21, 42};

        std::vector<std::byte> wire_bytes = ctpb::encode(ctpb_p);
        assert(!wire_bytes.empty());

        tutorial::Person google_decoded;
        bool parse_ok = google_decoded.ParseFromArray(wire_bytes.data(), static_cast<int>(wire_bytes.size()));
        assert(parse_ok);

        assert(google_decoded.name() == "Ada Lovelace");
        assert(google_decoded.id() == 1815);
        assert(google_decoded.email() == "ada@analyticalengine.org");
        assert(google_decoded.phones_size() == 1);
        assert(google_decoded.phones(0).number() == "+44-20-7946-0919");
        assert(google_decoded.phones(0).type() == tutorial::Person::HOME);
        assert(google_decoded.avatar() == std::string("\xCA\xFE\x00\xBA\xBE", 5));
        assert(google_decoded.lucky_numbers_size() == 4);
        assert(google_decoded.lucky_numbers(0) == 7);
        assert(google_decoded.lucky_numbers(3) == 42);
        std::printf("[PASS] Test 2: ctpb encode -> Google protoc decode validated (incl. bytes + packed repeated).\n");
    }

    // -------------------------------------------------------------
    // Test 3: Simulated gRPC Client-Server Transport Round-trip
    // -------------------------------------------------------------
    {
        // 1. Client (using ctpb): Encodes message and wraps into 5-byte gRPC frame
        CtpbPerson request_msg;
        request_msg.name = "Alex Tsvetanov";
        request_msg.id = 2026;
        request_msg.email = "alex@tu-sofia.bg";
        request_msg.phones.push_back({"+359-2-965-2111", 2}); // WORK

        std::vector<std::byte> payload = ctpb::encode(request_msg);
        std::vector<std::byte> client_grpc_frame = grpc_frame(payload);
        assert(client_grpc_frame.size() == 5 + payload.size());

        // 2. Server (using standard Google gRPC/Protobuf backend): Unframes and parses
        std::span<const std::byte> server_unframed = grpc_unframe(client_grpc_frame);
        tutorial::Person server_received;
        const bool received_ok =
            server_received.ParseFromArray(server_unframed.data(), static_cast<int>(server_unframed.size()));
        require(received_ok, "ParseFromArray (Test 3, server)");
        assert(server_received.name() == "Alex Tsvetanov");
        assert(server_received.id() == 2026);
        assert(server_received.email() == "alex@tu-sofia.bg");
        assert(server_received.phones_size() == 1);
        assert(server_received.phones(0).type() == tutorial::Person::WORK);

        // 3. Server sends response back in gRPC frame
        tutorial::Person server_response;
        server_response.set_name("Server Ack: " + server_received.name());
        server_response.set_id(server_received.id() + 1);
        server_response.set_email("ack@" + server_received.email());

        std::string server_res_str;
        const bool response_ok = server_response.SerializeToString(&server_res_str);
        require(response_ok, "SerializeToString (Test 3, response)");
        auto server_res_bytes = std::as_bytes(std::span(server_res_str.data(), server_res_str.size()));
        std::vector<std::byte> server_grpc_frame = grpc_frame(server_res_bytes);

        // 4. Client (ctpb): Unframes gRPC header and decodes payload with ctpb::decode
        std::span<const std::byte> client_unframed = grpc_unframe(server_grpc_frame);
        auto client_decoded_res = ctpb::decode<CtpbPerson>(client_unframed);
        assert(client_decoded_res.has_value());
        assert(client_decoded_res->name == "Server Ack: Alex Tsvetanov");
        assert(client_decoded_res->id == 2027);
        assert(client_decoded_res->email == "ack@alex@tu-sofia.bg");

        std::printf("[PASS] Test 3: Bidirectional gRPC framing exchange with Google backend validated.\n");
    }

    // -------------------------------------------------------------
    // Test 4: proto3 optional, oneof and map, both directions
    // -------------------------------------------------------------
    presence_test::check_all<ctpb::message<presence_proto, "Presence">, ctpb::message<presence_proto, "Choice">,
                             ctpb::message<presence_proto, "Maps">, ctpb::message<presence_proto, "Item">>();
    std::printf("[PASS] Test 4: optional, oneof and map match libprotobuf (template path).\n");
#if CTPB_HAS_P2996
    presence_test::check_all<ctpb::message_t<presence_proto, "Presence">, ctpb::message_t<presence_proto, "Choice">,
                             ctpb::message_t<presence_proto, "Maps">, ctpb::message_t<presence_proto, "Item">>();
    std::printf("[PASS] Test 5: optional, oneof and map match libprotobuf (P2996 path).\n");
#endif

    std::printf("test_interop_protobuf: ALL INTEROP & GRPC TESTS PASSED!\n");
    return 0;
}
