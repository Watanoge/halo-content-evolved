"""Content Extended's menus (SutlabSol's fork of this port: Firefight,
Forge), which port_settings.multiplayer_files writes into files of their own
beside the PC version's (content_extended_files):

- the gametype editor's FIREFIGHT (the list's row after RACE's: its patch,
  port_settings.WIDGET_PATCHES) and Firefight's rules, a screen of five
  buttons (FIREFIGHT_PAGES), each with its own screen of rows (FIREFIGHT_ROWS:
  "mp profile init/set firefight rules", its option table's ff_ rows);
- Firefight's menu (FIREFIGHT_MENU: SOLO, MULTIPLAYER, its variants), as
  Forge's is;
- Multiplayer's headers for the mode chosen (STANDARD, FIREFIGHT
  or FORGE: menu_functions.c's "mp type screen update") and Forge's menus,
  whose screens port/forge (forge_hooks.c) runs: FORGE_WIDGETS, as the fork
  laid them out, with the Tag Injector in the fork's tag screens' place.

Their words are the fork's."""

from xml.sax.saxutils import quoteattr

FIREFIGHT_EDIT = "main_menu/settings_select/multiplayer_setup/playlist_edit/firefight_edit"
GAMETYPE_SELECT = "main_menu/settings_select/multiplayer_setup/playlist_edit/game_type_select"
# the Tag Injector's screen, which Forge's menu opens (port_settings.py writes it)
TAG_INJECTOR_SCREEN = ("main_menu/settings_select/player_setup/player_profile_edit/gameplay/"
                       "tag_injector/tag_injector_screen")

# Firefight's menu
FIREFIGHT_SELECT = "main_menu/firefight_select"
MULTIPLAYER_SCREEN = "main_menu/multiplayer_type_select/multiplayer_type_select_screen"
VARIANTS_SCREEN = "main_menu/settings_select/multiplayer_setup/playlist_select/gametype_select_screen"
LOCAL_MAP_SCREEN = "main_menu/multiplayer_type_select/connected/connected_map_select_wrapper"
# the rules' buttons, each its page of FIREFIGHT_ROWS, and the units' page
FIREFIGHT_PAGES = ['WAVE & LIVES', 'FACTIONS & AI', 'ENEMY UNITS', 'VEHICLES & DROPS', 'SKULLS & TRAITS']
FIREFIGHT_UNITS_PAGE = 2
# the rows that open a screen over the rules (no choices of their own):
# what A runs (menu_functions.c's, which shows each its value)
FIREFIGHT_ACTION_ROWS = {'start_skulls': 'firefight skulls open', 'tag_profile': 'firefight tag profile open'}

# the rules' rows: their page, the row's key (op_ff<page>_<key>), the
# setting's (ff_<key>_label and _spinner, menu_functions.c's), its label's
# index in FIREFIGHT_LABELS and its choices' list (var_ff_<list>)
FIREFIGHT_ROWS = [(0, 'set_limit', 'set_limit', 1, 'set_limit'),
 (0, 'start_lives', 'starting_lives', 2, 'starting_lives'),
 (0, 'round_lives', 'round_lives', 3, 'round_lives'),
 (0, 'bonus_lives', 'bonus_lives', 4, 'bonus_lives'),
 (0, 'max_lives', 'max_lives', 5, 'max_lives'),
 (0, 'prep_time', 'prep_time', 6, 'prep_time'),
 (0, 'wave_limit', 'wave_limit', 7, 'wave_limit'),
 (0, 'bonus_round', 'bonus_round', 8, 'yes_no'),
 (0, 'bonus_dur', 'bonus_dur', 9, 'bonus_dur'),
 (1, 'faction', 'faction', 10, 'faction'),
 (1, 'allegiance', 'allegiance', 11, 'allegiance'),
 (1, 'difficulty', 'difficulty', 12, 'difficulty'),
 (1, 'escalation', 'escalation', 13, 'escalation'),
 (1, 'enemy_hp', 'enemy_hp', 14, 'vitality'),
 (1, 'enemy_shd', 'enemy_shd', 15, 'vitality'),
 (1, 'wave_size', 'wave_size', 16, 'wave_size'),
 (1, 'boss_freq', 'boss_freq', 17, 'boss_freq'),
 (2, 'enemy_loadout', 'enemy_loadout', 18, 'enemy_loadout'),
 (2, 'u_grunts', 'u_grunts', 19, 'on_off'),
 (2, 'u_jackals', 'u_jackals', 20, 'on_off'),
 (2, 'u_elites', 'u_elites', 21, 'on_off'),
 (2, 'u_specops', 'u_specops', 22, 'on_off'),
 (2, 'u_zealots', 'u_zealots', 23, 'on_off'),
 (2, 'u_hunters', 'u_hunters', 24, 'on_off'),
 (2, 'u_flood_c', 'u_flood_c', 25, 'on_off'),
 (2, 'u_flood_k', 'u_flood_k', 26, 'on_off'),
 (2, 'u_sents', 'u_sents', 27, 'on_off'),
 (3, 'dropships', 'dropships', 28, 'dropships'),
 (3, 'boss_veh', 'boss_veh', 29, 'boss_veh'),
 (3, 'ordnance', 'ordnance', 30, 'ordnance'),
 (3, 'r1_veh', 'r1_veh', 31, 'round_veh'),
 (3, 'r2_veh', 'r2_veh', 32, 'round_veh'),
 (3, 'r3_veh', 'r3_veh', 33, 'late_veh'),
 (3, 'allies', 'allies', 34, 'allies'),
 (3, 'generators', 'generators', 35, 'generators'),
 (3, 'gen_fail', 'gen_fail', 36, 'gen_fail'),
 (3, 'gen_attackers', 'gen_attackers', 49, 'gen_attackers'),
 (4, 'prog_skulls', 'prog_skulls', 37, 'prog_skulls'),
 (4, 'start_skulls', 'start_skulls', 38, None),
 (4, 'primary', 'primary', 50, 'loadout_weapon'),
 (4, 'secondary', 'secondary', 51, 'loadout_weapon'),
 (4, 'shields', 'shields', 40, 'shields'),
 (4, 'speed', 'speed', 41, 'speed'),
 (4, 'gravity', 'gravity', 42, 'gravity'),
 (4, 'ammo', 'ammo', 43, 'ammo'),
 (4, 'gen_hp', 'gen_hp', 44, 'gen_hp'),
 (4, 'tag_profile', 'tag_profile', 45, None)]
# the rows' labels
FIREFIGHT_LABELS = ['',
 'SET LIMIT:',
 'STARTING LIVES:',
 'BONUS LIVES / ROUND:',
 'BONUS LIVES / SET:',
 'MAX LIVES CAP:',
 'PRE-ROUND PREP TIME:',
 'WAVE TIME LIMIT:',
 'BONUS ROUND:',
 'BONUS DURATION:',
 'ENEMY FACTION:',
 'FACTION ALLEGIANCE:',
 'AI DIFFICULTY:',
 'PER-SET ESCALATION:',
 'ENEMY HEALTH SCALE:',
 'ENEMY SHIELD SCALE:',
 'WAVE SQUAD SIZE:',
 'BOSS WAVE FREQUENCY:',
 'ENEMY LOADOUT:',
 'COVENANT GRUNTS:',
 'COVENANT JACKALS:',
 'COVENANT ELITES:',
 'STEALTH / SPECOPS:',
 'ZEALOT COMMANDERS:',
 'COVENANT HUNTERS:',
 'FLOOD COMBAT FORMS:',
 'FLOOD CARRIER / INF:',
 'SENTINELS & HUMANS:',
 'WAVE INSERTION:',
 'ENEMY BOSS VEHICLES:',
 'ORDNANCE AMMO DROPS:',
 'ROUND 1 VEHICLE:',
 'ROUND 2 VEHICLE:',
 'ROUND 3 VEHICLE:',
 'AI REINFORCEMENTS:',
 'GENERATORS:',
 'GENERATOR FAIL MODE:',
 'SKULL ROTATION MODE:',
 'STARTING SKULLS:',
 'PLAYER SPECIES:',
 'PLAYER SHIELDS:',
 'PLAYER SPEED:',
 'PLAYER GRAVITY:',
 'INFINITE AMMO:',
 'GENERATOR HEALTH:',
 'TAG PROFILE:',
 'STATS CATEGORY:',
 'WEB & JSON SYNC:',
 'SERVICE RECORD',
 'GENERATOR ATTACKERS:',
 'PRIMARY WEAPON:',
 'SECONDARY WEAPON:']
# the help of the rules' buttons, then of each row's choices, in order
# (firefight_page_helps), then of the units' guard
FIREFIGHT_HELP = ['Configure Set limits, shared team lives,\\nprep countdowns, and Bonus Rounds.',
 'Configure enemy factions, allegiances,\\nAI combat difficulty, and per-Set scaling.',
 'Enable or disable individual Covenant,\\nFlood, Sentinel, and Human enemy unit types.',
 'Configure Wave Insertion beacons, Boss\\nvehicles, Round 1-3 vehicles, and Generators.',
 'Configure Skull rotation, starting skulls, the\\nSpartan loadout and traits, and the Tag Injector.',
 'Play endless sets until all defenders fall.',
 'Complete 1 Set (3 Rounds + Bonus Round) to win.',
 'Complete 2 Sets for victory.',
 'Complete 3 Sets for victory.',
 'Complete 5 Sets for victory.',
 'Complete 10 Sets for an endurance victory.',
 'Unlimited shared team lives.',
 'Start with 0 reserve lives (Last Man Standing).',
 'Start with 3 shared team lives.',
 'Start with 5 shared team lives.',
 'Start with 7 shared team lives (Reach default).',
 'Start with 10 shared team lives.',
 'Start with 15 shared team lives.',
 'Start with 20 shared team lives.',
 'No bonus lives awarded upon completing a Round.',
 'Award +1 shared team life after each Round.',
 'Award +2 shared team lives after each Round.',
 'Award +3 shared team lives after each Round.',
 'Award +5 shared team lives after each Round.',
 'No bonus lives awarded upon completing a Set.',
 'Award +1 shared team life after each Set.',
 'Award +2 shared team lives after each Set.',
 'Award +3 shared team lives after each Set.',
 'Award +5 shared team lives after each Set.',
 'Award +10 shared team lives after each Set.',
 'No upper cap on earned shared team reserve lives.',
 'Cap shared team reserve lives at 5.',
 'Cap shared team reserve lives at 10.',
 'Cap shared team reserve lives at 15.',
 'Cap shared team reserve lives at 20.',
 'Cap shared team reserve lives at 30.',
 'Cap shared team reserve lives at 50.',
 '5-second prep countdown before each Round begins.',
 '15-second prep countdown to collect ammo and gear.',
 'Short prep countdown, 5 to 20 seconds, longer\\nwhen vehicles are deployed.',
 '45-second prep countdown to fortify positions.',
 '60-second prep countdown to fortify positions.',
 'Next wave spawns when 2 or fewer enemies remain.',
 'Force-spawn the next wave after 60 seconds.',
 'Force-spawn the next wave after 90 seconds.',
 'Force-spawn the next wave after 120 seconds.',
 'Next wave spawns only when every enemy is dead.',
 'Enable the high-value Bonus Round after Round 3\\nof each Set.',
 'Skip the Bonus Round between Sets.',
 '30-second Bonus Round timer.',
 '45-second Bonus Round timer.',
 '60-second Bonus Round timer (Reach default).',
 '90-second Bonus Round timer.',
 '120-second Bonus Round timer.',
 'Spawn Covenant waves (Grunts, Jackals, Elites,\\nand Hunters).',
 'Spawn nothing but Covenant Grunts.',
 'Spawn Covenant Elites and Hunters, with no Grunts\\nor Jackals.',
 'Rotate Covenant, Flood, and Sentinels across Rounds.',
 'Spawn Flood waves (Infection, Combat, and Carrier\\nForms).',
 'Spawn Forerunner Sentinel and Enforcer waves.',
 'Spawn mixed Covenant and Flood waves together.',
 'Spawn mixed Covenant and Sentinel waves together.',
 'Spawn mixed Flood and Sentinel waves together.',
 'Fight UNSC Marines, ODSTs, Snipers, and Rocket\\nteams.',
 'Covenant, Flood, and Sentinels ally together\\nagainst the players.',
 'Enemy factions fight both the players and each\\nother (Multi-Team Chaos).',
 'Easy AI combat difficulty.',
 'Normal AI combat difficulty.',
 'Heroic AI combat difficulty (Reach standard).',
 'Legendary AI combat difficulty.',
 'Enemy health and shields increase by +25% after\\neach completed Set.',
 'Disable per-Set enemy health and shield escalation.',
 'Enemy health and shields increase by +15% after\\neach completed Set.',
 'Enemy health and shields increase by +40% after\\neach completed Set.',
 'Enemy health and shields increase by +60% after\\neach completed Set.',
 '100% base enemy body health.',
 '50% base enemy body health.',
 '150% base enemy body health.',
 '200% base enemy body health.',
 '300% base enemy body health.',
 '100% base enemy energy shield strength.',
 '50% base enemy energy shield strength.',
 '150% base enemy energy shield strength.',
 '200% base enemy energy shield strength.',
 '300% base enemy energy shield strength.',
 'Small squad size (50% enemy count for solo play).',
 'Normal squad size (100% enemy count per wave).',
 'Large squad size (150% enemy count per wave).',
 'Massive Horde squad size (200% enemy count).',
 'Swarm squad size (250% enemy count).',
 'Legion squad size (300% enemy count).',
 'Boss squads spawn on Wave 5 of each Round\\n(Reach standard).',
 'Boss squads spawn on Waves 3 and 5 of each Round.',
 'Boss Rush: Every wave spawns heavy commanders,\\nHunters, and vehicles!',
 'Enemies wield their standard faction weapons.',
 'Enemies wield any human weapon they can hold.',
 'Enemies wield any Covenant weapon they can hold.',
 'Fiesta: Every enemy spawns with a random UNSC\\nor Covenant weapon!',
 'Allow Covenant Grunts (Minor, Major, Heavy) to\\nspawn.',
 'Disable Covenant Grunts from spawning.',
 'Allow Covenant Shield Jackals to spawn.',
 'Disable Covenant Shield Jackals from spawning.',
 'Allow Covenant Elites (Minor, Major) to spawn.',
 'Disable standard Covenant Elites from spawning.',
 'Allow Stealth and SpecOps Elites to spawn.',
 'Disable Stealth and SpecOps Elites from spawning.',
 'Allow Zealot Field Marshals and Sword Commanders\\nto spawn.',
 'Disable Zealot Field Marshals from spawning.',
 'Allow Covenant Hunters to spawn in heavy and\\nboss waves.',
 'Disable Covenant Hunters from spawning.',
 'Allow Flood Elite and Human Combat Forms to spawn.',
 'Disable Flood Combat Forms from spawning.',
 'Allow Explosive Flood Carrier and Infection Forms\\nto spawn.',
 'Disable Flood Carrier and Infection Forms.',
 'Allow Forerunner Sentinels and UNSC Marine enemy\\nunits to spawn.',
 'Disable Forerunner Sentinels and UNSC Marine\\nenemy units.',
 'Insert enemy squads with Covenant Teleportation\\nbeacons at each active wave zone.',
 'Spawn enemy squads directly at ground zones\\nwith no insertion beacon effect.',
 'Highlight wave insertion zones with Covenant\\nPlasma Orbital Beacons.',
 'Highlight wave insertion zones with Flood Spore\\nBurst effects.',
 'Deploy enemy Wraiths and Banshees during Boss\\nWaves.',
 'Disable enemy Boss Wave combat vehicles.',
 'Deploy enemy Covenant Wraith mortar tanks on\\nBoss Waves.',
 'Deploy enemy Covenant Banshees on Boss Waves.',
 'Deploy enemy Scorpion tanks on Boss Waves.',
 'Disable UNSC weapon and ammo resupply drops.',
 'Drop resupply weapons at the start of each Set.',
 'Drop precision and heavy ordnance at the start\\nof each Round and Boss Wave.',
 'Drop double heavy anti-armor weapon crates every\\nRound and Boss Wave.',
 'No player vehicle deployed at Round 1.',
 'Deploy a Chain Gun Warthog at Round 1\\n(despawns at Set end).',
 'Deploy a Rocket Warthog at Round 1\\n(despawns at Set end).',
 'Deploy a Covenant Ghost at Round 1\\n(despawns at Set end).',
 'Deploy a Scorpion Tank at Round 1\\n(despawns at Set end).',
 'Deploy a Covenant Banshee at Round 1\\n(despawns at Set end).',
 'Deploy a drivable Wraith at Round 1\\n(despawns at Set end).',
 'No player vehicle deployed at Round 2.',
 'Deploy a Chain Gun Warthog at Round 2\\n(despawns at Set end).',
 'Deploy a Rocket Warthog at Round 2\\n(despawns at Set end).',
 'Deploy a Covenant Ghost at Round 2\\n(despawns at Set end).',
 'Deploy a Scorpion Tank at Round 2\\n(despawns at Set end).',
 'Deploy a Covenant Banshee at Round 2\\n(despawns at Set end).',
 'Deploy a drivable Wraith at Round 2\\n(despawns at Set end).',
 'No player vehicle deployed at Round 3.',
 'Deploy a Chain Gun Warthog at Round 3\\n(despawns at Set end).',
 'Deploy a Scorpion Tank at Round 3\\n(despawns at Set end).',
 'Deploy a Covenant Banshee at Round 3\\n(despawns at Set end).',
 'Deploy a drivable Wraith at Round 3\\n(despawns at Set end).',
 'No friendly AI squadmates.',
 'Deploy 2 friendly AI Marines (or Elites) at the\\nstart of each Set.',
 'Deploy 4 friendly AI Marines (or Elites) at the\\nstart of each Set.',
 'Deploy armored ODSTs / SpecOps Elites with heavy\\nweapons each Set.',
 'Reinforce friendly AI squadmates at the start of\\nevery Round.',
 'Standard survival Firefight with no generators\\nto defend.',
 'Defend 1 UNSC Generator (ALPHA) from enemy\\nassault.',
 'Defend 2 UNSC Generators (ALPHA, BETA) from\\nenemy assault.',
 'Defend 3 UNSC Generators (ALPHA, BETA, GAMMA)\\nwith 30s Lockdown switches.',
 'Defeat only when all generators are destroyed.',
 'Defeat immediately if any single generator is\\ndestroyed.',
 'Every enemy fights the players; generators are\\nonly hit by those that reach them.',
 'A quarter of each wave goes for the generators\\ninstead of the players.',
 'Half of each wave goes for the generators\\ninstead of the players.',
 'Three quarters of each wave goes for the\\ngenerators instead of the players.',
 'Every enemy goes for the generators while one\\nis unlocked.',
 'Only your chosen Starting Skulls are active;\\nno automatic Round skulls.',
 'Activate Tough Luck (R2), Catch (R3), Black Eye\\n(Bonus), and Set Skulls.',
 'Activate a random Firefight Skull every Round,\\nnever one already on.',
 'Press A to choose the skulls on from Wave 1. One\\nalso turned on by a round is simply left on.',
 'No primary weapon.',
 'A random primary weapon at each spawn.',
 'Spawn with an Assault Rifle.',
 'Spawn with a Pistol.',
 'Spawn with a Shotgun.',
 'Spawn with a Sniper Rifle.',
 'Spawn with a Rocket Launcher.',
 'Spawn with a Plasma Pistol.',
 'Spawn with a Plasma Rifle.',
 'Spawn with a Needler.',
 'No secondary weapon.',
 'A random secondary weapon at each spawn.',
 'An Assault Rifle as the second weapon.',
 'A Pistol as the second weapon.',
 'A Shotgun as the second weapon.',
 'A Sniper Rifle as the second weapon.',
 'A Rocket Launcher as the second weapon.',
 'A Plasma Pistol as the second weapon.',
 'A Plasma Rifle as the second weapon.',
 'A Needler as the second weapon.',
 'Standard 100% Spartan energy shields.',
 'Disable player energy shields (0% Shields).',
 '50% player energy shield capacity.',
 '150% reinforced player energy shield capacity.',
 '200% Overshield capacity on spawn.',
 '400% Juggernaut energy shield capacity on spawn.',
 '100% standard player movement speed.',
 '75% reduced player movement speed.',
 '125% fast player movement speed.',
 '150% sprint-tier player movement speed.',
 '100% standard gravity.',
 '50% low-gravity moon jumps.',
 '75% reduced gravity.',
 '150% heavy high-gravity combat.',
 'Standard ammunition consumption.',
 'Infinite reserve ammunition and grenades\\n(reloading still required).',
 'Bottomless Clip: Fire continuously without\\nreloading or running out of ammo!',
 'Standard Generator durability (2500 HP +\\nrechargeable energy shield).',
 'Fragile Generator durability (1250 HP).',
 'Reinforced Generator durability (5000 HP).',
 'Fortress Generator durability (10000 HP).',
 'Press A to choose the Tag Injector profile whose\\nAI and weapons join the waves and drops.',
 'CANNOT DISABLE ALL ENEMY TYPES -\\nAT LEAST 1 MUST REMAIN ON']
# the spinners' choices (var_ff_<name>), the page chooser's first
FIREFIGHT_CHOICES = {'var_ff_loadout_weapon': ['NONE', 'RANDOM', 'ASSAULT RIFLE', 'PISTOL', 'SHOTGUN', 'SNIPER RIFLE',
                                           'ROCKET LAUNCHER', 'PLASMA PISTOL', 'PLASMA RIFLE', 'NEEDLER'],
                     'var_ff_set_limit': ['UNLIMITED', '1 SET', '2 SETS', '3 SETS', '5 SETS', '10 SETS'],
 'var_ff_starting_lives': ['UNLIMITED',
                           '0 LIVES',
                           '3 LIVES',
                           '5 LIVES',
                           '7 LIVES',
                           '10 LIVES',
                           '15 LIVES',
                           '20 LIVES'],
 'var_ff_round_lives': ['NONE', '+1 LIFE', '+2 LIVES', '+3 LIVES', '+5 LIVES'],
 'var_ff_bonus_lives': ['NONE', '+1 LIFE', '+2 LIVES', '+3 LIVES', '+5 LIVES', '+10 LIVES'],
 'var_ff_max_lives': ['NO CAP', '5 LIVES', '10 LIVES', '15 LIVES', '20 LIVES', '30 LIVES', '50 LIVES'],
 'var_ff_prep_time': ['5 SECONDS', '15 SECONDS', 'AUTO', '45 SECONDS', '60 SECONDS'],
 'var_ff_wave_limit': ['2 ENEMIES LEFT', '60 SECONDS', '90 SECONDS', '120 SECONDS', 'CLEAR ALL'],
 'var_ff_yes_no': ['YES', 'NO'],
 'var_ff_bonus_dur': ['30 SECONDS', '45 SECONDS', '60 SECONDS', '90 SECONDS', '120 SECONDS'],
 'var_ff_faction': ['COVENANT',
                    'GRUNTS ONLY',
                    'ELITES & HEAVIES',
                    'ALL FACTIONS',
                    'FLOOD',
                    'SENTINELS',
                    'COV + FLOOD',
                    'COV + SENTINELS',
                    'FLOOD + SENTINELS',
                    'HUMANS (UNSC)'],
 'var_ff_allegiance': ['ALLIED VS TEAM', 'FREE-FOR-ALL'],
 'var_ff_difficulty': ['EASY', 'NORMAL', 'HEROIC', 'LEGENDARY'],
 'var_ff_escalation': ['+25% / SET', 'OFF', '+15% / SET', '+40% / SET', '+60% / SET'],
 'var_ff_vitality': ['100%', '50%', '150%', '200%', '300%'],
 'var_ff_wave_size': ['SMALL (50%)', 'NORMAL (100%)', 'LARGE (150%)', 'HORDE (200%)', 'SWARM (250%)',
                      'LEGION (300%)'],
 'var_ff_boss_freq': ['WAVE 5 ONLY', 'EVERY 3RD WAVE', 'BOSS RUSH'],
 'var_ff_enemy_loadout': ['DEFAULT', 'HUMAN', 'COVENANT', 'RANDOMIZED'],
 'var_ff_on_off': ['ON', 'OFF'],
 'var_ff_dropships': ['AUTO TELEPORT', 'GROUND ONLY', 'PLASMA BEACON', 'FLOOD BURST'],
 'var_ff_boss_veh': ['WRAITH/BANSHEE', 'OFF', 'WRAITH ONLY', 'BANSHEE ONLY', 'SCORPION ONLY'],
 'var_ff_ordnance': ['OFF', 'PER SET', 'PER ROUND', 'HEAVY CRATES'],
 'var_ff_round_veh': ['NONE', 'WARTHOG', 'ROCKET HOG', 'GHOST', 'SCORPION', 'BANSHEE', 'WRAITH'],
 'var_ff_late_veh': ['NONE', 'WARTHOG', 'SCORPION', 'BANSHEE', 'WRAITH'],
 'var_ff_allies': ['OFF', '2 ALLIES / SET', '4 ALLIES / SET', 'ODST / SPECOPS', 'PER ROUND'],
 'var_ff_generators': ['OFF', '1 GENERATOR', '2 GENERATORS', '3 GENERATORS'],
 'var_ff_gen_fail': ['LOSE ALL', 'LOSE ANY'],
 'var_ff_gen_attackers': ['NONE', '25% OF UNITS', '50% OF UNITS', '75% OF UNITS', 'ALL UNITS'],
 'var_ff_prog_skulls': ['OFF (LOCKED)', 'REACH STANDARD', 'RANDOM / ROUND'],
 'var_ff_species': ['SPARTAN (UNSC)', 'ELITE (COV)'],
 'var_ff_shields': ['NORMAL (100%)', 'NO SHIELDS', 'HALF (50%)', '1.5X (150%)', '2X OVERSHIELD', '4X JUGGERNAUT'],
 'var_ff_speed': ['100%', '75%', '125%', '150%'],
 'var_ff_gravity': ['100%', '50% (LOW GRAV)', '75%', '150% (HEAVY)'],
 'var_ff_ammo': ['OFF', 'INF. RESERVE', 'BOTTOMLESS'],
 'var_ff_gen_hp': ['NORMAL (2500)', 'FRAGILE (1250)', 'HEAVY (5000)', 'FORTRESS (10K)'],
 'var_ff_stats_view': ['CAREER OVERVIEW',
                       'COMBAT & SPREES',
                       'MULTIKILL MEDALS',
                       'SPREE MEDALS',
                       'WEAPON SPREES',
                       'STYLE MEDALS 1',
                       'STYLE MEDALS 2',
                       'MAP RECORDS'],
 'var_ff_stats_action': ['PRESS A: EXPORT', 'PRESS A: WEB URL', 'PRESS A: COPY ID']}
# Multiplayer's mode headers, and Forge's menus: each widget's
# name, attributes and what it holds
FORGE_WIDGETS = [('main_menu/multiplayer_type_select/header_standard',
  [('controller', '1'),
   ('left', '35'),
   ('top', '11'),
   ('width', '363'),
   ('height', '59'),
   ('bitmap', 'main_menu/multiplayer_type_select/header_standard')],
  []),
 ('main_menu/multiplayer_type_select/header_firefight',
  [('controller', '1'),
   ('left', '35'),
   ('top', '11'),
   ('width', '363'),
   ('height', '59'),
   ('bitmap', 'main_menu/multiplayer_type_select/header_firefight')],
  []),
 ('main_menu/multiplayer_type_select/header_forge',
  [('controller', '1'),
   ('left', '35'),
   ('top', '11'),
   ('width', '363'),
   ('height', '59'),
   ('bitmap', 'main_menu/multiplayer_type_select/header_forge')],
  []),
 ('main_menu/multiplayer_type_select/gamemode/gamemode_button_bar',
  [('type', 'column_list'),
   ('width', '640'),
   ('height', '28'),
   ('flags', 'pass_unhandled_to_focused_child left_right_tabs_items')],
  [('data', [('input', 'common button bar update')]),
   ('child', [('widget', 'common_button_back'), ('x', '510'), ('y', '1')])]),
 ('main_menu/forge_select/forge_item_single_player',
  [('type', 'text'),
   ('width', '320'),
   ('height', '33'),
   ('bitmap', 'main_menu/menu_single_player')],
  [('on', [('event', 'a'), ('open', 'main_menu/solo_level_select/solo_level_select_screen')]),
   ('on', [('event', 'start'), ('open', 'main_menu/solo_level_select/solo_level_select_screen')]),
   ('on', [('event', 'left_mouse'), ('run', 'mouse emit accept event')])]),
 ('main_menu/forge_select/forge_item_multiplayer',
  [('type', 'text'),
   ('width', '256'),
   ('height', '33'),
   ('bitmap', 'main_menu/menu_multiplayer')],
  [('on', [('event', 'a'), ('open', 'main_menu/forge_select/forge_mp_type_select_screen')]),
   ('on', [('event', 'start'), ('open', 'main_menu/forge_select/forge_mp_type_select_screen')]),
   ('on', [('event', 'left_mouse'), ('run', 'mouse emit accept event')])]),
 ('main_menu/forge_select/forge_item_tag_injector',
  [('type', 'text'),
   ('width', '320'),
   ('height', '33'),
   ('bitmap', 'main_menu/menu_tag_injector')],
  [('on', [('event', 'a'), ('open', TAG_INJECTOR_SCREEN)]),
   ('on', [('event', 'start'), ('open', TAG_INJECTOR_SCREEN)]),
   ('on', [('event', 'left_mouse'), ('run', 'mouse emit accept event')])]),
 ('main_menu/forge_select/forge_menu_select_list',
  [('type', 'column_list'),
   ('width', '640'),
   ('height', '480'),
   ('flags', 'pass_unhandled_to_focused_child up_down_tabs_items')],
  [('on', [('event', 'created'), ('run', 'gamemode menu init')]),
   ('child', [('widget', 'main_menu/forge_select/forge_item_single_player'), ('x', '160'), ('y', '208')]),
   ('child', [('widget', 'main_menu/forge_select/forge_item_multiplayer'), ('x', '192'), ('y', '240')]),
   ('child', [('widget', 'main_menu/forge_select/forge_item_tag_injector'), ('x', '160'), ('y', '272')]),
   ('child', [('widget', 'main_menu/campaign_select/menu_item_back'), ('x', '192'), ('y', '304')])]),
 ('main_menu/forge_select/forge_menu',
  [('width', '640'), ('height', '480'), ('flags', 'pass_unhandled_to_focused_child')],
  [('on', [('event', 'b'), ('back', 'true')]),
   ('on', [('event', 'back'), ('back', 'true')]),
   ('child', [('widget', 'main_menu/halo_logo'), ('y', '-44')]),
   ('child', [('widget', 'main_menu/forge_select/forge_menu_select_list')])]),
 ('main_menu/forge_select/forge_browser_button_choose',
  [('type', 'text'),
   ('width', '128'),
   ('height', '24'),
   ('bitmap', 'bitmaps/text_button_background'),
   ('text', 'CHOOSE'),
   ('font', 'ui\\small_ui'),
   ('color', '#FFFFFFFF'),
   ('align', 'center'),
   ('text_y', '2')],
  [('on', [('event', 'a'), ('run', 'load game menu activated')]),
   ('on', [('event', 'start'), ('run', 'load game menu activated')]),
   ('on', [('event', 'left_mouse'), ('run', 'mouse emit accept event')])]),
 ('main_menu/forge_select/forge_browser_button_back',
  [('type', 'text'),
   ('width', '128'),
   ('height', '24'),
   ('bitmap', 'bitmaps/text_button_background'),
   ('text', 'BACK'),
   ('font', 'ui\\small_ui'),
   ('color', '#FFFFFFFF'),
   ('align', 'center'),
   ('text_y', '2')],
  [('on', [('event', 'a'), ('run', 'mouse emit back event')]),
   ('on', [('event', 'start'), ('run', 'mouse emit back event')]),
   ('on', [('event', 'left_mouse'), ('run', 'mouse emit back event')])]),
 ('main_menu/forge_select/forge_browser_button_bar',
  [('type', 'column_list'),
   ('width', '640'),
   ('height', '28'),
   ('flags', 'pass_unhandled_to_focused_child left_right_tabs_items')],
  [('data', [('input', 'common button bar update')]),
   ('child', [('widget', 'main_menu/forge_select/forge_browser_button_choose'), ('x', '380'), ('y', '1')]),
   ('child', [('widget', 'main_menu/forge_select/forge_browser_button_back'), ('x', '510'), ('y', '1')])]),
 ('main_menu/forge_select/forge_mp_type_select_list',
  [('type', 'column_list'),
   ('width', '640'),
   ('height', '480'),
   ('flags', 'pass_unhandled_to_focused_child up_down_tabs_items'),
   ('description', 'main_menu/multiplayer_type_select/multiplayer_type_list_ext_desc')],
  [('data', [('input', 'multiplayer type menu update desc')]),
   ('data', [('input', 'mp type screen update')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_join_item'), ('x', '-32'), ('y', '78')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_join_internet_item'), ('y', '107')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_join_lan_item'), ('y', '136')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_join_direct_item'), ('y', '165')]),
   ('child',
    [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_create_item'), ('x', '-32'), ('y', '194')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_create_internet_item'), ('y', '223')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_create_lan_item'), ('y', '252')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_create_local_item'), ('y', '281')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/multiplayer_type_button_bar'), ('y', '414')])]),
 ('main_menu/forge_select/forge_mp_type_select_screen',
  [('width', '640'), ('height', '480'), ('flags', 'pass_unhandled_to_focused_child'), ('bitmap', 'bitmaps/gradient')],
  [('on', [('event', 'created'), ('run', 'mp type menu initialize')]),
   ('on', [('event', 'b'), ('back', 'true')]),
   ('on', [('event', 'back'), ('back', 'true')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/header_forge')]),
   ('child', [('widget', 'main_menu/forge_select/forge_mp_type_select_list')]),
   ('child', [('widget', 'main_menu/blueline'), ('x', '64'), ('y', '349')])]),
 ('main_menu/forge_select/forge_tag_profile_title',
  [('type', 'text'),
   ('controller', '1'),
   ('left', '68'),
   ('top', '74'),
   ('width', '504'),
   ('height', '22'),
   ('text', 'TAG INJECTOR PROFILE'),
   ('font', 'ui\\large_ui'),
   ('color', '#FFFFFFFF')],
  []),
 ('main_menu/forge_select/forge_tag_profile_list',
  [('type', 'column_list'),
   ('width', '640'),
   ('height', '480'),
   ('flags', 'pass_unhandled_to_focused_child up_down_tabs_children')],
  [('data', [('input', 'load game list update')]),
   ('on', [('event', 'created'), ('run', 'load game menu init')]),
   ('on', [('event', 'custom_activation'), ('run', 'load game menu activated')]),
   ('on', [('event', 'b'), ('back', 'true')]),
   ('on', [('event', 'back'), ('back', 'true')]),
   ('child', [('widget', 'main_menu/new_select/list_item_1'), ('x', '20'), ('y', '103')]),
   ('child', [('widget', 'main_menu/new_select/list_item_2'), ('x', '20'), ('y', '133')]),
   ('child', [('widget', 'main_menu/new_select/list_item_3'), ('x', '20'), ('y', '163')]),
   ('child', [('widget', 'main_menu/new_select/list_item_4'), ('x', '20'), ('y', '193')]),
   ('child', [('widget', 'main_menu/new_select/list_item_5'), ('x', '20'), ('y', '223')]),
   ('child', [('widget', 'main_menu/new_select/list_item_6'), ('x', '20'), ('y', '253')]),
   ('child', [('widget', 'main_menu/new_select/list_item_7'), ('x', '20'), ('y', '283')]),
   ('child', [('widget', 'main_menu/new_select/list_item_8'), ('x', '20'), ('y', '313')]),
   ('child', [('widget', 'main_menu/new_select/list_item_9'), ('x', '20'), ('y', '343')]),
   ('child', [('widget', 'main_menu/new_select/list_item_10'), ('x', '20'), ('y', '373')]),
   ('child', [('widget', 'main_menu/forge_select/forge_browser_button_bar'), ('y', '414')])]),
 ('main_menu/forge_select/forge_tag_profile_screen',
  [('width', '640'), ('height', '480'), ('flags', 'pass_unhandled_to_focused_child'), ('bitmap', 'bitmaps/gradient')],
  [('on', [('event', 'b'), ('back', 'true')]),
   ('on', [('event', 'back'), ('back', 'true')]),
   ('child', [('widget', 'main_menu/new_select/sel_list_desc_bkd')]),
   ('child', [('widget', 'main_menu/forge_select/forge_tag_profile_list')]),
   ('child', [('widget', 'main_menu/forge_select/forge_tag_profile_title')]),
   ('child', [('widget', 'main_menu/multiplayer_type_select/header_forge')])]),
 ]


def attributes(pairs: list) -> str:
    return "".join(f" {key}={quoteattr(str(value))}" for key, value in pairs if value is not None)


def widget(name: str, pairs: list, inner: list) -> list:
    return [f"\t<widget{attributes([('name', name)] + pairs)}>", *[f"\t\t{line}" for line in inner], "\t</widget>"]


def strings(name: str, texts: list) -> list:
    return [f"\t<strings{attributes([('name', name)])}>",
            *[f"\t\t<string{attributes([('text', text)])}/>" for text in texts], "\t</strings>"]


def _row(key: str, label_index: int, choices: str, color: str) -> list:
    """a row of the rules: its label, its spinner of choices, and the row"""
    return widget(f"{FIREFIGHT_EDIT}/ff_{key}_label",
                  [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                   ("string_list", f"{FIREFIGHT_EDIT}/firefight_labels"), ("string_index", label_index),
                   ("font", "ui\\large_ui"), ("color", color), ("text_x", 13), ("text_y", 4)], []) + \
        widget(f"{FIREFIGHT_EDIT}/ff_{key}_spinner",
               [("type", "spinner"), ("left", 2), ("top", 2), ("width", 206), ("height", 20),
                ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                ("string_list", f"{FIREFIGHT_EDIT}/var_ff_{choices}"), ("font", "ui\\large_ui"),
                ("color", color), ("align", "center"), ("text_y", 4), ("list_flags", "items_from_strings"),
                ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
               ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])


def _option(name: str, key: str, color: str) -> list:
    return widget(name, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                         ("bitmap", "bitmaps/option_bkds"), ("color", color)],
                  [f'<child widget="{FIREFIGHT_EDIT}/ff_{key}_label"/>',
                   f'<child widget="{FIREFIGHT_EDIT}/ff_{key}_spinner" x="286" y="1"/>'])


def _action_row(key: str, label_index: int, color: str) -> list:
    """a row that opens a screen: its label, and the value menu_functions.c
    shows where a spinner's would be"""
    return widget(f"{FIREFIGHT_EDIT}/ff_{key}_label",
                  [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                   ("string_list", f"{FIREFIGHT_EDIT}/firefight_labels"), ("string_index", label_index),
                   ("font", "ui\\large_ui"), ("color", color), ("text_x", 13), ("text_y", 4)], []) + \
        widget(f"{FIREFIGHT_EDIT}/ff_{key}_value",
               [("type", "text"), ("controller", 1), ("left", 2), ("top", 2), ("width", 206), ("height", 20),
                ("font", "ui\\large_ui"), ("color", color), ("align", "center"), ("text_y", 4)], [])


def _action_option(name: str, key: str, color: str) -> list:
    run = FIREFIGHT_ACTION_ROWS[key]
    return widget(name, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                         ("bitmap", "bitmaps/option_bkds"), ("color", color)],
                  [f'<on event="a" run="{run}"/>', f'<on event="start" run="{run}"/>',
                   '<on event="left_mouse" run="mouse emit accept event"/>',
                   f'<child widget="{FIREFIGHT_EDIT}/ff_{key}_label"/>',
                   f'<child widget="{FIREFIGHT_EDIT}/ff_{key}_value" x="286" y="1"/>'])


def firefight_page_helps() -> list:
    """FIREFIGHT_HELP's for each page, after the hub's: its rows' in order, a
    help for each of a spinner's choices, one for a row that opens a screen
    (menu_functions.c's gametype_option_help counts them so); the units'
    guard's last, on its page (FF_UNIT_GUARD_HELP_STRING_INDEX)"""
    pages = [[] for _ in FIREFIGHT_PAGES]
    place = len(FIREFIGHT_PAGES)
    for page, _, setting, _, choices in FIREFIGHT_ROWS:
        count = 1 if setting in FIREFIGHT_ACTION_ROWS else len(FIREFIGHT_CHOICES[f"var_ff_{choices}"])
        pages[page] += FIREFIGHT_HELP[place:place + count]
        place += count
    pages[FIREFIGHT_UNITS_PAGE].append(FIREFIGHT_HELP[place])
    return pages


def _header(name: str, text: str) -> list:
    return widget(name, [("type", "text"), ("controller", 1), ("left", 35), ("top", 11), ("width", 605), ("height", 59),
                         ("text", text), ("font", "ui\\large_ui"), ("color", "#FFFFFFFF")], [])


def _help(name: str, texts: str) -> list:
    return widget(name, [("type", "text"), ("controller", 1), ("left", 68), ("top", 348), ("width", 482),
                         ("height", 62), ("string_list", texts), ("font", "ui\\large_ui"),
                         ("color", "#FFFFFFFF")], [])


def firefight_editor() -> list:
    """the gametype editor's FIREFIGHT row, and a variant's rules: a screen of
    a button for each page, each opening its own screen of rows 27 apart,
    whose OK keeps them ("mp profile set firefight rules")"""
    lines = widget(f"{GAMETYPE_SELECT}/gametype_select_firefight_item",
                   [("type", "text"), ("left", 51), ("top", 78), ("width", 232), ("height", 32),
                    ("bitmap", "bitmaps/list_item_bkd_top"),
                    ("string_list", f"{GAMETYPE_SELECT}/gametype_select_list_options"), ("string_index", 6),
                    ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                   [f'<on event="{event}" run="mp profile set game engine" '
                    f'open="{FIREFIGHT_EDIT}/firefight_options_screen"/>' for event in ("a", "start")] +
                   ['<on event="left_mouse" run="mouse emit accept event"/>'])
    buttons = []
    for page, title in enumerate(FIREFIGHT_PAGES):
        screen = f"{FIREFIGHT_EDIT}/firefight_page_{page}_screen"
        lines += widget(f"{FIREFIGHT_EDIT}/op_ff_page_{page}",
                        [("type", "text"), ("width", 512), ("height", 28), ("bitmap", "bitmaps/option_bkds"),
                         ("text", title), ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                         ("text_y", 4)],
                        [*[f'<on event="{event}" open="{screen}"/>' for event in ("a", "start")],
                         '<on event="left_mouse" run="mouse emit accept event"/>'])
        buttons.append(f'<child widget="{FIREFIGHT_EDIT}/op_ff_page_{page}" x="54" y="{100 + 31 * page}"/>')
        children = []
        for row_page, key, setting, label_index, choices in FIREFIGHT_ROWS:
            if row_page != page:
                continue
            if setting in FIREFIGHT_ACTION_ROWS:
                lines += _action_row(setting, label_index, "#FF2896FF")
                lines += _action_option(f"{FIREFIGHT_EDIT}/op_ff{page}_{key}", setting, "#FF2896FF")
            else:
                lines += _row(setting, label_index, choices, "#FF2896FF")
                lines += _option(f"{FIREFIGHT_EDIT}/op_ff{page}_{key}", setting, "#FF2896FF")
            children.append(f'<child widget="{FIREFIGHT_EDIT}/op_ff{page}_{key}" x="54" y="{73 + 27 * len(children)}"/>')
        children.append(f'<child widget="{FIREFIGHT_EDIT}/firefight_button_bar" y="414"/>')
        lines += _help(f"{FIREFIGHT_EDIT}/firefight_page_{page}_help", f"{FIREFIGHT_EDIT}/cap_firefight_{page}")
        lines += widget(f"{FIREFIGHT_EDIT}/firefight_page_{page}_menu",
                        [("type", "column_list"), ("width", 640), ("height", 480),
                         ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                         ("description", f"{FIREFIGHT_EDIT}/firefight_page_{page}_help")],
                        ['<data input="game settings lists text update"/>',
                         '<on event="created" run="mp profile init firefight rules"/>', *children])
        lines += _header(f"{FIREFIGHT_EDIT}/header_firefight_page_{page}", title)
        lines += widget(screen, [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child"),
                                 ("bitmap", "bitmaps/gradient")],
                        ['<on event="created" run="NULL"/>',
                         f'<child widget="{FIREFIGHT_EDIT}/header_firefight_page_{page}"/>',
                         f'<child widget="{FIREFIGHT_EDIT}/firefight_page_{page}_menu"/>'])
    lines += widget(f"{FIREFIGHT_EDIT}/firefight_button_bar",
                    [("type", "column_list"), ("width", 640), ("height", 28),
                     ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                    ['<data input="common button bar update"/>',
                     f'<child widget="{FIREFIGHT_EDIT}/firefight_button_ok" x="380" y="1"/>',
                     '<child widget="common_button_cancel" x="510" y="1"/>'])
    lines += widget(f"{FIREFIGHT_EDIT}/firefight_button_ok",
                    [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
                     ("string_list", "strings/common_button_captions"), ("string_index", 1),
                     ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                    [f'<on event="{event}" run="mp profile set firefight rules" back="true"/>'
                     for event in ("a", "start")] + ['<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _help(f"{FIREFIGHT_EDIT}/firefight_options_help", f"{FIREFIGHT_EDIT}/cap_firefight")
    lines += widget(f"{FIREFIGHT_EDIT}/firefight_options_menu",
                    [("type", "column_list"), ("width", 640), ("height", 480),
                     ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                     ("description", f"{FIREFIGHT_EDIT}/firefight_options_help")],
                    ['<data input="game settings lists text update"/>', *buttons,
                     '<child widget="main_menu/multiplayer_type_select/gamemode/gamemode_button_bar" y="414"/>'])
    lines += _header(f"{FIREFIGHT_EDIT}/header_firefight_options", "FIREFIGHT OPTIONS")
    lines += widget(f"{FIREFIGHT_EDIT}/firefight_options_screen",
                    [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child"),
                     ("bitmap", "bitmaps/gradient")],
                    ['<on event="created" run="NULL"/>', f'<child widget="{FIREFIGHT_EDIT}/header_firefight_options"/>',
                     f'<child widget="{FIREFIGHT_EDIT}/firefight_options_menu"/>'])
    lines += strings(f"{FIREFIGHT_EDIT}/firefight_labels", FIREFIGHT_LABELS)
    for name, texts in FIREFIGHT_CHOICES.items():
        lines += strings(f"{FIREFIGHT_EDIT}/{name}", texts)
    lines += strings(f"{FIREFIGHT_EDIT}/cap_firefight", FIREFIGHT_HELP[:len(FIREFIGHT_PAGES)])
    for page, texts in enumerate(firefight_page_helps()):
        lines += strings(f"{FIREFIGHT_EDIT}/cap_firefight_{page}", texts)
    return lines


def _menu_item(name: str, bitmap: str, width: int, events: list) -> list:
    return widget(f"{FIREFIGHT_SELECT}/{name}",
                  [("type", "text"), ("width", width), ("height", 33), ("bitmap", bitmap)],
                  [*events, '<on event="left_mouse" run="mouse emit accept event"/>'])


def _opens(target: str) -> list:
    return [f'<on event="{event}" open="{target}"/>' for event in ("a", "start")]


def firefight_menu() -> list:
    """PLAY's FIREFIGHT, as Forge's menu: SOLO, a LOCAL game at once (its
    name is what makes it one: menu_functions.c's multiplayer_mode_set, as
    port_settings.py's _create_local), MULTIPLAYER, Multiplayer's screen and
    browser for Firefight's games, and FIREFIGHT VARIANTS, the gametype
    editor for its variants alone"""
    solo = [line for event in ("a", "start") for line in
            (f'<on event="{event}" run="join controller to mp game"/>',
             f'<on event="{event}" run="mp type set mode" open="{LOCAL_MAP_SCREEN}"/>')]
    lines = _menu_item("firefight_item_create_local", "main_menu/menu_solo", 256, solo)
    lines += _menu_item("firefight_item_multiplayer", "main_menu/menu_multiplayer", 256, _opens(MULTIPLAYER_SCREEN))
    lines += _menu_item("firefight_item_variants", "main_menu/menu_firefight_variants", 384, _opens(VARIANTS_SCREEN))
    # (each centred, as Campaign's rows are: its places)
    items = [("firefight_item_create_local", 192), ("firefight_item_multiplayer", 192), ("firefight_item_variants", 128)]
    lines += widget(f"{FIREFIGHT_SELECT}/firefight_menu_select_list",
                    [("type", "column_list"), ("width", 640), ("height", 480),
                     ("flags", "pass_unhandled_to_focused_child up_down_tabs_items")],
                    [*[f'<child widget="{FIREFIGHT_SELECT}/{item}" x="{x}" y="{283 + 36 * index}"/>'
                       for index, (item, x) in enumerate(items)],
                     '<child widget="main_menu/campaign_select/menu_item_back" x="192" y="391"/>'])
    lines += widget(f"{FIREFIGHT_SELECT}/firefight_menu",
                    [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child")],
                    ['<on event="created" run="mp type menu initialize"/>', '<on event="b" back="true"/>',
                     '<on event="back" back="true"/>', '<child widget="main_menu/halo_logo" y="28"/>',
                     f'<child widget="{FIREFIGHT_SELECT}/firefight_menu_select_list"/>'])
    return lines


def forge_menus() -> list:
    """Multiplayer's mode headers, and Forge's menus
    (FORGE_WIDGETS), then Firefight's"""
    lines = []
    for name, pairs, inner in FORGE_WIDGETS:
        lines += widget(name, pairs, [f"<{tag}{attributes(items)}/>" for tag, items in inner])
    return lines + firefight_menu()


def files(head: list) -> dict:
    """the files of Content Extended's menus, each opened with head (its XML
    declaration, comment and <menus>)"""
    return {
        "main_menu.settings_select.multiplayer_setup.playlist_edit.firefight_edit.port.xml":
            head + firefight_editor() + ["</menus>", ""],
        "main_menu.forge_select.port.xml": head + forge_menus() + ["</menus>", ""],
    }
