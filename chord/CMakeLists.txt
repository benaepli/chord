find_package(tl-expected CONFIG REQUIRED)

add_subdirectory(proto)

add_library(chord
        src/chord.cpp
        src/network.cpp
        src/node.cpp
)

target_link_libraries(chord PUBLIC tl::expected)
target_link_libraries(chord PRIVATE chord_protos)

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
