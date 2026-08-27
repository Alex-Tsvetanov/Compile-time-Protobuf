# Examples for ctpb

add_executable(example_person person.cpp)
target_link_libraries(example_person PRIVATE ctpb::ctpb)
