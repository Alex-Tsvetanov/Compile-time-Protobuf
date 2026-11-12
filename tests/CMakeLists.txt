# Tests for ctpb

# The tests check results with assert. Release flags define NDEBUG, which
# removes every assert, so undefine it for the tests.
function(ctpb_keep_asserts target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
endfunction()

function(ctpb_add_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE ctpb::ctpb)
    ctpb_keep_asserts(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ctpb_add_test(test_lexer test_lexer.cpp)
ctpb_add_test(test_parser test_parser.cpp)
ctpb_add_test(test_encode_decode test_encode_decode.cpp)
ctpb_add_test(test_roundtrip test_roundtrip.cpp)
ctpb_add_test(test_nested test_nested.cpp)
ctpb_add_test(test_oneof test_oneof.cpp)
ctpb_add_test(test_maps test_maps.cpp)
ctpb_add_test(test_wire_edge_cases test_wire_edge_cases.cpp)
ctpb_add_test(test_fixed_width test_fixed_width.cpp)
ctpb_add_test(test_keyword_fields test_keyword_fields.cpp)
ctpb_add_test(test_wire_groups test_wire_groups.cpp)
ctpb_add_test(test_schema_scopes test_schema_scopes.cpp)
ctpb_add_test(test_schema_diagnostics test_schema_diagnostics.cpp)
ctpb_add_test(test_reflected_submessage test_reflected_submessage.cpp)
ctpb_add_test(test_optional test_optional.cpp)
ctpb_add_test(test_oneof_variant test_oneof_variant.cpp)
ctpb_add_test(test_map_fields test_map_fields.cpp)
ctpb_add_test(test_reflected_autodefine test_reflected_autodefine.cpp)

# Schemas and structs that must be rejected at compile time. Each file in
# compile_fail/ starts with "// expect: <text>". The test builds the file and
# passes only when the build output contains that text. The text is used as a
# regular expression, so keep it free of regex special characters.
file(GLOB CTPB_COMPILE_FAIL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/*.cpp")
foreach(src IN LISTS CTPB_COMPILE_FAIL_SOURCES)
    get_filename_component(case_name "${src}" NAME_WE)
    file(STRINGS "${src}" expect_line REGEX "^// expect: " LIMIT_COUNT 1)
    string(REGEX REPLACE "^// expect: " "" expect_regex "${expect_line}")
    set(target "compile_fail_${case_name}")
    add_executable(${target} EXCLUDE_FROM_ALL "${src}")
    target_link_libraries(${target} PRIVATE ctpb::ctpb)
    add_test(NAME ${target}
             COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}" --target ${target} --config $<CONFIG>)
    # Each case runs a build in the same tree, so they must not run at the same time.
    set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${expect_regex}"
                                              RESOURCE_LOCK ctpb_compile_fail_build)
endforeach()

# Official Protobuf & gRPC interoperability verification
find_package(PkgConfig QUIET)
find_program(PROTOC_EXECUTABLE protoc)

if(PkgConfig_FOUND AND PROTOC_EXECUTABLE)
    pkg_check_modules(PROTOBUF_PKG protobuf)
    if(PROTOBUF_PKG_FOUND)
        set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/addressbook.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/presence.pb.cc")
        set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/addressbook.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/presence.pb.h")
        
        add_custom_command(
            OUTPUT ${PROTO_SRC} ${PROTO_HDR}
            COMMAND ${PROTOC_EXECUTABLE}
                --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/protos
                --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/protos/addressbook.proto
                ${CMAKE_CURRENT_SOURCE_DIR}/protos/presence.proto
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protos/addressbook.proto
                    ${CMAKE_CURRENT_SOURCE_DIR}/protos/presence.proto
            COMMENT "Generating addressbook.pb.cc and presence.pb.cc with protoc"
        )
        
        add_executable(test_interop_protobuf test_interop_protobuf.cpp ${PROTO_SRC} ${PROTO_HDR})
        target_include_directories(test_interop_protobuf PRIVATE ${PROTOBUF_PKG_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
        target_link_directories(test_interop_protobuf PRIVATE ${PROTOBUF_PKG_LIBRARY_DIRS})
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(test_interop_protobuf PRIVATE -stdlib=libstdc++)
            target_link_options(test_interop_protobuf PRIVATE -stdlib=libstdc++)
        endif()
        target_link_libraries(test_interop_protobuf PRIVATE ctpb::ctpb ${PROTOBUF_PKG_LIBRARIES})
        ctpb_keep_asserts(test_interop_protobuf)
        add_test(NAME test_interop_protobuf COMMAND test_interop_protobuf)
        message(STATUS "Protobuf found via PkgConfig: building test_interop_protobuf")
    endif()
endif()
