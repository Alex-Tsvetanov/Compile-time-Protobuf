# Benchmarks. Off by default; turn on with -DCTPB_BUILD_BENCHMARKS=ON.

add_executable(bench_compile_time bench_compile_time.cpp)
target_link_libraries(bench_compile_time PRIVATE ctpb::ctpb)

add_executable(bench_runtime bench_runtime.cpp)
target_link_libraries(bench_runtime PRIVATE ctpb::ctpb)
