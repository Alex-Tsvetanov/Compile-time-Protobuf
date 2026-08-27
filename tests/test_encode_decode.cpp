// tests/test_encode_decode.cpp

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <ctpb/ctpb.hpp>

int main() {
    using namespace ctpb::wire;

    // Test Varint
    {
        std::vector<std::byte> buf;
        write_varint(buf, 300);
        assert(buf.size() == 2);
        assert(buf[0] == static_cast<std::byte>(0xAC));
        assert(buf[1] == static_cast<std::byte>(0x02));

        reader r{buf};
        std::uint64_t val = 0;
        assert(r.read_varint(val));
        assert(val == 300);
    }

    // Test Zigzag
    {
        std::vector<std::byte> buf;
        write_zigzag32(buf, -1);
        write_zigzag32(buf, 1);
        write_zigzag32(buf, -2);
        write_zigzag32(buf, 2);

        reader r{buf};
        std::int32_t z1, z2, z3, z4;
        assert(r.read_zigzag32(z1) && z1 == -1);
        assert(r.read_zigzag32(z2) && z2 == 1);
        assert(r.read_zigzag32(z3) && z3 == -2);
        assert(r.read_zigzag32(z4) && z4 == 2);
    }

    // Test Fixed32 and Float
    {
        std::vector<std::byte> buf;
        write_fixed32(buf, 0x12345678);
        write_float(buf, 3.14f);

        reader r{buf};
        std::uint32_t u = 0;
        float f = 0.0f;
        assert(r.read_fixed32(u) && u == 0x12345678);
        assert(r.read_float(f) && (f > 3.13f && f < 3.15f));
    }

    // Test Fixed64 and Double
    {
        std::vector<std::byte> buf;
        write_fixed64(buf, 0x0102030405060708ULL);
        write_double(buf, 2.718281828459);

        reader r{buf};
        std::uint64_t u = 0;
        double d = 0.0;
        assert(r.read_fixed64(u) && u == 0x0102030405060708ULL);
        assert(r.read_double(d) && (d > 2.718 && d < 2.719));
    }

    // Test Length-delimited string
    {
        std::vector<std::byte> buf;
        write_string(buf, "Hello Protocol Buffers!");

        reader r{buf};
        std::string_view sv;
        assert(r.read_string(sv));
        assert(sv == "Hello Protocol Buffers!");
    }

    // Test Tag and Field Skipping
    {
        std::vector<std::byte> buf;
        write_tag(buf, 1, wire_type::varint);
        write_varint(buf, 99999);
        write_tag(buf, 2, wire_type::len);
        write_string(buf, "skip me");
        write_tag(buf, 3, wire_type::fixed32);
        write_fixed32(buf, 0xDEADBEEF);
        write_tag(buf, 4, wire_type::varint);
        write_varint(buf, 42);

        reader r{buf};
        std::int64_t fnum;
        wire_type wt;

        assert(r.read_tag(fnum, wt) && fnum == 1 && wt == wire_type::varint);
        assert(r.skip_field(wt));

        assert(r.read_tag(fnum, wt) && fnum == 2 && wt == wire_type::len);
        assert(r.skip_field(wt));

        assert(r.read_tag(fnum, wt) && fnum == 3 && wt == wire_type::fixed32);
        assert(r.skip_field(wt));

        assert(r.read_tag(fnum, wt) && fnum == 4 && wt == wire_type::varint);
        std::uint64_t final_val = 0;
        assert(r.read_varint(final_val) && final_val == 42);
        assert(r.empty());
    }

    std::printf("test_encode_decode: OK\n");
    return 0;
}
