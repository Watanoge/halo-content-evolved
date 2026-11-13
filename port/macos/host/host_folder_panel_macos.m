/*
HOST_FOLDER_PANEL_MACOS.M

macOS's folder panel for the game folder (host_game_folder_macos.c): an
NSOpenPanel that chooses a folder and can make a new one (New Folder), which
SDL's folder dialog cannot. Run on the main thread, before the game starts.
*/

#import <AppKit/AppKit.h>

#include <stdio.h>

/* the folder chosen from start (or none), its path in path; 1 if one was */
int host_macos_choose_folder(const char *start, char *path, size_t size)
{
	@autoreleasepool
	{
		NSOpenPanel *panel = [NSOpenPanel openPanel];

		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		[NSApp activateIgnoringOtherApps:YES];
		panel.canChooseDirectories = YES;
		panel.canChooseFiles = NO;
		panel.canCreateDirectories = YES;
		panel.allowsMultipleSelection = NO;
		panel.title = @"Game Folder";
		panel.message = @"Choose or make the folder for Halo's maps (any drive). "
			"maps, custom_maps and mcc_maps are made in it.";
		panel.prompt = @"Use This Folder";
		if (start && start[0])
			panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:start] isDirectory:YES];
		if ([panel runModal] != NSModalResponseOK || !panel.URL)
			return 0;
		snprintf(path, size, "%s", panel.URL.fileSystemRepresentation);
		return 1;
	}
}
