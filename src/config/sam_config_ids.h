// Control identifiers for the SAM Voice Settings dialog.
#pragma once

#define IDD_SETTINGS            100
#define IDI_APP                 101

// Voice selection
#define IDC_VOICE_LABEL         1000
#define IDC_VOICE               1001

// Per-voice parameters: each is an edit control with a spin buddy, so screen
// readers announce the exact number instead of a percentage.
#define IDC_PITCH_LABEL         1010
#define IDC_PITCH               1011
#define IDC_PITCH_SPIN          1012
#define IDC_SPEED_LABEL         1013
#define IDC_SPEED               1014
#define IDC_SPEED_SPIN          1015
#define IDC_MOUTH_LABEL         1016
#define IDC_MOUTH               1017
#define IDC_MOUTH_SPIN          1018
#define IDC_THROAT_LABEL        1019
#define IDC_THROAT              1020
#define IDC_THROAT_SPIN         1021
#define IDC_INFLECTION_LABEL    1022
#define IDC_INFLECTION          1023
#define IDC_INFLECTION_SPIN     1024
#define IDC_SINGMODE            1025

// Global settings
#define IDC_VOLUME_LABEL        1030
#define IDC_VOLUME              1031
#define IDC_VOLUME_SPIN         1032
#define IDC_EXPAND_NUMBERS      1033
#define IDC_SLOWEST_LABEL       1034
#define IDC_SLOWEST             1035
#define IDC_SLOWEST_SPIN        1036
#define IDC_FASTEST_LABEL       1037
#define IDC_FASTEST             1038
#define IDC_FASTEST_SPIN        1039
#define IDC_LOGLEVEL_LABEL      1040
#define IDC_LOGLEVEL            1041

// Test phrase and actions
#define IDC_TESTTEXT_LABEL      1050
#define IDC_TESTTEXT            1051
#define IDC_TEST                1052
#define IDC_STOP                1053
#define IDC_RESET_VOICE         1054
#define IDC_RESET_ALL           1055
#define IDC_OPEN_LOGS           1056
#define IDC_STATUS              1057

#define IDC_GROUP_VOICE         1090
#define IDC_GROUP_GLOBAL        1091
#define IDC_GROUP_TEST          1092
