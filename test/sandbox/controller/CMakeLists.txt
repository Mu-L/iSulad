project(iSulad_UT)

list(APPEND grpc_sandbox_type_srcs ${CMAKE_BINARY_DIR}/grpc/src/api/services/cri/v1/api_v1.pb.cc)
list(APPEND grpc_sandbox_type_srcs ${CMAKE_BINARY_DIR}/grpc/src/api/services/cri/gogo.pb.cc)

if (ENABLE_SANDBOXER)
    add_subdirectory(sandboxer)
    add_subdirectory(manager)
endif()
add_subdirectory(shim)
