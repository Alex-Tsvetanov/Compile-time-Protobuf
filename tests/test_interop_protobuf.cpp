// tests/test_interop_protobuf.cpp
// Automated interoperability test between ctpb and Google's official libprotobuf / protoc.
// Also validates standard gRPC HTTP/2 5-byte message framing round-trip.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include <ctpb/ctpb.hpp>
#include "addressbook.pb.h"

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
        assert(google_person.SerializeToString(&serialized));

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
        assert(server_received.ParseFromArray(server_unframed.data(), static_cast<int>(server_unframed.size())));
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
        assert(server_response.SerializeToString(&server_res_str));
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

    std::printf("test_interop_protobuf: ALL INTEROP & GRPC TESTS PASSED!\n");
    return 0;
}
