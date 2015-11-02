#include <asm/unistd.h> /* For system call numbers. */
#define MAX_ERRNO 4095  /* For recognizing system call error returns. */

#define __bionic_asm_custom_entry(f)
#define __bionic_asm_custom_end(f)
#define __bionic_asm_function_type @function

#include <machine/asm.h>

#define ENTRY(f) \
    .text; \
    .globl f; \
    .align __bionic_asm_align; \
    .type f, __bionic_asm_function_type; \
    f: \
    __bionic_asm_custom_entry(f); \
    .cfi_startproc \

#define END(f) \
    .cfi_endproc; \
    .size f, .-f; \
    __bionic_asm_custom_end(f) \

ENTRY(shmat)
    mov     ip, r7
    ldr     r7, =__NR_shmat
    swi     #0
    mov     r7, ip
    cmn     r0, #(MAX_ERRNO + 1)
    bxls    lr
    neg     r0, r0
    b       __set_errno_internal
END(shmat)

ENTRY(shmdt)
    mov     ip, r7
    ldr     r7, =__NR_shmdt
    swi     #0
    mov     r7, ip
    cmn     r0, #(MAX_ERRNO + 1)
    bxls    lr
    neg     r0, r0
    b       __set_errno_internal
END(shmdt)

ENTRY(shmget)
    mov     ip, r7
    ldr     r7, =__NR_shmget
    swi     #0
    mov     r7, ip
    cmn     r0, #(MAX_ERRNO + 1)
    bxls    lr
    neg     r0, r0
    b       __set_errno_internal
END(shmget)
