add_library(chord
        src/chord.cpp
        src/node.cpp
)

find_package(tl-expected CONFIG REQUIRED)
target_link_libraries(chord PUBLIC tl::expected)

find_package(gRPC CONFIG REQUIRED)
target_link_libraries(chord PRIVATE gRPC::gpr gRPC::grpc gRPC::grpc++ gRPC::grpc++_alts)


# Set up include directories
target_include_directories(chord
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(chord PROPERTIES EXPORT_NAME Chord)
