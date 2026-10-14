.section .text
.global __mlibc_restorer
__mlibc_restorer:
	mov $19, %rax
	syscall
	ud2
.section .note.GNU-stack,"",%progbits
