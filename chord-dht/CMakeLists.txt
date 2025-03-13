add_library(chord-dht
        src/dht.cpp
)

target_include_directories(chord-dht
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(chord-dht PUBLIC chord)
set_target_properties(chord-dht PROPERTIES EXPORT_NAME ChordDHT)
