	.text
	.global main
	.p2align 2
main:
	sub sp, sp, #64
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	str x30, [sp, #48]
	bl getint
	mov w23, w0
	movz w0, #40
	bl _sysy_starttime
	sub w6, w23, #3
	movz w5, #0
	movz w22, #4
	movz w21, #3
	movz w20, #2
	movz w19, #1
main_label_80:
	cmp w5, w6
	b.lt main_label_87
main_label_25:
	cmp w5, w23
	b.lt main_label_32
	movz w4, #0
	movz w1, #4
	movz w9, #3
	movz w0, #2
	movz w3, #1
main_label_52:
	cmp w4, w6
	b.lt main_label_59
main_label_5:
	cmp w4, w23
	b.lt main_label_12
main_label_18:
	add w2, w3, w0
	add w2, w2, w9
	add w24, w2, w1
	movz w0, #43
	bl _sysy_stoptime
	add w2, w19, w20
	add w2, w2, w21
	add w2, w2, w22
	mov w0, w2
	bl putint
	movz w0, #10
	bl putch
	mov w0, w24
	bl putint
	movz w0, #10
	bl putch
	mov w0, wzr
	b .Lmain_epilogue
main_label_12:
	add w2, w3, w0
	add w0, w0, w9
	add w9, w9, w1
	add w1, w1, w3
	add w4, w4, #1
	mov w3, w2
	b main_label_5
main_label_32:
	add w19, w19, w20
	add w20, w20, w21
	add w21, w21, w22
	add w22, w22, w19
	add w5, w5, #1
	b main_label_25
main_label_59:
	add w2, w3, w0
	add w0, w0, w9
	add w9, w9, w1
	add w1, w1, w3
	add w3, w2, w0
	add w0, w0, w9
	add w9, w9, w1
	add w2, w1, w2
	add w1, w3, w0
	add w0, w0, w9
	add w9, w9, w2
	add w2, w2, w3
	add w3, w1, w0
	add w0, w0, w9
	add w9, w9, w2
	add w1, w2, w1
	add w4, w4, #4
	b main_label_52
main_label_87:
	add w2, w19, w20
	add w1, w20, w21
	add w0, w21, w22
	add w9, w22, w2
	add w2, w2, w1
	add w1, w1, w0
	add w0, w0, w9
	add w9, w9, w2
	add w2, w2, w1
	add w1, w1, w0
	add w0, w0, w9
	add w9, w9, w2
	add w19, w2, w1
	add w20, w1, w0
	add w21, w0, w9
	add w22, w9, w19
	add w5, w5, #4
	b main_label_80
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldr x30, [sp, #48]
	add sp, sp, #64
	ret
