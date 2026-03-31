cmake_minimum_required(VERSION 3.25)

if (VCPKG_TARGET_ANDROID)
    # If we are building for Android, we must load vcpkg_android.cmake before the project() declaration.
    # This ensures that the CMAKE_TOOLCHAIN_FILE is set correctly.
    # (we cannot set CMAKE_TOOLCHAIN_FILE from Gradle, unfortunately, so this is the only place we can do it.)
    include("UI/Android/vcpkg_android.cmake")
endif()

# vcpkg flags depend on what linker we are using
include("Meta/CMake/use_linker.cmake")

if (APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0)
endif()

# Pass additional information to vcpkg toolchain files if we are using vcpkg.
if (CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake$")
    set(CMAKE_PROJECT_ladybird_INCLUDE_BEFORE "Meta/CMake/vcpkg/generate_vcpkg_toolchain_variables.cmake")
endif()

if (APPLE AND NOT CMAKE_OSX_SYSROOT)
    set(CMAKE_OSX_SYSROOT macosx)
endif()

project(ladybird
        VERSION 0.1.0
        LANGUAGES C CXX
        DESCRIPTION "Ladybird Web Browser"
        HOMEPAGE_URL "https://ladybird.org"
)

if (ANDROID OR IOS)
    set(BUILD_SHARED_LIBS OFF)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LADYBIRD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LADYBIRD_SOURCE_DIR}/Meta/CMake")

include(UI/cmake/EnableLagom.cmake)
include(lagom_options NO_POLICY_SCOPE)
include(lagom_compile_options)

if (AK_OS_RINOS)
    add_compile_definitions(AK_OS_RINOS=1)

    add_library(RinTLSShim STATIC
        ${LADYBIRD_SOURCE_DIR}/../rintls/rintls.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/aes.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/bignum.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/ecdh.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/hmac.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rsa.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/sha1.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/sha256.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/tls/handshake.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/tls/record.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/x509/cert.c
    )
    target_include_directories(RinTLSShim PRIVATE
        ${LADYBIRD_SOURCE_DIR}/../rintls
        ${LADYBIRD_SOURCE_DIR}/../libc
    )
    if (CMAKE_CROSSCOMPILING)
        target_compile_definitions(RinTLSShim PRIVATE RIN_USERSPACE=1 RIN_FREESTANDING=0)
    else()
        target_compile_definitions(RinTLSShim PRIVATE RIN_USERSPACE=1 RIN_FREESTANDING=0 RINTLS_HOST_LIBC=1)
    endif()
    target_compile_options(RinTLSShim PRIVATE
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wno-unused-variable
        -Wno-sign-compare
        -Wno-missing-prototypes
        -Wno-implicit-function-declaration
    )
endif()

if (HAIKU)
    # Haiku needs some extra compile definitions to make important stuff in its headers available.
    add_compile_definitions(_DEFAULT_SOURCE)
    add_compile_definitions(_GNU_SOURCE)
    add_compile_definitions(__USE_GNU)
endif()

add_cxx_compile_options(-Wno-expansion-to-defined)
add_cxx_compile_options(-Wno-user-defined-literals)

if (ANDROID AND ENABLE_QT)
    message(STATUS "Disabling Qt for Android")
    set(ENABLE_QT OFF CACHE BOOL "" FORCE)
endif()

if (ENABLE_QT AND ENABLE_GUI_TARGETS)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
endif()

if (RINOS_LEGACY_OPENSSL)
    # Legacy compatibility path; RinOS phase plan prefers rintls.
    message(STATUS "RinOS legacy mode: OpenSSL is enabled.")
    find_package(OpenSSL REQUIRED)
else()
    message(STATUS "RinOS policy: OpenSSL detection is disabled; legacy OpenSSL linkage should be replaced by rintls.")
endif()

include(CTest) # for BUILD_TESTING option, default ON

if (ENABLE_GUI_TARGETS)
    add_subdirectory(Services)
    add_subdirectory(UI)
endif()

add_custom_target(lint-shell-scripts
    COMMAND "${ladybird_SOURCE_DIR}/Meta/lint-shell-scripts.sh"
    USES_TERMINAL
)

find_package(Python3 COMPONENTS Interpreter)
if (Python3_FOUND)
    add_custom_target(check-style
        COMMAND ${Python3_EXECUTABLE} "${ladybird_SOURCE_DIR}/Meta/check-style.py"
        USES_TERMINAL
    )
endif()
