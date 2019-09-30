# build which type of lcr library
option(USESHARED "set type of libs, default is shared" ON)
if (USESHARED STREQUAL "ON")
    set(LIBTYPE "SHARED")
    message("--  Build shared library")
else ()
    set(LIBTYPE "STATIC")
    message("--  Build static library")
endif()

option(ENABLE_GRPC "use grpc as connector" ON)
if (ENABLE_GRPC STREQUAL "ON")
    add_definitions(-DGRPC_CONNECTOR)
    set(GRPC_CONNECTOR 1)
endif()

option(ENABLE_SYSTEMD_NOTIFY "enable systemd notify" ON)
if (ENABLE_SYSTEMD_NOTIFY STREQUAL "ON")
    add_definitions(-DSYSTEMD_NOTIFY)
    set(SYSTEMD_NOTIFY 1)
endif()

option(ENABLE_OPENSSL_VERIFY "use ssl with connector" ON)
if (ENABLE_OPENSSL_VERIFY STREQUAL "ON")
    add_definitions(-DOPENSSL_VERIFY)
    set(OPENSSL_VERIFY 1)
endif()

option(PACKAGE "set lcrd package" ON)
if (PACKAGE STREQUAL "ON")
    set(LCRD_PACKAGE "iSulad")
endif()

option(VERSION "set lcrd version" ON)
if (VERSION STREQUAL "ON")
    set(LCRD_VERSION "1.0.31")
endif()

option(DEBUG "set lcrd gcc option" ON)
if (DEBUG STREQUAL "ON")
    add_definitions("-g -O2")
endif()

option(GCOV "set lcrd gcov option" OFF)
if (GCOV STREQUAL "ON")
    set(ISULAD_GCOV "ON")
endif()
