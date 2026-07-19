	.text
	.global radixSort
	.p2align 2
radixSort:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #352
	stp x26, x25, [sp, #48]
	mov w25, w0
	sub	x0, x29, #80
	mov x9, x0
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	stp x28, x27, [sp, #64]
	str wzr, [x9]
	add	x9, x0, #4
	str wzr, [x9]
	add	x9, x0, #8
	str wzr, [x9]
	add	x9, x0, #12
	str wzr, [x9]
	add	x9, x0, #16
	str wzr, [x9]
	add	x9, x0, #20
	str wzr, [x9]
	add	x9, x0, #24
	str wzr, [x9]
	add	x9, x0, #28
	str wzr, [x9]
	add	x9, x0, #32
	str wzr, [x9]
	add	x9, x0, #36
	str wzr, [x9]
	add	x9, x0, #40
	str wzr, [x9]
	add	x9, x0, #44
	str wzr, [x9]
	add	x9, x0, #48
	str wzr, [x9]
	add	x9, x0, #52
	str wzr, [x9]
	add	x9, x0, #56
	str wzr, [x9]
	add	x9, x0, #60
	sub	x0, x29, #144
	str wzr, [x9]
	mov x9, x0
	str wzr, [x9]
	add	x9, x0, #4
	str wzr, [x9]
	add	x9, x0, #8
	str wzr, [x9]
	add	x9, x0, #12
	str wzr, [x9]
	add	x9, x0, #16
	str wzr, [x9]
	add	x9, x0, #20
	str wzr, [x9]
	add	x9, x0, #24
	str wzr, [x9]
	add	x9, x0, #28
	str wzr, [x9]
	add	x9, x0, #32
	str wzr, [x9]
	add	x9, x0, #36
	str wzr, [x9]
	add	x9, x0, #40
	str wzr, [x9]
	add	x9, x0, #44
	str wzr, [x9]
	add	x9, x0, #48
	str wzr, [x9]
	add	x9, x0, #52
	str wzr, [x9]
	add	x9, x0, #56
	str wzr, [x9]
	add	x9, x0, #60
	sub	x0, x29, #208
	str wzr, [x9]
	mov x9, x0
	str wzr, [x9]
	add	x9, x0, #4
	str wzr, [x9]
	add	x9, x0, #8
	str wzr, [x9]
	add	x9, x0, #12
	str wzr, [x9]
	add	x9, x0, #16
	str wzr, [x9]
	add	x9, x0, #20
	str wzr, [x9]
	add	x9, x0, #24
	str wzr, [x9]
	add	x9, x0, #28
	str wzr, [x9]
	add	x9, x0, #32
	str wzr, [x9]
	add	x9, x0, #36
	str wzr, [x9]
	add	x9, x0, #40
	str wzr, [x9]
	add	x9, x0, #44
	str wzr, [x9]
	add	x9, x0, #48
	movz w10, #65535
	str wzr, [x9]
	add	x9, x0, #52
	movk w10, #65535, lsl #16
	str wzr, [x9]
	add	x9, x0, #56
	str wzr, [x9]
	add	x9, x0, #60
	cmp w25, w10
	mov x26, x1
	mov w27, w2
	mov w28, w3
	str wzr, [x9]
	b.eq	.LradixSort_epilogue
radixSort_label_or_11:
	add w9, w27, #1
	cmp w9, w28
	b.ge	.LradixSort_epilogue
radixSort_label_while_cond_12.preheader:
	cmp w27, w28
	b.ge radixSort_label_while_end_14
	mov w24, w27
radixSort_label_while_body_13:
	mov x9, x26
	add x9, x9, w24, sxtw #2
	ldr w23, [x9]
	sub w22, w25, #3
	movz w21, #0
radixSort_label_177:
	cmp w21, w22
	b.lt radixSort_label_181
radixSort_label_139:
	cmp w21, w25
	b.lt radixSort_label_143
radixSort_label_146:
	cmp w23, #0
	cneg w9, w23, mi
	and w9, w9, #15
	cneg w9, w9, mi
	sub x0, x29, #208
	add x0, x0, w9, sxtw #2
	ldr w9, [x0]
	add w24, w24, #1
	cmp w24, w28
	add w9, w9, #1
	str w9, [x0]
	b.ge radixSort_label_while_end_14
	b radixSort_label_while_body_13
radixSort_label_while_end_14:
	sub x11, x29, #80
	str x11, [x29, #-216]
	str w27, [x11]
	sub x11, x29, #144
	sub x20, x29, #208
	str x11, [x29, #-232]
	ldr w9, [x20]
	movz w2, #1
	add w9, w27, w9
	str w9, [x11]
radixSort_label_while_body_16:
	sub w0, w2, #1
	sub x11, x29, #144
	add	x0, x11, w0, sxtw #2
	ldr w1, [x0]
	sub x9, x29, #80
	add x9, x9, w2, uxtw #2
	sub x0, x29, #144
	str w1, [x9]
	sub x9, x29, #208
	add x9, x9, w2, uxtw #2
	ldr w9, [x9]
	add x0, x0, w2, uxtw #2
	cmp w2, #15
	add w9, w1, w9
	str w9, [x0]
	add w9, w2, #1
	b.lt .LradixSort_edge_0
	movz w19, #0
	b radixSort_label_while_cond_18
.LradixSort_edge_0:
	mov w2, w9
	b radixSort_label_while_body_16
radixSort_label_while_cond_18:
	cmp w19, #16
	b.lt radixSort_label_while_cond_21.preheader
radixSort_label_while_end_20:
	ldr x11, [x29, #-216]
	str w27, [x11]
	ldr w9, [x20]
	ldr x11, [x29, #-232]
	add w9, w27, w9
	str w9, [x11]
	sub w11, w25, #1
	str w11, [x29, #-248]
	str	wzr, [x29, #-8]
radixSort_label_while_cond_27:
	ldr w11, [x29, #-8]
	cmp w11, #16
	b.lt radixSort_label_while_body_28
radixSort_label_ret:
	b .LradixSort_epilogue
radixSort_label_while_cond_21.preheader:
	sub x7, x29, #80
	sub x6, x29, #144
	add x7, x7, w19, uxtw #2
	add x6, x6, w19, uxtw #2
radixSort_label_while_cond_21:
	ldr w0, [x7]
	ldr w9, [x6]
	cmp w0, w9
	b.lt radixSort_label_while_body_22
radixSort_label_while_end_23:
	add w19, w19, #1
	b radixSort_label_while_cond_18
radixSort_label_while_body_22:
	mov x9, x26
	add x9, x9, w0, sxtw #2
	ldr w5, [x9]
radixSort_label_while_cond_24:
	sub w4, w25, #3
	movz w3, #0
	mov w2, w5
radixSort_label_164:
	cmp w3, w4
	b.lt radixSort_label_168
radixSort_label_127:
	cmp w3, w25
	b.lt radixSort_label_131
radixSort_label_134:
	cmp w2, #0
	cneg w0, w2, mi
	and w0, w0, #15
	cneg w0, w0, mi
	cmp w0, w19
	b.ne radixSort_label_while_body_25
radixSort_label_while_end_26:
	ldr w9, [x7]
	mov x11, x26
	add	x9, x11, w9, sxtw #2
	str w5, [x9]
	ldr w9, [x7]
	add w9, w9, #1
	str w9, [x7]
	b radixSort_label_while_cond_21
radixSort_label_while_body_25:
	sub x1, x29, #80
	add x1, x1, w0, sxtw #2
	ldr w9, [x1]
	mov x11, x26
	add	x9, x11, w9, sxtw #2
	ldr w0, [x9]
	str w5, [x9]
	ldr w9, [x1]
	mov w5, w0
	add w9, w9, #1
	str w9, [x1]
	b radixSort_label_while_cond_24
radixSort_label_while_body_28:
	ldr w11, [x29, #-8]
	cmp w11, #0
	b.gt radixSort_label_if_then_30
radixSort_label_if_else_31:
	ldr w11, [x29, #-8]
	sub x0, x29, #80
	sub x9, x29, #144
	ldr w12, [x29, #-248]
	add x0, x0, w11, uxtw #2
	ldr w11, [x29, #-8]
	ldr w0, [x0]
	mov x1, x26
	add x9, x9, w11, uxtw #2
	ldr w9, [x9]
	mov w11, w0
	mov w0, w12
	mov w2, w11
	mov w3, w9
	bl radixSort
	ldr w12, [x29, #-8]
	sub x17, x29, #264
	add w11, w12, #1
	str w11, [x17]
	ldr w11, [x17]
	str w11, [x29, #-8]
	b radixSort_label_while_cond_27
radixSort_label_if_then_30:
	ldr w11, [x29, #-8]
	sub x9, x29, #80
	add x9, x9, w11, uxtw #2
	ldr w11, [x29, #-8]
	sub w0, w11, #1
	sub x11, x29, #144
	add	x0, x11, w0, sxtw #2
	ldr w1, [x0]
	sub x0, x29, #144
	str w1, [x9]
	ldr w11, [x29, #-8]
	sub x9, x29, #208
	add x0, x0, w11, uxtw #2
	ldr w11, [x29, #-8]
	add x9, x9, w11, uxtw #2
	ldr w9, [x9]
	add w9, w1, w9
	str w9, [x0]
	b radixSort_label_if_else_31
radixSort_label_131:
	asr w11, w2, #31
	bic w11, w11, w11, lsl #4
	add w2, w2, w11
	asr w2, w2, #4
	add w3, w3, #1
	b radixSort_label_127
radixSort_label_143:
	asr w11, w23, #31
	bic w11, w11, w11, lsl #4
	add w23, w23, w11
	asr w23, w23, #4
	add w21, w21, #1
	b radixSort_label_139
radixSort_label_168:
	asr w11, w2, #31
	bic w11, w11, w11, lsl #4
	add w9, w2, w11
	asr w9, w9, #4
	asr w11, w9, #31
	bic w11, w11, w11, lsl #4
	add w9, w9, w11
	asr w9, w9, #4
	asr w11, w9, #31
	bic w11, w11, w11, lsl #4
	add w9, w9, w11
	asr w9, w9, #4
	asr w11, w9, #31
	bic w11, w11, w11, lsl #4
	add w2, w9, w11
	add w3, w3, #4
	asr w2, w2, #4
	b radixSort_label_164
radixSort_label_181:
	asr w11, w23, #31
	bic w11, w11, w11, lsl #4
	add w9, w23, w11
	asr w9, w9, #4
	asr w11, w9, #31
	bic w11, w11, w11, lsl #4
	add w9, w9, w11
	asr w9, w9, #4
	asr w11, w9, #31
	bic w11, w11, w11, lsl #4
	add w9, w9, w11
	asr w9, w9, #4
	asr w11, w9, #31
	bic w11, w11, w11, lsl #4
	add w23, w9, w11
	add w21, w21, #4
	asr w23, w23, #4
	b radixSort_label_177
.LradixSort_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldp x26, x25, [sp, #48]
	ldp x28, x27, [sp, #64]
	add sp, sp, #352
	ldp x29, x30, [sp], #16
	ret
	.global main
	.p2align 2
main:
	sub sp, sp, #48
	adrp x10, ans
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	str x30, [sp, #32]
	ldr w21, [x10, :lo12:ans]
	adrp x20, a
	add x20, x20, :lo12:a
	mov x0, x20
	bl getarray
	mov w19, w0
	movz w0, #90
	bl _sysy_starttime
	movz w0, #9
	mov x1, x20
	mov w2, wzr
	mov w3, w19
	bl radixSort
	sub w6, w19, #3
	movz w5, #0
main_label_24:
	cmp w5, w6
	b.lt main_label_29
main_label_while_cond_32:
	cmp w5, w19
	b.lt main_label_while_body_33
main_label_while_end_34:
	sub w9, wzr, w21
	cmp w21, #0
	csel w22, w9, w21, lt
	movz w0, #102
	bl _sysy_stoptime
	mov w0, w22
	bl putint
	movz w0, #10
	bl putch
	adrp x10, ans
	str w22, [x10, :lo12:ans]
	mov w0, wzr
	b .Lmain_epilogue
main_label_while_body_33:
	ldr	w1, [x20], #4
	add w0, w5, #2
	add w9, w5, #1
	sdiv w10, w1, w0
	msub w0, w10, w0, w1
	madd	w0, w5, w0, w21
	mov w5, w9
	add w21, w0, #3
	b main_label_while_cond_32
main_label_29:
	ldr w9, [x20]
	add w4, w5, #2
	add w3, w5, #3
	add w1, w5, #1
	sdiv w10, w9, w4
	msub w9, w10, w4, w9
	madd	w9, w5, w9, w21
	add w2, w9, #3
	add	x9, x20, #4
	ldr w0, [x9]
	sdiv w10, w0, w3
	msub w0, w10, w3, w0
	madd	w0, w1, w0, w2
	add	x1, x9, #4
	ldr w9, [x1]
	add w2, w0, #3
	add w0, w5, #4
	sdiv w10, w9, w0
	msub w9, w10, w0, w9
	madd	w9, w4, w9, w2
	add	x2, x1, #4
	ldr w1, [x2]
	add w4, w5, #5
	add w9, w9, #3
	add	x20, x2, #4
	sdiv w10, w1, w4
	mov w5, w0
	msub w1, w10, w4, w1
	madd	w9, w3, w1, w9
	add w21, w9, #3
	b main_label_24
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldr x30, [sp, #32]
	add sp, sp, #48
	ret
	.data
	.global ans
	.p2align 2
ans:
	.word 0

	.bss
	.global a
	.p2align 4
a:
	.zero 120000040

