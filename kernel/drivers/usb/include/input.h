#pragma once

#define KEY_RESERVED        0

// ==========================================
// 1. 主键盘区 (Alphanumeric & Punctuation)
// ==========================================
#define KEY_ESC             1
#define KEY_1               2
#define KEY_2               3
#define KEY_3               4
#define KEY_4               5
#define KEY_5               6
#define KEY_6               7
#define KEY_7               8
#define KEY_8               9
#define KEY_9               10
#define KEY_0               11
#define KEY_MINUS           12      // -
#define KEY_EQUAL           13      // =
#define KEY_BACKSPACE       14

#define KEY_TAB             15
#define KEY_Q               16
#define KEY_W               17
#define KEY_E               18
#define KEY_R               19
#define KEY_T               20
#define KEY_Y               21
#define KEY_U               22
#define KEY_I               23
#define KEY_O               24
#define KEY_P               25
#define KEY_LEFTBRACE       26      // [
#define KEY_RIGHTBRACE      27      // ]
#define KEY_ENTER           28

#define KEY_A               30
#define KEY_S               31
#define KEY_D               32
#define KEY_F               33
#define KEY_G               34
#define KEY_H               35
#define KEY_J               36
#define KEY_K               37
#define KEY_L               38
#define KEY_SEMICOLON       39      // ;
#define KEY_APOSTROPHE      40      // '
#define KEY_GRAVE           41      // ` (波浪号那个键)

#define KEY_BACKSLASH       43      // \ (反斜杠)
#define KEY_Z               44
#define KEY_X               45
#define KEY_C               46
#define KEY_V               47
#define KEY_B               48
#define KEY_N               49
#define KEY_M               50
#define KEY_COMMA           51      // ,
#define KEY_DOT             52      // .
#define KEY_SLASH           53      // /

#define KEY_SPACE           57

// ==========================================
// 2. 控制键与修饰键 (Modifiers)
// ==========================================
#define KEY_LEFTCTRL        29
#define KEY_LEFTSHIFT       42
#define KEY_RIGHTSHIFT      54
#define KEY_LEFTALT         56
#define KEY_CAPSLOCK        58
#define KEY_RIGHTCTRL       97
#define KEY_RIGHTALT        100
#define KEY_LEFTMETA        125     // 左 Windows/Super 键
#define KEY_RIGHTMETA       126     // 右 Windows/Super 键
#define KEY_COMPOSE         127     // 菜单键 (右侧 Win 和 Ctrl 之间)

// ==========================================
// 3. 功能键区 (Function Keys)
// ==========================================
#define KEY_F1              59
#define KEY_F2              60
#define KEY_F3              61
#define KEY_F4              62
#define KEY_F5              63
#define KEY_F6              64
#define KEY_F7              65
#define KEY_F8              66
#define KEY_F9              67
#define KEY_F10             68
#define KEY_F11             87
#define KEY_F12             88

// ==========================================
// 4. 控制面板区 (Navigation & Control)
// ==========================================
#define KEY_SYSRQ           99      // Print Screen / SysRq
#define KEY_SCROLLLOCK      70
#define KEY_PAUSE           119
#define KEY_INSERT          110
#define KEY_HOME            102
#define KEY_PAGEUP          104
#define KEY_DELETE          111
#define KEY_END             107
#define KEY_PAGEDOWN        109
#define KEY_UP              103
#define KEY_LEFT            105
#define KEY_RIGHT           106
#define KEY_DOWN            108

// ==========================================
// 5. 数字小键盘区 (Numpad)
// ==========================================
#define KEY_NUMLOCK         69
#define KEY_KPSLASH         98      // 小键盘 /
#define KEY_KPASTERISK      55      // 小键盘 *
#define KEY_KPMINUS         74      // 小键盘 -
#define KEY_KPPLUS          78      // 小键盘 +
#define KEY_KPENTER         96      // 小键盘 Enter
#define KEY_KPDOT           83      // 小键盘 .
#define KEY_KP0             82
#define KEY_KP1             79
#define KEY_KP2             80
#define KEY_KP3             81
#define KEY_KP4             75
#define KEY_KP5             76
#define KEY_KP6             77
#define KEY_KP7             71
#define KEY_KP8             72
#define KEY_KP9             73

// ==========================================
// 6. 鼠标按键 (复用 Key Code 体系)
// ==========================================
#define BTN_MOUSE           0x110
#define BTN_LEFT            0x110
#define BTN_RIGHT           0x111
#define BTN_MIDDLE          0x112
#define BTN_SIDE            0x113
#define BTN_EXTRA           0x114

// ==========================================
// 7. 最大值定义 (用于能力位图数组计算)
// ==========================================
// 标准按键到此为止，128~255 以及以上通常是多媒体键(音量加减等)、
// 手柄按键(BTN_GAMEPAD)和一些极其特殊的硬件开关。
#define KEY_MAX             0x2FF   // 767
#define KEY_CNT             (KEY_MAX + 1)