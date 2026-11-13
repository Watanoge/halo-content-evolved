/*
HOST_GAME_FOLDER_MACOS.C

The macOS build's game folder: the folder that holds the game's maps (maps,
the Xbox maps), Halo Custom Edition's (custom_maps) and MCC's (mcc_maps),
which the game takes as its data root (HALO_DATA_ROOT, paths.data:
port/linux/README.md). Maps take gigabytes, so the player chooses where it
is, on any drive (an external one too), before the game starts:

- the first time, or when the folder chosen is not there any more (its drive
  not connected), or when Option is held as the game starts: a dialog says
  what the folder is for, and macOS's folder panel picks it (or makes a new
  one: host_folder_panel_macos.m);
- maps, custom_maps and mcc_maps are made in it where they are not there;
- the choice is kept in game_folder.txt in the settings folder (below) and
  handed to the game as HALO_DATA_ROOT. A HALO_DATA_ROOT already set (a
  development run's) is used as it is, and nothing is asked.

The settings folder: inside the app (Halo Content Evolved.app,
tools/macos_build.py) ~/Library/Application Support/Halo Content Evolved,
which holds config.toml (HALO_CONFIG_FILE) and the saves (HALO_SAVE_ROOT),
as an app's own files may not be written inside it; outside the app (a
development build) the executable's folder, as on Linux. With no maps in
the game folder the game's first start offers to copy them out of a disc
image, and copies them into it (port/linux/src/xbox_files.c,
platform_data_root; port/linux/src/sdl_platform.c).
*/

#include "host.h"

#include <CoreGraphics/CoreGraphics.h>
#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FOLDER_NAME "Halo Content Evolved"

static char settings_folder[1024];

static int is_folder(const char *path)
{
	struct stat information;

	return path && path[0] && stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

/* whether the executable runs inside an app bundle */
static int in_bundle(void)
{
	const char *base = SDL_GetBasePath();

	return base && strstr(base, ".app/Contents/") != NULL;
}

static void make_folder(const char *path)
{
	if (mkdir(path, 0755) != 0 && errno != EEXIST)
		host_logf(HOST_LOG_WARN, "game folder: cannot make %s (%s)", path, strerror(errno));
}

/* ---------- the panel (host_folder_panel_macos.m: macOS's, which can make a
new folder) */

int host_macos_choose_folder(const char *start, char *path, size_t size);

static int choose_folder(const char *start, char *path, size_t size)
{
	char chosen[1024];

	if (!host_macos_choose_folder(is_folder(start) ? start : NULL, chosen, sizeof(chosen)))
		return 0;
	snprintf(path, size, "%s", chosen);
	return 1;
}

/* the dialog before the panel: 1 to choose, 0 to quit */
static int ask(const char *message, const char *choose)
{
	const SDL_MessageBoxButtonData buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, choose },
	};
	SDL_MessageBoxData data = { SDL_MESSAGEBOX_INFORMATION, NULL, "Halo: game folder", message, 2, buttons, NULL };
	int answer = 0;

	if (!SDL_ShowMessageBox(&data, &answer))
		return 0;
	return answer == 1;
}

/* ---------- the folder */

static void remembered_path(char *path, size_t size)
{
	snprintf(path, size, "%s/game_folder.txt", settings_folder);
}

static void read_remembered(char *folder, size_t size)
{
	char path[1100];
	FILE *file;

	folder[0] = 0;
	remembered_path(path, sizeof(path));
	file = fopen(path, "r");
	if (!file)
		return;
	if (fgets(folder, (int)size, file))
		folder[strcspn(folder, "\r\n")] = 0;
	fclose(file);
}

static void remember(const char *folder)
{
	char path[1100];
	FILE *file;

	remembered_path(path, sizeof(path));
	file = fopen(path, "w");
	if (!file)
	{
		host_logf(HOST_LOG_WARN, "game folder: cannot write %s (%s)", path, strerror(errno));
		return;
	}
	fprintf(file, "%s\n", folder);
	fclose(file);
}

/* the folder's maps, custom_maps and mcc_maps, made where they are not */
static void prepare(const char *folder)
{
	static const char *const names[] = { "maps", "custom_maps", "mcc_maps" };
	char path[1100];
	size_t index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
	{
		snprintf(path, sizeof(path), "%s/%s", folder, names[index]);
		make_folder(path);
	}
}

void host_macos_game_folder(void)
{
	const char *environment = getenv("HALO_DATA_ROOT");
	char folder[1024], message[1600];
	int option_held = (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskAlternate) != 0;

	/* the settings folder: Application Support's inside the app */
	if (in_bundle())
	{
		const char *home = getenv("HOME");
		char path[1100];

		snprintf(settings_folder, sizeof(settings_folder), "%s/Library/Application Support/" FOLDER_NAME,
			home && home[0] ? home : "/tmp");
		make_folder(settings_folder);
		snprintf(path, sizeof(path), "%s/config.toml", settings_folder);
		setenv("HALO_CONFIG_FILE", path, 0);
		snprintf(path, sizeof(path), "%s/saves", settings_folder);
		make_folder(path);
		setenv("HALO_SAVE_ROOT", path, 0);
	}
	else
	{
		const char *base = SDL_GetBasePath();

		snprintf(settings_folder, sizeof(settings_folder), "%s", base && base[0] ? base : ".");
		if (settings_folder[strlen(settings_folder) - 1] == '/')
			settings_folder[strlen(settings_folder) - 1] = 0;
	}

	if (environment && environment[0] && !option_held)
	{
		host_logf(HOST_LOG_INFO, "game folder: %s (HALO_DATA_ROOT)", environment);
		prepare(environment);
		return;
	}
	if (!SDL_Init(SDL_INIT_VIDEO))
		host_logf(HOST_LOG_WARN, "game folder: SDL's video did not start (%s)", SDL_GetError());

	read_remembered(folder, sizeof(folder));
	for (;;)
	{
		if (folder[0] && is_folder(folder) && !option_held)
			break;
		if (option_held)
			snprintf(message, sizeof(message),
				"The game folder is %s.\n\nChoose another? It holds the maps: maps (the Xbox maps), custom_maps "
				"(Halo Custom Edition's) and mcc_maps (MCC's), made in it if they are not there.",
				folder[0] ? folder : "not chosen yet");
		else if (folder[0])
			snprintf(message, sizeof(message),
				"The game folder %s is not there.\n\nIf it is on a drive that is not connected, connect it and "
				"start the game again; or choose another folder.", folder);
		else
			snprintf(message, sizeof(message),
				"Choose the game folder: the folder that holds Halo's maps, on any drive (maps take several "
				"gigabytes, so an external drive is fine).\n\nIt will hold maps (the Xbox maps), custom_maps "
				"(Halo Custom Edition's) and mcc_maps (MCC's), which are made in it if they are not there. "
				"Hold Option as the game starts to choose another.");
		if (!ask(message, "Choose Folder..."))
		{
			/* (Option held over a folder that is there: keep it) */
			if (option_held && folder[0] && is_folder(folder))
				break;
			host_logf(HOST_LOG_INFO, "game folder: none chosen; quitting");
			exit(0);
		}
		if (choose_folder(folder, folder, sizeof(folder)))
		{
			remember(folder);
			option_held = 0;
		}
	}
	prepare(folder);
	setenv("HALO_DATA_ROOT", folder, 1);
	host_logf(HOST_LOG_INFO, "game folder: %s (settings in %s)", folder, settings_folder);
}
