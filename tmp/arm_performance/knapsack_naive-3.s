	.text
	.global knapsack_naive
	.p2align 2
knapsack_naive:
	sub sp, sp, #48
	stp x22, x21, [sp, #16]
	mov w22, w0
	adrp x0, weight
	sub w9, w22, #1
	add x0, x0, :lo12:weight
	stp x20, x19, [sp, #0]
	str x23, [sp, #32]
	str x30, [sp, #40]
	mov w23, w1
	add x0, x0, w9, sxtw #2
knapsack_naive_label_tailrec_header:
	cbnz w22, knapsack_naive_label_or_3
	movz w9, #0
knapsack_naive_label_ret:
	mov w0, w9
	b .Lknapsack_naive_epilogue
knapsack_naive_label_if_else_2:
	ldr	w21, [x0], #-4
	sub w22, w22, #1
	cmp w21, w23
	b.le knapsack_naive_label_if_else_5
	b knapsack_naive_label_tailrec_header
knapsack_naive_label_if_else_5:
	mov w0, w22
	mov w1, w23
	bl knapsack_naive
	adrp x9, value
	add x9, x9, :lo12:value
	add x9, x9, w22, sxtw #2
	ldr w19, [x9]
	sub w9, w23, w21
	mov w20, w0
	mov w0, w22
	mov w1, w9
	bl knapsack_naive
	mov w9, w0
	add w9, w19, w9
	cmp w9, w20
	csel w9, w9, w20, gt
	b knapsack_naive_label_ret
knapsack_naive_label_or_3:
	cbnz w23, knapsack_naive_label_if_else_2
	movz w9, #0
	b knapsack_naive_label_ret
.Lknapsack_naive_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldr x23, [sp, #32]
	ldr x30, [sp, #40]
	add sp, sp, #48
	ret
	.global main
	.p2align 2
main:
	sub sp, sp, #32
	stp x20, x19, [sp, #0]
	str x21, [sp, #16]
	str x30, [sp, #24]
	bl getint
	mov w21, w0
	bl getint
	adrp x9, weight
	mov w20, w0
	add	x0, x9, :lo12:weight
	bl getarray
	adrp x9, value
	add	x0, x9, :lo12:value
	bl getarray
	movz w0, #32
	bl _sysy_starttime
	mov w0, w21
	mov w1, w20
	bl knapsack_naive
	mov w19, w0
	movz w0, #34
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	movz w0, #10
	bl putch
	adrp x10, N
	str w21, [x10, :lo12:N]
	adrp x10, W
	str w20, [x10, :lo12:W]
	mov w0, wzr
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldr x21, [sp, #16]
	ldr x30, [sp, #24]
	add sp, sp, #32
	ret
	.data
	.global N
	.p2align 2
N:
	.word 0

	.global W
	.p2align 2
W:
	.word 0

	.bss
	.global weight
	.p2align 4
weight:
	.zero 200

	.global value
	.p2align 4
value:
	.zero 200

