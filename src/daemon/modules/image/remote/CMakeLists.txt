# get current directory sources files
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} local_remote_srcs)

set(REMOTE_SRCS
    ${local_remote_srcs}
    PARENT_SCOPE
    )

set(REMOTE_INCS
    ${CMAKE_CURRENT_SOURCE_DIR}
        PARENT_SCOPE
    )

