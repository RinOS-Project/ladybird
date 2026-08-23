set(SOURCES
    AbstractMachine/AbstractMachine.cpp
    AbstractMachine/BytecodeInterpreter.cpp
    AbstractMachine/Configuration.cpp
    AbstractMachine/Validator.cpp
    Parser/Parser.cpp
    Printer/Printer.cpp
)

# WASI uses the target's POSIX-compatible libc surface. On RinOS those libc
# functions are implemented with the RinOS syscall ABI, so the provider must
# remain part of LibWasm. Whether an embedding registers WASI imports is a
# separate capability policy.
if (NOT WIN32)
    list(APPEND SOURCES WASI/Wasi.cpp)
endif()

ladybird_lib(LibWasm wasm EXPLICIT_SYMBOL_EXPORT)
target_link_libraries(LibWasm PRIVATE LibCore)

include(wasm_spec_tests)
