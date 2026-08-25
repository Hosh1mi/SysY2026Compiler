	.arch armv8-a
	.text
	.p2align 2
	.global kernel_ludcmp
	.type kernel_ludcmp, %function
kernel_ludcmp:
	sub sp, sp, #1, lsl #12
	sub sp, sp, #1568
	mov x5, x3
	mov x3, x4
	mov x17, x2
	movz w4, #0
	stp x19, x20, [sp]
	mov w8, w0
	stp x21, x22, [sp, #16]
	mov x7, x1
	stp x23, x24, [sp, #32]
	add x2, sp, #64
	stp x25, x26, [sp, #48]
	mov x6, x3
	mov w16, w4
	movz w13, #5600
	orr w14, wzr, #0x80000003
.Lkernel_ludcmp_bb1:
	cmp w16, w8
	b.ge .Lkernel_ludcmp_bb2
.Lkernel_ludcmp_bb12:
	movz w1, #5600
	smaddl x15, w16, w1, x7
	mov x20, x7
	mov x19, x15
	mov w0, w4
.Lkernel_ludcmp_bb13:
	cmp w0, w16
	b.ge .Lkernel_ludcmp_bb14
.Lkernel_ludcmp_bb43:
	ldr w9, [x19]
	movi v16.4s, #0
	mov v16.s[0], w9
	mov x25, x20
	mov x24, x15
	mov w22, w4
	orr w23, wzr, #0x7ffffffc
	movz x12, #5600
	movz x11, #11200
	movz x10, #16800
	movz x9, #22400
.Lkernel_ludcmp_bb44:
	cmp w22, w23
	cset w26, le
	add w21, w22, #3
	cmp w21, w0
	cset w21, lt
	and w21, w26, w21
	cbz w21, .Lkernel_ludcmp_bb46
.Lkernel_ludcmp_bb45:
	ldr w21, [x25]
	movi v17.4s, #0
	mov v17.s[0], w21
	add x21, x25, x12
	ldr w21, [x21]
	mov v17.s[1], w21
	add x21, x25, x11
	ldr w21, [x21]
	mov v17.s[2], w21
	add x21, x25, x10
	ldr w21, [x21]
	mov v17.s[3], w21
	ldr q18, [x24], #16
	mls v16.4s, v18.4s, v17.4s
	add w22, w22, #4
	add x25, x25, x9
	b .Lkernel_ludcmp_bb44
.Lkernel_ludcmp_bb2:
	sub w17, w8, #1
	cmp w8, #1
	b.lt .Lkernel_ludcmp_bb11
.Lkernel_ludcmp_bb52:
	movz w10, #5600
	orr w16, wzr, #0x80000001
.Lkernel_ludcmp_bb3:
	ldr w19, [x3, w17, sxtw #2]
	add w13, w17, #1
	cmp w13, w8
	b.ge .Lkernel_ludcmp_bb10
.Lkernel_ludcmp_bb4:
	sub w6, w8, w13
	smaddl x9, w17, w10, x7
	cmp w6, #1
	cset w12, gt
	cmp w6, w16
	cset w11, ge
	add x14, x9, w13, sxtw #2
	add x15, x5, w13, sxtw #2
	sub w0, w6, #1
	and w9, w11, w12
	cbz w9, .Lkernel_ludcmp_bb56
.Lkernel_ludcmp_bb54:
	mov x1, x15
	mov x2, x14
	mov w15, w4
.Lkernel_ludcmp_bb5:
	ldp w13, w11, [x2]
	ldp w12, w9, [x1]
	msub w12, w13, w12, w19
	msub w19, w11, w9, w12
	add x11, x2, #4
	add x9, x1, #4
	add w12, w15, #2
	add x2, x11, #4
	add x1, x9, #4
	cmp w12, w0
	b.ge .Lkernel_ludcmp_bb6
.Lkernel_ludcmp_bb55:
	mov w15, w12
	b .Lkernel_ludcmp_bb5
.Lkernel_ludcmp_bb6:
	cmp w12, w6
	b.ge .Lkernel_ludcmp_bb10
.Lkernel_ludcmp_bb57:
	mov x15, x1
	mov x14, x2
	mov w13, w19
.Lkernel_ludcmp_bb8:
	ldr w11, [x14], #4
	ldr w9, [x15], #4
	msub w13, w11, w9, w13
	add w12, w12, #1
	cmp w12, w6
	b.lt .Lkernel_ludcmp_bb8
.Lkernel_ludcmp_bb60:
	mov w19, w13
.Lkernel_ludcmp_bb10:
	smaddl x9, w17, w10, x7
	ldr w9, [x9, w17, sxtw #2]
	sdiv w9, w19, w9
	str w9, [x5, w17, sxtw #2]
	sub w9, w17, #1
	cmp w17, #1
	b.lt .Lkernel_ludcmp_bb11
.Lkernel_ludcmp_bb53:
	mov w17, w9
	b .Lkernel_ludcmp_bb3
.Lkernel_ludcmp_bb11:
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #1, lsl #12
	add sp, sp, #1568
	ret
.Lkernel_ludcmp_bb14:
	mov x9, x2
	cmp w8, #15
	sub w12, w8, #8
	b.le .Lkernel_ludcmp_bb63
.Lkernel_ludcmp_bb62:
	mov x10, x15
	mov w11, w4
.Lkernel_ludcmp_bb15:
	cmp w11, w12
	b.gt .Lkernel_ludcmp_bb64
.Lkernel_ludcmp_bb16:
	ldp q17, q16, [x10]
	stp q17, q16, [x9]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .Lkernel_ludcmp_bb15
.Lkernel_ludcmp_bb17:
	movz w9, #5600
	smaddl x9, w16, w9, x7
	add x12, x9, w19, sxtw #2
	sub w1, w8, #3
	orr w0, wzr, #0x80000003
.Lkernel_ludcmp_bb18:
	cmp w19, w1
	cset w10, lt
	cmp w8, w0
	cset w9, ge
	and w9, w9, w10
	cbnz w9, .Lkernel_ludcmp_bb42
.Lkernel_ludcmp_bb19:
	cmp w19, w8
	b.ge .Lkernel_ludcmp_bb66
.Lkernel_ludcmp_bb41:
	ldr w9, [x12], #4
	add w10, w19, #1
	str w9, [x2, w19, sxtw #2]
	mov w19, w10
	b .Lkernel_ludcmp_bb19
.Lkernel_ludcmp_bb20:
	cmp w19, w16
	b.ge .Lkernel_ludcmp_bb21
.Lkernel_ludcmp_bb35:
	ldr w9, [x0]
	dup v20.4s, w9
	cmp w16, w12
	smaddl x9, w19, w10, x7
	cset w21, le
	add w11, w16, #15
	cmp w11, w8
	cset w20, lt
	add x11, x9, w16, sxtw #2
	and w9, w21, w20
	add x22, x2, w16, sxtw #2
	sub w21, w8, #9
	cbz w9, .Lkernel_ludcmp_bb74
.Lkernel_ludcmp_bb73:
	mov x9, x11
	mov x11, x22
	mov w20, w16
.Lkernel_ludcmp_bb36:
	cmp w20, w21
	b.gt .Lkernel_ludcmp_bb75
.Lkernel_ludcmp_bb37:
	ldp q17, q16, [x11]
	ldp q19, q18, [x9]
	mls v17.4s, v20.4s, v19.4s
	mls v16.4s, v20.4s, v18.4s
	stp q17, q16, [x11]
	add w20, w20, #8
	add x11, x11, #32
	add x9, x9, #32
	b .Lkernel_ludcmp_bb36
.Lkernel_ludcmp_bb21:
	cmp w16, w1
	smaddl x9, w16, w13, x7
	cset w11, lt
	cmp w8, w14
	cset w10, ge
	add x0, x9, w16, sxtw #2
	and w9, w10, w11
	cbz w9, .Lkernel_ludcmp_bb69
.Lkernel_ludcmp_bb67:
	mov w12, w16
.Lkernel_ludcmp_bb22:
	ldr w10, [x2, w12, sxtw #2]
	add w9, w12, #1
	str w10, [x0]
	ldr w10, [x2, w9, sxtw #2]
	add w9, w12, #2
	str w10, [x0, #4]
	ldr w10, [x2, w9, sxtw #2]
	add w9, w12, #3
	str w10, [x0, #8]
	ldr w9, [x2, w9, sxtw #2]
	add x10, x0, #4
	add x10, x10, #4
	str w9, [x0, #12]
	add x11, x10, #4
	add w12, w12, #4
	add x0, x11, #4
	cmp w12, w1
	b.lt .Lkernel_ludcmp_bb22
.Lkernel_ludcmp_bb23:
	cmp w12, w8
	b.ge .Lkernel_ludcmp_bb26
.Lkernel_ludcmp_bb70:
	mov x11, x0
	mov w10, w12
.Lkernel_ludcmp_bb25:
	ldr w9, [x2, w10, sxtw #2]
	add w10, w10, #1
	str w9, [x11], #4
	cmp w10, w8
	b.lt .Lkernel_ludcmp_bb25
.Lkernel_ludcmp_bb26:
	ldr w9, [x17]
	movi v16.4s, #0
	mov v16.s[0], w9
	mov x12, x15
	mov x1, x3
	mov w11, w4
	orr w15, wzr, #0x7ffffff8
.Lkernel_ludcmp_bb27:
	cmp w11, w15
	cset w10, le
	add w9, w11, #7
	cmp w9, w16
	cset w9, lt
	and w9, w10, w9
	cbz w9, .Lkernel_ludcmp_bb29
.Lkernel_ludcmp_bb28:
	ldp q20, q18, [x12]
	ldp q19, q17, [x1]
	mls v16.4s, v20.4s, v19.4s
	mls v16.4s, v18.4s, v17.4s
	add x12, x12, #16
	add x9, x1, #16
	add w11, w11, #8
	add x1, x9, #16
	add x12, x12, #16
	b .Lkernel_ludcmp_bb27
.Lkernel_ludcmp_bb29:
	addv s16, v16.4s
	movz w9, #5600
	smaddl x9, w16, w9, x7
	fmov w21, s16
	add x20, x9, w11, sxtw #2
	add x19, x3, w11, sxtw #2
	sub w1, w16, #1
	mov w0, w11
	orr w15, wzr, #0x80000001
.Lkernel_ludcmp_bb30:
	cmp w0, w1
	cset w10, lt
	cmp w16, w15
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lkernel_ludcmp_bb72
.Lkernel_ludcmp_bb34:
	ldp w12, w10, [x20]
	ldp w11, w9, [x19]
	msub w11, w12, w11, w21
	msub w21, w10, w9, w11
	add x10, x20, #4
	add x9, x19, #4
	add w0, w0, #2
	add x20, x10, #4
	add x19, x9, #4
	b .Lkernel_ludcmp_bb30
.Lkernel_ludcmp_bb31:
	cmp w11, w16
	b.ge .Lkernel_ludcmp_bb33
.Lkernel_ludcmp_bb32:
	ldr w10, [x15], #4
	ldr w9, [x1], #4
	msub w12, w10, w9, w12
	add w11, w11, #1
	b .Lkernel_ludcmp_bb31
.Lkernel_ludcmp_bb33:
	str w12, [x6], #4
	add x17, x17, #4
	add w16, w16, #1
	b .Lkernel_ludcmp_bb1
.Lkernel_ludcmp_bb38:
	movz w9, #5600
	smaddl x9, w19, w9, x7
	add x23, x9, w22, sxtw #2
.Lkernel_ludcmp_bb39:
	add x21, x2, w22, sxtw #2
	ldr w20, [x21]
	ldr w11, [x0]
	ldr w9, [x23], #4
	msub w9, w11, w9, w20
	add w22, w22, #1
	str w9, [x21]
	cmp w22, w8
	b.lt .Lkernel_ludcmp_bb39
.Lkernel_ludcmp_bb40:
	add w19, w19, #1
	add x0, x0, #4
	b .Lkernel_ludcmp_bb20
.Lkernel_ludcmp_bb42:
	ldr w9, [x12]
	str w9, [x2, w19, sxtw #2]
	ldr w9, [x12, #4]
	add w10, w19, #1
	str w9, [x2, w10, sxtw #2]
	ldr w9, [x12, #8]
	add w10, w19, #2
	str w9, [x2, w10, sxtw #2]
	ldr w11, [x12, #12]
	add x9, x12, #4
	add x9, x9, #4
	add x9, x9, #4
	add w12, w19, #3
	str w11, [x2, w12, sxtw #2]
	add x9, x9, #4
	add w19, w19, #4
	mov x12, x9
	b .Lkernel_ludcmp_bb18
.Lkernel_ludcmp_bb46:
	addv s16, v16.4s
	movz w9, #5600
	smaddl x9, w22, w9, x7
	fmov w25, s16
	add x24, x9, w0, sxtw #2
	add x11, x15, w22, sxtw #2
	sub w23, w0, #1
	orr w21, wzr, #0x80000001
	movz x9, #5600
.Lkernel_ludcmp_bb47:
	cmp w22, w23
	cset w12, lt
	cmp w0, w21
	cset w10, ge
	and w10, w10, w12
	cbz w10, .Lkernel_ludcmp_bb77
.Lkernel_ludcmp_bb51:
	ldp w26, w12, [x11]
	ldr w10, [x24]
	msub w25, w26, w10, w25
	add x24, x24, x9
	ldr w10, [x24]
	msub w25, w12, w10, w25
	add x10, x11, #4
	add x11, x10, #4
	add x24, x24, x9
	add w22, w22, #2
	b .Lkernel_ludcmp_bb47
.Lkernel_ludcmp_bb48:
	cmp w12, w0
	b.ge .Lkernel_ludcmp_bb50
.Lkernel_ludcmp_bb49:
	ldr w11, [x23], #4
	ldr w10, [x24]
	msub w21, w11, w10, w21
	add w12, w12, #1
	add x24, x24, x9
	b .Lkernel_ludcmp_bb48
.Lkernel_ludcmp_bb50:
	smaddl x9, w0, w1, x7
	ldr w9, [x9, w0, sxtw #2]
	sdiv w9, w21, w9
	str w9, [x19], #4
	add w0, w0, #1
	add x20, x20, #4
	b .Lkernel_ludcmp_bb13
.Lkernel_ludcmp_bb56:
	mov w12, w4
	mov w13, w19
	b .Lkernel_ludcmp_bb8
.Lkernel_ludcmp_bb63:
	mov w19, w4
	b .Lkernel_ludcmp_bb17
.Lkernel_ludcmp_bb64:
	mov w19, w11
	b .Lkernel_ludcmp_bb17
.Lkernel_ludcmp_bb66:
	mov x0, x15
	mov w19, w4
	movz w10, #5600
	orr w12, wzr, #0x7ffffff0
	b .Lkernel_ludcmp_bb20
.Lkernel_ludcmp_bb69:
	mov x11, x0
	mov w10, w16
	b .Lkernel_ludcmp_bb25
.Lkernel_ludcmp_bb72:
	mov x1, x19
	mov x15, x20
	mov w12, w21
	mov w11, w0
	b .Lkernel_ludcmp_bb31
.Lkernel_ludcmp_bb74:
	mov w22, w16
	b .Lkernel_ludcmp_bb38
.Lkernel_ludcmp_bb75:
	mov w22, w20
	b .Lkernel_ludcmp_bb38
.Lkernel_ludcmp_bb77:
	mov x23, x11
	mov w21, w25
	mov w12, w22
	movz x9, #5600
	b .Lkernel_ludcmp_bb48
	.size kernel_ludcmp, .-kernel_ludcmp
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x19, x20, [sp]
	adrp x9, n
	stp x21, x22, [sp, #16]
	str x23, [sp, #32]
	ldr w19, [x9, :lo12:n]
	adrp x9, A
	add x20, x9, :lo12:A
	mov x0, x20
	bl getarray
	adrp x9, b
	add x22, x9, :lo12:b
	mov x0, x22
	bl getarray
	adrp x9, x
	add x23, x9, :lo12:x
	mov x0, x23
	bl getarray
	adrp x9, y
	add x21, x9, :lo12:y
	mov x0, x21
	bl getarray
	movz w0, #68
	bl _sysy_starttime
	mov w0, w19
	mov x1, x20
	mov x2, x22
	mov x3, x23
	mov x4, x21
	bl kernel_ludcmp
	movz w0, #71
	bl _sysy_stoptime
	mov w0, w19
	mov x1, x23
	bl putarray
	adrp x9, n
	str w19, [x9, :lo12:n]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
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
