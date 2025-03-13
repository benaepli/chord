add_executable(chord-cli
        src/main.cpp
)

target_include_directories(chord-cli
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

target_link_libraries(chord-cli PRIVATE chord-dht)