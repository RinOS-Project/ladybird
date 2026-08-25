set(TEST_SOURCES
    TestCSSIDSpeed.cpp
    TestContentFilter.cpp
    TestControlMessageQueue.cpp
    TestCSSInheritedProperty.cpp
    TestCSSPixels.cpp
    TestCSSSyntaxParser.cpp
    TestCSSTokenStream.cpp
    TestFetchFilteredResponse.cpp
    TestFetchURL.cpp
    TestHTMLTokenizer.cpp
    TestMicrosyntax.cpp
    TestMimeSniff.cpp
    TestNumbers.cpp
    TestSourceHighlighter.cpp
    TestStrings.cpp
)

foreach(source IN LISTS TEST_SOURCES)
    ladybird_test("${source}" LibWeb LIBS LibWeb)
endforeach()

target_link_libraries(TestContentFilter PRIVATE LibURL)
target_link_libraries(TestFetchURL PRIVATE LibURL)
target_link_libraries(TestSourceHighlighter PRIVATE LibURL LibWebView)

# test-web drives the desktop frontend and requires the UI-owned resource
# bundle. A RinOS helper-services-only build intentionally omits that frontend
# while still building LibWeb and its provider CTests, so do not register a
# target that cannot have its required resource dependency in that profile.
if (NOT RINOS_HELPER_SERVICES_ONLY)
    add_subdirectory("test-web")
endif()
