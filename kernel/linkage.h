#ifndef _LINKAGE_H_
#define _LINKAGE_H_

#define L1_CACHE_BYTES 32

#define asmlinkage __attribute__((regparm(0)))	

#define ____cacheline_aligned __attribute__((__aligned__(L1_CACHE_BYTES)))

#define SYMBOL_NAME(X)	X

#define SYMBOL_NAME_STR(X)	#X

#define SYMBOL_NAME_LABEL(X) X##:


#define ENTRY(name, handler, has_error_code) \
    .global name; \
name:; \
    .if has_error_code; \
        pushq $0; \
    .endif; \
    pushq %rax; \
    leaq handler(%rip), %rax; \
    xchgq %rax, (%rsp); \
    jmp interrupt_entry;

#define	R15		0x00
#define	R14		0x08
#define	R13		0x10
#define	R12		0x18
#define	R11		0x20
#define	R10		0x28
#define	R9		0x30
#define	R8		0x38
#define	RBX		0x40
#define	RCX		0x48
#define	RDX		0x50
#define	RSI		0x58
#define	RDI		0x60
#define	RBP		0x68
#define	DS		0x70
#define	ES		0x78
#define	RAX		0x80
#define	FUNC		0x88
#define	ERRCODE		0x90
#define	RIP		0x98
#define	CS		0xa0
#define	RFLAGS		0xa8
#define	OLDRSP		0xb0
#define	OLDSS		0xb8

#endif
