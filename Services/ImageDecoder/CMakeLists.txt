set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

set(SOURCES
    ConnectionFromClient.cpp
)

if (AK_OS_RINOS)
    # The Ladybird ImageDecoder endpoint is a private RinOS adapter.  Keep
    # the authenticated client and service-slot lookup out of public SDK
    # targets and out of non-RinOS builds.
    list(APPEND SOURCES
        ${LADYBIRD_SOURCE_DIR}/../../src/services/imagedecoder/rin_image_decoder_service_client.c
        ${LADYBIRD_SOURCE_DIR}/../../src/services/imagedecoder/rin_image_decoder_service_client_posix.c
        ${LADYBIRD_SOURCE_DIR}/../../src/services/imagedecoder/rin_image_decoder_service_transport.c
        ${LADYBIRD_SOURCE_DIR}/../../public-base/libs/rinruntime/src/service.c
    )
endif()

if (ANDROID)
    add_library(imagedecoderservice SHARED
        ${LADYBIRD_SOURCE_DIR}/UI/Android/src/main/cpp/ImageDecoderService.cpp
        ${LADYBIRD_SOURCE_DIR}/UI/Android/src/main/cpp/LadybirdServiceBaseJNI.cpp
        ${SOURCES}
    )
    target_link_libraries(imagedecoderservice PRIVATE LibWebView)
else()
    add_library(imagedecoderservice STATIC ${SOURCES})
endif()

add_executable(ImageDecoder main.cpp)

target_include_directories(imagedecoderservice PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../..)
target_include_directories(imagedecoderservice PRIVATE ${LADYBIRD_SOURCE_DIR}/Services/)
if (AK_OS_RINOS)
    target_include_directories(imagedecoderservice PRIVATE
        ${LADYBIRD_SOURCE_DIR}/../../public-base/RinOS-SDK/include)
endif()

target_link_libraries(ImageDecoder PRIVATE imagedecoderservice LibCore LibMain LibThreading)
target_link_libraries(imagedecoderservice PRIVATE LibCore LibGfx LibIPC LibImageDecoderClient LibMain LibThreading)

if (WIN32)
    lagom_windows_bin(ImageDecoder CONSOLE)
endif()
