# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

{
  'variables': {
    'qrc_files': [
      '<(submodules_loc)/lib_ui/fonts/fonts.qrc',
    ],
  },
  'conditions': [
    [ 'build_linux', {
      'variables': {
        'qrc_files': [
          '<(submodules_loc)/lib_ui/qt_conf/linux.qrc',
        ],
      }
    }],
    [ 'build_mac', {
      'variables': {
        'qrc_files': [
          '<(submodules_loc)/lib_ui/qt_conf/mac.qrc',
        ],
      },
    }],
    [ 'build_win', {
      'variables': {
        'qrc_files': [
          '<(submodules_loc)/lib_ui/qt_conf/win.qrc',
        ],
      }
    }],
  ],
  'actions': [{
    'action_name': 'update_dependent_qrc',
    'inputs': [
      'update_dependent.py',
      '<@(qrc_files)',
      '<!@(python <(submodules_loc)/lib_ui/gyp/update_dependent.py --qrc_list <@(qrc_files))',
    ],
    'outputs': [
      '<(qrc_timestamp)',
    ],
    'action': [
      'python', '<(submodules_loc)/lib_ui/gyp/update_dependent.py', '--qrc',
      '-o', '<(qrc_timestamp)',
      '<@(qrc_files)',
    ],
    'message': 'Updating dependent qrc files..',
  }],
  'rules': [{
    'rule_name': 'qt_rcc',
    'extension': 'qrc',
    'inputs': [
      '<(qrc_timestamp)',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/<(_target_name)/qrc/qrc_<(RULE_INPUT_ROOT).cpp',
    ],
    'action': [
      '<(qt_loc)/bin/rcc<(exe_ext)',
      '-name', '<(RULE_INPUT_ROOT)',
      '-no-compress',
      '<(RULE_INPUT_PATH)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/<(_target_name)/qrc/qrc_<(RULE_INPUT_ROOT).cpp',
    ],
    'message': 'Rcc-ing <(RULE_INPUT_ROOT).qrc..',
    'process_outputs_as_sources': 1,
  }],
}
