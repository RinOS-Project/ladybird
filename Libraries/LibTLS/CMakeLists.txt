add_cxx_compile_options(-Wvla)

set(SOURCES
    TLSv12.cpp
)

ladybird_lib(LibTLS tls)
target_link_libraries(LibTLS PRIVATE LibCore LibCrypto LibFileSystem)

if (AK_OS_RINOS)
    # rintls include paths and link for RinOS builds
    target_include_directories(LibTLS PRIVATE ${CMAKE_SOURCE_DIR}/../../rintls)
    target_compile_definitions(LibTLS PRIVATE AK_OS_RINOS=1)
elseif (RINOS_LEGACY_OPENSSL)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(LibTLS PUBLIC OpenSSL::SSL)
endif()
