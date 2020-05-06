function(generate_emoji target_name emoji_map suggestions_json)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(gen_timestamp ${gen_dst}/emoji.timestamp)
    set(gen_files
        ${gen_dst}/emoji.cpp
        ${gen_dst}/emoji.h
        ${gen_dst}/emoji_suggestions_data.cpp
        ${gen_dst}/emoji_suggestions_data.h
    )

    set(gen_src
        ${CMAKE_CURRENT_SOURCE_DIR}/${emoji_map}
        ${CMAKE_CURRENT_SOURCE_DIR}/${suggestions_json}
    )
    add_custom_command(
    OUTPUT
        ${gen_timestamp}
    BYPRODUCTS
        ${gen_files}
    COMMAND
        codegen_emoji
        -o${gen_dst}
        ${gen_src}
    COMMENT "Generating emoji (${target_name})"
    DEPENDS
        codegen_emoji
        ${gen_src}
    )
    generate_target(${target_name} emoji ${gen_timestamp} "${gen_files}" ${gen_dst})
endfunction()
