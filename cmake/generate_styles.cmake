function(append_generate_target target_name postfix generated_files gen_dst)
    add_custom_target(${target_name}_${postfix} DEPENDS ${generated_files})
    init_target(${target_name}_${postfix} "(gen)")
    add_dependencies(${target_name} ${target_name}_${postfix})
    target_sources(${target_name} PRIVATE ${generated_files})
    target_include_directories(${target_name} PUBLIC ${gen_dst})
    source_group("(gen)" FILES ${generated_files})
endfunction()

function(generate_palette target_name palette_file)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    set(gen_src ${CMAKE_CURRENT_SOURCE_DIR}/${palette_file})
    set(generated_files
        ${gen_dst}/styles/palette.cpp
        ${gen_dst}/styles/palette.h
    )
    add_custom_command(
    OUTPUT
        ${generated_files}
    COMMAND
        codegen_style
        -I${gen_dst}
        -o${gen_dst}/styles
        -w${CMAKE_CURRENT_SOURCE_DIR}
        ${gen_src}
    COMMENT "Generating palette (${target_name})"
    DEPENDS
        codegen_style
        ${gen_src}
    MAIN_DEPENDENCY
        ${gen_src}
    )
    append_generate_target(${target_name} palette "${generated_files}" ${gen_dst})
endfunction()

function(generate_styles target_name src_loc style_files dependent_style_files)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    set(full_generated_files "")
    set(full_dependencies_list ${style_files})
    list(APPEND full_dependencies_list ${dependent_style_files})
    foreach (file ${style_files})
        set(gen_src ${CMAKE_CURRENT_SOURCE_DIR}/${file})
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

    append_generate_target(${target_name} styles "${full_generated_files}" ${gen_dst})
endfunction()

function(generate_emoji target_name suggestions_json)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    set(gen_src ${CMAKE_CURRENT_SOURCE_DIR}/${suggestions_json})
    set(generated_files
        ${gen_dst}/emoji.cpp
        ${gen_dst}/emoji.h
        ${gen_dst}/emoji_suggestions_data.cpp
        ${gen_dst}/emoji_suggestions_data.h
    )
    add_custom_command(
    OUTPUT
        ${generated_files}
    COMMAND
        codegen_emoji
        -o${gen_dst}
        ${gen_src}
    COMMENT "Generating emoji (${target_name})"
    DEPENDS
        codegen_emoji
        ${gen_src}
    )
    append_generate_target(${target_name} emoji "${generated_files}" ${gen_dst})
endfunction()
