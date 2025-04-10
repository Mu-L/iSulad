# get current directory sources files

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} image_top_srcs)
add_subdirectory(external)

set(local_image_srcs
    ${image_top_srcs}
    ${EXTERNAL_SRCS}
    )

set(local_image_incs
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    )

if (ENABLE_OCI_IMAGE)
    add_subdirectory(oci)
    list(APPEND local_image_srcs
        ${OCI_SRCS}
        )
    list(APPEND local_image_incs
        ${OCI_INCS}
        )
endif()

if (ENABLE_EMBEDDED_IMAGE)
    add_subdirectory(embedded)
    list(APPEND local_image_srcs
        ${EMBEDDED_SRCS}
        )
    list(APPEND local_image_incs
        ${EMBEDDED_INCS}
        )
endif()

if (ENABLE_REMOTE_IMAGE)
    add_subdirectory(remote)
    list(APPEND local_image_srcs
        ${REMOTE_SRCS}
        )
    list(APPEND local_image_incs
        ${REMOTE_INCS}
        )
endif()

set(IMAGE_SRCS
    ${local_image_srcs}
    PARENT_SCOPE
    )
set(IMAGE_INCS
    ${local_image_incs}
    PARENT_SCOPE
    )

if (EANBLE_IMAGE_LIBARAY)
# set sources and headers for libisulad_img
set(LIB_ISULAD_IMG libisulad_img)

set(LIB_ISULAD_IMG_SRCS
    ${local_image_srcs}
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_regex.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_file.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_verify.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_array.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_convert.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_string.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_base64.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_timestamp.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_fs.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/utils_aes.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/util_atomic.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/filters.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/path.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/map/map.c
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/map/rb_tree.c
    ${CMAKE_SOURCE_DIR}/src/utils/sha256/sha256.c
    ${CMAKE_SOURCE_DIR}/src/utils/buffer/buffer.c
    ${CMAKE_SOURCE_DIR}/src/daemon/common/err_msg.c
    ${CMAKE_SOURCE_DIR}/src/daemon/common/sysinfo.c
    ${CMAKE_SOURCE_DIR}/src/daemon/common/cgroup/cgroup.c
    ${CMAKE_SOURCE_DIR}/src/daemon/common/cgroup/cgroup_v1.c
    ${CMAKE_SOURCE_DIR}/src/daemon/common/cgroup/cgroup_v2.c
    ${CMAKE_SOURCE_DIR}/src/utils/tar/util_gzip.c
    ${CMAKE_SOURCE_DIR}/src/daemon/config/isulad_config.c
    ${CMAKE_SOURCE_DIR}/src/daemon/config/daemon_arguments.c
    )

if (ENABLE_SELINUX)
    list(APPEND LIB_ISULAD_IMG_SRCS
        ${CMAKE_SOURCE_DIR}/src/daemon/common/selinux_label.c
        )
endif()

if (ENABLE_OCI_IMAGE)
    list(APPEND LIB_ISULAD_IMG_SRCS
        ${CMAKE_SOURCE_DIR}/src/utils/tar/isulad_tar.c
        ${CMAKE_SOURCE_DIR}/src/utils/tar/util_archive.c
        )
endif()

add_library(${LIB_ISULAD_IMG} ${LIBTYPE}
    ${LIB_ISULAD_IMG_SRCS}
    )
set_target_properties(${LIB_ISULAD_IMG} PROPERTIES LINKER_LANGUAGE "C")

target_include_directories(${LIB_ISULAD_IMG} PUBLIC
    ${local_image_incs}
    ${CMAKE_SOURCE_DIR}/src/daemon/modules/api
    ${CMAKE_SOURCE_DIR}/src/utils/sha256
    ${CMAKE_SOURCE_DIR}/src/http
    ${CMAKE_SOURCE_DIR}/src/buffer
    ${CMAKE_SOURCE_DIR}/src/utils/tar
    ${CMAKE_SOURCE_DIR}/src/daemon/config
    ${CMAKE_SOURCE_DIR}/src/daemon/common
    ${CMAKE_SOURCE_DIR}/src/daemon/common/cgroup
    ${CMAKE_SOURCE_DIR}/src/daemon/modules/spec/
    ${CMAKE_SOURCE_DIR}/src/utils/cutils
    ${CMAKE_SOURCE_DIR}/src/utils/cutils/map
    ${CHECKED_INCLUDE_DIRS}
    )

# set libisulad_img FLAGS
set_target_properties(${LIB_ISULAD_IMG} PROPERTIES PREFIX "")
target_link_libraries(${LIB_ISULAD_IMG}
    ${CMAKE_THREAD_LIBS_INIT}
    ${ISULA_LIBUTILS_LIBRARY}
    ${DEVMAPPER_LIBRARY}
    ${LIBTAR_LIBRARY}
    ${CRYPTO_LIBRARY}
    ${ZLIB_LIBRARY}
    libhttpclient)

if ((NOT ANDROID) AND (NOT MUSL))
    target_link_libraries(${LIB_ISULAD_IMG} -lpthread)
endif()

if (ENABLE_OCI_IMAGE)
    target_link_libraries(${LIB_ISULAD_IMG}
        ${LIBARCHIVE_LIBRARY}
        )
endif()

if (ENABLE_SELINUX)
    target_link_libraries(${LIB_ISULAD_IMG}
        ${SELINUX_LIBRARY}
        )
endif()

target_compile_definitions(${LIB_ISULAD_IMG} PRIVATE LIB_ISULAD_IMG_SO)

install(TARGETS ${LIB_ISULAD_IMG}
    ${INSTALL_TYPE} DESTINATION ${LIB_INSTALL_DIR_DEFAULT} PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)

endif() # EANBLE_IMAGE_LIBARAY
