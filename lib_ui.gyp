# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

{
  'includes': [
    '../gyp/helpers/common/common.gypi',
  ],
  'targets': [{
    'target_name': 'lib_ui',
    'hard_dependency': 1,
    'includes': [
      '../gyp/helpers/common/library.gypi',
      '../gyp/helpers/modules/qt.gypi',
      '../gyp/helpers/modules/qt_moc.gypi',
      '../gyp/helpers/modules/pch.gypi',
      '../gyp/helpers/modules/openssl.gypi',
      'gyp/styles_rule.gypi',
      'gyp/codegen_actions.gypi',
    ],
    'dependencies': [
      '<(submodules_loc)/codegen/codegen.gyp:codegen_emoji',
      '<(submodules_loc)/codegen/codegen.gyp:codegen_style',
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
    ],
    'export_dependent_settings': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
    ],
    'variables': {
      'src_loc': '.',
      'emoji_suggestions_loc': '<(src_loc)/emoji_suggestions',
      'style_files': [
        '<(src_loc)/ui/colors.palette',
        '<(src_loc)/ui/basic.style',
        '<(src_loc)/ui/layers/layers.style',
        '<(src_loc)/ui/widgets/widgets.style',
      ],
      'dependent_style_files': [
      ],
      'style_timestamp': '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles_ui.timestamp',
      'list_sources_command': 'python <(submodules_loc)/lib_base/gyp/list_sources.py --input gyp/sources.txt --replace src_loc=<(src_loc)',
      'pch_source': '<(src_loc)/ui/ui_pch.cpp',
      'pch_header': '<(src_loc)/ui/ui_pch.h',
    },
    'defines': [
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(emoji_suggestions_loc)',
    ],
    'direct_dependent_settings': {
      'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(emoji_suggestions_loc)',
      ],
    },
    'sources': [
      '<@(style_files)',
      'gyp/sources.txt',
      '<!@(<(list_sources_command) <(qt_moc_list_sources_arg))',
    ],
    'sources!': [
      '<!@(<(list_sources_command) <(qt_moc_list_sources_arg) --exclude_for <(build_os))',
    ],
  }],
}
