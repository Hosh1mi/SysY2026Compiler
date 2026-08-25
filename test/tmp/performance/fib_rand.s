	.arch armv8-a
	.text
	.p2align 2
	.global fib
	.type fib, %function
fib:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	str x21, [sp, #16]
	movz w9, #40763
	mov w21, w0
	movk w9, #1117, lsl #16
	mul w11, w21, w9
	stp x19, x20, [sp]
	movz w10, #57843
	mov w20, w1
	movk w10, #4509, lsl #16
	movz w9, #4919
	mul w10, w20, w10
	mov w19, w2
	movk w9, #428, lsl #16
	mul w9, w19, w9
	eor w10, w11, w10
	eor w10, w10, w9
	movz w9, #65535
	and w10, w10, w9
	adrp x9, __memo_hash_fib
	add w10, w10, w10, lsl #2
	add x9, x9, :lo12:__memo_hash_fib
	ldr w9, [x9, w10, sxtw #2]
	cbz w9, .Lfib_bb3
.Lfib_bb1:
	movz w9, #40763
	movk w9, #1117, lsl #16
	mul w11, w21, w9
	movz w10, #57843
	movk w10, #4509, lsl #16
	movz w9, #4919
	mul w10, w20, w10
	movk w9, #428, lsl #16
	mul w9, w19, w9
	eor w10, w11, w10
	eor w10, w10, w9
	movz w9, #65535
	and w9, w10, w9
	add w13, w9, w9, lsl #2
	adrp x9, __memo_hash_fib
	add w10, w13, #1
	add x9, x9, :lo12:__memo_hash_fib
	ldr w12, [x9, w10, sxtw #2]
	adrp x9, __memo_hash_fib
	add w10, w13, #2
	add x9, x9, :lo12:__memo_hash_fib
	ldr w11, [x9, w10, sxtw #2]
	adrp x9, __memo_hash_fib
	add w10, w13, #3
	add x9, x9, :lo12:__memo_hash_fib
	ldr w9, [x9, w10, sxtw #2]
	cmp w12, w21
	cset w12, eq
	cmp w11, w20
	cset w10, eq
	cmp w9, w19
	and w10, w12, w10
	cset w9, eq
	and w9, w10, w9
	cbz w9, .Lfib_bb3
.Lfib_bb2:
	movz w9, #40763
	movk w9, #1117, lsl #16
	mul w11, w21, w9
	movz w10, #57843
	movk w10, #4509, lsl #16
	movz w9, #4919
	mul w10, w20, w10
	movk w9, #428, lsl #16
	mul w9, w19, w9
	eor w10, w11, w10
	eor w10, w10, w9
	movz w9, #65535
	and w11, w10, w9
	movz w10, #5
	movz w9, #4
	madd w10, w11, w10, w9
	adrp x9, __memo_hash_fib
	ldp x20, x21, [sp, #8]
	add x9, x9, :lo12:__memo_hash_fib
	ldr w0, [x9, w10, sxtw #2]
	ldr x19, [sp]
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lfib_bb3:
	mov w0, w21
	mov w1, w20
	mov w2, w19
	bl fib_memo_body
	movz w9, #40763
	movk w9, #1117, lsl #16
	mul w11, w21, w9
	movz w10, #57843
	movk w10, #4509, lsl #16
	movz w9, #4919
	mul w10, w20, w10
	movk w9, #428, lsl #16
	mul w9, w19, w9
	eor w10, w11, w10
	eor w10, w10, w9
	movz w9, #65535
	and w9, w10, w9
	add w12, w9, w9, lsl #2
	adrp x9, __memo_hash_fib
	add w11, w12, #1
	add x10, x9, :lo12:__memo_hash_fib
	str w21, [x10, w11, sxtw #2]
	adrp x9, __memo_hash_fib
	add w11, w12, #2
	add x10, x9, :lo12:__memo_hash_fib
	str w20, [x10, w11, sxtw #2]
	adrp x9, __memo_hash_fib
	add w11, w12, #3
	add x10, x9, :lo12:__memo_hash_fib
	str w19, [x10, w11, sxtw #2]
	adrp x9, __memo_hash_fib
	add w11, w12, #4
	add x10, x9, :lo12:__memo_hash_fib
	str w0, [x10, w11, sxtw #2]
	adrp x9, __memo_hash_fib
	movz w10, #1
	add x9, x9, :lo12:__memo_hash_fib
	str w10, [x9, w12, sxtw #2]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
	.size fib, .-fib
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #96
	stp x27, x28, [sp, #64]
	movz w28, #0
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	bl getint
	str w0, [sp, #80]
	bl getint
	mov w23, w0
	movz w0, #24
	bl _sysy_starttime
	movz w26, #57186
	movz w24, #57607
	movz w21, #36553
	movz w20, #50349
	movz w19, #15187
	mov w27, w28
	movk w26, #304, lsl #16
	movz w25, #23333
	movk w24, #1525, lsl #16
	movk w21, #5497, lsl #16
	movk w20, #26824, lsl #16
	movk w19, #38711, lsl #16
	movz w22, #10
.Lmain_bb1:
	ldr w9, [sp, #80]
	cmp w27, w9
	b.ge .Lmain_bb6
.Lmain_bb2:
	tbz w27, #0, .Lmain_bb3
.Lmain_bb4:
	madd w10, w23, w26, w25
	smull x9, w10, w21
	asr x9, x9, #55
	add w9, w9, w9, lsr #31
	msub w10, w9, w24, w10
	cmp w10, #0
	add w9, w10, w24
	csel w23, w9, w10, lt
	smull x9, w23, w19
	asr x9, x9, #44
	add w10, w9, w9, lsr #31
	movn w9, #10006
	msub w0, w10, w9, w23
	mov w1, w27
	mov w2, w27
	bl fib
	cmp w0, #0
	cneg w9, w0, mi
	and w9, w9, #255
	cneg w9, w9, mi
	add w10, w28, w9
.Lmain_bb5:
	cmp w10, #256
	sub w9, w10, #256
	csel w10, w9, w10, ge
	movn w9, #255
	cmp w10, w9
	add w9, w10, #256
	csel w28, w9, w10, le
	mov w0, w28
	bl putint
	mov w0, w22
	bl putch
	add w27, w27, #1
	b .Lmain_bb1
.Lmain_bb3:
	madd w10, w23, w26, w25
	smull x9, w10, w21
	asr x9, x9, #55
	add w9, w9, w9, lsr #31
	msub w10, w9, w24, w10
	cmp w10, #0
	add w9, w10, w24
	csel w23, w9, w10, lt
	smull x9, w23, w20
	asr x9, x9, #44
	add w0, w9, w9, lsr #31
	mov w1, w27
	mov w2, w28
	bl fib
	cmp w0, #0
	cneg w9, w0, mi
	and w9, w9, #255
	cneg w9, w9, mi
	sub w10, w28, w9
	b .Lmain_bb5
.Lmain_bb6:
	movz w0, #36
	bl _sysy_stoptime
	adrp x9, seed
	str w23, [x9, :lo12:seed]
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #96
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global fib_memo_body
	.type fib_memo_body, %function
fib_memo_body:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	str x21, [sp, #16]
	mov w19, w0
	mov w20, w1
	mov w21, w2
	cbz w20, .Lfib_memo_body_bb2
.Lfib_memo_body_bb1:
	cmp w20, #1
	b.eq .Lfib_memo_body_bb2
.Lfib_memo_body_bb3:
	add w9, w21, #1
	add w9, w9, w9, lsr #31
	add w0, w19, #1
	sub w1, w20, #1
	asr w2, w9, #1
	bl fib
	sub w9, w21, #3
	sub w10, w19, #2
	cmp w9, #0
	add w10, w10, w10, lsr #31
	and w9, w9, #1
	mov w19, w0
	asr w0, w10, #1
	sub w1, w20, #2
	cneg w2, w9, mi
	bl fib
	add w0, w19, w0
.Lfib_memo_body_bb4:
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lfib_memo_body_bb2:
	movz w9, #21846
	lsl w11, w21, #1
	movk w9, #21845, lsl #16
	smull x9, w11, w9
	asr x9, x9, #32
	movz w10, #3
	add w9, w9, w9, lsr #31
	msub w10, w9, w10, w11
	add w9, w19, #1
	add w9, w9, w9, lsr #31
	asr w9, w9, #1
	add w0, w9, w10
	b .Lfib_memo_body_bb4
	.size fib_memo_body, .-fib_memo_body
	.data
	.global seed
	.p2align 2
seed:
	.zero 4
	.bss
	.global __memo_hash_fib
	.p2align 4
__memo_hash_fib:
	.zero 1310720
