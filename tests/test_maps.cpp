// tests/test_maps.cpp

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include <ctpb/ctpb.hpp>

struct InventoryMessage {
    std::unordered_map<std::string, std::int32_t> stock;

    void encode_into(std::vector<std::byte>& out) const {
        for (const auto& [k, v] : stock) {
            std::vector<std::byte> entry;
            // Key is field 1
            ctpb::wire::write_tag(entry, 1, ctpb::wire::wire_type::len);
            ctpb::wire::write_string(entry, k);
            // Value is field 2
            ctpb::wire::write_tag(entry, 2, ctpb::wire::wire_type::varint);
            ctpb::wire::write_varint(entry, static_cast<std::uint64_t>(v));

            // Map field number is 1
            ctpb::wire::write_tag(out, 1, ctpb::wire::wire_type::len);
            ctpb::wire::write_length_delimited(out, entry);
        }
    }

    void decode_from(std::span<const std::byte> bytes, ctpb::wire::decode_error& err) {
        ctpb::wire::reader r{bytes};
        while (!r.empty()) {
            std::int64_t num = 0;
            ctpb::wire::wire_type wt = ctpb::wire::wire_type::varint;
            if (!r.read_tag(num, wt)) { err.err = ctpb::wire::decode_error::bad_varint; return; }
            if (num == 1 && wt == ctpb::wire::wire_type::len) {
                std::span<const std::byte> entry_bytes;
                if (!r.read_length_delimited(entry_bytes)) { err.err = ctpb::wire::decode_error::truncated; return; }

                ctpb::wire::reader entry_reader{entry_bytes};
                std::string key;
                std::int32_t val = 0;
                while (!entry_reader.empty()) {
                    std::int64_t entry_num = 0;
                    ctpb::wire::wire_type entry_wt = ctpb::wire::wire_type::varint;
                    if (!entry_reader.read_tag(entry_num, entry_wt)) break;
                    if (entry_num == 1 && entry_wt == ctpb::wire::wire_type::len) {
                        std::string_view sv;
                        entry_reader.read_string(sv);
                        key = std::string(sv);
                    } else if (entry_num == 2 && entry_wt == ctpb::wire::wire_type::varint) {
                        std::uint64_t u = 0;
                        entry_reader.read_varint(u);
                        val = static_cast<std::int32_t>(u);
                    } else {
                        entry_reader.skip_field(entry_wt);
                    }
                }
                stock[key] = val;
            } else {
                r.skip_field(wt);
            }
        }
    }
};

int main() {
    using namespace ctpb;

    constexpr std::string_view maps_proto = R"(
        syntax = "proto3";
        package maps_test;

        message Inventory {
          map<string, int32> stock = 1;
          map<int64, string> labels = 2;
        }
    )";

    constexpr bool valid_maps = []() consteval {
        constexpr std::string_view proto = R"(
            syntax = "proto3";
            package maps_test;

            message Inventory {
              map<string, int32> stock = 1;
              map<int64, string> labels = 2;
            }
        )";
        parser p{proto};
        ast::file f = p.parse_file();
        if (f.messages.empty()) return false;
        return f.messages[0].maps.size() == 2;
    }();
    static_assert(valid_maps, "maps schema must be validated at compile time");

    parser p{maps_proto};
    auto schema = p.parse_file();
    assert(schema.messages.size() == 1);
    assert(schema.messages[0].maps.size() == 2);
    assert(schema.messages[0].maps[0].name == "stock");
    assert(schema.messages[0].maps[0].key_type.name == "string");
    assert(schema.messages[0].maps[0].value_type.name == "int32");

    InventoryMessage inv;
    inv.stock["apples"] = 50;
    inv.stock["oranges"] = 120;
    inv.stock["bananas"] = 85;

    auto bytes = ctpb::encode(inv);
    assert(!bytes.empty());

    auto dec = ctpb::decode<InventoryMessage>(bytes);
    assert(dec.has_value());
    assert(dec->stock.size() == 3);
    assert(dec->stock["apples"] == 50);
    assert(dec->stock["oranges"] == 120);
    assert(dec->stock["bananas"] == 85);

    std::printf("test_maps: OK\n");
    return 0;
}
