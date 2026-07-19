	.text
	.global main
	.p2align 2
main:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #256
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	stp x28, x27, [sp, #64]
	stp d8, d9, [sp, #80]
	str d10, [sp, #96]
	bl getint
	mov w28, w0
	str	wzr, [x29, #-8]
main_label_while_cond_13:
	ldr w10, [x29, #-8]
	cmp w10, w28
	b.lt main_label_while_cond_16.preheader
	str	wzr, [x29, #-24]
main_label_while_cond_19:
	ldr w10, [x29, #-24]
	cmp w10, w28
	b.lt main_label_while_cond_22.preheader
main_label_while_end_21:
	movz w0, #55
	bl _sysy_starttime
	adrp x10, A
	add x10, x10, :lo12:A
	str x10, [x29, #-88]
	adrp x10, B
	add x10, x10, :lo12:B
	str x10, [x29, #-104]
	str	wzr, [x29, #-40]
main_label_while_cond_25:
	ldr w10, [x29, #-40]
	cmp w10, #5
	b.ge main_label_while_end_27
	str	wzr, [x29, #-56]
main_label_while_cond_28:
	ldr w10, [x29, #-56]
	cmp w10, w28
	b.lt main_label_while_cond_31.preheader
	movz w27, #0
main_label_44:
	cmp w27, w28
	b.lt main_label_48.preheader
main_label_81:
	ldr w11, [x29, #-40]
	add w10, w11, #1
	str w10, [x29, #-136]
	str w10, [x29, #-40]
	b main_label_while_cond_25
main_label_while_cond_16.preheader:
	ldr w10, [x29, #-8]
	adrp x20, A
	add x20, x20, :lo12:A
	movz w19, #0
	sxtw x11, w10
	add x20, x20, x11, lsl #12
main_label_while_cond_16:
	cmp w19, w28
	b.lt main_label_while_body_17
main_label_while_end_18:
	ldr w11, [x29, #-8]
	add w10, w11, #1
	str w10, [x29, #-72]
	str w10, [x29, #-8]
	b main_label_while_cond_13
main_label_while_body_17:
	bl getfloat
	str	s0, [x20], #4
	add w19, w19, #1
	b main_label_while_cond_16
main_label_while_cond_22.preheader:
	ldr w10, [x29, #-24]
	adrp x20, C
	add x20, x20, :lo12:C
	movz w19, #0
	sxtw x11, w10
	add x20, x20, x11, lsl #12
main_label_while_cond_22:
	cmp w19, w28
	b.lt main_label_while_body_23
main_label_while_end_24:
	ldr w11, [x29, #-24]
	add w10, w11, #1
	str w10, [x29, #-120]
	str w10, [x29, #-24]
	b main_label_while_cond_19
main_label_while_body_23:
	bl getfloat
	str	s0, [x20], #4
	add w19, w19, #1
	b main_label_while_cond_22
main_label_while_end_27:
	movz w0, #70
	bl _sysy_stoptime
	movz w10, #0
	fmov s1, w10
	movz w3, #0
main_label_while_cond_34:
	cmp w3, w28
	b.lt main_label_while_cond_37.preheader
main_label_while_end_36:
	fmov s0, s1
	bl putfloat
	movz w0, #10
	bl putch
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_cond_31.preheader:
	ldr w10, [x29, #-56]
	adrp x0, B
	add x0, x0, :lo12:B
	adrp x9, C
	sxtw x11, w10
	ldr w10, [x29, #-56]
	add x0, x0, x11, lsl #12
	add x9, x9, :lo12:C
	sub w6, w28, #3
	sxtw x11, w10
	add x9, x9, x11, lsl #12
	sub w5, w28, #7
	movz w4, #0
main_label_220:
	cmp w4, w5
	b.lt main_label_225
main_label_91:
	cmp w4, w6
	b.lt main_label_94
main_label_101:
	cmp w4, w28
	b.lt main_label_while_body_32
main_label_104:
	ldr w11, [x29, #-56]
	add w10, w11, #1
	str w10, [x29, #-152]
	str w10, [x29, #-56]
	b main_label_while_cond_28
main_label_while_body_32:
	ldr w10, [x29, #-56]
	adrp x2, B
	add x2, x2, :lo12:B
	adrp x1, C
	sxtw x11, w10
	ldr w10, [x29, #-56]
	add x2, x2, x11, lsl #12
	add x1, x1, :lo12:C
	add x2, x2, w4, sxtw #2
	sxtw x11, w10
	add x1, x1, x11, lsl #12
	add x1, x1, w4, sxtw #2
	ldr s0, [x1]
	add w4, w4, #1
	str s0, [x2]
	b main_label_101
main_label_while_cond_37.preheader:
	adrp x2, B
	add x2, x2, :lo12:B
	sxtw x10, w3
	add x2, x2, x10, lsl #12
	sub w1, w28, #3
	movz w0, #0
main_label_238:
	cmp w0, w1
	b.lt main_label_243
main_label_while_cond_37:
	cmp w0, w28
	b.lt main_label_while_body_38
main_label_while_end_39:
	add w3, w3, #1
	b main_label_while_cond_34
main_label_while_body_38:
	ldr	s0, [x2], #4
	add w0, w0, #1
	fadd s1, s1, s0
	b main_label_while_cond_37
main_label_48.preheader:
	ldr x9, [x29, #-88]
	ldr x25, [x29, #-104]
	ldr x24, [x29, #-104]
	ldr x23, [x29, #-104]
	sxtw x10, w27
	ldr x22, [x29, #-104]
	add x9, x9, x10, lsl #12
	sxtw x10, w27
	add x9, x9, w27, sxtw #2
	add x25, x25, x10, lsl #12
	sxtw x10, w27
	ldr s3, [x9]
	add x24, x24, x10, lsl #12
	sxtw x10, w27
	add x23, x23, x10, lsl #12
	sxtw x10, w27
	add x22, x22, x10, lsl #12
	sub w26, w28, #3
	add x24, x24, #4
	add x23, x23, #8
	add x22, x22, #12
	mov x21, x25
	movz w20, #0
main_label_106:
	cmp w20, w26
	b.lt main_label_109
main_label_134.loopexit:
	ldr x19, [x29, #-104]
	sxtw x10, w27
	add x19, x19, x10, lsl #12
main_label_134:
	cmp w20, w28
	b.lt main_label_51
main_label_137:
	add w7, w27, #1
	cmp w7, w28
	b.lt .Lmain_edge_0
	mov w27, w7
	b main_label_44
.Lmain_edge_0:
	mov w6, w7
main_label_65.preheader:
	ldr x9, [x29, #-88]
	sxtw x10, w6
	ldr x5, [x29, #-104]
	sub w4, w28, #7
	add x9, x9, x10, lsl #12
	add x9, x9, w27, sxtw #2
	ldr s2, [x9]
	sxtw x10, w6
	add x5, x5, x10, lsl #12
	mov x3, x25
	fmov w10, s2
	mov v8.s[0], w10
	fmov w10, s2
	mov v8.s[1], w10
	fmov w10, s2
	mov v8.s[2], w10
	fmov w10, s2
	mov v10.16b, v8.16b
	mov v10.s[3], w10
	movz w2, #0
main_label_196:
	cmp w2, w4
	b.lt main_label_201
main_label_143:
	cmp w2, w26
	b.lt main_label_146
main_label_156.loopexit:
	ldr x1, [x29, #-104]
	sxtw x10, w6
	add x1, x1, x10, lsl #12
main_label_156:
	cmp w2, w28
	b.lt main_label_68
main_label_159:
	add w6, w6, #1
	cmp w6, w28
	b.lt	main_label_65.preheader
	mov w27, w7
	b main_label_44
main_label_51:
	mov x9, x19
	add x9, x9, w20, sxtw #2
	ldr s0, [x9]
	movz w10, #0
	movk w10, #16256, lsl #16
	fmov s16, w10
	fdiv s0, s0, s3
	add w20, w20, #1
	fadd s0, s0, s16
	str s0, [x9]
	b main_label_134
main_label_68:
	mov x9, x19
	add x9, x9, w2, sxtw #2
	ldr s0, [x9]
	mov x0, x1
	add x0, x0, w2, sxtw #2
	ldr s1, [x0]
	fmul s0, s2, s0
	add w2, w2, #1
	fsub s0, s1, s0
	str s0, [x0]
	b main_label_156
main_label_94:
	mov x1, x9
	ldr	q8, [x9], #16
	add w4, w4, #4
	mov x2, x0
	str	q8, [x0], #16
	b main_label_91
main_label_109:
	ldr s0, [x21]
	movz w10, #0
	movk w10, #16256, lsl #16
	fmov s16, w10
	fdiv s0, s0, s3
	movz w10, #0
	movk w10, #16256, lsl #16
	add w20, w20, #4
	fadd s0, s0, s16
	fmov s16, w10
	movz w10, #0
	movk w10, #16256, lsl #16
	str s0, [x21]
	ldr s0, [x24]
	add x21, x21, #16
	fdiv s0, s0, s3
	fadd s0, s0, s16
	fmov s16, w10
	movz w10, #0
	movk w10, #16256, lsl #16
	str s0, [x24]
	ldr s0, [x23]
	add x24, x24, #16
	fdiv s0, s0, s3
	fadd s0, s0, s16
	fmov s16, w10
	str s0, [x23]
	ldr s0, [x22]
	add x23, x23, #16
	fdiv s0, s0, s3
	fadd s0, s0, s16
	str	s0, [x22], #16
	b main_label_106
main_label_146:
	ldr	q8, [x3], #16
	mov x0, x5
	ldr	q9, [x0]
	add w2, w2, #4
	fmul v8.4s, v10.4s, v8.4s
	add x5, x5, #16
	fsub v8.4s, v9.4s, v8.4s
	str	q8, [x0]
	b main_label_143
main_label_201:
	mov x9, x3
	ldr	q8, [x9]
	mov x0, x5
	ldr	q9, [x0]
	add	x9, x5, #16
	fmul v8.4s, v10.4s, v8.4s
	add	x5, x9, #16
	fsub v8.4s, v9.4s, v8.4s
	str	q8, [x0]
	add	x0, x3, #16
	mov x1, x0
	ldr	q8, [x1]
	add w3, w2, #8
	mov x2, x9
	ldr	q9, [x2]
	fmul v8.4s, v10.4s, v8.4s
	add	x9, x0, #16
	fsub v8.4s, v9.4s, v8.4s
	str	q8, [x2]
	mov w2, w3
	mov x3, x9
	b main_label_196
main_label_225:
	mov x1, x9
	ldr	q9, [x1]
	add	x1, x9, #16
	mov x9, x1
	ldr	q8, [x9]
	add	x2, x0, #16
	mov x3, x0
	mov x0, x2
	add w4, w4, #8
	str	q9, [x3]
	str	q8, [x0]
	add	x0, x2, #16
	add	x9, x1, #16
	b main_label_220
main_label_243:
	ldr s0, [x2]
	add	x9, x2, #4
	add w0, w0, #4
	fadd s1, s1, s0
	ldr	s0, [x9], #4
	fadd s1, s1, s0
	ldr	s0, [x9], #4
	fadd s1, s1, s0
	ldr s0, [x9]
	add	x2, x9, #4
	fadd s1, s1, s0
	b main_label_238
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	ldp d8, d9, [sp, #80]
	ldr d10, [sp, #96]
	add sp, sp, #256
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

