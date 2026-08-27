// tests/test_wire_edge_cases.cpp
//
// Regression test for three wire-format defects found during external review
// (26 Aug 2026), all rooted in the same gap: the generic field dispatch in
// reflection.hpp special-cased strings and small scalars but not the other
// shapes IsVector<T> can match. Each case below reproduces one confirmed
// failure and pins the fix:
//   1. A singular `bytes` field encoded as N one-byte repeated varint fields
//      instead of one length-delimited blob (wire bloat + wrong wire type).
//   2. A `repeated int32` field could not decode real protobuf's packed
//      wire format at all: it silently produced zero elements, no error.
//   3. A `repeated float`/`double` field had no handling at all and
//      silently encoded to zero bytes, dropping the entire field.
// A fourth case (repeated bytes) shares the same root cause as (1) and is
// covered here too.

#include <cassert>
#include <cstdio>
#include <tuple>
#include <vector>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string blob_proto = R"(
    syntax = "proto3";
    message Blob { bytes data = 1; }
)";
using BlobSchema = ctpb::parse<blob_proto, "Blob">;
struct BlobMsg : ctpb::reflect_base<BlobSchema, BlobMsg> {
    std::vector<std::byte> data;
    auto reflect_members() { return std::tie(data); }
};

inline constexpr ctpb::fixed_string chunks_proto = R"(
    syntax = "proto3";
    message Chunks { repeated bytes blobs = 1; }
)";
using ChunksSchema = ctpb::parse<chunks_proto, "Chunks">;
struct ChunksMsg : ctpb::reflect_base<ChunksSchema, ChunksMsg> {
    std::vector<std::vector<std::byte>> blobs;
    auto reflect_members() { return std::tie(blobs); }
};

inline constexpr ctpb::fixed_string tags_proto = R"(
    syntax = "proto3";
    message Tags { repeated int32 ids = 1; }
)";
using TagsSchema = ctpb::parse<tags_proto, "Tags">;
struct TagsMsg : ctpb::reflect_base<TagsSchema, TagsMsg> {
    std::vector<std::int32_t> ids;
    auto reflect_members() { return std::tie(ids); }
};

inline constexpr ctpb::fixed_string scores_proto = R"(
    syntax = "proto3";
    message Scores { repeated float vals = 1; }
)";
using ScoresSchema = ctpb::parse<scores_proto, "Scores">;
struct ScoresMsg : ctpb::reflect_base<ScoresSchema, ScoresMsg> {
    std::vector<float> vals;
    auto reflect_members() { return std::tie(vals); }
};

int main() {
    // 1. Singular `bytes` must be one length-delimited field, matching what
    //    real protobuf emits: tag(0x0A) + len(4) + 4 payload bytes = 6 bytes.
    {
        BlobMsg m;
        m.data = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
        auto wire = ctpb::encode(m);
        std::vector<std::byte> expected = {
            std::byte{0x0A}, std::byte{0x04},
            std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}
        };
        assert(wire == expected);

        auto back = ctpb::decode<BlobMsg>(wire);
        assert(back.has_value());
        assert(back->data == m.data);
    }

    // 2. Repeated bytes: each blob is its own length-delimited element.
    {
        ChunksMsg m;
        m.blobs = {{std::byte{1}, std::byte{2}}, {std::byte{9}}};
        auto wire = ctpb::encode(m);
        auto back = ctpb::decode<ChunksMsg>(wire);
        assert(back.has_value());
        assert(back->blobs == m.blobs);
    }

    // 3. Repeated scalar must decode real protobuf's packed wire format:
    //    tag(0x0A) + len(3) + varints [1, 2, 3], with no outer per-element tag.
    {
        std::vector<std::byte> real_protobuf_wire = {
            std::byte{0x0A}, std::byte{0x03}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}
        };
        auto decoded = ctpb::decode<TagsMsg>(real_protobuf_wire);
        assert(decoded.has_value());
        assert((decoded->ids == std::vector<std::int32_t>{1, 2, 3}));

        // ctpb's own encode must also produce the packed form, byte for byte.
        TagsMsg m; m.ids = {1, 2, 3};
        auto wire = ctpb::encode(m);
        assert(wire == real_protobuf_wire);

        // Backward-compatible unpacked encoding (one tag per element) must
        // still decode, per the proto3 spec's compatibility guarantee.
        std::vector<std::byte> unpacked_wire = {
            std::byte{0x08}, std::byte{0x01}, std::byte{0x08}, std::byte{0x02}, std::byte{0x08}, std::byte{0x03}
        };
        auto decoded_unpacked = ctpb::decode<TagsMsg>(unpacked_wire);
        assert(decoded_unpacked.has_value());
        assert((decoded_unpacked->ids == std::vector<std::int32_t>{1, 2, 3}));
    }

    // 4. Repeated float/double must not be silently dropped.
    {
        ScoresMsg m;
        m.vals = {1.5f, 2.5f, 3.5f};
        auto wire = ctpb::encode(m);
        assert(!wire.empty());
        auto back = ctpb::decode<ScoresMsg>(wire);
        assert(back.has_value());
        assert(back->vals == m.vals);
    }

    std::printf("test_wire_edge_cases: OK\n");
    return 0;
}
