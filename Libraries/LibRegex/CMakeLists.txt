if (AK_OS_RINOS)
    set(SOURCES
        ECMAScriptRegex.cpp
        RegexByteCode.cpp
        RegexLexer.cpp
        RegexMatcher.cpp
        RegexOptimizer.cpp
        RegexParser.cpp
    )
else()
    if (NOT ENABLE_RUST)
        message(FATAL_ERROR "LibRegex requires ENABLE_RUST; the legacy C++ regex engine has been removed")
    endif()

    set(SOURCES
        ECMAScriptRegex.cpp
        RustRegex.cpp
    )
endif()

ladybird_lib(LibRegex regex EXPLICIT_SYMBOL_EXPORT)
target_link_libraries(LibRegex PRIVATE LibUnicode)

if (NOT AK_OS_RINOS)
    import_rust_crate(MANIFEST_PATH Rust/Cargo.toml CRATE_NAME libregex_rust)
    target_link_libraries(LibRegex PRIVATE libregex_rust)
    target_compile_definitions(LibRegex PRIVATE ENABLE_RUST)
else()
    message(STATUS "RinOS policy: LibRegex uses the restored C++ ECMAScript engine and does not build Rust crates.")
endif()
