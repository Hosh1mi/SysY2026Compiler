	.text
	.global pseudo_md5
	.p2align 2
pseudo_md5:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #912
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x26, x25, [sp, #48]
	stp x28, x27, [sp, #64]
	movz w28, #0
pseudo_md5_label_while_cond_1:
	cmp w28, #64
	b.lt pseudo_md5_label_while_body_2
pseudo_md5_label_while_end_3:
	mov x3, x0
	add x3, x3, w1, uxtw #2
	movz w10, #128
	str w10, [x3]
	add w3, w1, #1
	and w4, w3, #63
	cmp w4, #56
	b.ne pseudo_md5_label_while_body_23.preheader
pseudo_md5_label_while_end_24:
	mov x5, x0
	add x5, x5, w3, sxtw #2
	lsl w4, w1, #3
	str w4, [x5]
	add w4, w3, #1
	mov x6, x0
	add x6, x6, w4, sxtw #2
	movz w4, #1
pseudo_md5_label_while_body_26:
	cmp w4, #3
	str	wzr, [x6], #4
	b.ge pseudo_md5_label_while_end_27.from.label_while_body_26
	add	w4, w4, #1
	b pseudo_md5_label_while_body_26
pseudo_md5_label_while_end_27.from.label_while_body_26:
	add w10, w3, #4
	sub x17, x29, #568
	str w10, [x17]
	movz w10, #65532
	movk w10, #65535, lsl #16
	cmp w3, w10
	b.le .Lpseudo_md5_edge_0
	movz w10, #0
	movz w27, #21622
	movz w26, #56574
	movz w25, #43913
	movz w24, #8961
	movk w27, #4146, lsl #16
	movk w26, #39098, lsl #16
	movk w25, #61389, lsl #16
	movk w24, #26437, lsl #16
	str w10, [x29, #-8]
	b pseudo_md5_label_while_cond_31.preheader
.Lpseudo_md5_edge_0:
	movz w24, #8961
	movz w25, #43913
	movz w26, #56574
	movz w27, #21622
	movk w24, #26437, lsl #16
	movk w25, #61389, lsl #16
	movk w26, #39098, lsl #16
	movk w27, #4146, lsl #16
pseudo_md5_label_while_end_30:
	mov x3, x2
	str w24, [x3]
	add	x3, x2, #4
	str w25, [x3]
	add	x3, x2, #8
	str w26, [x3]
	add	x3, x2, #12
	str w27, [x3]
	b .Lpseudo_md5_epilogue
pseudo_md5_label_while_body_2:
	cmp w28, #16
	b.lt pseudo_md5_label_if_then_4
pseudo_md5_label_if_else_5:
	cmp w28, #32
	b.lt pseudo_md5_label_if_then_16
pseudo_md5_label_if_else_17:
	cmp w28, #48
	b.lt pseudo_md5_label_if_then_19
pseudo_md5_label_if_else_20:
	sub x3, x29, #304
	movz w10, #8772
	add x3, x3, w28, uxtw #2
	movk w10, #1065, lsl #16
	str w10, [x3]
	sub x3, x29, #560
	add x3, x3, w28, uxtw #2
	movz w10, #6
	str w10, [x3]
pseudo_md5_label_if_end_6:
	add w28, w28, #1
	b pseudo_md5_label_while_cond_1
pseudo_md5_label_if_then_4:
	and w4, w28, #3
	cbz w4, pseudo_md5_label_if_then_7
pseudo_md5_label_if_else_8:
	cmp w4, #1
	b.eq pseudo_md5_label_if_then_10
pseudo_md5_label_if_else_11:
	cmp w4, #2
	b.eq pseudo_md5_label_if_then_13
pseudo_md5_label_if_else_14:
	sub x3, x29, #304
	movz w10, #52974
	add x3, x3, w28, uxtw #2
	movk w10, #445, lsl #16
	str w10, [x3]
pseudo_md5_label_if_end_9:
	sub x3, x29, #560
	add x3, x3, w28, uxtw #2
	movz w10, #7
	str w10, [x3]
	b pseudo_md5_label_if_end_6
pseudo_md5_label_if_then_7:
	sub x3, x29, #304
	movz w10, #42104
	add x3, x3, w28, uxtw #2
	movk w10, #1898, lsl #16
	str w10, [x3]
	b pseudo_md5_label_if_end_9
pseudo_md5_label_if_then_10:
	sub x3, x29, #304
	movz w10, #46934
	add x3, x3, w28, uxtw #2
	movk w10, #2247, lsl #16
	str w10, [x3]
	b pseudo_md5_label_if_end_9
pseudo_md5_label_if_then_13:
	sub x3, x29, #304
	movz w10, #28891
	add x3, x3, w28, uxtw #2
	movk w10, #1056, lsl #16
	str w10, [x3]
	b pseudo_md5_label_if_end_9
pseudo_md5_label_if_then_16:
	sub x3, x29, #304
	movz w10, #9570
	add x3, x3, w28, uxtw #2
	movk w10, #1566, lsl #16
	str w10, [x3]
	sub x3, x29, #560
	add x3, x3, w28, uxtw #2
	movz w10, #5
	str w10, [x3]
	b pseudo_md5_label_if_end_6
pseudo_md5_label_if_then_19:
	sub x3, x29, #304
	movz w10, #24866
	add x3, x3, w28, uxtw #2
	movk w10, #3485, lsl #16
	str w10, [x3]
	sub x3, x29, #560
	add x3, x3, w28, uxtw #2
	movz w10, #4
	str w10, [x3]
	b pseudo_md5_label_if_end_6
pseudo_md5_label_while_body_23.preheader:
	mov x5, x0
	add x5, x5, w3, sxtw #2
pseudo_md5_label_while_body_23:
	add w3, w3, #1
	cmp w3, #0
	cneg w4, w3, mi
	and w4, w4, #63
	cneg w4, w4, mi
	cmp w4, #56
	str wzr, [x5]
	add x5, x5, #4
	b.eq	pseudo_md5_label_while_end_24
	b pseudo_md5_label_while_body_23
pseudo_md5_label_while_cond_31.preheader:
	ldr w11, [x29, #-8]
	mov x10, x0
	sub x17, x29, #648
	mov w23, wzr
	add x10, x10, w11, sxtw #2
	str x10, [x17]
	ldr x10, [x17]
	str x10, [x29, #-40]
pseudo_md5_label_365:
	cmp w23, #13
	b.lt pseudo_md5_label_369
	ldr x10, [x29, #-40]
	str x10, [x29, #-24]
pseudo_md5_label_while_cond_31:
	cmp w23, #16
	b.lt pseudo_md5_label_while_body_32
	movz w22, #0
	mov w21, w27
	mov w20, w26
	mov w19, w25
	mov w9, w24
pseudo_md5_label_while_cond_34:
	cmp w22, #64
	b.lt pseudo_md5_label_while_body_35
pseudo_md5_label_while_end_36:
	ldr w11, [x29, #-8]
	sub x17, x29, #680
	add w24, w24, w9
	add w25, w25, w19
	add w10, w11, #64
	str w10, [x17]
	ldr w12, [x17]
	sub x17, x29, #568
	ldr w13, [x17]
	add w26, w26, w20
	add w27, w27, w21
	cmp w12, w13
	b.ge	pseudo_md5_label_while_end_30
	sub x17, x29, #680
	ldr w10, [x17]
	str w10, [x29, #-8]
	b pseudo_md5_label_while_cond_31.preheader
pseudo_md5_label_while_body_32:
	ldr x10, [x29, #-24]
	sub x4, x29, #640
	add x4, x4, w23, uxtw #2
	sub x17, x29, #664
	ldr w3, [x10]
	add w23, w23, #1
	str w3, [x4]
	ldr x10, [x29, #-24]
	add x10, x10, #4
	str x10, [x17]
	ldr x10, [x17]
	str x10, [x29, #-24]
	b pseudo_md5_label_while_cond_31
pseudo_md5_label_while_body_35:
	cmp w22, #16
	b.lt pseudo_md5_label_if_then_37
pseudo_md5_label_if_else_38:
	cmp w22, #32
	b.lt pseudo_md5_label_if_then_40
pseudo_md5_label_if_else_41:
	cmp w22, #48
	b.lt pseudo_md5_label_if_then_43
pseudo_md5_label_if_else_44:
	lsl w10, w22, #3
	sub w3, w10, w22
	movz w10, #65535
	movk w10, #65535, lsl #16
	sub w4, w10, w21
	add w5, w19, w4
	cmp w3, #0
	cneg w7, w3, mi
	sub w3, w19, w5
	add w3, w3, w4
	sub w3, w3, w5
	add w4, w3, w5
	sub w3, w3, w4
	add w3, w3, w5
	sub w5, w3, w4
	add w4, w20, w5
	sub w3, w20, w4
	add w3, w3, w5
	sub w10, w3, w4
	sub x17, x29, #792
	str w10, [x17]
	ldr w5, [x17]
	and w7, w7, #15
	cneg w7, w7, mi
pseudo_md5_label_if_end_39:
	sub x3, x29, #304
	add x3, x3, w22, uxtw #2
	ldr w4, [x3]
	sub x3, x29, #640
	add x3, x3, w7, sxtw #2
	ldr w6, [x3]
	sub x3, x29, #560
	add x3, x3, w22, uxtw #2
	ldr w3, [x3]
	add w5, w9, w5
	add w4, w5, w4
	add w9, w4, w6
	cmp w3, #1
	b.gt pseudo_md5_label_while_body_47.preheader
pseudo_md5_label_while_end_48:
	add w3, w9, w19
	add w22, w22, #1
	mov w9, w21
	mov w21, w20
	mov w20, w19
	mov w19, w3
	b pseudo_md5_label_while_cond_34
pseudo_md5_label_if_then_37:
	add w10, w19, w20
	sub x17, x29, #696
	str w10, [x17]
	movz w10, #65535
	movk w10, #65535, lsl #16
	sub x17, x29, #696
	sub w3, w10, w19
	ldr w10, [x17]
	ldr w11, [x17]
	add w3, w3, w21
	sub x17, x29, #712
	add w4, w10, w3
	sub w10, w11, w4
	str w10, [x17]
	ldr w10, [x17]
	sub x17, x29, #728
	mov w7, w22
	add w3, w10, w3
	sub w10, w3, w4
	str w10, [x17]
	ldr w10, [x17]
	sub x17, x29, #728
	ldr w11, [x17]
	sub x17, x29, #744
	add w3, w10, w4
	sub w10, w11, w3
	str w10, [x17]
	ldr w10, [x17]
	add w4, w10, w4
	sub w3, w4, w3
	mov w5, w3
	b pseudo_md5_label_if_end_39
pseudo_md5_label_if_then_40:
	movz w10, #65535
	movk w10, #65535, lsl #16
	sub w4, w10, w21
	add w3, w21, w19
	add w4, w4, w20
	add w5, w3, w4
	sub w3, w3, w5
	add w3, w3, w4
	sub w3, w3, w5
	add w4, w3, w5
	sub w3, w3, w4
	add w3, w3, w5
	sub w10, w3, w4
	sub x17, x29, #760
	str w10, [x17]
	movz w10, #5
	movz w11, #1
	madd	w3, w22, w10, w11
	sub x17, x29, #760
	cmp w3, #0
	cneg w5, w3, mi
	and w5, w5, #15
	cneg w5, w5, mi
	mov w7, w5
	ldr w5, [x17]
	b pseudo_md5_label_if_end_39
pseudo_md5_label_if_then_43:
	add w5, w22, w22, lsl #1
	add w4, w20, w21
	sub w3, w20, w4
	add w5, w5, #5
	cmp w5, #0
	add w3, w3, w21
	cneg w6, w5, mi
	sub w5, w3, w4
	add w4, w19, w5
	sub w3, w19, w4
	add w3, w3, w5
	sub w10, w3, w4
	sub x17, x29, #776
	str w10, [x17]
	ldr w5, [x17]
	and w6, w6, #15
	cneg w6, w6, mi
	mov w7, w6
	b pseudo_md5_label_if_end_39
pseudo_md5_label_while_body_47.preheader:
	sub w10, w3, #3
	sub x17, x29, #808
	cmp w3, #4
	str w10, [x17]
	b.gt .Lpseudo_md5_edge_3
	movz w7, #1
	b	pseudo_md5_label_while_body_47
.Lpseudo_md5_edge_3:
	movz w7, #1
	b pseudo_md5_label_343
pseudo_md5_label_while_body_47.preheader.1:
pseudo_md5_label_while_body_47:
	tst w9, w9
	and w6, w9, #1
	add w7, w7, #1
	cneg w6, w6, mi
	lsl w5, w9, #1
	cmp w7, w3
	add w9, w5, w6
	b.lt	pseudo_md5_label_while_body_47
pseudo_md5_label_while_end_48.from.label_while_body_47:
	add w9, w5, w6
	b pseudo_md5_label_while_end_48
pseudo_md5_label_363:
	cmp w7, w3
	b.lt	pseudo_md5_label_while_body_47.preheader.1
	b pseudo_md5_label_while_end_48.from.label_while_body_47
pseudo_md5_label_369:
	ldr x10, [x29, #-40]
	sub x4, x29, #640
	add x4, x4, w23, uxtw #2
	sub x5, x29, #640
	ldr w3, [x10]
	sub x17, x29, #824
	str w3, [x4]
	ldr x4, [x29, #-40]
	add w3, w23, #1
	add x5, x5, w3, uxtw #2
	add x4, x4, #4
	ldr	w3, [x4], #4
	str w3, [x5]
	add w3, w23, #2
	sub x5, x29, #640
	add x5, x5, w3, sxtw #2
	ldr w3, [x4]
	str w3, [x5]
	add w3, w23, #3
	add	x5, x4, #4
	sub x4, x29, #640
	add x4, x4, w3, sxtw #2
	ldr w3, [x5]
	add	x10, x5, #4
	add w23, w23, #4
	str w3, [x4]
	str x10, [x17]
	ldr x10, [x17]
	str x10, [x29, #-40]
	b pseudo_md5_label_365
pseudo_md5_label_343:
	tst w9, w9
	and w5, w9, #1
	cneg w5, w5, mi
	lsl w4, w9, #1
	add w4, w4, w5
	tst w4, w4
	and w5, w4, #1
	cneg w5, w5, mi
	add	w4, w5, w4, lsl #1
	sub x17, x29, #808
	ldr w10, [x17]
	tst w4, w4
	and w5, w4, #1
	cneg w5, w5, mi
	add	w4, w5, w4, lsl #1
	add w7, w7, #4
	tst w4, w4
	and w6, w4, #1
	cneg w6, w6, mi
	lsl w5, w4, #1
	cmp w7, w10
	add w9, w5, w6
	b.ge pseudo_md5_label_363
	b pseudo_md5_label_343
.Lpseudo_md5_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	add sp, sp, #912
	ldp x29, x30, [sp], #16
	ret
	.global main
	.p2align 2
main:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #96
	sub	x0, x29, #20
	mov x9, x0
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	str x23, [sp, #32]
	str wzr, [x9]
	add	x9, x0, #4
	str wzr, [x9]
	add	x9, x0, #8
	str wzr, [x9]
	add	x9, x0, #12
	str wzr, [x9]
	add	x9, x0, #16
	sub x23, x29, #20
	str wzr, [x9]
	add	x9, x23, #4
	str wzr, [x23]
	str wzr, [x9]
	add	x9, x23, #8
	str wzr, [x9]
	add	x9, x23, #12
	str wzr, [x9]
	add	x9, x23, #16
	str wzr, [x9]
	bl getint
	mov w22, w0
	bl getint
	mov w21, w0
	movz w0, #261
	bl _sysy_starttime
	sub x9, x29, #52
	sub x20, x29, #52
	add x9, x9, #4
	str wzr, [x20]
	str wzr, [x9]
	sub x9, x29, #52
	add x9, x9, #8
	str wzr, [x9]
	sub x9, x29, #52
	add x9, x9, #12
	str wzr, [x9]
	sub x9, x29, #52
	add x9, x9, #16
	adrp x19, buffer
	cmp w21, #0
	str wzr, [x9]
	add x19, x19, :lo12:buffer
	b.le	main_label_while_end_77
	b main_label_while_cond_78.preheader
main_label_while_end_77:
	movz w0, #284
	bl _sysy_stoptime
	movz w0, #5
	mov x1, x20
	bl putarray
	adrp x10, state
	str w22, [x10, :lo12:state]
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_cond_78.preheader:
	mov x5, x19
	movz w4, #0
main_label_68:
	movz w10, #31997
	cmp w4, w10
	b.lt main_label_73
main_label_while_cond_78:
	movz w10, #32000
	cmp w4, w10
	b.lt main_label_while_body_79
main_label_while_end_80:
	mov x0, x19
	movz w1, #32000
	mov x2, x23
	bl pseudo_md5
	movz w3, #0
main_label_while_cond_81:
	cmp w3, #5
	b.lt main_label_while_body_82
main_label_while_end_83:
	cmp w21, #1
	b.le	main_label_while_end_77
	sub	w21, w21, #1
	b main_label_while_cond_78.preheader
main_label_while_body_79:
	add w0, w22, w22, lsl #13
	asr w10, w0, #31
	bic w10, w10, w10, lsl #17
	add w9, w0, w10
	asr w9, w9, #17
	add w9, w0, w9
	add w22, w9, w9, lsl #5
	cmp w22, #0
	cneg w9, w22, mi
	and w9, w9, #255
	cneg w9, w9, mi
	str	w9, [x5], #4
	add w4, w4, #1
	b main_label_while_cond_78
main_label_while_body_82:
	sub x2, x29, #52
	sub x0, x29, #20
	add x2, x2, w3, uxtw #2
	add x0, x0, w3, uxtw #2
	ldr w9, [x2]
	ldr w1, [x0]
	add w3, w3, #1
	add w0, w9, w1
	sub w9, w9, w0
	add w9, w9, w1
	sub w9, w9, w0
	str w9, [x2]
	b main_label_while_cond_81
main_label_73:
	add w0, w22, w22, lsl #13
	asr w10, w0, #31
	bic w10, w10, w10, lsl #17
	add w9, w0, w10
	asr w9, w9, #17
	add w9, w0, w9
	add w0, w9, w9, lsl #5
	cmp w0, #0
	cneg w0, w0, mi
	and w0, w0, #255
	movz w10, #8225
	cneg w0, w0, mi
	movk w10, #4, lsl #16
	str w0, [x5]
	mul w0, w9, w10
	add	x1, x5, #4
	add w4, w4, #4
	asr w10, w0, #31
	bic w10, w10, w10, lsl #17
	add w9, w0, w10
	asr w9, w9, #17
	add w9, w0, w9
	add w0, w9, w9, lsl #5
	cmp w0, #0
	cneg w0, w0, mi
	and w0, w0, #255
	movz w10, #8225
	cneg w0, w0, mi
	movk w10, #4, lsl #16
	str	w0, [x1], #4
	mul w0, w9, w10
	asr w10, w0, #31
	bic w10, w10, w10, lsl #17
	add w9, w0, w10
	asr w9, w9, #17
	add w9, w0, w9
	add w0, w9, w9, lsl #5
	cmp w0, #0
	cneg w0, w0, mi
	and w0, w0, #255
	movz w10, #8225
	cneg w0, w0, mi
	movk w10, #4, lsl #16
	str w0, [x1]
	add	x0, x1, #4
	mul w1, w9, w10
	add	x5, x0, #4
	asr w10, w1, #31
	bic w10, w10, w10, lsl #17
	add w9, w1, w10
	asr w9, w9, #17
	add w9, w1, w9
	add w22, w9, w9, lsl #5
	cmp w22, #0
	cneg w9, w22, mi
	and w9, w9, #255
	cneg w9, w9, mi
	str w9, [x0]
	b main_label_68
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldr x23, [sp, #32]
	add sp, sp, #96
	ldp x29, x30, [sp], #16
	ret
	.data
	.global state
	.p2align 2
state:
	.word 19260817

	.global buffer
	.p2align 4
buffer:
	.zero 131072

