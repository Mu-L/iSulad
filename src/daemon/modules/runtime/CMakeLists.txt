# get current directory sources files

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} runtime_top_srcs)
add_subdirectory(engines)
add_subdirectory(isula)

set(local_runtime_srcs
    ${runtime_top_srcs}
    ${ENGINES_SRCS}
    ${ISULA_SRCS}
    )

set(local_runtime_incs
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ENGINES_INCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/isula
    )

set(RUNTIME_SRCS
    ${local_runtime_srcs}
    PARENT_SCOPE
    )
set(RUNTIME_INCS
    ${local_runtime_incs}
    PARENT_SCOPE
    )
