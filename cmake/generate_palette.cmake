function(generate_palette target_name palette_file)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(gen_timestamp ${gen_dst}/styles/palette.timestamp)
    set(gen_files
         ${gen_dst}/styles/palette.cpp
         ${gen_dst}/styles/palette.h
    )

    set(gen_src ${CMAKE_CURRENT_SOURCE_DIR}/${palette_file})
    add_custom_command(
    OUTPUT
       ${gen_timestamp}
    BYPRODUCTS
        ${gen_files}
    COMMAND
        codegen_style
        -I${gen_dst}
        -o${gen_dst}/styles
        -t${gen_dst}/styles/palette
        -w${CMAKE_CURRENT_SOURCE_DIR}
        ${gen_src}
    COMMENT "Generating palette (${target_name})"
    DEPENDS
        codegen_style
        ${gen_src}
    MAIN_DEPENDENCY
        ${gen_src}
    )
    generate_target(${target_name} palette ${gen_timestamp} "${gen_files}" ${gen_dst})
endfunction()
