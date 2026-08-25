	.arch armv8-a
	.text
	.p2align 2
	.global set
	.type set, %function
set:
	sub sp, sp, #128
	movi v16.4s, #0
	movz w10, #1
	mov v17.16b, v16.16b
	mov v17.s[0], w10
	movz w9, #16
	mov v16.s[0], w9
	movz w10, #2
	mov v17.s[1], w10
	movz w9, #32
	mov v16.s[1], w9
	movz w10, #4
	mov v17.s[2], w10
	movz w9, #64
	mov v16.s[2], w9
	movz w15, #0
	movz w10, #8
	dup v18.4s, w15
	movz w9, #128
	mov v17.s[3], w10
	mov v16.s[3], w9
	mov x16, sp
	add x11, x16, #44
	str q18, [x11]
	add x10, x16, #60
	str q18, [x10]
	add x9, x16, #76
	str q18, [x9]
	add x10, x16, #92
	str q18, [x10]
	add x9, x16, #108
	str q18, [x9]
	movz w11, #256
	movz w10, #512
	movz w9, #1024
	stp q17, q16, [x16]
	mov x13, x0
	stp w11, w10, [x16, #32]
	mov w14, w1
	str w9, [x16, #40]
	mov w12, w2
	mov w17, w15
.Lset_bb1:
	cmp w17, #17
	b.ge .Lset_bb2
.Lset_bb9:
	add w9, w17, #10
	ldr w10, [x16, w9, sxtw #2]
	add w11, w17, #11
	add x11, x16, w11, sxtw #2
	lsl w9, w10, #1
	str w9, [x11]
	ldr w10, [x11]
	add w11, w17, #12
	add x11, x16, w11, sxtw #2
	lsl w10, w10, #1
	str w10, [x11]
	ldr w10, [x11]
	add w11, w17, #13
	add x11, x16, w11, sxtw #2
	lsl w9, w10, #1
	str w9, [x11]
	ldr w9, [x11]
	add w11, w17, #4
	add w10, w17, #14
	lsl w9, w9, #1
	str w9, [x16, w10, sxtw #2]
	mov w17, w11
	b .Lset_bb1
.Lset_bb2:
	cmp w17, #20
	b.ge .Lset_bb4
.Lset_bb3:
	add w9, w17, #10
	ldr w9, [x16, w9, sxtw #2]
	add w11, w17, #1
	add w10, w17, #11
	lsl w9, w9, #1
	str w9, [x16, w10, sxtw #2]
	mov w17, w11
	b .Lset_bb2
.Lset_bb4:
	movz w9, #34953
	movk w9, #34952, lsl #16
	smull x9, w14, w9
	asr x9, x9, #32
	add w9, w9, w14
	asr w9, w9, #4
	add w11, w9, w9, lsr #31
	movz w9, #10000
	cmp w11, w9
	b.ge .Lset_bb8
.Lset_bb5:
	movz w9, #34953
	movk w9, #34952, lsl #16
	smull x9, w14, w9
	asr x9, x9, #32
	add w9, w9, w14
	asr w9, w9, #4
	movz w10, #30
	add w9, w9, w9, lsr #31
	msub w9, w9, w10, w14
	add x17, x13, w11, sxtw #2
	ldr w10, [x17]
	ldr w14, [x16, w9, sxtw #2]
	sdiv w9, w10, w14
	cmp w9, #0
	and w9, w9, #1
	cneg w13, w9, mi
	cmp w13, w12
	b.eq .Lset_bb7
.Lset_bb6:
	cmp w13, #0
	cset w10, eq
	movz w11, #0
	cmp w12, #1
	csel w9, w14, w11, eq
	cmp w10, #0
	csel w11, w9, w11, ne
	cmp w13, #1
	cset w10, eq
	cmp w12, #0
	sub w9, w11, w14
	csel w9, w9, w11, eq
	cmp w10, #0
	csel w15, w9, w11, ne
.Lset_bb7:
	ldr w9, [x17]
	add w9, w9, w15
	str w9, [x17]
.Lset_bb8:
	movz w0, #0
	add sp, sp, #128
	ret
	.size set, .-set
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	bl getint
	mov w26, w0
	bl getint
	mov w25, w0
	movz w0, #56
	bl _sysy_starttime
	adrp x9, seed
	add x9, x9, :lo12:seed
	ldr w22, [x9, #8]
	adrp x11, seed
	adrp x10, seed
	add x11, x11, :lo12:seed
	add x10, x10, :lo12:seed
	ldr w24, [x11]
	ldr w23, [x10, #4]
	adrp x9, a
	movz w20, #37856
	movz w19, #7557
	add x21, x9, :lo12:a
	movk w20, #4, lsl #16
	movk w19, #28633, lsl #16
.Lmain_bb1:
	cmp w26, #0
	b.le .Lmain_bb3
.Lmain_bb2:
	madd w10, w25, w24, w23
	sdiv w9, w10, w22
	msub w10, w9, w22, w10
	cmp w10, #0
	add w9, w22, w10
	csel w12, w9, w10, lt
	madd w11, w12, w24, w23
	sdiv w9, w11, w22
	smull x10, w12, w19
	msub w11, w9, w22, w11
	asr x9, x10, #49
	add w9, w9, w9, lsr #31
	msub w1, w9, w20, w12
	cmp w11, #0
	add w10, w22, w11
	csel w25, w10, w11, lt
	cmp w25, #0
	and w9, w25, #1
	sub w26, w26, #1
	cneg w2, w9, mi
	mov x0, x21
	bl set
	b .Lmain_bb1
.Lmain_bb3:
	movz w0, #64
	bl _sysy_stoptime
	movz w0, #10000
	mov x1, x21
	bl putarray
	adrp x9, staticvalue
	str w25, [x9, :lo12:staticvalue]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.data
	.global seed
	.p2align 3
seed:
	.word 19971231
	.word 19981013
	.word 1000000007
	.global staticvalue
	.p2align 2
staticvalue:
	.zero 4
	.bss
	.global a
	.p2align 4
a:
	.zero 40000
