function(generate_styles target_name src_loc style_files dependent_style_files)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(full_generated_files "")
    set(full_dependencies_list ${dependent_style_files})
    foreach (file ${style_files})
        list(APPEND full_dependencies_list ${src_loc}/${file})
    endforeach()

    foreach (file ${style_files})
        set(gen_src ${src_loc}/${file})
        get_filename_component(file_name ${file} NAME_WLE)
        set(generated_files
            ${gen_dst}/styles/style_${file_name}.cpp
            ${gen_dst}/styles/style_${file_name}.h
        )
        list(APPEND full_generated_files ${generated_files})
        add_custom_command(
        OUTPUT
            ${generated_files}
        COMMAND
            codegen_style
            -I${src_loc}
            -I${submodules_loc}/lib_ui
            -I${submodules_loc}/Resources
            -o${gen_dst}/styles
            -w${CMAKE_CURRENT_SOURCE_DIR}
            ${gen_src}
        COMMENT "Generating style (${target_name}:${file_name})"
        DEPENDS
            codegen_style
            ${full_dependencies_list}
        MAIN_DEPENDENCY
            ${gen_src}
        )
    endforeach()

    generate_target(${target_name} styles "${full_generated_files}" ${gen_dst})
endfunction()
