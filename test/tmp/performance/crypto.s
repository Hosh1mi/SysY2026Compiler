	.arch armv8-a
	.text
	.p2align 2
	.global pseudo_md5
	.type pseudo_md5, %function
pseudo_md5:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #672
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x27, x28, [sp, #64]
	movz w22, #0
	stp x23, x24, [sp, #32]
	movz w27, #21622
	stp x25, x26, [sp, #48]
	movz w21, #56574
	movz w20, #43913
	movz w6, #42104
	movz w7, #46934
	movz w8, #28891
	movz w28, #52974
	movz w16, #9570
	movz w14, #24866
	movz w11, #8772
	str x2, [sp, #80]
	mov x26, x0
	mov w12, w1
	add x25, sp, #96
	add x24, sp, #352
	add x23, sp, #608
	movk w27, #4146, lsl #16
	movk w21, #39098, lsl #16
	movk w20, #61389, lsl #16
	movz w19, #1
	mov w4, w22
	movz w5, #3
	movk w6, #1898, lsl #16
	movk w7, #2247, lsl #16
	movk w8, #1056, lsl #16
	movk w28, #445, lsl #16
	movz w17, #7
	movk w16, #1566, lsl #16
	movz w15, #5
	movk w14, #3485, lsl #16
	movz w13, #4
	movk w11, #1065, lsl #16
	movz w10, #6
.Lpseudo_md5_bb1:
	cmp w4, #64
	b.ge .Lpseudo_md5_bb17
.Lpseudo_md5_bb2:
	cmp w4, #16
	b.ge .Lpseudo_md5_bb11
.Lpseudo_md5_bb3:
	and w9, w4, w5
	cbz w9, .Lpseudo_md5_bb4
.Lpseudo_md5_bb5:
	cmp w9, #1
	b.eq .Lpseudo_md5_bb6
.Lpseudo_md5_bb7:
	cmp w9, #2
	b.eq .Lpseudo_md5_bb8
.Lpseudo_md5_bb9:
	str w28, [x25, w4, sxtw #2]
.Lpseudo_md5_bb10:
	str w17, [x24, w4, sxtw #2]
.Lpseudo_md5_bb16:
	add w4, w4, #1
	b .Lpseudo_md5_bb1
.Lpseudo_md5_bb4:
	str w6, [x25, w4, sxtw #2]
	b .Lpseudo_md5_bb10
.Lpseudo_md5_bb6:
	str w7, [x25, w4, sxtw #2]
	b .Lpseudo_md5_bb10
.Lpseudo_md5_bb8:
	str w8, [x25, w4, sxtw #2]
	b .Lpseudo_md5_bb10
.Lpseudo_md5_bb11:
	cmp w4, #32
	b.lt .Lpseudo_md5_bb12
.Lpseudo_md5_bb13:
	cmp w4, #48
	b.lt .Lpseudo_md5_bb14
.Lpseudo_md5_bb15:
	str w11, [x25, w4, sxtw #2]
	str w10, [x24, w4, sxtw #2]
	b .Lpseudo_md5_bb16
.Lpseudo_md5_bb12:
	str w16, [x25, w4, sxtw #2]
	str w15, [x24, w4, sxtw #2]
	b .Lpseudo_md5_bb16
.Lpseudo_md5_bb14:
	str w14, [x25, w4, sxtw #2]
	str w13, [x24, w4, sxtw #2]
	b .Lpseudo_md5_bb16
.Lpseudo_md5_bb17:
	add w28, w12, #1
	movz w9, #63
	movz w10, #128
	and w9, w28, w9
	str w10, [x26, w12, sxtw #2]
	cmp w9, #56
	b.eq .Lpseudo_md5_bb20
.Lpseudo_md5_bb18:
	add x11, x26, w28, sxtw #2
	movz w10, #0
.Lpseudo_md5_bb19:
	add w28, w28, #1
	cmp w28, #0
	cneg w9, w28, mi
	and w9, w9, #63
	cneg w9, w9, mi
	str w10, [x11], #4
	cmp w9, #56
	b.ne .Lpseudo_md5_bb19
.Lpseudo_md5_bb20:
	lsl w10, w12, #3
	add w9, w28, #1
	str w10, [x26, w28, sxtw #2]
	add x0, x26, w9, sxtw #2
	movz w1, #0
	movz w2, #12
	bl memset
	movn w10, #3
	add w9, w28, #4
	str w9, [sp, #88]
	cmp w28, w10
	b.le .Lpseudo_md5_bb65
.Lpseudo_md5_bb50:
	movz w2, #8961
	mov w6, w22
	mov w5, w27
	mov w4, w21
	mov w3, w20
	movk w2, #26437, lsl #16
	orr w7, wzr, #0x7ffffff0
	movz w8, #16
	b .Lpseudo_md5_bb21
.Lpseudo_md5_bb44:
	ldr w9, [sp, #88]
	add w6, w6, #64
	add w2, w2, w10
	add w3, w3, w20
	add w4, w4, w21
	add w5, w5, w27
	cmp w6, w9
	b.ge .Lpseudo_md5_bb66
.Lpseudo_md5_bb21:
	add x15, x26, w6, sxtw #2
	mov w13, w22
.Lpseudo_md5_bb22:
	cmp w13, #13
	b.ge .Lpseudo_md5_bb24
.Lpseudo_md5_bb23:
	ldr w9, [x15]
	str w9, [x23, w13, sxtw #2]
	ldr w9, [x15, #4]
	add w10, w13, #1
	str w9, [x23, w10, sxtw #2]
	ldr w9, [x15, #8]
	add w10, w13, #2
	str w9, [x23, w10, sxtw #2]
	ldr w10, [x15, #12]
	add x9, x15, #4
	add x9, x9, #4
	add x9, x9, #4
	add w11, w13, #3
	str w10, [x23, w11, sxtw #2]
	add w13, w13, #4
	add x15, x9, #4
	b .Lpseudo_md5_bb22
.Lpseudo_md5_bb24:
	cmp w13, w7
	cset w12, le
	cmp w13, #1
	sub w10, w8, w13
	add x14, x23, w13, sxtw #2
	cset w11, lt
	add x9, x15, w10, sxtw #2
	cmp x9, x14
	add x9, x14, w10, sxtw #2
	cset w10, ls
	cmp x9, x15
	cset w9, ls
	and w11, w12, w11
	orr w9, w10, w9
	and w9, w11, w9
	cbz w9, .Lpseudo_md5_bb53
.Lpseudo_md5_bb52:
	mov x9, x14
	mov x10, x15
	mov w11, w13
.Lpseudo_md5_bb25:
	cmp w11, #8
	b.gt .Lpseudo_md5_bb54
.Lpseudo_md5_bb26:
	ldp q17, q16, [x10]
	stp q17, q16, [x9]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .Lpseudo_md5_bb25
.Lpseudo_md5_bb28:
	cmp w11, #16
	b.ge .Lpseudo_md5_bb55
.Lpseudo_md5_bb45:
	ldr w9, [x12], #4
	add w10, w11, #1
	str w9, [x23, w11, sxtw #2]
	mov w11, w10
	b .Lpseudo_md5_bb28
.Lpseudo_md5_bb29:
	cmp w28, #64
	b.ge .Lpseudo_md5_bb44
.Lpseudo_md5_bb30:
	cmp w28, #16
	b.lt .Lpseudo_md5_bb56
.Lpseudo_md5_bb31:
	cmp w28, #32
	b.lt .Lpseudo_md5_bb32
.Lpseudo_md5_bb33:
	cmp w28, #48
	b.lt .Lpseudo_md5_bb34
.Lpseudo_md5_bb35:
	mul w9, w28, w13
	cmp w9, #0
	cneg w9, w9, mi
	and w9, w9, #15
	sub w12, w14, w21
	cneg w9, w9, mi
.Lpseudo_md5_bb36:
	add w12, w10, w12
	ldr w10, [x25, w28, sxtw #2]
	add w10, w12, w10
	ldr w9, [x23, w9, sxtw #2]
	ldr w12, [x24, w28, sxtw #2]
	add w0, w10, w9
	cmp w12, #1
	b.le .Lpseudo_md5_bb43
.Lpseudo_md5_bb37:
	cmp w12, #4
	cset w10, gt
	cmp w12, w11
	cset w9, ge
	and w9, w9, w10
	sub w1, w12, #3
	cbz w9, .Lpseudo_md5_bb59
.Lpseudo_md5_bb57:
	mov w10, w19
.Lpseudo_md5_bb38:
	cmp w0, #0
	and w9, w0, #1
	lsl w0, w0, #1
	cneg w9, w9, mi
	add w0, w0, w9
	cmp w0, #0
	and w9, w0, #1
	lsl w0, w0, #1
	cneg w9, w9, mi
	add w0, w0, w9
	cmp w0, #0
	and w9, w0, #1
	lsl w0, w0, #1
	cneg w9, w9, mi
	add w0, w0, w9
	cmp w0, #0
	and w9, w0, #1
	add w10, w10, #4
	lsl w0, w0, #1
	cneg w9, w9, mi
	add w0, w0, w9
	cmp w10, w1
	b.lt .Lpseudo_md5_bb38
.Lpseudo_md5_bb39:
	cmp w10, w12
	b.ge .Lpseudo_md5_bb43
.Lpseudo_md5_bb60:
	mov w1, w0
.Lpseudo_md5_bb41:
	cmp w1, #0
	and w9, w1, #1
	add w10, w10, #1
	lsl w1, w1, #1
	cneg w9, w9, mi
	add w1, w1, w9
	cmp w10, w12
	b.lt .Lpseudo_md5_bb41
.Lpseudo_md5_bb63:
	mov w0, w1
.Lpseudo_md5_bb43:
	mov w10, w27
	mov w27, w21
	add w9, w0, w20
	mov w21, w20
	add w28, w28, #1
	mov w20, w9
	b .Lpseudo_md5_bb29
.Lpseudo_md5_bb32:
	madd w9, w28, w17, w16
	cmp w9, #0
	cneg w9, w9, mi
	and w9, w9, #15
	cneg w9, w9, mi
	mov w12, w22
	b .Lpseudo_md5_bb36
.Lpseudo_md5_bb34:
	madd w9, w28, w15, w17
	cmp w9, #0
	cneg w9, w9, mi
	add w12, w21, w27
	and w9, w9, #15
	sub w12, w12, w20
	cneg w9, w9, mi
	b .Lpseudo_md5_bb36
.Lpseudo_md5_bb66:
	mov w9, w2
.Lpseudo_md5_bb46:
	movi v16.4s, #0
	mov v16.s[0], w9
	mov v16.s[1], w3
	mov v16.s[2], w4
	mov v16.s[3], w5
	ldr x9, [sp, #80]
	str q16, [x9]
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #672
	ldp xzr, x30, [sp], #16
	ret
.Lpseudo_md5_bb53:
	mov x12, x15
	mov w11, w13
	b .Lpseudo_md5_bb28
.Lpseudo_md5_bb54:
	mov x12, x10
	b .Lpseudo_md5_bb28
.Lpseudo_md5_bb55:
	mov w28, w22
	mov w27, w5
	mov w21, w4
	mov w20, w3
	mov w10, w2
	movz w17, #5
	movz w16, #1
	movz w15, #3
	movz w14, #0
	movz w13, #7
	orr w11, wzr, #0x80000003
	b .Lpseudo_md5_bb29
.Lpseudo_md5_bb56:
	mov w9, w28
	mov w12, w22
	b .Lpseudo_md5_bb36
.Lpseudo_md5_bb59:
	mov w10, w19
	mov w1, w0
	b .Lpseudo_md5_bb41
.Lpseudo_md5_bb65:
	movz w9, #8961
	movk w9, #26437, lsl #16
	mov w3, w20
	mov w4, w21
	mov w5, w27
	b .Lpseudo_md5_bb46
	.size pseudo_md5, .-pseudo_md5
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #128
	stp x23, x24, [sp, #32]
	movz w23, #0
	dup v16.4s, w23
	stp x25, x26, [sp, #48]
	add x25, sp, #64
	stp x21, x22, [sp, #16]
	mov x22, x25
	stp x19, x20, [sp]
	add x9, x22, #4
	str w23, [x25]
	add x24, sp, #96
	str q16, [x9]
	bl getint
	mov w26, w0
	bl getint
	mov w19, w0
	movz w0, #261
	bl _sysy_starttime
	movz w10, #0
	dup v16.4s, w10
	adrp x9, buffer
	str q16, [x24]
	add x20, x9, :lo12:buffer
	str w10, [x24, #16]
	mov x21, x24
.Lmain_bb1:
	cmp w19, #0
	b.le .Lmain_bb12
.Lmain_bb13:
	movz w11, #8225
	mov x14, x20
	mov w10, w26
	mov w13, w23
	movz w12, #31997
	movk w11, #4, lsl #16
.Lmain_bb2:
	cmp w13, w12
	b.ge .Lmain_bb14
.Lmain_bb11:
	add w10, w10, w10, lsl #13
	asr w9, w10, #31
	lsr w9, w9, #15
	add w9, w10, w9
	asr w9, w9, #17
	add w15, w10, w9
	mul w10, w15, w11
	asr w9, w10, #31
	lsr w9, w9, #15
	add w9, w10, w9
	asr w9, w9, #17
	add w26, w10, w9
	mul w16, w26, w11
	asr w9, w16, #31
	add w10, w15, w15, lsl #5
	lsr w9, w9, #15
	cmp w10, #0
	add w9, w16, w9
	cneg w10, w10, mi
	asr w15, w9, #17
	and w9, w10, #255
	cneg w17, w9, mi
	add w15, w16, w15
	add w9, w26, w26, lsl #5
	mul w16, w15, w11
	cmp w9, #0
	cneg w9, w9, mi
	and w10, w9, #255
	asr w9, w16, #31
	cneg w26, w10, mi
	add w10, w15, w15, lsl #5
	lsr w9, w9, #15
	cmp w10, #0
	add w9, w16, w9
	cneg w10, w10, mi
	asr w15, w9, #17
	and w9, w10, #255
	add w10, w16, w15
	cneg w16, w9, mi
	add w10, w10, w10, lsl #5
	cmp w10, #0
	cneg w9, w10, mi
	add x15, x14, #4
	and w9, w9, #255
	add x15, x15, #4
	cneg w9, w9, mi
	stp w17, w26, [x14]
	add x15, x15, #4
	stp w16, w9, [x14, #8]
	add w13, w13, #4
	add x14, x15, #4
	b .Lmain_bb2
.Lmain_bb3:
	cmp w13, w11
	b.ge .Lmain_bb5
.Lmain_bb4:
	add w10, w26, w26, lsl #13
	asr w9, w10, #31
	lsr w9, w9, #15
	add w9, w10, w9
	asr w9, w9, #17
	add w9, w10, w9
	add w26, w9, w9, lsl #5
	cmp w26, #0
	cneg w9, w26, mi
	and w9, w9, #255
	cneg w9, w9, mi
	str w9, [x14], #4
	add w13, w13, #1
	b .Lmain_bb3
.Lmain_bb5:
	movz w1, #32000
	mov x0, x20
	mov x2, x22
	bl pseudo_md5
	mov w13, w23
	movz w12, #0
.Lmain_bb6:
	cmp w13, #2
	b.ge .Lmain_bb15
.Lmain_bb10:
	add x11, x24, w13, sxtw #2
	ldr w10, [x11]
	ldr w9, [x25, w13, sxtw #2]
	add w9, w10, w9
	sub w10, w12, w9
	str w10, [x11]
	add w9, w13, #1
	add x11, x24, w9, sxtw #2
	ldr w10, [x11]
	ldr w9, [x25, w9, sxtw #2]
	add w9, w10, w9
	sub w10, w12, w9
	str w10, [x11]
	add w9, w13, #2
	add x11, x24, w9, sxtw #2
	ldr w10, [x11]
	ldr w9, [x25, w9, sxtw #2]
	add w9, w10, w9
	sub w10, w12, w9
	str w10, [x11]
	add w9, w13, #3
	add x11, x24, w9, sxtw #2
	ldr w10, [x11]
	ldr w9, [x25, w9, sxtw #2]
	add w9, w10, w9
	sub w10, w12, w9
	str w10, [x11]
	add w13, w13, #4
	b .Lmain_bb6
.Lmain_bb7:
	cmp w13, #5
	b.ge .Lmain_bb9
.Lmain_bb8:
	add x12, x24, w13, sxtw #2
	ldr w10, [x12]
	ldr w9, [x25, w13, sxtw #2]
	add w9, w10, w9
	sub w10, w11, w9
	str w10, [x12]
	add w13, w13, #1
	b .Lmain_bb7
.Lmain_bb9:
	sub w19, w19, #1
	b .Lmain_bb1
.Lmain_bb12:
	movz w0, #284
	bl _sysy_stoptime
	movz w0, #5
	mov x1, x21
	bl putarray
	adrp x9, state
	str w26, [x9, :lo12:state]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #128
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb14:
	mov w26, w10
	movz w11, #32000
	b .Lmain_bb3
.Lmain_bb15:
	movz w11, #0
	b .Lmain_bb7
	.size main, .-main
	.data
	.global state
	.p2align 2
state:
	.word 19260817
	.global buffer
	.p2align 4
buffer:
	.zero 131072
