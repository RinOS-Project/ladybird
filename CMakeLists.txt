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

option(RINOS_HELPER_SERVICES_ONLY "Build helper services without the desktop Ladybird frontend" OFF)

if (ANDROID OR IOS)
    set(BUILD_SHARED_LIBS OFF)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LADYBIRD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LADYBIRD_SOURCE_DIR}/Meta/CMake")

# Enable the platform branch before EnableLagom creates AK and the other
# foundational targets.  Applying this only after that include leaves those
# targets on their host dependency path (for example fast_float), even though
# their LibWeb consumer is configured for RinOS.
if (AK_OS_RINOS)
    add_compile_definitions(AK_OS_RINOS=1)
endif()

include(UI/cmake/EnableLagom.cmake)
include(lagom_options NO_POLICY_SCOPE)
include(lagom_compile_options)

if (AK_OS_RINOS)
    # rintls owns the public provider boundary.  Build only the libecc
    # primitives required by WebCrypto instead of importing its broad default
    # algorithm set.  The provider's entropy callback is patched to return to
    # rintls, so a CSPRNG failure cannot fall through to a second RNG.
    file(STRINGS "${LADYBIRD_SOURCE_DIR}/../libecc/CMakeLists.txt" RINTLS_LIBECC_OPTION_LINES
        REGEX "^option\\(OPTION_SUPPORT")
    foreach(rintls_libecc_option_line IN LISTS RINTLS_LIBECC_OPTION_LINES)
        string(REGEX MATCH "^option\\((OPTION_SUPPORT_[A-Z0-9_]+)" rintls_libecc_option
            "${rintls_libecc_option_line}")
        if (CMAKE_MATCH_1)
            set(${CMAKE_MATCH_1} OFF CACHE BOOL "RinOS reduced libecc profile" FORCE)
        endif()
    endforeach()
    foreach(rintls_libecc_required_option IN ITEMS
        OPTION_SUPPORT_CURVE_SECP256R1
        OPTION_SUPPORT_CURVE_SECP384R1
        OPTION_SUPPORT_CURVE_SECP521R1
        OPTION_SUPPORT_CURVE_WEI25519
        OPTION_SUPPORT_CURVE_WEI448
        OPTION_SUPPORT_HASH_SHA256
        OPTION_SUPPORT_HASH_SHA384
        OPTION_SUPPORT_HASH_SHA512
        OPTION_SUPPORT_HASH_SHAKE256
        OPTION_SUPPORT_SIG_ECDSA
        OPTION_SUPPORT_SIG_EDDSA25519
        OPTION_SUPPORT_SIG_EDDSA448
        OPTION_SUPPORT_ECDH_ECCCDH
        OPTION_SUPPORT_ECDH_X25519
        OPTION_SUPPORT_ECDH_X448)
        set(${rintls_libecc_required_option} ON CACHE BOOL "RinOS required libecc primitive" FORCE)
    endforeach()
    set(OPTION_BUILD_SELF_TEST OFF CACHE BOOL "RinOS builds provider tests separately" FORCE)
    set(OPTION_BUILD_UTILITY OFF CACHE BOOL "RinOS does not ship libecc tools" FORCE)
    set(PLATFORM_INCLUDE_STDLIB ON CACHE BOOL "RinOS userspace libc supplies libecc basics" FORCE)
    set(OPTION_SPECIFY_CONFIG ON CACHE BOOL "RinOS uses a reduced libecc profile" FORCE)
    # libecc calls the rintls-owned entropy callback.  It must therefore be a
    # static implementation detail of RinTLSShim; a shared provider would
    # require its consumer to satisfy get_random at provider-load time.
    if (DEFINED BUILD_SHARED_LIBS)
        set(RINTLS_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
        set(RINTLS_RESTORE_BUILD_SHARED_LIBS ON)
    endif()
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(${LADYBIRD_SOURCE_DIR}/../libecc
        ${CMAKE_BINARY_DIR}/rintls-libecc EXCLUDE_FROM_ALL)
    if (RINTLS_RESTORE_BUILD_SHARED_LIBS)
        set(BUILD_SHARED_LIBS "${RINTLS_SAVED_BUILD_SHARED_LIBS}")
    else()
        unset(BUILD_SHARED_LIBS)
    endif()
    if (TARGET sign)
        set(RINTLS_LIBECC_TARGET sign)
    elseif (TARGET sign_dyn)
        set(RINTLS_LIBECC_TARGET sign_dyn)
    else()
        message(FATAL_ERROR "rintls requires the libecc signing/provider target")
    endif()
    target_compile_definitions(${RINTLS_LIBECC_TARGET} PRIVATE
        RINTLS_USE_PLATFORM_RANDOM=1
        # get_random is an internal libecc hook.  Give it a provider-specific
        # ELF name so it cannot interpose with a host runtime symbol.
        get_random=rintls_libecc_provider_random
        # libecc's SHA-2 implementation is private to its provider archive.
        # rintls has an independent SHA-2 implementation for TLS and exports
        # the same upstream generic names, so namespace libecc's symbols to
        # prevent a final static link from selecting two definitions.
        sha256=libecc_sha256
        sha256_init=libecc_sha256_init
        sha256_update=libecc_sha256_update
        sha256_final=libecc_sha256_final
        sha384=libecc_sha384
        sha384_init=libecc_sha384_init
        sha384_update=libecc_sha384_update
        sha384_final=libecc_sha384_final
        sha512=libecc_sha512
        sha512_init=libecc_sha512_init
        sha512_update=libecc_sha512_update
        sha512_final=libecc_sha512_final)

    set(RINTLS_LIBECC_COMPILE_DEFINITIONS
        WITH_STDLIB
        WITH_LIBECC_CONFIG_OVERRIDE
        WITH_CURVE_SECP256R1
        WITH_CURVE_SECP384R1
        WITH_CURVE_SECP521R1
        WITH_CURVE_WEI25519
        WITH_CURVE_WEI448
        WITH_HASH_SHA256
        WITH_HASH_SHA384
        WITH_HASH_SHA512
        WITH_HASH_SHAKE256
        WITH_SIG_ECDSA
        WITH_SIG_EDDSA25519
        WITH_SIG_EDDSA448
        WITH_ECCCDH
        WITH_X25519
        WITH_X448)
    # The declaration-only side of libecc's nn consistency check is compiled
    # into RinTLSShim. The defining nn.c object lives in libsign.a, so both
    # archives must receive the exact same reduced profile. Relying only on
    # libecc's directory-wide add_definitions() leaves an already configured
    # archive vulnerable to a stale/default MAX_DIGEST_SIZE ABI.
    target_compile_definitions(${RINTLS_LIBECC_TARGET} PRIVATE
        ${RINTLS_LIBECC_COMPILE_DEFINITIONS})

    # BearSSL provides the allocation-free RSA primitive used only behind the
    # rintls boundary. Its complete, pinned source tree is archived so source
    # selection never depends on a host-installed crypto package.
    file(GLOB RINTLS_BEARSSL_I31_SOURCES CONFIGURE_DEPENDS
        "${LADYBIRD_SOURCE_DIR}/../bearssl/src/int/i31_*.c")
    set(RINTLS_BEARSSL_SOURCES
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/codec/enc32be.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/codec/dec32be.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/codec/dec64be.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/codec/enc64be.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/codec/ccopy.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/hash/dig_size.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/hash/mgf1.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/hash/sha1.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/hash/sha2big.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/hash/sha2small.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/mac/hmac.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rand/hmac_drbg.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_keygen.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_keygen_bounded.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_keygen_inner.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_modulus.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_oaep_decrypt.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_oaep_encrypt.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_pkcs1_sign.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_pkcs1_vrfy.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_priv.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_privexp.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_pub.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_i31_pubexp.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_oaep_pad.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_oaep_unpad.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_pkcs1_sig_pad.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/rsa/rsa_pkcs1_sig_unpad.c
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src/int/i32_div32.c
        ${RINTLS_BEARSSL_I31_SOURCES})
    add_library(RinTLSBearSSL STATIC ${RINTLS_BEARSSL_SOURCES})
    target_include_directories(RinTLSBearSSL PRIVATE
        ${LADYBIRD_SOURCE_DIR}/../bearssl/inc
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src)
    target_compile_options(RinTLSBearSSL PRIVATE
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-sign-compare
        -Wno-implicit-fallthrough)

    add_library(RinTLSShim STATIC
        ${LADYBIRD_SOURCE_DIR}/../rintls/rintls.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/aes.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/bignum.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/ecdh.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/hmac.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/modern.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/pqc.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rsa.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rsa_webcrypto.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mlkem512.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mlkem768.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mlkem1024.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mldsa44.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mldsa65.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mldsa87.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/sha1.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/sha256.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/tls/handshake.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/tls/record.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/x509/cert.c
    )
    target_include_directories(RinTLSShim PRIVATE
        ${LADYBIRD_SOURCE_DIR}/../rintls
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto
        ${LADYBIRD_SOURCE_DIR}/../libecc/include
        ${LADYBIRD_SOURCE_DIR}/../bearssl/inc
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src
    )
    target_compile_definitions(RinTLSShim PRIVATE ${RINTLS_LIBECC_COMPILE_DEFINITIONS})
    target_link_libraries(RinTLSShim PRIVATE RinTLSBearSSL ${RINTLS_LIBECC_TARGET})
    if (CMAKE_CROSSCOMPILING)
        target_include_directories(RinTLSShim PRIVATE
            ${LADYBIRD_SOURCE_DIR}/../libc)
        target_compile_definitions(RinTLSShim PRIVATE RIN_USERSPACE=1)
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
    # LibCrypto publishes this dependency on native RinOS builds.  Export its
    # two static archives together so a host configuration remains valid and
    # installed consumers do not silently lose the crypto provider.
    if (NOT CMAKE_CROSSCOMPILING)
        set_target_properties(${RINTLS_LIBECC_TARGET} PROPERTIES
            EXPORT_NAME RinTLSLibECC)
        install(TARGETS RinTLSShim RinTLSBearSSL ${RINTLS_LIBECC_TARGET} EXPORT LagomTargets
            ARCHIVE COMPONENT Lagom_Development)
    endif()
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

if (AK_OS_RINOS AND BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    # Exercise the provider archive actually shipped through RinTLSShim.  This
    # cannot run under the bare-metal cross build, so it is a native CTest.
    add_executable(test-rintls-modern-backend
        ${LADYBIRD_SOURCE_DIR}/../rintls/tests/modern_backend_test.c)
    target_include_directories(test-rintls-modern-backend PRIVATE
        ${LADYBIRD_SOURCE_DIR}/../rintls
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto)
    target_compile_definitions(test-rintls-modern-backend PRIVATE
        RIN_USERSPACE=1 RIN_FREESTANDING=0 RINTLS_HOST_LIBC=1)
    target_link_libraries(test-rintls-modern-backend PRIVATE
        RinTLSShim ${RINTLS_LIBECC_TARGET})
    # The native test combines the freestanding-provider archive with the
    # host dynamic loader.  Resolve its PLT at load time, as production RinOS
    # static linking does, rather than relying on lazy binding at this seam.
    target_link_options(test-rintls-modern-backend PRIVATE -Wl,-z,now)
    add_test(NAME rintls-modern-backend COMMAND test-rintls-modern-backend)

    # This separate target deliberately compiles the provider boundary with
    # RIN_FREESTANDING=1. Its test-local rin_get_random_bytes() is therefore
    # the only entropy source, allowing every randomized API to prove that a
    # source failure clears its output instead of publishing partial material.
    add_executable(test-rintls-modern-random-failclose
        ${LADYBIRD_SOURCE_DIR}/../rintls/tests/modern_backend_random_failclose_test.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/modern.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/pqc.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mlkem512.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mlkem768.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mlkem1024.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mldsa44.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mldsa65.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rintls_mldsa87.c)
    target_include_directories(test-rintls-modern-random-failclose PRIVATE
        ${LADYBIRD_SOURCE_DIR}/../rintls
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto
        ${LADYBIRD_SOURCE_DIR}/../libecc/include)
    target_compile_definitions(test-rintls-modern-random-failclose PRIVATE
        RIN_FREESTANDING=1 RINTLS_TEST_HOST_TYPES=1
        ${RINTLS_LIBECC_COMPILE_DEFINITIONS})
    target_compile_options(test-rintls-modern-random-failclose PRIVATE
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wno-unused-variable
        -Wno-sign-compare
        -Wno-missing-prototypes
        -Wno-implicit-function-declaration)
    target_link_libraries(test-rintls-modern-random-failclose PRIVATE
        ${RINTLS_LIBECC_TARGET})
    target_link_options(test-rintls-modern-random-failclose PRIVATE -Wl,-z,now)
    add_test(NAME rintls-modern-random-failclose
        COMMAND test-rintls-modern-random-failclose)

    # Keygen and OAEP encryption are the randomized RSA operations. Compile
    # the boundary freestanding with an injected source so an entropy failure
    # cannot publish a key or ciphertext. The same target round-trips OAEP and
    # checks PKCS#1 v1.5 signature verification through the shipped provider.
    add_executable(test-rintls-rsa-webcrypto
        ${LADYBIRD_SOURCE_DIR}/../rintls/tests/rsa_webcrypto_test.c
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto/rsa_webcrypto.c)
    target_include_directories(test-rintls-rsa-webcrypto PRIVATE
        ${LADYBIRD_SOURCE_DIR}/../rintls
        ${LADYBIRD_SOURCE_DIR}/../rintls/crypto
        ${LADYBIRD_SOURCE_DIR}/../bearssl/inc
        ${LADYBIRD_SOURCE_DIR}/../bearssl/src)
    target_compile_definitions(test-rintls-rsa-webcrypto PRIVATE
        RIN_FREESTANDING=1 RINTLS_TEST_HOST_TYPES=1)
    target_compile_options(test-rintls-rsa-webcrypto PRIVATE
        -Wno-unused-parameter
        -Wno-missing-prototypes)
    target_link_libraries(test-rintls-rsa-webcrypto PRIVATE RinTLSBearSSL)
    target_link_options(test-rintls-rsa-webcrypto PRIVATE -Wl,-z,now)
    add_test(NAME rintls-rsa-webcrypto COMMAND test-rintls-rsa-webcrypto)
endif()

if (ENABLE_GUI_TARGETS)
    add_subdirectory(Services)
    if (RINOS_HELPER_SERVICES_ONLY)
        message(STATUS "RinOS helper-services-only build enabled; skipping Ladybird UI frontend.")
    else()
        add_subdirectory(UI)
    endif()
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
