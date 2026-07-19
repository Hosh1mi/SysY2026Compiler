	.text
	.global main
	.p2align 2
main:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #496
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	stp x28, x27, [sp, #64]
	stp d8, d9, [sp, #80]
	stp d10, d11, [sp, #96]
	bl getint
	mov w28, w0
	bl getint
	movz w14, #8177
	movk w14, #32704, lsl #16
	smull x15, w28, w14
	movz w16, #513
	str w0, [x29, #-168]
	asr x15, x15, #40
	add w15, w15, w28, lsr #31
	msub w16, w15, w16, w28
	movz w0, #134
	mov	w15, w16
	str w16, [x29, #-184]
	add w27, w15, #64
	bl _sysy_starttime
	add w15, w27, w27, lsr #31
	movz w10, #32769
	movz w12, #65024
	movz w13, #21846
	asr w15, w15, #1
	adrp x26, In
	movk w10, #32768, lsl #16
	movk w12, #255, lsl #16
	movk w13, #21845, lsl #16
	str w15, [x29, #-200]
	add x26, x26, :lo12:In
	str	wzr, [x29, #-24]
main_label_172:
	ldr w15, [x29, #-24]
	cmp w15, w27
	b.lt main_label_176
main_label_199:
	adrp x25, K
	add x25, x25, :lo12:K
	add	x15, x25, #4
	sub x17, x29, #264
	str	x25, [x29, #-248]
	str x15, [x17]
	add	x15, x25, #8
	sub x17, x29, #280
	str x15, [x17]
	add	x15, x25, #12
	sub x17, x29, #296
	str x15, [x17]
	ldr x16, [x17]
	sub x17, x29, #280
	ldr x16, [x17]
	sub x17, x29, #264
	ldr x15, [x29, #-248]
	ldr x16, [x17]
	ldr x16, [x29, #-248]
	movi v11.4s, #1
	mov w24, wzr
	str x15, [x29, #-40]
	str x16, [x29, #-56]
	str x16, [x29, #-72]
	str x16, [x29, #-88]
	str x16, [x29, #-104]
main_label_324:
	cmp w24, #22
	b.lt main_label_327
main_label_351:
	cmp w24, #25
	b.lt main_label_164
main_label_354:
	adrp x23, Out
	add x23, x23, :lo12:Out
	str	wzr, [x29, #-8]
main_label_83:
	ldr w15, [x29, #-8]
	ldr w16, [x29, #-168]
	cmp w15, w16
	b.ge main_label_67.preheader
	movz w22, #0
main_label_88:
	cmp w22, w27
	b.lt main_label_93.preheader
main_label_141:
	ldr w16, [x29, #-8]
	add w15, w16, #1
	str w15, [x29, #-216]
	str w15, [x29, #-8]
	b main_label_83
main_label_17:
	ldr	w2, [x9], #4
	add w0, w0, #1
	add w19, w19, w2
main_label_11:
	cmp w0, w21
	b.lt main_label_17
main_label_22:
	movz w0, #145
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	movz w0, #10
	bl putch
	adrp x15, state
	str w28, [x15, :lo12:state]
	ldr w16, [x29, #-168]
	adrp x15, repeat_factor
	mov w0, wzr
	str w16, [x15, :lo12:repeat_factor]
	adrp x15, N_eff
	str w27, [x15, :lo12:N_eff]
	b .Lmain_epilogue
main_label_30.preheader:
	ldr w15, [x29, #-184]
	mul w9, w27, w20
	mov x6, x23
	movz w4, #0
	add x6, x6, w9, sxtw #2
	add w7, w15, #61
	mov x5, x6
	movz w3, #0
main_label_441:
	cmp w4, w7
	b.lt main_label_446
main_label_30.preheader.1:
	movi	v9.4s, #0
	mov v9.s[0], w3
main_label_575:
	cmp w4, w7
	b.lt main_label_580
main_label_586:
	addv s0, v9.4s
	fmov w2, s0
main_label_30:
	cmp w4, w27
	b.lt main_label_35
	movz w1, #0
main_label_467:
	cmp w1, w7
	b.lt main_label_471
main_label_42:
	cmp w1, w27
	b.lt main_label_46
main_label_52:
	add w20, w20, #1
main_label_25:
	cmp w20, w27
	b.lt main_label_30.preheader
main_label_11.preheader:
	sub w4, w21, #3
	movi	v10.4s, #0
	mov v10.s[0], wzr
	mov x1, x23
	sub w5, w21, #7
	mov w0, wzr
main_label_531:
	cmp w0, w5
	b.lt main_label_536
main_label_298:
	cmp w0, w4
	b.lt main_label_302
main_label_308:
	addv s0, v10.4s
	mov x9, x23
	add x9, x9, w0, sxtw #2
	fmov w3, s0
main_label_509:
	cmp w0, w4
	b.lt main_label_514
main_label_11.preheader.1:
	movi	v9.4s, #0
	mov v9.s[0], w3
main_label_601:
	cmp w0, w4
	b.lt main_label_606
main_label_612:
	addv s0, v9.4s
	fmov w19, s0
	b main_label_11
main_label_35:
	ldr	w9, [x5], #4
	add w4, w4, #1
	add w2, w2, w9
	b main_label_30
main_label_46:
	ldr w9, [x6]
	add w1, w1, #1
	sub w9, w9, w2
	str	w9, [x6], #4
	b main_label_42
main_label_67.preheader:
	mul w21, w27, w27
	adrp x15, __sysy_par_ctx_0_0
	str x23, [x15, :lo12:__sysy_par_ctx_0_0]
	mov w0, wzr
	mov w1, wzr
	mov w2, w21
	bl __sysy_parallel_for
	movz w20, #0
	b main_label_25
main_label_93.preheader:
	mul w9, w27, w22
	mov x21, x23
	movz w20, #0
	add x21, x21, w9, sxtw #2
main_label_93:
	cmp w20, w27
	b.lt main_label_98.preheader
main_label_139:
	add w22, w22, #1
	b main_label_88
main_label_98.preheader:
	movz w15, #65534
	sub w9, wzr, w20
	movk w15, #65535, lsl #16
	add w0, w9, #2
	cmp w9, w15
	csel w19, w0, wzr, gt
	add w0, w27, w9
	add w9, w0, #2
	cmp w0, #3
	movz w15, #5
	mov x6, x25
	csel w7, w9, w15, lt
	add x6, x6, w19, sxtw #2
	movz w5, #0
	movz w4, #0
main_label_98:
	cmp w5, #5
	b.lt main_label_102
main_label_134:
	str	w4, [x21], #4
	add w20, w20, #1
	b main_label_93
main_label_102:
	add w0, w22, w5
	cmp w0, #2
	b.ge main_label_102.unsw.t
main_label_132:
	add w5, w5, #1
	add x6, x6, #20
	b main_label_98
main_label_109:
	add w1, w20, w0
	sub w1, w1, #2
	add w1, w3, w1
	mov x15, x26
	add	x1, x15, w1, sxtw #2
	ldr w2, [x1]
	ldr	w1, [x9], #4
	add w0, w0, #1
	cmp w0, w7
	madd	w4, w2, w1, w4
	b.ge	main_label_132
	b main_label_109
main_label_164:
	smull x15, w24, w13
	movz w16, #3
	mov x0, x25
	asr x15, x15, #32
	add w15, w15, w24, lsr #31
	msub w9, w15, w16, w24
	add x0, x0, w24, uxtw #2
	add w24, w24, #1
	sub w9, w9, #1
	str w9, [x0]
	b main_label_351
main_label_176:
	ldr w15, [x29, #-24]
	ldr w16, [x29, #-200]
	cmp w15, w16
	b.lt main_label_187.preheader
main_label_179.preheader:
	ldr w15, [x29, #-24]
	mov x1, x26
	movz w0, #0
	mul w9, w27, w15
	ldr w15, [x29, #-184]
	add x1, x1, w9, sxtw #2
	add w2, w15, #61
main_label_492:
	cmp w0, w2
	b.lt main_label_496
main_label_179:
	cmp w0, w27
	b.lt main_label_183
	mov w0, w28
main_label_191:
	ldr w16, [x29, #-24]
	add w15, w16, #1
	str w15, [x29, #-232]
	ldr w16, [x29, #-232]
	mov w15, w0
	mov w28, w15
	str w16, [x29, #-24]
	b main_label_172
main_label_183:
	movz w15, #65535
	movk w15, #65535, lsl #16
	str	w15, [x1], #4
	add w0, w0, #1
	b main_label_179
main_label_187.preheader:
	ldr w15, [x29, #-24]
	mov x1, x26
	mov w0, w28
	mul w9, w27, w15
	add x1, x1, w9, sxtw #2
	movz w9, #0
main_label_187:
	cmp w9, w27
	b.lt main_label_193
	b main_label_191
main_label_193:
	cmp w0, #0
	cneg w5, w0, mi
	and w5, w5, #2047
	cneg w5, w5, mi
	cmp w0, wzr
	cset w15, ge
	mov w4, w15
	movz w15, #1
	cmp w5, w15
	cset w16, ge
	cmp w5, w12
	mov w3, w16
	cset w15, le
	mov w2, w15
	and w3, w4, w3
	and w2, w3, w2
	sub w4, w5, #3
	cbnz w2, main_label_205.modfold.fast
	movz w3, #0
main_label_424:
	cmp w3, w4
	b.lt main_label_428
main_label_205:
	cmp w3, w5
	b.lt main_label_208
main_label_213:
	smull x15, w0, w10
	movz w16, #65535
	add w9, w9, #1
	asr x15, x15, #32
	add w15, w15, w0
	asr w15, w15, #15
	add w15, w15, w0, lsr #31
	msub w0, w15, w16, w0
	str	w0, [x1], #4
	b main_label_187
main_label_208:
	add w2, w0, #128
	smull x15, w2, w10
	movz w16, #65535
	add w3, w3, #1
	asr x15, x15, #32
	add w15, w15, w2
	asr w15, w15, #15
	add w15, w15, w2, lsr #31
	msub w0, w15, w16, w2
	b main_label_205
main_label_102.unsw.t:
	sub w0, w0, #2
	cmp w0, w27
	b.lt main_label_102.unsw.t.unsw.t
	b main_label_132
main_label_102.unsw.t.unsw.t:
	mul w3, w0, w27
	cmp w19, w7
	b.ge	main_label_132
	mov x9, x6
	mov w0, w19
	b main_label_109
main_label_302:
	ldr	q8, [x1], #16
	add w0, w0, #4
	add v10.4s, v10.4s, v8.4s
	b main_label_298
main_label_327:
	smull x15, w24, w13
	movz w16, #3
	add w9, w24, #1
	asr x15, x15, #32
	add w15, w15, w24, lsr #31
	msub w1, w15, w16, w24
	smull x15, w9, w13
	movz w16, #3
	add w3, w24, #3
	asr x15, x15, #32
	add w15, w15, w9, lsr #31
	msub w0, w15, w16, w9
	add w9, w24, #2
	smull x15, w9, w13
	movz w16, #3
	mov v8.s[0], w1
	asr x15, x15, #32
	add w15, w15, w9, lsr #31
	msub w9, w15, w16, w9
	smull x15, w3, w13
	movz w16, #3
	mov v8.s[1], w0
	asr x15, x15, #32
	add w15, w15, w3, lsr #31
	msub w3, w15, w16, w3
	ldr x15, [x29, #-40]
	mov v8.s[2], w9
	mov v8.s[3], w3
	sub v8.4s, v8.4s, v11.4s
	mov x9, x15
	str	q8, [x9]
	ldr x15, [x29, #-104]
	sub x17, x29, #312
	add w2, w24, #4
	add x15, x15, #16
	str x15, [x17]
	ldr x15, [x29, #-88]
	sub x17, x29, #328
	add x15, x15, #16
	str x15, [x17]
	ldr x15, [x29, #-72]
	sub x17, x29, #344
	add x15, x15, #16
	str x15, [x17]
	ldr x15, [x29, #-56]
	sub x17, x29, #360
	add x15, x15, #16
	str x15, [x17]
	ldr x15, [x29, #-40]
	sub x17, x29, #376
	add x15, x15, #16
	str x15, [x17]
	ldr x15, [x17]
	sub x17, x29, #360
	ldr x16, [x17]
	sub x17, x29, #344
	ldr x16, [x17]
	sub x17, x29, #328
	ldr x16, [x17]
	sub x17, x29, #312
	ldr x16, [x17]
	mov w16, w2
	str x15, [x29, #-40]
	str x16, [x29, #-56]
	str x16, [x29, #-72]
	str x16, [x29, #-88]
	str x16, [x29, #-104]
	mov w24, w16
	b main_label_324
main_label_205.modfold.fast:
	smull x15, w0, w10
	movz w16, #65535
	lsl w3, w5, #7
	asr x15, x15, #32
	add w15, w15, w0
	asr w15, w15, #15
	add w15, w15, w0, lsr #31
	msub w2, w15, w16, w0
	movz w16, #65535
	add w2, w2, w3
	smull x15, w2, w10
	asr x15, x15, #32
	add w15, w15, w2
	asr w15, w15, #15
	add w15, w15, w2, lsr #31
	msub w0, w15, w16, w2
	b main_label_213
main_label_428:
	add w2, w0, #128
	smull x15, w2, w10
	movz w16, #65535
	add w3, w3, #4
	asr x15, x15, #32
	add w15, w15, w2
	asr w15, w15, #15
	add w15, w15, w2, lsr #31
	msub w2, w15, w16, w2
	movz w16, #65535
	add w2, w2, #128
	smull x15, w2, w10
	asr x15, x15, #32
	add w15, w15, w2
	asr w15, w15, #15
	add w15, w15, w2, lsr #31
	msub w2, w15, w16, w2
	movz w16, #65535
	add w2, w2, #128
	smull x15, w2, w10
	asr x15, x15, #32
	add w15, w15, w2
	asr w15, w15, #15
	add w15, w15, w2, lsr #31
	msub w2, w15, w16, w2
	movz w16, #65535
	add w2, w2, #128
	smull x15, w2, w10
	asr x15, x15, #32
	add w15, w15, w2
	asr w15, w15, #15
	add w15, w15, w2, lsr #31
	msub w0, w15, w16, w2
	b main_label_424
main_label_446:
	ldr w9, [x5]
	add w4, w4, #4
	add w0, w3, w9
	add	x9, x5, #4
	ldr	w1, [x9], #4
	add w1, w0, w1
	ldr w0, [x9]
	add w1, w1, w0
	add	x0, x9, #4
	ldr w9, [x0]
	add	x5, x0, #4
	add w3, w1, w9
	b main_label_441
main_label_471:
	ldr w9, [x6]
	add	x0, x6, #4
	add w1, w1, #4
	sub w9, w9, w2
	str w9, [x6]
	ldr w9, [x0]
	sub w9, w9, w2
	str	w9, [x0], #4
	ldr w9, [x0]
	sub w9, w9, w2
	str	w9, [x0], #4
	ldr w9, [x0]
	add	x6, x0, #4
	sub w9, w9, w2
	str w9, [x0]
	b main_label_467
main_label_496:
	movz w15, #65535
	movk w15, #65535, lsl #16
	str w15, [x1]
	movz w15, #65535
	add	x9, x1, #4
	movk w15, #65535, lsl #16
	str	w15, [x9], #4
	movz w15, #65535
	movk w15, #65535, lsl #16
	str	w15, [x9], #4
	movz w15, #65535
	movk w15, #65535, lsl #16
	str w15, [x9]
	add w0, w0, #4
	add	x1, x9, #4
	b main_label_492
main_label_514:
	ldr	w2, [x9], #4
	add w0, w0, #4
	add w3, w3, w2
	ldr	w2, [x9], #4
	add w3, w3, w2
	ldr	w2, [x9], #4
	add w3, w3, w2
	ldr	w2, [x9], #4
	add w2, w3, w2
	mov w3, w2
	b main_label_509
main_label_536:
	ldr	q8, [x1], #16
	add w0, w0, #8
	add v9.4s, v10.4s, v8.4s
	ldr	q8, [x1], #16
	add v10.4s, v9.4s, v8.4s
	b main_label_531
main_label_580:
	ldr	q8, [x5], #16
	add w4, w4, #4
	add v9.4s, v9.4s, v8.4s
	b main_label_575
main_label_606:
	mov x2, x9
	ldr	q8, [x9], #16
	add w0, w0, #4
	add v9.4s, v9.4s, v8.4s
	b main_label_601
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	ldp d8, d9, [sp, #80]
	ldp d10, d11, [sp, #96]
	add sp, sp, #496
	ldp x29, x30, [sp], #16
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	sub sp, sp, #16
	adrp x10, __sysy_par_ctx_0_0
	str x28, [sp, #0]
	ldr x2, [x10, :lo12:__sysy_par_ctx_0_0]
	movz w28, #2027
	movk w28, #5405, lsl #16
	sub w6, w1, #3
	mov x4, x2
	add x4, x4, w0, sxtw #2
	mov w5, w0
__sysy_par_body_0_label_7:
	cmp w5, w6
	b.lt __sysy_par_body_0_label_11
__sysy_par_body_0_label_67:
	cmp w5, w1
	b.lt __sysy_par_body_0_label_72
__sysy_par_body_0_label_par_ret:
	b .L__sysy_par_body_0_epilogue
__sysy_par_body_0_label_72:
	ldr w3, [x4]
	movz w11, #97
	add w5, w5, #1
	add w2, w3, #3
	mul w2, w2, w3
	sub w2, w2, #7
	smull x10, w2, w28
	asr x10, x10, #35
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	str	w2, [x4], #4
	b __sysy_par_body_0_label_67
__sysy_par_body_0_label_11:
	ldr w3, [x4]
	movz w11, #97
	add w5, w5, #4
	add w2, w3, #3
	mul w2, w2, w3
	sub w2, w2, #7
	smull x10, w2, w28
	asr x10, x10, #35
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	movz w11, #97
	str	w2, [x4], #4
	ldr w3, [x4]
	add w2, w3, #3
	mul w2, w2, w3
	sub w2, w2, #7
	smull x10, w2, w28
	asr x10, x10, #35
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	movz w11, #97
	str	w2, [x4], #4
	ldr w3, [x4]
	add w2, w3, #3
	mul w2, w2, w3
	sub w2, w2, #7
	smull x10, w2, w28
	asr x10, x10, #35
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	movz w11, #97
	str	w2, [x4], #4
	ldr w3, [x4]
	add w2, w3, #3
	mul w2, w2, w3
	sub w2, w2, #7
	smull x10, w2, w28
	asr x10, x10, #35
	add w10, w10, w2, lsr #31
	msub w2, w10, w11, w2
	str	w2, [x4], #4
	b __sysy_par_body_0_label_7
.L__sysy_par_body_0_epilogue:
	ldr x28, [sp, #0]
	add sp, sp, #16
	ret
	.data
	.global state
	.p2align 2
state:
	.word 0

	.global repeat_factor
	.p2align 2
repeat_factor:
	.word 0

	.global N_eff
	.p2align 2
N_eff:
	.word 0

	.bss
	.global In
	.p2align 4
In:
	.zero 16777216

	.global Out
	.p2align 4
Out:
	.zero 16777216

	.global K
	.p2align 4
K:
	.zero 100

	.global __sysy_par_ctx_0_0
	.p2align 3
__sysy_par_ctx_0_0:
	.zero 8


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
