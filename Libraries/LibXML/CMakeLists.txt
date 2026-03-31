set(SOURCES
    Parser/Parser.cpp
    DOM/Node.cpp
)

ladybird_lib(LibXML xml EXPLICIT_SYMBOL_EXPORT)

if (NOT AK_OS_RINOS)
    find_package(LibXml2 REQUIRED)
    target_link_libraries(LibXML PRIVATE LibXml2::LibXml2)
else()
    # RinOS: LibXML uses Ladybird's built-in XML parser; libxml2 binding is excluded
    message(STATUS "RinOS policy: LibXml2 external dependency excluded.")
endif()
