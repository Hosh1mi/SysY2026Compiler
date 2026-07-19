	.text
	.global main
	.p2align 2
main:
	sub sp, sp, #32
	stp x20, x19, [sp, #0]
	str x30, [sp, #16]
	bl getint
	mov w20, w0
	movz w0, #23
	bl _sysy_starttime
	movz w10, #51719
	movz w11, #12185
	cmp w20, #1
	movk w10, #15258, lsl #16
	movk w11, #17592, lsl #16
	b.lt .Lmain_edge_0
	movz w3, #1
	movz w19, #0
	b main_label_11.preheader
.Lmain_edge_0:
	movz w19, #0
main_label_while_end_13:
	movz w0, #28
	bl _sysy_stoptime
	mov w0, w19
	bl putint
	adrp x12, lim
	str w20, [x12, :lo12:lim]
	mov w0, wzr
	b .Lmain_epilogue
main_label_19:
	asr w1, w1, #1
main_label_11.backedge:
	cmp w1, #1
	add w2, w2, #1
	b.eq	main_label_36
main_label_16:
	tbz	w1, #0, main_label_19
main_label_22:
	add w0, w1, w1, lsl #1
	cmp w0, w20
	b.lt main_label_26
	movz w2, #7
main_label_36:
	add w9, w19, w2
	smull x12, w9, w11
	cmp w3, w20
	asr x12, x12, #60
	add w12, w12, w9, lsr #31
	msub w19, w12, w10, w9
	b.ge	main_label_while_end_13
	add	w3, w3, #1
main_label_11.preheader:
	cmp w3, #1
	b.eq .Lmain_edge_2
	movz w2, #0
	mov w1, w3
	b main_label_16
.Lmain_edge_2:
	movz w2, #0
	b main_label_36
main_label_26:
	add w1, w0, #1
	b main_label_11.backedge
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldr x30, [sp, #16]
	add sp, sp, #32
	ret
	.data
	.global lim
	.p2align 2
lim:
	.word 0

