"""Firefight's menu (SOLO, MULTIPLAYER, FIREFIGHT VARIANTS), its rules' STARTING
SKULLS and TAG PROFILE rows, its browser's filter, and a variant's profile
shared by its host (tools/content_extended_menus.py, port_settings.py,
port/linux/game/menu_functions.c, firefight.c, firefight_spawner.c,
tag_multiplayer.c, randomizer.c, docs/tag_injector.md)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import content_extended_menus as menus  # noqa: E402
import port_settings  # noqa: E402

MENU_C = (ROOT / "port/linux/game/menu_functions.c").read_text()
FIREFIGHT_C = (ROOT / "port/linux/game/firefight.c").read_text()
SPAWNER_C = (ROOT / "port/linux/game/firefight_spawner.c").read_text()
SKULLS_C = (ROOT / "port/linux/game/skulls.c").read_text()
TAGS_C = (ROOT / "port/linux/game/tag_multiplayer.c").read_text()
RANDOMIZER_C = (ROOT / "port/linux/game/randomizer.c").read_text()
ENGINE_H = (ROOT / "source/game/game_engine.h").read_text()


def menu(name):
    return "\n".join(port_settings.multiplayer_files()[name])


def test_the_menus_are_as_the_generators_write_them():
    for name in ("main_menu.forge_select.port.xml", "main_menu.play.xml",
                 "main_menu.settings_select.multiplayer_setup.playlist_edit.firefight_edit.port.xml"):
        written = (ROOT / "port/assets/menus/ce" / name).read_text()
        assert written.rstrip("\n") == menu(name).rstrip("\n")


def test_play_opens_firefight_s_menu_and_its_rows_are_forge_s():
    assert 'run="gamemode select firefight" open="main_menu/firefight_select/firefight_menu"' in menu("main_menu.play.xml")
    rows = re.findall(r'firefight_select/(firefight_item_[a-z_]+)" x="\d+" y="\d+"', menu("main_menu.forge_select.port.xml"))
    assert rows == ["firefight_item_create_local", "firefight_item_multiplayer", "firefight_item_variants"]


def test_firefight_s_and_forge_s_menus_are_laid_out_as_campaign_s():
    """no background, header or picture: the logo and rows about the same
    middle (menu_item_frames: 130 of a 256 wide row, the rest half each way)"""
    forge = menu("main_menu.forge_select.port.xml")
    for screen, rows in (("main_menu/firefight_select/firefight_menu", "firefight_menu_select_list"),
                         ("main_menu/forge_select/forge_menu", "forge_menu_select_list")):
        widget = re.search(rf'<widget name="{screen}".*?</widget>', forge, re.S).group(0)
        assert "bitmap=" not in widget.split(">")[0] and "header_" not in widget and "blueline" not in widget
        assert 'widget="main_menu/halo_logo"' in widget
        items = re.search(rf'<widget name="[^"]*/{rows}".*?</widget>', forge, re.S).group(0)
        assert "description=" not in items and "button_bar" not in items
        assert 'widget="main_menu/campaign_select/menu_item_back"' in items
        for name, x in re.findall(r'child widget="([^"]+_item_[^"]+)" x="(\d+)"', items):
            if name.endswith("menu_item_back"):
                continue
            width = int(re.search(rf'<widget name="{name}"[^>]* width="(\d+)"', forge).group(1))
            assert int(x) + width / 2 == 320


def test_a_variant_s_picture_and_summary():
    """Slayer's figure, the crown if it has generators (frames, not engines:
    game_type_grafix's order is its own), and what the rules say of it"""
    assert "#define GAMETYPE_PICTURE_KING 1" in MENU_C and "#define GAMETYPE_PICTURE_SLAYER 2" in MENU_C
    assert "generator_count > 0 ? GAMETYPE_PICTURE_KING : GAMETYPE_PICTURE_SLAYER" in MENU_C
    assert MENU_C.count('text_set_length(named(description, "gametype_right_data", 0), rules, GAMETYPE_RULES_LENGTH)') == 2
    assert 'text_set(named(description, "gametype_right_data"' not in MENU_C
    for words in ("Sets: %s", "Rounds: %s", "Enemies: %s", "Objective: %s", "Skulls: %s"):
        assert words in MENU_C


def test_solo_is_a_local_game_by_its_name():
    """multiplayer_mode_set reads the mode from the item's name"""
    assert 'strstr(name, "create_local") ? _multiplayer_mode_host_local' in MENU_C
    solo = re.search(r'<widget name="main_menu/firefight_select/firefight_item_create_local".*?</widget>',
                     menu("main_menu.forge_select.port.xml"), re.S).group(0)
    assert 'run="join controller to mp game"' in solo and 'run="mp type set mode"' in solo


def test_firefight_s_multiplayer_lists_only_firefight_s_games():
    assert "BROWSER_SHOWS_ENGINE(game->engine_type)" in MENU_C  # (LAN, Direct Link)
    assert "BROWSER_SHOWS_ENGINE(games[read].engine_type)" in MENU_C  # (the server browser)
    assert "(!multiplayer_firefight_mode() || (engine) == game_engine_firefight)" in MENU_C


def test_each_page_s_help_matches_its_rows():
    """each spinner's choices and a help for each row that opens a screen,
    in order, and the guard's after ENEMY UNITS' (gametype_option_help)"""
    helps = menus.firefight_page_helps()
    assert len(helps) == len(menus.FIREFIGHT_PAGES)
    for page, texts in enumerate(helps):
        expected = sum(1 if setting in menus.FIREFIGHT_ACTION_ROWS else len(menus.FIREFIGHT_CHOICES[f"var_ff_{choices}"])
                       for row_page, _, setting, _, choices in menus.FIREFIGHT_ROWS if row_page == page)
        assert len(texts) == expected + (page == menus.FIREFIGHT_UNITS_PAGE)
    guard = int(re.search(r"#define FF_UNIT_GUARD_HELP_STRING_INDEX (\d+)", MENU_C).group(1))
    assert "CANNOT DISABLE" in helps[menus.FIREFIGHT_UNITS_PAGE][guard]


def test_the_rules_are_buttons_each_with_its_screen():
    xml = menu("main_menu.settings_select.multiplayer_setup.playlist_edit.firefight_edit.port.xml")
    assert "ff_page_spinner" not in xml + MENU_C
    hub = re.search(r'<widget name="[^"]*/firefight_options_menu".*?</widget>', xml, re.S).group(0)
    assert re.findall(r'/(op_ff_page_\d)"', hub) == [f"op_ff_page_{page}" for page in range(5)]
    for page in range(5):
        assert f'open="{menus.FIREFIGHT_EDIT}/firefight_page_{page}_screen"' in xml
        screen = re.search(rf'<widget name="[^"]*/firefight_page_{page}_menu".*?</widget>', xml, re.S).group(0)
        assert 'run="mp profile init firefight rules"' in screen
        assert len(re.findall(rf'/op_ff{page}_', screen)) == sum(row[0] == page for row in menus.FIREFIGHT_ROWS)


def test_game_options_goes_straight_to_a_firefight_variant_s_rules():
    edit = (ROOT / "port/assets/menus/ce/main_menu.settings_select.multiplayer_setup.playlist_edit.xml").read_text()
    assert 'run="mp edit game options" open=' in edit
    assert '"mp edit game options"' in (ROOT / "port/linux/game/menu_tags.c").read_text()
    opens = MENU_C[MENU_C.index("static boolean gametype_game_options_open("):]
    assert "ui_widget_port_open(widget, FIREFIGHT_RULES_SCREEN, widget_deleted);\n\treturn FALSE;" in opens
    assert 'strcmp(widget->name, "op_game_type") || gametype_game_options_open' in MENU_C


def test_the_row_values_are_the_widgets_the_code_sets():
    xml = menu("main_menu.settings_select.multiplayer_setup.playlist_edit.firefight_edit.port.xml")
    for key, run in menus.FIREFIGHT_ACTION_ROWS.items():
        assert f'/ff_{key}_value"' in xml and f'"ff_{key}_value"' in MENU_C
        assert f'run="{run}"' in xml and f'"{run}"' in MENU_C
        assert f'"{run}"' in (ROOT / "port/linux/game/menu_tags.c").read_text()


def test_the_rotation_s_words_are_its_values():
    """OFF once stored the value of Reach's standard rotation"""
    assert menus.FIREFIGHT_CHOICES["var_ff_prog_skulls"] == ["OFF (LOCKED)", "REACH STANDARD", "RANDOM / ROUND"]
    assert re.search(r"firefight_skull_progression_mode\), 0, 3,\s+\{ _firefight_skull_prog_off, "
                     r"_firefight_skull_prog_standard, _firefight_skull_prog_random_round \}", MENU_C)


def test_rules_never_write_into_the_round_skulls():
    """ENEMY HEALTH SCALE, SHIELD SCALE and BONUS DURATION were stored in the
    per-round skull overrides, which the progression then read as skulls"""
    assert "firefight_round_" not in ENGINE_H + MENU_C + FIREFIGHT_C
    assert "OPTIONS_FIELD(firefight_enemy_health_scale)" in MENU_C
    assert "firefight_bonus_seconds * TICKS_PER_SECOND" in FIREFIGHT_C
    assert "firefight_set_vitality_scale_get() * scale" in FIREFIGHT_C


def test_a_skull_on_from_the_start_is_ignored_by_the_rounds():
    assert "ff_state.round_skulls &= ~started;" in FIREFIGHT_C
    assert "ff_state.set_skulls &= ~started;" in FIREFIGHT_C
    assert "ff_state.temporary_skulls = combined & ~(started | ff_state.set_skulls);" in FIREFIGHT_C
    assert "pool &= ~firefight_started_skulls();" in FIREFIGHT_C


def test_firefight_s_skulls_are_drawn_plain_or_pending_never_lost():
    branch = SKULLS_C.index("if (firefight_active() && firefight_40_skull_active(skull))\n\t\t{\n\t\t\tif (firefight_40")
    assert branch < SKULLS_C.index("else if (skull_set_has(&level_set, skull))\n\t\t\toverlay = NONE;")
    assert "firefight_40_skull_temporary(skull)" in SKULLS_C[branch:branch + 200]
    assert "pkt.temporary_skulls = ff_state.temporary_skulls;" in (ROOT / "port/linux/game/firefight_network.c").read_text()


def test_a_variant_s_profile_is_shared_and_joins_the_game():
    assert "_played_firefight" in TAGS_C and "_share_firefight" in TAGS_C
    assert "host_firefight_profile(game);" in TAGS_C
    # (a variant chosen again takes it again: the game's setter forgets it)
    setter = TAGS_C[TAGS_C.index("void tag_multiplayer_game_profile_set("):]
    assert "firefight_profile_hash = 0UL" in setter[:setter.index("\n}")]
    assert "tag_multiplayer_firefight_level_profile(scenario_name)" in RANDOMIZER_C
    assert "scan_custom_tags();" in SPAWNER_C
    assert "pick_custom_enemy(" in SPAWNER_C and "pick_custom_weapon(" in SPAWNER_C


def test_built_in_variants_are_kept():
    """a built-in one changed asks for a new name, and saves as a new one"""
    player_ui = (ROOT / "source/interface/player_ui.c").read_text()
    assert "cannot save over default profiles; must rename and save-as a new profile" in player_ui
    events = (ROOT / "source/interface/ui_widget_event_handler_functions.c").read_text()
    assert "player_ui_edit_profile_is_default_profile() && !player_ui_edit_profile_name_is_dirty()" in events
    assert "gametype_edit_gather(&last);" in MENU_C


def test_hostile_marines_score_and_friendly_ones_still_cost():
    score = (ROOT / "port/linux/game/campaign_score.c").read_text()
    flip = score.index("firefight_team_is_hostile(unit->object.owner_team_index)")
    assert flip < score.index("if (base < 0)\n\t{\n\t\tif (killing_player_index != NONE)")
    assert "base = -base / 2;" in score
    helper = FIREFIGHT_C[FIREFIGHT_C.index("boolean firefight_team_is_hostile("):]
    assert "wave_faction == _firefight_faction_unsc_humans" in helper[:helper.index("\n}")]
    assert "game_team_is_enemy" not in helper[:helper.index("\n}")]  # (Woe Is Me's)


def test_server_setup_adds_to_the_variant_s_skulls_never_takes_them():
    settings = menu("main_menu.multiplayer_type_select.server_settings.xml")
    row = re.search(r'<widget name="[^"]*/op_ff_start_skulls".*?</widget>', settings, re.S).group(0)
    assert 'run="ss firefight skulls"' in row and "/ff_start_skulls_value" in row
    assert '"ss firefight skulls"' in (ROOT / "port/linux/game/menu_tags.c").read_text()
    # (its help, by index, after the section rows': the list's order)
    keys = [row[0] for row in port_settings.FIREFIGHT_SETUP_ROWS]
    base = int(re.search(r"#define FF_PLAYER_SCALE_HELP (\d+)", MENU_C).group(1))
    assert base + keys.index("ff_start_skulls") == int(re.search(r"#define FF_START_SKULLS_HELP (\d+)", MENU_C).group(1))
    # (the lock is what the gametype had as chosen, not the copy, which has
    # the host's own after a game)
    choose = MENU_C[MENU_C.index("static boolean gametype_choose("):]
    assert "firefight_game_skulls_remember();" in choose[:choose.index("\n}")]
    assert "gametype_edit.setup ? skull_set_intersection(firefight_game_skulls, skulls_menu_set)" in MENU_C
    # (every way of changing the set keeps it)
    assert "|| skull_set_has(&skulls_menu_locked, skull))" in MENU_C
    assert "struct skull_set chosen = skulls_menu_locked;" in MENU_C
    assert "skull_set_union(skulls_menu_locked, skull_set_is_empty(&on)" in MENU_C
    assert "skull_set_union(skulls_menu_set, skulls_menu_locked)" in MENU_C


def option_values(spinner):
    """the values of a rules' spinner, as menu_functions.c lists them"""
    row = re.search(rf'\{{ "{spinner}", .*?\{{(.*?)\}} \}},', MENU_C, re.S).group(1)
    return [value.strip() for value in row.split(",")]


def test_every_built_in_variant_s_settings_have_a_choice_to_load_into():
    # (gametype_option_index falls on the nearest choice, and OK saves that)
    profiles = FIREFIGHT_C[FIREFIGHT_C.index("built_in_firefight_profiles[NUMBER"):FIREFIGHT_C.index("/* ---------- skull mapping")]
    factions = option_values("ff_faction_spinner")
    for faction in set(re.findall(r"^\s+(_firefight_faction_\w+),$", profiles, re.M)):
        assert faction in factions
    numbers = re.findall(r"^\s+(-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+),$", profiles, re.M)
    assert len(numbers) == 8
    for place, spinner in enumerate(("ff_set_limit_spinner", "ff_starting_lives_spinner", "ff_round_lives_spinner",
                                     "ff_bonus_lives_spinner", "ff_max_lives_spinner", "ff_generators_spinner")):
        for row in numbers:
            assert row[place] in option_values(spinner), spinner


def test_each_firefight_spinner_has_as_many_values_as_choices():
    for _, _, setting, _, choices in menus.FIREFIGHT_ROWS:
        if setting not in menus.FIREFIGHT_ACTION_ROWS:
            spinner = f"ff_{setting}_spinner"
            count = int(re.search(rf'\{{ "{spinner}", .*?(\d+),\s+\{{', MENU_C, re.S).group(1))
            assert count == len(option_values(spinner)) == len(menus.FIREFIGHT_CHOICES[f"var_ff_{choices}"]), spinner


def test_the_defaults_show_as_what_they_are():
    escalation = option_values("ff_escalation_spinner")
    assert menus.FIREFIGHT_CHOICES["var_ff_escalation"][escalation.index("_firefight_escalation_medium")] == "+25% / SET"
    late = option_values("ff_r3_veh_spinner")
    assert menus.FIREFIGHT_CHOICES["var_ff_late_veh"][late.index("_firefight_late_vehicle_scorpion")] == "SCORPION"


def test_generator_attackers_default_to_half_of_a_wave_and_outweigh_the_players():
    attackers = option_values("ff_gen_attackers_spinner")
    assert menus.FIREFIGHT_CHOICES["var_ff_gen_attackers"][attackers.index("_firefight_generator_attackers_50")] == "50% OF UNITS"
    assert "_firefight_generator_attackers_50 = 0," in (ROOT / "port/linux/include/halo_firefight.h").read_text()
    perception = (ROOT / "source/ai/actor_perception.c").read_text()
    assert "firefight_actor_attacks_generator(actor_index, prop->unit_index)" in perception
    assert "generator_attacker_slot(i)" in SPAWNER_C


def test_forge_s_generator_objective_is_where_firefight_puts_its_generators():
    """first of the places, and never a place for enemies to spawn"""
    engine = (ROOT / "port/forge/include/forge_engine.h").read_text()
    tags = (ROOT / "port/forge/src/forge_runtime_tags.c").read_text()
    generators = (ROOT / "port/linux/game/firefight_generators.c").read_text()
    number = re.search(r"#define FORGE_NETGAME_FLAG_FIREFIGHT_GENERATOR (\d+)", engine).group(1)
    assert f"GENERATOR_NETGAME_FLAG_OBJECTIVE = {number}" in generators
    assert f"FF_NETGAME_FLAG_FIREFIGHT_GENERATOR = {number}" in SPAWNER_C
    assert 'objective\\\\firefight_generator", "[FF] Generator"' in tags
    placed = generators.index("flag->type == GENERATOR_NETGAME_FLAG_OBJECTIVE")
    assert placed < generators.index("scen->players.count && found < count")
    assert "flag->type != FF_NETGAME_FLAG_FIREFIGHT_GENERATOR" in SPAWNER_C


def test_forge_shows_the_generator_a_match_will_have():
    """the model Firefight spawns first, in the world and the palette, turned as it will stand"""
    tags = (ROOT / "port/forge/src/forge_runtime_tags.c").read_text()
    generators = (ROOT / "port/linux/game/firefight_generators.c").read_text()
    model = re.search(r"candidate_scenery\[\] = \{\s+\"([^\"]+)\"", generators).group(1)
    assert f"forge_resolve_engine_tag('scen', \"{model}\", NULL)" in tags
    assert "draw_native_object_model_at(generator_model, NONE, NULL, &pos, &fwd, &up, 0, 0, 0)" in tags
    assert "obj_def_idx = generator ? forge_generator_model_tag()" in tags
    assert "g_generator_facings[found] = flag->facing" in generators



def spinner_values(name):
    return re.search(rf'"{name}".*?\{{(.*?)\}}', MENU_C, re.S).group(1)


def test_wave_squad_sizes_run_in_order_up_to_300():
    assert menus.FIREFIGHT_CHOICES["var_ff_wave_size"][0].startswith("SMALL (50%)")
    assert menus.FIREFIGHT_CHOICES["var_ff_wave_size"][-1].endswith("(300%)")
    percents = [int(re.search(r"(\d+)%", label).group(1)) for label in menus.FIREFIGHT_CHOICES["var_ff_wave_size"]]
    assert percents == [50, 100, 150, 200, 250, 300]
    values = re.findall(r"_firefight_wave_size_(\d+)", spinner_values("ff_wave_size_spinner"))
    assert [int(value) for value in values] == percents


def test_floodfight_defaults_to_250_and_the_fiestas_to_random_enemy_weapons():
    assert "TRUE, _firefight_wave_size_250" in FIREFIGHT_C
    assert FIREFIGHT_C.count("FALSE, 0, _firefight_enemy_loadout_random") == 2


def test_vehicle_rows_are_the_values_their_labels_say():
    names = {"NONE": "none", "WARTHOG": "warthog", "ROCKET HOG": "rocket_warthog", "GHOST": "ghost",
             "SCORPION": "scorpion", "BANSHEE": "banshee", "WRAITH": "wraith"}
    for spinner in ("ff_r1_veh_spinner", "ff_r2_veh_spinner"):
        values = re.findall(r"_firefight_early_vehicle_(\w+)", spinner_values(spinner))
        assert values == [names[label] for label in menus.FIREFIGHT_CHOICES["var_ff_round_veh"]]
    assert "OPTIONS_FIELD(firefight_round2_vehicle)" in MENU_C


def test_enemy_loadout_is_on_enemy_units_page():
    rows = [row for row in menus.FIREFIGHT_ROWS if row[1] == "enemy_loadout"]
    assert rows and rows[0][0] == menus.FIREFIGHT_UNITS_PAGE
    assert menus.FIREFIGHT_CHOICES["var_ff_enemy_loadout"] == ["DEFAULT", "HUMAN", "COVENANT", "RANDOMIZED"]
    assert "op_ff2_enemy_loadout" in menu("main_menu.settings_select.multiplayer_setup.playlist_edit.firefight_edit.port.xml")


def test_the_map_vote_is_laid_out_as_the_lobby():
    lobby = menu("main_menu.multiplayer_type_select.lobby.xml")
    screen = re.search(r'<widget name="[^"]*map_vote/map_vote_screen".*?</widget>', lobby, re.S).group(0)
    assert "sel_list_desc_bkd" not in screen
    vote = re.search(r'<widget name="[^"]*map_vote/map_vote_list".*?</widget>', lobby, re.S).group(0)
    assert 'description="main_menu/multiplayer_type_select/lobby/lobby_desc"' in vote
    assert '!strcmp(widget->name, "map_vote_screen")' in MENU_C


def test_a_players_vehicle_goes_on_a_pad_or_the_maps_own_spots():
    body = SPAWNER_C.split("static boolean player_vehicle_place(", 1)[1].split("\n}\n", 1)[0]
    assert "calculate_safe_player_resupply_point" not in body
    assert "return map_vehicle_spot(vehicle_tag, out_position, out_facing);" in body


def test_the_players_vehicles_are_there_from_the_start_and_stay():
    """all three rows' at the second tick, made again at a round where one
    went, and none taken away at a set's end or the bonus round"""
    assert "if (ff_state.session_elapsed_ticks == 2)\n\t\t\tfirefight_spawn_player_vehicles();" in FIREFIGHT_C
    assert FIREFIGHT_C.count("firefight_spawn_player_vehicles();") == 2
    assert "despawn" not in FIREFIGHT_C and "firefight_despawn" not in SPAWNER_C
    body = SPAWNER_C.split("void firefight_spawn_player_vehicles(void)", 1)[1].split("\n}\n", 1)[0]
    assert "if (player_vehicle_present(ff_state.deployed_set_vehicles[slot]))" in body


def test_map_content_is_a_server_setup_section_not_a_variant_rule():
    """MAP CONTENT after GAME SETTINGS, its USE CONTENT and ADVANCED rows; a
    variant has none (saved or in the copy Server Setup starts from)"""
    assert not [row for row in menus.FIREFIGHT_ROWS if row[1] == "map_content"]
    assert "var_ff_map_content" not in menus.FIREFIGHT_CHOICES
    keys = [section[0] for section in port_settings.SERVER_SETUP_SECTIONS]
    assert keys.index("map_content") == keys.index("game_settings") + 1
    members = dict((section[0], section[3]) for section in port_settings.SERVER_SETUP_SECTIONS)["map_content"]
    assert members == ["ff_map_content", "ff_mc_enemies", "ff_mc_enemies_list", "ff_mc_weapons", "ff_mc_weapons_list",
                       "ff_mc_vehicles", "ff_mc_vehicles_list"]
    rows = dict((row[0], row[2]) for row in port_settings.FIREFIGHT_SETUP_ROWS)
    assert rows["ff_map_content"] == ["VANILLA ONLY", "INCLUDE MAP", "ONLY MAP"]
    assert rows["ff_mc_enemies"] == rows["ff_mc_weapons"] == rows["ff_mc_vehicles"] == ["ALL ON", "CUSTOM", "OFF"]
    assert '"op_lobby_settings", "op_game_settings", "op_map_content", "op_fork_settings"' in MENU_C
    assert "gametype_edit.setup_options.firefight_map_content = _firefight_map_content_vanilla;" in MENU_C
    profile = (ROOT / "source/saved games/playlist_profile.c").read_text()
    assert "options->firefight_map_content = _firefight_map_content_vanilla;" in profile
    assert "byte firefight_map_content;" in ENGINE_H and "firefight_last_man_boost" not in ENGINE_H


def test_server_setup_s_help_indices_match_its_strings():
    xml = menu("main_menu.multiplayer_type_select.server_settings.xml")
    block = re.search(r'<strings name="main_menu/multiplayer_type_select/server_settings/help_strings">(.*?)</strings>',
                      xml, re.S).group(1)
    helps = re.findall(r'text="([^"]*)"', block)
    for macro, row in (("FF_PLAYER_SCALE_HELP", "ff_player_scale"), ("SERVER_SCORING_HELP", "scoring"),
                       ("FF_START_SKULLS_HELP", "ff_start_skulls"), ("FF_MAP_CONTENT_HELP", "ff_map_content")):
        index = int(re.search(rf"#define {macro} (\d+)", MENU_C).group(1))
        expected = dict((r[0], r[3]) for r in port_settings.FIREFIGHT_SETUP_ROWS)[row]
        assert helps[index] == expected.replace("'", "&apos;") or helps[index] == expected
    section = int(re.search(r"#define SERVER_SETUP_SECTION_HELP (\d+)", MENU_C).group(1))
    assert helps[section + 2].startswith("Play the map")
    # (ADVANCED's kinds after USE CONTENT's, CUSTOM's CHOOSE rows after them:
    # menu_functions.c's FF_MAP_CONTENT_HELP + 1 + kind, + 4 + kind)
    base = int(re.search(r"#define FF_MAP_CONTENT_HELP (\d+)", MENU_C).group(1))
    by_key = dict((r[0], r[3]) for r in port_settings.FIREFIGHT_SETUP_ROWS)
    for kind, key in enumerate(("enemies", "weapons", "vehicles")):
        for offset, row in ((1, f"ff_mc_{key}"), (4, f"ff_mc_{key}_list")):
            assert helps[base + offset + kind].replace("&apos;", "'") == by_key[row]


def test_map_content_custom_lists_open_the_map_s_tags_kept_per_map():
    xml = menu("main_menu.multiplayer_type_select.server_settings.xml")
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    screen = "main_menu/multiplayer_type_select/server_settings/map_content/map_content_tags_screen"
    for kind in ("enemies", "weapons", "vehicles"):
        row = re.search(rf'<widget name="[^"]*/op_ff_mc_{kind}_list".*?</widget>', xml, re.S).group(0)
        assert f'run="ss map content {kind}" open="{screen}"' in row
        assert f"/ff_mc_{kind}_list_value" in row
        assert f'"ss map content {kind}"' in tags and f'"ss map content {kind}"' in MENU_C
    assert f'<widget name="{screen}"' in xml
    for run in ("map content tags init", "map content tag toggle", "map content tags all on",
                "map content tags all off", "map content tags update"):
        assert f'"{run}"' in tags and f'"{run}"' in MENU_C
    assert xml.count('run="map content tag toggle"') == 2 * port_settings.MAP_CONTENT_TAG_ROWS
    assert f"#define MAP_CONTENT_TAG_ROWS {port_settings.MAP_CONTENT_TAG_ROWS}" in MENU_C
    spawner = (ROOT / "port/linux/game/firefight_spawner.c").read_text()
    # (kept per map in a file of the host's, and played by the host alone)
    assert '#define FF_MC_FILE "d:\\\\firefight_map_content.txt"' in spawner
    for kind in ("ENEMIES", "WEAPONS", "VEHICLES"):
        assert f"map_content_custom_off(FIREFIGHT_MAP_CONTENT_{kind}_CUSTOM" in spawner
    header = (ROOT / "port/linux/include/halo_firefight.h").read_text()
    bits = [int(value, 16) for value in re.findall(r"#define FIREFIGHT_MAP_CONTENT_\w+_(?:OFF|CUSTOM) (0x[0-9A-F]+)", header)]
    mask = int(re.search(r"#define FIREFIGHT_MAP_CONTENT_MODE_MASK (0x[0-9A-F]+)", header).group(1), 16)
    assert len(bits) == 6 and len(set(bits)) == 6 and not any(bit & mask for bit in bits)


def test_the_ordnance_drops_in_a_pod_every_machine_shows():
    network = (ROOT / "port/linux/game/firefight_network.c").read_text()
    assert "pkt.ordnance_pod_drop_time = ff_state.ordnance_pod_drop_time;" in network
    assert "size < (long)FIREFIGHT_NET_PACKET_BEFORE_POD" in network
    # (before the client's return: the pod is each machine's own scenery)
    update = FIREFIGHT_C.split("static void firefight_update(", 1)[1]
    assert update.index("firefight_ordnance_pod_update();") < update.index("if (network_game_distributed_client())")
    assert '"scenery\\\\c_storage\\\\c_storage"' in RANDOMIZER_C
    # (where a pod lands: map ground, seen, clear round, open sky; its weapons
    # held on its sides, full)
    for rule in ("if (collision.object_index != NONE)", "if (collision.plane.n.k < 0.8f)",
                 "if (pod_ray(structure, eye, &vector, &collision) && collision.t < 0.98f)",
                 "collision.t * FF_POD_FALL_HEIGHT < sky"):
        assert rule in SPAWNER_C
    assert "pod_weapon_fill(object_index);" in SPAWNER_C and "pod_weapons_hold(FALSE);" in SPAWNER_C
    assert "_ammo" not in SPAWNER_C.split("void firefight_spawn_ordnance_drop(", 1)[1].split("\n}\n", 1)[0]
    modes = (ROOT / "port/assets/tag_packs/watanoge_gamemodes.toml").read_text()
    assert '"scenery\\\\c_storage\\\\"' in modes


def test_a_waves_zones_stay_near_the_players():
    body = SPAWNER_C.split("static short select_wave_spawn_zones(", 1)[1].split("/* ---------- AI Actor Creation", 1)[0]
    # (Forge's markers as well as the map's own points)
    assert body.count("zones_keep_near(") == 2


def test_a_destroyed_vehicle_stays_as_a_wreck():
    assert "if (!is_wreck && !is_player_set_vehicle &&" in SPAWNER_C
    # (off the garbage, or the first collection takes it once no one looks)
    assert "object_set_garbage(vehicle_index, FALSE);" in SPAWNER_C.split("static void wreck_keep(", 1)[1].split("\n}\n", 1)[0]
    # (one of the waves' whose driver died goes up; an empty one of the map's doesn't)
    assert "if (enemy_vehicle_noted(dead_vehicles[i]) && vehicle_has_wreck_model(dead_vehicles[i]))" in SPAWNER_C
    assert SPAWNER_C.count("enemy_vehicle_note(vehicle_index);") == 2
    assert "firefight_wrecks_reset();" in FIREFIGHT_C
    assert '"vehicles\\\\ghost\\\\ghost",\n\t\t"vehicles\\\\ghost\\\\ghost_mp",\n\t\t"vehicles\\\\scorpion' in RANDOMIZER_C


def test_an_automatic_dropship_flies_straight_down_and_up():
    route = (ROOT / "port/linux/include/halo_firefight_route.h").read_text()
    assert "if (!has_route)\n\t\tff_route_flight_automatic(" in SPAWNER_C
    assert route.count("= FF_ROUTE_STYLE_SKY;") == 2


def test_a_firefight_game_takes_sixteen_bots_and_coop_three():
    header = (ROOT / "port/linux/include/halo_firefight.h").read_text()
    assert "FIREFIGHT_MAX_BOTS = 16," in header
    assert "short max_pve_bots = hosting_cooperative() ? 3 : FIREFIGHT_MAX_BOTS;" in MENU_C
    assert "pve_bots ? (long)FIREFIGHT_MAX_BOTS : (long)BOTS_MAXIMUM" in MENU_C


def test_map_content_custom_list_previews_each_tag_s_model():
    xml = menu("main_menu.multiplayer_type_select.server_settings.xml")
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    # (a widget named tag_preview: ui_widget.c draws tag_preview_picture in it)
    assert '<widget name="main_menu/multiplayer_type_select/server_settings/map_content/tag_preview"' in xml
    assert '<on event="deleted" run="map content tags close"/>' in xml
    assert '"map content tags close"' in tags and '"map content tags close"' in MENU_C
    assert "tag_injector_map_named(server_settings_map_name())" in MENU_C
    assert "tag_preview_map_tag(category, index);" in MENU_C
    injector = (ROOT / "port/linux/game/tag_injector.h").read_text()
    assert "short tag_injector_map_named(" in injector


def test_fiesta_s_random_weapons_take_the_map_s_pool():
    firefight = (ROOT / "port/linux/game/firefight.c").read_text()
    random = firefight[firefight.index("case _loadout_weapon_random:"):]
    random = random[:random.index("case _loadout_weapon_assault_rifle:")]
    assert random.index("firefight_map_loadout_weapon(other)") < random.index("s_ff_loadout_weapon_paths[index]")
    spawner = (ROOT / "port/linux/game/firefight_spawner.c").read_text()
    body = spawner[spawner.index("char const *firefight_map_loadout_weapon(char const *other)"):]
    assert "mode == _firefight_map_content_only_map" in body[:body.index("\n}\n")]
