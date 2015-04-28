#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>

/* 纯C环境下，不定义宏NO_CPP_DEMANGLE */
#if !defined(__cplusplus) && !defined(NO_CPP_DEMANGLE)
#define NO_CPP_DEMANGLE
#endif

#ifndef NO_CPP_DEMANGLE
    #include <cxxabi.h>
    #ifdef __cplusplus
    	using __cxxabiv1::__cxa_demangle;
    #endif
#endif

#ifdef HAS_ULSLIB
	#include <uls/logger.h>
	#define sigsegv_outp(x)	sigsegv_outp(, gx)
#else
	#define sigsegv_outp(x, ...) 	fprintf(stderr, x"\n", ##__VA_ARGS__)
#endif

#if defined(REG_RIP)	/* __x86_64__	*/
	#define SIGSEGV_STACK_IA64
	#define REGFORMAT	"%016lx"
#elif defined(REG_EIP)	/* __i386__		*/
	#define SIGSEGV_STACK_X86
	#define REGFORMAT	"%08x"
#else					/* arm 			*/
	#define SIGSEGV_STACK_ARM
	#define REGFORMAT	"%x"
#endif

static void print_reg(ucontext_t *uc) 
{
	int i;
#if defined SIGSEGV_STACK_X86 || defined SIGSEGV_STACK_IA64
	for (i = 0; i < NGREG; i++) {
		sigsegv_outp("reg[%02d]: 0x"REGFORMAT, i, uc->uc_mcontext.gregs[i]);
	}
#else
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 0, uc->uc_mcontext.arm_r0);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 1, uc->uc_mcontext.arm_r1);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 2, uc->uc_mcontext.arm_r2);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 3, uc->uc_mcontext.arm_r3);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 4, uc->uc_mcontext.arm_r4);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 5, uc->uc_mcontext.arm_r5);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 6, uc->uc_mcontext.arm_r6);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 7, uc->uc_mcontext.arm_r7);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 8, uc->uc_mcontext.arm_r8);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 9, uc->uc_mcontext.arm_r9);
	sigsegv_outp("reg[%02d]		= 0x"REGFORMAT, 10, uc->uc_mcontext.arm_r10);
	sigsegv_outp("FP		= 0x"REGFORMAT, uc->uc_mcontext.arm_fp);
	sigsegv_outp("IP		= 0x"REGFORMAT, uc->uc_mcontext.arm_ip);
	sigsegv_outp("SP		= 0x"REGFORMAT, uc->uc_mcontext.arm_sp);
	sigsegv_outp("LR		= 0x"REGFORMAT, uc->uc_mcontext.arm_lr);
	sigsegv_outp("PC		= 0x"REGFORMAT, uc->uc_mcontext.arm_pc);
	sigsegv_outp("CPSR		= 0x"REGFORMAT, uc->uc_mcontext.arm_cpsr);
	sigsegv_outp("Fault Address	= 0x"REGFORMAT, uc->uc_mcontext.fault_address);
	sigsegv_outp("Trap no		= 0x"REGFORMAT, uc->uc_mcontext.trap_no);
	sigsegv_outp("Err Code	= 0x"REGFORMAT, uc->uc_mcontext.error_code);
	sigsegv_outp("Old Mask	= 0x"REGFORMAT, uc->uc_mcontext.oldmask);
#endif
}

static void print_call_link(ucontext_t *uc) 
{
	int i = 0;
	void **frame_pointer = (void **)NULL;
	void *return_address = NULL;
	Dl_info	dl_info = { 0 };
#if defined SIGSEGV_STACK_X86 
	frame_pointer = (void **)uc->uc_mcontext.gregs[REG_EBP];
	return_address = (void *)uc->uc_mcontext.gregs[REG_EIP];
#elif defined SIGSEGV_STACK_IA64
	frame_pointer = (void **)uc->uc_mcontext.gregs[REG_RBP];
	return_address = (void *)uc->uc_mcontext.gregs[REG_RIP];
#else
/* sigcontext_t on ARM:
        unsigned long trap_no;
        unsigned long error_code;
        unsigned long oldmask;
        unsigned long arm_r0;
        ...
        unsigned long arm_r10;
        unsigned long arm_fp;
        unsigned long arm_ip;
        unsigned long arm_sp;
        unsigned long arm_lr;
        unsigned long arm_pc;
        unsigned long arm_cpsr;
        unsigned long fault_address;
*/
	frame_pointer = (void **)uc->uc_mcontext.arm_fp;
	return_address = (void *)uc->uc_mcontext.arm_pc;
#endif

	sigsegv_outp("\nStack trace:");
	while (frame_pointer && return_address) {
		if (!dladdr(return_address, &dl_info))	break;
		const char *sname = dl_info.dli_sname;	
#ifndef NO_CPP_DEMANGLE
		int status;
		char *tmp = __cxa_demangle(sname, NULL, 0, &status);
		if (status == 0 && tmp) {
			sname = tmp;
		}
#endif
		/* No: return address <sym-name + offset> (filename) */
		sigsegv_outp("%02d: %p <%s + %lu> (%s)", ++i, return_address, sname, 
			(unsigned long)return_address - (unsigned long)dl_info.dli_saddr, 
													dl_info.dli_fname);
#ifndef NO_CPP_DEMANGLE
		if (tmp)	free(tmp);
#endif
		if (dl_info.dli_sname && !strcmp(dl_info.dli_sname, "main")) {
			break;
		}

#if defined SIGSEGV_STACK_X86 || defined SIGSEGV_STACK_IA64 
		return_address = frame_pointer[1];
		frame_pointer = frame_pointer[0];
#else
		return_address = frame_pointer[-1];	
		frame_pointer = frame_pointer[-3];
#endif
	}
	sigsegv_outp("Stack trace end.");
}

static void sigsegv_handler(int signo, siginfo_t *info, void *context)
{
	if (context) {
		ucontext_t *uc = (ucontext_t *)context;

   		sigsegv_outp("Segmentation Fault!");
    	sigsegv_outp("info.si_signo = %d", signo);
    	sigsegv_outp("info.si_errno = %d", info->si_errno);
    	sigsegv_outp("info.si_code  = %d (%s)", info->si_code, 
			(info->si_code == SEGV_MAPERR) ? "SEGV_MAPERR" : "SEGV_ACCERR");
    	sigsegv_outp("info.si_addr  = %p\n", info->si_addr);

		print_reg(uc);
		print_call_link(uc);
	}

	_exit(0);
}

static void __attribute((constructor)) setup_sigsegv(void) 
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = sigsegv_handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGSEGV, &sa, NULL) < 0) {
		perror("sigaction: ");
	}
}

#if 1
void func3(void)
{
	char *p = (char *)0x12345678;
	*p = 10;
}

void func2(void)
{
	func3();	
}

void func1(void)
{
	func2();
}

int main(int argc, const char *argv[])
{
	func1();	
	exit(EXIT_SUCCESS);
}
#endif