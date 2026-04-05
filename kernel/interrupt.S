.section .text "ax"

# 宏：没有错误码的中断 (如除零、定时器、所有外部 MSI 中断)
.macro ISR_NOERRCODE vector
.global isr\vector
isr\vector:
    pushq $0            # 压入假错误码，对齐堆栈
    pushq $\vector      # 压入中断号
    jmp isr_common_stub
.endm

# 宏：带有错误码的 CPU 异常 (如 8, 10-14, 17, 21 等)
.macro ISR_ERRCODE vector
.global isr\vector
isr\vector:
    # CPU 已经压入了错误码，直接压中断号即可
    pushq $\vector
    jmp isr_common_stub
.endm

# ========================================================
# 1. 批量生成 256 个中断入口桩
# ========================================================
ISR_NOERRCODE 0   #DE	Divide Error (除零)	Fault
ISR_NOERRCODE 1   #DB	Debug (调试)	Fault/Trap
ISR_NOERRCODE 2   #-	NMI (不可屏蔽中断)	Interrupt
ISR_NOERRCODE 3   #BP	Breakpoint (断点)	Trap
ISR_NOERRCODE 4   #OF	Overflow (溢出)	Trap
ISR_NOERRCODE 5   #BR	BOUND Range Exceeded	Fault
ISR_NOERRCODE 6   #UD	Invalid Opcode (非法指令)	Fault
ISR_NOERRCODE 7   #NM	Device Not Available (FPU不可用)
ISR_ERRCODE   8   #DF	Double Fault (双重故障)	Abort
ISR_NOERRCODE 9   #-	Coprocessor Segment Overrun (废弃)	Fault
ISR_ERRCODE   10  #TS	Invalid TSS (无效TSS)	Fault
ISR_ERRCODE   11  #NP	Segment Not Present (段不存在)	Fault
ISR_ERRCODE   12  #SS	Stack-Segment Fault (栈段错误)	Fault
ISR_ERRCODE   13  #GP	General Protection (常规保护错误)	Fault
ISR_ERRCODE   14  #PF	Page Fault (页错误)	Fault
ISR_NOERRCODE 15  #-	Intel Reserved (保留)	-
ISR_NOERRCODE 16  #MF	x87 FPU Floating-Point Error	Fault
ISR_ERRCODE   17  #AC	Alignment Check (对齐检查)	Fault
ISR_NOERRCODE 18  #MC	Machine Check (机器检查)	Abort
ISR_NOERRCODE 19  #XM	SIMD Floating-Point Exception	Fault
ISR_NOERRCODE 20  #VE	Virtualization Exception	Fault
ISR_ERRCODE   21  #CP	Control Protection Exception	Fault
ISR_NOERRCODE 22  #保留
ISR_NOERRCODE 23  #保留
ISR_NOERRCODE 24  #保留
ISR_NOERRCODE 25  #保留
ISR_NOERRCODE 26  #保留
ISR_NOERRCODE 27  #保留
ISR_NOERRCODE 28  #保留
ISR_NOERRCODE 29  #保留
ISR_NOERRCODE 30  #保留
ISR_NOERRCODE 31  #保留

# 32~255 全部是外部硬件中断 (无错误码)
ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47
ISR_NOERRCODE 48
ISR_NOERRCODE 49
ISR_NOERRCODE 50
ISR_NOERRCODE 51
ISR_NOERRCODE 52
ISR_NOERRCODE 53
ISR_NOERRCODE 54
ISR_NOERRCODE 55
ISR_NOERRCODE 56
ISR_NOERRCODE 57
ISR_NOERRCODE 58
ISR_NOERRCODE 59
ISR_NOERRCODE 60
ISR_NOERRCODE 61
ISR_NOERRCODE 62
ISR_NOERRCODE 63
ISR_NOERRCODE 64
ISR_NOERRCODE 65
ISR_NOERRCODE 66
ISR_NOERRCODE 67
ISR_NOERRCODE 68
ISR_NOERRCODE 69
ISR_NOERRCODE 70
ISR_NOERRCODE 71
ISR_NOERRCODE 72
ISR_NOERRCODE 73
ISR_NOERRCODE 74
ISR_NOERRCODE 75
ISR_NOERRCODE 76
ISR_NOERRCODE 77
ISR_NOERRCODE 78
ISR_NOERRCODE 79
ISR_NOERRCODE 80
ISR_NOERRCODE 81
ISR_NOERRCODE 82
ISR_NOERRCODE 83
ISR_NOERRCODE 84
ISR_NOERRCODE 85
ISR_NOERRCODE 86
ISR_NOERRCODE 87
ISR_NOERRCODE 88
ISR_NOERRCODE 89
ISR_NOERRCODE 90
ISR_NOERRCODE 91
ISR_NOERRCODE 92
ISR_NOERRCODE 93
ISR_NOERRCODE 94
ISR_NOERRCODE 95
ISR_NOERRCODE 96
ISR_NOERRCODE 97
ISR_NOERRCODE 98
ISR_NOERRCODE 99
ISR_NOERRCODE 100
ISR_NOERRCODE 101
ISR_NOERRCODE 102
ISR_NOERRCODE 103
ISR_NOERRCODE 104
ISR_NOERRCODE 105
ISR_NOERRCODE 106
ISR_NOERRCODE 107
ISR_NOERRCODE 108
ISR_NOERRCODE 109
ISR_NOERRCODE 110
ISR_NOERRCODE 111
ISR_NOERRCODE 112
ISR_NOERRCODE 113
ISR_NOERRCODE 114
ISR_NOERRCODE 115
ISR_NOERRCODE 116
ISR_NOERRCODE 117
ISR_NOERRCODE 118
ISR_NOERRCODE 119
ISR_NOERRCODE 120
ISR_NOERRCODE 121
ISR_NOERRCODE 122
ISR_NOERRCODE 123
ISR_NOERRCODE 124
ISR_NOERRCODE 125
ISR_NOERRCODE 126
ISR_NOERRCODE 127
ISR_NOERRCODE 128
ISR_NOERRCODE 129
ISR_NOERRCODE 130
ISR_NOERRCODE 131
ISR_NOERRCODE 132
ISR_NOERRCODE 133
ISR_NOERRCODE 134
ISR_NOERRCODE 135
ISR_NOERRCODE 136
ISR_NOERRCODE 137
ISR_NOERRCODE 138
ISR_NOERRCODE 139
ISR_NOERRCODE 140
ISR_NOERRCODE 141
ISR_NOERRCODE 142
ISR_NOERRCODE 143
ISR_NOERRCODE 144
ISR_NOERRCODE 145
ISR_NOERRCODE 146
ISR_NOERRCODE 147
ISR_NOERRCODE 148
ISR_NOERRCODE 149
ISR_NOERRCODE 150
ISR_NOERRCODE 151
ISR_NOERRCODE 152
ISR_NOERRCODE 153
ISR_NOERRCODE 154
ISR_NOERRCODE 155
ISR_NOERRCODE 156
ISR_NOERRCODE 157
ISR_NOERRCODE 158
ISR_NOERRCODE 159
ISR_NOERRCODE 160
ISR_NOERRCODE 161
ISR_NOERRCODE 162
ISR_NOERRCODE 163
ISR_NOERRCODE 164
ISR_NOERRCODE 165
ISR_NOERRCODE 166
ISR_NOERRCODE 167
ISR_NOERRCODE 168
ISR_NOERRCODE 169
ISR_NOERRCODE 170
ISR_NOERRCODE 171
ISR_NOERRCODE 172
ISR_NOERRCODE 173
ISR_NOERRCODE 174
ISR_NOERRCODE 175
ISR_NOERRCODE 176
ISR_NOERRCODE 177
ISR_NOERRCODE 178
ISR_NOERRCODE 179
ISR_NOERRCODE 180
ISR_NOERRCODE 181
ISR_NOERRCODE 182
ISR_NOERRCODE 183
ISR_NOERRCODE 184
ISR_NOERRCODE 185
ISR_NOERRCODE 186
ISR_NOERRCODE 187
ISR_NOERRCODE 188
ISR_NOERRCODE 189
ISR_NOERRCODE 190
ISR_NOERRCODE 191
ISR_NOERRCODE 192
ISR_NOERRCODE 193
ISR_NOERRCODE 194
ISR_NOERRCODE 195
ISR_NOERRCODE 196
ISR_NOERRCODE 197
ISR_NOERRCODE 198
ISR_NOERRCODE 199
ISR_NOERRCODE 200
ISR_NOERRCODE 201
ISR_NOERRCODE 202
ISR_NOERRCODE 203
ISR_NOERRCODE 204
ISR_NOERRCODE 205
ISR_NOERRCODE 206
ISR_NOERRCODE 207
ISR_NOERRCODE 208
ISR_NOERRCODE 209
ISR_NOERRCODE 210
ISR_NOERRCODE 211
ISR_NOERRCODE 212
ISR_NOERRCODE 213
ISR_NOERRCODE 214
ISR_NOERRCODE 215
ISR_NOERRCODE 216
ISR_NOERRCODE 217
ISR_NOERRCODE 218
ISR_NOERRCODE 219
ISR_NOERRCODE 220
ISR_NOERRCODE 221
ISR_NOERRCODE 222
ISR_NOERRCODE 223
ISR_NOERRCODE 224
ISR_NOERRCODE 225
ISR_NOERRCODE 226
ISR_NOERRCODE 227
ISR_NOERRCODE 228
ISR_NOERRCODE 229
ISR_NOERRCODE 230
ISR_NOERRCODE 231
ISR_NOERRCODE 232
ISR_NOERRCODE 233
ISR_NOERRCODE 234
ISR_NOERRCODE 235
ISR_NOERRCODE 236
ISR_NOERRCODE 237
ISR_NOERRCODE 238
ISR_NOERRCODE 239
ISR_NOERRCODE 240
ISR_NOERRCODE 241
ISR_NOERRCODE 242
ISR_NOERRCODE 243
ISR_NOERRCODE 244
ISR_NOERRCODE 245
ISR_NOERRCODE 246
ISR_NOERRCODE 247
ISR_NOERRCODE 248
ISR_NOERRCODE 249
ISR_NOERRCODE 250
ISR_NOERRCODE 251
ISR_NOERRCODE 252
ISR_NOERRCODE 253
ISR_NOERRCODE 254
ISR_NOERRCODE 255

# ========================================================
# 2. 公共大一统跳板：保护现场 -> 调用 C -> 恢复现场
# ========================================================
.extern c_interrupt_dispatcher  # 声明 C 语言大管家

isr_common_stub:
    # 保存所有通用寄存器 (按照 cpu_registers_t 的顺序)
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    # 此时 %rsp 完美指向 cpu_registers_t 结构的起始位置
    # System V AMD64 ABI 规定：第一个参数用 %rdi 传递
    movq %rsp, %rdi

    # 呼叫 C 语言中枢
    call c_interrupt_dispatcher

    # 恢复现场
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    # 跨过我们在桩里手动压入的 int_no 和 err_code (共 16 字节)
    addq $16, %rsp

    # x64 硬件级中断返回，恢复 RIP, CS, RFLAGS, RSP, SS
    iretq

# ========================================================
# 👑 3. 架构师黑科技：中断处理函数指针表
# 将刚才生成的 256 个标号地址打包成一个数组，C 语言直接遍历即可！
# ========================================================
.section    .data, "wa"
.global isr_stub_table
isr_stub_table:
    .quad isr0
    .quad isr1
    .quad isr2
    .quad isr3
    .quad isr4
    .quad isr5
    .quad isr6
    .quad isr7
    .quad isr8
    .quad isr9
    .quad isr10
    .quad isr11
    .quad isr12
    .quad isr13
    .quad isr14
    .quad isr15
    .quad isr16
    .quad isr17
    .quad isr18
    .quad isr19
    .quad isr20
    .quad isr21
    .quad isr22
    .quad isr23
    .quad isr24
    .quad isr25
    .quad isr26
    .quad isr27
    .quad isr28
    .quad isr29
    .quad isr30
    .quad isr31
    .quad isr32
    .quad isr33
    .quad isr34
    .quad isr35
    .quad isr36
    .quad isr37
    .quad isr38
    .quad isr39
    .quad isr40
    .quad isr41
    .quad isr42
    .quad isr43
    .quad isr44
    .quad isr45
    .quad isr46
    .quad isr47
    .quad isr48
    .quad isr49
    .quad isr50
    .quad isr51
    .quad isr52
    .quad isr53
    .quad isr54
    .quad isr55
    .quad isr56
    .quad isr57
    .quad isr58
    .quad isr59
    .quad isr60
    .quad isr61
    .quad isr62
    .quad isr63
    .quad isr64
    .quad isr65
    .quad isr66
    .quad isr67
    .quad isr68
    .quad isr69
    .quad isr70
    .quad isr71
    .quad isr72
    .quad isr73
    .quad isr74
    .quad isr75
    .quad isr76
    .quad isr77
    .quad isr78
    .quad isr79
    .quad isr80
    .quad isr81
    .quad isr82
    .quad isr83
    .quad isr84
    .quad isr85
    .quad isr86
    .quad isr87
    .quad isr88
    .quad isr89
    .quad isr90
    .quad isr91
    .quad isr92
    .quad isr93
    .quad isr94
    .quad isr95
    .quad isr96
    .quad isr97
    .quad isr98
    .quad isr99
    .quad isr100
    .quad isr101
    .quad isr102
    .quad isr103
    .quad isr104
    .quad isr105
    .quad isr106
    .quad isr107
    .quad isr108
    .quad isr109
    .quad isr110
    .quad isr111
    .quad isr112
    .quad isr113
    .quad isr114
    .quad isr115
    .quad isr116
    .quad isr117
    .quad isr118
    .quad isr119
    .quad isr120
    .quad isr121
    .quad isr122
    .quad isr123
    .quad isr124
    .quad isr125
    .quad isr126
    .quad isr127
    .quad isr128
    .quad isr129
    .quad isr130
    .quad isr131
    .quad isr132
    .quad isr133
    .quad isr134
    .quad isr135
    .quad isr136
    .quad isr137
    .quad isr138
    .quad isr139
    .quad isr140
    .quad isr141
    .quad isr142
    .quad isr143
    .quad isr144
    .quad isr145
    .quad isr146
    .quad isr147
    .quad isr148
    .quad isr149
    .quad isr150
    .quad isr151
    .quad isr152
    .quad isr153
    .quad isr154
    .quad isr155
    .quad isr156
    .quad isr157
    .quad isr158
    .quad isr159
    .quad isr160
    .quad isr161
    .quad isr162
    .quad isr163
    .quad isr164
    .quad isr165
    .quad isr166
    .quad isr167
    .quad isr168
    .quad isr169
    .quad isr170
    .quad isr171
    .quad isr172
    .quad isr173
    .quad isr174
    .quad isr175
    .quad isr176
    .quad isr177
    .quad isr178
    .quad isr179
    .quad isr180
    .quad isr181
    .quad isr182
    .quad isr183
    .quad isr184
    .quad isr185
    .quad isr186
    .quad isr187
    .quad isr188
    .quad isr189
    .quad isr190
    .quad isr191
    .quad isr192
    .quad isr193
    .quad isr194
    .quad isr195
    .quad isr196
    .quad isr197
    .quad isr198
    .quad isr199
    .quad isr200
    .quad isr201
    .quad isr202
    .quad isr203
    .quad isr204
    .quad isr205
    .quad isr206
    .quad isr207
    .quad isr208
    .quad isr209
    .quad isr210
    .quad isr211
    .quad isr212
    .quad isr213
    .quad isr214
    .quad isr215
    .quad isr216
    .quad isr217
    .quad isr218
    .quad isr219
    .quad isr220
    .quad isr221
    .quad isr222
    .quad isr223
    .quad isr224
    .quad isr225
    .quad isr226
    .quad isr227
    .quad isr228
    .quad isr229
    .quad isr230
    .quad isr231
    .quad isr232
    .quad isr233
    .quad isr234
    .quad isr235
    .quad isr236
    .quad isr237
    .quad isr238
    .quad isr239
    .quad isr240
    .quad isr241
    .quad isr242
    .quad isr243
    .quad isr244
    .quad isr245
    .quad isr246
    .quad isr247
    .quad isr248
    .quad isr249
    .quad isr250
    .quad isr251
    .quad isr252
    .quad isr253
    .quad isr254
    .quad isr255