find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

set(PROTO_FILES chord_protos/node.proto)

add_library(chord_protos ${PROTO_FILES})
target_link_libraries(chord_protos
        PUBLIC
        gRPC::grpc++ gRPC::grpc++_alts
        protobuf::libprotobuf
)
target_include_directories(chord_protos PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET chord_protos LANGUAGE cpp)
protobuf_generate(TARGET chord_protos LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")
