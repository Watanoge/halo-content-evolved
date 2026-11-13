/*
HOST_VK_DRIVER_MACOS.C

The Vulkan driver of the macOS build, in place of the Android host's
host_vk_driver.c (whose driver archives and libadrenotools a Mac has no use
for): MoltenVK, Vulkan on Metal, loaded from libMoltenVK.dylib beside the
executable (or in the app's Frameworks, or Homebrew's), without the Vulkan
loader: MoltenVK exports vkGetInstanceProcAddr itself. Its surface is made on
the game window's CAMetalLayer (VK_EXT_metal_surface, host_vk_present.c).
*/

#include "host.h"
#include "host_vk.h"
#include "host_vk_driver.h"

#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

void *host_vk_metal_layer;

static void *library;
static char library_path[1024];

PFN_vkGetInstanceProcAddr host_vk_driver_open(const char *setting, char *description, size_t size)
{
	const char *base = SDL_GetBasePath();
	char candidates[3][1024];
	PFN_vkGetInstanceProcAddr get;
	int index;

	if (setting && setting[0])
		host_logf(HOST_LOG_WARN, "vk driver: display.vk_driver is \"%s\"; macOS has one driver, MoltenVK", setting);
	if (!library)
	{
		snprintf(candidates[0], sizeof(candidates[0]), "%slibMoltenVK.dylib", base ? base : "");
		snprintf(candidates[1], sizeof(candidates[1]), "%s../Frameworks/libMoltenVK.dylib", base ? base : "");
		snprintf(candidates[2], sizeof(candidates[2]), "/opt/homebrew/opt/molten-vk/lib/libMoltenVK.dylib");
		for (index = 0; index < 3 && !library; index++)
		{
			library = dlopen(candidates[index], RTLD_NOW | RTLD_LOCAL);
			if (library)
				snprintf(library_path, sizeof(library_path), "%s", candidates[index]);
		}
	}
	if (!library)
	{
		snprintf(description, size, "MoltenVK not found (libMoltenVK.dylib beside the game): %s", dlerror());
		return NULL;
	}
	get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
	snprintf(description, size, "MoltenVK (%s)", library_path);
	return get;
}

int host_vk_driver_verify(char *text, size_t size)
{
	snprintf(text, size, "MoltenVK");
	return library != NULL;
}

void host_vk_driver_close(void)
{
}

/* the guest asks (the main menu shows a driver archive's name under the
version number): none, as for a phone's own driver */
uint32_t host_vk_driver_name(uint32_t out, uint32_t size)
{
	char *text = (char *)host_pointer(out);

	if (!text || !size)
		return 0;
	text[0] = 0;
	return 0;
}
