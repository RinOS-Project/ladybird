set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

if (AK_OS_RINOS)
    set(SOURCES
        ConnectionFromClient.cpp
        RinHTTPTransport.cpp
        Request.cpp
        RequestPipe.cpp
        Resolver.cpp
        ResourceSubstitutionMap.cpp
    )
else()
    set(SOURCES
        ConnectionFromClient.cpp
        CURL.cpp
        Request.cpp
        RequestPipe.cpp
        Resolver.cpp
        ResourceSubstitutionMap.cpp
        WebSocketImplCurl.cpp
    )
endif()

set(GENERATED_SOURCES
    RequestClientEndpoint.h
    RequestServerEndpoint.h
)

set(RS_LIB_TYPE STATIC)
if (ANDROID)
    list(APPEND SOURCES
        ${LADYBIRD_SOURCE_DIR}/UI/Android/src/main/cpp/RequestServerService.cpp
        ${LADYBIRD_SOURCE_DIR}/UI/Android/src/main/cpp/LadybirdServiceBaseJNI.cpp
    )
    set(RS_LIB_TYPE SHARED)
endif()

add_library(requestserverservice ${RS_LIB_TYPE} ${SOURCES} ${GENERATED_SOURCES})
ladybird_generated_sources(requestserverservice)

find_package(PkgConfig)
if (NOT AK_OS_RINOS)
    find_package(CURL REQUIRED)
endif()

add_executable(RequestServer main.cpp)

target_include_directories(requestserverservice PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../..)
target_include_directories(requestserverservice PRIVATE ${LADYBIRD_SOURCE_DIR}/Services/)

target_link_libraries(RequestServer PRIVATE requestserverservice)
if (AK_OS_RINOS)
    target_link_libraries(requestserverservice PUBLIC LibCore LibDNS LibHTTP LibIPC LibMain LibRequests LibTLS LibWebSocket LibURL LibTextCodec)
else()
    target_link_libraries(requestserverservice PUBLIC LibCore LibDNS LibHTTP LibIPC LibMain LibRequests LibTLS LibWebSocket LibURL LibTextCodec CURL::libcurl)
endif()
if (RINOS_LEGACY_OPENSSL)
    target_link_libraries(requestserverservice PRIVATE OpenSSL::Crypto OpenSSL::SSL)
endif()

if (WIN32)
    lagom_windows_bin(RequestServer CONSOLE)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
    # Solaris has socket and networking related functions in two extra libraries
    target_link_libraries(requestserverservice PUBLIC nsl socket)
endif()
if (HAIKU)
    # Haiku has networking related functions in the network library
    target_link_libraries(RequestServer PRIVATE network)
endif()
if (ANDROID)
    # We need s_ladybird_resource_root from LibWebView
    target_link_libraries(requestserverservice PUBLIC LibWebView)
endif()
