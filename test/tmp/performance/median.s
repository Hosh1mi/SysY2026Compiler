	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	stp x21, x22, [sp, #16]
	adrp x9, a
	add x21, x9, :lo12:a
	stp x19, x20, [sp]
	movz w22, #0
	str x23, [sp, #32]
	mov x0, x21
	bl getarray
	mov w19, w0
	movz w0, #59
	bl _sysy_starttime
	add w9, w19, w19, lsr #31
	sub w16, w19, #1
	asr w20, w9, #1
	mov w17, w22
.Lmain_bb1:
	add x22, x21, w17, sxtw #2
	ldr w15, [x22]
	add w14, w16, #1
	mov w13, w17
	movn w23, #0
.Lmain_bb2:
	add w9, w14, w23
	add x11, x21, w9, sxtw #2
	movn x9, #3
	b .Lmain_bb3
.Lmain_bb4:
	ldr w10, [x11]
	sub w14, w14, #1
	cmp w10, w15
	add x11, x11, x9
	b.lt .Lmain_bb5
.Lmain_bb3:
	cmp w13, w14
	b.lt .Lmain_bb4
.Lmain_bb5:
	add w9, w13, #1
	add x10, x21, w9, sxtw #2
	b .Lmain_bb6
.Lmain_bb7:
	ldr w9, [x10], #4
	add w13, w13, #1
	cmp w9, w15
	b.ge .Lmain_bb8
.Lmain_bb6:
	cmp w13, w14
	b.lt .Lmain_bb7
.Lmain_bb8:
	cmp w13, w14
	b.eq .Lmain_bb9
.Lmain_bb14:
	add x12, x21, w13, sxtw #2
	add x10, x21, w14, sxtw #2
	ldr w11, [x12]
	ldr w9, [x10]
	str w9, [x12]
	str w11, [x10]
	b .Lmain_bb2
.Lmain_bb9:
	str w15, [x22]
	add x10, x21, w13, sxtw #2
	ldr w9, [x10]
	str w9, [x22]
	cmp w13, w20
	str w15, [x10]
	b.gt .Lmain_bb21
.Lmain_bb10:
	cmp w13, w20
	b.ge .Lmain_bb13
.Lmain_bb11:
	add w17, w13, #1
	b .Lmain_bb1
.Lmain_bb13:
	movz w0, #61
	bl _sysy_stoptime
	mov w0, w19
	mov x1, x21
	bl putarray
	adrp x9, a
	add x9, x9, :lo12:a
	ldr w9, [x9, w20, sxtw #2]
	cmp w9, #0
	cneg w9, w9, mi
	and w9, w9, #255
	cneg w0, w9, mi
	adrp x9, n
	str w19, [x9, :lo12:n]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb21:
	mov w16, w13
	b .Lmain_bb1
	.size main, .-main
	.data
	.global n
	.p2align 2
n:
	.zero 4
	.bss
	.global a
	.p2align 4
a:
	.zero 40000000
