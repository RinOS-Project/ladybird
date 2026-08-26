set(SOURCES
    Deflate.cpp
    GenericZlib.cpp
    Gzip.cpp
    PackBitsDecoder.cpp
    Zlib.cpp
)

ladybird_lib(LibCompress compress)
target_link_libraries(LibCompress PRIVATE LibCore LibCrypto)

if (NOT AK_OS_RINOS OR (RINOS_HELPER_SERVICES_ONLY AND NOT CMAKE_CROSSCOMPILING))
    # A native helper-only CTest is a POSIX executable, not a RinOS image.
    # Keep the target build on RinOS zlib, but resolve its real zlib calls on
    # the host so LibGfx/LibIPC consumers can be linked and executed.
    find_package(ZLIB REQUIRED)
    target_link_libraries(LibCompress PRIVATE ZLIB::ZLIB)
else()
    # RinOS: zlib is provided by libs/zlib, linked externally
    message(STATUS "RinOS policy: ZLIB provided by RinOS native zlib.")
endif()
