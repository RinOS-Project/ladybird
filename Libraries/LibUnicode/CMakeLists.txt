set(SOURCES
    Calendar.cpp
    Calendars/AdjustedEraCalendar.cpp
    Calendars/ChineseDangiCalendar.cpp
    CharacterTypes.cpp
    Collator.cpp
    CurrencyCode.cpp
    DateTimeFormat.cpp
    DisplayNames.cpp
    DurationFormat.cpp
    ICU.cpp
    IDNA.cpp
    ListFormat.cpp
    Locale.cpp
    Normalize.cpp
    NumberFormat.cpp
    PluralRules.cpp
    RelativeTimeFormat.cpp
    Segmenter.cpp
    String.cpp
    TimeZone.cpp
    UnicodeKeywords.cpp
    Utf16String.cpp
)

if (AK_OS_RINOS)
    list(APPEND SOURCES RinICUBridge.cpp)

    set(RINICU_HOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/RinICUHost")
    set(LIBUNICODE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../libunicode")

    if (NOT TARGET rinicu)
        add_library(rinicu STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/RinICUHostCompat.c
        )
        target_include_directories(rinicu PRIVATE ${RINICU_HOST_ROOT})
        target_compile_options(rinicu PRIVATE -Wno-cast-qual)
    endif()

    if (NOT TARGET libunicode)
        add_library(libunicode STATIC
            ${LIBUNICODE_ROOT}/core.c
            ${LIBUNICODE_ROOT}/locale_adapter.c
        )
        target_include_directories(libunicode PRIVATE ${LIBUNICODE_ROOT})
        target_compile_options(libunicode PRIVATE -Wno-cast-qual)
    endif()
endif()

set(GENERATED_SOURCES ${CURRENT_LIB_GENERATED})

ladybird_lib(LibUnicode unicode)

if (AK_OS_RINOS)
    # RinOS: link rinicu + libunicode instead of ICU/Rust
    target_link_libraries(LibUnicode PRIVATE rinicu libunicode)
    target_compile_definitions(LibUnicode PRIVATE AK_OS_RINOS=1)
    target_compile_options(LibUnicode PRIVATE -Wno-unused-const-variable -Wno-unused-function)
    target_include_directories(LibUnicode PRIVATE
        ${RINICU_HOST_ROOT}
        ${LIBUNICODE_ROOT}
    )
else()
    if (RINOS_USE_ICU_BACKEND)
        find_package(ICU 78.2 EXACT REQUIRED COMPONENTS data i18n uc)
        target_link_libraries(LibUnicode PRIVATE ICU::i18n ICU::uc ICU::data)
    else()
        message(STATUS "RinOS policy: ICU package lookup is disabled for LibUnicode. using libunicode_rust only.")
    endif()

    import_rust_crate(MANIFEST_PATH Rust/Cargo.toml CRATE_NAME libunicode_rust)
    target_link_libraries(LibUnicode PRIVATE libunicode_rust)
endif()

# FIXME: Add support for building LibGfx in sanitize
#   lld-link: error: /failifmismatch: mismatch detected for 'annotate_string':
#   >>> lagom-unicode.lib(TimeZone.cpp.obj) has value 1
#   >>> lagom-gfx.lib(PaintingSurface.cpp.obj) has value 0
if (WIN32 AND ENABLE_ADDRESS_SANITIZER)
    target_compile_options(LibUnicode PRIVATE -fno-sanitize=address)
endif()
