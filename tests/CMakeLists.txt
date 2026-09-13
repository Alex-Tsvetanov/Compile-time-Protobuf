# Tests for ctpb

function(ctpb_add_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE ctpb::ctpb)
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
ctpb_add_test(test_utf8_validation test_utf8_validation.cpp)
ctpb_add_test(test_view test_view.cpp)
ctpb_add_test(test_view_regressions test_view_regressions.cpp)

# Official Protobuf & gRPC interoperability verification
find_package(PkgConfig QUIET)
find_program(PROTOC_EXECUTABLE protoc)

if(PkgConfig_FOUND AND PROTOC_EXECUTABLE)
    pkg_check_modules(PROTOBUF_PKG protobuf)
    if(PROTOBUF_PKG_FOUND)
        set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/addressbook.pb.cc")
        set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/addressbook.pb.h")
        
        add_custom_command(
            OUTPUT "${PROTO_SRC}" "${PROTO_HDR}"
            COMMAND ${PROTOC_EXECUTABLE}
                --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/protos
                --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/protos/addressbook.proto
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protos/addressbook.proto
            COMMENT "Generating addressbook.pb.cc with protoc"
        )
        
        add_executable(test_interop_protobuf test_interop_protobuf.cpp ${PROTO_SRC} ${PROTO_HDR})
        target_include_directories(test_interop_protobuf PRIVATE ${PROTOBUF_PKG_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
        target_link_directories(test_interop_protobuf PRIVATE ${PROTOBUF_PKG_LIBRARY_DIRS})
        # libstdc++ is what the protobuf packages are built against on Linux.
        # Elsewhere (macOS, where Clang ships libc++ and no libstdc++) asking
        # for it fails the compile on headers as basic as <limits>.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_options(test_interop_protobuf PRIVATE -stdlib=libstdc++)
            target_link_options(test_interop_protobuf PRIVATE -stdlib=libstdc++)
        endif()
        target_link_libraries(test_interop_protobuf PRIVATE ctpb::ctpb ${PROTOBUF_PKG_LIBRARIES})
        add_test(NAME test_interop_protobuf COMMAND test_interop_protobuf)
        message(STATUS "Protobuf found via PkgConfig: building test_interop_protobuf")
    endif()
endif()
