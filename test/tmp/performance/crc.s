	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	stp x19, x20, [sp]
	adrp x9, a
	stp x21, x22, [sp, #16]
	add x19, x9, :lo12:a
	stp x23, x24, [sp, #32]
	movz w22, #0
	stp x25, x26, [sp, #48]
	movz w21, #1
	stp x27, x28, [sp, #64]
	movz w20, #32
	mov x0, x19
	bl getarray
	movz w9, #58769
	movk w9, #293, lsl #16
	dup v20.4s, w9
	adrp x9, crc32table
	add x9, x9, :lo12:crc32table
	mov w23, w0
	mov x11, x9
	mov w24, w22
.Lmain_bb1:
	cmp w24, #248
	b.gt .Lmain_bb2
.Lmain_bb34:
	add w9, w24, #4
	dup v17.4s, w9
	adrp x10, .LCPI_main_0
	dup v19.4s, w24
	adrp x9, .LCPI_main_0
	ldr q18, [x10, :lo12:.LCPI_main_0]
	ldr q16, [x9, :lo12:.LCPI_main_0]
	add v18.4s, v19.4s, v18.4s
	add v16.4s, v17.4s, v16.4s
	add v17.4s, v18.4s, v20.4s
	add v16.4s, v16.4s, v20.4s
	stp q17, q16, [x11]
	add w24, w24, #8
	add x11, x11, #32
	b .Lmain_bb1
.Lmain_bb2:
	adrp x9, crc32table
	add x9, x9, :lo12:crc32table
	movz w17, #58769
	movz w15, #58770
	movz w14, #58771
	movz w11, #58772
	add x25, x9, w24, sxtw #2
	movk w17, #293, lsl #16
	movk w15, #293, lsl #16
	movk w14, #293, lsl #16
	movk w11, #293, lsl #16
.Lmain_bb3:
	cmp w24, #253
	b.ge .Lmain_bb35
.Lmain_bb33:
	add x9, x25, #4
	add x9, x9, #4
	add x12, x9, #4
	add w16, w24, w17
	add w10, w24, w15
	add w13, w24, w14
	add w9, w24, w11
	stp w16, w10, [x25]
	stp w13, w9, [x25, #8]
	add w24, w24, #4
	add x25, x12, #4
	b .Lmain_bb3
.Lmain_bb4:
	cmp w10, #256
	b.ge .Lmain_bb6
.Lmain_bb5:
	add w9, w10, w11
	str w9, [x12], #4
	add w10, w10, #1
	b .Lmain_bb4
.Lmain_bb6:
	movz w0, #68
	bl _sysy_starttime
	cmp w23, #0
	cset w14, ne
	cbz w14, .Lmain_bb32
.Lmain_bb36:
	movz w12, #58769
	movz w10, #23095
	movk w12, #293, lsl #16
	movk w10, #14271, lsl #16
.Lmain_bb7:
	mov x24, x19
	sub w13, w14, #1
	mov w23, w22
	mov w26, w22
	orr w17, wzr, #0x80000001
	movz w16, #0
	movz w15, #255
.Lmain_bb8:
	cmp w23, w13
	cset w11, lt
	cmp w14, w17
	cset w9, ge
	and w9, w9, w11
	cbz w9, .Lmain_bb22
.Lmain_bb9:
	ldr w27, [x24]
	cmp w26, #0
	csel w9, w16, w26, lt
	and w28, w9, w15
	orr w9, w28, w27
	cmp w9, #0
	b.ge .Lmain_bb11
.Lmain_bb38:
	mov w6, w21
	mov w25, w22
	mov w7, w20
	movz w8, #1
.Lmain_bb10:
	cmp w27, #0
	and w9, w27, #1
	and w11, w28, w8
	cneg w9, w9, mi
	cmp w11, w9
	add w11, w25, w6
	add w9, w27, w27, lsr #31
	csel w25, w11, w25, ne
	asr w27, w9, #1
	asr w28, w28, #1
	lsl w6, w6, #1
	sub w9, w7, #1
	cmp w7, #1
	b.eq .Lmain_bb12
.Lmain_bb39:
	mov w7, w9
	b .Lmain_bb10
.Lmain_bb11:
	eor w25, w28, w27
.Lmain_bb12:
	adrp x9, crc32table
	add x9, x9, :lo12:crc32table
	ldr w27, [x9, w25, sxtw #2]
	asr w9, w26, #31
	lsr w9, w9, #24
	add w9, w26, w9
	asr w26, w9, #8
	orr w9, w26, w27
	cmp w9, #0
	b.ge .Lmain_bb14
.Lmain_bb41:
	mov w8, w21
	mov w7, w22
	mov w28, w20
.Lmain_bb13:
	cmp w26, #0
	and w9, w26, #1
	cneg w11, w9, mi
	cmp w27, #0
	and w9, w27, #1
	cneg w9, w9, mi
	cmp w11, w9
	add w25, w7, w8
	add w9, w27, w27, lsr #31
	add w11, w26, w26, lsr #31
	csel w7, w25, w7, ne
	asr w27, w9, #1
	asr w26, w11, #1
	lsl w8, w8, #1
	sub w9, w28, #1
	cmp w28, #1
	b.eq .Lmain_bb43
.Lmain_bb42:
	mov w28, w9
	b .Lmain_bb13
.Lmain_bb14:
	eor w27, w26, w27
.Lmain_bb15:
	ldr w26, [x24, #4]
	cmp w27, #0
	csel w9, w16, w27, lt
	and w28, w9, w15
	orr w9, w28, w26
	add x24, x24, #4
	cmp w9, #0
	b.ge .Lmain_bb17
.Lmain_bb44:
	mov w6, w21
	mov w25, w22
	mov w7, w20
	movz w8, #1
.Lmain_bb16:
	cmp w26, #0
	and w9, w26, #1
	and w11, w28, w8
	cneg w9, w9, mi
	cmp w11, w9
	add w11, w25, w6
	add w9, w26, w26, lsr #31
	csel w25, w11, w25, ne
	asr w26, w9, #1
	asr w28, w28, #1
	lsl w6, w6, #1
	sub w9, w7, #1
	cmp w7, #1
	b.eq .Lmain_bb18
.Lmain_bb45:
	mov w7, w9
	b .Lmain_bb16
.Lmain_bb17:
	eor w25, w28, w26
.Lmain_bb18:
	adrp x9, crc32table
	add x9, x9, :lo12:crc32table
	ldr w26, [x9, w25, sxtw #2]
	asr w9, w27, #31
	lsr w9, w9, #24
	add w9, w27, w9
	asr w27, w9, #8
	orr w9, w27, w26
	cmp w9, #0
	b.ge .Lmain_bb20
.Lmain_bb47:
	mov w8, w21
	mov w7, w22
	mov w28, w20
.Lmain_bb19:
	cmp w27, #0
	and w9, w27, #1
	cneg w11, w9, mi
	cmp w26, #0
	and w9, w26, #1
	cneg w9, w9, mi
	cmp w11, w9
	add w25, w7, w8
	add w9, w26, w26, lsr #31
	add w11, w27, w27, lsr #31
	csel w7, w25, w7, ne
	asr w26, w9, #1
	asr w27, w11, #1
	lsl w8, w8, #1
	sub w9, w28, #1
	cmp w28, #1
	b.eq .Lmain_bb21
.Lmain_bb48:
	mov w28, w9
	b .Lmain_bb19
.Lmain_bb20:
	eor w7, w27, w26
.Lmain_bb21:
	add w23, w23, #2
	add x24, x24, #4
	mov w26, w7
	b .Lmain_bb8
.Lmain_bb22:
	cmp w23, w14
	b.ge .Lmain_bb57
.Lmain_bb50:
	mov w27, w26
	movz w17, #0
	movz w16, #255
.Lmain_bb23:
	cmp w23, w14
	b.ge .Lmain_bb31
.Lmain_bb24:
	ldr w25, [x24]
	cmp w27, #0
	csel w9, w17, w27, lt
	and w26, w9, w16
	orr w9, w26, w25
	cmp w9, #0
	b.ge .Lmain_bb26
.Lmain_bb51:
	mov w7, w21
	mov w15, w22
	mov w8, w20
	movz w28, #1
.Lmain_bb25:
	cmp w25, #0
	and w9, w25, #1
	and w11, w26, w28
	cneg w9, w9, mi
	cmp w11, w9
	add w11, w15, w7
	add w9, w25, w25, lsr #31
	csel w15, w11, w15, ne
	asr w25, w9, #1
	asr w26, w26, #1
	lsl w7, w7, #1
	sub w9, w8, #1
	cmp w8, #1
	b.eq .Lmain_bb27
.Lmain_bb52:
	mov w8, w9
	b .Lmain_bb25
.Lmain_bb26:
	eor w15, w26, w25
.Lmain_bb27:
	adrp x9, crc32table
	add x9, x9, :lo12:crc32table
	ldr w25, [x9, w15, sxtw #2]
	asr w9, w27, #31
	lsr w9, w9, #24
	add w9, w27, w9
	asr w26, w9, #8
	orr w9, w26, w25
	cmp w9, #0
	b.ge .Lmain_bb29
.Lmain_bb54:
	mov w8, w21
	mov w28, w22
	mov w27, w20
.Lmain_bb28:
	cmp w26, #0
	and w9, w26, #1
	cneg w11, w9, mi
	cmp w25, #0
	and w9, w25, #1
	cneg w9, w9, mi
	cmp w11, w9
	add w15, w28, w8
	add w9, w25, w25, lsr #31
	add w11, w26, w26, lsr #31
	csel w28, w15, w28, ne
	asr w25, w9, #1
	asr w26, w11, #1
	lsl w8, w8, #1
	sub w9, w27, #1
	cmp w27, #1
	b.eq .Lmain_bb30
.Lmain_bb55:
	mov w27, w9
	b .Lmain_bb28
.Lmain_bb29:
	eor w28, w26, w25
.Lmain_bb30:
	add w23, w23, #1
	add x24, x24, #4
	mov w27, w28
	b .Lmain_bb23
.Lmain_bb31:
	smull x9, w27, w10
	asr x9, x9, #54
	add w9, w9, w9, lsr #31
	msub w9, w9, w12, w27
	cmp w14, #1
	b.eq .Lmain_bb60
.Lmain_bb37:
	mov w14, w13
	b .Lmain_bb7
.Lmain_bb32:
	movz w0, #73
	bl _sysy_stoptime
	mov w0, w22
	bl putint
	movz w0, #10
	bl putch
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb35:
	movz w11, #58769
	mov x12, x25
	mov w10, w24
	movk w11, #293, lsl #16
	b .Lmain_bb4
.Lmain_bb43:
	mov w27, w7
	b .Lmain_bb15
.Lmain_bb57:
	mov w27, w26
	b .Lmain_bb31
.Lmain_bb60:
	mov w22, w9
	b .Lmain_bb32
	.size main, .-main
	.section .rodata
	.p2align 4
.LCPI_main_0:
	.word 0x0
	.word 0x1
	.word 0x2
	.word 0x3
	.text
	.bss
	.global crc32table
	.p2align 4
crc32table:
	.zero 1024
	.global a
	.p2align 4
a:
	.zero 400080
