#!/bin/bash
set -e

DIR="/tmp/eval_ctpb_size"
rm -rf "$DIR"
mkdir -p "$DIR"
cd "$DIR"

# 1. Compile ctpb binary and object file from examples/person.cpp
clang++ -O3 -std=c++26 -freflection -I/workspace/include /workspace/examples/person.cpp -o "$DIR/person_ctpb"
clang++ -O3 -std=c++26 -freflection -I/workspace/include -c /workspace/examples/person.cpp -o "$DIR/person_ctpb.o"

# 2. Generate Protobuf C++ code via protoc
cat << 'EOF' > "$DIR/addressbook.proto"
syntax = "proto3";
package tutorial;

message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;
}
EOF

protoc --cpp_out="$DIR" --proto_path="$DIR" "$DIR/addressbook.proto"

cat << 'EOF' > "$DIR/person_protoc.cpp"
#include <cstdio>
#include <string>
#include "addressbook.pb.h"

int main() {
    tutorial::Person p;
    p.set_name("Alex Tsvetanov");
    p.set_id(42);
    p.set_email("atsvetanov@tu-sofia.bg");

    std::string wire;
    p.SerializeToString(&wire);
    std::printf("Encoded Person to %zu wire bytes\n", wire.size());

    tutorial::Person back;
    if (back.ParseFromString(wire)) {
        std::printf("Successfully decoded Person via protoc:\n");
        std::printf("  name  : %s\n", back.name().c_str());
        std::printf("  id    : %d\n", back.id());
        std::printf("  email : %s\n", back.email().c_str());
        return 0;
    }
    return 1;
}
EOF

# 3. Compile protoc generated files and main
clang++ -O3 -std=c++20 -stdlib=libstdc++ -I"$DIR" -c "$DIR/addressbook.pb.cc" -o "$DIR/addressbook.pb.o"
clang++ -O3 -std=c++20 -stdlib=libstdc++ -I"$DIR" -c "$DIR/person_protoc.cpp" -o "$DIR/person_protoc.o"

# Link protoc dynamic
clang++ -O3 -std=c++20 -stdlib=libstdc++ "$DIR/person_protoc.o" "$DIR/addressbook.pb.o" -lprotobuf -o "$DIR/person_protoc_dyn"

# Link protoc static if libprotobuf.a is available
if [ -f /usr/lib/x86_64-linux-gnu/libprotobuf.a ]; then
    clang++ -O3 -std=c++20 -stdlib=libstdc++ "$DIR/person_protoc.o" "$DIR/addressbook.pb.o" /usr/lib/x86_64-linux-gnu/libprotobuf.a -lpthread -lz -o "$DIR/person_protoc_static"
fi

echo "================================================================"
echo "1. GENERATED INTERMEDIATE SOURCE CODE LINES"
echo "================================================================"
echo "protoc code generator output:"
wc -l "$DIR/addressbook.pb.h" "$DIR/addressbook.pb.cc"
echo "ctpb static reflection output:"
echo "       0 lines (0 files, purely in-compiler AST reflection)"

echo ""
echo "================================================================"
echo "2. OBJECT FILE (.o) SIZES AND SECTIONS"
echo "================================================================"
ls -lh "$DIR/person_ctpb.o" "$DIR/addressbook.pb.o" "$DIR/person_protoc.o"
echo ""
size "$DIR/person_ctpb.o" "$DIR/addressbook.pb.o" "$DIR/person_protoc.o"

echo ""
echo "================================================================"
echo "3. UNSTRIPPED EXECUTABLE SIZES"
echo "================================================================"
ls -lh "$DIR/person_ctpb" "$DIR/person_protoc_dyn" "$DIR/person_protoc_static" 2>/dev/null || ls -lh "$DIR/person_ctpb" "$DIR/person_protoc_dyn"

echo ""
echo "================================================================"
echo "4. STRIPPED STANDALONE EXECUTABLE SIZES"
echo "================================================================"
strip -s "$DIR/person_ctpb" -o "$DIR/person_ctpb_stripped"
strip -s "$DIR/person_protoc_dyn" -o "$DIR/person_protoc_dyn_stripped"
if [ -f "$DIR/person_protoc_static" ]; then
    strip -s "$DIR/person_protoc_static" -o "$DIR/person_protoc_static_stripped"
fi
ls -lh "$DIR"/person_*_stripped

# Exact byte counts. `ls -lh` rounds to 19K / 1.7M, which is not enough to
# reproduce the footprint ratio quoted in the paper, so record the raw sizes too.
echo
echo "Exact sizes (bytes):"
stat -c '%s\t%n' "$DIR"/person_*_stripped 2>/dev/null || stat -f '%z%t%N' "$DIR"/person_*_stripped

echo ""
echo "================================================================"
echo "5. SECTION BREAKDOWN OF STRIPPED EXECUTABLES (size)"
echo "================================================================"
size "$DIR"/person_*_stripped

echo ""
echo "================================================================"
echo "6. RUNTIME DYNAMIC SHARED LIBRARY DEPENDENCIES (ldd)"
echo "================================================================"
echo "-- ctpb dependencies:"
ldd "$DIR/person_ctpb"
echo ""
echo "-- protoc dynamic dependencies:"
ldd "$DIR/person_protoc_dyn"

echo ""
echo "================================================================"
echo "7. EXECUTION VALIDATION"
echo "================================================================"
echo "-- Running ctpb:"
"$DIR/person_ctpb"
echo ""
echo "-- Running protoc:"
"$DIR/person_protoc_dyn"
