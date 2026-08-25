	.arch armv8-a
	.text
	.p2align 2
	.global reduce
	.type reduce, %function
reduce:
	mov w9, w0
	mov w13, w1
	mov w14, w2
	movz w12, #1
	movz w0, #0
	cbz w9, .Lreduce_bb1
.Lreduce_bb6:
	cmp w9, #1
	b.eq .Lreduce_bb31
.Lreduce_bb8:
	cmp w9, #2
	b.eq .Lreduce_bb9
.Lreduce_bb10:
	cmp w9, #3
	b.eq .Lreduce_bb33
.Lreduce_bb16:
	cmp w9, #4
	b.ne .Lreduce_bb22
.Lreduce_bb35:
	movz w11, #1
	movz w10, #16384, lsl #16
	b .Lreduce_bb17
.Lreduce_bb21:
	lsl w12, w12, #1
	cmp w12, w10
	b.ge .Lreduce_bb22
.Lreduce_bb17:
	sdiv w9, w13, w12
	cmp w9, #0
	and w9, w9, #1
	cneg w9, w9, mi
	cmp w9, #1
	b.eq .Lreduce_bb18
.Lreduce_bb20:
	lsl w0, w0, #1
	b .Lreduce_bb21
.Lreduce_bb22:
	ret
.Lreduce_bb1:
	add w0, w13, w14
	movz w9, #16384, lsl #16
	cmp w0, w9
	b.le .Lreduce_bb3
.Lreduce_bb23:
	mov w11, w0
	movz w9, #16384, lsl #16
.Lreduce_bb2:
	sub w11, w11, w9
	cmp w11, w9
	b.gt .Lreduce_bb2
.Lreduce_bb26:
	mov w0, w11
.Lreduce_bb3:
	cmp w0, #0
	b.ge .Lreduce_bb22
.Lreduce_bb27:
	mov w11, w0
	movz w10, #16384, lsl #16
	movz w9, #49152, lsl #16
.Lreduce_bb4:
	add w0, w11, w10
	cmp w11, w9
	b.ge .Lreduce_bb22
.Lreduce_bb28:
	mov w11, w0
	b .Lreduce_bb4
.Lreduce_bb7:
	sdiv w9, w13, w16
	sdiv w11, w14, w16
	cmp w9, #0
	and w9, w9, #1
	cneg w15, w9, mi
	cmp w11, #0
	and w9, w11, #1
	cneg w9, w9, mi
	lsl w11, w0, #1
	cmp w15, w9
	orr w9, w11, w12
	csel w0, w11, w9, eq
	lsl w16, w16, #1
	cmp w16, w10
	b.lt .Lreduce_bb7
	b .Lreduce_bb22
.Lreduce_bb9:
	cmp w13, w14
	csel w0, w13, w14, gt
	b .Lreduce_bb22
.Lreduce_bb15:
	lsl w12, w12, #1
	cmp w12, w10
	b.ge .Lreduce_bb22
.Lreduce_bb11:
	sdiv w9, w13, w12
	cmp w9, #0
	and w9, w9, #1
	cneg w9, w9, mi
	cmp w9, #1
	b.eq .Lreduce_bb13
.Lreduce_bb12:
	sdiv w9, w14, w12
	cmp w9, #0
	and w9, w9, #1
	cneg w9, w9, mi
	cmp w9, #1
	b.eq .Lreduce_bb13
.Lreduce_bb14:
	lsl w0, w0, #1
	b .Lreduce_bb15
.Lreduce_bb13:
	lsl w9, w0, #1
	orr w0, w9, w11
	b .Lreduce_bb15
.Lreduce_bb18:
	sdiv w9, w14, w12
	cmp w9, #0
	and w9, w9, #1
	cneg w9, w9, mi
	cmp w9, #1
	b.ne .Lreduce_bb20
.Lreduce_bb19:
	lsl w9, w0, #1
	orr w0, w9, w11
	b .Lreduce_bb21
.Lreduce_bb31:
	mov w16, w12
	movz w12, #1
	movz w10, #16384, lsl #16
	b .Lreduce_bb7
.Lreduce_bb33:
	movz w11, #1
	movz w10, #16384, lsl #16
	b .Lreduce_bb11
	.size reduce, .-reduce
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #176
	stp x21, x22, [sp, #16]
	movz w22, #0
	stp x19, x20, [sp]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	bl getint
	mov w24, w0
	bl getint
	mov w21, w0
	bl getint
	adrp x9, a
	add x19, x9, :lo12:a
	mov w20, w0
	mov x0, x19
	bl getarray
	adrp x9, kernelid
	add x23, x9, :lo12:kernelid
	mov x0, x23
	bl getarray
	str w0, [sp, #80]
	movz w0, #109
	bl _sysy_starttime
	mul w9, w21, w20
	adrp x10, b
	str w9, [sp, #100]
	add x9, x10, :lo12:b
	str x9, [sp, #88]
	add w10, w24, w24, lsr #31
	asr w9, w10, #1
	str w9, [sp, #96]
	str x23, [sp, #104]
	str w22, [sp, #112]
.Lmain_bb1:
	ldr w9, [sp, #80]
	ldr w10, [sp, #112]
	cmp w10, w9
	b.ge .Lmain_bb18
.Lmain_bb2:
	ldr x9, [sp, #104]
	ldr w23, [x9]
	ldr x9, [sp, #88]
	stp x9, x9, [sp, #120]
	str w22, [sp, #136]
.Lmain_bb3:
	ldr w9, [sp, #96]
	ldr w10, [sp, #136]
	sub w9, w10, w9
	str w9, [sp, #140]
	ldr w9, [sp, #96]
	add w9, w10, w9
	str w9, [sp, #144]
	ldr x9, [sp, #128]
	str x9, [sp, #152]
	str w22, [sp, #160]
.Lmain_bb4:
	ldr w9, [sp, #96]
	ldr w10, [sp, #160]
	ldr w26, [sp, #140]
	sub w24, w10, w9
	add w25, w10, w9
	mov w11, w22
	b .Lmain_bb5
.Lmain_bb14:
	ldr w9, [sp, #144]
	add w26, w26, #1
	cmp w26, w9
	b.ge .Lmain_bb15
.Lmain_bb5:
	cmp w26, #0
	b.lt .Lmain_bb22
.Lmain_bb7:
	cmp w26, w21
	b.ge .Lmain_bb24
.Lmain_bb9:
	madd w9, w20, w26, w24
	add x27, x19, w9, sxtw #2
	mov w28, w24
	b .Lmain_bb10
.Lmain_bb13:
	mov w0, w23
	mov w1, w11
	bl reduce
	add w28, w28, #1
	mov w11, w0
	cmp w28, w25
	add x27, x27, #4
	b.ge .Lmain_bb14
.Lmain_bb10:
	cmp w28, #0
	b.lt .Lmain_bb27
.Lmain_bb11:
	cmp w28, w20
	b.ge .Lmain_bb28
.Lmain_bb12:
	ldr w2, [x27]
	b .Lmain_bb13
.Lmain_bb15:
	ldr w9, [sp, #160]
	add w10, w9, #1
	ldr x9, [sp, #152]
	cmp w10, w20
	str w11, [x9], #4
	b.ge .Lmain_bb16
.Lmain_bb20:
	str x9, [sp, #152]
	str w10, [sp, #160]
	b .Lmain_bb4
.Lmain_bb6:
	mov w0, w23
	mov w1, w11
	mov w2, w27
	bl reduce
	add w28, w28, #1
	mov w11, w0
	cmp w28, w25
	b.ge .Lmain_bb14
	b .Lmain_bb6
.Lmain_bb8:
	mov w0, w23
	mov w1, w11
	mov w2, w27
	bl reduce
	add w28, w28, #1
	mov w11, w0
	cmp w28, w25
	b.ge .Lmain_bb14
	b .Lmain_bb8
.Lmain_bb16:
	ldr w9, [sp, #136]
	add w10, w9, #1
	ldr x9, [sp, #128]
	cmp w10, w21
	add x9, x9, w20, sxtw #2
	b.ge .Lmain_bb17
.Lmain_bb19:
	str x9, [sp, #128]
	str w10, [sp, #136]
	b .Lmain_bb3
.Lmain_bb17:
	ldr w9, [sp, #100]
	ldr x1, [sp, #120]
	lsl w2, w9, #2
	mov x0, x19
	bl memcpy
	ldr w9, [sp, #112]
	add w10, w9, #1
	ldr x9, [sp, #104]
	add x9, x9, #4
	str x9, [sp, #104]
	str w10, [sp, #112]
	b .Lmain_bb1
.Lmain_bb18:
	movz w0, #116
	bl _sysy_stoptime
	ldr w0, [sp, #100]
	mov x1, x19
	bl putarray
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #176
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb22:
	mov w28, w24
	movz w27, #0
	b .Lmain_bb6
.Lmain_bb24:
	mov w28, w24
	movz w27, #0
	b .Lmain_bb8
.Lmain_bb27:
	mov w2, w22
	b .Lmain_bb13
.Lmain_bb28:
	mov w2, w22
	b .Lmain_bb13
	.size main, .-main
	.bss
	.global a
	.p2align 4
a:
	.zero 40000000
	.global b
	.p2align 4
b:
	.zero 40000000
	.global kernelid
	.p2align 4
kernelid:
	.zero 40000
