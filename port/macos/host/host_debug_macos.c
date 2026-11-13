/*
HOST_DEBUG_MACOS.C

macOS's part of port/android/host/host_debug.c, which it replaces in the
macOS build (as the PS5's host_debug_ps5.c does there): the guest threads'
bookkeeping. Android's sampler and profiler (config.toml's
debug.sample_seconds and debug.profile_hz) signal each guest thread and read
/proc, which macOS has not; they answer "not on macOS" when asked
(Instruments samples the whole process instead).
*/

#include "host.h"

/* the guest threads running now, and the most at once */
static int threads_running, threads_most;

void host_debug_thread_started(void)
{
	int running = __atomic_add_fetch(&threads_running, 1, __ATOMIC_RELAXED);
	int most = __atomic_load_n(&threads_most, __ATOMIC_RELAXED);

	while (running > most && !__atomic_compare_exchange_n(&threads_most, &most, running, 0, __ATOMIC_RELAXED,
		__ATOMIC_RELAXED))
		;
}

void host_debug_thread_exited(void)
{
	__atomic_sub_fetch(&threads_running, 1, __ATOMIC_RELAXED);
}

void host_debug_start_sampler(const char *setting)
{
	host_logf(HOST_LOG_INFO, "debug.sample_seconds (%s): the sampler is not on macOS (use Instruments)",
		setting ? setting : "");
}

void host_debug_start_profiler(int hz, const char *data_root)
{
	(void)data_root;
	if (hz > 0)
		host_logf(HOST_LOG_INFO, "debug.profile_hz (%d): the profiler is not on macOS (use Instruments)", hz);
}

void host_debug_profile_frame(uint64_t nanoseconds)
{
	(void)nanoseconds;
}
