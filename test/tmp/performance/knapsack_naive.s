	.arch armv8-a
	.text
	.p2align 2
	.global knapsack_naive
	.type knapsack_naive, %function
knapsack_naive:
	mov w12, w0
	cmp w12, #55
	mov w11, w1
	cset w10, lo
	cmp w11, #1024
	cset w9, lo
	and w9, w10, w9
	cbz w9, .Lknapsack_naive_bb3
.Lknapsack_naive_bb1:
	adrp x9, __memo_knapsack_naive
	add x10, x9, :lo12:__memo_knapsack_naive
	sxtw x9, w12
	add x9, x10, x9, lsl #13
	add x9, x9, w11, sxtw #3
	ldr w9, [x9]
	cbz w9, .Lknapsack_naive_bb3
.Lknapsack_naive_bb2:
	adrp x9, __memo_knapsack_naive
	add x10, x9, :lo12:__memo_knapsack_naive
	sxtw x9, w12
	add x9, x10, x9, lsl #13
	add x9, x9, w11, sxtw #3
	ldr w0, [x9, #4]
	ret
.Lknapsack_naive_bb3:
	mov w0, w12
	mov w1, w11
	b knapsack_naive_memo_fill
	.size knapsack_naive, .-knapsack_naive
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	str x21, [sp, #16]
	bl getint
	mov w19, w0
	bl getint
	adrp x9, weight
	add x9, x9, :lo12:weight
	mov w20, w0
	mov x0, x9
	bl getarray
	adrp x9, value
	add x9, x9, :lo12:value
	mov x0, x9
	bl getarray
	movz w0, #32
	bl _sysy_starttime
	mov w0, w19
	mov w1, w20
	bl knapsack_naive
	mov w21, w0
	movz w0, #34
	bl _sysy_stoptime
	mov w0, w21
	bl putint
	movz w0, #10
	bl putch
	adrp x10, N
	adrp x9, W
	str w19, [x10, :lo12:N]
	str w20, [x9, :lo12:W]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global knapsack_naive_memo_body
	.type knapsack_naive_memo_body, %function
knapsack_naive_memo_body:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #48
	str x23, [sp, #32]
	mov w23, w0
	adrp x9, weight
	movn w10, #0
	add x9, x9, :lo12:weight
	add w10, w23, w10
	stp x21, x22, [sp, #16]
	add x10, x9, w10, sxtw #2
	stp x19, x20, [sp]
	mov w22, w1
	movz w0, #0
	movn x9, #3
	b .Lknapsack_naive_memo_body_bb1
.Lknapsack_naive_memo_body_bb3:
	ldr w21, [x10]
	sub w23, w23, #1
	cmp w21, w22
	add x10, x10, x9
	b.le .Lknapsack_naive_memo_body_bb4
.Lknapsack_naive_memo_body_bb1:
	cbz w23, .Lknapsack_naive_memo_body_bb5
.Lknapsack_naive_memo_body_bb2:
	cbz w22, .Lknapsack_naive_memo_body_bb5
	b .Lknapsack_naive_memo_body_bb3
.Lknapsack_naive_memo_body_bb4:
	mov w0, w23
	mov w1, w22
	bl knapsack_naive
	adrp x9, value
	add x9, x9, :lo12:value
	ldr w19, [x9, w23, sxtw #2]
	mov w20, w0
	sub w1, w22, w21
	mov w0, w23
	bl knapsack_naive
	add w9, w19, w0
	cmp w9, w20
	csel w0, w9, w20, gt
.Lknapsack_naive_memo_body_bb5:
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #48
	ldp xzr, x30, [sp], #16
	ret
	.size knapsack_naive_memo_body, .-knapsack_naive_memo_body
	.p2align 2
	.global knapsack_naive_memo_fill
	.type knapsack_naive_memo_fill, %function
knapsack_naive_memo_fill:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #16
	stp x19, x20, [sp]
	mov w20, w0
	mov w19, w1
	mov w0, w20
	mov w1, w19
	bl knapsack_naive_memo_body
	cmp w20, #55
	cset w10, lo
	cmp w19, #1024
	cset w9, lo
	mov w13, w0
	and w9, w10, w9
	cbz w9, .Lknapsack_naive_memo_fill_bb2
.Lknapsack_naive_memo_fill_bb1:
	adrp x9, __memo_knapsack_naive
	add x11, x9, :lo12:__memo_knapsack_naive
	adrp x9, __memo_knapsack_naive
	sxtw x10, w20
	add x9, x9, :lo12:__memo_knapsack_naive
	add x11, x11, x10, lsl #13
	mov x10, x9
	sxtw x9, w20
	add x11, x11, w19, sxtw #3
	movz w12, #1
	add x9, x10, x9, lsl #13
	str w12, [x11]
	add x9, x9, w19, sxtw #3
	str w13, [x9, #4]
.Lknapsack_naive_memo_fill_bb2:
	mov w0, w13
	ldp x19, x20, [sp]
	add sp, sp, #16
	ldp xzr, x30, [sp], #16
	ret
	.size knapsack_naive_memo_fill, .-knapsack_naive_memo_fill
	.data
	.global N
	.p2align 2
N:
	.zero 4
	.global W
	.p2align 2
W:
	.zero 4
	.bss
	.global weight
	.p2align 4
weight:
	.zero 200
	.global value
	.p2align 4
value:
	.zero 200
	.global __memo_knapsack_naive
	.p2align 4
__memo_knapsack_naive:
	.zero 450560
