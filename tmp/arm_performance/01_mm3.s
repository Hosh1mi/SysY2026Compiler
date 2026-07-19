	.text
	.global main
	.p2align 2
main:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #352
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	stp x28, x27, [sp, #64]
	stp d8, d9, [sp, #80]
	stp d10, d11, [sp, #96]
	bl getint
	mov w28, w0
	str	wzr, [x29, #-8]
main_label_while_cond_18:
	ldr w10, [x29, #-8]
	cmp w10, w28
	b.lt main_label_while_cond_21.preheader
	str	wzr, [x29, #-24]
main_label_while_cond_24:
	ldr w10, [x29, #-24]
	cmp w10, w28
	b.lt main_label_while_cond_27.preheader
main_label_while_end_26:
	movz w0, #65
	bl _sysy_starttime
	adrp x10, A
	add x10, x10, :lo12:A
	adrp x27, B
	adrp x26, C
	str x10, [x29, #-152]
	add x27, x27, :lo12:B
	add x26, x26, :lo12:C
	str	wzr, [x29, #-40]
main_label_while_cond_30:
	ldr w10, [x29, #-40]
	cmp w10, #5
	b.lt .Lmain_edge_0
	movz w21, #0
	movz w20, #0
	b main_label_while_cond_33
.Lmain_edge_0:
	str	wzr, [x29, #-72]
	b main_label_82
main_label_while_cond_33:
	cmp w20, w28
	b.lt main_label_while_cond_36.preheader
main_label_while_end_35:
	movz w0, #84
	bl _sysy_stoptime
	mov w0, w21
	bl putint
	movz w0, #10
	bl putch
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_cond_21.preheader:
	ldr w10, [x29, #-8]
	adrp x20, A
	add x20, x20, :lo12:A
	movz w19, #0
	sxtw x11, w10
	add x20, x20, x11, lsl #12
main_label_while_cond_21:
	cmp w19, w28
	b.lt main_label_while_body_22
main_label_while_end_23:
	ldr w11, [x29, #-8]
	add w10, w11, #1
	str w10, [x29, #-136]
	str w10, [x29, #-8]
	b main_label_while_cond_18
main_label_while_body_22:
	bl getint
	str	w0, [x20], #4
	add w19, w19, #1
	b main_label_while_cond_21
main_label_while_cond_27.preheader:
	ldr w10, [x29, #-24]
	adrp x20, B
	add x20, x20, :lo12:B
	movz w19, #0
	sxtw x11, w10
	add x20, x20, x11, lsl #12
main_label_while_cond_27:
	cmp w19, w28
	b.lt main_label_while_body_28
main_label_while_end_29:
	ldr w11, [x29, #-24]
	add w10, w11, #1
	str w10, [x29, #-168]
	str w10, [x29, #-24]
	b main_label_while_cond_24
main_label_while_body_28:
	bl getint
	str	w0, [x20], #4
	add w19, w19, #1
	b main_label_while_cond_27
main_label_while_cond_36.preheader:
	adrp x7, B
	add x7, x7, :lo12:B
	sxtw x10, w20
	sub w19, w28, #3
	movi	v10.4s, #0
	mov v10.s[0], w21
	add x7, x7, x10, lsl #12
	sub w6, w28, #7
	mov w5, wzr
main_label_379:
	cmp w5, w6
	b.lt main_label_384
main_label_147:
	cmp w5, w19
	b.lt main_label_151
main_label_157:
	addv s0, v10.4s
	adrp x3, B
	add x3, x3, :lo12:B
	sxtw x10, w20
	add x3, x3, x10, lsl #12
	fmov w4, s0
	add x3, x3, w5, sxtw #2
main_label_357:
	cmp w5, w19
	b.lt main_label_362
main_label_while_cond_36.loopexit:
	movi	v9.4s, #0
	mov v9.s[0], w4
main_label_422:
	cmp w5, w19
	b.lt main_label_427
main_label_433:
	addv s0, v9.4s
	fmov w21, s0
main_label_while_cond_36:
	cmp w5, w28
	b.lt main_label_while_body_37
main_label_while_end_38:
	add w20, w20, #1
	b main_label_while_cond_33
main_label_while_body_37:
	ldr	w9, [x3], #4
	add w5, w5, #1
	add w21, w21, w9
	b main_label_while_cond_36
main_label_40.preheader:
	ldr w10, [x29, #-56]
	mov x9, x27
	sub w4, w28, #3
	movi v8.4s, #0
	sxtw x11, w10
	add x9, x9, x11, lsl #12
	sub w3, w28, #7
	movz w2, #0
main_label_335:
	cmp w2, w3
	b.lt main_label_339
main_label_218:
	cmp w2, w4
	b.lt main_label_221
main_label_225.loopexit:
	ldr w10, [x29, #-56]
	mov x1, x27
	sxtw x11, w10
	add x1, x1, x11, lsl #12
main_label_225:
	cmp w2, w28
	b.lt main_label_43
main_label_228:
	ldr w11, [x29, #-56]
	add w10, w11, #1
	str w10, [x29, #-216]
	str w10, [x29, #-56]
main_label_36:
	ldr w10, [x29, #-56]
	cmp w10, w28
	b.lt main_label_40.preheader
	movz w23, #0
main_label_145:
	ldr x22, [x29, #-152]
	sxtw x10, w23
	movz w21, #0
	add x22, x22, x10, lsl #12
main_label_49:
	cmp w21, w28
	b.lt main_label_56
main_label_53.backedge:
	add w23, w23, #1
	cmp w23, w28
	b.ge main_label_79
	b main_label_145
main_label_79:
	ldr w11, [x29, #-40]
	add w10, w11, #1
	str w10, [x29, #-184]
	str w10, [x29, #-40]
	b main_label_while_cond_30
main_label_43:
	mov x0, x1
	add x0, x0, w2, sxtw #2
	str wzr, [x0]
	add w2, w2, #1
	b main_label_225
main_label_56:
	ldr w20, [x22]
	cmp w20, #1
	b.eq main_label_77
main_label_63.preheader:
	mov x7, x27
	sxtw x10, w23
	add x7, x7, x10, lsl #12
	mov x6, x26
	sxtw x10, w21
	sub w19, w28, #3
	dup v11.4s, w20
	add x6, x6, x10, lsl #12
	sub w5, w28, #7
	movz w4, #0
main_label_287:
	cmp w4, w5
	b.lt main_label_292
main_label_174:
	cmp w4, w19
	b.lt main_label_177
main_label_187.loopexit:
	mov x3, x27
	sxtw x10, w23
	add x3, x3, x10, lsl #12
	mov x2, x26
	sxtw x10, w21
	add x2, x2, x10, lsl #12
main_label_187:
	cmp w4, w28
	b.lt main_label_66
main_label_77:
	add w21, w21, #1
	add x22, x22, #4
	b main_label_49
main_label_66:
	mov x1, x3
	add x1, x1, w4, sxtw #2
	ldr w9, [x1]
	mov w16, w20
	mov w8, w9
	mov x9, x2
	add x9, x9, w4, sxtw #2
	ldr w9, [x9]
	add w4, w4, #1
	madd	w9, w8, w16, w9
	str w9, [x1]
	b main_label_187
main_label_86.preheader:
	ldr w10, [x29, #-72]
	mov x9, x26
	sub w4, w28, #3
	movi v8.4s, #0
	sxtw x11, w10
	add x9, x9, x11, lsl #12
	sub w3, w28, #7
	movz w2, #0
main_label_346:
	cmp w2, w3
	b.lt main_label_350
main_label_234:
	cmp w2, w4
	b.lt main_label_237
main_label_241.loopexit:
	ldr w10, [x29, #-72]
	mov x1, x26
	sxtw x11, w10
	add x1, x1, x11, lsl #12
main_label_241:
	cmp w2, w28
	b.lt main_label_89
main_label_244:
	ldr w11, [x29, #-72]
	add w10, w11, #1
	str w10, [x29, #-232]
	str w10, [x29, #-72]
main_label_82:
	ldr w10, [x29, #-72]
	cmp w10, w28
	b.lt main_label_86.preheader
	str	wzr, [x29, #-88]
main_label_146:
	ldr w10, [x29, #-88]
	ldr x25, [x29, #-152]
	movz w24, #0
	sxtw x11, w10
	add x25, x25, x11, lsl #12
main_label_95:
	cmp w24, w28
	b.lt main_label_102
main_label_99.backedge:
	ldr w11, [x29, #-88]
	add w10, w11, #1
	cmp	w10, w28
	str w10, [x29, #-200]
	b.lt .Lmain_edge_1
	str	wzr, [x29, #-56]
	b main_label_36
.Lmain_edge_1:
	ldr w10, [x29, #-200]
	str w10, [x29, #-88]
	b main_label_146
main_label_89:
	mov x0, x1
	add x0, x0, w2, sxtw #2
	str wzr, [x0]
	add w2, w2, #1
	b main_label_241
main_label_102:
	ldr w20, [x25]
	cmp w20, #1
	b.eq main_label_123
main_label_109.preheader:
	ldr w10, [x29, #-88]
	mov x7, x26
	mov x6, x27
	sub w19, w28, #3
	sxtw x11, w10
	sxtw x10, w24
	dup v11.4s, w20
	add x7, x7, x11, lsl #12
	add x6, x6, x10, lsl #12
	sub w5, w28, #7
	movz w4, #0
main_label_311:
	cmp w4, w5
	b.lt main_label_316
main_label_196:
	cmp w4, w19
	b.lt main_label_199
main_label_209.preheader:
	ldr w10, [x29, #-88]
	mov x3, x26
	mov x2, x27
	sxtw x11, w10
	sxtw x10, w24
	add x3, x3, x11, lsl #12
	add x2, x2, x10, lsl #12
main_label_209:
	cmp w4, w28
	b.lt main_label_112
main_label_123:
	add w24, w24, #1
	add x25, x25, #4
	b main_label_95
main_label_112:
	mov x1, x3
	add x1, x1, w4, sxtw #2
	ldr w9, [x1]
	mov w16, w20
	mov w8, w9
	mov x9, x2
	add x9, x9, w4, sxtw #2
	ldr w9, [x9]
	add w4, w4, #1
	madd	w9, w8, w16, w9
	str w9, [x1]
	b main_label_209
main_label_151:
	ldr	q8, [x7], #16
	add w5, w5, #4
	add v10.4s, v10.4s, v8.4s
	b main_label_147
main_label_177:
	ldr	q8, [x6], #16
	mov x0, x7
	ldr	q0, [x7], #16
	add w4, w4, #4
	mla v8.4s, v0.4s, v11.4s
	str	q8, [x0]
	b main_label_174
main_label_199:
	ldr	q8, [x6], #16
	mov x0, x7
	ldr	q0, [x7], #16
	add w4, w4, #4
	mla v8.4s, v0.4s, v11.4s
	str	q8, [x0]
	b main_label_196
main_label_221:
	add w2, w2, #4
	str	q8, [x9], #16
	b main_label_218
main_label_237:
	add w2, w2, #4
	str	q8, [x9], #16
	b main_label_234
main_label_292:
	mov x9, x7
	mov x0, x6
	ldr	q0, [x9]
	ldr	q10, [x0]
	add	x0, x7, #16
	add	x1, x6, #16
	mov x3, x0
	mov x2, x1
	ldr	q8, [x2]
	mla v10.4s, v0.4s, v11.4s
	ldr	q0, [x3]
	add w4, w4, #8
	str	q10, [x9]
	add	x7, x0, #16
	mla v8.4s, v0.4s, v11.4s
	add	x6, x1, #16
	str	q8, [x3]
	b main_label_287
main_label_316:
	mov x9, x7
	mov x0, x6
	ldr	q0, [x9]
	ldr	q10, [x0]
	add	x0, x7, #16
	add	x1, x6, #16
	mov x3, x0
	mov x2, x1
	ldr	q8, [x2]
	mla v10.4s, v0.4s, v11.4s
	ldr	q0, [x3]
	add w4, w4, #8
	str	q10, [x9]
	add	x7, x0, #16
	mla v8.4s, v0.4s, v11.4s
	add	x6, x1, #16
	str	q8, [x3]
	b main_label_311
main_label_339:
	add	x0, x9, #16
	mov x1, x9
	mov x9, x0
	add w2, w2, #8
	str	q8, [x1]
	str	q8, [x9]
	add	x9, x0, #16
	b main_label_335
main_label_350:
	add	x0, x9, #16
	mov x1, x9
	mov x9, x0
	add w2, w2, #8
	str	q8, [x1]
	str	q8, [x9]
	add	x9, x0, #16
	b main_label_346
main_label_362:
	ldr w9, [x3]
	add w5, w5, #4
	add w0, w4, w9
	add	x9, x3, #4
	ldr	w1, [x9], #4
	add w1, w0, w1
	ldr	w0, [x9], #4
	add w1, w1, w0
	ldr w0, [x9]
	add	x3, x9, #4
	add w4, w1, w0
	b main_label_357
main_label_384:
	mov x9, x7
	ldr	q8, [x9]
	add	x0, x7, #16
	mov x9, x0
	add w5, w5, #8
	add v9.4s, v10.4s, v8.4s
	ldr	q8, [x9]
	add	x7, x0, #16
	add v10.4s, v9.4s, v8.4s
	b main_label_379
main_label_427:
	ldr	q8, [x3], #16
	add w5, w5, #4
	add v9.4s, v9.4s, v8.4s
	b main_label_422
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	ldp d8, d9, [sp, #80]
	ldp d10, d11, [sp, #96]
	add sp, sp, #352
	ldp x29, x30, [sp], #16
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

