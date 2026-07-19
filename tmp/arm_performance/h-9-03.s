	.text
	.global main
	.p2align 2
main:
	sub sp, sp, #48
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	str x30, [sp, #32]
	bl getint
	mov w21, w0
	bl getint
	mov w20, w0
	movz w0, #52
	bl _sysy_starttime
	movz w10, #57607
	movz w11, #36553
	movz w12, #50349
	movk w10, #1525, lsl #16
	movk w11, #5497, lsl #16
	movk w12, #26824, lsl #16
	movz w19, #0
	movz w22, #0
main_label_while_cond_17:
	cmp w19, w21
	b.lt main_label_while_body_18
main_label_while_end_19:
	movz w0, #76
	bl _sysy_stoptime
	adrp x13, seed
	str w20, [x13, :lo12:seed]
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_body_18:
	tbz	w19, #0, main_label_if_then_20
main_label_if_else_21:
	movz w13, #57186
	movk w13, #304, lsl #16
	movz w14, #23333
	madd	w9, w20, w13, w14
	movz w14, #10007
	smull x13, w9, w11
	asr x13, x13, #55
	add w13, w13, w9, lsr #31
	msub w0, w13, w10, w9
	add w9, w0, w10
	cmp w0, #0
	csel w20, w9, w0, lt
	smull x13, w20, w12
	asr x13, x13, #44
	add w13, w13, w20, lsr #31
	msub w9, w13, w14, w20
	add w4, w22, w9
	tbz	w4, #0, .Lmain_edge_0
	mov w1, w4
	movz w2, #0
	b main_label_27.preheader
.Lmain_edge_0:
	movz w2, #0
	mov w1, w4
	b main_label_49
main_label_27.preheader:
	cmp w1, #9
	b.lt	main_label_43
	movz w0, #3
	b main_label_33.preheader
main_label_43:
	add w9, w2, #1
	cmp w1, #2
	csel w3, w9, w2, gt
main_label_if_end_22:
	add w9, w4, w3
	cmp w9, #0
	cneg w22, w9, mi
	and w22, w22, #255
	cneg w22, w22, mi
	mov w0, w22
	bl putint
	movz w0, #10
	bl putch
	movz w10, #57607
	movz w11, #36553
	movz w12, #50349
	movk w10, #1525, lsl #16
	movk w11, #5497, lsl #16
	movk w12, #26824, lsl #16
	add w19, w19, #1
	b main_label_while_cond_17
main_label_if_then_20:
	movz w13, #57186
	movk w13, #304, lsl #16
	movz w14, #23333
	madd	w9, w20, w13, w14
	smull x13, w9, w11
	asr x13, x13, #55
	add w13, w13, w9, lsr #31
	msub w0, w13, w10, w9
	add w9, w0, w10
	cmp w0, #0
	csel w20, w9, w0, lt
	smull x13, w20, w12
	asr x13, x13, #44
	add w9, w13, w20, lsr #31
	sub w4, w22, w9
	tbz	w4, #0, .Lmain_edge_2
	mov w1, w4
	movz w2, #0
	b main_label_72.preheader
.Lmain_edge_2:
	movz w2, #0
	mov w1, w4
	b main_label_94
main_label_72.preheader:
	cmp w1, #9
	b.lt	main_label_88
	movz w0, #3
	b main_label_78.preheader
main_label_88:
	add w9, w2, #1
	cmp w1, #2
	csel w3, w9, w2, gt
	b main_label_if_end_22
main_label_49:
	asr w1, w1, #1
	add w2, w2, #1
	tbz	w1, #0, main_label_49
	b main_label_27.preheader
main_label_33.preheader:
	sdiv w13, w1, w0
	msub w9, w13, w0, w1
	cbz	w9, main_label_38
main_label_41:
	add w0, w0, #2
	mul w9, w0, w0
	cmp w9, w1
	b.gt	main_label_43
	b main_label_33.preheader
main_label_38:
	sdiv w1, w1, w0
	add w2, w2, #1
	sdiv w13, w1, w0
	msub w9, w13, w0, w1
	cbz	w9, main_label_38
	b main_label_41
main_label_94:
	asr w1, w1, #1
	add w2, w2, #1
	tbz	w1, #0, main_label_94
	b main_label_72.preheader
main_label_78.preheader:
	sdiv w13, w1, w0
	msub w9, w13, w0, w1
	cbz	w9, main_label_83
main_label_86:
	add w0, w0, #2
	mul w9, w0, w0
	cmp w9, w1
	b.gt	main_label_88
	b main_label_78.preheader
main_label_83:
	sdiv w1, w1, w0
	add w2, w2, #1
	sdiv w13, w1, w0
	msub w9, w13, w0, w1
	cbz	w9, main_label_83
	b main_label_86
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldr x30, [sp, #32]
	add sp, sp, #48
	ret
	.data
	.global seed
	.p2align 2
seed:
	.word 0

