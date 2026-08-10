set(SOURCES
    Database.cpp
)

if ((NOT AK_OS_RINOS) OR (AK_OS_RINOS AND NOT CMAKE_CROSSCOMPILING))
    find_package(SQLite3 REQUIRED)
endif()

ladybird_lib(LibDatabase database EXPLICIT_SYMBOL_EXPORT)
if ((NOT AK_OS_RINOS) OR (AK_OS_RINOS AND NOT CMAKE_CROSSCOMPILING))
    target_link_libraries(LibDatabase PRIVATE LibCore SQLite::SQLite3)
else()
    # RinOS: libsqlite3 is provided by libs/libsqlite3, linked externally
    set(RINOS_SQLITE3_SOURCE ${CMAKE_SOURCE_DIR}/../libsqlite3/sqlite3.c)
    target_link_libraries(LibDatabase PRIVATE LibCore)
    target_sources(LibDatabase PRIVATE ${RINOS_SQLITE3_SOURCE})
    target_include_directories(LibDatabase BEFORE PRIVATE
        ${CMAKE_SOURCE_DIR}/../libc
        ${CMAKE_SOURCE_DIR}/../libsqlite3
    )
    target_compile_definitions(LibDatabase PRIVATE SQLITE_THREADSAFE=0 SQLITE_OMIT_LOAD_EXTENSION=1)
    set_source_files_properties(${RINOS_SQLITE3_SOURCE} PROPERTIES COMPILE_OPTIONS "-Wno-error")
    message(STATUS "RinOS policy: SQLite3 provided by RinOS native libsqlite3.")
endif()
