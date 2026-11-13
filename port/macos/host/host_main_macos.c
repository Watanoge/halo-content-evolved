/*
HOST_MAIN_MACOS.C

Entry point of the macOS build (tools/macos_build.py, port/macos/README.md).

The host is the Android port's (port/android/host), as the Linux desktop
guest builds' is (port/linux/host/host_main.c), with this file in place of
its entry point: the game is the Linux desktop build of it, compiled as
ILP32 AArch64 code for a base register (its 4 GB lie wherever the host
reserved them, host_memory_macos.c), with the Vulkan renderer, which runs on
MoltenVK (host_vk_driver_macos.c).

Cocoa's windows and events belong to the process's main thread, and the
game makes its window and pumps its events on its own main thread, so the
game runs on this one: on a stack in guest memory (guest code keeps stack
addresses in 32-bit registers), which host_run_on_stack moves it to.

The game finds its data, saves and config.toml as the Linux desktop builds
do (port/linux/README.md): config.toml beside the executable
(SDL_GetBasePath), HALO_DATA_ROOT the folder that holds maps/. The log is the
standard error stream.
*/

#include "host.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

/* guest_image.S */
extern const unsigned char halo_guest_image_start[];
extern const unsigned char halo_guest_image_end[];

/* host_game_folder_macos.c */
void host_macos_game_folder(void);

/* host_enter.S */
void host_run_on_stack(void (*function)(void *), void *argument, void *stack_top);

extern char **environ;

int host_renderer_vulkan;

#define MAIN_STACK_SIZE (16 * 1024 * 1024)
#define STACK_GUARD 0x4000

/* ---------- the log: the standard error stream */

static void log_to_stderr(int priority, const char *line)
{
	const char *level = priority >= HOST_LOG_FATAL ? "fatal: " : priority >= HOST_LOG_ERROR ? "error: " :
		priority >= HOST_LOG_WARN ? "warning: " : "";
	struct iovec parts[4];

	parts[0].iov_base = (void *)"halo-host: ";
	parts[0].iov_len = 11;
	parts[1].iov_base = (void *)level;
	parts[1].iov_len = strlen(level);
	parts[2].iov_base = (void *)line;
	parts[2].iov_len = strlen(line);
	parts[3].iov_base = (void *)"\n";
	parts[3].iov_len = 1;
	if (writev(2, parts, 4) < 0)
		return;
}

/* ---------- folders */

const char *host_macos_cache_folder(void)
{
	static char folder[1024];

	if (!folder[0])
	{
		const char *home = getenv("HOME");

		snprintf(folder, sizeof(folder), "%s/Library/Caches/HaloCE", home && home[0] ? home : "/tmp");
		if (mkdir(folder, 0755) != 0 && errno != EEXIST)
			host_logf(HOST_LOG_WARN, "cannot make the cache folder %s (%s)", folder, strerror(errno));
	}
	return folder;
}

/* ---------- the guest's environment */

static int environment_passes(const char *entry)
{
	static const char *const prefixes[] =
	{
		"HALO_", "HOME=", "XDG_", "USER=", "LANG=", "LC_", "TMPDIR=", "TMP=", "TEMP=",
	};
	size_t index;

	for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++)
	{
		if (!strncmp(entry, prefixes[index], strlen(prefixes[index])))
			return 1;
	}
	return 0;
}

/* ---------- main */

static uint32_t boot;

/* on the guest stack: the guest's main, which never returns */
static void game_main(void *unused)
{
	(void)unused;
	host_debug_thread_started();
	host_run_guest_main(boot);
}

int main(int argc, char *argv[])
{
	struct host_environment environment = { { 0 }, 0 };
	const char *base;
	char zone[64], path[1024], line[600];
	char **entry;
	char *stack;

	(void)argc;
	(void)argv;
	host_log_set_sink(log_to_stderr);
	/* (a write to a closed socket or pipe: an error, never the end of the game) */
	signal(SIGPIPE, SIG_IGN);
	host_install_signal_handlers();
	host_signal_stack_install();
	host_memory_reserve_early();
	/* the game folder (HALO_DATA_ROOT) and, inside the app, where the
	settings and saves go: before the guest's environment is taken */
	host_macos_game_folder();

	for (entry = environ; *entry; entry++)
	{
		if (environment_passes(*entry))
			host_environment_add(&environment, *entry);
	}
	host_time_zone(zone, sizeof(zone));
	host_environment_set(&environment, "TZ", zone);

	/* the renderer first: the Vulkan image is the only one (no OpenGL) */
	host_renderer_vulkan = host_vk_startup("", getenv("HALO_VK_VALIDATION") != NULL, line, sizeof(line));
	host_logf(HOST_LOG_INFO, "renderer: %s", line);
	if (!host_renderer_vulkan)
		host_fatal("Vulkan (MoltenVK) did not start: %s", line);

	if (host_load_image(halo_guest_image_start, (size_t)(halo_guest_image_end - halo_guest_image_start), NULL, 0) != 0)
		host_fatal("cannot load the game image");
	base = SDL_GetBasePath();
	if (getenv("HALO_CONFIG_FILE"))
		snprintf(path, sizeof(path), "%s", getenv("HALO_CONFIG_FILE"));
	else
		snprintf(path, sizeof(path), "%sconfig.toml", base ? base : "");
	/* (the write tracking compares pages' contents: a 16 KB page cannot
	carry each 4 KB guest page's protection, host_memory_macos.c) */
	host_apply_debug_settings(path, "macOS's 16 KB pages");
	host_vk_render_thread = 1;
	boot = host_make_boot(&environment, "halo");

	stack = host_low_map(MAIN_STACK_SIZE + STACK_GUARD, 3 /* PROT_READ | PROT_WRITE */);
	if (!stack)
		host_fatal("cannot allocate the game's stack");
	host_run_on_stack(game_main, NULL, stack + STACK_GUARD + MAIN_STACK_SIZE);
	host_fatal("the game returned");
	return 1;
}
