	.arch armv8-a
	.text
	.p2align 2
	.global multiply
	.type multiply, %function
multiply:
	mov w16, w1
	mov w15, w0
	cmp w16, #0
	b.le .Lmultiply_bb14
.Lmultiply_bb1:
	movz w9, #1
	movk w9, #15232, lsl #16
	cmp w15, w9
	b.lo .Lmultiply_bb2
.Lmultiply_bb3:
	movz w9, #51217
	movk w9, #4405, lsl #16
	smull x9, w15, w9
	asr x9, x9, #58
	movz w12, #1
	movk w12, #15232, lsl #16
	add w9, w9, w9, lsr #31
	msub w0, w9, w12, w15
	clz w11, w16
	movz w10, #31
	sub w10, w10, w11
	movz w9, #1
	lsl w9, w9, w10
	lsr w14, w9, #1
	cbz w14, .Lmultiply_bb8
.Lmultiply_bb9:
	movz w13, #1
	movz w10, #51217
	movk w13, #15232, lsl #16
	movn w12, #15232, lsl #16
	movk w10, #4405, lsl #16
	b .Lmultiply_bb4
.Lmultiply_bb6:
	lsr w14, w14, #1
	cbz w14, .Lmultiply_bb8
.Lmultiply_bb4:
	lsl w11, w0, #1
	cmp w11, w13
	sub w9, w11, w13
	csel w11, w9, w11, ge
	cmp w11, w12
	add w9, w11, w13
	csel w0, w9, w11, le
	tst w16, w14
	b.eq .Lmultiply_bb6
.Lmultiply_bb5:
	add w11, w0, w15
	smull x9, w11, w10
	asr x9, x9, #58
	add w9, w9, w9, lsr #31
	msub w0, w9, w13, w11
	b .Lmultiply_bb6
.Lmultiply_bb8:
	ret
.Lmultiply_bb2:
	smull x12, w15, w16
	cmp x12, #0
	neg x9, x12
	csel x10, x9, x12, lt
	movz x9, #1082
	movk x9, #19826, lsl #16
	movk x9, #4, lsl #32
	umulh x9, x10, x9
	movz x11, #1
	movk x11, #15232, lsl #16
	msub x10, x9, x11, x10
	sub x9, x10, x11
	cmp x10, x11
	csel x10, x9, x10, hs
	neg x9, x10
	cmp x12, #0
	csel x9, x9, x10, lt
	mov w0, w9
	b .Lmultiply_bb8
.Lmultiply_bb14:
	movz w0, #0
	b .Lmultiply_bb8
	.size multiply, .-multiply
	.p2align 2
	.global power
	.type power, %function
power:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #16
	stp x19, x20, [sp]
	mov w20, w0
	mov w19, w1
	cbz w19, .Lpower_bb4
.Lpower_bb1:
	add w9, w19, w19, lsr #31
	asr w1, w9, #1
	mov w0, w20
	bl power
	mov w1, w0
	mov w0, w1
	bl multiply
	cmp w19, #0
	and w9, w19, #1
	cneg w9, w9, mi
	mov w10, w0
	cmp w9, #1
	b.eq .Lpower_bb3
.Lpower_bb2:
	mov w0, w10
	ldp x19, x20, [sp]
	add sp, sp, #16
	ldp xzr, x30, [sp], #16
	ret
.Lpower_bb3:
	mov w1, w20
	ldp x19, x20, [sp]
	mov w0, w10
	add sp, sp, #16
	ldp xzr, x30, [sp], #16
	b multiply
.Lpower_bb4:
	movz w10, #1
	b .Lpower_bb2
	.size power, .-power
	.p2align 2
	.global fft
	.type fft, %function
fft:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	stp x21, x22, [sp, #16]
	mov w13, w2
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	mov x26, x0
	stp x19, x20, [sp]
	mov w25, w1
	stp x27, x28, [sp, #64]
	mov w24, w3
	movz w23, #0
	movz w22, #1
	cmp w13, #1
	b.eq .Lfft_bb40
.Lfft_bb1:
	add w9, w13, w13, lsr #31
	add x20, x26, w25, sxtw #2
	asr w21, w9, #1
	mov x19, x20
	mov w17, w23
	movz w16, #1
	movz w15, #0
.Lfft_bb2:
	cmp w17, w13
	b.ge .Lfft_bb4
.Lfft_bb3:
	and w9, w17, w16
	ldr w14, [x19], #4
	cmp w9, #0
	lsr w11, w17, #1
	csel w10, w15, w21, eq
	adrp x9, temp
	add w12, w11, w10
	add x11, x9, :lo12:temp
	str w14, [x11, w12, sxtw #2]
	add w17, w17, #1
	b .Lfft_bb2
.Lfft_bb4:
	adrp x9, temp
	add x9, x9, :lo12:temp
	mov x16, x9
	sub w19, w13, #3
	mov x17, x20
	mov w15, w23
	orr w14, wzr, #0x80000003
.Lfft_bb5:
	cmp w15, w19
	cset w10, lt
	cmp w13, w14
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lfft_bb7
.Lfft_bb6:
	ldr w9, [x16]
	str w9, [x17]
	ldr w9, [x16, #4]
	str w9, [x17, #4]
	ldr w9, [x16, #8]
	str w9, [x17, #8]
	ldr w9, [x16, #12]
	add x10, x17, #4
	add x11, x16, #4
	add x10, x10, #4
	add x11, x11, #4
	str w9, [x17, #12]
	add x10, x10, #4
	add x12, x11, #4
	add w15, w15, #4
	add x17, x10, #4
	add x16, x12, #4
	b .Lfft_bb5
.Lfft_bb7:
	orr w9, wzr, #0x7ffffff0
	cmp w15, w9
	cset w12, le
	add w9, w15, #15
	cmp w9, w13
	sub w10, w13, w15
	cset w11, lt
	add x9, x16, w10, sxtw #2
	cmp x9, x17
	add x9, x17, w10, sxtw #2
	cset w10, ls
	cmp x9, x16
	cset w9, ls
	and w11, w12, w11
	orr w9, w10, w9
	sub w14, w13, #8
	and w9, w11, w9
	cbz w9, .Lfft_bb42
.Lfft_bb41:
	mov x9, x17
	mov x12, x16
	mov w11, w15
.Lfft_bb8:
	cmp w11, w14
	b.gt .Lfft_bb43
.Lfft_bb9:
	ldp q17, q16, [x12]
	stp q17, q16, [x9]
	add w11, w11, #8
	add x12, x12, #32
	add x9, x9, #32
	b .Lfft_bb8
.Lfft_bb11:
	cmp w11, w13
	b.ge .Lfft_bb13
.Lfft_bb12:
	ldr w9, [x12], #4
	add w11, w11, #1
	str w9, [x10], #4
	b .Lfft_bb11
.Lfft_bb13:
	cmp w24, #0
	cset w19, gt
	cbz w19, .Lfft_bb44
.Lfft_bb14:
	movz w9, #1
	movk w9, #15232, lsl #16
	cmp w24, w9
	b.lo .Lfft_bb15
.Lfft_bb16:
	movz w10, #51217
	movk w10, #4405, lsl #16
	smull x9, w24, w10
	asr x9, x9, #58
	movz w14, #1
	movk w14, #15232, lsl #16
	add w9, w9, w9, lsr #31
	msub w15, w9, w14, w24
	clz w12, w24
	movz w11, #31
	sub w11, w11, w12
	movz w9, #1
	lsl w9, w9, w11
	lsr w13, w9, #1
	mov w11, w15
	movn w12, #15232, lsl #16
.Lfft_bb17:
	cbz w13, .Lfft_bb45
.Lfft_bb37:
	lsl w11, w11, #1
	cmp w11, w14
	sub w9, w11, w14
	csel w11, w9, w11, ge
	cmp w11, w12
	add w9, w11, w14
	csel w11, w9, w11, le
	tst w24, w13
	b.eq .Lfft_bb39
.Lfft_bb38:
	add w11, w11, w24
	smull x9, w11, w10
	asr x9, x9, #58
	add w9, w9, w9, lsr #31
	msub w11, w9, w14, w11
.Lfft_bb39:
	lsr w13, w13, #1
	b .Lfft_bb17
.Lfft_bb15:
	smull x12, w24, w24
	cmp x12, #0
	neg x9, x12
	csel x10, x9, x12, lt
	movz x9, #1082
	movk x9, #19826, lsl #16
	movk x9, #4, lsl #32
	umulh x9, x10, x9
	movz x11, #1
	movk x11, #15232, lsl #16
	msub x10, x9, x11, x10
	sub x9, x10, x11
	cmp x10, x11
	csel x10, x9, x10, hs
	neg x9, x10
	cmp x12, #0
	csel x9, x9, x10, lt
	mov w27, w9
.Lfft_bb18:
	mov x0, x26
	mov w1, w25
	mov w2, w21
	mov w3, w27
	bl fft
	add w1, w25, w21
	mov x0, x26
	mov w2, w21
	mov w3, w27
	bl fft
	clz w10, w24
	movz w9, #31
	sub w10, w9, w10
	movz w9, #1
	movz x13, #1082
	lsl w9, w9, w10
	movz w14, #1
	movk x13, #19826, lsl #16
	movz x12, #1
	movz w11, #51217
	lsr w15, w9, #1
	mov w16, w23
	movk w14, #15232, lsl #16
	movk x13, #4, lsl #32
	movk x12, #15232, lsl #16
	movk w11, #4405, lsl #16
.Lfft_bb19:
	cmp w16, w21
	b.ge .Lfft_bb54
.Lfft_bb20:
	add w9, w25, w16
	add w9, w9, w21
	add x17, x26, w9, sxtw #2
	ldr w8, [x17]
	ldr w27, [x20]
	cmp w8, #0
	b.le .Lfft_bb46
.Lfft_bb21:
	cmp w22, w14
	b.lo .Lfft_bb22
.Lfft_bb23:
	movz w10, #51217
	movk w10, #4405, lsl #16
	smull x9, w22, w10
	asr x9, x9, #58
	movz w7, #1
	movk w7, #15232, lsl #16
	add w9, w9, w9, lsr #31
	msub w4, w9, w7, w22
	clz w6, w8
	movz w28, #31
	sub w28, w28, w6
	movz w9, #1
	lsl w9, w9, w28
	lsr w5, w9, #1
	mov w28, w4
	movn w6, #15232, lsl #16
.Lfft_bb24:
	cbz w5, .Lfft_bb47
.Lfft_bb34:
	lsl w28, w28, #1
	cmp w28, w7
	sub w9, w28, w7
	csel w28, w9, w28, ge
	cmp w28, w6
	add w9, w28, w7
	csel w28, w9, w28, le
	tst w8, w5
	b.eq .Lfft_bb36
.Lfft_bb35:
	add w28, w28, w22
	smull x9, w28, w10
	asr x9, x9, #58
	add w9, w9, w9, lsr #31
	msub w28, w9, w7, w28
.Lfft_bb36:
	lsr w5, w5, #1
	b .Lfft_bb24
.Lfft_bb22:
	smull x28, w22, w8
	cmp x28, #0
	neg x9, x28
	csel x10, x9, x28, lt
	umulh x9, x10, x13
	msub x10, x9, x12, x10
	sub x9, x10, x12
	cmp x10, x12
	csel x10, x9, x10, hs
	neg x9, x10
	cmp x28, #0
	csel x9, x9, x10, lt
.Lfft_bb25:
	add w28, w27, w9
	sub w9, w27, w9
	smull x10, w28, w11
	add w27, w9, w14
	smull x9, w27, w11
	asr x10, x10, #58
	add w10, w10, w10, lsr #31
	asr x9, x9, #58
	msub w10, w10, w14, w28
	add w9, w9, w9, lsr #31
	msub w9, w9, w14, w27
	str w10, [x20]
	str w9, [x17]
	cbz w19, .Lfft_bb48
.Lfft_bb26:
	cmp w22, w14
	b.lo .Lfft_bb27
.Lfft_bb28:
	movz w10, #51217
	movk w10, #4405, lsl #16
	smull x9, w22, w10
	asr x9, x9, #58
	movz w28, #1
	movk w28, #15232, lsl #16
	add w9, w9, w9, lsr #31
	msub w17, w9, w28, w22
	mov w8, w15
	movn w27, #15232, lsl #16
.Lfft_bb29:
	cbz w8, .Lfft_bb30
.Lfft_bb31:
	lsl w17, w17, #1
	cmp w17, w28
	sub w9, w17, w28
	csel w17, w9, w17, ge
	cmp w17, w27
	add w9, w17, w28
	csel w17, w9, w17, le
	tst w24, w8
	b.eq .Lfft_bb33
.Lfft_bb32:
	add w17, w17, w22
	smull x9, w17, w10
	asr x9, x9, #58
	add w9, w9, w9, lsr #31
	msub w17, w9, w28, w17
.Lfft_bb33:
	lsr w8, w8, #1
	b .Lfft_bb29
.Lfft_bb27:
	smull x17, w22, w24
	cmp x17, #0
	neg x9, x17
	csel x10, x9, x17, lt
	umulh x9, x10, x13
	msub x10, x9, x12, x10
	sub x9, x10, x12
	cmp x10, x12
	csel x10, x9, x10, hs
	neg x9, x10
	cmp x17, #0
	csel x9, x9, x10, lt
	mov w22, w9
	mov w17, w22
.Lfft_bb30:
	add w16, w16, #1
	add x20, x20, #4
	mov w22, w17
	b .Lfft_bb19
.Lfft_bb40:
	mov w0, w22
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lfft_bb42:
	mov x10, x17
	mov x12, x16
	mov w11, w15
	b .Lfft_bb11
.Lfft_bb43:
	mov x10, x9
	b .Lfft_bb11
.Lfft_bb44:
	mov w27, w23
	b .Lfft_bb18
.Lfft_bb45:
	mov w27, w11
	b .Lfft_bb18
.Lfft_bb46:
	mov w9, w23
	b .Lfft_bb25
.Lfft_bb47:
	mov w9, w28
	b .Lfft_bb25
.Lfft_bb48:
	mov w17, w23
	b .Lfft_bb30
.Lfft_bb54:
	mov w22, w23
	b .Lfft_bb40
	.size fft, .-fft
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x19, x20, [sp]
	adrp x9, a
	stp x21, x22, [sp, #16]
	add x20, x9, :lo12:a
	stp x23, x24, [sp, #32]
	movz w22, #1
	mov x0, x20
	bl getarray
	adrp x9, b
	add x24, x9, :lo12:b
	mov w21, w0
	mov x0, x24
	bl getarray
	mov w19, w0
	movz w0, #60
	bl _sysy_starttime
	add w9, w21, w19
	sub w21, w9, #1
	cmp w9, #2
	b.le .Lmain_bb5
.Lmain_bb3:
	mov w19, w22
.Lmain_bb1:
	lsl w19, w19, #1
	cmp w19, w21
	b.lt .Lmain_bb1
.Lmain_bb2:
	movz w9, #15232, lsl #16
	sdiv w22, w9, w19
	movz w0, #5
	mov w1, w22
	bl power
	mov w23, w0
	movz w1, #0
	mov x0, x20
	mov w2, w19
	mov w3, w23
	bl fft
	movz w1, #0
	mov x0, x24
	mov w2, w19
	mov w3, w23
	bl fft
	movz w0, #1
	movz w1, #0
	mov w2, w19
	bl __sysy_parallel_for
	movz w9, #15232, lsl #16
	sub w1, w9, w22
	movz w0, #5
	bl power
	mov w3, w0
	movz w1, #0
	mov x0, x20
	mov w2, w19
	bl fft
	movn w1, #50304, lsl #16
	mov w0, w19
	bl power
	clz w10, w0
	movz w9, #31
	sub w10, w9, w10
	movz w9, #1
	lsl w9, w9, w10
	lsr w11, w9, #1
	adrp x10, __sysy_par_ctx_0_0
	str w0, [x10, :lo12:__sysy_par_ctx_0_0]
	adrp x9, __sysy_par_ctx_0_1
	movz w1, #0
	str w11, [x9, :lo12:__sysy_par_ctx_0_1]
	mov w0, w1
	mov w2, w19
	bl __sysy_parallel_for
	movz w0, #79
	bl _sysy_stoptime
	mov w0, w21
	mov x1, x20
	bl putarray
	adrp x9, d
	str w19, [x9, :lo12:d]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb5:
	mov w19, w22
	b .Lmain_bb2
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x10, __sysy_par_ctx_0_0
	adrp x9, __sysy_par_ctx_0_1
	ldr w17, [x10, :lo12:__sysy_par_ctx_0_0]
	ldr w16, [x9, :lo12:__sysy_par_ctx_0_1]
	movz x13, #1082
	movz w15, #1
	movk x13, #19826, lsl #16
	movz x12, #1
	movz w11, #51217
	mov w6, w0
	mov w8, w1
	movz w7, #0
	movk w15, #15232, lsl #16
	movk x13, #4, lsl #32
	movk x12, #15232, lsl #16
	movk w11, #4405, lsl #16
.L__sysy_par_body_0_bb1:
	cmp w6, w8
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	adrp x9, a
	add x9, x9, :lo12:a
	add x5, x9, w6, sxtw #2
	ldr w2, [x5]
	cmp w17, #0
	b.le .L__sysy_par_body_0_bb17
.L__sysy_par_body_0_bb4:
	cmp w2, w15
	b.lo .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb6:
	smull x9, w2, w11
	asr x9, x9, #58
	add w9, w9, w9, lsr #31
	msub w10, w9, w15, w2
	cbz w16, .L__sysy_par_body_0_bb11
.L__sysy_par_body_0_bb12:
	mov w14, w10
	movz w3, #1
	movz w10, #51217
	mov w1, w16
	movk w3, #15232, lsl #16
	movn w4, #15232, lsl #16
	movk w10, #4405, lsl #16
	b .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb9:
	lsr w1, w1, #1
	cbz w1, .L__sysy_par_body_0_bb16
.L__sysy_par_body_0_bb7:
	lsl w14, w14, #1
	cmp w14, w3
	sub w9, w14, w3
	csel w14, w9, w14, ge
	cmp w14, w4
	add w9, w14, w3
	csel w14, w9, w14, le
	tst w17, w1
	b.eq .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb8:
	add w14, w14, w2
	smull x9, w14, w10
	asr x9, x9, #58
	add w9, w9, w9, lsr #31
	msub w14, w9, w3, w14
	b .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb16:
	mov w10, w14
.L__sysy_par_body_0_bb11:
	add w6, w6, #1
	str w10, [x5]
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb5:
	smull x14, w2, w17
	cmp x14, #0
	neg x9, x14
	csel x10, x9, x14, lt
	umulh x9, x10, x13
	msub x10, x9, x12, x10
	sub x9, x10, x12
	cmp x10, x12
	csel x10, x9, x10, hs
	neg x9, x10
	cmp x14, #0
	csel x9, x9, x10, lt
	mov w10, w9
	b .L__sysy_par_body_0_bb11
.L__sysy_par_body_0_bb17:
	mov w10, w7
	b .L__sysy_par_body_0_bb11
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	movz x13, #1082
	movz w17, #1
	movk x13, #19826, lsl #16
	movz x12, #1
	movz w11, #51217
	mov w6, w0
	mov w8, w1
	movz w7, #0
	movk w17, #15232, lsl #16
	movk x13, #4, lsl #32
	movk x12, #15232, lsl #16
	movk w11, #4405, lsl #16
	movz w16, #31
	movz w15, #1
.L__sysy_par_body_1_bb1:
	cmp w6, w8
	b.ge .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb3:
	adrp x9, a
	add x10, x9, :lo12:a
	adrp x9, b
	add x9, x9, :lo12:b
	ldr w3, [x9, w6, sxtw #2]
	add x5, x10, w6, sxtw #2
	ldr w2, [x5]
	cmp w3, #0
	b.le .L__sysy_par_body_1_bb17
.L__sysy_par_body_1_bb4:
	cmp w2, w17
	b.lo .L__sysy_par_body_1_bb5
.L__sysy_par_body_1_bb6:
	smull x9, w2, w11
	asr x9, x9, #58
	add w9, w9, w9, lsr #31
	msub w14, w9, w17, w2
	clz w10, w3
	sub w9, w16, w10
	lsl w9, w15, w9
	lsr w0, w9, #1
	cbz w0, .L__sysy_par_body_1_bb15
.L__sysy_par_body_1_bb12:
	movz w1, #1
	movz w10, #51217
	movk w1, #15232, lsl #16
	movn w4, #15232, lsl #16
	movk w10, #4405, lsl #16
	b .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb9:
	lsr w0, w0, #1
	cbz w0, .L__sysy_par_body_1_bb16
.L__sysy_par_body_1_bb7:
	lsl w14, w14, #1
	cmp w14, w1
	sub w9, w14, w1
	csel w14, w9, w14, ge
	cmp w14, w4
	add w9, w14, w1
	csel w14, w9, w14, le
	tst w3, w0
	b.eq .L__sysy_par_body_1_bb9
.L__sysy_par_body_1_bb8:
	add w14, w14, w2
	smull x9, w14, w10
	asr x9, x9, #58
	add w9, w9, w9, lsr #31
	msub w14, w9, w1, w14
	b .L__sysy_par_body_1_bb9
.L__sysy_par_body_1_bb16:
	mov w10, w14
.L__sysy_par_body_1_bb11:
	add w6, w6, #1
	str w10, [x5]
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb2:
	ret
.L__sysy_par_body_1_bb5:
	smull x14, w2, w3
	cmp x14, #0
	neg x9, x14
	csel x10, x9, x14, lt
	umulh x9, x10, x13
	msub x10, x9, x12, x10
	sub x9, x10, x12
	cmp x10, x12
	csel x10, x9, x10, hs
	neg x9, x10
	cmp x14, #0
	csel x9, x9, x10, lt
	mov w10, w9
	b .L__sysy_par_body_1_bb11
.L__sysy_par_body_1_bb15:
	mov w10, w14
	b .L__sysy_par_body_1_bb11
.L__sysy_par_body_1_bb17:
	mov w10, w7
	b .L__sysy_par_body_1_bb11
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.data
	.global d
	.p2align 2
d:
	.zero 4
	.bss
	.global temp
	.p2align 4
temp:
	.zero 8388608
	.global a
	.p2align 4
a:
	.zero 8388608
	.global b
	.p2align 4
b:
	.zero 8388608
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_ctx_0_1
	.p2align 2
__sysy_par_ctx_0_1:
	.zero 4

	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	cmp w0, #1
	b.eq .Lsysy_disp_1
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
.Lsysy_disp_1:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_1

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
