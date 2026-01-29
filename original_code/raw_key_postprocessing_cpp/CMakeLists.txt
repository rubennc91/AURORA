cmake_minimum_required(VERSION 3.16)

project(raw_keys_driver)

set(CMAKE_CXX_STANDARD 17)

#Warnings and optimizations to compiler
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O3" )

#gRPC-Protobuf

#message(STATUS "Using gRPC via add_subdirectory (FetchContent).")
#include(FetchContent)
#set(ABSL_ENABLE_INSTALL ON)
#FetchContent_Declare(
#grpc
#GIT_REPOSITORY https://github.com/grpc/grpc.git
#GIT_TAG        v1.74.0)
#FetchContent_MakeAvailable(grpc)

# Since FetchContent uses add_subdirectory under the hood, we can use
# the grpc targets directly from this build.

#set(_PROTOBUF_LIBPROTOBUF libprotobuf)
#set(_REFLECTION grpc++_reflection)
#set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc>)
#set(_GRPC_GRPCPP grpc++)
#set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)


#Compile protos

#set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
#set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto/src")
#
#file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")
#
#foreach(PROTO_FILE ${PROTO_FILES})
#    get_filename_component (FILENAME ${PROTO_FILE} NAME_WLE)
#    set(PROTO_SRC "${OUTPUT_DIR}/${FILENAME}.pb.cc")
#    set(PROTO_HDR "${OUTPUT_DIR}/${FILENAME}.pb.h")
#    set(GRPC_SRC "${OUTPUT_DIR}/${FILENAME}.grpc.pb.cc")
#    set(GRPC_HDR "${OUTPUT_DIR}/${FILENAME}.grpc.pb.h")
#
#    add_custom_command(
#        OUTPUT "${PROTO_SRC}" "${PROTO_HDR}" "${GRPC_SRC}" "${GRPC_HDR}"
#        COMMAND ${_PROTOBUF_PROTOC}
#        ARGS --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
#            --cpp_out="${OUTPUT_DIR}" --grpc_out="${OUTPUT_DIR}"
#            -I"${PROTO_DIR}" "${PROTO_FILE}"
#        DEPENDS ${PROTO_FILE}
#    )
#    list(APPEND OUTPUT_SOURCES ${PROTO_SRC} ${PROTO_HDR} ${GRPC_SRC} ${GRPC_HDR})
#endforeach()
#
#add_library(grpc_proto ${OUTPUT_SOURCES})
#
#target_link_libraries(grpc_proto PUBLIC ${_PROTOBUF_LIBPROTOBUF} ${_GRPC_GRPCPP})

#Main

include_directories(raw_key_client
    include
#    proto/src
)

add_executable(raw_key_client
    src/client.cpp
)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(raw_key_client PUBLIC
        OpenMP::OpenMP_CXX
#        grpc_proto
    )
else()
    target_link_libraries(raw_key_client PUBLIC
#        grpc_proto
    )
endif()
