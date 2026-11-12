/* The MCC reader's linked tag window (port/linux/src/mcc_memory.c), as the
Android host gives it (port/android/host/host_mcc_memory.c): one window of
MCC_TAG_SIZE bytes at the address the map's tags are linked to, mapped when
the guest asks and given back when it is done. At MCC Tool's address
(0x50000000) the window is the range host_memory.c mapped and held from the
start of main (host_memory_hold_mcc_range): taken when asked for, a thread's
local storage page had landed in it on some consoles first. Elsewhere it is
taken when asked for, where nothing of this host's is (host_mman.c's
MAP_FIXED_NOREPLACE refuses a range in use). */
#include "host.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define MCC_TAG_SIZE 0x04000000u
#define MCC_TAG_BASE 0x50000000u
/* (a moved window's alignment: any 64 KB, as the guest asks) */
#define CHUNK_ALIGNMENT 0x10000u

long long host_mcc_memory_reserve(unsigned int address, unsigned int size);
int host_mcc_memory_release(unsigned int address, unsigned int size);

static void *mcc_window;

long long host_mcc_memory_reserve(unsigned int address, unsigned int size)
{
	void *wanted = (void *)(uintptr_t)address;
	void *result;

	if (mcc_window)
		return -EBUSY;
	/* Address 0: anywhere clear of linked game data, for a map whose own
	address is taken; the guest moves its tags there
	(port/linux/game/mcc_cache.c) */
	if (!address && size == MCC_TAG_SIZE)
	{
		uint64_t placed = host_memory_map_clear(MCC_TAG_SIZE, CHUNK_ALIGNMENT);

		if (!placed)
		{
			host_logf(HOST_LOG_WARN, "mcc memory: no free 64 MB below 4 GB clear of linked game data");
			return -ENOMEM;
		}
		host_logf(HOST_LOG_INFO, "mcc memory: tag window at %08llx instead", (unsigned long long)placed);
		mcc_window = (void *)(uintptr_t)placed;
		return (long long)placed;
	}
	/* (the MCC reader's allowed windows, as the Android host checks them) */
	if ((address & 0xffffu) || size != MCC_TAG_SIZE || address < 0x42000000u || address > 0x70000000u)
		return -EINVAL;
	result = host_memory_take_mcc_hold(address, size);
	if (result)
	{
		mcc_window = result;
		return (long long)address;
	}
	/* (MCC Tool's own range is always claimed, to keep the port's memory out
	of it: host_memory.c. Only something mapped there refuses it, below) */
	if (address != MCC_TAG_BASE && host_memory_range_is_reserved(address, size))
	{
		host_logf(HOST_LOG_WARN, "mcc memory: %08x-%08x is held by the host", address, address + size);
		return -EEXIST;
	}
	result = mmap(wanted, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (result == MAP_FAILED)
	{
		int error = errno ? errno : ENOMEM;

		host_logf(HOST_LOG_WARN, "mcc memory: cannot map %08x-%08x (%s)", address, address + size, strerror(error));
		return -error;
	}
	if (result != wanted)
	{
		/* (the tags' pointers are linked to the address asked for) */
		munmap(result, size);
		return -EEXIST;
	}
	mcc_window = result;
	return (long long)address;
}

int host_mcc_memory_release(unsigned int address, unsigned int size)
{
	if (!mcc_window || mcc_window != (void *)(uintptr_t)address || size != MCC_TAG_SIZE)
		return -EINVAL;
	/* (the held range stays mapped, for the next map) */
	if (host_memory_return_mcc_hold(address, size))
	{
		mcc_window = NULL;
		return 0;
	}
	if (munmap(mcc_window, size))
		return errno ? -errno : -EINVAL;
	mcc_window = NULL;
	return 0;
}
