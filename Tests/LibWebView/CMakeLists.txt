set(TEST_SOURCES
    TestMenu.cpp
    TestSiteIsolation.cpp
    TestWebViewURL.cpp
)

foreach(source IN LISTS TEST_SOURCES)
    ladybird_test("${source}" LibWebView LIBS LibWebView LibURL)
endforeach()
