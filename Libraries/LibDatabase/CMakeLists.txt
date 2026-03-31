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
    target_link_libraries(LibDatabase PRIVATE LibCore)
    message(STATUS "RinOS policy: SQLite3 provided by RinOS native libsqlite3.")
endif()
