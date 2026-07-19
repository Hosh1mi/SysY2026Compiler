	.text
	.global main
	.p2align 2
main:
	sub sp, sp, #48
	stp x27, x21, [sp, #16]
	str x28, [sp, #32]
	movz w28, #65535
	movz w27, #65535
	stp x20, x19, [sp, #0]
	str x30, [sp, #40]
	movk w28, #16383, lsl #16
	movk w27, #32767, lsl #16
	bl getint
	mov w21, w0
	movz w0, #33
	bl _sysy_starttime
	cbz w21, main_label_while_end_9
main_label_while_body_8:
	bl getint
	mov w20, w0
	bl getint
	mov w19, w0
	bl getint
	movz w10, #15363
	movz w11, #26809
	movz w12, #19923
	movz w13, #26581
	movz w14, #0
	movz w15, #501
	cmp w20, w19
	movk w10, #35246, lsl #16
	movk w11, #297, lsl #16
	movk w12, #4194, lsl #16
	movk w13, #7051, lsl #16
	movk w14, #8192, lsl #16
	movk w15, #15232, lsl #16
	mov w2, w0
	b.ge .Lmain_edge_0
	movz w1, #0
	b main_label_14
.Lmain_edge_0:
	movz w1, #0
main_label_20:
	mov w0, w1
	bl putint
	movz w0, #10
	bl putch
	cmp w21, #1
	b.eq main_label_while_end_9
	sub	w21, w21, #1
	b main_label_while_body_8
main_label_while_end_9:
	movz w0, #42
	bl _sysy_stoptime
	mov w0, wzr
	b .Lmain_epilogue
main_label_14:
	sub w9, w27, w20
	cmp w20, w9
	csel w0, w9, w20, lt
	sub w9, w28, w0
	cmp w0, w9
	csel w0, w9, w0, lt
	sub w9, w14, w0
	cmp w0, w9
	csel w0, w9, w0, lt
	add w9, w0, w0, lsl #1
	smull x16, w9, w12
	add w20, w20, w2
	cmp w20, w19
	asr x16, x16, #38
	add w9, w16, w9, lsr #31
	movz w16, #1001
	madd	w9, w9, w16, w0
	smull x16, w9, w13
	asr x16, x16, #53
	add w16, w16, w9, lsr #31
	msub w9, w16, w11, w9
	add w9, w1, w9
	add w9, w9, #1
	smull x16, w9, w10
	asr x16, x16, #32
	add w16, w16, w9
	asr w16, w16, #29
	add w16, w16, w9, lsr #31
	msub w1, w16, w15, w9
	b.ge	main_label_20
	b main_label_14
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x27, x21, [sp, #16]
	ldr x28, [sp, #32]
	ldr x30, [sp, #40]
	add sp, sp, #48
	ret
