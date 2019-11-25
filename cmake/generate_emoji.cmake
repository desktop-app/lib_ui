function(generate_emoji target_name suggestions_json)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

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
    generate_target(${target_name} emoji "${generated_files}" ${gen_dst})
endfunction()
