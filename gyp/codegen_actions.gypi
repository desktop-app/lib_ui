# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

{
  'actions': [{
    'action_name': 'codegen_palette',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '<(style_timestamp)',
      '<(src_loc)/ui/colors.palette',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/styles/palette.h',
      '<(SHARED_INTERMEDIATE_DIR)/styles/palette.cpp',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '-I', '<(src_loc)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/styles',
      '-w', '<(PRODUCT_DIR)/..',

      # GYP/Ninja bug workaround: if we specify just <(RULE_INPUT_PATH)
      # the <(RULE_INPUT_ROOT) variables won't be available in Ninja,
      # and the 'message' will be just 'codegen_style-ing .style..'
      # Looks like the using the <(RULE_INPUT_ROOT) here "exports" it
      # for using in the 'message' field.

      '<(src_loc)/ui/colors.palette',
    ],
    'message': 'codegen_palette-ing colors..',
    'process_outputs_as_sources': 1,
  }, {
    'action_name': 'codegen_emoji',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_emoji<(exe_ext)',
      '<(emoji_suggestions_loc)/emoji_autocomplete.json',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/emoji.cpp',
      '<(SHARED_INTERMEDIATE_DIR)/emoji.h',
      '<(SHARED_INTERMEDIATE_DIR)/emoji_suggestions_data.cpp',
      '<(SHARED_INTERMEDIATE_DIR)/emoji_suggestions_data.h',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_emoji<(exe_ext)',
      '<(emoji_suggestions_loc)/emoji_autocomplete.json',
      '-o', '<(SHARED_INTERMEDIATE_DIR)',
    ],
    'message': 'codegen_emoji-ing..',
    'process_outputs_as_sources': 1,
  }],
}
