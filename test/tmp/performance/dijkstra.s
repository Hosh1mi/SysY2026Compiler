	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #96
	stp x21, x22, [sp, #16]
	movz w22, #0
	str d8, [sp, #80]
	fmov s8, w22
	stp x19, x20, [sp]
	movn w21, #0
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	bl getint
	mov w20, w0
	bl getint
	mov w19, w0
	movz w0, #56
	bl _sysy_starttime
	adrp x9, INF_F
	ldr s18, [x9, :lo12:INF_F]
	mov w14, w22
.Lmain_bb1:
	cmp w14, w20
	b.ge .Lmain_bb2
.Lmain_bb53:
	adrp x9, graph
	add x10, x9, :lo12:graph
	sxtw x9, w14
	add x9, x10, x9, lsl #13
	movz w11, #34079
	add w16, w14, w14, lsl #4
	sub w26, w20, #1
	mov x27, x9
	mov w25, w22
	orr w24, wzr, #0x80000001
	movz w12, #0
	movz w23, #23
	movz w17, #100
	movk w11, #20971, lsl #16
	movz w10, #16672, lsl #16
.Lmain_bb54:
	cmp w25, w26
	cset w13, lt
	cmp w20, w24
	cset w9, ge
	and w9, w9, w13
	cbz w9, .Lmain_bb66
.Lmain_bb55:
	cmp w14, w25
	b.eq .Lmain_bb56
.Lmain_bb57:
	madd w9, w25, w23, w16
	add w13, w9, w19
	smull x9, w13, w11
	asr x9, x9, #37
	add w9, w9, w9, lsr #31
	msub w9, w9, w17, w13
	cmp w9, #70
	b.lt .Lmain_bb58
.Lmain_bb59:
	str s18, [x27]
.Lmain_bb60:
	add w9, w25, #1
	add x15, x27, #4
	cmp w14, w9
	b.eq .Lmain_bb61
.Lmain_bb62:
	madd w9, w9, w23, w16
	add w13, w9, w19
	smull x9, w13, w11
	asr x9, x9, #37
	add w9, w9, w9, lsr #31
	msub w9, w9, w17, w13
	cmp w9, #70
	b.lt .Lmain_bb63
.Lmain_bb64:
	str s18, [x27, #4]
.Lmain_bb65:
	add w25, w25, #2
	add x27, x15, #4
	b .Lmain_bb54
.Lmain_bb2:
	adrp x9, INF_F
	ldr s19, [x9, :lo12:INF_F]
	adrp x9, dist
	add x15, x9, :lo12:dist
	mov x14, x15
	mov w13, w22
.Lmain_bb3:
	cmp w13, #51
	b.ge .Lmain_bb4
.Lmain_bb10:
	dup v17.4s, v19.s[0]
	adrp x9, visited
	movi v16.4s, #0
	add x16, x9, :lo12:visited
	cmp w20, #15
	sub w12, w20, #8
	b.le .Lmain_bb77
.Lmain_bb76:
	mov x9, x16
	mov x10, x14
	mov w11, w22
.Lmain_bb11:
	cmp w11, w12
	b.gt .Lmain_bb13
.Lmain_bb12:
	str q17, [x10]
	str q16, [x9]
	str q17, [x10, #16]
	str q16, [x9, #16]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .Lmain_bb11
.Lmain_bb4:
	adrp x9, INF_F
	ldr s21, [x9, :lo12:INF_F]
	sub w12, w20, #3
	orr w11, wzr, #0x80000003
.Lmain_bb5:
	cmp w22, w12
	cset w10, lt
	cmp w20, w11
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb75
.Lmain_bb9:
	ldp s17, s19, [x14]
	fadd s16, s8, s17
	fcmp s17, s21
	fcsel s20, s16, s8, lt
	fadd s16, s20, s19
	ldp s18, s17, [x14, #8]
	fcmp s19, s21
	fcsel s19, s16, s20, lt
	fadd s16, s19, s18
	fcmp s18, s21
	fcsel s18, s16, s19, lt
	fadd s16, s18, s17
	fcmp s17, s21
	add x9, x14, #4
	add x9, x9, #4
	fcsel s8, s16, s18, lt
	add x9, x9, #4
	add w22, w22, #4
	add x14, x9, #4
	b .Lmain_bb5
.Lmain_bb6:
	cmp w10, w20
	b.ge .Lmain_bb8
.Lmain_bb7:
	ldr s17, [x9], #4
	fadd s16, s8, s17
	fcmp s17, s21
	fcsel s8, s16, s8, lt
	add w10, w10, #1
	b .Lmain_bb6
.Lmain_bb8:
	movz w0, #92
	bl _sysy_stoptime
	movz w9, #29491
	movk w9, #17568, lsl #16
	fmov s16, w9
	fsub s17, s8, s16
	movz w9, #0
	fmov s16, w9
	fsub s16, s16, s17
	fcmp s17, #0.0
	movz w9, #19714
	fcsel s17, s16, s17, lt
	movk w9, #16292, lsl #16
	fmov s16, w9
	fcmp s17, s16
	cset w0, le
	bl putint
	movz w0, #10
	bl putch
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	ldr d8, [sp, #80]
	movz w0, #0
	add sp, sp, #96
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb13:
	adrp x10, dist
	adrp x9, visited
	add x10, x10, :lo12:dist
	add x9, x9, :lo12:visited
	add x19, x10, w11, sxtw #2
	add x23, x9, w11, sxtw #2
	sub w24, w20, #3
	orr w17, wzr, #0x80000003
	movz w12, #0
.Lmain_bb14:
	cmp w11, w24
	cset w10, lt
	cmp w20, w17
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb79
.Lmain_bb52:
	str s19, [x19]
	str w12, [x23]
	str s19, [x19, #4]
	str w12, [x23, #4]
	str s19, [x19, #8]
	add x10, x19, #4
	str w12, [x23, #8]
	add x9, x23, #4
	str s19, [x19, #12]
	add x10, x10, #4
	add x9, x9, #4
	str w12, [x23, #12]
	add x10, x10, #4
	add x9, x9, #4
	add w11, w11, #4
	add x19, x10, #4
	add x23, x9, #4
	b .Lmain_bb14
.Lmain_bb15:
	cmp w11, w20
	b.ge .Lmain_bb17
.Lmain_bb16:
	str s19, [x10], #4
	add w11, w11, #1
	str w12, [x9], #4
	b .Lmain_bb15
.Lmain_bb17:
	movz w9, #0
	fmov s16, w9
	str s16, [x15]
	mov w23, w22
	movn w19, #0
.Lmain_bb18:
	cmp w23, w20
	b.ge .Lmain_bb51
.Lmain_bb19:
	sub w25, w20, #1
	mov x27, x14
	mov x24, x16
	mov w12, w21
	fmov s17, s19
	mov w11, w22
	orr w26, wzr, #0x80000001
.Lmain_bb20:
	cmp w11, w25
	cset w10, lt
	cmp w20, w26
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb26
.Lmain_bb21:
	ldr w9, [x24]
	cbz w9, .Lmain_bb22
.Lmain_bb23:
	ldr w9, [x24, #4]
	add x24, x24, #4
	add x17, x27, #4
	cbz w9, .Lmain_bb24
.Lmain_bb25:
	add w11, w11, #2
	add x24, x24, #4
	add x27, x17, #4
	b .Lmain_bb20
.Lmain_bb22:
	ldr s16, [x27]
	fcmp s16, s17
	cset w9, lt
	cmp w9, #0
	csel w12, w11, w12, ne
	cmp w9, #0
	fcsel s17, s16, s17, ne
	b .Lmain_bb23
.Lmain_bb24:
	ldr s16, [x27, #4]
	fcmp s16, s17
	cset w10, lt
	add w9, w11, #1
	cmp w10, #0
	csel w12, w9, w12, ne
	cmp w10, #0
	fcsel s17, s16, s17, ne
	b .Lmain_bb25
.Lmain_bb26:
	cmp w11, w20
	b.ge .Lmain_bb84
.Lmain_bb82:
	mov x17, x27
	mov x10, x24
.Lmain_bb27:
	cmp w11, w20
	b.ge .Lmain_bb85
.Lmain_bb28:
	ldr w9, [x10]
	cbz w9, .Lmain_bb29
.Lmain_bb30:
	add w11, w11, #1
	add x10, x10, #4
	add x17, x17, #4
	b .Lmain_bb27
.Lmain_bb29:
	ldr s16, [x17]
	fcmp s16, s17
	cset w9, lt
	cmp w9, #0
	csel w12, w11, w12, ne
	cmp w9, #0
	fcsel s17, s16, s17, ne
	b .Lmain_bb30
.Lmain_bb31:
	cmp w24, w19
	b.eq .Lmain_bb51
.Lmain_bb32:
	adrp x12, visited
	adrp x9, graph
	add x17, x12, :lo12:visited
	add x10, x9, :lo12:graph
	adrp x11, dist
	movz w12, #1
	str w12, [x17, w24, sxtw #2]
	sxtw x9, w24
	add x11, x11, :lo12:dist
	add x9, x10, x9, lsl #13
	add x24, x11, w24, sxtw #2
	mov x28, x14
	mov x27, x9
	mov x11, x16
	mov w12, w22
	orr w26, wzr, #0x80000001
.Lmain_bb33:
	cmp w12, w25
	cset w10, lt
	cmp w20, w26
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb43
.Lmain_bb34:
	ldr w9, [x11]
	cbz w9, .Lmain_bb35
.Lmain_bb38:
	ldr w9, [x11, #4]
	add x11, x11, #4
	add x10, x27, #4
	add x17, x28, #4
	cbz w9, .Lmain_bb39
.Lmain_bb42:
	add w12, w12, #2
	add x11, x11, #4
	add x27, x10, #4
	add x28, x17, #4
	b .Lmain_bb33
.Lmain_bb35:
	ldr s18, [x27]
	fcmp s18, s19
	b.ge .Lmain_bb38
.Lmain_bb36:
	ldr s17, [x24]
	ldr s16, [x28]
	fadd s17, s17, s18
	fcmp s17, s16
	b.ge .Lmain_bb38
.Lmain_bb37:
	str s17, [x28]
	b .Lmain_bb38
.Lmain_bb39:
	ldr s18, [x27, #4]
	fcmp s18, s19
	b.ge .Lmain_bb42
.Lmain_bb40:
	ldr s17, [x24]
	ldr s16, [x28, #4]
	fadd s17, s17, s18
	fcmp s17, s16
	b.ge .Lmain_bb42
.Lmain_bb41:
	str s17, [x28, #4]
	b .Lmain_bb42
.Lmain_bb43:
	cmp w12, w20
	b.ge .Lmain_bb50
.Lmain_bb86:
	mov x17, x28
	mov x10, x27
.Lmain_bb44:
	cmp w12, w20
	b.ge .Lmain_bb50
.Lmain_bb45:
	ldr w9, [x11]
	cbz w9, .Lmain_bb46
.Lmain_bb49:
	add w12, w12, #1
	add x11, x11, #4
	add x10, x10, #4
	add x17, x17, #4
	b .Lmain_bb44
.Lmain_bb46:
	ldr s18, [x10]
	fcmp s18, s19
	b.ge .Lmain_bb49
.Lmain_bb47:
	ldr s17, [x24]
	ldr s16, [x17]
	fadd s17, s17, s18
	fcmp s17, s16
	b.ge .Lmain_bb49
.Lmain_bb48:
	str s17, [x17]
	b .Lmain_bb49
.Lmain_bb50:
	add w23, w23, #1
	b .Lmain_bb18
.Lmain_bb51:
	add w13, w13, #1
	b .Lmain_bb3
.Lmain_bb56:
	fmov s16, w12
	str s16, [x27]
	b .Lmain_bb60
.Lmain_bb58:
	add w9, w9, #1
	scvtf s17, w9
	fmov s16, w10
	fdiv s16, s17, s16
	str s16, [x27]
	b .Lmain_bb60
.Lmain_bb61:
	fmov s16, w12
	str s16, [x27, #4]
	b .Lmain_bb65
.Lmain_bb63:
	add w9, w9, #1
	scvtf s17, w9
	fmov s16, w10
	fdiv s16, s17, s16
	str s16, [x27, #4]
	b .Lmain_bb65
.Lmain_bb66:
	cmp w25, w20
	b.ge .Lmain_bb74
.Lmain_bb87:
	movz w11, #34079
	mov x24, x27
	mov w23, w25
	movz w12, #0
	movz w17, #23
	movz w15, #100
	movk w11, #20971, lsl #16
	movz w9, #16672, lsl #16
.Lmain_bb67:
	cmp w23, w20
	b.ge .Lmain_bb74
.Lmain_bb68:
	cmp w14, w23
	b.eq .Lmain_bb69
.Lmain_bb70:
	madd w10, w23, w17, w16
	add w13, w10, w19
	smull x10, w13, w11
	asr x10, x10, #37
	add w10, w10, w10, lsr #31
	msub w10, w10, w15, w13
	cmp w10, #70
	b.lt .Lmain_bb71
.Lmain_bb72:
	str s18, [x24]
.Lmain_bb73:
	add w23, w23, #1
	add x24, x24, #4
	b .Lmain_bb67
.Lmain_bb69:
	fmov s16, w12
	str s16, [x24]
	b .Lmain_bb73
.Lmain_bb71:
	add w10, w10, #1
	scvtf s17, w10
	fmov s16, w9
	fdiv s16, s17, s16
	str s16, [x24]
	b .Lmain_bb73
.Lmain_bb74:
	add w14, w14, #1
	b .Lmain_bb1
.Lmain_bb75:
	mov x9, x14
	mov w10, w22
	b .Lmain_bb6
.Lmain_bb77:
	mov w11, w22
	b .Lmain_bb13
.Lmain_bb79:
	mov x9, x23
	mov x10, x19
	movz w12, #0
	b .Lmain_bb15
.Lmain_bb84:
	mov w24, w12
	b .Lmain_bb31
.Lmain_bb85:
	mov w24, w12
	b .Lmain_bb31
	.size main, .-main
	.data
	.global INF_F
	.p2align 2
INF_F:
	.word 0x49742400
	.bss
	.global dist
	.p2align 4
dist:
	.zero 8192
	.global visited
	.p2align 4
visited:
	.zero 8192
	.global graph
	.p2align 4
graph:
	.zero 16777216
