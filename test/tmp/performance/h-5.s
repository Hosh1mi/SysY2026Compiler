	.arch armv8-a
	.text
	.p2align 2
	.global kernel_ludcmp
	.type kernel_ludcmp, %function
kernel_ludcmp:
	sub sp, sp, #1, lsl #12
	sub sp, sp, #1760
	stp x19, x20, [sp]
	mov w11, w0
	stp x21, x22, [sp, #16]
	mov x12, x1
	stp x23, x24, [sp, #32]
	add x14, sp, #256
	stp x25, x26, [sp, #48]
	movz w13, #0
	stp x27, x28, [sp, #64]
	stp x2, x3, [sp, #152]
	str x4, [sp, #168]
.Lkernel_ludcmp_bb1:
	cmp w13, w11
	b.ge .Lkernel_ludcmp_bb2
.Lkernel_ludcmp_bb23:
	movz w16, #5600
	smaddl x17, w13, w16, x12
	movn x8, #3
	smaddl x15, w13, w16, x12
	add x3, x12, x8
	smaddl x9, w13, w16, x12
	add x0, x17, #4
	add x8, x17, #8
	add x7, x17, #12
	add x6, x17, #20
	add x17, x15, #36
	str x17, [sp, #200]
	smaddl x10, w13, w16, x12
	str x9, [sp, #176]
	mov x17, x0
	ldr x0, [sp, #176]
	add x19, x9, #16
	add x20, x9, #32
	add x2, x12, #12
	add x1, x12, #28
	add x5, x15, #24
	add x4, x15, #28
	stp x1, x2, [sp, #208]
	add x15, x15, #40
	add x9, x10, #44
	stp x9, x15, [sp, #184]
	movz w1, #0
	str x3, [sp, #224]
.Lkernel_ludcmp_bb24:
	add w9, w1, #11
	str w9, [sp, #232]
	cmp w9, w13
	b.ge .Lkernel_ludcmp_bb25
.Lkernel_ludcmp_bb57:
	ldr q19, [x0]
	ldr q22, [x19]
	ldr q21, [x20]
	ldp x2, x21, [sp, #208]
	ldr x22, [sp, #224]
	ldr x3, [sp, #176]
	movz w15, #0
	movz x9, #5600
.Lkernel_ludcmp_bb58:
	cmp w15, w1
	b.ge .Lkernel_ludcmp_bb60
.Lkernel_ludcmp_bb59:
	ldr w10, [x3], #4
	dup v20.4s, w10
	ldr q18, [x22]
	ldr q16, [x21]
	ldr q17, [x2]
	mls v19.4s, v20.4s, v18.4s
	mls v22.4s, v20.4s, v16.4s
	mls v21.4s, v20.4s, v17.4s
	add w15, w15, #1
	add x22, x22, x9
	add x21, x21, x9
	add x2, x2, x9
	b .Lkernel_ludcmp_bb58
.Lkernel_ludcmp_bb2:
	ldr x15, [sp, #168]
	ldr x17, [sp, #152]
	mov x16, x15
	movz w8, #0
.Lkernel_ludcmp_bb3:
	cmp w8, w11
	b.ge .Lkernel_ludcmp_bb13
.Lkernel_ludcmp_bb4:
	ldr w9, [x17]
	movi v16.4s, #0
	mov v16.s[0], w9
	movz w9, #5600
	smaddl x9, w8, w9, x12
	mov x6, x15
	mov x14, x9
	movz w13, #0
	orr w7, wzr, #0x7ffffff8
.Lkernel_ludcmp_bb5:
	cmp w13, w7
	cset w10, le
	add w9, w13, #7
	cmp w9, w8
	cset w9, lt
	and w9, w10, w9
	cbz w9, .Lkernel_ludcmp_bb7
.Lkernel_ludcmp_bb6:
	ldp q20, q18, [x14]
	ldp q19, q17, [x6]
	mls v16.4s, v20.4s, v19.4s
	mls v16.4s, v18.4s, v17.4s
	add x14, x14, #16
	add x9, x6, #16
	add w13, w13, #8
	add x6, x9, #16
	add x14, x14, #16
	b .Lkernel_ludcmp_bb5
.Lkernel_ludcmp_bb7:
	movz w9, #5600
	smaddl x9, w8, w9, x12
	addv s16, v16.4s
	add x3, x9, w13, sxtw #2
	ldr x9, [sp, #168]
	fmov w4, s16
	add x6, x9, w13, sxtw #2
	sub w2, w8, #1
	mov w5, w13
	orr w7, wzr, #0x80000001
.Lkernel_ludcmp_bb8:
	cmp w5, w2
	cset w10, lt
	cmp w8, w7
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lkernel_ludcmp_bb61
.Lkernel_ludcmp_bb12:
	ldp w14, w10, [x3]
	ldp w13, w9, [x6]
	msub w13, w14, w13, w4
	msub w4, w10, w9, w13
	add x10, x3, #4
	add x9, x6, #4
	add w5, w5, #2
	add x3, x10, #4
	add x6, x9, #4
	b .Lkernel_ludcmp_bb8
.Lkernel_ludcmp_bb9:
	cmp w13, w8
	b.ge .Lkernel_ludcmp_bb11
.Lkernel_ludcmp_bb10:
	ldr w10, [x7], #4
	ldr w9, [x6], #4
	msub w14, w10, w9, w14
	add w13, w13, #1
	b .Lkernel_ludcmp_bb9
.Lkernel_ludcmp_bb11:
	str w14, [x16], #4
	add w8, w8, #1
	add x17, x17, #4
	b .Lkernel_ludcmp_bb3
.Lkernel_ludcmp_bb13:
	sub w7, w11, #1
	cmp w11, #1
	b.lt .Lkernel_ludcmp_bb22
.Lkernel_ludcmp_bb62:
	movz w10, #5600
	orr w8, wzr, #0x80000001
.Lkernel_ludcmp_bb14:
	ldr x9, [sp, #168]
	ldr w2, [x9, w7, sxtw #2]
	add w15, w7, #1
	cmp w15, w11
	b.ge .Lkernel_ludcmp_bb21
.Lkernel_ludcmp_bb15:
	smaddl x9, w7, w10, x12
	add x16, x9, w15, sxtw #2
	ldr x9, [sp, #160]
	sub w6, w11, w15
	cmp w6, #1
	cset w14, gt
	cmp w6, w8
	cset w13, ge
	add x17, x9, w15, sxtw #2
	sub w3, w6, #1
	and w9, w13, w14
	cbz w9, .Lkernel_ludcmp_bb66
.Lkernel_ludcmp_bb64:
	mov x4, x17
	mov x5, x16
	movz w17, #0
.Lkernel_ludcmp_bb16:
	ldp w15, w13, [x5]
	ldp w14, w9, [x4]
	msub w14, w15, w14, w2
	msub w2, w13, w9, w14
	add x13, x5, #4
	add x9, x4, #4
	add w14, w17, #2
	add x5, x13, #4
	add x4, x9, #4
	cmp w14, w3
	b.ge .Lkernel_ludcmp_bb17
.Lkernel_ludcmp_bb65:
	mov w17, w14
	b .Lkernel_ludcmp_bb16
.Lkernel_ludcmp_bb17:
	cmp w14, w6
	b.ge .Lkernel_ludcmp_bb21
.Lkernel_ludcmp_bb67:
	mov x17, x4
	mov x16, x5
	mov w9, w14
	mov w15, w2
.Lkernel_ludcmp_bb18:
	mov w14, w9
.Lkernel_ludcmp_bb19:
	ldr w13, [x16], #4
	ldr w9, [x17], #4
	msub w15, w13, w9, w15
	add w14, w14, #1
	cmp w14, w6
	b.lt .Lkernel_ludcmp_bb19
.Lkernel_ludcmp_bb70:
	mov w2, w15
.Lkernel_ludcmp_bb21:
	smaddl x9, w7, w10, x12
	ldr w9, [x9, w7, sxtw #2]
	sdiv w13, w2, w9
	ldr x9, [sp, #160]
	str w13, [x9, w7, sxtw #2]
	sub w9, w7, #1
	cmp w7, #1
	b.lt .Lkernel_ludcmp_bb22
.Lkernel_ludcmp_bb63:
	mov w7, w9
	b .Lkernel_ludcmp_bb14
.Lkernel_ludcmp_bb22:
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #1, lsl #12
	add sp, sp, #1760
	ret
.Lkernel_ludcmp_bb25:
	movz w17, #5600
	smaddl x5, w13, w17, x12
.Lkernel_ludcmp_bb26:
	cmp w1, w13
	b.ge .Lkernel_ludcmp_bb27
.Lkernel_ludcmp_bb48:
	add x6, x5, w1, sxtw #2
	ldr w9, [x6]
	movi v16.4s, #0
	mov v16.s[0], w9
	ldr x0, [sp, #176]
	sub w4, w1, #1
	add x7, x12, w4, sxtw #2
	movz w3, #0
	orr w2, wzr, #0x7ffffffc
	movz x16, #5600
	movz x15, #11200
	movz x10, #16800
	movz x9, #22400
.Lkernel_ludcmp_bb49:
	cmp w3, w2
	cset w19, le
	add w8, w3, #3
	cmp w8, w1
	cset w8, lt
	and w8, w19, w8
	cbz w8, .Lkernel_ludcmp_bb51
.Lkernel_ludcmp_bb50:
	ldr w8, [x7]
	movi v17.4s, #0
	mov v17.s[0], w8
	add x8, x7, x16
	ldr w8, [x8]
	mov v17.s[1], w8
	add x8, x7, x15
	ldr w8, [x8]
	mov v17.s[2], w8
	add x8, x7, x10
	ldr w8, [x8]
	mov v17.s[3], w8
	ldr q18, [x0], #16
	mls v16.4s, v18.4s, v17.4s
	add w3, w3, #4
	add x7, x7, x9
	b .Lkernel_ludcmp_bb49
.Lkernel_ludcmp_bb27:
	mov x16, x14
	cmp w11, #15
	sub w17, w11, #8
	b.le .Lkernel_ludcmp_bb73
.Lkernel_ludcmp_bb72:
	ldr x10, [sp, #176]
	movz w15, #0
.Lkernel_ludcmp_bb28:
	cmp w15, w17
	b.gt .Lkernel_ludcmp_bb74
.Lkernel_ludcmp_bb29:
	ldp q17, q16, [x10]
	stp q17, q16, [x16]
	add w15, w15, #8
	add x10, x10, #32
	add x16, x16, #32
	b .Lkernel_ludcmp_bb28
.Lkernel_ludcmp_bb30:
	movz w9, #5600
	smaddl x9, w13, w9, x12
	add x16, x9, w7, sxtw #2
	sub w17, w11, #3
	orr w8, wzr, #0x80000003
.Lkernel_ludcmp_bb31:
	cmp w7, w17
	cset w10, lt
	cmp w11, w8
	cset w9, ge
	and w9, w9, w10
	cbnz w9, .Lkernel_ludcmp_bb47
.Lkernel_ludcmp_bb32:
	cmp w7, w11
	b.ge .Lkernel_ludcmp_bb76
.Lkernel_ludcmp_bb46:
	ldr w9, [x16], #4
	add w10, w7, #1
	str w9, [x14, w7, sxtw #2]
	mov w7, w10
	b .Lkernel_ludcmp_bb32
.Lkernel_ludcmp_bb33:
	cmp w3, w13
	b.ge .Lkernel_ludcmp_bb34
.Lkernel_ludcmp_bb40:
	ldr w9, [x4]
	dup v20.4s, w9
	cmp w13, w5
	smaddl x9, w3, w10, x12
	cset w16, le
	add w15, w13, #15
	cmp w15, w11
	cset w15, lt
	add x7, x9, w13, sxtw #2
	add x6, x14, w13, sxtw #2
	and w9, w16, w15
	sub w8, w11, #9
	cbz w9, .Lkernel_ludcmp_bb83
.Lkernel_ludcmp_bb82:
	mov x9, x7
	mov x15, x6
	mov w16, w13
.Lkernel_ludcmp_bb41:
	cmp w16, w8
	b.gt .Lkernel_ludcmp_bb84
.Lkernel_ludcmp_bb42:
	ldp q17, q16, [x15]
	ldp q19, q18, [x9]
	mls v17.4s, v20.4s, v19.4s
	mls v16.4s, v20.4s, v18.4s
	stp q17, q16, [x15]
	add w16, w16, #8
	add x15, x15, #32
	add x9, x9, #32
	b .Lkernel_ludcmp_bb41
.Lkernel_ludcmp_bb34:
	movz w9, #5600
	smaddl x10, w13, w9, x12
	cmp w13, w17
	cset w15, lt
	orr w9, wzr, #0x80000003
	cmp w11, w9
	cset w9, ge
	add x8, x10, w13, sxtw #2
	and w9, w9, w15
	cbz w9, .Lkernel_ludcmp_bb79
.Lkernel_ludcmp_bb77:
	mov w16, w13
.Lkernel_ludcmp_bb35:
	ldr w10, [x14, w16, sxtw #2]
	add w9, w16, #1
	str w10, [x8]
	ldr w10, [x14, w9, sxtw #2]
	add w9, w16, #2
	str w10, [x8, #4]
	ldr w10, [x14, w9, sxtw #2]
	add w9, w16, #3
	str w10, [x8, #8]
	ldr w9, [x14, w9, sxtw #2]
	add x10, x8, #4
	add x10, x10, #4
	str w9, [x8, #12]
	add x15, x10, #4
	add w16, w16, #4
	add x8, x15, #4
	cmp w16, w17
	b.lt .Lkernel_ludcmp_bb35
.Lkernel_ludcmp_bb36:
	cmp w16, w11
	b.ge .Lkernel_ludcmp_bb39
.Lkernel_ludcmp_bb80:
	mov x15, x8
	mov w10, w16
.Lkernel_ludcmp_bb38:
	ldr w9, [x14, w10, sxtw #2]
	add w10, w10, #1
	str w9, [x15], #4
	cmp w10, w11
	b.lt .Lkernel_ludcmp_bb38
.Lkernel_ludcmp_bb39:
	add w13, w13, #1
	b .Lkernel_ludcmp_bb1
.Lkernel_ludcmp_bb43:
	movz w9, #5600
	smaddl x9, w3, w9, x12
	add x6, x9, w7, sxtw #2
.Lkernel_ludcmp_bb44:
	add x8, x14, w7, sxtw #2
	ldr w16, [x8]
	ldr w15, [x4]
	ldr w9, [x6], #4
	msub w9, w15, w9, w16
	add w7, w7, #1
	str w9, [x8]
	cmp w7, w11
	b.lt .Lkernel_ludcmp_bb44
.Lkernel_ludcmp_bb45:
	add w3, w3, #1
	add x4, x4, #4
	b .Lkernel_ludcmp_bb33
.Lkernel_ludcmp_bb47:
	ldr w9, [x16]
	str w9, [x14, w7, sxtw #2]
	ldr w9, [x16, #4]
	add w10, w7, #1
	str w9, [x14, w10, sxtw #2]
	ldr w9, [x16, #8]
	add w10, w7, #2
	str w9, [x14, w10, sxtw #2]
	ldr w15, [x16, #12]
	add x9, x16, #4
	add x9, x9, #4
	add x9, x9, #4
	add w16, w7, #3
	str w15, [x14, w16, sxtw #2]
	add x9, x9, #4
	add w7, w7, #4
	mov x16, x9
	b .Lkernel_ludcmp_bb31
.Lkernel_ludcmp_bb51:
	addv s16, v16.4s
	movz w9, #5600
	smaddl x9, w3, w9, x12
	movn w15, #0
	add w15, w1, w15
	fmov w0, s16
	add x19, x5, w3, sxtw #2
	add x7, x9, w15, sxtw #2
	mov w2, w3
	orr w3, wzr, #0x80000001
	movz x9, #5600
.Lkernel_ludcmp_bb52:
	cmp w2, w4
	cset w15, lt
	cmp w1, w3
	cset w10, ge
	and w10, w10, w15
	cbz w10, .Lkernel_ludcmp_bb86
.Lkernel_ludcmp_bb56:
	ldr w16, [x7]
	ldp w8, w15, [x19]
	add x7, x7, x9
	ldr w10, [x7]
	msub w16, w8, w16, w0
	msub w0, w15, w10, w16
	add x10, x19, #4
	add w2, w2, #2
	add x19, x10, #4
	add x7, x7, x9
	b .Lkernel_ludcmp_bb52
.Lkernel_ludcmp_bb53:
	cmp w16, w1
	b.ge .Lkernel_ludcmp_bb55
.Lkernel_ludcmp_bb54:
	ldr w15, [x7], #4
	ldr w10, [x4]
	msub w8, w15, w10, w8
	add w16, w16, #1
	add x4, x4, x9
	b .Lkernel_ludcmp_bb53
.Lkernel_ludcmp_bb55:
	smaddl x9, w1, w17, x12
	ldr w9, [x9, w1, sxtw #2]
	sdiv w9, w8, w9
	add w1, w1, #1
	str w9, [x6]
	b .Lkernel_ludcmp_bb26
.Lkernel_ludcmp_bb60:
	smaddl x9, w1, w16, x12
	umov w15, v19.s[0]
	add x10, x9, w1, sxtw #2
	ldr w9, [x10]
	sdiv w2, w15, w9
	umov w21, v19.s[1]
	add w3, w1, #1
	str w2, [x0]
	smaddl x9, w3, w16, x12
	ldr w10, [x10]
	add x15, x9, w3, sxtw #2
	ldr w9, [x15]
	msub w10, w2, w10, w21
	sdiv w22, w10, w9
	smaddl x9, w1, w16, x12
	str w22, [x17]
	ldr w2, [x9, w3, sxtw #2]
	umov w23, v19.s[2]
	ldr w21, [x0]
	add w9, w1, #2
	smaddl x10, w9, w16, x12
	ldr w15, [x15]
	msub w2, w21, w2, w23
	add x24, x10, w9, sxtw #2
	ldr w10, [x24]
	msub w15, w22, w15, w2
	sdiv w22, w15, w10
	smaddl x10, w1, w16, x12
	str w22, [x8]
	ldr w15, [x10, w9, sxtw #2]
	umov w26, v19.s[3]
	ldr w23, [x0]
	smaddl x10, w3, w16, x12
	ldr w21, [x17]
	ldr w2, [x10, w9, sxtw #2]
	msub w23, w23, w15, w26
	add w25, w1, #3
	smaddl x10, w25, w16, x12
	msub w21, w21, w2, w23
	ldr w15, [x24]
	add x2, x10, w25, sxtw #2
	ldr w10, [x2]
	msub w15, w22, w15, w21
	sdiv w23, w15, w10
	smaddl x10, w1, w16, x12
	str w23, [x7]
	ldr w15, [x10, w25, sxtw #2]
	smaddl x10, w3, w16, x12
	umov w22, v22.s[0]
	ldr w21, [x0]
	ldr w24, [x10, w25, sxtw #2]
	ldr w26, [x17]
	smaddl x10, w9, w16, x12
	msub w27, w21, w15, w22
	ldr w22, [x8]
	ldr w21, [x10, w25, sxtw #2]
	msub w24, w26, w24, w27
	add w15, w1, #4
	smaddl x10, w15, w16, x12
	msub w22, w22, w21, w24
	ldr w2, [x2]
	add x21, x10, w15, sxtw #2
	ldr w10, [x21]
	msub w2, w23, w2, w22
	sdiv w2, w2, w10
	smaddl x10, w1, w16, x12
	str w2, [x19]
	ldr w27, [x10, w15, sxtw #2]
	smaddl x10, w3, w16, x12
	umov w26, v22.s[1]
	ldr w24, [x0]
	ldr w22, [x10, w15, sxtw #2]
	smaddl x10, w9, w16, x12
	msub w27, w24, w27, w26
	ldr w23, [x17]
	ldr w24, [x10, w15, sxtw #2]
	smaddl x10, w25, w16, x12
	msub w27, w23, w22, w27
	ldr w26, [x8]
	ldr w22, [x10, w15, sxtw #2]
	ldr w23, [x7]
	add w28, w1, #5
	msub w24, w26, w24, w27
	smaddl x10, w28, w16, x12
	ldr w21, [x21]
	msub w22, w23, w22, w24
	add x26, x10, w28, sxtw #2
	ldr w10, [x26]
	msub w2, w2, w21, w22
	sdiv w23, w2, w10
	smaddl x10, w1, w16, x12
	str w23, [x6]
	ldr w21, [x10, w28, sxtw #2]
	smaddl x10, w3, w16, x12
	umov w27, v22.s[2]
	ldr w24, [x0]
	ldr w2, [x10, w28, sxtw #2]
	smaddl x10, w9, w16, x12
	msub w27, w24, w21, w27
	ldr w22, [x17]
	ldr w21, [x10, w28, sxtw #2]
	ldr w24, [x8]
	smaddl x10, w25, w16, x12
	msub w27, w22, w2, w27
	ldr w22, [x7]
	ldr w10, [x10, w28, sxtw #2]
	smaddl x2, w15, w16, x12
	msub w27, w24, w21, w27
	ldr w21, [x19]
	ldr w2, [x2, w28, sxtw #2]
	msub w27, w22, w10, w27
	add w24, w1, #6
	smaddl x10, w24, w16, x12
	msub w21, w21, w2, w27
	add x2, x10, w24, sxtw #2
	ldr w22, [x26]
	ldr w10, [x2]
	str x2, [sp, #120]
	msub w2, w23, w22, w21
	sdiv w26, w2, w10
	smaddl x10, w1, w16, x12
	str w26, [x5]
	ldr w2, [x10, w24, sxtw #2]
	smaddl x10, w3, w16, x12
	umov w27, v22.s[3]
	ldr w21, [x0]
	ldr w22, [x10, w24, sxtw #2]
	smaddl x10, w9, w16, x12
	msub w27, w21, w2, w27
	ldr w23, [x17]
	ldr w2, [x10, w24, sxtw #2]
	smaddl x10, w25, w16, x12
	msub w27, w23, w22, w27
	ldr w21, [x8]
	ldr w22, [x10, w24, sxtw #2]
	ldr w23, [x7]
	smaddl x10, w15, w16, x12
	msub w27, w21, w2, w27
	ldr w2, [x19]
	ldr w10, [x10, w24, sxtw #2]
	msub w27, w23, w22, w27
	smaddl x21, w28, w16, x12
	msub w27, w2, w10, w27
	ldr w22, [x6]
	ldr w21, [x21, w24, sxtw #2]
	ldr x10, [sp, #120]
	add w23, w1, #7
	smaddl x2, w23, w16, x12
	msub w22, w22, w21, w27
	ldr w10, [x10]
	add x21, x2, w23, sxtw #2
	ldr w2, [x21]
	msub w10, w26, w10, w22
	sdiv w2, w10, w2
	str x21, [sp, #144]
	smaddl x10, w1, w16, x12
	str w2, [x4]
	str w2, [sp, #92]
	ldr w22, [x10, w23, sxtw #2]
	smaddl x10, w3, w16, x12
	umov w27, v21.s[0]
	ldr w26, [x0]
	ldr w2, [x10, w23, sxtw #2]
	smaddl x10, w9, w16, x12
	msub w27, w26, w22, w27
	ldr w21, [x17]
	ldr w22, [x10, w23, sxtw #2]
	smaddl x10, w25, w16, x12
	msub w27, w21, w2, w27
	ldr w26, [x8]
	ldr w2, [x10, w23, sxtw #2]
	smaddl x10, w15, w16, x12
	msub w27, w26, w22, w27
	ldr w21, [x7]
	ldr w22, [x10, w23, sxtw #2]
	ldr w26, [x19]
	smaddl x10, w28, w16, x12
	msub w27, w21, w2, w27
	ldr w2, [x6]
	ldr w10, [x10, w23, sxtw #2]
	msub w26, w26, w22, w27
	smaddl x21, w24, w16, x12
	msub w27, w2, w10, w26
	ldr w22, [x5]
	ldr w21, [x21, w23, sxtw #2]
	ldr x10, [sp, #144]
	str w23, [sp, #136]
	ldr w26, [x10]
	add w23, w1, #8
	smaddl x2, w23, w16, x12
	add x10, x2, w23, sxtw #2
	ldr w2, [x10]
	str x10, [sp, #104]
	ldr w10, [sp, #92]
	msub w21, w22, w21, w27
	msub w10, w10, w26, w21
	sdiv w2, w10, w2
	smaddl x10, w1, w16, x12
	str w2, [x20]
	str w2, [sp, #112]
	ldr w2, [x10, w23, sxtw #2]
	smaddl x10, w3, w16, x12
	umov w27, v21.s[1]
	ldr w26, [x0]
	ldr w21, [x10, w23, sxtw #2]
	smaddl x10, w9, w16, x12
	msub w27, w26, w2, w27
	ldr w22, [x17]
	ldr w2, [x10, w23, sxtw #2]
	smaddl x10, w25, w16, x12
	msub w27, w22, w21, w27
	ldr w21, [x10, w23, sxtw #2]
	ldr w26, [x8]
	ldr w22, [x7]
	smaddl x10, w15, w16, x12
	str w15, [sp, #132]
	ldr w15, [x10, w23, sxtw #2]
	msub w26, w26, w2, w27
	smaddl x10, w28, w16, x12
	msub w21, w22, w21, w26
	ldr w2, [x19]
	ldr w22, [x10, w23, sxtw #2]
	msub w27, w2, w15, w21
	ldr w26, [x6]
	smaddl x10, w24, w16, x12
	ldr w15, [sp, #136]
	ldr w2, [x5]
	ldr w10, [x10, w23, sxtw #2]
	msub w26, w26, w22, w27
	smaddl x21, w15, w16, x12
	msub w27, w2, w10, w26
	ldr w22, [x4]
	ldr w21, [x21, w23, sxtw #2]
	ldr x10, [sp, #104]
	str w23, [sp, #96]
	ldr w26, [x10]
	add w23, w1, #9
	smaddl x2, w23, w16, x12
	msub w21, w22, w21, w27
	ldr w10, [sp, #112]
	add x27, x2, w23, sxtw #2
	ldr w2, [x27]
	msub w10, w10, w26, w21
	sdiv w26, w10, w2
	umov w10, v21.s[2]
	str w10, [sp, #236]
	ldr x10, [sp, #200]
	smaddl x2, w1, w16, x12
	str w26, [x10]
	ldr w22, [x0]
	ldr w21, [x2, w23, sxtw #2]
	smaddl x10, w3, w16, x12
	str w3, [sp, #80]
	ldr w3, [x10, w23, sxtw #2]
	ldr w2, [x17]
	smaddl x10, w9, w16, x12
	str w9, [sp, #88]
	ldr w9, [sp, #236]
	msub w22, w22, w21, w9
	ldr w9, [x10, w23, sxtw #2]
	ldr w21, [x8]
	smaddl x10, w25, w16, x12
	str w25, [sp, #128]
	ldr w25, [sp, #132]
	msub w2, w2, w3, w22
	smaddl x22, w25, w16, x12
	msub w21, w21, w9, w2
	ldr w3, [x7]
	ldr w10, [x10, w23, sxtw #2]
	ldr w2, [x19]
	ldr w22, [x22, w23, sxtw #2]
	smaddl x9, w28, w16, x12
	msub w21, w3, w10, w21
	str w28, [sp, #84]
	ldr w3, [x9, w23, sxtw #2]
	ldr w10, [x6]
	smaddl x9, w24, w16, x12
	msub w21, w2, w22, w21
	ldr w2, [x9, w23, sxtw #2]
	msub w28, w10, w3, w21
	ldr w22, [x5]
	ldr w3, [sp, #96]
	smaddl x9, w15, w16, x12
	ldr w21, [x4]
	ldr w9, [x9, w23, sxtw #2]
	smaddl x10, w3, w16, x12
	msub w28, w22, w2, w28
	ldr w2, [x20]
	ldr w10, [x10, w23, sxtw #2]
	msub w21, w21, w9, w28
	add w22, w1, #10
	smaddl x9, w22, w16, x12
	msub w10, w2, w10, w21
	ldr w27, [x27]
	add x21, x9, w22, sxtw #2
	ldr w9, [x21]
	msub w10, w26, w27, w10
	sdiv w2, w10, w9
	ldr x9, [sp, #192]
	smaddl x10, w1, w16, x12
	str w2, [x9]
	ldr w26, [x10, w22, sxtw #2]
	ldr w9, [sp, #80]
	smaddl x10, w9, w16, x12
	ldr w27, [x0], #48
	ldr w9, [x17]
	str w9, [sp, #240]
	ldr w9, [x10, w22, sxtw #2]
	str w9, [sp, #248]
	ldr w9, [sp, #88]
	umov w28, v21.s[3]
	smaddl x10, w9, w16, x12
	msub w9, w27, w26, w28
	str w9, [sp, #244]
	ldr w9, [x8], #48
	str w9, [sp, #252]
	ldr w9, [sp, #128]
	ldr w27, [x10, w22, sxtw #2]
	smaddl x28, w9, w16, x12
	ldp w26, w10, [sp, #240]
	ldr w9, [sp, #248]
	msub w26, w26, w9, w10
	ldr w9, [sp, #252]
	msub w27, w9, w27, w26
	ldr w10, [x7], #48
	ldr w28, [x28, w22, sxtw #2]
	ldr w9, [sp, #84]
	smaddl x25, w25, w16, x12
	smaddl x9, w9, w16, x12
	msub w28, w10, w28, w27
	ldr w26, [x19], #48
	ldr w25, [x25, w22, sxtw #2]
	ldr w10, [x9, w22, sxtw #2]
	smaddl x9, w24, w16, x12
	msub w25, w26, w25, w28
	ldr w27, [x6], #48
	ldr w26, [x9, w22, sxtw #2]
	smaddl x9, w15, w16, x12
	msub w10, w27, w10, w25
	ldr w27, [x9, w22, sxtw #2]
	smaddl x9, w3, w16, x12
	ldr w24, [x5], #48
	ldr w15, [x9, w22, sxtw #2]
	msub w3, w24, w26, w10
	ldr x9, [sp, #200]
	ldr w25, [x4], #48
	smaddl x10, w23, w16, x12
	ldr w23, [x9]
	msub w25, w25, w27, w3
	ldr w24, [x20], #48
	ldr w3, [x10, w22, sxtw #2]
	ldr w9, [sp, #232]
	msub w22, w24, w15, w25
	smaddl x10, w9, w16, x12
	ldr w15, [x21]
	ldr w9, [x10, w9, sxtw #2]
	msub w3, w23, w3, w22
	msub w10, w2, w15, w3
	sdiv w10, w10, w9
	ldr x9, [sp, #184]
	str w10, [x9]
	ldr x9, [sp, #224]
	add x15, x9, #48
	ldr x9, [sp, #216]
	add x3, x9, #48
	ldr x9, [sp, #208]
	add x2, x9, #48
	ldr x9, [sp, #200]
	add x21, x17, #48
	add x17, x9, #48
	ldr x9, [sp, #192]
	add x10, x9, #48
	ldr x9, [sp, #184]
	add x9, x9, #48
	str x17, [sp, #200]
	add w1, w1, #12
	stp x9, x10, [sp, #184]
	mov x17, x21
	stp x2, x3, [sp, #208]
	str x15, [sp, #224]
	b .Lkernel_ludcmp_bb24
.Lkernel_ludcmp_bb61:
	mov x7, x3
	mov w14, w4
	mov w13, w5
	b .Lkernel_ludcmp_bb9
.Lkernel_ludcmp_bb66:
	movz w9, #0
	mov w15, w2
	b .Lkernel_ludcmp_bb18
.Lkernel_ludcmp_bb73:
	movz w7, #0
	b .Lkernel_ludcmp_bb30
.Lkernel_ludcmp_bb74:
	mov w7, w15
	b .Lkernel_ludcmp_bb30
.Lkernel_ludcmp_bb76:
	ldr x4, [sp, #176]
	movz w3, #0
	movz w10, #5600
	orr w5, wzr, #0x7ffffff0
	b .Lkernel_ludcmp_bb33
.Lkernel_ludcmp_bb79:
	mov x15, x8
	mov w10, w13
	b .Lkernel_ludcmp_bb38
.Lkernel_ludcmp_bb83:
	mov w7, w13
	b .Lkernel_ludcmp_bb43
.Lkernel_ludcmp_bb84:
	mov w7, w16
	b .Lkernel_ludcmp_bb43
.Lkernel_ludcmp_bb86:
	mov x4, x7
	mov x7, x19
	mov w8, w0
	mov w16, w2
	movz x9, #5600
	b .Lkernel_ludcmp_bb53
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
	movz w0, #70
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
