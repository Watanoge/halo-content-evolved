/*
HOST_MEMORY_MACOS.C

The guest's memory on macOS, in place of the Android host's host_memory.c
(and host_mcc_memory.c).

macOS on Apple silicon keeps the low 4 GB of every process unmapped
(__PAGEZERO), so the guest's 4 GB lie at a base the host chooses: a
multiple of 4 GB (host_guest_base), and a guest address is the offset from
it (port/android/host/host.h; tools/guest_base_register.py). Nothing but
the guest lives there, so unlike Android's nothing has to be found room
for: the image and the Xbox window are where the guest was linked for, and
Custom Edition's and MCC's tag windows are always free for the guest to
claim.

macOS's pages are 16 KB and the guest's 4 KB (its musl, the Xbox memory of
port/linux/src/xbox_memory.c). So the 4 GB are mapped readable and writable
once, at start-up (memory is only given when a page is first touched), but
for the first 16 MB, which stay inaccessible so that a null pointer faults,
and the guest's own memory calls are kept here, a bit a 4 KB page:

- an anonymous mapping takes a free run of pages (from 16 MB, clear of the
  image, the window and the two tag windows), and a fixed one the pages it
  names (MAP_FIXED_NOREPLACE fails where any is in use, as the kernel's);
- free pages are always zero: what is given back is zeroed as it goes (the
  host pages wholly inside it mapped again, which also hands their memory
  back to the system, and the rest cleared), so a new mapping needs no
  clearing;
- a mapping of a file is read into place (a private copy: the guest maps
  files only to read them);
- mprotect is answered without a call, but for PROT_NONE guard pages, which
  are kept only in the bookkeeping; madvise's MADV_DONTNEED zeroes.

The write tracking (port/linux/src/memory_watch.c's interface) compares the
contents of the window's pages (host_watch_hash.c) rather than protecting
them: a 16 KB host page cannot carry the protection of each of its 4 KB
pages. SIGSEGV, SIGBUS and SIGILL are reported (host_arch.c) and passed on.
*/

#include "host.h"
#include "host_mcc_memory.h"
#include "host_watch_hash.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE 0x1000ULL
#define HOST_PAGE 0x4000ULL
#define LIMIT 0x100000000ULL
/* the first 16 MB stay inaccessible: a null pointer's accesses fault */
#define LOW_START 0x01000000ULL
#define PAGE_COUNT (LIMIT / PAGE)

/* the guest's linked ranges, never handed out by an anonymous mapping */
#define MCC_LINKED_BASE 0x50000000ULL
#define MCC_LINKED_END 0x54000000ULL
#define MCC_TAG_SIZE 0x04000000u

/* Linux's mmap flags, the guest's (host_syscall_macos.c passes them on) */
#define LINUX_MAP_SHARED 0x01
#define LINUX_MAP_PRIVATE 0x02
#define LINUX_MAP_FIXED 0x10
#define LINUX_MAP_ANONYMOUS 0x20
#define LINUX_MAP_FIXED_NOREPLACE 0x100000
#define LINUX_MADV_DONTNEED 4

uintptr_t host_guest_base;
uint32_t host_memory_window_wanted;
uint32_t host_memory_image_wanted;

static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;
/* a bit for each 4 KB page of the guest's: 1 in use */
static uint8_t used[PAGE_COUNT / 8];
static uint64_t image_base, image_end;
static uint64_t window_base, window_end, window_size;
/* where the search for a free run starts: past the last one taken */
static uint64_t search_from = LOW_START;

static uint64_t round_up(uint64_t value, uint64_t unit)
{
	return (value + unit - 1) & ~(unit - 1);
}

static int page_used(uint64_t page)
{
	return (used[page >> 3] >> (page & 7)) & 1;
}

static void set_used(uint64_t address, uint64_t length, int value)
{
	uint64_t page;

	for (page = address / PAGE; page < (address + length) / PAGE; page++)
	{
		if (value)
			used[page >> 3] |= (uint8_t)(1 << (page & 7));
		else
			used[page >> 3] &= (uint8_t)~(1 << (page & 7));
	}
}

static int any_used(uint64_t address, uint64_t length)
{
	uint64_t page;

	for (page = address / PAGE; page < (address + length) / PAGE; page++)
	{
		if (page_used(page))
			return 1;
	}
	return 0;
}

/* whether a range crosses what an anonymous mapping never takes */
static int linked_range(uint64_t address, uint64_t length)
{
	uint64_t end = address + length;

	return (address < HALO_GUEST_CUSTOM_EDITION_END && end > HALO_GUEST_CUSTOM_EDITION_BASE) ||
		(address < MCC_LINKED_END && end > MCC_LINKED_BASE) ||
		(address < (uint64_t)HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE && end > HALO_GUEST_IMAGE_BASE) ||
		(address < (uint64_t)HALO_GUEST_WINDOW_BASE + HALO_GUEST_WINDOW_MAXIMUM && end > HALO_GUEST_WINDOW_BASE);
}

/* ---------- the reservation */

static void reserve(void)
{
	uint64_t span = 2 * LIMIT;
	uint8_t *mapping;
	uintptr_t base;

	if (host_guest_base)
		return;
	mapping = mmap(NULL, span, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
	if (mapping == MAP_FAILED)
		host_fatal("cannot reserve the guest's 4 GB of address space (%s)", strerror(errno));
	base = ((uintptr_t)mapping + LIMIT - 1) & ~(uintptr_t)(LIMIT - 1);
	/* (the rest of the 8 GB given back: the guest's 4 GB alone stay) */
	if (base > (uintptr_t)mapping)
		munmap(mapping, base - (uintptr_t)mapping);
	if ((uintptr_t)mapping + span > base + LIMIT)
		munmap((void *)(base + LIMIT), (uintptr_t)mapping + span - (base + LIMIT));
	if (mprotect((void *)(base + LOW_START), LIMIT - LOW_START, PROT_READ | PROT_WRITE) != 0)
		host_fatal("cannot make the guest's memory writable (%s)", strerror(errno));
	host_guest_base = base;
	host_logf(HOST_LOG_INFO, "guest memory: 4 GB at %016llx", (unsigned long long)base);
}

void host_memory_reserve_early(void)
{
	reserve();
}

int host_memory_fixed_unavailable(void)
{
	return 0;
}

void host_memory_report_low_mappings(FILE *file)
{
	(void)file;
}

int host_memory_initialize(uint32_t preferred_base, uint32_t size, uint32_t *base)
{
	reserve();
	pthread_mutex_lock(&memory_lock);
	image_base = preferred_base;
	image_end = preferred_base + round_up(size, HOST_PAGE);
	window_base = HALO_GUEST_WINDOW_BASE;
	window_size = HALO_GUEST_WINDOW_SIZE;
	window_end = window_base + window_size;
	set_used(image_base, image_end - image_base, 1);
	pthread_mutex_unlock(&memory_lock);
	*base = (uint32_t)image_base;
	host_logf(HOST_LOG_INFO, "Xbox memory window of %u MB at %08llx, guest image at %08llx-%08llx",
		(unsigned int)(window_size >> 20), (unsigned long long)window_base, (unsigned long long)image_base,
		(unsigned long long)image_end);
	return 0;
}

uint32_t host_memory_window_base(void)
{
	return (uint32_t)window_base;
}

uint32_t host_memory_window_size(void)
{
	return (uint32_t)window_size;
}

/* ---------- giving pages out and back */

/* a free run of length bytes on a host page, clear of the linked ranges,
marked used; 0 if there is none (with the memory lock held) */
static uint64_t take_run(uint64_t length)
{
	uint64_t start, pass;

	for (pass = 0; pass < 2; pass++)
	{
		start = pass ? LOW_START : round_up(search_from, HOST_PAGE);
		while (start + length <= LIMIT)
		{
			uint64_t page, last = 0;
			int clear = 1;

			if (linked_range(start, length))
			{
				start = round_up(start + HOST_PAGE, HOST_PAGE);
				continue;
			}
			for (page = start / PAGE; page < (start + length) / PAGE; page++)
			{
				if (page_used(page))
				{
					clear = 0;
					last = page;
					break;
				}
			}
			if (clear)
			{
				set_used(start, length, 1);
				search_from = start + length;
				return start;
			}
			start = round_up((last + 1) * PAGE, HOST_PAGE);
		}
	}
	return 0;
}

/* the pages of a range zeroed: those of whole host pages mapped again (the
system takes their memory back), the rest cleared */
static void zero_range(uint64_t address, uint64_t length)
{
	uint64_t end = address + length;
	uint64_t inner_start = round_up(address, HOST_PAGE), inner_end = end & ~(HOST_PAGE - 1);

	if (inner_start < inner_end)
	{
		if (mmap((void *)host_address((uint32_t)inner_start), inner_end - inner_start, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED)
			memset((void *)host_address((uint32_t)inner_start), 0, inner_end - inner_start);
		if (address < inner_start)
			memset((void *)host_address((uint32_t)address), 0, inner_start - address);
		if (inner_end < end)
			memset((void *)host_address((uint32_t)inner_end), 0, end - inner_end);
	}
	else
		memset((void *)host_address((uint32_t)address), 0, length);
}

/* gives a range back: zeroed, then free */
static void release_range(uint64_t address, uint64_t length)
{
	zero_range(address, length);
	set_used(address, length, 0);
}

void *host_low_map(size_t size, int protection)
{
	uint64_t length = round_up(size, PAGE), address;

	(void)protection;
	if (!length)
		return NULL;
	reserve();
	pthread_mutex_lock(&memory_lock);
	address = take_run(length);
	pthread_mutex_unlock(&memory_lock);
	return address ? (void *)host_address((uint32_t)address) : NULL;
}

void host_low_unmap(void *address, size_t size)
{
	uint64_t start, length;

	if (!address || !host_in_guest((uintptr_t)address))
		return;
	start = guest_address(address) & ~(PAGE - 1);
	length = round_up(guest_address(address) + size, PAGE) - start;
	/* (a thread stack's guard page, host_thread.c: writable again) */
	mprotect((void *)host_address((uint32_t)(start & ~(HOST_PAGE - 1))),
		round_up(start + length, HOST_PAGE) - (start & ~(HOST_PAGE - 1)), PROT_READ | PROT_WRITE);
	pthread_mutex_lock(&memory_lock);
	release_range(start, length);
	pthread_mutex_unlock(&memory_lock);
}

int host_low_owns(uintptr_t address, size_t size)
{
	return host_in_guest(address) && host_in_guest(address + size - 1);
}

/* ---------- the guest's memory system calls (host_syscall_macos.c): guest
addresses, Linux's flags, -errno */

/* a file's contents read into place */
static long read_file(uint64_t address, uint64_t length, int fd, int64_t offset)
{
	char *to = (char *)host_address((uint32_t)address);
	uint64_t done = 0;

	while (done < length)
	{
		ssize_t got = pread(fd, to + done, length - done, offset + (off_t)done);

		if (got < 0)
		{
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!got)
			break;
		done += (uint64_t)got;
	}
	return 0;
}

long host_guest_mmap(uint64_t address, uint64_t size, int protection, int flags, int fd, int64_t offset)
{
	uint64_t length = round_up(size, PAGE);
	long status;

	(void)protection;
	if (!length || length < size)
		return -EINVAL;
	if (!(flags & LINUX_MAP_ANONYMOUS) && (flags & LINUX_MAP_SHARED))
	{
		static int said;

		if (!said++)
			host_logf(HOST_LOG_WARN, "guest memory: a shared file mapping, made a private copy");
	}
	pthread_mutex_lock(&memory_lock);
	if (flags & (LINUX_MAP_FIXED | LINUX_MAP_FIXED_NOREPLACE))
	{
		if ((address & (PAGE - 1)) || address < LOW_START || address + length > LIMIT)
		{
			status = (flags & LINUX_MAP_FIXED) ? -EINVAL : -ENOMEM;
			goto done;
		}
		if (any_used(address, length))
		{
			if (!(flags & LINUX_MAP_FIXED))
			{
				status = -EEXIST;
				goto done;
			}
			/* (a mapping replaced: what was there goes) */
			zero_range(address, length);
		}
		set_used(address, length, 1);
	}
	else
	{
		address = take_run(length);
		if (!address)
		{
			status = -ENOMEM;
			goto done;
		}
	}
	status = (long)address;
	if (!(flags & LINUX_MAP_ANONYMOUS) && fd >= 0)
	{
		long read = read_file(address, length, fd, offset);

		if (read < 0)
		{
			release_range(address, length);
			status = read;
		}
	}
done:
	pthread_mutex_unlock(&memory_lock);
	if (status >= 0 && address >= window_base && address < window_end)
		host_memory_watch_forget((uint32_t)address, (uint32_t)length);
	return status;
}

long host_guest_munmap(uint64_t address, uint64_t size)
{
	uint64_t length = round_up(size, PAGE);

	if ((address & (PAGE - 1)) || !length || address + length > LIMIT)
		return -EINVAL;
	/* (the image is never unmapped) */
	if (address < image_end && address + length > image_base)
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	release_range(address, length);
	pthread_mutex_unlock(&memory_lock);
	if (address < window_end && address + length > window_base)
		host_memory_watch_forget((uint32_t)address, (uint32_t)length);
	return 0;
}

long host_guest_mprotect(uint64_t address, uint64_t size, int protection)
{
	(void)protection;
	if ((address & (PAGE - 1)) || address + size > LIMIT)
		return -EINVAL;
	return 0;
}

long host_guest_madvise(uint64_t address, uint64_t size, int advice)
{
	uint64_t length = round_up(size, PAGE);

	if ((address & (PAGE - 1)) || address + length > LIMIT)
		return -EINVAL;
	if (advice == LINUX_MADV_DONTNEED && length)
	{
		pthread_mutex_lock(&memory_lock);
		zero_range(address, length);
		pthread_mutex_unlock(&memory_lock);
	}
	return 0;
}

/* mremap with MREMAP_MAYMOVE (musl's realloc of a large block): a new run,
the contents copied, the old run given back */
long host_guest_mremap(uint64_t address, uint64_t old_size, uint64_t new_size, int may_move)
{
	uint64_t old_length = round_up(old_size, PAGE), new_length = round_up(new_size, PAGE), moved;

	if ((address & (PAGE - 1)) || !new_length || address + old_length > LIMIT)
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	if (new_length <= old_length)
	{
		if (new_length < old_length)
			release_range(address + new_length, old_length - new_length);
		pthread_mutex_unlock(&memory_lock);
		return (long)address;
	}
	if (address + new_length <= LIMIT && !any_used(address + old_length, new_length - old_length) &&
		!linked_range(address + old_length, new_length - old_length))
	{
		set_used(address + old_length, new_length - old_length, 1);
		pthread_mutex_unlock(&memory_lock);
		return (long)address;
	}
	if (!may_move)
	{
		pthread_mutex_unlock(&memory_lock);
		return -ENOMEM;
	}
	moved = take_run(new_length);
	if (!moved)
	{
		pthread_mutex_unlock(&memory_lock);
		return -ENOMEM;
	}
	memcpy((void *)host_address((uint32_t)moved), (const void *)host_address((uint32_t)address), old_length);
	release_range(address, old_length);
	pthread_mutex_unlock(&memory_lock);
	return (long)moved;
}

/* ---------- MCC's tag window (port/linux/src/mcc_memory.c): always free
here, at the address asked for, or 0x50000000 for "anywhere" */

static int mcc_active;
static uint32_t mcc_address;

void host_mcc_memory_initialize(void)
{
}

long long host_mcc_memory_reserve(unsigned int address, unsigned int size)
{
	long long result;

	if (!address && size == MCC_TAG_SIZE)
		address = (unsigned int)MCC_LINKED_BASE;
	if ((address & 0xffffu) || size != MCC_TAG_SIZE || address < 0x42000000u || address > 0x70000000u)
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	if (mcc_active)
		result = -EBUSY;
	else if (any_used(address, size))
		result = -EEXIST;
	else
	{
		set_used(address, size, 1);
		mcc_active = 1;
		mcc_address = address;
		result = address;
	}
	pthread_mutex_unlock(&memory_lock);
	return result;
}

int host_mcc_memory_release(unsigned int address, unsigned int size)
{
	int error = 0;

	pthread_mutex_lock(&memory_lock);
	if (!mcc_active || address != mcc_address || size != MCC_TAG_SIZE)
		error = -EINVAL;
	else
	{
		release_range(address, size);
		mcc_active = 0;
	}
	pthread_mutex_unlock(&memory_lock);
	return error;
}

uint64_t host_memory_place_clear(uint64_t size, uint64_t alignment)
{
	(void)size;
	(void)alignment;
	return MCC_LINKED_BASE;
}

/* ---------- write tracking (port/linux/src/memory_watch.c): page hashes */

#define WATCH_PAGE_COUNT (HALO_GUEST_WINDOW_MAXIMUM / PAGE)

static volatile uint32_t current_generation = 1;
static uint8_t page_hash_watched[WATCH_PAGE_COUNT];
static uint64_t page_hash[WATCH_PAGE_COUNT];
static uint32_t page_hashed_frame[WATCH_PAGE_COUNT];
static uint32_t page_generation[WATCH_PAGE_COUNT];
static struct watch_hash page_hashes =
{
	.page_count = WATCH_PAGE_COUNT,
	.watched = page_hash_watched,
	.hash = page_hash,
	.hashed_frame = page_hashed_frame,
	.generation = page_generation,
	.current_generation = &current_generation,
	.frame = 1,
};
static int watch_active;

static int in_window(uint64_t address)
{
	return address >= window_base && address - window_base < window_size;
}

static void watch_pages(uint32_t address, uint32_t size, uint32_t *first, uint32_t *last)
{
	uint64_t end = (uint64_t)address + size - 1;

	*first = (uint32_t)((address - window_base) / PAGE);
	*last = (uint32_t)(((end < window_end ? end : window_end - 1) - window_base) / PAGE);
}

void host_memory_watch_initialize(void)
{
	page_hashes.base = (const uint8_t *)host_address(HALO_GUEST_WINDOW_BASE);
	watch_active = 1;
}

void host_memory_watch_use_hashes(void)
{
}

void host_memory_watch_protect(uint32_t address, uint32_t size)
{
	uint32_t first, last;

	if (!watch_active || !size || !in_window(address))
		return;
	watch_pages(address, size, &first, &last);
	watch_hash_protect(&page_hashes, first, last);
}

/* (the texture caches skip their checks while this is unchanged: with page
hashes a write changes no generation until the pages are hashed again, so the
frame's number is in it too, and each frame they look) */
uint32_t host_memory_watch_serial(void)
{
	return current_generation + page_hashes.frame;
}

uint32_t host_memory_watch_generation(uint32_t address, uint32_t size)
{
	uint32_t first, last;

	if (!size || !in_window(address))
		return 0;
	watch_pages(address, size, &first, &last);
	return watch_hash_generation(&page_hashes, first, last);
}

void host_memory_watch_prepare_write(uint32_t address, uint32_t size)
{
	(void)address;
	(void)size;
}

void host_memory_watch_forget(uint32_t address, uint32_t size)
{
	uint32_t first, last;

	if (!size || !in_window(address))
		return;
	watch_pages(address, size, &first, &last);
	watch_hash_forget(&page_hashes, first, last);
}

void host_memory_watch_begin_frame(void)
{
	watch_hash_begin_frame(&page_hashes);
}

/* ---------- faults: reported, then the previous handler's */

static struct sigaction previous_segv, previous_bus, previous_ill;

static void report_crash(int signal_number, siginfo_t *information, void *context)
{
	char line[256];
	uint64_t pc, sp, fp, lr;
	uint64_t returns[24];
	int count, index;

	host_arch_context(context, &pc, &sp, &fp, &lr);
	snprintf(line, sizeof(line), "signal %d at address %p (guest %08x): pc %016llx lr %016llx sp %016llx fp %016llx",
		signal_number, information->si_addr, guest_address(information->si_addr), (unsigned long long)pc,
		(unsigned long long)lr, (unsigned long long)sp, (unsigned long long)fp);
	host_log(HOST_LOG_ERROR, line);
	if (host_in_guest(pc) && guest_address((void *)pc) >= host_image.base && guest_address((void *)pc) < host_image.end)
	{
		snprintf(line, sizeof(line), "  in the guest image: addr2line -e halo_guest_vk.elf 0x%x 0x%x",
			guest_address((void *)pc), host_in_guest(lr) ? guest_address((void *)lr) : 0);
		host_log(HOST_LOG_ERROR, line);
	}
	for (index = 0; host_arch_register_line(context, index, line, sizeof(line)); index++)
		host_log(HOST_LOG_ERROR, line);
	count = host_arch_walk_frames(fp, host_arch_read_memory, returns, (int)(sizeof(returns) / sizeof(returns[0])));
	for (index = 0; index < count; index++)
	{
		snprintf(line, sizeof(line), "  frame %2d: return %016llx%s", index, (unsigned long long)returns[index],
			host_in_guest(returns[index]) ? " (guest)" : "");
		host_log(HOST_LOG_ERROR, line);
	}
}

static void chain(struct sigaction *previous, int signal_number, siginfo_t *information, void *context)
{
	sigaction(signal_number, previous, NULL);
	if (previous->sa_flags & SA_SIGINFO)
	{
		if (previous->sa_sigaction)
			previous->sa_sigaction(signal_number, information, context);
	}
	else if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN)
		previous->sa_handler(signal_number);
}

static void fault_handler(int signal_number, siginfo_t *information, void *context)
{
	int saved_errno = errno;

	report_crash(signal_number, information, context);
	chain(signal_number == SIGSEGV ? &previous_segv : signal_number == SIGBUS ? &previous_bus : &previous_ill,
		signal_number, information, context);
	errno = saved_errno;
}

void host_install_signal_handlers(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
	sigemptyset(&action.sa_mask);
	action.sa_sigaction = fault_handler;
	sigaction(SIGSEGV, &action, &previous_segv);
	sigaction(SIGBUS, &action, &previous_bus);
	sigaction(SIGILL, &action, &previous_ill);
}
