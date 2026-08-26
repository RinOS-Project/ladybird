set(SOURCES
    Deflate.cpp
    GenericZlib.cpp
    Gzip.cpp
    PackBitsDecoder.cpp
    Zlib.cpp
)

ladybird_lib(LibCompress compress)
target_link_libraries(LibCompress PRIVATE LibCore LibCrypto)

if (NOT AK_OS_RINOS)
    find_package(ZLIB REQUIRED)
    target_link_libraries(LibCompress PRIVATE ZLIB::ZLIB)
else()
    # RinOS: zlib is provided by libs/zlib, linked externally
    message(STATUS "RinOS policy: ZLIB provided by RinOS native zlib.")
endif()
