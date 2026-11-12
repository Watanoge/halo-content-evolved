/* The MCC reader's linked tag window (port/linux/src/mcc_memory.c), as the
Android host gives it (port/android/host/host_mcc_memory.c): one window of
MCC_TAG_SIZE bytes at the address the map's tags are linked to, mapped when
the guest asks and given back when it is done. The console has no other
process to share the address space with, so nothing is held in advance:
the window is taken when asked for, where nothing of this host's is
(host_mman.c's MAP_FIXED_NOREPLACE refuses a range in use). */
#include "host.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define MCC_TAG_SIZE 0x04000000u

long long host_mcc_memory_reserve(unsigned int address, unsigned int size);
int host_mcc_memory_release(unsigned int address, unsigned int size);

static void *mcc_window;

long long host_mcc_memory_reserve(unsigned int address, unsigned int size)
{
	void *wanted = (void *)(uintptr_t)address;
	void *result;

	/* (the MCC reader's allowed windows, as the Android host checks them) */
	if ((address & 0xffffu) || size != MCC_TAG_SIZE || address < 0x42000000u || address > 0x70000000u)
		return -EINVAL;
	if (mcc_window)
		return -EBUSY;
	if (host_memory_range_is_reserved(address, size))
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
	if (munmap(mcc_window, size))
		return errno ? -errno : -EINVAL;
	mcc_window = NULL;
	return 0;
}
