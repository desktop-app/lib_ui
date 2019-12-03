function(generate_styles target_name src_loc style_files dependent_style_files)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(gen_timestamp ${gen_dst}/styles/${target_name}_style.timestamp)
    set(gen_files "")
    set(full_generation_sources "")
    set(full_dependencies_list ${dependent_style_files})
    foreach (file ${style_files})
        list(APPEND full_generation_sources ${src_loc}/${file})
        get_filename_component(file_name ${file} NAME_WLE)
        list(APPEND gen_files
            ${gen_dst}/styles/style_${file_name}.cpp
            ${gen_dst}/styles/style_${file_name}.h
        )
    endforeach()
    list(APPEND full_dependencies_list ${full_generation_sources})

    add_custom_command(
    OUTPUT
        ${gen_timestamp}
    BYPRODUCTS
        ${gen_files}
    COMMAND
        codegen_style
        -I${src_loc}
        -I${submodules_loc}/lib_ui
        -I${submodules_loc}/Resources
        -o${gen_dst}/styles
        -t${gen_dst}/styles/${target_name}_style
        -w${CMAKE_CURRENT_SOURCE_DIR}
        ${full_generation_sources}
    COMMENT "Generating styles (${target_name})"
    DEPENDS
        codegen_style
        ${full_dependencies_list}
    )

    generate_target(${target_name} styles ${gen_timestamp} "${gen_files}" ${gen_dst})
endfunction()
