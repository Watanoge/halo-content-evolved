/*
HOST_SYSCALL_MACOS.C

The guest's system calls on macOS, in place of the Android host's
host_syscall.c (which hands most to Linux's kernel as they are). The guest's
musl was built for Linux (port/android/include/halo_guest_syscall.h): its
calls have Linux's numbers, flags, structure layouts and error numbers,
which this file translates into the Darwin calls that do the same, and back:

- error numbers: Linux's and Darwin's agree up to 34 but for 11 (Linux's
  EAGAIN, Darwin's EDEADLK); above, linux_errno translates (also for the
  posix_* helpers' errno, host_errno);
- open's flags, fcntl's commands and lock types, the AT_ flags and
  AT_FDCWD, the clocks, utimensat's UTIME_NOW and UTIME_OMIT;
- getdents64, which Darwin has not: a directory stream (fdopendir) for each
  descriptor read so, its entries written in Linux's struct linux_dirent64;
- the futexes of musl's locks and conditions: os_sync_wait_on_address and
  its wakes (macOS 14.4); a requeue wakes every waiter, who then wait again;
- clock_nanosleep (Darwin has not): nanosleep, an absolute time made
  relative;
- memory (host_memory_macos.c), in guest addresses with Linux's flags.

Pointers are guest addresses (port/android/host/host.h: GUEST adds the
base). A call the guest's runtime never makes fails with ENOSYS, said once.
*/

#include "host.h"
#include "halo_guest_syscall.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <os/os_sync_wait_on_address.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define GUEST(type, value) ((type)host_pointer((uint32_t)(value)))

long host_guest_mremap(uint64_t address, uint64_t old_size, uint64_t new_size, int may_move);
void host_abort(const char *reason) __attribute__((noreturn));

/* ---------- error numbers */

/* Linux's number for a Darwin errno */
int host_linux_errno(int error)
{
	switch (error)
	{
	case EDEADLK: return 35;
	case EAGAIN: return 11;
	case EINPROGRESS: return 115;
	case EALREADY: return 114;
	case ENOTSOCK: return 88;
	case EDESTADDRREQ: return 89;
	case EMSGSIZE: return 90;
	case EPROTOTYPE: return 91;
	case ENOPROTOOPT: return 92;
	case EPROTONOSUPPORT: return 93;
	case ESOCKTNOSUPPORT: return 94;
	case ENOTSUP: return 95;
	case EOPNOTSUPP: return 95;
	case EPFNOSUPPORT: return 96;
	case EAFNOSUPPORT: return 97;
	case EADDRINUSE: return 98;
	case EADDRNOTAVAIL: return 99;
	case ENETDOWN: return 100;
	case ENETUNREACH: return 101;
	case ENETRESET: return 102;
	case ECONNABORTED: return 103;
	case ECONNRESET: return 104;
	case ENOBUFS: return 105;
	case EISCONN: return 106;
	case ENOTCONN: return 107;
	case ESHUTDOWN: return 108;
	case ETOOMANYREFS: return 109;
	case ETIMEDOUT: return 110;
	case ECONNREFUSED: return 111;
	case ELOOP: return 40;
	case ENAMETOOLONG: return 36;
	case EHOSTDOWN: return 112;
	case EHOSTUNREACH: return 113;
	case ENOTEMPTY: return 39;
	case EUSERS: return 87;
	case EDQUOT: return 122;
	case ESTALE: return 116;
	case ENOLCK: return 37;
	case ENOSYS: return 38;
	case EOVERFLOW: return 75;
	case ECANCELED: return 125;
	case EIDRM: return 43;
	case ENOMSG: return 42;
	case EILSEQ: return 84;
	case EBADMSG: return 74;
	case EMULTIHOP: return 72;
	case ENODATA: return 61;
	case ENOLINK: return 67;
	case EPROTO: return 71;
	case ETIME: return 62;
	}
	return error < 35 ? error : 5 /* EIO */;
}

static long result_of(long value)
{
	return value == -1 ? -host_linux_errno(errno) : value;
}

/* a -errno of this file's own (Darwin's constant) in Linux's number */
#define FAIL(error) (-(long)host_linux_errno(error))

/* ---------- open's flags, the AT_ flags, descriptors */

struct open_flag
{
	uint32_t guest;
	int host;
};

static const struct open_flag open_flags[] =
{
	{GUEST_O_CREAT, O_CREAT},
	{GUEST_O_EXCL, O_EXCL},
	{GUEST_O_NOCTTY, O_NOCTTY},
	{GUEST_O_TRUNC, O_TRUNC},
	{GUEST_O_APPEND, O_APPEND},
	{GUEST_O_NONBLOCK, O_NONBLOCK},
	{GUEST_O_DSYNC, O_DSYNC},
	{GUEST_O_ASYNC, O_ASYNC},
	{GUEST_O_DIRECTORY, O_DIRECTORY},
	{GUEST_O_NOFOLLOW, O_NOFOLLOW},
	{GUEST_O_CLOEXEC, O_CLOEXEC},
	{GUEST_O_SYNC_BIT, O_SYNC},
	/* (none on Darwin: taken and dropped) */
	{GUEST_O_DIRECT, 0},
	{GUEST_O_LARGEFILE, 0},
	{GUEST_O_NOATIME, 0},
};

#define OPEN_FLAG_COUNT ((int)(sizeof(open_flags) / sizeof(open_flags[0])))

static long open_flags_in(uint32_t guest)
{
	uint32_t remaining = guest & ~(uint32_t)GUEST_O_ACCMODE;
	long host = guest & GUEST_O_ACCMODE;
	int index;

	for (index = 0; index < OPEN_FLAG_COUNT; index++)
	{
		if (remaining & open_flags[index].guest)
		{
			host |= open_flags[index].host;
			remaining &= ~open_flags[index].guest;
		}
	}
	return remaining ? FAIL(EINVAL) : host;
}

static uint32_t open_flags_out(long host)
{
	uint32_t guest = (uint32_t)(host & O_ACCMODE);
	int index;

	for (index = 0; index < OPEN_FLAG_COUNT; index++)
	{
		if (open_flags[index].host && (host & open_flags[index].host) == open_flags[index].host)
			guest |= open_flags[index].guest;
	}
	return guest;
}

static int directory_in(long long guest)
{
	return (int)guest == GUEST_AT_FDCWD ? AT_FDCWD : (int)guest;
}

#define LINUX_AT_EACCESS 0x200

static int at_flags_in(int guest, int access)
{
	int host = 0;

	if (guest & GUEST_AT_SYMLINK_NOFOLLOW)
		host |= AT_SYMLINK_NOFOLLOW;
	if (access ? (guest & LINUX_AT_EACCESS) != 0 : 0)
		host |= AT_EACCESS;
	if (!access && (guest & GUEST_AT_REMOVEDIR))
		host |= AT_REMOVEDIR;
	return host;
}

/* ---------- structures */

static int timespec_in(uint64_t address, struct timespec *result)
{
	const struct halo_guest_timespec *value = GUEST(const struct halo_guest_timespec *, address);

	if (!address)
		return 0;
	result->tv_sec = value->seconds;
	result->tv_nsec = value->nanoseconds;
	return 1;
}

static void timespec_out(uint64_t address, const struct timespec *value)
{
	struct halo_guest_timespec *result = GUEST(struct halo_guest_timespec *, address);

	if (!address)
		return;
	result->seconds = (int32_t)value->tv_sec;
	result->nanoseconds = (int32_t)value->tv_nsec;
}

static void kstat_out(const struct stat *value, struct halo_guest_kstat *result)
{
	memset(result, 0, sizeof(*result));
	result->st_dev = (uint64_t)value->st_dev;
	result->st_ino = (uint64_t)value->st_ino;
	result->st_mode = (uint32_t)value->st_mode;
	result->st_nlink = (uint32_t)value->st_nlink;
	result->st_uid = (uint32_t)value->st_uid;
	result->st_gid = (uint32_t)value->st_gid;
	result->st_rdev = (uint64_t)value->st_rdev;
	result->st_size = (int64_t)value->st_size;
	result->st_blksize = (int32_t)value->st_blksize;
	result->st_blocks = (int64_t)value->st_blocks;
	result->atime_seconds = (int64_t)value->st_atimespec.tv_sec;
	result->atime_nanoseconds = (int64_t)value->st_atimespec.tv_nsec;
	result->mtime_seconds = (int64_t)value->st_mtimespec.tv_sec;
	result->mtime_nanoseconds = (int64_t)value->st_mtimespec.tv_nsec;
	result->ctime_seconds = (int64_t)value->st_ctimespec.tv_sec;
	result->ctime_nanoseconds = (int64_t)value->st_ctimespec.tv_nsec;
}

static long guest_fstatat(long long directory, uint64_t path, uint64_t result, int flags, int by_descriptor)
{
	struct stat value;
	int status;
	const char *name = GUEST(const char *, path);

	if (!result || (!by_descriptor && !path))
		return FAIL(EFAULT);
	if (by_descriptor || ((flags & GUEST_AT_EMPTY_PATH) && name && !name[0]))
		status = fstat((int)directory, &value);
	else
		status = fstatat(directory_in(directory), name, &value, at_flags_in(flags, 0));
	if (status != 0)
		return result_of(-1);
	kstat_out(&value, GUEST(struct halo_guest_kstat *, result));
	return 0;
}

/* ---------- vectored reads and writes */

#define IOVEC_STACK_COUNT 64

static long iovec_in(uint64_t vector, int count, struct iovec *buffer, struct iovec **result)
{
	const struct halo_guest_iovec *guest_vector = GUEST(const struct halo_guest_iovec *, vector);
	struct iovec *host_vector = buffer;
	int index;

	if (count < 0 || count > GUEST_IOV_MAX)
		return FAIL(EINVAL);
	if (count && !vector)
		return FAIL(EFAULT);
	if (count > IOVEC_STACK_COUNT)
	{
		host_vector = malloc((size_t)count * sizeof(*host_vector));
		if (!host_vector)
			return FAIL(ENOMEM);
	}
	for (index = 0; index < count; index++)
	{
		host_vector[index].iov_base = GUEST(void *, guest_vector[index].base);
		host_vector[index].iov_len = guest_vector[index].length;
	}
	*result = host_vector;
	return 0;
}

static long guest_vectored(int fd, uint64_t vector, int count, int64_t offset, int positional, int writing)
{
	struct iovec buffer[IOVEC_STACK_COUNT];
	struct iovec *host_vector;
	long result = iovec_in(vector, count, buffer, &host_vector);

	if (result)
		return result;
	if (writing)
		result = result_of(positional ? pwritev(fd, host_vector, count, offset) : writev(fd, host_vector, count));
	else
		result = result_of(positional ? preadv(fd, host_vector, count, offset) : readv(fd, host_vector, count));
	if (host_vector != buffer)
		free(host_vector);
	return result;
}

static int64_t offset_of_halves(long long low, long long high)
{
	return (int64_t)((uint64_t)(uint32_t)low | ((uint64_t)(uint32_t)high << 32));
}

/* ---------- directories: getdents64 on a stream of the descriptor's own */

#define DIRECTORY_STREAMS 64

static struct
{
	int fd;
	DIR *stream;
	/* an entry read that did not fit the last buffer, given first next time */
	struct dirent pending;
	int has_pending;
} directories[DIRECTORY_STREAMS];
static pthread_mutex_t directory_lock = PTHREAD_MUTEX_INITIALIZER;

static void directory_forget(int fd)
{
	int index;

	pthread_mutex_lock(&directory_lock);
	for (index = 0; index < DIRECTORY_STREAMS; index++)
	{
		if (directories[index].stream && directories[index].fd == fd)
		{
			closedir(directories[index].stream);
			directories[index].stream = NULL;
		}
	}
	pthread_mutex_unlock(&directory_lock);
}

/* struct linux_dirent64: d_ino, d_off, d_reclen, d_type, d_name, 8-aligned */
static long guest_getdents64(int fd, uint64_t buffer, uint32_t size)
{
	unsigned char *out = GUEST(unsigned char *, buffer);
	uint32_t used = 0;
	int index, slot = -1;

	pthread_mutex_lock(&directory_lock);
	for (index = 0; index < DIRECTORY_STREAMS; index++)
	{
		if (directories[index].stream && directories[index].fd == fd)
			slot = index;
	}
	if (slot < 0)
	{
		int copy;

		for (index = 0; index < DIRECTORY_STREAMS && slot < 0; index++)
		{
			if (!directories[index].stream)
				slot = index;
		}
		copy = slot < 0 ? -1 : dup(fd);
		if (copy < 0 || !(directories[slot].stream = fdopendir(copy)))
		{
			long error = copy < 0 && slot >= 0 ? result_of(-1) : FAIL(EMFILE);

			if (copy >= 0)
				close(copy);
			pthread_mutex_unlock(&directory_lock);
			return slot < 0 ? FAIL(EMFILE) : error;
		}
		directories[slot].fd = fd;
		directories[slot].has_pending = 0;
	}
	for (;;)
	{
		struct dirent *entry;
		size_t length;
		uint32_t record;
		uint64_t inode;
		int64_t next;
		uint16_t record_length;

		if (directories[slot].has_pending)
			entry = &directories[slot].pending;
		else
		{
			errno = 0;
			entry = readdir(directories[slot].stream);
			if (!entry)
				break;
		}
		length = strlen(entry->d_name);
		record = (uint32_t)((19 + length + 1 + 7) & ~(size_t)7);
		if (used + record > size)
		{
			if (entry != &directories[slot].pending)
			{
				directories[slot].pending = *entry;
				directories[slot].has_pending = 1;
			}
			if (!used)
			{
				pthread_mutex_unlock(&directory_lock);
				return FAIL(EINVAL);
			}
			break;
		}
		directories[slot].has_pending = 0;
		inode = entry->d_ino;
		next = used + record;
		record_length = (uint16_t)record;
		memset(out + used, 0, record);
		memcpy(out + used, &inode, 8);
		memcpy(out + used + 8, &next, 8);
		memcpy(out + used + 16, &record_length, 2);
		out[used + 18] = entry->d_type;
		memcpy(out + used + 19, entry->d_name, length);
		used += record;
	}
	pthread_mutex_unlock(&directory_lock);
	return used;
}

/* ---------- fcntl */

static int lock_type_in(int guest)
{
	/* Linux's F_RDLCK 0, F_WRLCK 1, F_UNLCK 2 */
	return guest == 0 ? F_RDLCK : guest == 1 ? F_WRLCK : F_UNLCK;
}

static int lock_type_out(int host)
{
	return host == F_RDLCK ? 0 : host == F_WRLCK ? 1 : 2;
}

static long guest_fcntl(int fd, int command, uint64_t argument)
{
	switch (command)
	{
	case GUEST_F_DUPFD:
		return result_of(fcntl(fd, F_DUPFD, (int)argument));
	case GUEST_F_DUPFD_CLOEXEC:
		return result_of(fcntl(fd, F_DUPFD_CLOEXEC, (int)argument));
	case GUEST_F_GETFD:
		return result_of(fcntl(fd, F_GETFD));
	case GUEST_F_SETFD:
		return result_of(fcntl(fd, F_SETFD, (int)argument));
	case GUEST_F_GETFL:
	{
		long result = result_of(fcntl(fd, F_GETFL));

		return result < 0 ? result : (long)open_flags_out(result);
	}
	case GUEST_F_SETFL:
	{
		long flags = open_flags_in((uint32_t)argument);

		if (flags < 0)
			return flags;
		return result_of(fcntl(fd, F_SETFL, (int)flags));
	}
	case GUEST_F_GETOWN:
		return result_of(fcntl(fd, F_GETOWN));
	case GUEST_F_SETOWN:
		return result_of(fcntl(fd, F_SETOWN, (int)argument));
	case GUEST_F_GETLK:
	case GUEST_F_SETLK:
	case GUEST_F_SETLKW:
	case GUEST_F_OFD_GETLK:
	case GUEST_F_OFD_SETLK:
	case GUEST_F_OFD_SETLKW:
	{
		struct halo_guest_flock *guest_lock = GUEST(struct halo_guest_flock *, argument);
		struct flock lock;
		int host_command = (command == GUEST_F_GETLK || command == GUEST_F_OFD_GETLK) ? F_GETLK :
			(command == GUEST_F_SETLKW || command == GUEST_F_OFD_SETLKW) ? F_SETLKW : F_SETLK;
		long result;

		if (!argument)
			return FAIL(EFAULT);
		memset(&lock, 0, sizeof(lock));
		lock.l_type = (short)lock_type_in(guest_lock->l_type);
		lock.l_whence = guest_lock->l_whence;
		lock.l_start = guest_lock->l_start;
		lock.l_len = guest_lock->l_len;
		lock.l_pid = guest_lock->l_pid;
		result = result_of(fcntl(fd, host_command, &lock));
		if (result == 0)
		{
			guest_lock->l_type = (int16_t)lock_type_out(lock.l_type);
			guest_lock->l_whence = lock.l_whence;
			guest_lock->l_start = lock.l_start;
			guest_lock->l_len = lock.l_len;
			guest_lock->l_pid = lock.l_pid;
		}
		return result;
	}
	default:
		return FAIL(EINVAL);
	}
}

/* ---------- time */

#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID 2
#define LINUX_CLOCK_THREAD_CPUTIME_ID 3
#define LINUX_CLOCK_MONOTONIC_RAW 4
#define LINUX_CLOCK_REALTIME_COARSE 5
#define LINUX_CLOCK_MONOTONIC_COARSE 6
#define LINUX_CLOCK_BOOTTIME 7
#define LINUX_TIMER_ABSTIME 1
#define LINUX_UTIME_NOW ((1l << 30) - 1l)
#define LINUX_UTIME_OMIT ((1l << 30) - 2l)

static int clock_in(long long guest, clockid_t *host)
{
	switch ((int)guest)
	{
	case LINUX_CLOCK_REALTIME:
	case LINUX_CLOCK_REALTIME_COARSE:
		*host = CLOCK_REALTIME;
		return 1;
	case LINUX_CLOCK_MONOTONIC:
	case LINUX_CLOCK_MONOTONIC_COARSE:
	case LINUX_CLOCK_BOOTTIME:
		*host = CLOCK_MONOTONIC;
		return 1;
	case LINUX_CLOCK_MONOTONIC_RAW:
		*host = CLOCK_MONOTONIC_RAW;
		return 1;
	case LINUX_CLOCK_PROCESS_CPUTIME_ID:
		*host = CLOCK_PROCESS_CPUTIME_ID;
		return 1;
	case LINUX_CLOCK_THREAD_CPUTIME_ID:
		*host = CLOCK_THREAD_CPUTIME_ID;
		return 1;
	}
	return 0;
}

static int64_t nanoseconds_of(const struct timespec *value)
{
	return (int64_t)value->tv_sec * 1000000000ll + value->tv_nsec;
}

/* a relative time until an absolute one on a clock; 0 if past */
static struct timespec until(clockid_t clock, const struct timespec *deadline)
{
	struct timespec now, result = { 0, 0 };
	int64_t left;

	clock_gettime(clock, &now);
	left = nanoseconds_of(deadline) - nanoseconds_of(&now);
	if (left > 0)
	{
		result.tv_sec = (time_t)(left / 1000000000ll);
		result.tv_nsec = (long)(left % 1000000000ll);
	}
	return result;
}

/* ---------- futexes */

#define LINUX_FUTEX_WAIT 0
#define LINUX_FUTEX_WAKE 1
#define LINUX_FUTEX_REQUEUE 3
#define LINUX_FUTEX_CMP_REQUEUE 4
#define LINUX_FUTEX_WAKE_OP 5
#define LINUX_FUTEX_WAIT_BITSET 9
#define LINUX_FUTEX_WAKE_BITSET 10
#define LINUX_FUTEX_PRIVATE_FLAG 128
#define LINUX_FUTEX_CLOCK_REALTIME 256

static long futex_wake(void *address, uint32_t count)
{
	int result;

	if (!count)
		return 0;
	result = count == 1 ? os_sync_wake_by_address_any(address, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE) :
		os_sync_wake_by_address_all(address, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE);
	/* (Darwin says ENOENT where nobody waited: Linux's count, 0) */
	return result == 0 ? 1 : 0;
}

static long guest_futex(uint64_t address, int operation, uint32_t value, uint64_t timeout, uint64_t address2,
	uint32_t value3)
{
	uint32_t *word = GUEST(uint32_t *, address);
	int command = operation & ~(LINUX_FUTEX_PRIVATE_FLAG | LINUX_FUTEX_CLOCK_REALTIME);

	if (!address || (address & 3))
		return FAIL(EINVAL);
	switch (command)
	{
	case LINUX_FUTEX_WAIT:
	case LINUX_FUTEX_WAIT_BITSET:
	{
		struct timespec wait;
		int result;

		if (__atomic_load_n(word, __ATOMIC_SEQ_CST) != value)
			return -11 /* EAGAIN */;
		if (timespec_in(timeout, &wait))
		{
			/* (WAIT's timeout is relative, WAIT_BITSET's absolute: on the
			realtime clock when asked, else the monotonic one) */
			if (command == LINUX_FUTEX_WAIT_BITSET)
				wait = until((operation & LINUX_FUTEX_CLOCK_REALTIME) ? CLOCK_REALTIME : CLOCK_MONOTONIC, &wait);
			if (!wait.tv_sec && !wait.tv_nsec)
				return -110 /* ETIMEDOUT */;
			result = os_sync_wait_on_address_with_timeout(word, value, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE,
				OS_CLOCK_MACH_ABSOLUTE_TIME, (uint64_t)nanoseconds_of(&wait));
		}
		else
			result = os_sync_wait_on_address(word, value, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE);
		if (result < 0)
			return errno == ETIMEDOUT ? -110 : errno == EINTR ? -4 : FAIL(errno);
		return 0;
	}
	case LINUX_FUTEX_WAKE:
	case LINUX_FUTEX_WAKE_BITSET:
		return futex_wake(word, value);
	case LINUX_FUTEX_CMP_REQUEUE:
		if (__atomic_load_n(word, __ATOMIC_SEQ_CST) != value3)
			return -11 /* EAGAIN */;
		/* fall through */
	case LINUX_FUTEX_REQUEUE:
		/* every waiter woken: each looks at its condition again and waits
		again where it must (musl's condition variables do) */
		(void)address2;
		futex_wake(word, UINT32_MAX);
		return 1;
	case LINUX_FUTEX_WAKE_OP:
		futex_wake(word, value);
		futex_wake(GUEST(uint32_t *, address2), (uint32_t)timeout);
		return 1;
	}
	return FAIL(ENOSYS);
}

/* ---------- the rest */

static long guest_uname(uint64_t address)
{
	/* Linux's struct utsname: six fields of 65 characters */
	char *out = GUEST(char *, address);
	struct utsname value;

	if (!out)
		return FAIL(EFAULT);
	if (uname(&value) != 0)
		return result_of(-1);
	memset(out, 0, 6 * 65);
	strncpy(out, value.sysname, 64);
	strncpy(out + 65, value.nodename, 64);
	strncpy(out + 130, value.release, 64);
	strncpy(out + 195, value.version, 64);
	strncpy(out + 260, value.machine, 64);
	return 0;
}

static long guest_prlimit(int resource, uint64_t old)
{
	/* Linux's RLIMIT_NOFILE is 7 (Darwin's 8); the rest are answered unlimited */
	uint64_t *out = GUEST(uint64_t *, old);
	struct rlimit limit;

	if (!out)
		return 0;
	out[0] = out[1] = UINT64_MAX;
	if (resource == 7 && getrlimit(RLIMIT_NOFILE, &limit) == 0)
	{
		out[0] = limit.rlim_cur == RLIM_INFINITY ? UINT64_MAX : (uint64_t)limit.rlim_cur;
		out[1] = limit.rlim_max == RLIM_INFINITY ? UINT64_MAX : (uint64_t)limit.rlim_max;
	}
	else if (resource == 3 && getrlimit(RLIMIT_STACK, &limit) == 0)
	{
		out[0] = limit.rlim_cur == RLIM_INFINITY ? UINT64_MAX : (uint64_t)limit.rlim_cur;
		out[1] = limit.rlim_max == RLIM_INFINITY ? UINT64_MAX : (uint64_t)limit.rlim_max;
	}
	return 0;
}

static long guest_sysinfo(uint64_t address)
{
	struct halo_guest_sysinfo *out = GUEST(struct halo_guest_sysinfo *, address);
	uint64_t memory = 0;
	size_t size = sizeof(memory);
	struct timeval boot;
	size_t boot_size = sizeof(boot);
	int mib[2] = { CTL_KERN, KERN_BOOTTIME };
	uint32_t unit = 1;

	if (!out)
		return FAIL(EFAULT);
	memset(out, 0, sizeof(*out));
	sysctlbyname("hw.memsize", &memory, &size, NULL, 0);
	while (memory > UINT32_MAX && unit < 0x80000000u)
	{
		memory >>= 1;
		unit <<= 1;
	}
	out->totalram = (uint32_t)memory;
	out->freeram = (uint32_t)(memory / 2);
	out->mem_unit = unit;
	out->procs = 1;
	if (sysctl(mib, 2, &boot, &boot_size, NULL, 0) == 0)
		out->uptime = (uint32_t)(time(NULL) - boot.tv_sec);
	return 0;
}

static long guest_getrandom(uint64_t buffer, uint32_t size)
{
	unsigned char *out = GUEST(unsigned char *, buffer);
	uint32_t done = 0;

	while (done < size)
	{
		uint32_t part = size - done > 256 ? 256 : size - done;

		if (getentropy(out + done, part) != 0)
			return result_of(-1);
		done += part;
	}
	return size;
}

static long guest_pipe2(uint64_t fds, uint32_t guest_flags)
{
	int *out = GUEST(int *, fds), pair[2];
	long flags = open_flags_in(guest_flags);

	if (flags < 0)
		return flags;
	if (pipe(pair) != 0)
		return result_of(-1);
	if (flags & O_CLOEXEC)
	{
		fcntl(pair[0], F_SETFD, FD_CLOEXEC);
		fcntl(pair[1], F_SETFD, FD_CLOEXEC);
	}
	if (flags & O_NONBLOCK)
	{
		fcntl(pair[0], F_SETFL, O_NONBLOCK);
		fcntl(pair[1], F_SETFL, O_NONBLOCK);
	}
	out[0] = pair[0];
	out[1] = pair[1];
	return 0;
}

static long guest_utimensat(long long directory, uint64_t path, uint64_t times_address, int flags)
{
	struct timespec times[2];
	int index;

	if (times_address)
	{
		timespec_in(times_address, &times[0]);
		timespec_in(times_address + sizeof(struct halo_guest_timespec), &times[1]);
		for (index = 0; index < 2; index++)
		{
			if (times[index].tv_nsec == LINUX_UTIME_NOW)
				times[index].tv_nsec = UTIME_NOW;
			else if (times[index].tv_nsec == LINUX_UTIME_OMIT)
				times[index].tv_nsec = UTIME_OMIT;
		}
	}
	if (!path)
		return result_of(futimens((int)directory, times_address ? times : NULL));
	return result_of(utimensat(directory_in(directory), GUEST(const char *, path), times_address ? times : NULL,
		at_flags_in(flags, 0)));
}

static long guest_renameat2(long long from_directory, uint64_t from, long long to_directory, uint64_t to,
	uint32_t flags)
{
	/* Linux's RENAME_NOREPLACE 1, RENAME_EXCHANGE 2 */
	unsigned int host_flags = (flags & 1 ? RENAME_EXCL : 0) | (flags & 2 ? RENAME_SWAP : 0);

	if (!host_flags)
		return result_of(renameat(directory_in(from_directory), GUEST(const char *, from),
			directory_in(to_directory), GUEST(const char *, to)));
	return result_of(renameatx_np(directory_in(from_directory), GUEST(const char *, from),
		directory_in(to_directory), GUEST(const char *, to), host_flags));
}

static long unsupported(long long number)
{
	static unsigned char said[512];

	if (number >= 0 && number < 512 && !said[number])
	{
		said[number] = 1;
		host_logf(HOST_LOG_WARN, "guest system call %lld is not supported on macOS", number);
	}
	return FAIL(ENOSYS);
}

/* ---------- dispatch */

long long host_syscall(long long number, long long a, long long b, long long c, long long d, long long e, long long f)
{
	switch (number)
	{
	case GUEST_SYS_read:
		return result_of(read((int)a, GUEST(void *, b), (size_t)(uint32_t)c));
	case GUEST_SYS_write:
		return result_of(write((int)a, GUEST(const void *, b), (size_t)(uint32_t)c));
	case GUEST_SYS_pread64:
		return result_of(pread((int)a, GUEST(void *, b), (size_t)(uint32_t)c, (off_t)d));
	case GUEST_SYS_pwrite64:
		return result_of(pwrite((int)a, GUEST(const void *, b), (size_t)(uint32_t)c, (off_t)d));
	case GUEST_SYS_writev:
		return guest_vectored((int)a, (uint64_t)b, (int)c, 0, 0, 1);
	case GUEST_SYS_pwritev:
		return guest_vectored((int)a, (uint64_t)b, (int)c, offset_of_halves(d, e), 1, 1);
	case GUEST_SYS_readv:
		return guest_vectored((int)a, (uint64_t)b, (int)c, 0, 0, 0);
	case GUEST_SYS_preadv:
		return guest_vectored((int)a, (uint64_t)b, (int)c, offset_of_halves(d, e), 1, 0);
	case GUEST_SYS_close:
		directory_forget((int)a);
		return result_of(close((int)a));

	case GUEST_SYS_openat:
	{
		long flags = open_flags_in((uint32_t)c);

		if (flags < 0)
			return flags;
		if ((uint32_t)c & GUEST_O_PATH)
			flags = (flags & ~O_ACCMODE) | O_RDONLY;
		return result_of(openat(directory_in(a), GUEST(const char *, b), (int)flags, (mode_t)(uint32_t)d));
	}
	case GUEST_SYS_fstat:
		return guest_fstatat(a, 0, (uint64_t)b, 0, 1);
	case GUEST_SYS_newfstatat:
		return guest_fstatat(a, (uint64_t)b, (uint64_t)c, (int)d, 0);
	case GUEST_SYS__llseek:
	{
		int64_t offset = (int64_t)(((uint64_t)(uint32_t)b << 32) | (uint32_t)c);
		off_t position;

		if (!d)
			return FAIL(EFAULT);
		position = lseek((int)a, (off_t)offset, (int)e);
		if (position == (off_t)-1)
			return result_of(-1);
		/* (a directory read from the start again) */
		if (offset == 0 && (int)e == SEEK_SET)
			directory_forget((int)a);
		memcpy(GUEST(void *, d), &(int64_t){(int64_t)position}, sizeof(int64_t));
		return 0;
	}
	case GUEST_SYS_getdents64:
		return guest_getdents64((int)a, (uint64_t)b, (uint32_t)c);
	case GUEST_SYS_fcntl:
		return guest_fcntl((int)a, (int)b, (uint64_t)c);
	case GUEST_SYS_pipe2:
		return guest_pipe2((uint64_t)a, (uint32_t)b);
	case GUEST_SYS_dup:
		return result_of(dup((int)a));
	case GUEST_SYS_dup3:
	{
		long flags = open_flags_in((uint32_t)c);
		long result;

		if (flags < 0)
			return flags;
		result = result_of(dup2((int)a, (int)b));
		if (result >= 0 && (flags & O_CLOEXEC))
			fcntl((int)result, F_SETFD, FD_CLOEXEC);
		return result;
	}
	case GUEST_SYS_unlinkat:
		return result_of(unlinkat(directory_in(a), GUEST(const char *, b), at_flags_in((int)c, 0)));
	case GUEST_SYS_renameat:
		return guest_renameat2(a, (uint64_t)b, c, (uint64_t)d, 0);
	case GUEST_SYS_renameat2:
		return guest_renameat2(a, (uint64_t)b, c, (uint64_t)d, (uint32_t)e);
	case GUEST_SYS_mkdirat:
		return result_of(mkdirat(directory_in(a), GUEST(const char *, b), (mode_t)(uint32_t)c));
	case GUEST_SYS_fchmod:
		return result_of(fchmod((int)a, (mode_t)(uint32_t)b));
	case GUEST_SYS_fchmodat:
		return result_of(fchmodat(directory_in(a), GUEST(const char *, b), (mode_t)(uint32_t)c, 0));
	case GUEST_SYS_ftruncate:
		return result_of(ftruncate((int)a, (off_t)b));
	case GUEST_SYS_fsync:
	case GUEST_SYS_fdatasync:
		return result_of(fsync((int)a));
	case GUEST_SYS_getcwd:
	{
		char *buffer = GUEST(char *, a);

		if (!getcwd(buffer, (size_t)(uint32_t)b))
			return result_of(-1);
		return (long)strlen(buffer) + 1;
	}
	case GUEST_SYS_chdir:
		return result_of(chdir(GUEST(const char *, a)));
	case GUEST_SYS_readlinkat:
		return result_of(readlinkat(directory_in(a), GUEST(const char *, b), GUEST(char *, c), (size_t)(uint32_t)d));
	case GUEST_SYS_faccessat:
		return result_of(faccessat(directory_in(a), GUEST(const char *, b), (int)c, at_flags_in((int)d, 1)));
	case GUEST_SYS_utimensat:
		return guest_utimensat(a, (uint64_t)b, (uint64_t)c, (int)d);
	case GUEST_SYS_umask:
		return (long)umask((mode_t)(uint32_t)a);
	case GUEST_SYS_flock:
		return result_of(flock((int)a, (int)b));

	case GUEST_SYS_getpid:
		return getpid();
	case GUEST_SYS_getppid:
		return getppid();
	case GUEST_SYS_gettid:
	case GUEST_SYS_set_tid_address:
		return (long)pthread_mach_thread_np(pthread_self());
	case GUEST_SYS_getuid:
		return getuid();
	case GUEST_SYS_geteuid:
		return geteuid();
	case GUEST_SYS_getgid:
		return getgid();
	case GUEST_SYS_getegid:
		return getegid();
	case GUEST_SYS_sched_yield:
		sched_yield();
		return 0;
	case GUEST_SYS_getrandom:
		return guest_getrandom((uint64_t)a, (uint32_t)b);
	case GUEST_SYS_uname:
		return guest_uname((uint64_t)a);
	case GUEST_SYS_prlimit64:
		return guest_prlimit((int)b, (uint64_t)d);
	case GUEST_SYS_getrlimit:
		return guest_prlimit((int)a, (uint64_t)b);
	case GUEST_SYS_sysinfo:
		return guest_sysinfo((uint64_t)a);
	case GUEST_SYS_membarrier:
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		return 0;

	case GUEST_SYS_kill:
	case GUEST_SYS_tkill:
	case GUEST_SYS_tgkill:
	{
		/* (the guest signals only itself: abort's SIGABRT, 6 on both) */
		int signal_number = (int)(number == GUEST_SYS_tgkill ? c : b);

		if (signal_number == 6)
			host_abort("the guest raised SIGABRT");
		return 0;
	}
	case GUEST_SYS_rt_sigprocmask:
		if (c)
			memset(GUEST(void *, c), 0, 8);
		return 0;
	case GUEST_SYS_rt_sigaction:
	case GUEST_SYS_sigaltstack:
		/* the host owns signal handling */
		return 0;

	case GUEST_SYS_clock_gettime:
	case GUEST_SYS_clock_getres:
	{
		struct timespec value;
		clockid_t clock;
		int result;

		if (!clock_in(a, &clock))
			return FAIL(EINVAL);
		result = number == GUEST_SYS_clock_gettime ? clock_gettime(clock, &value) : clock_getres(clock, &value);
		if (result == 0)
			timespec_out((uint64_t)b, &value);
		return result_of(result);
	}
	case GUEST_SYS_gettimeofday:
	{
		struct timespec value;
		struct halo_guest_timespec *result = GUEST(struct halo_guest_timespec *, a);

		clock_gettime(CLOCK_REALTIME, &value);
		if (result)
		{
			result->seconds = (int32_t)value.tv_sec;
			result->nanoseconds = (int32_t)(value.tv_nsec / 1000);
		}
		return 0;
	}
	case GUEST_SYS_nanosleep:
	case GUEST_SYS_clock_nanosleep:
	{
		struct timespec request, remaining;
		int absolute = number == GUEST_SYS_clock_nanosleep && ((int)b & LINUX_TIMER_ABSTIME);
		uint64_t request_address = number == GUEST_SYS_nanosleep ? (uint64_t)a : (uint64_t)c;
		uint64_t remaining_address = number == GUEST_SYS_nanosleep ? (uint64_t)b : (uint64_t)d;
		long result;

		if (!timespec_in(request_address, &request))
			return FAIL(EFAULT);
		if (absolute)
		{
			clockid_t clock;

			if (!clock_in(a, &clock))
				return FAIL(EINVAL);
			request = until(clock, &request);
		}
		result = result_of(nanosleep(&request, &remaining));
		if (result == -4 && !absolute)
			timespec_out(remaining_address, &remaining);
		return result;
	}
	case GUEST_SYS_futex:
		return guest_futex((uint64_t)a, (int)b, (uint32_t)c, (uint64_t)d, (uint64_t)e, (uint32_t)f);
	case GUEST_SYS_ppoll:
	{
		struct timespec timeout;
		int has_timeout = timespec_in((uint64_t)c, &timeout);
		int milliseconds = has_timeout ? (int)(timeout.tv_sec * 1000 + (timeout.tv_nsec + 999999) / 1000000) : -1;

		/* (struct pollfd and POLLIN to POLLNVAL are the same on both) */
		return result_of(poll(GUEST(struct pollfd *, a), (nfds_t)(uint32_t)b, milliseconds));
	}

	case GUEST_SYS_mmap:
		return host_guest_mmap((uint32_t)a, (uint32_t)b, (int)c, (int)d, (int)e, f);
	case GUEST_SYS_munmap:
		return host_guest_munmap((uint32_t)a, (uint32_t)b);
	case GUEST_SYS_mprotect:
		return host_guest_mprotect((uint32_t)a, (uint32_t)b, (int)c);
	case GUEST_SYS_madvise:
		return host_guest_madvise((uint32_t)a, (uint32_t)b, (int)c);
	case GUEST_SYS_mremap:
		/* (MREMAP_MAYMOVE is 1; MREMAP_FIXED is not taken) */
		if ((int)d & ~1)
			return FAIL(EINVAL);
		return host_guest_mremap((uint32_t)a, (uint32_t)b, (uint32_t)c, (int)d & 1);
	case GUEST_SYS_brk:
		return FAIL(ENOMEM);

	case GUEST_SYS_exit:
	case GUEST_SYS_exit_group:
		host_exit((int)a);

	case GUEST_SYS_ioctl:
		return -25 /* ENOTTY */;
	}
	return unsupported(number);
}
