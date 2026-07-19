	.text
	.global kernel_ludcmp
	.p2align 2
kernel_ludcmp:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #400
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	stp x28, x27, [sp, #64]
	stp d8, d9, [sp, #80]
	stp d10, d11, [sp, #96]
	movz w28, #0
kernel_ludcmp_label_while_cond_1:
	cmp w28, w0
	b.lt kernel_ludcmp_label_while_cond_4.preheader
kernel_ludcmp_label_while_cond_16.loopexit:
	mov x27, x4
	mov x10, x27
	str	x2, [x29, #-184]
	mov w25, wzr
	mov x26, x10
	str	x2, [x29, #-24]
kernel_ludcmp_label_while_cond_16:
	cmp w25, w0
	b.lt kernel_ludcmp_label_while_body_17
kernel_ludcmp_label_while_end_18:
	cmp w0, #1
	sub w24, w0, #1
	b.lt	.Lkernel_ludcmp_epilogue
	mov w23, w24
kernel_ludcmp_label_while_body_23:
	mov x5, x4
	add x5, x5, w23, sxtw #2
	ldr w22, [x5]
	add w21, w23, #1
	cmp w21, w0
	b.lt kernel_ludcmp_label_while_body_26.preheader
kernel_ludcmp_label_while_end_27:
	mov x5, x1
	sxtw x10, w23
	movz x11, #5600
	madd	x5, x10, x11, x5
	mov x6, x3
	add x6, x6, w23, sxtw #2
	add x5, x5, w23, sxtw #2
	ldr w5, [x5]
	cmp w23, #1
	sdiv w5, w22, w5
	str w5, [x6]
	b.lt	.Lkernel_ludcmp_epilogue
	sub	w23, w23, #1
	b kernel_ludcmp_label_while_body_23
kernel_ludcmp_label_while_cond_4.preheader:
	mov x27, x1
	sxtw x10, w28
	movz x11, #5600
	madd	x27, x10, x11, x27
	mov x10, x1
	sxtw x11, w28
	movz x12, #5600
	madd	x10, x11, x12, x10
	mov	x26, x27
	str	wzr, [x29, #-8]
	str x10, [x29, #-136]
kernel_ludcmp_label_while_cond_4:
	ldr w10, [x29, #-8]
	cmp w10, w28
	b.lt kernel_ludcmp_label_while_body_5
	mov w25, w28
kernel_ludcmp_label_while_body_11:
	ldr x10, [x29, #-136]
	sub w24, w28, #3
	movi	v11.4s, #0
	mov w22, wzr
	add x10, x10, w25, sxtw #2
	str x10, [x29, #-168]
	ldr w5, [x10]
	mov x10, x27
	mov x23, x10
	mov v11.s[0], w5
kernel_ludcmp_label_143:
	cmp w22, w24
	b.lt kernel_ludcmp_label_147
kernel_ludcmp_label_169:
	addv s0, v11.4s
	ldr x20, [x29, #-136]
	fmov w21, s0
	add x20, x20, w22, sxtw #2
kernel_ludcmp_label_289:
	cmp w22, w24
	b.lt kernel_ludcmp_label_294
kernel_ludcmp_label_while_cond_13.preheader:
	movi	v10.4s, #0
	mov v10.s[0], w21
kernel_ludcmp_label_501:
	cmp w22, w24
	b.lt kernel_ludcmp_label_506
kernel_ludcmp_label_528:
	addv s0, v10.4s
	fmov w7, s0
kernel_ludcmp_label_while_cond_13:
	cmp w22, w28
	b.lt kernel_ludcmp_label_while_body_14
kernel_ludcmp_label_while_end_15:
	ldr x10, [x29, #-168]
	add w25, w25, #1
	cmp w25, w0
	str w7, [x10]
	b.ge kernel_ludcmp_label_while_end_12
	b kernel_ludcmp_label_while_body_11
kernel_ludcmp_label_while_end_12:
	add w28, w28, #1
	b kernel_ludcmp_label_while_cond_1
kernel_ludcmp_label_while_body_5:
	ldr w10, [x29, #-8]
	ldr w5, [x26]
	movi	v11.4s, #0
	mov w22, wzr
	sub w25, w10, #1
	ldr w10, [x29, #-8]
	mov v11.s[0], w5
	sub w24, w10, #3
	mov x10, x27
	mov x23, x10
kernel_ludcmp_label_102:
	cmp w22, w24
	b.lt kernel_ludcmp_label_106
kernel_ludcmp_label_128:
	addv s0, v11.4s
	ldr x20, [x29, #-136]
	fmov w21, s0
	add x20, x20, w22, sxtw #2
kernel_ludcmp_label_255:
	cmp w22, w24
	b.lt kernel_ludcmp_label_260
kernel_ludcmp_label_while_cond_7.preheader:
	movi	v10.4s, #0
	mov v10.s[0], w21
kernel_ludcmp_label_428:
	cmp w22, w24
	b.lt kernel_ludcmp_label_433
kernel_ludcmp_label_455:
	addv s0, v10.4s
	fmov w7, s0
kernel_ludcmp_label_while_cond_7:
	ldr w10, [x29, #-8]
	cmp w22, w10
	b.lt kernel_ludcmp_label_while_body_8
kernel_ludcmp_label_while_end_9:
	ldr w10, [x29, #-8]
	mov x5, x1
	movz x12, #5600
	sxtw x11, w10
	ldr w10, [x29, #-8]
	madd	x5, x11, x12, x5
	add x5, x5, w10, sxtw #2
	ldr w5, [x5]
	sdiv w5, w7, w5
	str	w5, [x26], #4
	ldr w11, [x29, #-8]
	add w10, w11, #1
	str w10, [x29, #-152]
	str w10, [x29, #-8]
	b kernel_ludcmp_label_while_cond_4
kernel_ludcmp_label_while_body_8:
	mov x5, x1
	sxtw x10, w22
	movz x11, #5600
	madd	x5, x10, x11, x5
	ldr	w6, [x20], #4
	add w22, w22, #1
	add x5, x5, w25, sxtw #2
	ldr w5, [x5]
	msub	w7, w6, w5, w7
	b kernel_ludcmp_label_while_cond_7
kernel_ludcmp_label_while_body_14:
	mov x5, x1
	sxtw x10, w22
	movz x11, #5600
	madd	x5, x10, x11, x5
	ldr	w6, [x20], #4
	add w22, w22, #1
	add x5, x5, w25, sxtw #2
	ldr w5, [x5]
	msub	w7, w6, w5, w7
	b kernel_ludcmp_label_while_cond_13
kernel_ludcmp_label_while_body_17:
	ldr x10, [x29, #-24]
	mov x23, x1
	movz x11, #5600
	sub w24, w25, #3
	ldr w5, [x10]
	sxtw x10, w25
	madd	x23, x10, x11, x23
	sub w10, w25, #7
	str w10, [x29, #-200]
	mov x10, x27
	movi	v11.4s, #0
	mov v11.s[0], w5
	mov w21, wzr
	mov x22, x10
kernel_ludcmp_label_368:
	ldr w10, [x29, #-200]
	cmp w21, w10
	b.lt kernel_ludcmp_label_374
kernel_ludcmp_label_184:
	cmp w21, w24
	b.lt kernel_ludcmp_label_188
kernel_ludcmp_label_198:
	addv s0, v11.4s
	mov x19, x1
	sxtw x10, w25
	movz x11, #5600
	madd	x19, x10, x11, x19
	mov x9, x4
	sub w10, w25, #1
	fmov w20, s0
	add x19, x19, w21, sxtw #2
	add x9, x9, w21, sxtw #2
	str w10, [x29, #-232]
kernel_ludcmp_label_323:
	ldr w10, [x29, #-232]
	cmp w21, w10
	b.lt kernel_ludcmp_label_329
kernel_ludcmp_label_while_cond_19.loopexit:
	movi	v10.4s, #0
	mov v10.s[0], w20
kernel_ludcmp_label_470:
	cmp w21, w24
	b.lt kernel_ludcmp_label_476
kernel_ludcmp_label_486:
	addv s0, v10.4s
	fmov w7, s0
kernel_ludcmp_label_while_cond_19:
	cmp w21, w25
	b.lt kernel_ludcmp_label_while_body_20
kernel_ludcmp_label_while_end_21:
	str	w7, [x26], #4
	ldr x10, [x29, #-24]
	add w25, w25, #1
	add x10, x10, #4
	str x10, [x29, #-216]
	str x10, [x29, #-24]
	b kernel_ludcmp_label_while_cond_16
kernel_ludcmp_label_while_body_20:
	ldr	w6, [x19], #4
	ldr	w5, [x9], #4
	add w21, w21, #1
	msub	w7, w6, w5, w7
	b kernel_ludcmp_label_while_cond_19
kernel_ludcmp_label_while_body_26.preheader:
	mov x20, x1
	sxtw x10, w23
	movz x11, #5600
	madd	x20, x10, x11, x20
	mov x19, x3
	cmp w21, w24
	add x20, x20, w21, sxtw #2
	add x19, x19, w21, sxtw #2
	b.lt	kernel_ludcmp_label_346
	b	kernel_ludcmp_label_while_body_26
kernel_ludcmp_label_while_body_26.preheader.1:
kernel_ludcmp_label_while_body_26:
	ldr	w6, [x20], #4
	ldr	w5, [x19], #4
	add w21, w21, #1
	cmp w21, w0
	msub	w22, w6, w5, w22
	b.ge	kernel_ludcmp_label_while_end_27
	b kernel_ludcmp_label_while_body_26
kernel_ludcmp_label_106:
	mov x6, x1
	sxtw x10, w22
	movz x11, #5600
	madd	x6, x10, x11, x6
	mov x5, x23
	ldr	q9, [x5]
	add x6, x6, w25, sxtw #2
	add w5, w22, #1
	ld1 {v8.s}[0], [x6]
	mov x6, x1
	sxtw x10, w5
	movz x11, #5600
	madd	x6, x10, x11, x6
	add w5, w22, #2
	sxtw x10, w5
	add x6, x6, w25, sxtw #2
	ld1 {v8.s}[1], [x6]
	mov x6, x1
	movz x11, #5600
	madd	x6, x10, x11, x6
	add w5, w22, #3
	mov x10, x1
	add x6, x6, w25, sxtw #2
	sxtw x11, w5
	movz x12, #5600
	ld1 {v8.s}[2], [x6]
	madd	x10, x11, x12, x10
	add w22, w22, #4
	add x23, x23, #16
	add	x5, x10, w25, sxtw #2
	ld1 {v8.s}[3], [x5]
	mls v11.4s, v9.4s, v8.4s
	b kernel_ludcmp_label_102
kernel_ludcmp_label_147:
	mov x6, x1
	sxtw x10, w22
	movz x11, #5600
	madd	x6, x10, x11, x6
	mov x5, x23
	ldr	q9, [x5]
	add x6, x6, w25, sxtw #2
	add w5, w22, #1
	ld1 {v8.s}[0], [x6]
	mov x6, x1
	sxtw x10, w5
	movz x11, #5600
	madd	x6, x10, x11, x6
	add w5, w22, #2
	sxtw x10, w5
	add x6, x6, w25, sxtw #2
	ld1 {v8.s}[1], [x6]
	mov x6, x1
	movz x11, #5600
	madd	x6, x10, x11, x6
	add w5, w22, #3
	mov x10, x1
	add x6, x6, w25, sxtw #2
	sxtw x11, w5
	movz x12, #5600
	ld1 {v8.s}[2], [x6]
	madd	x10, x11, x12, x10
	add w22, w22, #4
	add x23, x23, #16
	add	x5, x10, w25, sxtw #2
	ld1 {v8.s}[3], [x5]
	mls v11.4s, v9.4s, v8.4s
	b kernel_ludcmp_label_143
kernel_ludcmp_label_188:
	ldr	q9, [x23], #16
	ldr	q8, [x22], #16
	add w21, w21, #4
	mls v11.4s, v9.4s, v8.4s
	b kernel_ludcmp_label_184
kernel_ludcmp_label_260:
	mov x5, x1
	sxtw x10, w22
	movz x11, #5600
	madd	x5, x10, x11, x5
	ldr w6, [x20]
	mov x10, x1
	add x5, x5, w25, sxtw #2
	ldr w5, [x5]
	movz x12, #5600
	msub	w7, w6, w5, w21
	add w5, w22, #1
	sxtw x11, w5
	madd	x10, x11, x12, x10
	add	x6, x20, #4
	ldr w9, [x6]
	add	x5, x10, w25, sxtw #2
	ldr w5, [x5]
	mov x10, x1
	movz x12, #5600
	add x6, x6, #4
	msub	w9, w9, w5, w7
	add w5, w22, #2
	sxtw x11, w5
	madd	x10, x11, x12, x10
	ldr w7, [x6]
	movz x12, #5600
	add	x5, x10, w25, sxtw #2
	ldr w5, [x5]
	mov x10, x1
	msub	w9, w7, w5, w9
	add w5, w22, #3
	sxtw x11, w5
	madd	x10, x11, x12, x10
	add	x7, x6, #4
	ldr w6, [x7]
	add	x5, x10, w25, sxtw #2
	ldr w5, [x5]
	add w22, w22, #4
	add	x20, x7, #4
	msub	w21, w6, w5, w9
	b kernel_ludcmp_label_255
kernel_ludcmp_label_294:
	mov x5, x1
	sxtw x10, w22
	movz x11, #5600
	madd	x5, x10, x11, x5
	ldr w6, [x20]
	mov x10, x1
	add x5, x5, w25, sxtw #2
	ldr w5, [x5]
	movz x12, #5600
	msub	w7, w6, w5, w21
	add w5, w22, #1
	sxtw x11, w5
	madd	x10, x11, x12, x10
	add	x6, x20, #4
	ldr w9, [x6]
	add	x5, x10, w25, sxtw #2
	ldr w5, [x5]
	mov x10, x1
	movz x12, #5600
	add x6, x6, #4
	msub	w9, w9, w5, w7
	add w5, w22, #2
	sxtw x11, w5
	madd	x10, x11, x12, x10
	ldr w7, [x6]
	movz x12, #5600
	add	x5, x10, w25, sxtw #2
	ldr w5, [x5]
	mov x10, x1
	msub	w9, w7, w5, w9
	add w5, w22, #3
	sxtw x11, w5
	madd	x10, x11, x12, x10
	add	x7, x6, #4
	ldr w6, [x7]
	add	x5, x10, w25, sxtw #2
	ldr w5, [x5]
	add w22, w22, #4
	add	x20, x7, #4
	msub	w21, w6, w5, w9
	b kernel_ludcmp_label_289
kernel_ludcmp_label_329:
	ldr w6, [x19]
	ldr w5, [x9]
	add w21, w21, #2
	msub	w5, w6, w5, w20
	add	x6, x19, #4
	add	x19, x9, #4
	ldr w9, [x6]
	ldr w7, [x19]
	msub	w20, w9, w7, w5
	add	x5, x6, #4
	add	x9, x19, #4
	mov x19, x5
	b kernel_ludcmp_label_323
kernel_ludcmp_label_366:
	cmp w21, w0
	b.ge	kernel_ludcmp_label_while_end_27
	b kernel_ludcmp_label_while_body_26.preheader.1
kernel_ludcmp_label_374:
	mov x5, x23
	ldr	q9, [x5]
	mov x5, x22
	ldr	q8, [x5]
	add	x6, x23, #16
	mov v10.16b, v11.16b
	add	x5, x22, #16
	mov x7, x6
	mls v10.4s, v9.4s, v8.4s
	ldr	q9, [x7]
	mov x7, x5
	ldr	q8, [x7]
	mov v11.16b, v10.16b
	add w21, w21, #8
	add	x23, x6, #16
	mls v11.4s, v9.4s, v8.4s
	add	x22, x5, #16
	b kernel_ludcmp_label_368
kernel_ludcmp_label_433:
	mov x6, x1
	sxtw x10, w22
	movz x11, #5600
	madd	x6, x10, x11, x6
	mov x5, x20
	ldr	q9, [x5]
	add x6, x6, w25, sxtw #2
	add w5, w22, #1
	ld1 {v8.s}[0], [x6]
	mov x6, x1
	sxtw x10, w5
	movz x11, #5600
	madd	x6, x10, x11, x6
	add w5, w22, #2
	sxtw x10, w5
	add x6, x6, w25, sxtw #2
	ld1 {v8.s}[1], [x6]
	mov x6, x1
	movz x11, #5600
	madd	x6, x10, x11, x6
	add w5, w22, #3
	mov x10, x1
	add x6, x6, w25, sxtw #2
	sxtw x11, w5
	movz x12, #5600
	ld1 {v8.s}[2], [x6]
	madd	x10, x11, x12, x10
	add w22, w22, #4
	add x20, x20, #16
	add	x5, x10, w25, sxtw #2
	ld1 {v8.s}[3], [x5]
	mls v10.4s, v9.4s, v8.4s
	b kernel_ludcmp_label_428
kernel_ludcmp_label_476:
	ldr	q9, [x19], #16
	ldr	q8, [x9], #16
	add w21, w21, #4
	mls v10.4s, v9.4s, v8.4s
	b kernel_ludcmp_label_470
kernel_ludcmp_label_506:
	mov x6, x1
	sxtw x10, w22
	movz x11, #5600
	madd	x6, x10, x11, x6
	mov x5, x20
	ldr	q9, [x5]
	add x6, x6, w25, sxtw #2
	add w5, w22, #1
	ld1 {v8.s}[0], [x6]
	mov x6, x1
	sxtw x10, w5
	movz x11, #5600
	madd	x6, x10, x11, x6
	add w5, w22, #2
	sxtw x10, w5
	add x6, x6, w25, sxtw #2
	ld1 {v8.s}[1], [x6]
	mov x6, x1
	movz x11, #5600
	madd	x6, x10, x11, x6
	add w5, w22, #3
	mov x10, x1
	add x6, x6, w25, sxtw #2
	sxtw x11, w5
	movz x12, #5600
	ld1 {v8.s}[2], [x6]
	madd	x10, x11, x12, x10
	add w22, w22, #4
	add x20, x20, #16
	add	x5, x10, w25, sxtw #2
	ld1 {v8.s}[3], [x5]
	mls v10.4s, v9.4s, v8.4s
	b kernel_ludcmp_label_501
kernel_ludcmp_label_346:
	ldr w6, [x20]
	ldr w5, [x19]
	add	x9, x20, #4
	add	x7, x19, #4
	add w21, w21, #2
	msub w10, w6, w5, w22
	cmp w21, w24
	add	x20, x9, #4
	str w10, [x29, #-248]
	ldr w6, [x9]
	ldr w5, [x7]
	mov	w12, w10
	add	x19, x7, #4
	msub	w22, w6, w5, w12
	b.ge kernel_ludcmp_label_366
	b kernel_ludcmp_label_346
.Lkernel_ludcmp_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	ldp d8, d9, [sp, #80]
	ldp d10, d11, [sp, #96]
	add sp, sp, #400
	ldp x29, x30, [sp], #16
	ret
	.global main
	.p2align 2
main:
	sub sp, sp, #48
	adrp x10, n
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	str x23, [sp, #32]
	str x30, [sp, #40]
	ldr w23, [x10, :lo12:n]
	adrp x22, A
	add x22, x22, :lo12:A
	mov x0, x22
	bl getarray
	adrp x21, b
	add x21, x21, :lo12:b
	mov x0, x21
	bl getarray
	adrp x20, x
	add x20, x20, :lo12:x
	mov x0, x20
	bl getarray
	adrp x19, y
	add x19, x19, :lo12:y
	mov x0, x19
	bl getarray
	movz w0, #68
	bl _sysy_starttime
	mov w0, w23
	mov x1, x22
	mov x2, x21
	mov x3, x20
	mov x4, x19
	bl kernel_ludcmp
	movz w0, #70
	bl _sysy_stoptime
	mov w0, w23
	mov x1, x20
	bl putarray
	adrp x10, n
	str w23, [x10, :lo12:n]
	mov w0, wzr
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldr x23, [sp, #32]
	ldr x30, [sp, #40]
	add sp, sp, #48
	ret
	.data
	.global n
	.p2align 2
n:
	.word 1400

	.bss
	.global A
	.p2align 4
A:
	.zero 7840000

	.global b
	.p2align 4
b:
	.zero 5600

	.global x
	.p2align 4
x:
	.zero 5600

	.global y
	.p2align 4
y:
	.zero 5600

