# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

add_library(lib_ui STATIC)
add_library(desktop-app::lib_ui ALIAS lib_ui)
init_target(lib_ui)

include(cmake/generate_palette.cmake)
include(cmake/generate_styles.cmake)
include(cmake/generate_emoji.cmake)

get_filename_component(src_loc . REALPATH)

set(style_files
    ui/basic.style
    ui/layers/layers.style
    ui/widgets/widgets.style
)

generate_palette(lib_ui ui/colors.palette)
generate_styles(lib_ui ${src_loc} "${style_files}" ui/colors.palette)
generate_emoji(lib_ui emoji.txt emoji_suggestions/emoji_autocomplete.json)

set_target_properties(lib_ui PROPERTIES AUTOMOC ON)

target_precompile_headers(lib_ui PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${src_loc}/ui/ui_pch.h>)
nice_target_sources(lib_ui ${src_loc}
PRIVATE
    ${style_files}
    ui/colors.palette
    emoji_suggestions/emoji_autocomplete.json

    qt_conf/win.qrc

    ui/dpr/dpr_icon.cpp
    ui/dpr/dpr_icon.h
    ui/dpr/dpr_image.h
    ui/effects/animation_value.cpp
    ui/effects/animation_value.h
    ui/effects/animation_value_f.h
    ui/effects/animations.cpp
    ui/effects/animations.h
    ui/effects/cross_animation.cpp
    ui/effects/cross_animation.h
    ui/effects/cross_line.cpp
    ui/effects/cross_line.h
    ui/effects/fade_animation.cpp
    ui/effects/fade_animation.h
    ui/effects/frame_generator.cpp
    ui/effects/frame_generator.h
    ui/effects/gradient.cpp
    ui/effects/gradient.h
    ui/effects/numbers_animation.cpp
    ui/effects/numbers_animation.h
    ui/effects/panel_animation.cpp
    ui/effects/panel_animation.h
    ui/effects/path_shift_gradient.cpp
    ui/effects/path_shift_gradient.h
    ui/effects/radial_animation.cpp
    ui/effects/radial_animation.h
    ui/effects/ripple_animation.cpp
    ui/effects/ripple_animation.h
    ui/effects/round_area_with_shadow.cpp
    ui/effects/round_area_with_shadow.h
    ui/effects/show_animation.cpp
    ui/effects/show_animation.h
    ui/effects/slide_animation.cpp
    ui/effects/slide_animation.h
    ui/effects/spoiler_mess.cpp
    ui/effects/spoiler_mess.h
    ui/gl/gl_detection.cpp
    ui/gl/gl_detection.h
    ui/gl/gl_image.cpp
    ui/gl/gl_image.h
    ui/gl/gl_math.cpp
    ui/gl/gl_math.h
    ui/gl/gl_primitives.cpp
    ui/gl/gl_primitives.h
    ui/gl/gl_shader.cpp
    ui/gl/gl_shader.h
    ui/gl/gl_surface.cpp
    ui/gl/gl_surface.h
    ui/gl/gl_window.cpp
    ui/gl/gl_window.h
    ui/image/image_prepare.cpp
    ui/image/image_prepare.h
    ui/layers/box_content.cpp
    ui/layers/box_content.h
    ui/layers/box_layer_widget.cpp
    ui/layers/box_layer_widget.h
    ui/layers/generic_box.cpp
    ui/layers/generic_box.h
    ui/layers/layer_manager.cpp
    ui/layers/layer_manager.h
    ui/layers/layer_widget.cpp
    ui/layers/layer_widget.h
    ui/layers/show.cpp
    ui/layers/show.h
    ui/paint/arcs.cpp
    ui/paint/arcs.h
    ui/paint/blob.cpp
    ui/paint/blob.h
    ui/paint/blobs.cpp
    ui/paint/blobs.h
    ui/paint/blobs_linear.cpp
    ui/paint/blobs_linear.h
    ui/platform/linux/ui_linux_wayland_integration.cpp
    ui/platform/linux/ui_linux_wayland_integration_dummy.cpp
    ui/platform/linux/ui_linux_wayland_integration.h
    ui/platform/linux/ui_window_linux.cpp
    ui/platform/linux/ui_window_linux.h
    ui/platform/linux/ui_window_title_linux.cpp
    ui/platform/linux/ui_window_title_linux.h
    ui/platform/linux/ui_utility_linux.cpp
    ui/platform/linux/ui_utility_linux.h
    ui/platform/mac/ui_window_mac.h
    ui/platform/mac/ui_window_mac.mm
    ui/platform/mac/ui_window_title_mac.h
    ui/platform/mac/ui_window_title_mac.mm
    ui/platform/mac/ui_utility_mac.h
    ui/platform/mac/ui_utility_mac.mm
    ui/platform/win/ui_window_shadow_win.cpp
    ui/platform/win/ui_window_shadow_win.h
    ui/platform/win/ui_window_title_win.cpp
    ui/platform/win/ui_window_title_win.h
    ui/platform/win/ui_window_win.cpp
    ui/platform/win/ui_window_win.h
    ui/platform/win/ui_windows_direct_manipulation.cpp
    ui/platform/win/ui_windows_direct_manipulation.h
    ui/platform/win/ui_utility_win.cpp
    ui/platform/win/ui_utility_win.h
    ui/platform/ui_platform_window_title.cpp
    ui/platform/ui_platform_window_title.h
    ui/platform/ui_platform_window.cpp
    ui/platform/ui_platform_window.h
    ui/platform/ui_platform_utility.h
    ui/style/style_core.cpp
    ui/style/style_core.h
    ui/style/style_core_color.cpp
    ui/style/style_core_color.h
    ui/style/style_core_custom_font.cpp
    ui/style/style_core_custom_font.h
    ui/style/style_core_direction.cpp
    ui/style/style_core_direction.h
    ui/style/style_core_font.cpp
    ui/style/style_core_font.h
    ui/style/style_core_icon.cpp
    ui/style/style_core_icon.h
    ui/style/style_core_palette.cpp
    ui/style/style_core_palette.h
    ui/style/style_core_scale.cpp
    ui/style/style_core_scale.h
    ui/style/style_core_types.cpp
    ui/style/style_core_types.h
    ui/style/style_palette_colorizer.cpp
    ui/style/style_palette_colorizer.h
    ui/text/custom_emoji_instance.cpp
    ui/text/custom_emoji_instance.h
    ui/text/text.cpp
    ui/text/text.h
    ui/text/text_block.cpp
    ui/text/text_block.h
    ui/text/text_custom_emoji.cpp
    ui/text/text_custom_emoji.h
    ui/text/text_entity.cpp
    ui/text/text_entity.h
    ui/text/text_isolated_emoji.h
    ui/text/text_parser.cpp
    ui/text/text_parser.h
    ui/text/text_renderer.cpp
    ui/text/text_renderer.h
    ui/text/text_spoiler_data.cpp
    ui/text/text_spoiler_data.h
    ui/text/text_utilities.cpp
    ui/text/text_utilities.h
    ui/text/text_variant.cpp
    ui/text/text_variant.h
    ui/toast/toast.cpp
    ui/toast/toast.h
    ui/toast/toast_manager.cpp
    ui/toast/toast_manager.h
    ui/toast/toast_widget.cpp
    ui/toast/toast_widget.h
    ui/widgets/box_content_divider.cpp
    ui/widgets/box_content_divider.h
    ui/widgets/buttons.cpp
    ui/widgets/buttons.h
    ui/widgets/call_button.cpp
    ui/widgets/call_button.h
    ui/widgets/checkbox.cpp
    ui/widgets/checkbox.h
    ui/widgets/dropdown_menu.cpp
    ui/widgets/dropdown_menu.h
    ui/widgets/icon_button_with_text.cpp
    ui/widgets/icon_button_with_text.h
    ui/widgets/inner_dropdown.cpp
    ui/widgets/inner_dropdown.h
    ui/widgets/fields/input_field.cpp
    ui/widgets/fields/input_field.h
    ui/widgets/fields/masked_input_field.cpp
    ui/widgets/fields/masked_input_field.h
    ui/widgets/fields/number_input.cpp
    ui/widgets/fields/number_input.h
    ui/widgets/fields/password_input.cpp
    ui/widgets/fields/password_input.h
    ui/widgets/fields/time_part_input.cpp
    ui/widgets/fields/time_part_input.h
    ui/widgets/labels.cpp
    ui/widgets/labels.h
    ui/widgets/menu/menu.cpp
    ui/widgets/menu/menu.h
    ui/widgets/menu/menu_action.cpp
    ui/widgets/menu/menu_action.h
    ui/widgets/menu/menu_add_action_callback.cpp
    ui/widgets/menu/menu_add_action_callback.h
    ui/widgets/menu/menu_add_action_callback_factory.cpp
    ui/widgets/menu/menu_add_action_callback_factory.h
    ui/widgets/menu/menu_common.cpp
    ui/widgets/menu/menu_common.h
    ui/widgets/menu/menu_item_base.cpp
    ui/widgets/menu/menu_item_base.h
    ui/widgets/menu/menu_multiline_action.cpp
    ui/widgets/menu/menu_multiline_action.h
    ui/widgets/menu/menu_separator.cpp
    ui/widgets/menu/menu_separator.h
    ui/widgets/menu/menu_toggle.cpp
    ui/widgets/menu/menu_toggle.h
    ui/widgets/elastic_scroll.cpp
    ui/widgets/elastic_scroll.h
    ui/widgets/popup_menu.cpp
    ui/widgets/popup_menu.h
    ui/widgets/rp_window.cpp
    ui/widgets/rp_window.h
    ui/widgets/separate_panel.cpp
    ui/widgets/separate_panel.h
    ui/widgets/scroll_area.cpp
    ui/widgets/scroll_area.h
    ui/widgets/side_bar_button.cpp
    ui/widgets/side_bar_button.h
    ui/widgets/shadow.cpp
    ui/widgets/shadow.h
    ui/widgets/time_input.cpp
    ui/widgets/time_input.h
    ui/widgets/tooltip.cpp
    ui/widgets/tooltip.h
    ui/wrap/fade_wrap.cpp
    ui/wrap/fade_wrap.h
    ui/wrap/follow_slide_wrap.cpp
    ui/wrap/follow_slide_wrap.h
    ui/wrap/padding_wrap.cpp
    ui/wrap/padding_wrap.h
    ui/wrap/slide_wrap.cpp
    ui/wrap/slide_wrap.h
    ui/wrap/vertical_layout.cpp
    ui/wrap/vertical_layout.h
    ui/wrap/vertical_layout_reorder.cpp
    ui/wrap/vertical_layout_reorder.h
    ui/wrap/wrap.h
    ui/abstract_button.cpp
    ui/abstract_button.h
    ui/animated_icon.cpp
    ui/animated_icon.h
    ui/basic_click_handlers.cpp
    ui/basic_click_handlers.h
    ui/cached_special_layer_shadow_corners.cpp
    ui/cached_special_layer_shadow_corners.h
    ui/click_handler.cpp
    ui/click_handler.h
    ui/delayed_activation.cpp
    ui/delayed_activation.h
    ui/dragging_scroll_manager.cpp
    ui/dragging_scroll_manager.h
    ui/emoji_config.cpp
    ui/emoji_config.h
    ui/focus_persister.h
    ui/inactive_press.cpp
    ui/inactive_press.h
    ui/integration.cpp
    ui/integration.h
    ui/main_queue_processor.cpp
    ui/main_queue_processor.h
    ui/painter.h
    ui/ph.cpp
    ui/ph.h
    ui/rect.h
    ui/rect_part.h
    ui/round_rect.cpp
    ui/round_rect.h
    ui/rp_widget.cpp
    ui/rp_widget.h
    ui/ui_utility.cpp
    ui/ui_utility.h

    ui/ui_pch.h

    emoji_suggestions/emoji_suggestions.cpp
    emoji_suggestions/emoji_suggestions.h
    emoji_suggestions/emoji_suggestions_helper.h
)

if (NOT DESKTOP_APP_USE_PACKAGED_FONTS)
    nice_target_sources(lib_ui ${src_loc} PRIVATE fonts/fonts.qrc)
endif()

if (WIN32)
    nuget_add_winrt(lib_ui)
endif()

if (DESKTOP_APP_DISABLE_WAYLAND_INTEGRATION)
    remove_target_sources(lib_ui ${src_loc} ui/platform/linux/ui_linux_wayland_integration.cpp)
elseif(LINUX)
    remove_target_sources(lib_ui ${src_loc} ui/platform/linux/ui_linux_wayland_integration_dummy.cpp)

    qt_generate_wayland_protocol_client_sources(lib_ui
    FILES
        ${third_party_loc}/wayland/protocol/wayland.xml
        ${third_party_loc}/wayland-protocols/stable/xdg-shell/xdg-shell.xml
    )

    target_link_libraries(lib_ui
    PUBLIC
        desktop-app::external_wayland_client
    )
endif()

target_include_directories(lib_ui
PUBLIC
    ${src_loc}
    ${src_loc}/emoji_suggestions
)

target_link_libraries(lib_ui
PUBLIC
    desktop-app::lib_base
PRIVATE
    desktop-app::external_zlib
    desktop-app::external_jpeg
    desktop-app::external_lz4
    desktop-app::external_xxhash
)

target_prepare_qrc(lib_ui)
