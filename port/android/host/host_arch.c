/*
HOST_ARCH.C

The host's processor, for the crash report (host_memory.c) and the sampler
(host_debug.c): the registers a signal handler's ucontext_t holds, and the
guest's frame records.

AArch64 (the arm64_32 guest) keeps the return address in the link register
x30 and the frame pointer in x29; x86-64 (the x32 guest) keeps the return
address on the stack and the frame pointer in rbp. Both guests' frame
records are 16 bytes: the caller's frame pointer, then the return address,
8 bytes each (stp x29, x30; or call's return address and push rbp, which in
64-bit mode are 8 bytes, x32 too).

The walk runs inside signal handlers, from whatever the frame pointer
register held: in host or driver code built without frame pointers it is an
ordinary register, and even a real frame record may point into a guard page
(PROT_NONE), a part of a pool never handed out, or memory another thread has
just unmapped. So nothing is read through a pointer: each record is copied
by process_vm_readv on this process, which fails with EFAULT instead of
faulting. It takes no lock and touches only the caller's buffer, so it is
async-signal-safe; where a sandbox refuses the call the walk finds nothing.
*/

#include "host.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/ucontext.h>
#else
#include <sys/syscall.h>
#include <ucontext.h>
#endif
#include <sys/uio.h>
#include <unistd.h>

#if defined(__aarch64__) && defined(__APPLE__)

/* macOS: Darwin's mcontext, whose general registers are named fields
(arm/_mcontext.h) */
void host_arch_context(const void *ucontext, uint64_t *pc, uint64_t *sp, uint64_t *fp, uint64_t *lr)
{
	const struct __darwin_arm_thread_state64 *state = &((const ucontext_t *)ucontext)->uc_mcontext->__ss;

	*pc = (uint64_t)__darwin_arm_thread_state64_get_pc(*state);
	*sp = (uint64_t)__darwin_arm_thread_state64_get_sp(*state);
	*fp = (uint64_t)__darwin_arm_thread_state64_get_fp(*state);
	*lr = (uint64_t)__darwin_arm_thread_state64_get_lr(*state);
}

/* x0 to x28, four a line, then fp, lr, sp, pc and cpsr */
int host_arch_register_line(const void *ucontext, int line, char *buffer, size_t size)
{
	const struct __darwin_arm_thread_state64 *state = &((const ucontext_t *)ucontext)->uc_mcontext->__ss;
	uint64_t pc, sp, fp, lr;
	int first = line * 4, count, index, used;

	if (line < 0 || line > 8)
		return 0;
	if (line == 8)
	{
		host_arch_context(ucontext, &pc, &sp, &fp, &lr);
		snprintf(buffer, size, "  fp  %016llx lr  %016llx sp  %016llx pc  %016llx cpsr %08x",
			(unsigned long long)fp, (unsigned long long)lr, (unsigned long long)sp, (unsigned long long)pc,
			(unsigned)state->__cpsr);
		return 1;
	}
	count = first + 4 <= 29 ? 4 : 29 - first;
	used = snprintf(buffer, size, "  x%-2d", first);
	for (index = 0; index < count && used > 0 && (size_t)used < size; index++)
		used += snprintf(buffer + used, size - (size_t)used, " %016llx", (unsigned long long)state->__x[first + index]);
	return 1;
}

#elif defined(__aarch64__)

static const struct sigcontext *registers_of(const void *ucontext)
{
	return (const struct sigcontext *)&((const ucontext_t *)ucontext)->uc_mcontext;
}

void host_arch_context(const void *ucontext, uint64_t *pc, uint64_t *sp, uint64_t *fp, uint64_t *lr)
{
	const struct sigcontext *registers = registers_of(ucontext);

	*pc = registers->pc;
	*sp = registers->sp;
	*fp = registers->regs[29];
	*lr = registers->regs[30];
}

/* x0 to x30, four a line, then sp, pc and pstate */
int host_arch_register_line(const void *ucontext, int line, char *buffer, size_t size)
{
	const struct sigcontext *registers = registers_of(ucontext);
	int first = line * 4;

	if (line < 0 || line > 8)
		return 0;
	if (line == 8)
	{
		snprintf(buffer, size, "  sp  %016llx pc  %016llx pstate %016llx", (unsigned long long)registers->sp,
			(unsigned long long)registers->pc, (unsigned long long)registers->pstate);
		return 1;
	}
	if (first + 3 < 31)
	{
		snprintf(buffer, size, "  x%-2d %016llx %016llx %016llx %016llx", first,
			(unsigned long long)registers->regs[first], (unsigned long long)registers->regs[first + 1],
			(unsigned long long)registers->regs[first + 2], (unsigned long long)registers->regs[first + 3]);
	}
	else
	{
		snprintf(buffer, size, "  x%-2d %016llx %016llx %016llx", first, (unsigned long long)registers->regs[first],
			(unsigned long long)registers->regs[first + 1], (unsigned long long)registers->regs[first + 2]);
	}
	return 1;
}

#elif defined(__x86_64__) && defined(HALO_PS5)

/* the PS5's FreeBSD: mcontext_t's named fields (x86/ucontext.h), where
phase 0 B's host probe read them in a fault */
void host_arch_context(const void *ucontext, uint64_t *pc, uint64_t *sp, uint64_t *fp, uint64_t *lr)
{
	const mcontext_t *m = &((const ucontext_t *)ucontext)->uc_mcontext;

	*pc = (uint64_t)m->mc_rip;
	*sp = (uint64_t)m->mc_rsp;
	*fp = (uint64_t)m->mc_rbp;
	*lr = 0;
}

int host_arch_register_line(const void *ucontext, int line, char *buffer, size_t size)
{
	const mcontext_t *m = &((const ucontext_t *)ucontext)->uc_mcontext;
	const long values[5][4] = { { m->mc_rax, m->mc_rbx, m->mc_rcx, m->mc_rdx },
		{ m->mc_rsi, m->mc_rdi, m->mc_rbp, m->mc_rsp }, { m->mc_r8, m->mc_r9, m->mc_r10, m->mc_r11 },
		{ m->mc_r12, m->mc_r13, m->mc_r14, m->mc_r15 }, { m->mc_rip, m->mc_rflags, m->mc_err, m->mc_trapno } };
	static const char names[5][4][4] = { { "rax", "rbx", "rcx", "rdx" }, { "rsi", "rdi", "rbp", "rsp" },
		{ "r8 ", "r9 ", "r10", "r11" }, { "r12", "r13", "r14", "r15" }, { "rip", "efl", "err", "trp" } };

	if (line < 0 || line >= 5)
		return 0;
	snprintf(buffer, size, "  %s %016lx %s %016lx %s %016lx %s %016lx", names[line][0], values[line][0],
		names[line][1], values[line][1], names[line][2], values[line][2], names[line][3], values[line][3]);
	return 1;
}

#elif defined(__x86_64__)

static const greg_t *registers_of(const void *ucontext)
{
	return ((const ucontext_t *)ucontext)->uc_mcontext.gregs;
}

void host_arch_context(const void *ucontext, uint64_t *pc, uint64_t *sp, uint64_t *fp, uint64_t *lr)
{
	const greg_t *registers = registers_of(ucontext);

	*pc = (uint64_t)registers[REG_RIP];
	*sp = (uint64_t)registers[REG_RSP];
	*fp = (uint64_t)registers[REG_RBP];
	*lr = 0;
}

static const struct
{
	const char *names[4];
	int registers[4];
} register_lines[] =
{
	{ { "rax", "rbx", "rcx", "rdx" }, { REG_RAX, REG_RBX, REG_RCX, REG_RDX } },
	{ { "rsi", "rdi", "rbp", "rsp" }, { REG_RSI, REG_RDI, REG_RBP, REG_RSP } },
	{ { "r8 ", "r9 ", "r10", "r11" }, { REG_R8, REG_R9, REG_R10, REG_R11 } },
	{ { "r12", "r13", "r14", "r15" }, { REG_R12, REG_R13, REG_R14, REG_R15 } },
	{ { "rip", "efl", "err", "trp" }, { REG_RIP, REG_EFL, REG_ERR, REG_TRAPNO } },
};

int host_arch_register_line(const void *ucontext, int line, char *buffer, size_t size)
{
	const greg_t *registers = registers_of(ucontext);

	if (line < 0 || line >= (int)(sizeof(register_lines) / sizeof(register_lines[0])))
		return 0;
	snprintf(buffer, size, "  %s %016llx %s %016llx %s %016llx %s %016llx",
		register_lines[line].names[0], (unsigned long long)registers[register_lines[line].registers[0]],
		register_lines[line].names[1], (unsigned long long)registers[register_lines[line].registers[1]],
		register_lines[line].names[2], (unsigned long long)registers[register_lines[line].registers[2]],
		register_lines[line].names[3], (unsigned long long)registers[register_lines[line].registers[3]]);
	return 1;
}

#else
#error "the host's registers are not known for this architecture"
#endif

#if defined(__APPLE__)
/* macOS: the kernel copies, and a page that cannot be read fails the copy
rather than faulting (no lock is taken) */
int host_arch_read_memory(uintptr_t address, void *buffer, size_t size)
{
	mach_vm_size_t copied = 0;

	if (!size)
		return 1;
	if (address + size < address)
		return 0;
	return mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)address, (mach_vm_size_t)size,
		(mach_vm_address_t)(uintptr_t)buffer, &copied) == KERN_SUCCESS && copied == size;
}
#elif defined(HALO_PS5)
/* The PS5 has no process_vm_readv: every page of the range is asked of the
kernel first (one system call each, no lock), and copied only if all can be
read. Nothing unmaps guest memory there (host_memory.c keeps it). */
int32_t sceKernelQueryMemoryProtection(void *address, void **start, void **end, uint32_t *protection);

int host_arch_read_memory(uintptr_t address, void *buffer, size_t size)
{
	uintptr_t page = address;
	void *start, *end = NULL;
	uint32_t protection;

	if (address + size < address)
		return 0;
	for (; page < address + size; page = (uintptr_t)end)
		if (sceKernelQueryMemoryProtection((void *)page, &start, &end, &protection) != 0 || !(protection & 1) ||
			(uintptr_t)end <= page)
			return 0;
	__builtin_memcpy(buffer, (const void *)address, size);
	return 1;
}
#else
int host_arch_read_memory(uintptr_t address, void *buffer, size_t size)
{
	struct iovec local = { buffer, size };
	struct iovec remote = { (void *)address, size };
	long copied;

	if (!size)
		return 1;
	if (address + size < address)
		return 0;
	/* (the system call itself: no C library wrapper may take a lock) */
	copied = syscall(SYS_process_vm_readv, (long)getpid(), &local, 1L, &remote, 1L, 0L);
	return copied == (long)size;
}
#endif

int host_arch_walk_frames(uint64_t fp, int (*read)(uintptr_t address, void *buffer, size_t size),
	uint64_t *returns, int maximum)
{
	int count = 0;

	while (count < maximum && fp && !(fp & 7) && fp <= UINTPTR_MAX - 16)
	{
		uint64_t frame[2];

		if (!read((uintptr_t)fp, frame, sizeof(frame)))
			break;
		returns[count++] = frame[1];
		/* (the stack grows down: a caller's record is above its callee's) */
		if (frame[0] <= fp)
			break;
		fp = frame[0];
	}
	return count;
}
