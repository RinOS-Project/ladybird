include(FindPackageHandleStandardArgs)

set(_lagom_tool_names
    IPCCompiler
    BindingsGenerator
    GenerateAriaRoles
    GenerateCSSDescriptors
    GenerateCSSEnums
    GenerateCSSEnvironmentVariable
    GenerateCSSKeyword
    GenerateCSSMathFunctions
    GenerateCSSMediaFeatureID
    GenerateCSSNumericFactoryMethods
    GenerateCSSPropertyID
    GenerateCSSPseudoClass
    GenerateCSSPseudoElement
    GenerateCSSStyleProperties
    GenerateCSSTransformFunctions
    GenerateCSSUnits
    GenerateNamedCharacterReferences
    GenerateWindowOrWorkerInterfaces
)

set(_lagom_root_candidates)
foreach(candidate
    "${LagomTools_ROOT}"
    "${LAGOM_TOOLS_ROOT}"
    "$ENV{LagomTools_ROOT}"
    "$ENV{LAGOM_TOOLS_ROOT}"
    "${CMAKE_PREFIX_PATH}"
)
    if(candidate)
        list(APPEND _lagom_root_candidates "${candidate}")
    endif()
endforeach()

list(APPEND _lagom_root_candidates
    "${CMAKE_BINARY_DIR}"
    "${CMAKE_BINARY_DIR}/.."
)

set(_lagom_tool_hints)
foreach(root ${_lagom_root_candidates})
    if(root)
        list(APPEND _lagom_tool_hints
            "${root}"
            "${root}/bin"
            "${root}/Lagom"
            "${root}/Lagom/bin"
            "${root}/Build/lagom"
            "${root}/Build/lagom/bin"
            "${root}/Tools/CodeGenerators"
            "${root}/Lagom/Tools/CodeGenerators"
        )
    endif()
endforeach()
list(REMOVE_DUPLICATES _lagom_tool_hints)

function(_lagom_import_tool tool_name)
    if(TARGET Lagom::${tool_name})
        return()
    endif()

    find_program(LAGOM_${tool_name}_EXECUTABLE
        NAMES ${tool_name}
        HINTS ${_lagom_tool_hints}
        NO_CACHE
    )

    if(LAGOM_${tool_name}_EXECUTABLE)
        add_executable(Lagom::${tool_name} IMPORTED GLOBAL)
        set_target_properties(Lagom::${tool_name} PROPERTIES
            IMPORTED_LOCATION "${LAGOM_${tool_name}_EXECUTABLE}"
        )
        set(LAGOM_${tool_name}_EXECUTABLE "${LAGOM_${tool_name}_EXECUTABLE}" PARENT_SCOPE)
    endif()
endfunction()

set(_lagom_required_vars)
foreach(tool_name ${_lagom_tool_names})
    _lagom_import_tool(${tool_name})
    list(APPEND _lagom_required_vars LAGOM_${tool_name}_EXECUTABLE)
endforeach()

find_package_handle_standard_args(LagomTools
    REQUIRED_VARS ${_lagom_required_vars}
)

mark_as_advanced(${_lagom_required_vars})
