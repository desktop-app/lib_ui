# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

add_library(lib_ui OBJECT)
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

set_target_properties(lib_ui PROPERTIES AUTOMOC ON AUTORCC ON)

target_precompile_headers(lib_ui PRIVATE ${src_loc}/ui/ui_pch.h)
nice_target_sources(lib_ui ${src_loc}
PRIVATE
    ${style_files}
    ui/colors.palette
    emoji_suggestions/emoji_autocomplete.json

    qt_conf/mac.qrc
    qt_conf/win.qrc

    ui/effects/animation_value.cpp
    ui/effects/animation_value.h
    ui/effects/animations.cpp
    ui/effects/animations.h
    ui/effects/cross_animation.cpp
    ui/effects/cross_animation.h
    ui/effects/fade_animation.cpp
    ui/effects/fade_animation.h
    ui/effects/numbers_animation.cpp
    ui/effects/numbers_animation.h
    ui/effects/panel_animation.cpp
    ui/effects/panel_animation.h
    ui/effects/radial_animation.cpp
    ui/effects/radial_animation.h
    ui/effects/ripple_animation.cpp
    ui/effects/ripple_animation.h
    ui/effects/slide_animation.cpp
    ui/effects/slide_animation.h
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
    ui/platform/linux/ui_window_linux.cpp
    ui/platform/linux/ui_window_linux.h
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
    ui/platform/win/ui_utility_win.cpp
    ui/platform/win/ui_utility_win.h
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
    ui/style/style_core_scale.cpp
    ui/style/style_core_scale.h
    ui/style/style_core_types.cpp
    ui/style/style_core_types.h
    ui/text/qtextitemint.cpp
    ui/text/text.cpp
    ui/text/text.h
    ui/text/text_block.cpp
    ui/text/text_block.h
    ui/text/text_entity.cpp
    ui/text/text_entity.h
    ui/text/text_isolated_emoji.h
    ui/text/text_utilities.cpp
    ui/text/text_utilities.h
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
    ui/widgets/checkbox.cpp
    ui/widgets/checkbox.h
    ui/widgets/dropdown_menu.cpp
    ui/widgets/dropdown_menu.h
    ui/widgets/inner_dropdown.cpp
    ui/widgets/inner_dropdown.h
    ui/widgets/input_fields.cpp
    ui/widgets/input_fields.h
    ui/widgets/labels.cpp
    ui/widgets/labels.h
    ui/widgets/menu.cpp
    ui/widgets/menu.h
    ui/widgets/popup_menu.cpp
    ui/widgets/popup_menu.h
    ui/widgets/scroll_area.cpp
    ui/widgets/scroll_area.h
    ui/widgets/side_bar_button.cpp
    ui/widgets/side_bar_button.h
    ui/widgets/shadow.cpp
    ui/widgets/shadow.h
    ui/widgets/tooltip.cpp
    ui/widgets/tooltip.h
    ui/widgets/window.cpp
    ui/widgets/window.h
    ui/wrap/fade_wrap.cpp
    ui/wrap/fade_wrap.h
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
    ui/basic_click_handlers.cpp
    ui/basic_click_handlers.h
    ui/click_handler.cpp
    ui/click_handler.h
    ui/delayed_activation.cpp
    ui/delayed_activation.h
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
    ui/rect_part.h
    ui/round_rect.cpp
    ui/round_rect.h
    ui/rp_widget.cpp
    ui/rp_widget.h
    ui/ui_log.cpp
    ui/ui_log.h
    ui/ui_utility.cpp
    ui/ui_utility.h
    emoji_suggestions/emoji_suggestions.cpp
    emoji_suggestions/emoji_suggestions.h
    emoji_suggestions/emoji_suggestions_helper.h
)

if (NOT DESKTOP_APP_USE_PACKAGED)
    nice_target_sources(lib_ui ${src_loc} PRIVATE qt_conf/linux.qrc)
endif()

if (NOT DESKTOP_APP_USE_PACKAGED_FONTS)
    nice_target_sources(lib_ui ${src_loc} PRIVATE fonts/fonts.qrc)
endif()

target_include_directories(lib_ui
PUBLIC
    ${src_loc}
    ${src_loc}/emoji_suggestions
)

target_link_libraries(lib_ui
PUBLIC
    desktop-app::lib_base
)
