	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	stp x23, x24, [sp, #32]
	movz w24, #0
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	bl getint
	mov w19, w0
	bl getint
	movz w9, #8177
	movk w9, #32704, lsl #16
	smull x9, w19, w9
	asr x9, x9, #40
	movz w10, #513
	add w9, w9, w9, lsr #31
	msub w22, w9, w10, w19
	mov w23, w0
	add w21, w22, #64
	movz w0, #134
	bl _sysy_starttime
	adrp x9, In
	add x20, x9, :lo12:In
	add w9, w21, w21, lsr #31
	mov x28, x20
	asr w27, w9, #1
	mov w26, w24
	movz w25, #255
.Lmain_bb1:
	cmp w26, w21
	b.ge .Lmain_bb15
.Lmain_bb2:
	cmp w26, w27
	b.ge .Lmain_bb13
.Lmain_bb3:
	mul w9, w21, w26
	movz w10, #32769
	add x13, x28, w9, sxtw #2
	mov w3, w19
	mov w14, w24
	orr w12, wzr, #0xfffe00
	movz w11, #65535
	movk w10, #32768, lsl #16
.Lmain_bb4:
	cmp w14, w21
	b.ge .Lmain_bb63
.Lmain_bb5:
	cmp w3, #0
	cneg w9, w3, mi
	and w9, w9, #2047
	cneg w17, w9, mi
	cmp w3, #0
	cset w16, ge
	cmp w17, #1
	cset w9, ge
	cmp w17, w12
	cset w15, le
	and w9, w16, w9
	and w9, w9, w15
	cbnz w9, .Lmain_bb11
.Lmain_bb6:
	movz w15, #32769
	sub w4, w17, #3
	mov w5, w24
	orr w6, wzr, #0x80000003
	movz w8, #65535
	movk w15, #32768, lsl #16
	movz w19, #65407
	movn w16, #65406
.Lmain_bb7:
	cmp w5, w4
	cset w7, lt
	cmp w17, w6
	cset w9, ge
	and w9, w9, w7
	cbz w9, .Lmain_bb61
.Lmain_bb10:
	add w7, w3, #128
	smull x9, w7, w15
	asr x9, x9, #32
	add w9, w9, w7
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w9, w9, w8, w7
	add w7, w9, #128
	cmp w9, w19
	add w9, w9, w16
	csel w9, w9, w7, ge
	add w7, w9, #128
	cmp w9, w19
	add w9, w9, w16
	csel w9, w9, w7, ge
	add w7, w9, #128
	cmp w9, w19
	add w9, w9, w16
	csel w3, w9, w7, ge
	add w5, w5, #4
	b .Lmain_bb7
.Lmain_bb8:
	cmp w5, w17
	b.ge .Lmain_bb12
.Lmain_bb9:
	add w16, w16, #128
	smull x9, w16, w15
	asr x9, x9, #32
	add w9, w9, w16
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w16, w9, w19, w16
	add w5, w5, #1
	b .Lmain_bb8
.Lmain_bb11:
	smull x9, w3, w10
	asr x9, x9, #32
	add w9, w9, w3
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w15, w9, w11, w3
	lsl w9, w17, #7
	add w15, w15, w9
	smull x9, w15, w10
	asr x9, x9, #32
	add w9, w9, w15
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w16, w9, w11, w15
.Lmain_bb12:
	smull x9, w16, w10
	asr x9, x9, #32
	add w9, w9, w16
	asr w9, w9, #15
	add w9, w9, w9, lsr #31
	msub w3, w9, w11, w16
	str w3, [x13], #4
	add w14, w14, #1
	b .Lmain_bb4
.Lmain_bb13:
	mul w9, w21, w26
	add x0, x28, w9, sxtw #2
	lsl w2, w21, #2
	mov w1, w25
	bl memset
.Lmain_bb14:
	add w26, w26, #1
	b .Lmain_bb1
.Lmain_bb15:
	adrp x9, K
	add x9, x9, :lo12:K
	mov x14, x9
	movz w13, #21846
	mov x17, x14
	mov w25, w24
	movz w16, #3
	movk w13, #21845, lsl #16
.Lmain_bb16:
	cmp w25, #22
	b.ge .Lmain_bb64
.Lmain_bb60:
	smull x12, w25, w13
	add w27, w25, #1
	add w15, w25, #3
	smull x11, w27, w13
	add w26, w25, #2
	smull x9, w15, w13
	asr x12, x12, #32
	smull x10, w26, w13
	add w12, w12, w12, lsr #31
	msub w28, w12, w16, w25
	asr x11, x11, #32
	asr x9, x9, #32
	asr x10, x10, #32
	add w11, w11, w11, lsr #31
	add w9, w9, w9, lsr #31
	msub w12, w11, w16, w27
	add w10, w10, w10, lsr #31
	msub w9, w9, w16, w15
	msub w10, w10, w16, w26
	add x11, x17, #4
	sub w15, w12, #1
	add x11, x11, #4
	sub w26, w28, #1
	sub w12, w10, #1
	sub w9, w9, #1
	stp w26, w15, [x17]
	add x11, x11, #4
	stp w12, w9, [x17, #8]
	add w25, w25, #4
	add x17, x11, #4
	b .Lmain_bb16
.Lmain_bb17:
	cmp w11, #25
	b.ge .Lmain_bb19
.Lmain_bb18:
	smull x9, w11, w10
	asr x9, x9, #32
	add w9, w9, w9, lsr #31
	msub w9, w9, w12, w11
	sub w9, w9, #1
	str w9, [x17], #4
	add w11, w11, #1
	b .Lmain_bb17
.Lmain_bb19:
	adrp x9, Out
	add x25, x9, :lo12:Out
	mov w15, w24
.Lmain_bb20:
	cmp w15, w23
	b.ge .Lmain_bb47
.Lmain_bb65:
	mov w16, w24
.Lmain_bb21:
	cmp w16, w21
	b.ge .Lmain_bb46
.Lmain_bb22:
	mul w9, w21, w16
	add x17, x25, w9, sxtw #2
	mov w26, w24
.Lmain_bb23:
	cmp w26, w21
	b.ge .Lmain_bb45
.Lmain_bb66:
	mov x27, x14
	mov w28, w24
	mov w11, w24
.Lmain_bb24:
	cmp w28, #5
	b.ge .Lmain_bb44
.Lmain_bb25:
	add w9, w16, w28
	cmp w9, #2
	b.lt .Lmain_bb43
.Lmain_bb26:
	sub w9, w9, #2
	cmp w9, w21
	b.ge .Lmain_bb43
.Lmain_bb27:
	mul w8, w9, w21
	mov x13, x27
	mov w7, w24
.Lmain_bb28:
	cmp w7, #4
	b.ge .Lmain_bb36
.Lmain_bb29:
	add w9, w26, w7
	cmp w9, #2
	b.lt .Lmain_bb32
.Lmain_bb30:
	sub w9, w9, #2
	cmp w9, w21
	b.ge .Lmain_bb32
.Lmain_bb31:
	add w9, w8, w9
	ldr w10, [x20, w9, sxtw #2]
	ldr w9, [x13]
	madd w11, w10, w9, w11
.Lmain_bb32:
	add w9, w7, #1
	add w9, w26, w9
	add x12, x13, #4
	cmp w9, #2
	b.lt .Lmain_bb35
.Lmain_bb33:
	sub w9, w9, #2
	cmp w9, w21
	b.ge .Lmain_bb35
.Lmain_bb34:
	add w9, w8, w9
	ldr w10, [x20, w9, sxtw #2]
	ldr w9, [x13, #4]
	madd w11, w10, w9, w11
.Lmain_bb35:
	add w7, w7, #2
	add x13, x12, #4
	b .Lmain_bb28
.Lmain_bb36:
	cmp w7, #5
	b.ge .Lmain_bb43
.Lmain_bb37:
	cmp w7, #5
	b.ge .Lmain_bb43
.Lmain_bb38:
	add w9, w26, w7
	cmp w9, #2
	b.lt .Lmain_bb41
.Lmain_bb39:
	sub w9, w9, #2
	cmp w9, w21
	b.ge .Lmain_bb41
.Lmain_bb40:
	add w9, w8, w9
	ldr w10, [x20, w9, sxtw #2]
	ldr w9, [x13]
	madd w11, w10, w9, w11
.Lmain_bb41:
	add w7, w7, #1
	add x13, x13, #4
	b .Lmain_bb37
.Lmain_bb43:
	add w28, w28, #1
	add x27, x27, #20
	b .Lmain_bb24
.Lmain_bb44:
	str w11, [x17], #4
	add w26, w26, #1
	b .Lmain_bb23
.Lmain_bb45:
	add w16, w16, #1
	b .Lmain_bb21
.Lmain_bb46:
	add w15, w15, #1
	b .Lmain_bb20
.Lmain_bb47:
	mul w26, w21, w21
	adrp x9, __sysy_par_ctx_2_0
	str x25, [x9, :lo12:__sysy_par_ctx_2_0]
	movz w0, #2
	movz w1, #0
	mov w2, w26
	bl __sysy_parallel_for
	mov w27, w24
	orr w20, wzr, #0x7ffffff0
.Lmain_bb48:
	cmp w27, w21
	b.ge .Lmain_bb49
.Lmain_bb50:
	mul w28, w27, w21
	adrp x14, __sysy_par_ctx_1_0
	adrp x13, __sysy_par_ctx_1_1
	movz w1, #0
	adrp x12, __sysy_par_scalar_start_1
	adrp x11, __sysy_par_scalar_bound_1
	adrp x10, __sysy_par_scalar_partial_1_0
	adrp x9, __sysy_par_scalar_partial_1_1
	str w28, [x14, :lo12:__sysy_par_ctx_1_0]
	movz w0, #1
	str x25, [x13, :lo12:__sysy_par_ctx_1_1]
	mov w2, w21
	str w1, [x12, :lo12:__sysy_par_scalar_start_1]
	str w21, [x11, :lo12:__sysy_par_scalar_bound_1]
	str w1, [x10, :lo12:__sysy_par_scalar_partial_1_0]
	str w1, [x9, :lo12:__sysy_par_scalar_partial_1_1]
	bl __sysy_parallel_for
	adrp x10, __sysy_par_scalar_partial_1_0
	adrp x9, __sysy_par_scalar_partial_1_1
	ldr w10, [x10, :lo12:__sysy_par_scalar_partial_1_0]
	ldr w9, [x9, :lo12:__sysy_par_scalar_partial_1_1]
	add x17, x25, w28, sxtw #2
	add w12, w10, w9
	add w7, w22, #61
	mov w16, w24
	orr w8, wzr, #0x80000003
.Lmain_bb51:
	cmp w16, w7
	cset w10, lt
	cmp w21, w8
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb53
.Lmain_bb52:
	ldr w13, [x17, #4]
	ldr w14, [x17]
	ldr w10, [x17, #8]
	ldr w9, [x17, #12]
	add x11, x17, #4
	add x11, x11, #4
	sub w15, w13, w12
	sub w28, w14, w12
	add x13, x11, #4
	sub w14, w10, w12
	sub w11, w9, w12
	str w28, [x17]
	add x9, x13, #4
	str w15, [x17, #4]
	str w14, [x17, #8]
	str w11, [x17, #12]
	add w16, w16, #4
	mov x17, x9
	b .Lmain_bb51
.Lmain_bb49:
	adrp x13, __sysy_par_ctx_0_0
	movz w1, #0
	adrp x12, __sysy_par_scalar_start_0
	adrp x11, __sysy_par_scalar_bound_0
	adrp x10, __sysy_par_scalar_partial_0_0
	adrp x9, __sysy_par_scalar_partial_0_1
	str x25, [x13, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	str w1, [x12, :lo12:__sysy_par_scalar_start_0]
	mov w2, w26
	str w26, [x11, :lo12:__sysy_par_scalar_bound_0]
	str w1, [x10, :lo12:__sysy_par_scalar_partial_0_0]
	str w1, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	bl __sysy_parallel_for
	adrp x10, __sysy_par_scalar_partial_0_0
	adrp x9, __sysy_par_scalar_partial_0_1
	ldr w22, [x10, :lo12:__sysy_par_scalar_partial_0_0]
	ldr w20, [x9, :lo12:__sysy_par_scalar_partial_0_1]
	movz w0, #145
	bl _sysy_stoptime
	add w0, w22, w20
	bl putint
	movz w0, #10
	bl putch
	adrp x11, state
	adrp x10, repeat_factor
	adrp x9, N_eff
	str w19, [x11, :lo12:state]
	str w23, [x10, :lo12:repeat_factor]
	str w21, [x9, :lo12:N_eff]
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb53:
	dup v18.4s, w12
	cmp w16, w20
	cset w10, le
	add w9, w16, #15
	cmp w9, w21
	cset w9, lt
	and w9, w10, w9
	add w11, w22, #56
	cbz w9, .Lmain_bb79
.Lmain_bb78:
	mov x9, x17
	mov w10, w16
.Lmain_bb54:
	cmp w10, w11
	b.gt .Lmain_bb80
.Lmain_bb55:
	ldp q17, q16, [x9]
	sub v17.4s, v17.4s, v18.4s
	sub v16.4s, v16.4s, v18.4s
	stp q17, q16, [x9]
	add w10, w10, #8
	add x9, x9, #32
	b .Lmain_bb54
.Lmain_bb57:
	cmp w10, w21
	b.ge .Lmain_bb59
.Lmain_bb58:
	ldr w9, [x11]
	sub w9, w9, w12
	str w9, [x11], #4
	add w10, w10, #1
	b .Lmain_bb57
.Lmain_bb59:
	add w27, w27, #1
	b .Lmain_bb48
.Lmain_bb61:
	movz w15, #32769
	mov w16, w3
	movz w19, #65535
	movk w15, #32768, lsl #16
	b .Lmain_bb8
.Lmain_bb63:
	mov w19, w3
	b .Lmain_bb14
.Lmain_bb64:
	movz w10, #21846
	mov w11, w25
	movz w12, #3
	movk w10, #21845, lsl #16
	b .Lmain_bb17
.Lmain_bb79:
	mov x11, x17
	mov w10, w16
	b .Lmain_bb57
.Lmain_bb80:
	mov x11, x9
	b .Lmain_bb57
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, __sysy_par_ctx_0_0
	ldr x14, [x9, :lo12:__sysy_par_ctx_0_0]
	adrp x9, __sysy_par_scalar_start_0
	ldr w16, [x9, :lo12:__sysy_par_scalar_start_0]
	mov w15, w0
	movi v18.4s, #0
	mov w17, w1
	add x12, x14, w15, sxtw #2
	mov w8, w15
	orr w11, wzr, #0x7ffffff8
.L__sysy_par_body_0_bb1:
	cmp w8, w11
	cset w10, le
	add w9, w8, #7
	cmp w9, w17
	cset w9, lt
	and w9, w10, w9
	cbz w9, .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb2:
	ldp q17, q16, [x12]
	add v17.4s, v18.4s, v17.4s
	add v18.4s, v17.4s, v16.4s
	add x9, x12, #16
	add w8, w8, #8
	add x12, x9, #16
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb3:
	addv s16, v18.4s
	add x7, x14, w8, sxtw #2
	fmov w13, s16
	sub w6, w17, #3
	orr w14, wzr, #0x80000003
.L__sysy_par_body_0_bb4:
	cmp w8, w6
	cset w10, lt
	cmp w17, w14
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb8:
	ldp w12, w11, [x7]
	ldp w10, w9, [x7, #8]
	add w12, w13, w12
	add x13, x7, #4
	add w12, w12, w11
	add x11, x13, #4
	add w10, w12, w10
	add x12, x11, #4
	add w13, w10, w9
	add w8, w8, #4
	add x7, x12, #4
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb5:
	cmp w10, w17
	b.ge .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb7:
	ldr w9, [x12], #4
	add w11, w11, w9
	add w10, w10, #1
	b .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb6:
	adrp x10, __sysy_par_scalar_partial_0_0
	adrp x9, __sysy_par_scalar_partial_0_1
	cmp w15, w16
	add x10, x10, :lo12:__sysy_par_scalar_partial_0_0
	add x9, x9, :lo12:__sysy_par_scalar_partial_0_1
	csel x9, x10, x9, eq
	str w11, [x9]
	ret
.L__sysy_par_body_0_bb9:
	mov x12, x7
	mov w10, w8
	mov w11, w13
	b .L__sysy_par_body_0_bb5
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	adrp x9, __sysy_par_ctx_1_0
	ldr w11, [x9, :lo12:__sysy_par_ctx_1_0]
	adrp x9, __sysy_par_ctx_1_1
	ldr x10, [x9, :lo12:__sysy_par_ctx_1_1]
	adrp x9, __sysy_par_scalar_start_1
	ldr w15, [x9, :lo12:__sysy_par_scalar_start_1]
	mov w14, w0
	mov w16, w1
	add w9, w11, w14
	movz w13, #0
	add x7, x10, w9, sxtw #2
	sub w6, w16, #3
	mov w8, w14
	orr w17, wzr, #0x80000003
.L__sysy_par_body_1_bb1:
	cmp w8, w6
	cset w10, lt
	cmp w16, w17
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_1_bb3
.L__sysy_par_body_1_bb2:
	ldp w12, w11, [x7]
	ldp w10, w9, [x7, #8]
	add w12, w13, w12
	add x13, x7, #4
	add w12, w12, w11
	add x11, x13, #4
	add w10, w12, w10
	add x12, x11, #4
	add w13, w10, w9
	add w8, w8, #4
	add x7, x12, #4
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb3:
	movi v18.4s, #0
	mov v18.s[0], w13
	mov x12, x7
	mov w13, w8
	orr w11, wzr, #0x7ffffff8
.L__sysy_par_body_1_bb4:
	cmp w13, w11
	cset w10, le
	add w9, w13, #7
	cmp w9, w16
	cset w9, lt
	and w9, w10, w9
	cbz w9, .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb5:
	ldp q17, q16, [x12]
	add v17.4s, v18.4s, v17.4s
	add v18.4s, v17.4s, v16.4s
	add x9, x12, #16
	add w13, w13, #8
	add x12, x9, #16
	b .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb6:
	addv s16, v18.4s
	fmov w11, s16
	mov w10, w13
.L__sysy_par_body_1_bb7:
	cmp w10, w16
	b.ge .L__sysy_par_body_1_bb8
.L__sysy_par_body_1_bb9:
	ldr w9, [x12], #4
	add w11, w11, w9
	add w10, w10, #1
	b .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb8:
	adrp x10, __sysy_par_scalar_partial_1_0
	adrp x9, __sysy_par_scalar_partial_1_1
	cmp w14, w15
	add x10, x10, :lo12:__sysy_par_scalar_partial_1_0
	add x9, x9, :lo12:__sysy_par_scalar_partial_1_1
	csel x9, x10, x9, eq
	str w11, [x9]
	ret
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.p2align 2
	.global __sysy_par_body_2
	.type __sysy_par_body_2, %function
__sysy_par_body_2:
	adrp x9, __sysy_par_ctx_2_0
	ldr x9, [x9, :lo12:__sysy_par_ctx_2_0]
	mov w5, w0
	mov w15, w1
	movz w13, #2027
	add x7, x9, w5, sxtw #2
	sub w6, w15, #3
	orr w8, wzr, #0x80000003
	movz w17, #97
	movk w13, #5405, lsl #16
.L__sysy_par_body_2_bb1:
	cmp w5, w6
	cset w10, lt
	cmp w15, w8
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_2_bb6
.L__sysy_par_body_2_bb5:
	ldr w3, [x7]
	ldr w16, [x7, #4]
	ldr w12, [x7, #8]
	ldr w10, [x7, #12]
	add w4, w3, #3
	mul w4, w4, w3
	add w14, w16, #3
	add w11, w12, #3
	mul w14, w14, w16
	mul w11, w11, w12
	add w9, w10, #3
	sub w3, w4, #7
	mul w9, w9, w10
	sub w4, w14, #7
	sub w16, w11, #7
	smull x11, w4, w13
	sub w14, w9, #7
	smull x12, w3, w13
	asr x11, x11, #35
	smull x9, w14, w13
	add w11, w11, w11, lsr #31
	smull x10, w16, w13
	asr x12, x12, #35
	msub w4, w11, w17, w4
	asr x9, x9, #35
	add w12, w12, w12, lsr #31
	msub w2, w12, w17, w3
	add w9, w9, w9, lsr #31
	msub w11, w9, w17, w14
	asr x10, x10, #35
	add w10, w10, w10, lsr #31
	msub w12, w10, w17, w16
	add x3, x7, #4
	add x9, x3, #4
	add x9, x9, #4
	str w2, [x7]
	add x9, x9, #4
	str w4, [x7, #4]
	str w12, [x7, #8]
	str w11, [x7, #12]
	add w5, w5, #4
	mov x7, x9
	b .L__sysy_par_body_2_bb1
.L__sysy_par_body_2_bb2:
	cmp w5, w15
	b.ge .L__sysy_par_body_2_bb3
.L__sysy_par_body_2_bb4:
	ldr w11, [x7]
	add w9, w11, #3
	mul w9, w9, w11
	sub w11, w9, #7
	smull x9, w11, w10
	asr x9, x9, #35
	add w9, w9, w9, lsr #31
	msub w9, w9, w12, w11
	str w9, [x7], #4
	add w5, w5, #1
	b .L__sysy_par_body_2_bb2
.L__sysy_par_body_2_bb3:
	ret
.L__sysy_par_body_2_bb6:
	movz w10, #2027
	movz w12, #97
	movk w10, #5405, lsl #16
	b .L__sysy_par_body_2_bb2
	.size __sysy_par_body_2, .-__sysy_par_body_2
	.data
	.global state
	.p2align 2
state:
	.zero 4
	.global repeat_factor
	.p2align 2
repeat_factor:
	.zero 4
	.global N_eff
	.p2align 2
N_eff:
	.zero 4
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
	.global __sysy_par_ctx_1_0
	.p2align 2
__sysy_par_ctx_1_0:
	.zero 4
	.global __sysy_par_ctx_1_1
	.p2align 3
__sysy_par_ctx_1_1:
	.zero 8
	.global __sysy_par_scalar_start_1
	.p2align 2
__sysy_par_scalar_start_1:
	.zero 4
	.global __sysy_par_scalar_bound_1
	.p2align 2
__sysy_par_scalar_bound_1:
	.zero 4
	.global __sysy_par_scalar_partial_1_0
	.p2align 2
__sysy_par_scalar_partial_1_0:
	.zero 4
	.global __sysy_par_scalar_partial_1_1
	.p2align 2
__sysy_par_scalar_partial_1_1:
	.zero 4
	.global __sysy_par_ctx_2_0
	.p2align 3
__sysy_par_ctx_2_0:
	.zero 8

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
