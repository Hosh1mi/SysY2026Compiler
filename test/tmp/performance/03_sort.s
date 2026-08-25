	.arch armv8-a
	.text
	.p2align 2
	.global radixSort
	.type radixSort, %function
radixSort:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #240
	movz w12, #0
	dup v16.4s, w12
	stp x21, x22, [sp, #16]
	add x22, sp, #48
	stp x19, x20, [sp]
	stp x23, x24, [sp, #32]
	add x20, sp, #112
	stp q16, q16, [x22]
	stp q16, q16, [x22, #32]
	mov w10, w0
	stp q16, q16, [x20]
	add x19, sp, #176
	stp q16, q16, [x20, #32]
	movn w9, #0
	stp q16, q16, [x19]
	mov x21, x1
	stp q16, q16, [x19, #32]
	mov w11, w2
	mov w15, w3
	cmp w10, w9
	b.eq .LradixSort_bb30
.LradixSort_bb1:
	add w9, w11, #1
	cmp w9, w15
	b.ge .LradixSort_bb30
.LradixSort_bb31:
	mov w16, w11
.LradixSort_bb2:
	cmp w16, w15
	b.ge .LradixSort_bb9
.LradixSort_bb3:
	ldr w13, [x21, w16, sxtw #2]
	sub w23, w10, #3
	mov w17, w12
	movz w14, #1
.LradixSort_bb4:
	cmp w17, w23
	cset w9, lt
	and w9, w9, w14
	cbz w9, .LradixSort_bb32
.LradixSort_bb8:
	asr w9, w13, #31
	lsr w9, w9, #16
	add w9, w13, w9
	asr w13, w9, #16
	add w17, w17, #4
	b .LradixSort_bb4
.LradixSort_bb5:
	cmp w14, w10
	b.ge .LradixSort_bb7
.LradixSort_bb6:
	asr w9, w13, #31
	lsr w9, w9, #28
	add w9, w13, w9
	asr w13, w9, #4
	add w14, w14, #1
	b .LradixSort_bb5
.LradixSort_bb7:
	cmp w13, #0
	cneg w9, w13, mi
	and w9, w9, #15
	cneg w9, w9, mi
	add x14, x19, w9, sxtw #2
	ldr w9, [x14]
	add w13, w9, #1
	str w13, [x14]
	add w16, w16, #1
	b .LradixSort_bb2
.LradixSort_bb9:
	str w11, [x22]
	ldr w9, [x19]
	add w9, w11, w9
	str w9, [x20]
	mov w14, w12
.LradixSort_bb10:
	cmp w14, #15
	b.ge .LradixSort_bb33
.LradixSort_bb29:
	ldr w13, [x20, w14, sxtw #2]
	add w14, w14, #1
	str w13, [x22, w14, sxtw #2]
	ldr w9, [x19, w14, sxtw #2]
	add w9, w13, w9
	str w9, [x20, w14, sxtw #2]
	b .LradixSort_bb10
.LradixSort_bb11:
	cmp w14, #16
	b.ge .LradixSort_bb24
.LradixSort_bb12:
	add x15, x22, w14, sxtw #2
.LradixSort_bb13:
	ldr w13, [x15]
	ldr w9, [x20, w14, sxtw #2]
	cmp w13, w9
	b.ge .LradixSort_bb23
.LradixSort_bb14:
	ldr w17, [x21, w13, sxtw #2]
.LradixSort_bb15:
	sub w24, w10, #3
	mov w23, w12
	mov w13, w17
	movz w16, #1
.LradixSort_bb16:
	cmp w23, w24
	cset w9, lt
	and w9, w9, w16
	cbz w9, .LradixSort_bb34
.LradixSort_bb22:
	asr w9, w13, #31
	lsr w9, w9, #16
	add w9, w13, w9
	asr w13, w9, #16
	add w23, w23, #4
	b .LradixSort_bb16
.LradixSort_bb17:
	cmp w16, w10
	b.ge .LradixSort_bb19
.LradixSort_bb18:
	asr w9, w13, #31
	lsr w9, w9, #28
	add w9, w13, w9
	asr w13, w9, #4
	add w16, w16, #1
	b .LradixSort_bb17
.LradixSort_bb19:
	cmp w13, #0
	cneg w9, w13, mi
	and w9, w9, #15
	cneg w13, w9, mi
	cmp w13, w14
	b.eq .LradixSort_bb21
.LradixSort_bb20:
	add x16, x22, w13, sxtw #2
	ldr w9, [x16]
	add x9, x21, w9, sxtw #2
	ldr w13, [x9]
	str w17, [x9]
	ldr w9, [x16]
	add w9, w9, #1
	str w9, [x16]
	mov w17, w13
	b .LradixSort_bb15
.LradixSort_bb21:
	ldr w9, [x15]
	str w17, [x21, w9, sxtw #2]
	ldr w9, [x15]
	add w9, w9, #1
	str w9, [x15]
	b .LradixSort_bb13
.LradixSort_bb23:
	add w14, w14, #1
	b .LradixSort_bb11
.LradixSort_bb24:
	str w11, [x22]
	ldr w9, [x19]
	add w9, w11, w9
	str w9, [x20]
	sub w23, w10, #1
	mov w24, w12
.LradixSort_bb25:
	cmp w24, #16
	b.ge .LradixSort_bb30
.LradixSort_bb26:
	cmp w24, #0
	b.le .LradixSort_bb28
.LradixSort_bb27:
	sub w9, w24, #1
	ldr w10, [x20, w9, sxtw #2]
	str w10, [x22, w24, sxtw #2]
	ldr w9, [x19, w24, sxtw #2]
	add w9, w10, w9
	str w9, [x20, w24, sxtw #2]
.LradixSort_bb28:
	ldr w2, [x22, w24, sxtw #2]
	ldr w3, [x20, w24, sxtw #2]
	mov w0, w23
	mov x1, x21
	bl radixSort
	add w24, w24, #1
	b .LradixSort_bb25
.LradixSort_bb30:
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #240
	ldp xzr, x30, [sp], #16
	ret
.LradixSort_bb32:
	mov w14, w17
	b .LradixSort_bb5
.LradixSort_bb33:
	mov w14, w12
	b .LradixSort_bb11
.LradixSort_bb34:
	mov w16, w23
	b .LradixSort_bb17
	.size radixSort, .-radixSort
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	adrp x9, ans
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	ldr w26, [x9, :lo12:ans]
	adrp x9, a
	add x24, x9, :lo12:a
	movz w25, #0
	mov x0, x24
	bl getarray
	mov w19, w0
	movz w0, #90
	bl _sysy_starttime
	movz w0, #9
	movz w2, #0
	mov x1, x24
	mov w3, w19
	bl radixSort
	sub w22, w19, #3
	orr w21, wzr, #0x80000003
.Lmain_bb1:
	cmp w25, w22
	cset w10, lt
	cmp w19, w21
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb6
.Lmain_bb5:
	ldp w20, w17, [x24]
	add w23, w25, #2
	sdiv w9, w20, w23
	add w16, w25, #3
	ldp w15, w13, [x24, #8]
	sdiv w11, w17, w16
	add w14, w25, #4
	sdiv w10, w15, w14
	msub w20, w9, w23, w20
	add w12, w25, #5
	sdiv w9, w13, w12
	madd w20, w25, w20, w26
	msub w11, w11, w16, w17
	add w20, w20, #3
	add w17, w25, #1
	madd w11, w17, w11, w20
	msub w10, w10, w14, w15
	add w11, w11, #3
	madd w10, w23, w10, w11
	msub w9, w9, w12, w13
	add w10, w10, #3
	madd w9, w16, w9, w10
	add x10, x24, #4
	add x10, x10, #4
	add x11, x10, #4
	add w26, w9, #3
	add x24, x11, #4
	mov w25, w14
	b .Lmain_bb1
.Lmain_bb2:
	cmp w12, w19
	b.ge .Lmain_bb4
.Lmain_bb3:
	ldr w11, [x24], #4
	add w10, w12, #2
	sdiv w9, w11, w10
	msub w9, w9, w10, w11
	madd w9, w12, w9, w13
	add w13, w9, #3
	add w12, w12, #1
	b .Lmain_bb2
.Lmain_bb4:
	movz w9, #0
	cmp w13, #0
	sub w9, w9, w13
	csel w19, w9, w13, lt
	movz w0, #102
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	movz w0, #10
	bl putch
	adrp x9, ans
	str w19, [x9, :lo12:ans]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb6:
	mov w13, w26
	mov w12, w25
	b .Lmain_bb2
	.size main, .-main
	.data
	.global ans
	.p2align 2
ans:
	.zero 4
	.bss
	.global a
	.p2align 4
a:
	.zero 120000040
