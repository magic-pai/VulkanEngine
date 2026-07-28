function(selfengine_patch_imgui_node_editor source_dir)
    set(header_path "${source_dir}/imgui_extra_math.h")
    set(inline_path "${source_dir}/imgui_extra_math.inl")

    foreach(source_path IN ITEMS "${header_path}" "${inline_path}")
        if(NOT EXISTS "${source_path}")
            message(FATAL_ERROR
                "imgui-node-editor compatibility source is missing: ${source_path}"
            )
        endif()
    endforeach()

    file(READ "${header_path}" header_content)
    set(operator_declaration
        "inline ImVec2 operator*(const float lhs, const ImVec2& rhs);"
    )
    if(NOT header_content MATCHES
        "IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED[\r\n]+inline ImVec2 operator\\\*"
    )
        string(FIND "${header_content}" "${operator_declaration}" declaration_offset)
        if(declaration_offset EQUAL -1)
            message(FATAL_ERROR
                "imgui-node-editor scalar ImVec2 declaration changed; update the compatibility patch."
            )
        endif()
        string(REPLACE
            "${operator_declaration}"
            "# ifndef IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED\n${operator_declaration}\n# endif"
            header_content
            "${header_content}"
        )
        file(WRITE "${header_path}" "${header_content}")
    endif()

    file(READ "${inline_path}" inline_content)
    if(NOT inline_content MATCHES
        "IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED[\r\n]+inline ImVec2 operator\\\*"
    )
        set(operator_definition_pattern
            "inline ImVec2 operator\\\*\\(const float lhs, const ImVec2& rhs\\)[\r\n]+\\{[\r\n]+[ \t]+return ImVec2\\(lhs \\* rhs\\.x, lhs \\* rhs\\.y\\);[\r\n]+\\}"
        )
        if(NOT inline_content MATCHES "${operator_definition_pattern}")
            message(FATAL_ERROR
                "imgui-node-editor scalar ImVec2 definition changed; update the compatibility patch."
            )
        endif()
        string(REGEX REPLACE
            "${operator_definition_pattern}"
            "# ifndef IMGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED\ninline ImVec2 operator*(const float lhs, const ImVec2& rhs)\n{\n    return ImVec2(lhs * rhs.x, lhs * rhs.y);\n}\n# endif"
            inline_content
            "${inline_content}"
        )
        file(WRITE "${inline_path}" "${inline_content}")
    endif()
endfunction()
