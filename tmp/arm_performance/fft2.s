	.text
	.global multiply
	.p2align 2
multiply:
	sub sp, sp, #32
	stp x20, x19, [sp, #0]
	str x30, [sp, #16]
	mov w19, w0
	mov w20, w1
	cbnz w20, multiply_label_if_else_2
	movz w9, #0
multiply_label_ret:
	mov w0, w9
	b .Lmultiply_epilogue
multiply_label_if_else_2:
	cmp w20, #1
	b.eq multiply_label_if_then_3
multiply_label_if_else_4:
	add w9, w20, w20, lsr #31
	asr w9, w9, #1
	mov w0, w19
	mov w1, w9
	bl multiply
	mov w9, w0
	movz w10, #1
	lsl w0, w9, #1
	movk w10, #15232, lsl #16
	sub w9, w0, w10
	movz w10, #1
	movk w10, #15232, lsl #16
	cmp w0, w10
	csel w9, w9, w0, ge
	tst w20, w20
	and w0, w20, #1
	cneg w0, w0, mi
	cmp w0, #1
	b.eq multiply_label_if_then_5
	b multiply_label_ret
multiply_label_if_then_5:
	movz w10, #51217
	add w0, w9, w19
	movk w10, #4405, lsl #16
	smull x11, w0, w10
	movz w12, #1
	movk w12, #15232, lsl #16
	asr x11, x11, #58
	add w11, w11, w0, lsr #31
	msub w9, w11, w12, w0
	b multiply_label_ret
multiply_label_if_then_3:
	movz w10, #51217
	movk w10, #4405, lsl #16
	smull x11, w19, w10
	movz w12, #1
	movk w12, #15232, lsl #16
	asr x11, x11, #58
	add w11, w11, w19, lsr #31
	msub w9, w11, w12, w19
	b multiply_label_ret
.Lmultiply_epilogue:
	ldp x20, x19, [sp, #0]
	ldr x30, [sp, #16]
	add sp, sp, #32
	ret
	.global power
	.p2align 2
power:
	sub sp, sp, #32
	stp x20, x19, [sp, #0]
	str x30, [sp, #16]
	mov w19, w0
	mov w20, w1
	cbnz w20, power_label_if_else_9
	movz w0, #1
power_label_ret:
	b .Lpower_epilogue
power_label_if_else_9:
	add w9, w20, w20, lsr #31
	asr w9, w9, #1
	mov w0, w19
	mov w1, w9
	bl power
	mov	w1, w0
	bl multiply
	tst w20, w20
	and w9, w20, #1
	cneg w9, w9, mi
	cmp w9, #1
	b.eq power_label_if_then_10
	b	.Lpower_epilogue
power_label_if_then_10:
	ldr x30, [sp, #16]
	mov w1, w19
	ldp x20, x19, [sp, #0]
	add sp, sp, #32
	b multiply
.Lpower_epilogue:
	ldp x20, x19, [sp, #0]
	ldr x30, [sp, #16]
	add sp, sp, #32
	ret
	.global fft
	.p2align 2
fft:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #144
	stp x28, x27, [sp, #64]
	mov w28, w2
	cmp w28, #1
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	str w3, [x29, #-8]
	mov x26, x0
	mov w27, w1
	b.ne fft_label_while_cond_18.preheader
	movz w9, #1
fft_label_ret:
	mov w0, w9
	b .Lfft_epilogue
fft_label_while_cond_18.preheader:
	mov x24, x26
	add w25, w28, w28, lsr #31
	add x24, x24, w27, uxtw #2
	asr w25, w25, #1
	mov x7, x24
	mov x6, x24
	movz w5, #0
fft_label_while_cond_18:
	cmp w5, w28
	b.lt fft_label_while_body_19
fft_label_while_end_20:
	adrp x9, temp
	sub w12, w28, #3
	add	x4, x9, :lo12:temp
	str w12, [x29, #-24]
	mov x3, x24
	movz w2, #0
fft_label_286:
	ldr w12, [x29, #-24]
	cmp w2, w12
	b.lt fft_label_291
fft_label_121:
	cmp w2, w28
	b.lt fft_label_124
fft_label_130:
	ldr w12, [x29, #-8]
	cmp w12, wzr
	cset w13, eq
	mov w20, w13
	cbz w20, fft_label_99
	movz w19, #0
fft_label_116:
	mov x0, x26
	mov w1, w27
	mov w2, w25
	mov w3, w19
	bl fft
	add w9, w27, w25
	mov x0, x26
	mov w1, w9
	mov w2, w25
	mov w3, w19
	bl fft
	movz w11, #51217
	movz w10, #1
	movk w11, #4405, lsl #16
	movk w10, #15232, lsl #16
	cbz w20, fft_label_116.unsw.f
	movz w23, #1
	movz w22, #0
fft_label_while_cond_24:
	cmp w22, w25
	b.lt fft_label_while_body_25
	movz w9, #0
	b fft_label_ret
fft_label_while_body_25:
	add w9, w27, w22
	add w9, w9, w25
	mov x21, x26
	add x21, x21, w9, sxtw #2
	ldr w20, [x24]
	ldr w19, [x21]
	cbnz w19, fft_label_75
	movz w9, #0
fft_label_92:
	add w0, w20, w9
	smull x12, w0, w11
	sub w9, w20, w9
	add w9, w9, w10
	asr x12, x12, #58
	add w12, w12, w0, lsr #31
	msub w0, w12, w10, w0
	smull x12, w9, w11
	add w22, w22, #1
	str w0, [x24]
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w9, w12, w10, w9
	add x24, x24, #4
	movz w23, #0
	str w9, [x21]
	b fft_label_while_cond_24
fft_label_while_body_19:
	tbz	w5, #0, fft_label_if_then_21
fft_label_if_else_22:
	add w9, w5, w5, lsr #31
	asr w9, w9, #1
	adrp x0, temp
	add w9, w25, w9
	add x0, x0, :lo12:temp
	add x0, x0, w9, sxtw #2
	ldr w9, [x7]
	str w9, [x0]
fft_label_if_end_23:
	add w5, w5, #1
	add x6, x6, #4
	add x7, x7, #4
	b fft_label_while_cond_18
fft_label_if_then_21:
	adrp x0, temp
	asr w9, w5, #1
	add x0, x0, :lo12:temp
	add x0, x0, w9, sxtw #2
	ldr w9, [x6]
	str w9, [x0]
	b fft_label_if_end_23
fft_label_75:
	cmp w19, #1
	b.ne fft_label_79
	mov w9, w23
	b fft_label_92
fft_label_79:
	add w9, w19, w19, lsr #31
	asr w9, w9, #1
	mov w0, w23
	mov w1, w9
	bl multiply
	movz w10, #1
	mov w9, w0
	movz w12, #1
	movk w10, #15232, lsl #16
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	sub w9, w0, w10
	cmp w0, w12
	csel w9, w9, w0, ge
	tst w19, w19
	and w0, w19, #1
	cneg w0, w0, mi
	movz w11, #51217
	cmp w0, #1
	movk w11, #4405, lsl #16
	b.eq fft_label_88
	b fft_label_92
fft_label_88:
	add w0, w9, w23
	smull x12, w0, w11
	asr x12, x12, #58
	add w12, w12, w0, lsr #31
	msub w9, w12, w10, w0
	b fft_label_92
fft_label_99:
	ldr w12, [x29, #-8]
	cmp w12, #1
	b.ne fft_label_103
	ldr w19, [x29, #-8]
	b fft_label_116
fft_label_103:
	ldr w12, [x29, #-8]
	add w9, w12, w12, lsr #31
	ldr w12, [x29, #-8]
	asr w9, w9, #1
	mov w1, w9
	mov w0, w12
	bl multiply
	mov w9, w0
	movz w12, #1
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	cmp w0, w12
	ldr w12, [x29, #-8]
	movz w10, #1
	movk w10, #15232, lsl #16
	sub w9, w0, w10
	csel w19, w9, w0, ge
	tst w12, w12
	and w9, w12, #1
	cneg w9, w9, mi
	movz w11, #51217
	cmp w9, #1
	movk w11, #4405, lsl #16
	b.eq fft_label_112
	b fft_label_116
fft_label_112:
	ldr w12, [x29, #-8]
	add w9, w19, w12
	smull x12, w9, w11
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w19, w12, w10, w9
	b fft_label_116
fft_label_124:
	ldr	w9, [x4], #4
	add w2, w2, #1
	str	w9, [x3], #4
	b fft_label_121
fft_label_while_body_25.unsw:
	add w9, w27, w22
	add w9, w9, w25
	mov x21, x26
	add x21, x21, w9, sxtw #2
	ldr w20, [x24]
	ldr w19, [x21]
	cbnz w19, fft_label_75.unsw
	movz w9, #0
fft_label_92.unsw:
	add w0, w20, w9
	smull x12, w0, w11
	sub w9, w20, w9
	add w9, w9, w10
	asr x12, x12, #58
	add w12, w12, w0, lsr #31
	msub w0, w12, w10, w0
	smull x12, w9, w11
	add w22, w22, #1
	str w0, [x24]
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w9, w12, w10, w9
	add x24, x24, #4
	str w9, [x21]
fft_label_while_cond_24.unsw:
	cmp w22, w25
	b.lt fft_label_while_body_25.unsw
	movz w9, #0
	b fft_label_ret
fft_label_75.unsw:
	cmp w19, #1
	b.ne fft_label_79.unsw
	movz w9, #1
	b fft_label_92.unsw
fft_label_79.unsw:
	add w9, w19, w19, lsr #31
	asr w9, w9, #1
	movz w0, #1
	mov w1, w9
	bl multiply
	movz w10, #1
	mov w9, w0
	movz w12, #1
	movk w10, #15232, lsl #16
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	sub w9, w0, w10
	cmp w0, w12
	csel w9, w9, w0, ge
	tst w19, w19
	and w0, w19, #1
	cneg w0, w0, mi
	movz w11, #51217
	cmp w0, #1
	movk w11, #4405, lsl #16
	b.eq fft_label_88.unsw
	b fft_label_92.unsw
fft_label_88.unsw:
	add w0, w9, #1
	smull x12, w0, w11
	asr x12, x12, #58
	add w12, w12, w0, lsr #31
	msub w9, w12, w10, w0
	b fft_label_92.unsw
fft_label_116.unsw.f:
	ldr w12, [x29, #-8]
	cmp w12, #1
	b.ne fft_label_116.unsw.f.unsw.f
	movz w22, #0
	b fft_label_while_cond_24.unsw
fft_label_116.unsw.f.unsw.f:
	ldr w12, [x29, #-8]
	add w23, w12, w12, lsr #31
	ldr w12, [x29, #-8]
	asr w23, w23, #1
	tst w12, w12
	and w9, w12, #1
	cneg w9, w9, mi
	cmp w9, #1
	b.eq .Lfft_edge_0
	movz w22, #1
	movz w21, #0
	b fft_label_while_cond_24.unsw.unsw.unsw
.Lfft_edge_0:
	movz w22, #1
	movz w21, #0
	b fft_label_while_cond_24.unsw.unsw
fft_label_while_cond_24.unsw.unsw.unsw:
	cmp w21, w25
	b.lt fft_label_while_body_25.unsw.unsw.unsw
	movz w9, #0
	b fft_label_ret
fft_label_while_body_25.unsw.unsw.unsw:
	add w9, w27, w21
	add w9, w9, w25
	mov x20, x26
	add x20, x20, w9, sxtw #2
	ldr w12, [x20]
	ldr w19, [x24]
	str w12, [x29, #-56]
	mov	w13, w12
	cbnz w13, fft_label_75.unsw.unsw.unsw
	movz w0, #0
fft_label_92.unsw.unsw.unsw:
	add w9, w19, w0
	smull x12, w9, w11
	mov w1, w23
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w9, w12, w10, w9
	str w9, [x24]
	sub w9, w19, w0
	add w9, w9, w10
	smull x12, w9, w11
	mov w0, w22
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w9, w12, w10, w9
	str w9, [x20]
	bl multiply
	movz w10, #1
	mov w9, w0
	movz w12, #1
	movk w10, #15232, lsl #16
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	movz w11, #51217
	sub w9, w0, w10
	cmp w0, w12
	movk w11, #4405, lsl #16
	csel w22, w9, w0, ge
	add w21, w21, #1
	add x24, x24, #4
	b fft_label_while_cond_24.unsw.unsw.unsw
fft_label_while_body_25.unsw.unsw:
	add w9, w27, w21
	add w9, w9, w25
	mov x20, x26
	add x20, x20, w9, sxtw #2
	ldr w12, [x20]
	ldr w19, [x24]
	str w12, [x29, #-40]
	mov	w13, w12
	cbnz w13, fft_label_75.unsw.unsw
	movz w0, #0
fft_label_92.unsw.unsw:
	add w9, w19, w0
	smull x12, w9, w11
	mov w1, w23
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w9, w12, w10, w9
	str w9, [x24]
	sub w9, w19, w0
	add w9, w9, w10
	smull x12, w9, w11
	mov w0, w22
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w9, w12, w10, w9
	str w9, [x20]
	bl multiply
	movz w10, #1
	mov w9, w0
	movz w12, #1
	movk w10, #15232, lsl #16
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	sub w9, w0, w10
	cmp w0, w12
	movz w11, #51217
	csel w9, w9, w0, ge
	movk w11, #4405, lsl #16
	add w9, w9, w22
	smull x12, w9, w11
	add w21, w21, #1
	add x24, x24, #4
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w22, w12, w10, w9
fft_label_while_cond_24.unsw.unsw:
	cmp w21, w25
	b.lt fft_label_while_body_25.unsw.unsw
	movz w9, #0
	b fft_label_ret
fft_label_75.unsw.unsw:
	ldr w12, [x29, #-40]
	cmp w12, #1
	b.eq fft_label_77.unsw.unsw
fft_label_79.unsw.unsw:
	ldr w12, [x29, #-40]
	mov w0, w22
	add w9, w12, w12, lsr #31
	asr w9, w9, #1
	mov w1, w9
	bl multiply
	mov w9, w0
	movz w12, #1
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	cmp w0, w12
	ldr w12, [x29, #-40]
	movz w10, #1
	movk w10, #15232, lsl #16
	sub w9, w0, w10
	csel w1, w9, w0, ge
	tst w12, w12
	and w9, w12, #1
	cneg w9, w9, mi
	movz w11, #51217
	cmp w9, #1
	movk w11, #4405, lsl #16
	b.eq fft_label_88.unsw.unsw
	mov w0, w1
	b fft_label_92.unsw.unsw
fft_label_88.unsw.unsw:
	add w9, w1, w22
	smull x12, w9, w11
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w0, w12, w10, w9
	b fft_label_92.unsw.unsw
fft_label_77.unsw.unsw:
	smull x12, w22, w11
	asr x12, x12, #58
	add w12, w12, w22, lsr #31
	msub	w0, w12, w10, w22
	b fft_label_92.unsw.unsw
fft_label_75.unsw.unsw.unsw:
	ldr w12, [x29, #-56]
	cmp w12, #1
	b.eq fft_label_77.unsw.unsw.unsw
fft_label_79.unsw.unsw.unsw:
	ldr w12, [x29, #-56]
	mov w0, w22
	add w9, w12, w12, lsr #31
	asr w9, w9, #1
	mov w1, w9
	bl multiply
	mov w9, w0
	movz w12, #1
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	cmp w0, w12
	ldr w12, [x29, #-56]
	movz w10, #1
	movk w10, #15232, lsl #16
	sub w9, w0, w10
	csel w1, w9, w0, ge
	tst w12, w12
	and w9, w12, #1
	cneg w9, w9, mi
	movz w11, #51217
	cmp w9, #1
	movk w11, #4405, lsl #16
	b.eq fft_label_88.unsw.unsw.unsw
	mov w0, w1
	b fft_label_92.unsw.unsw.unsw
fft_label_88.unsw.unsw.unsw:
	add w9, w1, w22
	smull x12, w9, w11
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w0, w12, w10, w9
	b fft_label_92.unsw.unsw.unsw
fft_label_77.unsw.unsw.unsw:
	smull x12, w22, w11
	asr x12, x12, #58
	add w12, w12, w22, lsr #31
	msub	w0, w12, w10, w22
	b fft_label_92.unsw.unsw.unsw
fft_label_291:
	ldr	q16, [x4], #16
	add w2, w2, #4
	str	q16, [x3], #16
	b fft_label_286
.Lfft_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	add sp, sp, #144
	ldp x29, x30, [sp], #16
	ret
	.global main
	.p2align 2
main:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #96
	adrp x12, a
	add x12, x12, :lo12:a
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	stp x28, x27, [sp, #64]
	str x12, [x29, #-8]
	mov x0, x12
	bl getarray
	adrp x28, b
	add x28, x28, :lo12:b
	mov w20, w0
	mov x0, x28
	bl getarray
	mov w19, w0
	movz w0, #60
	bl _sysy_starttime
	add w9, w20, w19
	cmp w9, #2
	sub w27, w9, #1
	b.gt .Lmain_edge_0
	movz w26, #1
	b main_label_while_end_29
.Lmain_edge_0:
	movz w26, #1
	b main_label_while_body_28
main_label_while_end_29:
	movz w12, #0
	movk w12, #15232, lsl #16
	sdiv w25, w12, w26
	movz w0, #5
	mov w1, w25
	bl power
	ldr x12, [x29, #-8]
	mov w19, w0
	mov w1, wzr
	mov w2, w26
	mov x0, x12
	mov w3, w19
	bl fft
	mov x0, x28
	mov w1, wzr
	mov w2, w26
	mov w3, w19
	bl fft
	ldr x24, [x29, #-8]
	movz w11, #51217
	movz w10, #1
	movk w11, #4405, lsl #16
	movk w10, #15232, lsl #16
	movz w23, #0
main_label_while_cond_30:
	cmp w23, w26
	b.lt main_label_while_body_31
main_label_while_end_32:
	movz w12, #0
	movk w12, #15232, lsl #16
	sub w9, w12, w25
	movz w0, #5
	mov w1, w9
	bl power
	ldr x12, [x29, #-8]
	mov w9, w0
	mov w1, wzr
	mov w2, w26
	mov x0, x12
	mov w3, w9
	bl fft
	movz w1, #65535
	mov w0, w26
	movk w1, #15231, lsl #16
	bl power
	mov w1, w0
	add w22, w1, w1, lsr #31
	tst w1, w1
	and w0, w1, #1
	asr w22, w22, #1
	cneg w0, w0, mi
	cbz w1, main_label_while_end_32.unsw.t
main_label_while_end_32.unsw.f:
	cmp w1, #1
	b.eq main_label_while_end_32.unsw.f.unsw.t
main_label_while_end_32.unsw.f.unsw.f:
	cmp w0, #1
	b.eq .Lmain_edge_1
	ldr x20, [x29, #-8]
	movz w19, #0
	b main_label_while_cond_33.unsw.unsw.unsw
.Lmain_edge_1:
	ldr x21, [x29, #-8]
	movz w20, #0
	b main_label_while_cond_33.unsw.unsw
main_label_while_cond_33.unsw.unsw.unsw:
	cmp w19, w26
	b.lt main_label_while_body_34.unsw.unsw.unsw
main_label_while_end_35:
	movz w0, #79
	bl _sysy_stoptime
	ldr x12, [x29, #-8]
	mov w0, w27
	mov x1, x12
	bl putarray
	adrp x12, d
	str w26, [x12, :lo12:d]
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_body_28:
	lsl w26, w26, #1
	cmp w26, w27
	b.lt	main_label_while_body_28
	b main_label_while_end_29
main_label_while_body_31:
	ldr w20, [x24]
	ldr w19, [x28]
	cbnz w19, main_label_65
	movz w9, #0
main_label_82:
	str	w9, [x24], #4
	add w23, w23, #1
	add x28, x28, #4
	b main_label_while_cond_30
main_label_65:
	cmp w19, #1
	b.eq main_label_67
main_label_69:
	add w9, w19, w19, lsr #31
	asr w9, w9, #1
	mov w0, w20
	mov w1, w9
	bl multiply
	movz w10, #1
	mov w9, w0
	movz w12, #1
	movk w10, #15232, lsl #16
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	sub w9, w0, w10
	cmp w0, w12
	csel w9, w9, w0, ge
	tst w19, w19
	and w0, w19, #1
	cneg w0, w0, mi
	movz w11, #51217
	cmp w0, #1
	movk w11, #4405, lsl #16
	b.eq main_label_78
	b main_label_82
main_label_78:
	add w0, w9, w20
	smull x12, w0, w11
	asr x12, x12, #58
	add w12, w12, w0, lsr #31
	msub w9, w12, w10, w0
	b main_label_82
main_label_67:
	smull x12, w20, w11
	asr x12, x12, #58
	add w12, w12, w20, lsr #31
	msub w9, w12, w10, w20
	b main_label_82
main_label_while_end_32.unsw.t:
	mov w0, wzr
	mov w1, wzr
	mov w2, w26
	bl __sysy_parallel_for
	b main_label_while_end_35
main_label_while_body_34.unsw.unsw:
	ldr w19, [x21]
	mov w1, w22
	mov w0, w19
	bl multiply
	movz w10, #1
	mov w9, w0
	movz w12, #1
	movk w10, #15232, lsl #16
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	sub w9, w0, w10
	cmp w0, w12
	movz w11, #51217
	csel w9, w9, w0, ge
	movk w11, #4405, lsl #16
	add w9, w9, w19
	smull x12, w9, w11
	add w20, w20, #1
	asr x12, x12, #58
	add w12, w12, w9, lsr #31
	msub w9, w12, w10, w9
	str	w9, [x21], #4
main_label_while_cond_33.unsw.unsw:
	cmp w20, w26
	b.lt main_label_while_body_34.unsw.unsw
	b main_label_while_end_35
main_label_while_end_32.unsw.f.unsw.t:
	movz w0, #1
	mov w1, wzr
	mov w2, w26
	bl __sysy_parallel_for
	b main_label_while_end_35
main_label_while_body_34.unsw.unsw.unsw:
	ldr w9, [x20]
	mov w1, w22
	mov w0, w9
	bl multiply
	movz w10, #1
	mov w9, w0
	movz w12, #1
	movk w10, #15232, lsl #16
	lsl w0, w9, #1
	movk w12, #15232, lsl #16
	sub w9, w0, w10
	cmp w0, w12
	csel w9, w9, w0, ge
	str	w9, [x20], #4
	add w19, w19, #1
	b main_label_while_cond_33.unsw.unsw.unsw
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	add sp, sp, #96
	ldp x29, x30, [sp], #16
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	sub sp, sp, #16
	adrp x2, a
	add x2, x2, :lo12:a
	str d8, [sp, #0]
	sub w7, w1, #3
	movi v8.4s, #0
	add x2, x2, w0, sxtw #2
	sub w6, w1, #7
	mov w5, w0
__sysy_par_body_0_label_22:
	cmp w5, w6
	b.lt __sysy_par_body_0_label_26
__sysy_par_body_0_label_7:
	cmp w5, w7
	b.lt __sysy_par_body_0_label_10
__sysy_par_body_0_label_14:
	cmp w5, w1
	b.lt __sysy_par_body_0_label_while_body_34
__sysy_par_body_0_label_17:
	b .L__sysy_par_body_0_epilogue
__sysy_par_body_0_label_while_body_34:
	adrp x3, a
	add x3, x3, :lo12:a
	add x3, x3, w5, sxtw #2
	str wzr, [x3]
	add w5, w5, #1
	b __sysy_par_body_0_label_14
__sysy_par_body_0_label_10:
	add w5, w5, #4
	str	q8, [x2], #16
	b __sysy_par_body_0_label_7
__sysy_par_body_0_label_26:
	add	x3, x2, #16
	mov x4, x2
	mov x2, x3
	add w5, w5, #8
	str	q8, [x4]
	str	q8, [x2]
	add	x2, x3, #16
	b __sysy_par_body_0_label_22
.L__sysy_par_body_0_epilogue:
	ldr d8, [sp, #0]
	add sp, sp, #16
	ret
	.global __sysy_par_body_1
	.p2align 2
__sysy_par_body_1:
	sub sp, sp, #16
	adrp x3, a
	stp x28, x27, [sp, #0]
	movz w28, #51217
	movz w27, #1
	add x3, x3, :lo12:a
	movk w28, #4405, lsl #16
	movk w27, #15232, lsl #16
	add x3, x3, w0, sxtw #2
	sub w5, w1, #3
	mov w4, w0
__sysy_par_body_1_label_6:
	cmp w4, w5
	b.lt __sysy_par_body_1_label_10
__sysy_par_body_1_label_while_cond_33.unsw:
	cmp w4, w1
	b.lt __sysy_par_body_1_label_while_body_34.unsw
__sysy_par_body_1_label_par_ret:
	b .L__sysy_par_body_1_epilogue
__sysy_par_body_1_label_while_body_34.unsw:
	ldr w2, [x3]
	add w4, w4, #1
	smull x10, w2, w28
	asr x10, x10, #58
	add w10, w10, w2, lsr #31
	msub w2, w10, w27, w2
	str	w2, [x3], #4
	b __sysy_par_body_1_label_while_cond_33.unsw
__sysy_par_body_1_label_10:
	ldr w2, [x3]
	add w4, w4, #4
	smull x10, w2, w28
	asr x10, x10, #58
	add w10, w10, w2, lsr #31
	msub w2, w10, w27, w2
	str	w2, [x3], #4
	ldr w2, [x3]
	smull x10, w2, w28
	asr x10, x10, #58
	add w10, w10, w2, lsr #31
	msub w2, w10, w27, w2
	str	w2, [x3], #4
	ldr w2, [x3]
	smull x10, w2, w28
	asr x10, x10, #58
	add w10, w10, w2, lsr #31
	msub w2, w10, w27, w2
	str	w2, [x3], #4
	ldr w2, [x3]
	smull x10, w2, w28
	asr x10, x10, #58
	add w10, w10, w2, lsr #31
	msub w2, w10, w27, w2
	str	w2, [x3], #4
	b __sysy_par_body_1_label_6
.L__sysy_par_body_1_epilogue:
	ldp x28, x27, [sp, #0]
	add sp, sp, #16
	ret
	.data
	.global d
	.p2align 2
d:
	.word 0

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
