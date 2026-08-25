	.arch armv8-a
	.text
	.p2align 2
	.global func
	.type func, %function
func:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #448
	stp x21, x22, [sp, #16]
	add x21, sp, #48
	stp x19, x20, [sp]
	str x23, [sp, #32]
	mov w20, w0
	mov w19, w1
	mov x23, x21
	movz w22, #0
	movz w2, #400
	mov x0, x23
	mov w1, w22
	bl memset
	cmp w19, #1
	b.gt .Lfunc_bb1
.Lfunc_bb108:
	movi v18.4s, #0
.Lfunc_bb100:
	orr w9, wzr, #0x7ffffff8
	cmp w22, w9
	cset w10, le
	cmp w22, #93
	cset w9, lt
	and w9, w10, w9
	cbz w9, .Lfunc_bb102
.Lfunc_bb101:
	ldp q17, q16, [x23]
	add v17.4s, v18.4s, v17.4s
	add v18.4s, v17.4s, v16.4s
	add x9, x23, #16
	add w22, w22, #8
	add x23, x9, #16
	b .Lfunc_bb100
.Lfunc_bb1:
	movz w9, #1
	str w9, [x21, #4]
	cmp w19, #2
	b.le .Lfunc_bb109
.Lfunc_bb2:
	movz w9, #2
	str w9, [x21, #8]
	cmp w19, #3
	b.le .Lfunc_bb110
.Lfunc_bb3:
	movz w9, #3
	str w9, [x21, #12]
	cmp w19, #4
	b.le .Lfunc_bb111
.Lfunc_bb4:
	movz w9, #4
	str w9, [x21, #16]
	cmp w19, #5
	b.le .Lfunc_bb112
.Lfunc_bb5:
	movz w9, #5
	str w9, [x21, #20]
	cmp w19, #6
	b.le .Lfunc_bb113
.Lfunc_bb6:
	movz w9, #6
	str w9, [x21, #24]
	cmp w19, #7
	b.le .Lfunc_bb114
.Lfunc_bb7:
	movz w9, #7
	str w9, [x21, #28]
	cmp w19, #8
	b.le .Lfunc_bb115
.Lfunc_bb8:
	movz w9, #8
	str w9, [x21, #32]
	cmp w19, #9
	b.le .Lfunc_bb116
.Lfunc_bb9:
	movz w9, #9
	str w9, [x21, #36]
	cmp w19, #10
	b.le .Lfunc_bb117
.Lfunc_bb10:
	movz w9, #10
	str w9, [x21, #40]
	cmp w19, #11
	b.le .Lfunc_bb118
.Lfunc_bb11:
	movz w9, #11
	str w9, [x21, #44]
	cmp w19, #12
	b.le .Lfunc_bb119
.Lfunc_bb12:
	movz w9, #12
	str w9, [x21, #48]
	cmp w19, #13
	b.le .Lfunc_bb120
.Lfunc_bb13:
	movz w9, #13
	str w9, [x21, #52]
	cmp w19, #14
	b.le .Lfunc_bb121
.Lfunc_bb14:
	movz w9, #14
	str w9, [x21, #56]
	cmp w19, #15
	b.le .Lfunc_bb122
.Lfunc_bb15:
	movz w9, #15
	str w9, [x21, #60]
	cmp w19, #16
	b.le .Lfunc_bb123
.Lfunc_bb16:
	movz w9, #16
	str w9, [x21, #64]
	cmp w19, #17
	b.le .Lfunc_bb124
.Lfunc_bb17:
	movz w9, #17
	str w9, [x21, #68]
	cmp w19, #18
	b.le .Lfunc_bb125
.Lfunc_bb18:
	movz w9, #18
	str w9, [x21, #72]
	cmp w19, #19
	b.le .Lfunc_bb126
.Lfunc_bb19:
	movz w9, #19
	str w9, [x21, #76]
	cmp w19, #20
	b.le .Lfunc_bb127
.Lfunc_bb20:
	movz w9, #20
	str w9, [x21, #80]
	cmp w19, #21
	b.le .Lfunc_bb128
.Lfunc_bb21:
	movz w9, #21
	str w9, [x21, #84]
	cmp w19, #22
	b.le .Lfunc_bb129
.Lfunc_bb22:
	movz w9, #22
	str w9, [x21, #88]
	cmp w19, #23
	b.le .Lfunc_bb130
.Lfunc_bb23:
	movz w9, #23
	str w9, [x21, #92]
	cmp w19, #24
	b.le .Lfunc_bb131
.Lfunc_bb24:
	movz w9, #24
	str w9, [x21, #96]
	cmp w19, #25
	b.le .Lfunc_bb132
.Lfunc_bb25:
	movz w9, #25
	str w9, [x21, #100]
	cmp w19, #26
	b.le .Lfunc_bb133
.Lfunc_bb26:
	movz w9, #26
	str w9, [x21, #104]
	cmp w19, #27
	b.le .Lfunc_bb134
.Lfunc_bb27:
	movz w9, #27
	str w9, [x21, #108]
	cmp w19, #28
	b.le .Lfunc_bb135
.Lfunc_bb28:
	movz w9, #28
	str w9, [x21, #112]
	cmp w19, #29
	b.le .Lfunc_bb136
.Lfunc_bb29:
	movz w9, #29
	str w9, [x21, #116]
	cmp w19, #30
	b.le .Lfunc_bb137
.Lfunc_bb30:
	movz w9, #30
	str w9, [x21, #120]
	cmp w19, #31
	b.le .Lfunc_bb138
.Lfunc_bb31:
	movz w9, #31
	str w9, [x21, #124]
	cmp w19, #32
	b.le .Lfunc_bb139
.Lfunc_bb32:
	movz w9, #32
	str w9, [x21, #128]
	cmp w19, #33
	b.le .Lfunc_bb140
.Lfunc_bb33:
	movz w9, #33
	str w9, [x21, #132]
	cmp w19, #34
	b.le .Lfunc_bb141
.Lfunc_bb34:
	movz w9, #34
	str w9, [x21, #136]
	cmp w19, #35
	b.le .Lfunc_bb142
.Lfunc_bb35:
	movz w9, #35
	str w9, [x21, #140]
	cmp w19, #36
	b.le .Lfunc_bb143
.Lfunc_bb36:
	movz w9, #36
	str w9, [x21, #144]
	cmp w19, #37
	b.le .Lfunc_bb144
.Lfunc_bb37:
	movz w9, #37
	str w9, [x21, #148]
	cmp w19, #38
	b.le .Lfunc_bb145
.Lfunc_bb38:
	movz w9, #38
	str w9, [x21, #152]
	cmp w19, #39
	b.le .Lfunc_bb146
.Lfunc_bb39:
	movz w9, #39
	str w9, [x21, #156]
	cmp w19, #40
	b.le .Lfunc_bb147
.Lfunc_bb40:
	movz w9, #40
	str w9, [x21, #160]
	cmp w19, #41
	b.le .Lfunc_bb148
.Lfunc_bb41:
	movz w9, #41
	str w9, [x21, #164]
	cmp w19, #42
	b.le .Lfunc_bb149
.Lfunc_bb42:
	movz w9, #42
	str w9, [x21, #168]
	cmp w19, #43
	b.le .Lfunc_bb150
.Lfunc_bb43:
	movz w9, #43
	str w9, [x21, #172]
	cmp w19, #44
	b.le .Lfunc_bb151
.Lfunc_bb44:
	movz w9, #44
	str w9, [x21, #176]
	cmp w19, #45
	b.le .Lfunc_bb152
.Lfunc_bb45:
	movz w9, #45
	str w9, [x21, #180]
	cmp w19, #46
	b.le .Lfunc_bb153
.Lfunc_bb46:
	movz w9, #46
	str w9, [x21, #184]
	cmp w19, #47
	b.le .Lfunc_bb154
.Lfunc_bb47:
	movz w9, #47
	str w9, [x21, #188]
	cmp w19, #48
	b.le .Lfunc_bb155
.Lfunc_bb48:
	movz w9, #48
	str w9, [x21, #192]
	cmp w19, #49
	b.le .Lfunc_bb156
.Lfunc_bb49:
	movz w9, #49
	str w9, [x21, #196]
	cmp w19, #50
	b.le .Lfunc_bb157
.Lfunc_bb50:
	movz w9, #50
	str w9, [x21, #200]
	cmp w19, #51
	b.le .Lfunc_bb158
.Lfunc_bb51:
	movz w9, #51
	str w9, [x21, #204]
	cmp w19, #52
	b.le .Lfunc_bb159
.Lfunc_bb52:
	movz w9, #52
	str w9, [x21, #208]
	cmp w19, #53
	b.le .Lfunc_bb160
.Lfunc_bb53:
	movz w9, #53
	str w9, [x21, #212]
	cmp w19, #54
	b.le .Lfunc_bb161
.Lfunc_bb54:
	movz w9, #54
	str w9, [x21, #216]
	cmp w19, #55
	b.le .Lfunc_bb162
.Lfunc_bb55:
	movz w9, #55
	str w9, [x21, #220]
	cmp w19, #56
	b.le .Lfunc_bb163
.Lfunc_bb56:
	movz w9, #56
	str w9, [x21, #224]
	cmp w19, #57
	b.le .Lfunc_bb164
.Lfunc_bb57:
	movz w9, #57
	str w9, [x21, #228]
	cmp w19, #58
	b.le .Lfunc_bb165
.Lfunc_bb58:
	movz w9, #58
	str w9, [x21, #232]
	cmp w19, #59
	b.le .Lfunc_bb166
.Lfunc_bb59:
	movz w9, #59
	str w9, [x21, #236]
	cmp w19, #60
	b.le .Lfunc_bb167
.Lfunc_bb60:
	movz w9, #60
	str w9, [x21, #240]
	cmp w19, #61
	b.le .Lfunc_bb168
.Lfunc_bb61:
	movz w9, #61
	str w9, [x21, #244]
	cmp w19, #62
	b.le .Lfunc_bb169
.Lfunc_bb62:
	movz w9, #62
	str w9, [x21, #248]
	cmp w19, #63
	b.le .Lfunc_bb170
.Lfunc_bb63:
	movz w9, #63
	str w9, [x21, #252]
	cmp w19, #64
	b.le .Lfunc_bb171
.Lfunc_bb64:
	movz w9, #64
	str w9, [x21, #256]
	cmp w19, #65
	b.le .Lfunc_bb172
.Lfunc_bb65:
	movz w9, #65
	str w9, [x21, #260]
	cmp w19, #66
	b.le .Lfunc_bb173
.Lfunc_bb66:
	movz w9, #66
	str w9, [x21, #264]
	cmp w19, #67
	b.le .Lfunc_bb174
.Lfunc_bb67:
	movz w9, #67
	str w9, [x21, #268]
	cmp w19, #68
	b.le .Lfunc_bb175
.Lfunc_bb68:
	movz w9, #68
	str w9, [x21, #272]
	cmp w19, #69
	b.le .Lfunc_bb176
.Lfunc_bb69:
	movz w9, #69
	str w9, [x21, #276]
	cmp w19, #70
	b.le .Lfunc_bb177
.Lfunc_bb70:
	movz w9, #70
	str w9, [x21, #280]
	cmp w19, #71
	b.le .Lfunc_bb178
.Lfunc_bb71:
	movz w9, #71
	str w9, [x21, #284]
	cmp w19, #72
	b.le .Lfunc_bb179
.Lfunc_bb72:
	movz w9, #72
	str w9, [x21, #288]
	cmp w19, #73
	b.le .Lfunc_bb180
.Lfunc_bb73:
	movz w9, #73
	str w9, [x21, #292]
	cmp w19, #74
	b.le .Lfunc_bb181
.Lfunc_bb74:
	movz w9, #74
	str w9, [x21, #296]
	cmp w19, #75
	b.le .Lfunc_bb182
.Lfunc_bb75:
	movz w9, #75
	str w9, [x21, #300]
	cmp w19, #76
	b.le .Lfunc_bb183
.Lfunc_bb76:
	movz w9, #76
	str w9, [x21, #304]
	cmp w19, #77
	b.le .Lfunc_bb184
.Lfunc_bb77:
	movz w9, #77
	str w9, [x21, #308]
	cmp w19, #78
	b.le .Lfunc_bb185
.Lfunc_bb78:
	movz w9, #78
	str w9, [x21, #312]
	cmp w19, #79
	b.le .Lfunc_bb186
.Lfunc_bb79:
	movz w9, #79
	str w9, [x21, #316]
	cmp w19, #80
	b.le .Lfunc_bb187
.Lfunc_bb80:
	movz w9, #80
	str w9, [x21, #320]
	cmp w19, #81
	b.le .Lfunc_bb188
.Lfunc_bb81:
	movz w9, #81
	str w9, [x21, #324]
	cmp w19, #82
	b.le .Lfunc_bb189
.Lfunc_bb82:
	movz w9, #82
	str w9, [x21, #328]
	cmp w19, #83
	b.le .Lfunc_bb190
.Lfunc_bb83:
	movz w9, #83
	str w9, [x21, #332]
	cmp w19, #84
	b.le .Lfunc_bb191
.Lfunc_bb84:
	movz w9, #84
	str w9, [x21, #336]
	cmp w19, #85
	b.le .Lfunc_bb192
.Lfunc_bb85:
	movz w9, #85
	str w9, [x21, #340]
	cmp w19, #86
	b.le .Lfunc_bb193
.Lfunc_bb86:
	movz w9, #86
	str w9, [x21, #344]
	cmp w19, #87
	b.le .Lfunc_bb194
.Lfunc_bb87:
	movz w9, #87
	str w9, [x21, #348]
	cmp w19, #88
	b.le .Lfunc_bb195
.Lfunc_bb88:
	movz w9, #88
	str w9, [x21, #352]
	cmp w19, #89
	b.le .Lfunc_bb196
.Lfunc_bb89:
	movz w9, #89
	str w9, [x21, #356]
	cmp w19, #90
	b.le .Lfunc_bb197
.Lfunc_bb90:
	movz w9, #90
	str w9, [x21, #360]
	cmp w19, #91
	b.le .Lfunc_bb198
.Lfunc_bb91:
	movz w9, #91
	str w9, [x21, #364]
	cmp w19, #92
	b.le .Lfunc_bb199
.Lfunc_bb92:
	movz w9, #92
	str w9, [x21, #368]
	cmp w19, #93
	b.le .Lfunc_bb200
.Lfunc_bb93:
	movz w9, #93
	str w9, [x21, #372]
	cmp w19, #94
	b.le .Lfunc_bb201
.Lfunc_bb94:
	movz w9, #94
	str w9, [x21, #376]
	cmp w19, #95
	b.le .Lfunc_bb202
.Lfunc_bb95:
	movz w9, #95
	str w9, [x21, #380]
	cmp w19, #96
	b.le .Lfunc_bb203
.Lfunc_bb96:
	movz w9, #96
	str w9, [x21, #384]
	cmp w19, #97
	b.le .Lfunc_bb204
.Lfunc_bb97:
	movz w9, #97
	str w9, [x21, #388]
	cmp w19, #98
	b.le .Lfunc_bb205
.Lfunc_bb98:
	movz w9, #98
	str w9, [x21, #392]
	cmp w19, #99
	b.gt .Lfunc_bb99
.Lfunc_bb206:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb99:
	movz w9, #99
	movi v18.4s, #0
	str w9, [x21, #396]
	b .Lfunc_bb100
.Lfunc_bb102:
	addv s16, v18.4s
	fmov w15, s16
	mov w13, w22
.Lfunc_bb103:
	cmp w13, #97
	b.ge .Lfunc_bb207
.Lfunc_bb107:
	add w9, w13, #1
	ldr w11, [x21, w9, sxtw #2]
	add w9, w13, #2
	ldr w10, [x21, w9, sxtw #2]
	ldr w12, [x21, w13, sxtw #2]
	add w9, w13, #3
	ldr w9, [x21, w9, sxtw #2]
	add w12, w15, w12
	add w11, w12, w11
	add w10, w11, w10
	add w15, w10, w9
	add w13, w13, #4
	b .Lfunc_bb103
.Lfunc_bb104:
	cmp w11, #100
	b.ge .Lfunc_bb106
.Lfunc_bb105:
	ldr w9, [x21, w11, sxtw #2]
	add w15, w15, w9
	add w11, w11, #1
	b .Lfunc_bb104
.Lfunc_bb106:
	adrp x14, __sysy_par_ctx_0_0
	adrp x13, __sysy_par_ctx_0_1
	movz w1, #0
	adrp x12, __sysy_par_scalar_start_0
	adrp x11, __sysy_par_scalar_bound_0
	adrp x10, __sysy_par_scalar_partial_0_0
	adrp x9, __sysy_par_scalar_partial_0_1
	str w19, [x14, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	str w15, [x13, :lo12:__sysy_par_ctx_0_1]
	mov w2, w20
	str w1, [x12, :lo12:__sysy_par_scalar_start_0]
	str w20, [x11, :lo12:__sysy_par_scalar_bound_0]
	str w1, [x10, :lo12:__sysy_par_scalar_partial_0_0]
	str w1, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	bl __sysy_parallel_for
	adrp x9, __sysy_par_scalar_partial_0_1
	ldr w15, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	adrp x9, __sysy_par_scalar_partial_0_0
	ldr w16, [x9, :lo12:__sysy_par_scalar_partial_0_0]
	cmp w15, #0
	movz w13, #65535
	cset w14, ge
	sub w9, w13, w15
	sub w10, w16, w9
	add w12, w16, w15
	cmp w16, w9
	movn w9, #65534
	csel w11, w10, w12, ge
	sub w10, w9, w15
	sub w9, w16, w10
	cmp w16, w10
	csel w9, w9, w12, le
	cmp w14, #0
	csel w10, w11, w9, ne
	movz w9, #32769
	movk w9, #32768, lsl #16
	smull x9, w10, w9
	asr x9, x9, #32
	add w9, w9, w10
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	asr w9, w9, #15
	ldr x19, [sp]
	add w9, w9, w9, lsr #31
	msub w0, w9, w13, w10
	add sp, sp, #448
	ldp xzr, x30, [sp], #16
	ret
.Lfunc_bb109:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb110:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb111:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb112:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb113:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb114:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb115:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb116:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb117:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb118:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb119:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb120:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb121:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb122:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb123:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb124:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb125:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb126:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb127:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb128:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb129:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb130:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb131:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb132:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb133:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb134:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb135:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb136:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb137:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb138:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb139:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb140:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb141:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb142:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb143:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb144:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb145:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb146:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb147:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb148:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb149:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb150:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb151:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb152:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb153:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb154:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb155:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb156:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb157:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb158:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb159:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb160:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb161:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb162:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb163:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb164:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb165:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb166:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb167:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb168:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb169:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb170:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb171:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb172:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb173:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb174:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb175:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb176:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb177:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb178:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb179:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb180:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb181:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb182:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb183:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb184:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb185:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb186:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb187:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb188:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb189:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb190:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb191:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb192:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb193:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb194:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb195:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb196:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb197:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb198:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb199:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb200:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb201:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb202:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb203:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb204:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb205:
	movi v18.4s, #0
	b .Lfunc_bb100
.Lfunc_bb207:
	mov w11, w13
	b .Lfunc_bb104
	.size func, .-func
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #16
	str x19, [sp]
	movz w0, #397
	bl _sysy_starttime
	bl getint
	mov w19, w0
	bl getint
	mov w1, w0
	mov w0, w19
	bl func
	bl putint
	movz w0, #10
	bl putch
	movz w0, #402
	bl _sysy_stoptime
	movz w0, #0
	ldr x19, [sp]
	add sp, sp, #16
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x10, __sysy_par_ctx_0_1
	ldr w8, [x10, :lo12:__sysy_par_ctx_0_1]
	adrp x9, __sysy_par_scalar_start_0
	ldr w15, [x9, :lo12:__sysy_par_scalar_start_0]
	mov w16, w0
	mov w17, w1
	movz w10, #32769
	movz w6, #0
	sub w14, w17, #3
	mov w13, w16
	orr w12, wzr, #0x80000003
	movz w11, #65535
	movk w10, #32768, lsl #16
.L__sysy_par_body_0_bb1:
	cmp w13, w14
	cset w7, lt
	cmp w17, w12
	cset w9, ge
	and w9, w9, w7
	cbz w9, .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb5:
	add w7, w6, w8
	smull x9, w7, w10
	asr x9, x9, #32
	add w9, w9, w7
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w9, w9, w11, w7
	add w7, w9, w8
	smull x9, w7, w10
	asr x9, x9, #32
	add w9, w9, w7
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w9, w9, w11, w7
	add w7, w9, w8
	smull x9, w7, w10
	asr x9, x9, #32
	add w9, w9, w7
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w9, w9, w11, w7
	add w7, w9, w8
	smull x9, w7, w10
	asr x9, x9, #32
	add w9, w9, w7
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w6, w9, w11, w7
	add w13, w13, #4
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb2:
	cmp w13, w17
	b.ge .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb4:
	add w11, w11, w8
	smull x9, w11, w10
	asr x9, x9, #32
	add w9, w9, w11
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w11, w9, w12, w11
	add w13, w13, #1
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	adrp x10, __sysy_par_scalar_partial_0_0
	adrp x9, __sysy_par_scalar_partial_0_1
	cmp w16, w15
	add x10, x10, :lo12:__sysy_par_scalar_partial_0_0
	add x9, x9, :lo12:__sysy_par_scalar_partial_0_1
	csel x9, x10, x9, eq
	str w11, [x9]
	ret
.L__sysy_par_body_0_bb6:
	movz w10, #32769
	mov w11, w6
	movz w12, #65535
	movk w10, #32768, lsl #16
	b .L__sysy_par_body_0_bb2
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_ctx_0_1
	.p2align 2
__sysy_par_ctx_0_1:
	.zero 4
	.global __sysy_par_scalar_start_0
	.p2align 2
__sysy_par_scalar_start_0:
	.zero 4
	.global __sysy_par_scalar_bound_0
	.p2align 2
__sysy_par_scalar_bound_0:
	.zero 4
	.global __sysy_par_scalar_partial_0_0
	.p2align 2
__sysy_par_scalar_partial_0_0:
	.zero 4
	.global __sysy_par_scalar_partial_0_1
	.p2align 2
__sysy_par_scalar_partial_0_1:
	.zero 4

	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0

	.arch armv8-a
	.file	"par_runtime_only.c"
	.text
	.align	2
	.p2align 4,,11
	.type	__sysy_bind_cpu.part.0, %function
__sysy_bind_cpu.part.0:
.LFB3:
	.cfi_startproc
	adrp	x5, .LANCHOR0
	mov	x1, 0
	add	x5, x5, :lo12:.LANCHOR0
	mov	w4, 0
	b	.L5
	.p2align 2,,3
.L3:
	add	w4, w4, 1
.L2:
	cmp	x1, 1024
	beq	.L15
.L5:
	lsr	x2, x1, 6
	and	w3, w1, 63
	add	x1, x1, 1
	lsl	x6, x2, 3
	ldr	x2, [x5, x2, lsl 3]
	lsr	x2, x2, x3
	tbz	x2, 0, .L2
	cmp	w0, w4
	bne	.L3
	stp	x29, x30, [sp, -144]!
	.cfi_def_cfa_offset 144
	.cfi_offset 29, -144
	.cfi_offset 30, -136
	mov	x4, 1
	lsl	x4, x4, x3
	movi	v0.4s, 0
	add	x2, sp, 16
	mov	x29, sp
	mov	x1, 128
	mov	w0, 0
	stp	q0, q0, [x2]
	stp	q0, q0, [x2, 32]
	stp	q0, q0, [x2, 64]
	stp	q0, q0, [x2, 96]
	ldr	x3, [x2, x6]
	orr	x3, x3, x4
	str	x3, [x2, x6]
	bl	sched_setaffinity
	ldp	x29, x30, [sp], 144
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa_offset 0
	ret
	.p2align 2,,3
.L15:
	ret
	.cfi_endproc
.LFE3:
	.size	__sysy_bind_cpu.part.0, .-__sysy_bind_cpu.part.0
	.align	2
	.p2align 4,,11
	.type	__sysy_worker, %function
__sysy_worker:
.LFB1:
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	ldr	w0, [x0, 128]
	cbz	w0, .L17
	mov	w0, 3
	bl	__sysy_bind_cpu.part.0
.L17:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w19, 0
	add	x21, x20, 132
	add	x22, x20, 148
	.p2align 3,,7
.L18:
	ldar	w0, [x21]
	cmp	w0, w19
	beq	.L18
.L25:
	ldr	w0, [x20, 136]
	add	w19, w19, 1
	ldr	w1, [x20, 140]
	ldr	w2, [x20, 144]
	bl	__sysy_par_dispatch
	stlr	w19, [x22]
	ldar	w0, [x21]
	cmp	w0, w19
	beq	.L18
	b	.L25
	.cfi_endproc
.LFE1:
	.size	__sysy_worker, .-__sysy_worker
	.align	2
	.p2align 4,,11
	.global	__sysy_parallel_for
	.type	__sysy_parallel_for, %function
__sysy_parallel_for:
.LFB2:
	.cfi_startproc
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	mov	w20, w2
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	mov	w21, w1
	mov	w22, w0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	sub	w23, w2, w1
	cmp	w23, 1
	ble	.L33
	adrp	x19, .LANCHOR0
	add	x24, x19, :lo12:.LANCHOR0
	ldr	w0, [x24, 152]
	cbz	w0, .L28
	ldr	w0, [x24, 156]
.L29:
	cbz	w0, .L33
	add	x19, x19, :lo12:.LANCHOR0
	add	w2, w21, w23, asr 1
	mov	x0, x19
	str	w22, [x19, 136]
	str	w2, [x19, 140]
	str	w20, [x19, 144]
	ldr	w20, [x0, 132]!
	add	w20, w20, 1
	stlr	w20, [x0]
	mov	w0, w22
	mov	w1, w21
	bl	__sysy_par_dispatch
	add	x0, x19, 148
	.p2align 3,,7
.L34:
	ldar	w1, [x0]
	cmp	w1, w20
	bne	.L34
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	ret
	.p2align 2,,3
.L33:
	.cfi_restore_state
	mov	w2, w20
	mov	w1, w21
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	b	__sysy_par_dispatch
	.p2align 2,,3
.L28:
	.cfi_restore_state
	mov	x2, x24
	mov	x1, 128
	mov	w0, 0
	str	x25, [sp, 64]
	.cfi_offset 25, -16
	mov	w25, 1
	str	w25, [x24, 152]
	bl	sched_getaffinity
	cbnz	w0, .L30
	str	w25, [x24, 128]
.L31:
	mov	w0, 2
	bl	__sysy_bind_cpu.part.0
	b	.L32
	.p2align 2,,3
.L30:
	ldr	w0, [x24, 128]
	cbnz	w0, .L31
.L32:
	mov	w2, 3840
	adrp	x0, __sysy_worker
	movk	w2, 0x5, lsl 16
	add	x0, x0, :lo12:__sysy_worker
	adrp	x1, __sysy_wstack+1048576
	mov	x3, 0
	add	x1, x1, :lo12:__sysy_wstack+1048576
	bl	clone
	add	x1, x19, :lo12:.LANCHOR0
	cmp	w0, 0
	cset	w0, gt
	ldr	x25, [sp, 64]
	.cfi_restore 25
	str	w0, [x1, 156]
	b	.L29
	.cfi_endproc
.LFE2:
	.size	__sysy_parallel_for, .-__sysy_parallel_for
	.bss
	.align	4
	.set	.LANCHOR0,. + 0
	.type	__sysy_orig_mask, %object
	.size	__sysy_orig_mask, 128
__sysy_orig_mask:
	.zero	128
	.type	__sysy_orig_mask_valid, %object
	.size	__sysy_orig_mask_valid, 4
__sysy_orig_mask_valid:
	.zero	4
	.type	__sysy_job_seq, %object
	.size	__sysy_job_seq, 4
__sysy_job_seq:
	.zero	4
	.type	__sysy_job_id, %object
	.size	__sysy_job_id, 4
__sysy_job_id:
	.zero	4
	.type	__sysy_job_lo, %object
	.size	__sysy_job_lo, 4
__sysy_job_lo:
	.zero	4
	.type	__sysy_job_hi, %object
	.size	__sysy_job_hi, 4
__sysy_job_hi:
	.zero	4
	.type	__sysy_done_seq, %object
	.size	__sysy_done_seq, 4
__sysy_done_seq:
	.zero	4
	.type	__sysy_worker_started, %object
	.size	__sysy_worker_started, 4
__sysy_worker_started:
	.zero	4
	.type	__sysy_worker_ok, %object
	.size	__sysy_worker_ok, 4
__sysy_worker_ok:
	.zero	4
	.type	__sysy_wstack, %object
	.size	__sysy_wstack, 1048576
__sysy_wstack:
	.zero	1048576
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
