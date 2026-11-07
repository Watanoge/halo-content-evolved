"""Content Extended's Forge (port/forge: the 3D Monitor editor, its runtime
tags, its .fmap cache builder and its network sync) in every build.

Forge takes over some of the game's functions: each one's definition and
the calls to it in its own source are renamed (orig_..., or Forge's own
replacements of the C library's file calls), and Forge's definition of the
name takes its place. Windows renames the symbols of the compiled objects with
llvm-objcopy (tools/windows_build.py); the other builds rename them in the
source as it is compiled, with -D (forge_source_flags), the same names. A few
of the game's networking sources see Forge's network version too
(forge_net_override.h), which keeps Forge's games apart from the others."""

import re
from pathlib import Path
from typing import Dict, List

FORGE_DIR = Path("port/forge")
FORGE_NET_OVERRIDE = FORGE_DIR / "include" / "forge_net_override.h"

# the renames, as llvm-objcopy takes them for Windows' 32-bit objects (a
# leading underscore, a stdcall function's @<bytes>)
FORGE_OBJCOPY_REDEFINES: Dict[str, List[str]] = {
    "port/linux/game/menu_functions.c": [
        "--redefine-sym", "_pc_menu_event_function_invoke=_orig_pc_menu_event_function_invoke",
        "--redefine-sym", "_pc_menu_game_data_function_invoke=_orig_pc_menu_game_data_function_invoke",
        "--redefine-sym", "_custom_edition_maps_look_again=_forge_menu_custom_edition_maps_look_again",
        "--redefine-sym", "_player_profile_get=_forge_player_profile_get",
    ],
    "port/linux/game/network_chat.c": [
        "--redefine-sym", "_network_chat_update=_orig_network_chat_update",
        "--redefine-sym", "_network_chat_draw=_orig_network_chat_draw",
        "--redefine-sym", "_network_chat_typing=_orig_network_chat_typing",
        "--redefine-sym", "_network_chat_handle_up=_orig_network_chat_handle_up",
        "--redefine-sym", "_network_chat_handle_down=_orig_network_chat_handle_down",
    ],
    "port/linux/game/network_objects.c": [
        "--redefine-sym", "_network_objects_handle_changes=_orig_network_objects_handle_changes",
    ],
    "source/camera/director.c": [
        "--redefine-sym", "_director_update=_orig_director_update",
        "--redefine-sym", "_director_get_perspective=_orig_director_get_perspective",
    ],
    "port/linux/game/render_interpolation.c": [
        "--redefine-sym", "_render_interpolation_camera=_orig_render_interpolation_camera",
    ],
    "source/interface/first_person_weapons.c": [
        "--redefine-sym", "_first_person_weapon_draw=_orig_first_person_weapon_draw",
    ],
    "source/interface/hud_draw.c": [
        "--redefine-sym", "_hud_draw_bitmap_direct=_orig_hud_draw_bitmap_direct",
        "--redefine-sym", "_hud_draw_numbers=_orig_hud_draw_numbers",
        "--redefine-sym", "_real_argb_color_to_pixel32=_orig_real_argb_color_to_pixel32",
    ],
    "source/interface/hud_weapon.c": [
        "--redefine-sym", "_hud_render_weapon_interface=_orig_hud_render_weapon_interface",
    ],
    "source/hs/hs.c": [
        "--redefine-sym", "_hs_compile_and_evaluate=_orig_hs_compile_and_evaluate",
    ],
    "source/hs/hs_runtime.c": [
        "--redefine-sym", "_hs_runtime_update=_orig_hs_runtime_update",
    ],
    "port/linux/game/mod_sounds.c": [
        "--redefine-sym", "_mod_sounds_read=_orig_mod_sounds_read",
    ],
    "port/linux/src/sdl_platform.c": [
        "--redefine-sym", "_platform_next_keystroke=_orig_platform_next_keystroke",
        "--redefine-sym", "_platform_ui_pointer_read=_orig_platform_ui_pointer_read",
        "--redefine-sym", "_platform_input_read=_orig_platform_input_read",
    ],
    "port/linux/src/xinput_sdl.c": [
        "--redefine-sym", "_XInputGetState@8=_orig_XInputGetState@8",
    ],
    "source/cutscene/cinematics.c": [
        "--redefine-sym", "_cinematic_start=_orig_cinematic_start",
    ],
    "source/scenario/scenario.c": [
        "--redefine-sym", "_scenario_trigger_volume_test_object=_orig_scenario_trigger_volume_test_object",
    ],
    "source/models/models.c": [
        "--redefine-sym", "_render_model=_orig_render_model",
    ],
    "source/game/game_engine.c": [
        "--redefine-sym", "_game_engine_post_rasterize_objects=_orig_game_engine_post_rasterize_objects",
        "--redefine-sym", "_game_engine_remap_object_definition=_orig_game_engine_remap_object_definition",
    ],
    "port/linux/game/menu_tags.c": [
        "--redefine-sym", "_menu_tags_loaded=_orig_menu_tags_loaded",
        "--redefine-sym", "_menu_tags_unloaded=_orig_menu_tags_unloaded",
    ],
    "port/linux/game/custom_edition_cache.c": [
        "--redefine-sym", "_file_reference_create_from_path=_forge_ce_file_reference_create_from_path",
        "--redefine-sym", "_halo_windows_fopen=_forge_ce_fopen",
        "--redefine-sym", "_halo_xbox_fopen=_forge_ce_fopen",
        "--redefine-sym", "_fopen=_forge_ce_fopen",
        "--redefine-sym", "_halo_xbox_FindFirstFileA@8=_forge_ce_FindFirstFileA@8",
        "--redefine-sym", "_halo_xbox_FindNextFileA@8=_forge_ce_FindNextFileA@8",
        "--redefine-sym", "_halo_xbox_CloseHandle@4=_forge_ce_CloseHandle@4",
    ],
    "port/linux/game/custom_edition_maps.c": [
        "--redefine-sym", "_file_reference_create_from_path=_forge_ce_file_reference_create_from_path",
        "--redefine-sym", "_halo_windows_fopen=_forge_ce_fopen",
        "--redefine-sym", "_halo_xbox_fopen=_forge_ce_fopen",
        "--redefine-sym", "_fopen=_forge_ce_fopen",
        "--redefine-sym", "_halo_xbox_FindFirstFileA@8=_forge_ce_FindFirstFileA@8",
        "--redefine-sym", "_halo_xbox_FindNextFileA@8=_forge_ce_FindNextFileA@8",
        "--redefine-sym", "_halo_xbox_CloseHandle@4=_forge_ce_CloseHandle@4",
        "--redefine-sym", "_fread=_forge_ce_fread",
        "--redefine-sym", "_fseek=_forge_ce_fseek",
        "--redefine-sym", "_ftell=_forge_ce_ftell",
        "--redefine-sym", "_fclose=_forge_ce_fclose",
    ],
    "port/linux/game/map_share_files.c": [
        "--redefine-sym", "_file_reference_create_from_path=_forge_ce_file_reference_create_from_path",
        "--redefine-sym", "_halo_windows_fopen=_forge_ce_fopen",
        "--redefine-sym", "_halo_xbox_fopen=_forge_ce_fopen",
        "--redefine-sym", "_fopen=_forge_ce_fopen",
        "--redefine-sym", "_halo_xbox_FindFirstFileA@8=_forge_ce_FindFirstFileA@8",
        "--redefine-sym", "_halo_xbox_FindNextFileA@8=_forge_ce_FindNextFileA@8",
        "--redefine-sym", "_halo_xbox_CloseHandle@4=_forge_ce_CloseHandle@4",
    ],
}

# the game's sources that ask for the network version through Forge's
# (forge_get_network_version: its own in a Forge game)
FORGE_NET_OVERRIDE_SOURCES = {
    "source/networking/network_server_message_handler.c",
    "source/networking/network_client_manager.c",
    "port/linux/game/menu_functions.c",
    "port/linux/src/p2p_lobby.c",
}
# the sources built as a release's with Forge (as Forge itself is)
FORGE_RELEASE_SOURCES = {"source/cseries/cseries.c", "source/cseries/cseries_windows.c"}


def forge_sources() -> List[Path]:
    """Forge's own sources"""
    return sorted((FORGE_DIR / "src").glob("*.c"))


def forge_renames(source: Path) -> List[tuple]:
    """the C names a source's functions are renamed from and to"""
    flags = FORGE_OBJCOPY_REDEFINES.get(source.as_posix(), [])
    pairs = [flag for flag in flags if flag != "--redefine-sym"]
    return [tuple(re.sub(r"^_|@\d+$", "", name) for name in pair.split("=")) for pair in pairs]


# the names the other builds' sources call what Windows' call by these (the
# port's Xbox file layer and C runtime: port/linux/src/xbox_files.c,
# msvc_crt.c), renamed alike
POSIX_NAMES = {
    "halo_xbox_FindFirstFileA": "FindFirstFileA",
    "halo_xbox_FindNextFileA": "FindNextFileA",
    "halo_xbox_CloseHandle": "CloseHandle",
    "halo_windows_fopen": "halo_linux_fopen",
}


def forge_source_flags(source: Path) -> str:
    """the flags a game or platform source is compiled with for Forge, in a
    build that renames in the source (-D): its renames (and their POSIX
    names'), the network version's override, a release's build"""
    renames = forge_renames(source)
    renames += [(POSIX_NAMES[old], new) for old, new in renames if old in POSIX_NAMES]
    flags = [f"-D{old}={new}" for old, new in renames]
    if source.as_posix() in FORGE_NET_OVERRIDE_SOURCES:
        flags.append(f"-include {FORGE_NET_OVERRIDE}")
    if source.as_posix() in FORGE_RELEASE_SOURCES:
        flags.append("-DHALO_RELEASE")
    return " ".join(flags)


def forge_cflags(game_cflags: str, zlib_dir: Path) -> str:
    """Forge's own sources' flags: the game's, its headers and zlib's"""
    return f"{game_cflags} -I{FORGE_DIR / 'include'} -I{zlib_dir} -DHALO_RELEASE"
