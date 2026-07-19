	.text
	.global read_bits
	.p2align 2
read_bits:
	sub sp, sp, #16
	adrp x10, bits
	str x19, [sp, #0]
	ldr w19, [x10, :lo12:bits]
	adrp x10, buf
	ldr w9, [x10, :lo12:buf]
	adrp x10, pos
	ldr w7, [x10, :lo12:pos]
	cmp w19, w0
	b.lt read_bits_label_sink_load_0
read_bits_label_while_end_52:
	movz w10, #1
	lsl w6, w10, w0
	movz w10, #8
	sub w5, w10, w0
	cmp w5, #8
	movz w10, #1
	csel w3, w6, w10, lo
	cmp w9, #0
	sub w2, w3, #1
	csel w1, wzr, w9, lt
	cmp w3, #1
	sdiv w3, w9, w6
	csel w2, wzr, w2, lt
	cmp w5, #8
	and w1, w1, w2
	adrp x10, bits
	csel w2, w3, w9, lo
	sub w3, w19, w0
	str w3, [x10, :lo12:bits]
	adrp x10, buf
	str w2, [x10, :lo12:buf]
	adrp x10, pos
	str w7, [x10, :lo12:pos]
	mov w0, w1
	b .Lread_bits_epilogue
read_bits_label_while_body_51:
	ldr w3, [x4]
	movz w10, #8
	sub w2, w10, w19
	cmp w2, #8
	lsl w1, w3, w19
	csel w1, w1, w3, lo
	cmp w9, #0
	csel w2, wzr, w9, lt
	cmp w1, #0
	add w19, w19, #8
	csel w1, wzr, w1, lt
	cmp w19, w0
	orr w9, w2, w1
	add w7, w7, #1
	add x4, x4, #4
	b.ge	read_bits_label_while_end_52
read_bits_label_and_53:
	cmp w7, w5
	b.lt read_bits_label_while_body_51
	b read_bits_label_while_end_52
read_bits_label_sink_load_0:
	adrp x10, size
	ldr w5, [x10, :lo12:size]
read_bits_label_and_53.preheader:
	adrp x4, data
	add x4, x4, :lo12:data
	add x4, x4, w7, sxtw #2
	b read_bits_label_and_53
.Lread_bits_epilogue:
	ldr x19, [sp, #0]
	add sp, sp, #16
	ret
	.global main
	.p2align 2
main:
	sub sp, sp, #80
	adrp x9, data
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	str x30, [sp, #64]
	add	x0, x9, :lo12:data
	bl getarray
	adrp x13, size
	str	w0, [x13, :lo12:size]
	movz w0, #153
	bl _sysy_starttime
	adrp x13, size
	ldr w26, [x13, :lo12:size]
	movz w25, #2000
main_label_while_body_86:
	adrp x13, pos
	str wzr, [x13, :lo12:pos]
	adrp x13, bits
	str wzr, [x13, :lo12:bits]
	movz w0, #1
	bl read_bits
	mov w24, w0
	movz w0, #2
	bl read_bits
	mov w23, w0
main_label_53:
	adrp x13, bits
	ldr w9, [x13, :lo12:bits]
	cmp w9, #0
	b.gt main_label_71
	movz w22, #0
main_label_56:
	movz w0, #5
	bl read_bits
	cmp w0, #0
	b.gt main_label_60
main_label_66:
	adrp x13, pos
	ldr w9, [x13, :lo12:pos]
	cmp w9, w26
	b.ge main_label_70
main_label_56.backedge:
	b main_label_56
main_label_while_end_87.from.label_70:
	movz w0, #162
	bl _sysy_stoptime
	mov w0, w24
	bl putint
	movz w0, #32
	bl putch
	mov w0, w23
	bl putint
	movz w0, #10
	bl putch
	movz w11, #34464
	movz w10, #34079
	movz w12, #26215
	cmp w22, #1000
	movk w11, #1, lsl #16
	movk w10, #20971, lsl #16
	movk w12, #26214, lsl #16
	b.le main_label_while_end_87.unsw.t
main_label_while_end_87.unsw.f:
	cmp w22, #1000
	b.gt main_label_while_end_87.unsw.f.unsw.t
main_label_while_end_87.unsw.f.unsw.f:
	movz w13, #10000
	cmp w22, w13
	b.gt main_label_while_end_87.unsw.f.unsw.f.unsw.t
main_label_while_end_87.unsw.f.unsw.f.unsw.f:
	adrp x20, out
	add x20, x20, :lo12:out
	movz w19, #0
main_label_while_cond_88.unsw.unsw.unsw:
	cmp w19, w22
	b.lt main_label_if_then_91.unsw.unsw.unsw
main_label_while_end_90:
	movz w0, #10
	bl putch
	adrp x13, out_num
	str w22, [x13, :lo12:out_num]
	mov w0, wzr
	b .Lmain_epilogue
main_label_if_then_91:
	ldr w9, [x20]
	mov w0, w9
	bl putch
	add w19, w19, #1
	add x20, x20, #4
main_label_while_cond_88:
	cmp w19, w22
	b.lt main_label_if_then_91
	b main_label_while_end_90
main_label_60:
	cmp w0, #80
	b.le main_label_62
	b main_label_66
main_label_62:
	adrp x9, out
	add x9, x9, :lo12:out
	add x9, x9, w22, sxtw #2
	add w0, w0, #64
	str w0, [x9]
	add w22, w22, #1
	b	main_label_56
main_label_70:
	cmp w25, #1
	sub w1, w25, #1
	b.le main_label_while_end_87.from.label_70
	mov w25, w1
	b main_label_while_body_86
main_label_71:
	movz w0, #1
	bl read_bits
	b main_label_53
main_label_if_then_91.unsw:
	smull x13, w19, w12
	movz w14, #5
	asr x13, x13, #33
	add w13, w13, w19, lsr #31
	msub w9, w13, w14, w19
	cbz w9, main_label_29.unsw
main_label_50.unsw:
	add w19, w19, #1
	add x20, x20, #4
main_label_while_cond_88.unsw:
	cmp w19, w22
	b.lt main_label_if_then_91.unsw
	b main_label_while_end_90
main_label_29.unsw:
	ldr w9, [x20]
	mov w0, w9
	bl putch
	movz w12, #26215
	movk w12, #26214, lsl #16
	b main_label_50.unsw
main_label_while_end_87.unsw.t:
	adrp x20, out
	add x20, x20, :lo12:out
	movz w19, #0
	b main_label_while_cond_88
main_label_if_then_91.unsw.unsw:
	smull x13, w19, w10
	movz w14, #50
	asr x13, x13, #36
	add w13, w13, w19, lsr #31
	msub w9, w13, w14, w19
	cbz w9, main_label_41.unsw.unsw
main_label_50.unsw.unsw:
	add w19, w19, #1
	add x20, x20, #4
main_label_while_cond_88.unsw.unsw:
	cmp w19, w22
	b.lt main_label_if_then_91.unsw.unsw
	b main_label_while_end_90
main_label_41.unsw.unsw:
	ldr w9, [x20]
	mov w0, w9
	bl putch
	movz w10, #34079
	movk w10, #20971, lsl #16
	b main_label_50.unsw.unsw
main_label_while_end_87.unsw.f.unsw.t:
	movz w13, #10000
	cmp w22, w13
	b.le main_label_while_end_87.unsw.f.unsw.t.unsw.t
main_label_while_end_87.unsw.f.unsw.t.unsw.f:
	movz w13, #10000
	cmp w22, w13
	b.gt main_label_while_end_87.unsw.f.unsw.t.unsw.f.unsw.t
main_label_while_end_87.unsw.f.unsw.t.unsw.f.unsw.f:
	adrp x20, out
	add x20, x20, :lo12:out
	movz w19, #0
main_label_while_cond_88.unsw.unsw.1.unsw:
	cmp w19, w22
	b.lt main_label_if_then_91.unsw.unsw.1.unsw
	b main_label_while_end_90
main_label_if_then_91.unsw.unsw.1.unsw:
	smull x13, w19, w10
	movz w14, #100
	asr x13, x13, #37
	add w13, w13, w19, lsr #31
	msub w9, w13, w14, w19
	cbz w9, main_label_47.unsw.unsw.1.unsw
main_label_50.unsw.unsw.1.unsw:
	add w19, w19, #1
	add x20, x20, #4
	b main_label_while_cond_88.unsw.unsw.1.unsw
main_label_if_then_91.unsw.unsw.1:
	cmp w22, w11
	b.le main_label_38.unsw.unsw.1
main_label_44.unsw.unsw.1:
	smull x13, w19, w10
	movz w14, #100
	asr x13, x13, #37
	add w13, w13, w19, lsr #31
	msub w9, w13, w14, w19
	cbz w9, main_label_47.unsw.unsw.1
main_label_50.unsw.unsw.1:
	add w19, w19, #1
	add x21, x21, #4
	add x20, x20, #4
main_label_while_cond_88.unsw.unsw.1:
	cmp w19, w22
	b.lt main_label_if_then_91.unsw.unsw.1
	b main_label_while_end_90
main_label_38.unsw.unsw.1:
	smull x13, w19, w10
	movz w14, #50
	asr x13, x13, #36
	add w13, w13, w19, lsr #31
	msub w9, w13, w14, w19
	cbz w9, main_label_41.unsw.unsw.1
	b main_label_50.unsw.unsw.1
main_label_41.unsw.unsw.1:
	ldr w9, [x21]
	mov w0, w9
	bl putch
	movz w11, #34464
	movz w10, #34079
	movk w11, #1, lsl #16
	movk w10, #20971, lsl #16
	b main_label_50.unsw.unsw.1
main_label_47.unsw.unsw.1:
	ldr w9, [x20]
	mov w0, w9
	bl putch
	movz w11, #34464
	movz w10, #34079
	movk w11, #1, lsl #16
	movk w10, #20971, lsl #16
	b main_label_50.unsw.unsw.1
main_label_while_end_87.unsw.f.unsw.t.unsw.t:
	adrp x20, out
	add x20, x20, :lo12:out
	movz w19, #0
	b main_label_while_cond_88.unsw
main_label_if_then_91.unsw.unsw.unsw:
	smull x13, w19, w10
	movz w14, #100
	asr x13, x13, #37
	add w13, w13, w19, lsr #31
	msub w9, w13, w14, w19
	cbz w9, main_label_47.unsw.unsw.unsw
main_label_50.unsw.unsw.unsw:
	add w19, w19, #1
	add x20, x20, #4
	b main_label_while_cond_88.unsw.unsw.unsw
main_label_47.unsw.unsw.unsw:
	ldr w9, [x20]
	mov w0, w9
	bl putch
	movz w10, #34079
	movk w10, #20971, lsl #16
	b main_label_50.unsw.unsw.unsw
main_label_while_end_87.unsw.f.unsw.f.unsw.t:
	cmp w22, w11
	b.le main_label_while_end_87.unsw.f.unsw.f.unsw.t.unsw.t
main_label_while_end_87.unsw.f.unsw.f.unsw.t.unsw.f:
	adrp x20, out
	add x20, x20, :lo12:out
	movz w19, #0
main_label_while_cond_88.unsw.unsw.unsw.1:
	cmp w19, w22
	b.lt main_label_if_then_91.unsw.unsw.unsw.1
	b main_label_while_end_90
main_label_if_then_91.unsw.unsw.unsw.1:
	smull x13, w19, w10
	movz w14, #100
	asr x13, x13, #37
	add w13, w13, w19, lsr #31
	msub w9, w13, w14, w19
	cbz w9, main_label_47.unsw.unsw.unsw.1
main_label_50.unsw.unsw.unsw.1:
	add w19, w19, #1
	add x20, x20, #4
	b main_label_while_cond_88.unsw.unsw.unsw.1
main_label_47.unsw.unsw.unsw.1:
	ldr w9, [x20]
	mov w0, w9
	bl putch
	movz w10, #34079
	movk w10, #20971, lsl #16
	b main_label_50.unsw.unsw.unsw.1
main_label_while_end_87.unsw.f.unsw.f.unsw.t.unsw.t:
	adrp x20, out
	add x20, x20, :lo12:out
	movz w19, #0
	b main_label_while_cond_88.unsw.unsw
main_label_47.unsw.unsw.1.unsw:
	ldr w9, [x20]
	mov w0, w9
	bl putch
	movz w10, #34079
	movk w10, #20971, lsl #16
	b main_label_50.unsw.unsw.1.unsw
main_label_while_end_87.unsw.f.unsw.t.unsw.f.unsw.t:
	adrp x21, out
	add x21, x21, :lo12:out
	mov x20, x21
	movz w19, #0
	b main_label_while_cond_88.unsw.unsw.1
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldr x30, [sp, #64]
	add sp, sp, #80
	ret
	.data
	.global size
	.p2align 2
size:
	.word 0

	.global out_num
	.p2align 2
out_num:
	.word 0

	.global pos
	.p2align 2
pos:
	.word 0

	.global buf
	.p2align 2
buf:
	.word 0

	.global bits
	.p2align 2
bits:
	.word 0

	.bss
	.global data
	.p2align 4
data:
	.zero 400000

	.global out
	.p2align 4
out:
	.zero 400000

