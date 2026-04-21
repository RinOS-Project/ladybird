if (UNIX AND NOT APPLE)
    ladybird_test("TestTransportSocket.cpp" LibIPC LIBS LibGfx LibIPC)
endif()
