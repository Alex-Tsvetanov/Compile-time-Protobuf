# Dockerfile for ctpb with P2996 Static Reflection Toolchain
FROM --platform=linux/amd64 sleepingeight/clang-cpp-refl:latest

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    cmake \
    git \
    ninja-build \
    python3 \
    pkg-config \
    libprotobuf-dev \
    protobuf-compiler \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace

# Run full build, examples, and tests with P2996 reflection enabled
# No -stdlib=libstdc++ here: <meta> ships in the libc++ tree of this toolchain,
# so asking for libstdc++ hides it and the whole library silently falls back to
# its C++20 path -- which is what used to happen, benchmarks included. The
# protobuf interoperability test still selects libstdc++ for itself, since the
# distribution's libprotobuf is built against it.
CMD ["/bin/bash", "-c", "rm -rf build && cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_STANDARD=26 -DCTPB_BUILD_TESTS=ON -DCTPB_BUILD_BENCHMARKS=ON && cmake --build build && ctest --test-dir build --output-on-failure && ./build/examples/example_person"]
