option(PACKAGE "set isulad package" ON)
if (PACKAGE STREQUAL "ON")
    set(ISULAD_PACKAGE "iSulad")
    message("${BoldBlue}PackageName: ${ISULAD_PACKAGE} ${ColourReset}")
endif()

option(VERSION "set isulad version" ON)
if (VERSION STREQUAL "ON")
    set(ISULAD_VERSION "2.1.5")
    message("${BoldBlue}Version: ${ISULAD_VERSION} ${ColourReset}")
endif()

message("${BoldGreen}---- Selected options begin ----${ColourReset}")

# build which type of lcr library
option(USESHARED "set type of libs, default is shared" ON)
if (USESHARED STREQUAL "ON")
    set(LIBTYPE "SHARED")
    message("${Green}--  Build shared library${ColourReset}")
    set(INSTALL_TYPE LIBRARY)
else ()
    set(LIBTYPE "STATIC")
    message("${Green}--  Build static library${ColourReset}")
    set(INSTALL_TYPE ARCHIVE)
    add_definitions(-DISULAD_STATIC_LIB)
    set(ISULAD_STATIC_LIB 1)
endif()

option(ENABLE_GRPC "Use grpc as connector" ON)
if (ENABLE_GRPC STREQUAL "ON")
    add_definitions(-DGRPC_CONNECTOR)
    set(GRPC_CONNECTOR 1)
    message("${Green}--  Use grpc connector${ColourReset}")
endif()

option(ENABLE_CRI_API_V1 "Enable CRI API V1" OFF)
if (ENABLE_CRI_API_V1 STREQUAL "ON")
    add_definitions(-DENABLE_CRI_API_V1)
    set(ENABLE_CRI_API_V1 1)
    message("${Green}--  Enable CRI API V1${ColourReset}")
endif()

option(ENABLE_CDI "Enable CDI" OFF)
if (ENABLE_CDI STREQUAL "ON")
    if (ENABLE_CRI_API_V1)
        add_definitions(-DENABLE_CDI)
        set(ENABLE_CDI 1)
        message("${Green}--  Enable CDI${ColourReset}")
    else()
        message("${Yellow}-- Can not enable CDI, CDI need enable CRI API V1 first ${ColourReset}")
    endif()
endif()

option(ENABLE_SANDBOXER "Enable sandbox API" OFF)
if (ENABLE_SANDBOXER STREQUAL "ON")
    add_definitions(-DENABLE_SANDBOXER)
    set(ENABLE_SANDBOXER 1)
    message("${Green}--  Enable sandbox API${ColourReset}")
endif()

option(ENABLE_OOM_MONITOR "Enable oom monitor" ON)
if (ENABLE_OOM_MONITOR STREQUAL "ON")
    add_definitions(-DENABLE_OOM_MONITOR)
    set(ENABLE_OOM_MONITOR 1)
    message("${Green}--  Enable oom monitor${ColourReset}")
endif()

option(ENABLE_NRI "Enable NRI API" OFF)
if (ENABLE_NRI STREQUAL "ON")
    if (ENABLE_CRI_API_V1)
        add_definitions(-DENABLE_NRI)
        set(ENABLE_NRI 1)
        message("${Green}--  Enable NRI API${ColourReset}")
    else()
        message("${Yellow}-- Can not enable NRI, NRI need enable CRI API V1 first ${ColourReset}")
    endif()
endif()

option(ENABLE_SYSTEMD_NOTIFY "Enable systemd notify" ON)
if (ENABLE_SYSTEMD_NOTIFY STREQUAL "ON")
    add_definitions(-DSYSTEMD_NOTIFY)
    set(SYSTEMD_NOTIFY 1)
    message("${Green}--  Enable systemd notify${ColourReset}")
endif()

option(ENABLE_OPENSSL_VERIFY "use ssl with connector" ON)
if (ENABLE_OPENSSL_VERIFY STREQUAL "ON")
    add_definitions(-DOPENSSL_VERIFY)
    set(OPENSSL_VERIFY 1)
    message("${Green}--  Enable ssl with connector${ColourReset}")
endif()

option(DEBUG "set isulad gcc option" ON)
if (DEBUG STREQUAL "ON")
    add_definitions("-g -O2")
endif()

option(GCOV "set isulad gcov option" OFF)
if (GCOV STREQUAL "ON")
    set(ISULAD_GCOV "ON")
    message("${Green}--  Enable GCOV${ColourReset}")
endif()
OPTION(ENABLE_UT "ut switch" OFF)
if (ENABLE_UT STREQUAL "ON")
    set(ENABLE_UT 1)
    message("${Green}--  Enable UT${ColourReset}")
endif()
OPTION(ENABLE_FUZZ "fuzz switch" OFF)
if (ENABLE_FUZZ STREQUAL "ON")
    set(ENABLE_FUZZ 1)
    message("${Green}--  Enable FUZZ${ColourReset}")
endif()

# set OCI image server type
option(DISABLE_OCI "disable oci image" OFF)
if (DISABLE_OCI STREQUAL "ON")
    message("${Green}--  Disable OCI image${ColourReset}")
else()
    add_definitions(-DENABLE_OCI_IMAGE=1)
    set(ENABLE_OCI_IMAGE 2)
    message("${Green}--  Enable OCI image${ColourReset}")
endif()

option(ENABLE_EMBEDDED "enable embedded image" OFF)
if (ENABLE_EMBEDDED STREQUAL "ON")
    add_definitions(-DENABLE_EMBEDDED_IMAGE=1)
    set(ENABLE_EMBEDDED_IMAGE 1)
    message("${Green}--  Enable embedded image${ColourReset}")
endif()

option(ENABLE_SELINUX "enable isulad daemon selinux option" ON)
if (ENABLE_SELINUX STREQUAL "ON")
    add_definitions(-DENABLE_SELINUX=1)
    set(ENABLE_SELINUX 1)
    message("${Green}--  Enable selinux${ColourReset}")
endif()

option(ENABLE_GRPC_REMOTE_CONNECT "enable gRPC remote connect" OFF)
if (ENABLE_GRPC_REMOTE_CONNECT STREQUAL "ON")
	add_definitions(-DENABLE_GRPC_REMOTE_CONNECT=1)
	set(ENABLE_GRPC_REMOTE_CONNECT 1)
    message("${Green}--  enable gRPC remote connect${ColourReset}")
endif()

option(ENABLE_SHIM_V2 "enable shim v2 runtime" OFF)
if (ENABLE_SHIM_V2 STREQUAL "ON")
	add_definitions(-DENABLE_SHIM_V2=1)
	set(ENABLE_SHIM_V2 1)
    message("${Green}--  Enable shim v2 runtime${ColourReset}")
endif()

option(ENABLE_METRICS "enable metrics for CRI" OFF)
if (ENABLE_METRICS STREQUAL "ON")
    add_definitions(-DENABLE_METRICS)
    set(ENABLE_METRICS 1)
    message("${Green}--  Enable metrics for CRI${ColourReset}")
endif()

option(ENABLE_NATIVE_NETWORK "Enable native network" ON)
if (ENABLE_NATIVE_NETWORK STREQUAL "ON")
    add_definitions(-DENABLE_NATIVE_NETWORK)
    set(ENABLE_NATIVE_NETWORK 1)
    message("${Green}--  Enable native network${ColourReset}")
endif()

if (ENABLE_NATIVE_NETWORK OR ENABLE_GRPC)
    add_definitions(-DENABLE_NETWORK)
    set(ENABLE_NETWORK 1)
endif()

option(ENABLE_PLUGIN "enable plugin module" OFF)
if (ENABLE_PLUGIN STREQUAL "ON")
	add_definitions(-DENABLE_PLUGIN=1)
	set(ENABLE_PLUGIN 1)
    message("${Green}--  Enable plugin module${ColourReset}")
endif()

option(ENABLE_LOGIN_PASSWORD_OPTION "enable login password option" ON)
if (ENABLE_LOGIN_PASSWORD_OPTION STREQUAL "ON")
	add_definitions(-DENABLE_LOGIN_PASSWORD_OPTION=1)
	set(ENABLE_LOGIN_PASSWORD_OPTION 1)
    message("${Green}--  Enable login password option${ColourReset}")
endif()

option(EANBLE_IMAGE_LIBARAY "create libisulad_image.so" OFF)
if (EANBLE_IMAGE_LIBARAY STREQUAL "ON")
    add_definitions(-DEANBLE_IMAGE_LIBARAY)
    set(EANBLE_IMAGE_LIBARAY 1)
endif()

option(ENABLE_GVISOR "enable gvisor" OFF)
if (ENABLE_GVISOR)
    add_definitions(-DENABLE_GVISOR)
    message("${Green}--  Enable runtime gvisor${ColourReset}")
endif()

option(ENABLE_USERNS_REMAP "enable userns remap" OFF)
if (ENABLE_USERNS_REMAP)
    add_definitions(-DENABLE_USERNS_REMAP)
    message("${Green}--  Enable userns remap${ColourReset}")
endif()

option(ENABLE_SUP_GROUPS "enable sup groups" OFF)
if (ENABLE_SUP_GROUPS)
    add_definitions(-DENABLE_SUP_GROUPS)
    message("${Green}--  Enable sup groups${ColourReset}")
endif()

option(DISABLE_CLEANUP "disable cleanup module" OFF)
if (DISABLE_CLEANUP STREQUAL "ON")
    add_definitions(-DDISABLE_CLEANUP)
    message("${Green}--  Disable cleanup module")
endif()

option(ENABLE_REMOTE_LAYER_STORE "enable remote layer store" OFF)
if (ENABLE_REMOTE_LAYER_STORE STREQUAL "ON")
    add_definitions(-DENABLE_REMOTE_LAYER_STORE)
    message("${Green}--  Enable remote layer store")
endif()

option(MUSL "available for musl" OFF)
if (MUSL)
    add_definitions(-D__MUSL__)
    message("${Green}--  Available for MUSL${ColourReset}")
endif()

option(ANDROID "available for android" OFF)
if (ANDROID)
    add_definitions(-D__ANDROID__)
    message("${Green}--  Available for ANDROID${ColourReset}")
endif()

if (NOT RUNPATH)
    set(RUNPATH "/var/run")
endif()
add_definitions(-DRUNPATH="${RUNPATH}")
message("${Green}--  RUNPATH=${RUNPATH}${ColourReset}")

if (NOT SYSCONFDIR_PREFIX)
    set(SYSCONFDIR_PREFIX "")
endif()
add_definitions(-DSYSCONFDIR_PREFIX="${SYSCONFDIR_PREFIX}")
message("${Green}--  SYSCONFDIR_PREFIX=${SYSCONFDIR_PREFIX}${ColourReset}")

option(ENABLE_IMAGE_SEARCH "Enable isula search" ON)
if (ENABLE_IMAGE_SEARCH STREQUAL "ON")
    add_definitions(-DENABLE_IMAGE_SEARCH)
    set(ENABLE_IMAGE_SEARCH 1)
    message("${Green}--  Enable isula search${ColourReset}")
endif()

message("${BoldGreen}---- Selected options end ----${ColourReset}")
