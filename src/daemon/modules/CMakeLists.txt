# get current directory sources files

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} modules_top_srcs)
add_subdirectory(runtime)
add_subdirectory(image)
add_subdirectory(spec)
add_subdirectory(container)
add_subdirectory(log)
add_subdirectory(events)
add_subdirectory(events_sender)
add_subdirectory(service)
add_subdirectory(api)
add_subdirectory(volume)

set(local_modules_srcs
    ${modules_top_srcs}
    ${RUNTIME_SRCS}
    ${IMAGE_SRCS}
    ${SPEC_SRCS}
    ${MANAGER_SRCS}
    ${LOG_GATHER_SRCS}
    ${EVENTS_SRCS}
    ${EVENTS_SENDER_SRCS}
    ${SERVICE_SRCS}
    ${VOLUME_SRCS}
    )

set(local_modules_incs
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${RUNTIME_INCS}
    ${IMAGE_INCS}
    ${SPEC_INCS}
    ${MANAGER_INCS}
    ${LOG_GATHER_INCS}
    ${EVENTS_INCS}
    ${EVENTS_SENDER_INCS}
    ${SERVICE_INCS}
    ${MODULES_API_INCS}
    ${VOLUME_INCS}
    )

if (ENABLE_PLUGIN)
    add_subdirectory(plugin)
    list(APPEND local_modules_srcs
        ${PLUGIN_SRCS}
        )
    list(APPEND local_modules_incs
        ${PLUGIN_INCS}
        )
endif()

if (ENABLE_CDI)
    add_subdirectory(device)
    list(APPEND local_modules_srcs
        ${DEVICE_SRCS}
        )
    list(APPEND local_modules_incs
        ${DEVICE_INCS}
        )
endif()

set(MODULES_SRCS
    ${local_modules_srcs}
    PARENT_SCOPE
    )
set(MODULES_INCS
    ${local_modules_incs}
    PARENT_SCOPE
    )

if(ENABLE_NETWORK)
    add_subdirectory(network)
    set(DAEMON_NETWORK_SRCS
        ${NETWORK_SRCS}
        PARENT_SCOPE
        )
    set(DAEMON_NETWORK_INCS
        ${NETWORK_INCS}
        PARENT_SCOPE
        )
endif()


