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
CMD ["/bin/bash", "-c", "rm -rf build && cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS='-freflection -stdlib=libstdc++' -DCMAKE_CXX_STANDARD=26 -DCTPB_BUILD_TESTS=ON -DCTPB_BUILD_BENCHMARKS=ON && cmake --build build && ctest --test-dir build --output-on-failure && ./build/examples/example_person"]
