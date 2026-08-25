	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #96
	stp x19, x20, [sp]
	adrp x9, buf
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	ldr w25, [x9, :lo12:buf]
	adrp x12, pos
	adrp x11, bits
	adrp x10, out_num
	ldr w23, [x12, :lo12:pos]
	ldr w24, [x11, :lo12:bits]
	ldr w22, [x10, :lo12:out_num]
	adrp x9, data
	add x21, x9, :lo12:data
	movz w19, #0
	mov x0, x21
	bl getarray
	mov w20, w0
	movz w0, #153
	bl _sysy_starttime
	mov w15, w24
	mov w16, w23
	mov w17, w19
	mov w14, w25
	mov w23, w19
	mov w24, w19
	movz w12, #1
	movz w11, #0
	movz w10, #3
.Lmain_bb1:
	cmp w17, #2000
	b.ge .Lmain_bb85
.Lmain_bb67:
	mov x6, x21
	mov w28, w19
	mov w13, w14
	mov w7, w19
	movz w8, #8
	movz w27, #0
.Lmain_bb2:
	cmp w28, #1
	b.ge .Lmain_bb5
.Lmain_bb3:
	cmp w28, w20
	b.ge .Lmain_bb5
.Lmain_bb4:
	ldr w26, [x6], #4
	sub w9, w8, w7
	lsl w25, w26, w7
	cmp w9, #8
	csel w9, w25, w26, lo
	cmp w13, #0
	csel w13, w27, w13, lt
	cmp w9, #0
	csel w9, w27, w9, lt
	orr w13, w13, w9
	add w28, w28, #1
	add w7, w7, #8
	b .Lmain_bb2
.Lmain_bb5:
	add w9, w13, w13, lsr #31
	asr w26, w9, #1
	sub w25, w7, #1
	cmp w7, #3
	b.ge .Lmain_bb10
.Lmain_bb6:
	adrp x9, data
	add x9, x9, :lo12:data
	mov w6, w25
	add x5, x9, w28, sxtw #2
	mov w27, w26
	movz w7, #8
	movz w8, #0
	movn w25, #5
.Lmain_bb7:
	cmp w28, w20
	b.ge .Lmain_bb69
.Lmain_bb8:
	ldr w4, [x5], #4
	sub w9, w7, w6
	lsl w26, w4, w6
	cmp w9, #8
	csel w9, w26, w4, lo
	cmp w27, #0
	csel w26, w8, w27, lt
	cmp w9, #0
	csel w9, w8, w9, lt
	orr w27, w26, w9
	add w26, w6, #8
	add w28, w28, #1
	cmp w6, w25
	b.ge .Lmain_bb70
.Lmain_bb68:
	mov w6, w26
	b .Lmain_bb7
.Lmain_bb10:
	asr w9, w26, #31
	lsr w9, w9, #30
	add w9, w26, w9
	asr w27, w9, #2
	sub w4, w25, #2
	cmp w25, #2
	b.le .Lmain_bb17
.Lmain_bb72:
	mov w2, w4
	b .Lmain_bb11
.Lmain_bb16:
	add w9, w8, w8, lsr #31
	asr w27, w9, #1
	sub w2, w25, #1
	cmp w25, #1
	b.le .Lmain_bb79
.Lmain_bb11:
	cmp w2, #1
	b.ge .Lmain_bb77
.Lmain_bb12:
	adrp x9, data
	add x9, x9, :lo12:data
	add x4, x9, w28, sxtw #2
	mov w8, w27
	mov w5, w2
	movz w6, #8
	movz w7, #0
	movn w25, #6
.Lmain_bb13:
	cmp w28, w20
	b.ge .Lmain_bb75
.Lmain_bb14:
	ldr w3, [x4], #4
	sub w9, w6, w5
	lsl w27, w3, w5
	cmp w9, #8
	csel w9, w27, w3, lo
	cmp w8, #0
	csel w27, w7, w8, lt
	cmp w9, #0
	csel w9, w7, w9, lt
	orr w8, w27, w9
	add w27, w5, #8
	add w28, w28, #1
	cmp w5, w25
	b.ge .Lmain_bb76
.Lmain_bb74:
	mov w5, w27
	b .Lmain_bb13
.Lmain_bb79:
	mov w4, w2
.Lmain_bb17:
	mov w25, w19
	movz w6, #0
	movz w7, #31
.Lmain_bb18:
	cmp w4, #5
	b.ge .Lmain_bb83
.Lmain_bb19:
	adrp x9, data
	add x9, x9, :lo12:data
	mov w8, w27
	add x1, x9, w28, sxtw #2
	mov w2, w4
	movz w3, #8
	movz w5, #0
	movn w27, #2
.Lmain_bb20:
	cmp w28, w20
	b.ge .Lmain_bb81
.Lmain_bb21:
	ldr w0, [x1], #4
	sub w9, w3, w2
	lsl w4, w0, w2
	cmp w9, #8
	csel w9, w4, w0, lo
	cmp w8, #0
	csel w8, w5, w8, lt
	cmp w9, #0
	csel w9, w5, w9, lt
	orr w8, w8, w9
	add w4, w2, #8
	add w28, w28, #1
	cmp w2, w27
	b.ge .Lmain_bb23
.Lmain_bb80:
	mov w2, w4
	b .Lmain_bb20
.Lmain_bb23:
	cmp w8, #0
	asr w9, w8, #31
	csel w27, w6, w8, lt
	lsr w9, w9, #27
	and w5, w27, w7
	add w9, w8, w9
	asr w27, w9, #5
	sub w8, w4, #5
	cmp w5, #0
	b.gt .Lmain_bb24
.Lmain_bb25:
	cmp w28, w20
	b.ge .Lmain_bb27
.Lmain_bb26:
	mov w4, w8
	b .Lmain_bb18
.Lmain_bb24:
	adrp x9, out
	add x4, x9, :lo12:out
	add w5, w5, #64
	str w5, [x4, w25, sxtw #2]
	add w9, w25, #1
	mov w25, w9
	b .Lmain_bb26
.Lmain_bb27:
	cmp w27, w14
	cset w6, ne
	cmp w25, w22
	cset w7, ne
	cmp w8, w15
	cset w22, ne
	cmp w28, w16
	cset w15, ne
	cmp w26, #0
	csel w9, w11, w26, lt
	and w26, w9, w10
	cmp w26, w23
	cset w14, ne
	cmp w13, #0
	orr w16, w6, w7
	csel w9, w11, w13, lt
	orr w16, w16, w22
	and w13, w9, w12
	orr w9, w16, w15
	cmp w13, w24
	orr w14, w9, w14
	cset w9, ne
	add w17, w17, #1
	orr w9, w14, w9
	cbz w9, .Lmain_bb86
.Lmain_bb66:
	mov w14, w27
	mov w22, w25
	mov w15, w8
	mov w16, w28
	mov w23, w26
	mov w24, w13
	b .Lmain_bb1
.Lmain_bb28:
	movz w0, #162
	bl _sysy_stoptime
	mov w0, w24
	bl putint
	movz w0, #32
	bl putch
	mov w0, w26
	bl putint
	movz w0, #10
	bl putch
	cmp w22, #1000
	b.le .Lmain_bb29
.Lmain_bb32:
	cmp w22, #1000
	b.le .Lmain_bb48
.Lmain_bb33:
	movz w9, #10000
	cmp w22, w9
	b.le .Lmain_bb34
.Lmain_bb39:
	adrp x9, out
	mov w28, w19
	add x9, x9, :lo12:out
	movz w25, #34464
	movz w19, #34079
	mov x27, x9
	movz w26, #10000
	movk w25, #1, lsl #16
	movz w24, #50
	movk w19, #20971, lsl #16
	movz w23, #100
.Lmain_bb40:
	cmp w28, w22
	b.ge .Lmain_bb62
.Lmain_bb41:
	cmp w22, w26
	b.le .Lmain_bb45
.Lmain_bb42:
	cmp w22, w25
	b.gt .Lmain_bb45
.Lmain_bb43:
	smull x9, w28, w19
	asr x9, x9, #36
	add w9, w9, w9, lsr #31
	msub w9, w9, w24, w28
	cbz w9, .Lmain_bb44
.Lmain_bb47:
	add w28, w28, #1
	add x27, x27, #4
	b .Lmain_bb40
.Lmain_bb29:
	adrp x9, out
	add x9, x9, :lo12:out
	mov x23, x9
.Lmain_bb30:
	cmp w19, w22
	b.ge .Lmain_bb62
.Lmain_bb31:
	ldr w0, [x23]
	bl putch
	add w19, w19, #1
	add x23, x23, #4
	b .Lmain_bb30
.Lmain_bb34:
	adrp x9, out
	mov w25, w19
	add x9, x9, :lo12:out
	movz w19, #26215
	mov x24, x9
	movz w23, #5
	movk w19, #26214, lsl #16
.Lmain_bb35:
	cmp w25, w22
	b.ge .Lmain_bb62
.Lmain_bb36:
	smull x9, w25, w19
	asr x9, x9, #33
	add w9, w9, w9, lsr #31
	msub w9, w9, w23, w25
	cbz w9, .Lmain_bb37
.Lmain_bb38:
	add w25, w25, #1
	add x24, x24, #4
	b .Lmain_bb35
.Lmain_bb37:
	ldr w0, [x24]
	bl putch
	b .Lmain_bb38
.Lmain_bb44:
	ldr w0, [x27]
	bl putch
	b .Lmain_bb47
.Lmain_bb45:
	smull x9, w28, w19
	asr x9, x9, #37
	add w9, w9, w9, lsr #31
	msub w9, w9, w23, w28
	cbnz w9, .Lmain_bb47
.Lmain_bb46:
	ldr w0, [x27]
	bl putch
	b .Lmain_bb47
.Lmain_bb48:
	movz w9, #10000
	cmp w22, w9
	b.le .Lmain_bb60
.Lmain_bb49:
	movz w9, #34464
	movk w9, #1, lsl #16
	cmp w22, w9
	b.le .Lmain_bb50
.Lmain_bb55:
	adrp x9, out
	mov w25, w19
	add x9, x9, :lo12:out
	movz w19, #34079
	mov x24, x9
	movz w23, #100
	movk w19, #20971, lsl #16
.Lmain_bb56:
	cmp w25, w22
	b.ge .Lmain_bb62
.Lmain_bb57:
	smull x9, w25, w19
	asr x9, x9, #37
	add w9, w9, w9, lsr #31
	msub w9, w9, w23, w25
	cbz w9, .Lmain_bb58
.Lmain_bb59:
	add w25, w25, #1
	add x24, x24, #4
	b .Lmain_bb56
.Lmain_bb50:
	adrp x9, out
	mov w25, w19
	add x9, x9, :lo12:out
	movz w19, #34079
	mov x24, x9
	movz w23, #50
	movk w19, #20971, lsl #16
.Lmain_bb51:
	cmp w25, w22
	b.ge .Lmain_bb62
.Lmain_bb52:
	smull x9, w25, w19
	asr x9, x9, #36
	add w9, w9, w9, lsr #31
	msub w9, w9, w23, w25
	cbz w9, .Lmain_bb53
.Lmain_bb54:
	add w25, w25, #1
	add x24, x24, #4
	b .Lmain_bb51
.Lmain_bb53:
	ldr w0, [x24]
	bl putch
	b .Lmain_bb54
.Lmain_bb58:
	ldr w0, [x24]
	bl putch
	b .Lmain_bb59
.Lmain_bb60:
	adrp x9, out
	mov w25, w19
	add x9, x9, :lo12:out
	movz w19, #34079
	mov x24, x9
	movz w23, #100
	movk w19, #20971, lsl #16
.Lmain_bb61:
	cmp w25, w22
	b.ge .Lmain_bb62
.Lmain_bb63:
	smull x9, w25, w19
	asr x9, x9, #37
	add w9, w9, w9, lsr #31
	msub w9, w9, w23, w25
	cbz w9, .Lmain_bb64
.Lmain_bb65:
	add w25, w25, #1
	add x24, x24, #4
	b .Lmain_bb61
.Lmain_bb62:
	movz w0, #10
	bl putch
	adrp x9, size
	str w20, [x9, :lo12:size]
	ldr w9, [sp, #80]
	adrp x13, pos
	str w9, [x13, :lo12:pos]
	ldr w9, [sp, #84]
	adrp x12, bits
	adrp x11, out_num
	adrp x10, buf
	str w9, [x12, :lo12:bits]
	str w22, [x11, :lo12:out_num]
	str w21, [x10, :lo12:buf]
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #96
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb64:
	ldr w0, [x24]
	bl putch
	b .Lmain_bb65
.Lmain_bb69:
	mov w25, w6
	mov w26, w27
	b .Lmain_bb10
.Lmain_bb70:
	mov w25, w26
	mov w26, w27
	b .Lmain_bb10
.Lmain_bb75:
	mov w25, w5
	b .Lmain_bb16
.Lmain_bb76:
	mov w25, w27
	b .Lmain_bb16
.Lmain_bb77:
	mov w25, w2
	mov w8, w27
	b .Lmain_bb16
.Lmain_bb81:
	mov w4, w2
	b .Lmain_bb23
.Lmain_bb83:
	mov w8, w27
	b .Lmain_bb23
.Lmain_bb85:
	mov w26, w23
	stp w16, w15, [sp, #80]
	mov w21, w14
	b .Lmain_bb28
.Lmain_bb86:
	mov w24, w13
	stp w28, w8, [sp, #80]
	mov w22, w25
	mov w21, w27
	b .Lmain_bb28
	.size main, .-main
	.data
	.global size
	.p2align 2
size:
	.zero 4
	.global out_num
	.p2align 2
out_num:
	.zero 4
	.global pos
	.p2align 2
pos:
	.zero 4
	.global buf
	.p2align 2
buf:
	.zero 4
	.global bits
	.p2align 2
bits:
	.zero 4
	.bss
	.global data
	.p2align 4
data:
	.zero 400000
	.global out
	.p2align 4
out:
	.zero 400000
