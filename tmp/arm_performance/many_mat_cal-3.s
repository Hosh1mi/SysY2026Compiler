	.text
	.global main
	.p2align 2
main:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #144
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	stp x28, x27, [sp, #64]
	stp d8, d9, [sp, #80]
	stp d10, d11, [sp, #96]
	str d12, [sp, #112]
	bl getint
	mov w28, w0
	bl getint
	add w10, w28, w28, lsr #31
	asr w10, w10, #1
	cmp w10, w28
	mov	w11, w10
	adrp x26, A
	str w0, [x29, #-8]
	str w10, [x29, #-24]
	csel w27, w11, w28, lt
	add x26, x26, :lo12:A
	movz w25, #0
main_label_while_cond_1:
	cmp w25, w27
	b.lt main_label_while_body_2
main_label_while_cond_6.loopexit:
	ldr w10, [x29, #-24]
	ldr w11, [x29, #-24]
	cmp w10, #0
	csel w20, w11, wzr, gt
	cmp w20, w28
	b.lt main_label_while_body_7.preheader
main_label_while_end_8:
	movz w0, #25
	bl _sysy_starttime
	ldr w11, [x29, #-24]
	adrp x10, __sysy_par_ctx_0_0
	mov w0, wzr
	mov w1, wzr
	str w11, [x10, :lo12:__sysy_par_ctx_0_0]
	adrp x10, __sysy_par_ctx_0_1
	str w28, [x10, :lo12:__sysy_par_ctx_0_1]
	mov w2, w28
	bl __sysy_parallel_for
	ldr w11, [x29, #-24]
	adrp x10, __sysy_par_ctx_1_0
	movz w0, #1
	mov w1, wzr
	str w11, [x10, :lo12:__sysy_par_ctx_1_0]
	adrp x10, __sysy_par_ctx_1_1
	str w28, [x10, :lo12:__sysy_par_ctx_1_1]
	mov w2, w28
	bl __sysy_parallel_for
	adrp x10, __sysy_par_ctx_2_0
	str w28, [x10, :lo12:__sysy_par_ctx_2_0]
	movz w0, #2
	mov w1, wzr
	mov w2, w28
	bl __sysy_parallel_for
	adrp x10, __sysy_par_ctx_3_0
	str w28, [x10, :lo12:__sysy_par_ctx_3_0]
	movz w0, #3
	mov w1, wzr
	mov w2, w28
	bl __sysy_parallel_for
	movz w24, #0
main_label_while_cond_39:
	cmp w24, w28
	b.lt main_label_80.preheader
main_label_while_cond_48.preheader:
	ldr w10, [x29, #-8]
	cmp w10, #0
	b.le .Lmain_edge_0
	movz w1, #0
	movz w4, #0
	b main_label_while_cond_51
.Lmain_edge_0:
	movz w19, #0
main_label_while_end_50:
	movz w0, #105
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	movz w0, #10
	bl putch
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_body_2:
	mov	x0, x26
	bl getarray
	movz x17, #4096
	add w25, w25, #1
	add x26, x26, x17
	b main_label_while_cond_1
main_label_while_body_7.preheader:
	adrp x19, B
	add x19, x19, :lo12:B
	sxtw x10, w20
	add x19, x19, x10, lsl #12
main_label_while_body_7:
	mov	x0, x19
	bl getarray
	add w20, w20, #1
	movz x17, #4096
	cmp w20, w28
	add x19, x19, x17
	b.ge main_label_while_end_8
	b main_label_while_body_7
main_label_while_cond_54.preheader:
	adrp x9, A
	add x9, x9, :lo12:A
	sxtw x10, w4
	add x9, x9, x10, lsl #12
	sub w3, w28, #3
	movz w2, #0
main_label_244:
	cmp w2, w3
	b.lt main_label_249
main_label_while_cond_54:
	cmp w2, w28
	b.lt main_label_while_body_55
main_label_while_end_56:
	add w4, w4, #1
main_label_while_cond_51:
	cmp w4, w28
	b.lt main_label_while_cond_54.preheader
main_label_while_end_53:
	ldr w10, [x29, #-8]
	mul w19, w1, w10
	b main_label_while_end_50
main_label_while_body_55:
	ldr	w0, [x9], #4
	add w2, w2, #1
	madd	w1, w0, w0, w1
	b main_label_while_cond_54
main_label_80.preheader:
	adrp x22, __mm_tmp_0_1024
	add x22, x22, :lo12:__mm_tmp_0_1024
	sub w23, w28, #3
	movi v12.4s, #0
	sub w21, w28, #7
	mov x20, x22
	movz w19, #0
main_label_270:
	cmp w19, w21
	b.lt main_label_274
main_label_139:
	cmp w19, w23
	b.lt main_label_142
main_label_146:
	cmp w19, w28
	b.lt main_label_83
main_label_149:
	adrp x7, C
	add x7, x7, :lo12:C
	sxtw x10, w24
	add x7, x7, x10, lsl #12
	movz w6, #0
main_label_86:
	cmp w6, w28
	b.lt main_label_91
main_label_95.preheader:
	adrp x9, A
	add x9, x9, :lo12:A
	sxtw x10, w24
	add x9, x9, x10, lsl #12
	movz w3, #0
main_label_281:
	cmp w3, w21
	b.lt main_label_286
main_label_151:
	cmp w3, w23
	b.lt main_label_154
main_label_161:
	cmp w3, w28
	b.lt main_label_98
main_label_164:
	add w24, w24, #1
	b main_label_while_cond_39
main_label_83:
	adrp x9, __mm_tmp_0_1024
	add x9, x9, :lo12:__mm_tmp_0_1024
	add x9, x9, w19, sxtw #2
	str wzr, [x9]
	add w19, w19, #1
	b main_label_146
main_label_91:
	ld1 {v11.s}[0], [x7]
	adrp x0, A
	add x0, x0, :lo12:A
	sxtw x10, w6
	ld1 {v11.s}[1], [x7]
	add x0, x0, x10, lsl #12
	mov x1, x22
	movz w9, #0
	ld1 {v11.s}[2], [x7]
	ld1 {v11.s}[3], [x7]
main_label_220:
	cmp w9, w21
	b.lt main_label_225
main_label_170:
	cmp w9, w23
	b.lt main_label_173
main_label_183:
	cmp w9, w28
	b.lt main_label_103
main_label_186:
	add w6, w6, #1
	add x7, x7, #4
	b main_label_86
main_label_98:
	adrp x0, __mm_tmp_0_1024
	add x0, x0, :lo12:__mm_tmp_0_1024
	add x0, x0, w3, sxtw #2
	ldr w1, [x0]
	adrp x0, A
	add x0, x0, :lo12:A
	sxtw x10, w24
	add x0, x0, x10, lsl #12
	add x0, x0, w3, sxtw #2
	str w1, [x0]
	add w3, w3, #1
	b main_label_161
main_label_103:
	adrp x2, A
	adrp x4, __mm_tmp_0_1024
	add x2, x2, :lo12:A
	sxtw x10, w6
	add x4, x4, :lo12:__mm_tmp_0_1024
	add x2, x2, x10, lsl #12
	add x4, x4, w9, sxtw #2
	add x2, x2, w9, sxtw #2
	ldr w3, [x4]
	ldr w2, [x2]
	add w9, w9, #1
	madd	w2, w5, w2, w3
	str w2, [x4]
	b main_label_183
main_label_142:
	add w19, w19, #4
	str	q12, [x20], #16
	b main_label_139
main_label_154:
	ldr	q8, [x22], #16
	add w3, w3, #4
	str	q8, [x9], #16
	b main_label_151
main_label_173:
	mov x3, x1
	ldr	q8, [x1], #16
	mov x2, x0
	ldr	q0, [x0], #16
	add w9, w9, #4
	mla v8.4s, v11.4s, v0.4s
	str	q8, [x3]
	b main_label_170
main_label_225:
	mov x4, x1
	mov x2, x0
	ldr	q0, [x2]
	ldr	q10, [x4]
	add	x3, x1, #16
	add	x2, x0, #16
	add w1, w9, #8
	mov x0, x3
	mov x9, x2
	ldr	q8, [x0]
	mla v10.4s, v11.4s, v0.4s
	ldr	q0, [x9]
	add	x9, x3, #16
	mov x10, x9
	mov w11, w1
	mla v8.4s, v11.4s, v0.4s
	str	q10, [x4]
	mov x1, x10
	str	q8, [x0]
	add	x0, x2, #16
	mov w9, w11
	b main_label_220
main_label_249:
	ldr	w0, [x9], #4
	add w2, w2, #4
	madd	w1, w0, w0, w1
	ldr	w0, [x9], #4
	madd	w1, w0, w0, w1
	ldr w0, [x9]
	madd	w1, w0, w0, w1
	add	x0, x9, #4
	ldr w9, [x0]
	madd	w1, w9, w9, w1
	add	x9, x0, #4
	b main_label_244
main_label_274:
	add	x1, x20, #16
	mov x9, x20
	mov x0, x1
	add w19, w19, #8
	str	q12, [x9]
	str	q12, [x0]
	add	x20, x1, #16
	b main_label_270
main_label_286:
	mov x0, x22
	add	x1, x22, #16
	ldr	q9, [x0]
	mov x2, x9
	add	x0, x9, #16
	mov x9, x1
	ldr	q8, [x9]
	mov x9, x0
	add w3, w3, #8
	str	q9, [x2]
	str	q8, [x9]
	add	x22, x1, #16
	add	x9, x0, #16
	b main_label_281
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	ldp d8, d9, [sp, #80]
	ldp d10, d11, [sp, #96]
	ldr d12, [sp, #112]
	add sp, sp, #144
	ldp x29, x30, [sp], #16
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	sub sp, sp, #32
	adrp x10, __sysy_par_ctx_0_0
	stp x20, x19, [sp, #0]
	str d8, [sp, #16]
	ldr w20, [x10, :lo12:__sysy_par_ctx_0_0]
	adrp x10, __sysy_par_ctx_0_1
	ldr w19, [x10, :lo12:__sysy_par_ctx_0_1]
	mov w9, w0
__sysy_par_body_0_label_while_cond_11:
	cmp w9, w1
	b.lt __sysy_par_body_0_label_while_body_12
__sysy_par_body_0_label_par_ret:
	b .L__sysy_par_body_0_epilogue
__sysy_par_body_0_label_while_body_12:
	cmp w9, w20
	b.ge __sysy_par_body_0_label_while_cond_16.preheader
__sysy_par_body_0_label_if_else_15:
	add w9, w9, #1
	b __sysy_par_body_0_label_while_cond_11
__sysy_par_body_0_label_while_cond_16.preheader:
	movz w10, #65535
	movk w10, #65535, lsl #16
	adrp x2, A
	dup v8.4s, w10
	add x2, x2, :lo12:A
	sxtw x10, w9
	sub w7, w19, #3
	add x2, x2, x10, lsl #12
	sub w6, w19, #7
	movz w5, #0
__sysy_par_body_0_label_24:
	cmp w5, w6
	b.lt __sysy_par_body_0_label_28
__sysy_par_body_0_label_9:
	cmp w5, w7
	b.lt __sysy_par_body_0_label_12
__sysy_par_body_0_label_16:
	cmp w5, w19
	b.lt __sysy_par_body_0_label_while_body_17
	b __sysy_par_body_0_label_if_else_15
__sysy_par_body_0_label_while_body_17:
	adrp x3, A
	add x3, x3, :lo12:A
	sxtw x10, w9
	add x3, x3, x10, lsl #12
	movz w10, #65535
	add x3, x3, w5, sxtw #2
	movk w10, #65535, lsl #16
	str w10, [x3]
	add w5, w5, #1
	b __sysy_par_body_0_label_16
__sysy_par_body_0_label_12:
	add w5, w5, #4
	str	q8, [x2], #16
	b __sysy_par_body_0_label_9
__sysy_par_body_0_label_28:
	add	x3, x2, #16
	mov x4, x2
	mov x2, x3
	add w5, w5, #8
	str	q8, [x4]
	str	q8, [x2]
	add	x2, x3, #16
	b __sysy_par_body_0_label_24
.L__sysy_par_body_0_epilogue:
	ldp x20, x19, [sp, #0]
	ldr d8, [sp, #16]
	add sp, sp, #32
	ret
	.global __sysy_par_body_1
	.p2align 2
__sysy_par_body_1:
	sub sp, sp, #32
	adrp x10, __sysy_par_ctx_1_0
	stp x20, x19, [sp, #0]
	str d8, [sp, #16]
	ldr w20, [x10, :lo12:__sysy_par_ctx_1_0]
	adrp x10, __sysy_par_ctx_1_1
	ldr w19, [x10, :lo12:__sysy_par_ctx_1_1]
	mov w9, w0
__sysy_par_body_1_label_while_cond_19:
	cmp w9, w1
	b.lt __sysy_par_body_1_label_while_body_20
__sysy_par_body_1_label_par_ret:
	b .L__sysy_par_body_1_epilogue
__sysy_par_body_1_label_while_body_20:
	cmp w9, w20
	b.lt __sysy_par_body_1_label_while_cond_24.preheader
__sysy_par_body_1_label_if_else_23:
	add w9, w9, #1
	b __sysy_par_body_1_label_while_cond_19
__sysy_par_body_1_label_while_cond_24.preheader:
	movz w10, #65535
	movk w10, #65535, lsl #16
	adrp x2, B
	dup v8.4s, w10
	add x2, x2, :lo12:B
	sxtw x10, w9
	sub w7, w19, #3
	add x2, x2, x10, lsl #12
	sub w6, w19, #7
	movz w5, #0
__sysy_par_body_1_label_24:
	cmp w5, w6
	b.lt __sysy_par_body_1_label_28
__sysy_par_body_1_label_9:
	cmp w5, w7
	b.lt __sysy_par_body_1_label_12
__sysy_par_body_1_label_16:
	cmp w5, w19
	b.lt __sysy_par_body_1_label_while_body_25
	b __sysy_par_body_1_label_if_else_23
__sysy_par_body_1_label_while_body_25:
	adrp x3, B
	add x3, x3, :lo12:B
	sxtw x10, w9
	add x3, x3, x10, lsl #12
	movz w10, #65535
	add x3, x3, w5, sxtw #2
	movk w10, #65535, lsl #16
	str w10, [x3]
	add w5, w5, #1
	b __sysy_par_body_1_label_16
__sysy_par_body_1_label_12:
	add w5, w5, #4
	str	q8, [x2], #16
	b __sysy_par_body_1_label_9
__sysy_par_body_1_label_28:
	add	x3, x2, #16
	mov x4, x2
	mov x2, x3
	add w5, w5, #8
	str	q8, [x4]
	str	q8, [x2]
	add	x2, x3, #16
	b __sysy_par_body_1_label_24
.L__sysy_par_body_1_epilogue:
	ldp x20, x19, [sp, #0]
	ldr d8, [sp, #16]
	add sp, sp, #32
	ret
	.global __sysy_par_body_2
	.p2align 2
__sysy_par_body_2:
	sub sp, sp, #64
	adrp x10, __sysy_par_ctx_2_0
	stp x20, x19, [sp, #0]
	str x21, [sp, #16]
	stp d8, d9, [sp, #24]
	stp d10, d11, [sp, #40]
	ldr w21, [x10, :lo12:__sysy_par_ctx_2_0]
	mov w20, w0
__sysy_par_body_2_label_while_cond_27:
	cmp w20, w1
	b.lt __sysy_par_body_2_label_while_cond_30.preheader
__sysy_par_body_2_label_par_ret:
	b .L__sysy_par_body_2_epilogue
__sysy_par_body_2_label_while_cond_30.preheader:
	adrp x9, C
	add x9, x9, :lo12:C
	sxtw x10, w20
	adrp x7, A
	add x9, x9, x10, lsl #12
	add x7, x7, :lo12:A
	sxtw x10, w20
	adrp x6, B
	add x7, x7, x10, lsl #12
	add x6, x6, :lo12:B
	sxtw x10, w20
	sub w19, w21, #3
	movi v11.4s, #1
	movi v10.4s, #3
	add x6, x6, x10, lsl #12
	movz w5, #0
__sysy_par_body_2_label_12:
	cmp w5, w19
	b.lt __sysy_par_body_2_label_15
__sysy_par_body_2_label_28:
	cmp w5, w21
	b.lt __sysy_par_body_2_label_while_body_31
__sysy_par_body_2_label_31:
	add w20, w20, #1
	b __sysy_par_body_2_label_while_cond_27
__sysy_par_body_2_label_while_body_31:
	adrp x4, C
	add x4, x4, :lo12:C
	sxtw x10, w20
	adrp x2, A
	add x4, x4, x10, lsl #12
	add x2, x2, :lo12:A
	sxtw x10, w20
	add x2, x2, x10, lsl #12
	add x2, x2, w5, sxtw #2
	ldr w2, [x2]
	sxtw x10, w20
	add x4, x4, w5, sxtw #2
	lsl w3, w2, #1
	adrp x2, B
	add x2, x2, :lo12:B
	add x2, x2, x10, lsl #12
	add x2, x2, w5, sxtw #2
	ldr w2, [x2]
	movz w10, #3
	add w5, w5, #1
	madd	w2, w2, w10, w3
	str w2, [x4]
	b __sysy_par_body_2_label_28
__sysy_par_body_2_label_15:
	mov x2, x7
	ldr	q8, [x2]
	mov x2, x6
	ldr	q0, [x2]
	mov x3, x9
	sshl v9.4s, v8.4s, v11.4s
	mov v8.16b, v9.16b
	mla v8.4s, v0.4s, v10.4s
	add w5, w5, #4
	add x9, x9, #16
	str	q8, [x3]
	add x7, x7, #16
	add x6, x6, #16
	b __sysy_par_body_2_label_12
.L__sysy_par_body_2_epilogue:
	ldp x20, x19, [sp, #0]
	ldr x21, [sp, #16]
	ldp d8, d9, [sp, #24]
	ldp d10, d11, [sp, #40]
	add sp, sp, #64
	ret
	.global __sysy_par_body_3
	.p2align 2
__sysy_par_body_3:
	sub sp, sp, #16
	adrp x10, __sysy_par_ctx_3_0
	str x28, [sp, #0]
	ldr w7, [x10, :lo12:__sysy_par_ctx_3_0]
	movz w28, #21846
	movk w28, #21845, lsl #16
	mov w6, w0
__sysy_par_body_3_label_while_cond_33:
	cmp w6, w1
	b.lt __sysy_par_body_3_label_while_cond_36.preheader
__sysy_par_body_3_label_par_ret:
	b .L__sysy_par_body_3_epilogue
__sysy_par_body_3_label_while_cond_36.preheader:
	adrp x3, C
	add x3, x3, :lo12:C
	sxtw x10, w6
	add x3, x3, x10, lsl #12
	sub w5, w7, #3
	movz w4, #0
__sysy_par_body_3_label_7:
	cmp w4, w5
	b.lt __sysy_par_body_3_label_11
__sysy_par_body_3_label_while_cond_36:
	cmp w4, w7
	b.lt __sysy_par_body_3_label_while_body_37
__sysy_par_body_3_label_while_end_38:
	add w6, w6, #1
	b __sysy_par_body_3_label_while_cond_33
__sysy_par_body_3_label_while_body_37:
	ldr w2, [x3]
	movz w10, #7
	add w4, w4, #1
	madd	w2, w2, w2, w10
	smull x10, w2, w28
	asr x10, x10, #32
	add w2, w10, w2, lsr #31
	str	w2, [x3], #4
	b __sysy_par_body_3_label_while_cond_36
__sysy_par_body_3_label_11:
	ldr w2, [x3]
	movz w10, #7
	add w4, w4, #4
	madd	w2, w2, w2, w10
	smull x10, w2, w28
	asr x10, x10, #32
	add w2, w10, w2, lsr #31
	str	w2, [x3], #4
	ldr w2, [x3]
	movz w10, #7
	madd	w2, w2, w2, w10
	smull x10, w2, w28
	asr x10, x10, #32
	add w2, w10, w2, lsr #31
	str	w2, [x3], #4
	ldr w2, [x3]
	movz w10, #7
	madd	w2, w2, w2, w10
	smull x10, w2, w28
	asr x10, x10, #32
	add w2, w10, w2, lsr #31
	str	w2, [x3], #4
	ldr w2, [x3]
	movz w10, #7
	madd	w2, w2, w2, w10
	smull x10, w2, w28
	asr x10, x10, #32
	add w2, w10, w2, lsr #31
	str	w2, [x3], #4
	b __sysy_par_body_3_label_7
.L__sysy_par_body_3_epilogue:
	ldr x28, [sp, #0]
	add sp, sp, #16
	ret
	.bss
	.global A
	.p2align 4
A:
	.zero 4194304

	.global B
	.p2align 4
B:
	.zero 4194304

	.global C
	.p2align 4
C:
	.zero 4194304

	.global __mm_tmp_0_1024
	.p2align 4
__mm_tmp_0_1024:
	.zero 4096

	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4

	.global __sysy_par_ctx_0_1
	.p2align 2
__sysy_par_ctx_0_1:
	.zero 4

	.global __sysy_par_ctx_1_0
	.p2align 2
__sysy_par_ctx_1_0:
	.zero 4

	.global __sysy_par_ctx_1_1
	.p2align 2
__sysy_par_ctx_1_1:
	.zero 4

	.global __sysy_par_ctx_2_0
	.p2align 2
__sysy_par_ctx_2_0:
	.zero 4

	.global __sysy_par_ctx_3_0
	.p2align 2
__sysy_par_ctx_3_0:
	.zero 4


	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	cmp w0, #1
	b.eq .Lsysy_disp_1
	cmp w0, #2
	b.eq .Lsysy_disp_2
	cmp w0, #3
	b.eq .Lsysy_disp_3
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
.Lsysy_disp_1:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_1
.Lsysy_disp_2:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_2
.Lsysy_disp_3:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_3

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
	.global	__aarch64_ldadd4_rel
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
	mov	x1, x22
	mov	w0, 1
	bl	__aarch64_ldadd4_rel
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
	add	w23, w21, w23, asr 1
	mov	x1, x19
	mov	w0, 1
	str	w22, [x19, 136]
	str	w23, [x19, 140]
	str	w20, [x19, 144]
	ldr	w20, [x1, 132]!
	add	w20, w20, w0
	bl	__aarch64_ldadd4_rel
	mov	w0, w22
	mov	w2, w23
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
