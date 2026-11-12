/*
FIREFIGHT_SPAWNER.C

port: Self-contained Firefight wave spawner, faction/unit roster manager,
2-3 zone randomized squad placement, animated Spirit/Pelican dropships,
Boss Wave vehicle drops, Safe Player Ordnance/Heavy Weapon drops,
Friendly AI Reinforcements, and Round-Scheduled Player Vehicles.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "ai/ai.h"
#include "ai/ai_globals.h"
#include "ai/ai_scenario_definitions.h"
#include "ai/actors.h"
#include "ai/actor_definitions.h"
#include "ai/actor_types.h"
#include "ai/encounters.h"
#include "ai/props.h"
#include "cache/cache_files.h"
#include "effects/effect_definitions.h"
#include "effects/effects.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "items/equipment_definitions.h"
#include "items/item_definitions.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "models/model_definitions.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "structures/structures.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "units/vehicle_definitions.h"
#include "units/vehicles.h"

#include "bots.h"
#include "custom_edition_cache.h"
#include "mcc_cache.h"
#include "halo_firefight.h"
#include "halo_firefight_route.h"
#include "opence_features.h"
#include "halo_skulls.h"

/* ---------- constants */

enum
{
	MAX_ROSTER_VARIANTS = 64,
	MAX_SPAWN_POINTS = 128,
	MAX_FRIENDLY_UNITS = 8,
	MAX_ORDNANCE_TAG_POOL = 24,
	FIREFIGHT_SAFE_ACTIVE_ACTOR_CAP = 28,
	DROPSHIP_PHASE_NONE = 0,
	DROPSHIP_PHASE_DESCENDING = 1,
	DROPSHIP_PHASE_HOVER_DEPLOY = 2,
	DROPSHIP_PHASE_ASCENDING = 3,

	/* Custom Forge Firefight Spawn Marker ('ffsp' -> scenario_netgame_flag.type == 15) */
	FF_NETGAME_FLAG_FIREFIGHT_SPAWN = 15,
	/* (Forge's generator objective, firefight_generators.c's: no spawn point) */
	FF_NETGAME_FLAG_FIREFIGHT_GENERATOR = 16,
	FF_SPAWN_TYPE_AI_INFANTRY       = 0,
	FF_SPAWN_TYPE_AI_AND_VEHICLES   = 1,
	FF_SPAWN_TYPE_ENEMY_VEHICLE     = 2,
	FF_SPAWN_TYPE_ENEMY_GHOST       = 3,
	FF_SPAWN_TYPE_ENEMY_HEAVY_VEH   = 4,
	FF_SPAWN_TYPE_DROPSHIP_ZONE     = 5,
	FF_SPAWN_TYPE_PLAYER_VEHICLE    = 6,

	FF_ROUND_ALL       = 0,
	FF_ROUND_1         = 1,
	FF_ROUND_2         = 2,
	FF_ROUND_3         = 3,
	FF_ROUND_4         = 4,
	FF_ROUND_5         = 5,
	FF_ROUND_1_TO_3    = 6,
	FF_ROUND_4_TO_5    = 7,
	FF_ROUND_BONUS     = 8,

	FF_WAVE_ALL        = 0,
	FF_WAVE_1          = 1,
	FF_WAVE_2          = 2,
	FF_WAVE_3_BOSS     = 3
};

#define FF_UNPACK_SPAWN_TYPE(u)   ((int)((u) & 0x0F))
#define FF_UNPACK_ROUND_FILTER(u) ((int)(((u) >> 4) & 0x0F))
#define FF_UNPACK_WAVE_FILTER(u)  ((int)(((u) >> 8) & 0x07))

/* ---------- structures */

struct firefight_roster_entry
{
	long variant_tag_index;
	short faction;
	short unit_toggle_bit;
	byte tier;
	byte subtype;
	boolean is_human_ally_or_enemy;
	/* the map's own (MAP CONTENT): neither copied in nor a profile's */
	boolean maps_own;
	/* (one of them MAP ENEMY AI's CUSTOM turns off) */
	boolean custom_off;
};

/* ---------- globals */

extern struct game_variant global_variant;
boolean actor_set_active(long actor_index, boolean active);

static struct firefight_roster_entry g_roster[MAX_ROSTER_VARIANTS];
static short g_roster_count = 0;

static real_point3d g_spawn_points[MAX_SPAWN_POINTS];
static real g_spawn_facings[MAX_SPAWN_POINTS];
static short g_spawn_point_count = 0;
/* the players' base: the map's starting location everyone spawns about
(firefight_select_player_spawn_point), chosen at the first spawn of a map */
#define FF_PLAYER_BASE_SPAWNS 8
static short g_player_base_location = NONE;

static long g_friendly_units[MAX_FRIENDLY_UNITS];
static short g_friendly_unit_count = 0;

static long ff_power_weapon_tags[MAX_ORDNANCE_TAG_POOL];
static short ff_power_weapon_tag_count = 0;
static long ff_standard_weapon_tags[MAX_ORDNANCE_TAG_POOL];
static short ff_standard_weapon_tag_count = 0;
static long ff_ammo_tags[MAX_ORDNANCE_TAG_POOL];
static short ff_ammo_tag_count = 0;
static long ff_grenade_tags[MAX_ORDNANCE_TAG_POOL];
static short ff_grenade_tag_count = 0;

/* a variant's Tag Injector profile: its AI, which join the waves whatever
the faction, and its weapons, which join the drops */
static long ff_custom_enemy_tags[MAX_ROSTER_VARIANTS];
static byte ff_custom_enemy_tiers[MAX_ROSTER_VARIANTS];
static short ff_custom_enemy_count = 0;
static long ff_custom_weapon_tags[MAX_ROSTER_VARIANTS];
static boolean ff_custom_weapon_power[MAX_ROSTER_VARIANTS];
static short ff_custom_weapon_count = 0;

static short g_queued_reinforcement_enemies = 0;
static byte g_queued_reinforcement_subtypes[256];
static byte g_dropship_squad_subtypes[32];
static short g_dropship_squad_subtype_count = 0;
static real g_active_wave_facings[FIREFIGHT_MAX_SPAWN_ZONES_PER_WAVE];

/* MAP CONTENT's: the map's own enemies (ones the roster has no place for:
those it has are its entries of maps_own), weapons and vehicles; and, for
ONLY MAP, whether the roster's picks keep to its entries of the map's */
enum
{
	MAX_MAP_VEHICLES = 16
};
static long ff_map_enemy_tags[MAX_ROSTER_VARIANTS];
static byte ff_map_enemy_tiers[MAX_ROSTER_VARIANTS];
static short ff_map_enemy_count = 0;
static long ff_map_weapon_tags[MAX_ROSTER_VARIANTS];
static boolean ff_map_weapon_power[MAX_ROSTER_VARIANTS];
static boolean ff_map_weapon_stock[MAX_ROSTER_VARIANTS];
static short ff_map_weapon_count = 0;
static long ff_map_vehicle_tags[MAX_MAP_VEHICLES];
static boolean ff_map_vehicle_stock[MAX_MAP_VEHICLES];
static short ff_map_vehicle_count = 0;
static boolean g_roster_map_only = FALSE;

/* the ordnance pod's (firefight_ordnance_pod_update) */
enum
{
	FF_POD_FALL_TICKS = 2 * TICKS_PER_SECOND,
	FF_POD_STAY_TICKS = 40 * TICKS_PER_SECOND,
	/* (a pod's weapon: a drop of more takes more pods, one each) */
	FF_POD_MAX_CONTENTS = FIREFIGHT_MAX_ORDNANCE_PODS,
	/* the drop areas Forge marks (FF_DROP_AREA_TAG) */
	FF_MAX_DROP_AREAS = 16
};
#define FF_POD_FALL_HEIGHT 45.0f
/* the pod: the game's props' capsule (tools/prop_assets.py), else Two
Betrayals' storage crate. The capsule is its Firefight copy, the drop area's
tag, which is the props' alone: the map it was taken from has its own
altis\scenery\capsule\capsule (a weapon spawner, a health pack in it), which a
level plays in place of the props' of that name */
#define FF_POD_TAG "firefight\\props\\drop_area_pod"
#define FF_POD_FALLBACK_TAG "scenery\\c_storage\\c_storage"
/* the capsule stands on its end: its model lies along its x axis, from its
origin at its base (x 0.14) to its top (x -1.0), its open side +z */
#define FF_POD_CAPSULE_BASE 0.141f
/* where its weapon stands in it: so far out of its middle towards its open
side, and so high */
#define FF_POD_CAPSULE_WEAPON_OUT 0.12f
#define FF_POD_CAPSULE_WEAPON_HEIGHT 0.67f
/* a drop area, Forge's FIREFIGHT category's (tools/prop_assets.py's copy of
the capsule, the pods' own tag): where the pods come down, near it, instead
of near the player. Only a marker, taken away on every machine as Firefight
plays (before any pod is made) */
#define FF_DROP_AREA_TAG FF_POD_TAG

/* (the host's) what the pods in the air hold; then, landed, each weapon held
against its pod's side at its place till it's taken (or knocked off) */
static long g_pod_contents[FF_POD_MAX_CONTENTS];
static short g_pod_content_count = 0;
static struct
{
	long object_index;
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
} g_pod_held[FF_POD_MAX_CONTENTS];
static short g_pod_held_count = 0;
/* (each machine's) whether the pods shown have landed, each one's scenery,
how high it falls from, and the drop they show */
static boolean g_pod_landed = FALSE;
static long g_pod_objects[FIREFIGHT_MAX_ORDNANCE_PODS];
static real g_pod_fall_heights[FIREFIGHT_MAX_ORDNANCE_PODS];
static long g_pod_shown_time = NONE;
/* (each machine's) whether the pods shown are the capsule (else the crate) */
static boolean g_pod_capsule = FALSE;
/* (each machine's) the drop areas the level has, found once it plays */
static real_point3d g_drop_areas[FF_MAX_DROP_AREAS];
static short g_drop_area_count = 0;
static boolean g_drop_areas_found = FALSE;
/* why spots were refused, by check (pod_spot_passes), and the first object
a ray down met: what the test drop says when none is found */
enum
{
	_pod_spot_no_ground,
	_pod_spot_object_under,
	_pod_spot_slope,
	_pod_spot_outside_map,
	_pod_spot_unseen,
	_pod_spot_cramped,
	_pod_spot_roofed,
	_pod_spot_crowded,
	_pod_spot_too_near,
	_pod_spot_enemies,
	NUMBER_OF_POD_SPOT_REFUSALS
};
static long g_pod_spot_refusals[NUMBER_OF_POD_SPOT_REFUSALS];
static long g_pod_spot_blocker = NONE;
#define POD_SPOT_REFUSED(reason) (g_pod_spot_refusals[(reason)]++, FALSE)


static boolean map_vehicles_only(void);
static long pick_map_vehicle(boolean heavy, boolean custom_only);
static long pick_map_weapon(boolean heavy, boolean custom_only);
static boolean map_weapons_only(void);
static boolean vehicle_tag_flies(long vehicle_tag);
static boolean vehicle_tag_hovers(long vehicle_tag);

static unsigned long g_spawner_rng = 0x9E3779B9UL;
static long g_last_aggro_refresh_tick = 0;
/* the generator unit each tracked enemy is sent at (NONE: the players) */
static long g_generator_attack_units[FIREFIGHT_MAX_TRACKED_ENEMIES];

/* ---------- helpers */

static unsigned long spawner_rand(void)
{
	g_spawner_rng = g_spawner_rng * 1664525UL + 1013904223UL;
	return (g_spawner_rng >> 8) & 0xFFFFFFUL;
}

static real spawner_rand_range(real low, real high)
{
	real t = (real)(spawner_rand() & 0xFFFF) / 65535.0f;
	return low + (high - low) * t;
}

static void lowercase_copy(char const *src, char *dst, size_t dst_size)
{
	size_t i = 0;

	if (!src || !dst_size)
	{
		if (dst_size > 0)
			dst[0] = '\0';
		return;
	}
	for (; src[i] != '\0' && i + 1 < dst_size; i++)
	{
		char c = src[i];
		dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
	}
	dst[i] = '\0';
}

/* ---------- Unit Toggle Validation Guard (Section 4.2 & 4.9) */

boolean firefight_unit_toggle_enabled(
	enum firefight_unit_toggle_bit unit_bit)
{
	struct game_variant_options const *opts = game_variant_options_get();
	unsigned short mask = (opts && opts->firefight_unit_toggles != 0) ?
		opts->firefight_unit_toggles : FIREFIGHT_UNIT_MASK_ALL;

	if (unit_bit < 0 || unit_bit >= NUMBER_OF_FIREFIGHT_UNIT_TOGGLES)
		return TRUE;
	return (mask & (1U << unit_bit)) != 0;
}

static unsigned short faction_relevant_unit_mask(
	byte wave_faction)
{
	unsigned short cov = (1U << _ff_unit_covenant_grunts_bit) |
		(1U << _ff_unit_covenant_jackals_bit) |
		(1U << _ff_unit_covenant_elites_bit) |
		(1U << _ff_unit_covenant_specops_stealth_bit) |
		(1U << _ff_unit_covenant_zealots_bit) |
		(1U << _ff_unit_covenant_hunters_bit);
	unsigned short fld = (1U << _ff_unit_flood_infection_bit) |
		(1U << _ff_unit_flood_combat_bit) |
		(1U << _ff_unit_flood_carrier_bit);
	unsigned short snt = (1U << _ff_unit_sentinels_bit);
	unsigned short hum = (1U << _ff_unit_human_marines_bit) |
		(1U << _ff_unit_human_odst_heavies_bit);

	switch (wave_faction)
	{
	case _firefight_faction_covenant:           return cov;
	case _firefight_faction_flood:              return fld;
	case _firefight_faction_grunts:             return (1U << _ff_unit_covenant_grunts_bit);
	case _firefight_faction_elites_heavies:     return cov & ~((1U << _ff_unit_covenant_grunts_bit) | (1U << _ff_unit_covenant_jackals_bit));
	case _firefight_faction_sentinels:          return snt;
	case _firefight_faction_covenant_flood:     return cov | fld;
	case _firefight_faction_covenant_sentinels: return cov | snt;
	case _firefight_faction_flood_sentinels:    return fld | snt;
	case _firefight_faction_unsc_humans:        return hum;
	case _firefight_faction_mixed:
	default:                                    return cov | fld | snt | hum;
	}
}

boolean firefight_validate_unit_toggles(
	byte wave_faction,
	unsigned short candidate_unit_mask)
{
	unsigned short relevant = faction_relevant_unit_mask(wave_faction);

	return (candidate_unit_mask & relevant) != 0;
}

/* ---------- Roster Scanning & Spawn Point Discovery */

int tag_import_tag_imported(long tag_index);
boolean tag_index_is_group(long tag_index, long group_tag);
boolean tag_injector_tag_injected(long tag_index);
int randomizer_stock_tag_exists(long group_tag, char const *name);

/* whether a tag is the map's own: not one copied in from the campaign's
(the randomizer's, the skulls', Firefight's), nor one a Tag Injector profile
added */
static boolean tag_is_maps_own(long tag_index)
{
	return tag_index != NONE && !tag_import_tag_imported(tag_index) && !tag_injector_tag_injected(tag_index);
}

byte firefight_map_content(void)
{
	struct game_variant_options const *opts = game_variant_options_get();
	byte mode = opts ? (byte)(opts->firefight_map_content & FIREFIGHT_MAP_CONTENT_MODE_MASK) : 0;

	/* (an MCC map's Firefight is vanilla: none of its own enemies, weapons
	or vehicles, whatever a setting carried from another map says) */
	if (mcc_cache_tags_loaded())
		return _firefight_map_content_vanilla;
	if (mode != _firefight_map_content_include_map && mode != _firefight_map_content_only_map)
		return _firefight_map_content_vanilla;
	return mode;
}

/* ... for one kind (FIREFIGHT_MAP_CONTENT_*_OFF, its advanced row's): VANILLA
when that's turned off */
static byte map_content_of(byte off_bit)
{
	struct game_variant_options const *opts = game_variant_options_get();

	return opts && (opts->firefight_map_content & off_bit) ? _firefight_map_content_vanilla : firefight_map_content();
}

static boolean name_has_word(char const *lowered, char const *const *words)
{
	short i;

	for (i = 0; words[i]; i++)
	{
		if (strstr(lowered, words[i]))
			return TRUE;
	}
	return FALSE;
}

static void classify_actor_variant(long variant_index)
{
	char const *raw_name;
	char name[256];
	struct actor_variant_definition *variant_def;
	struct firefight_roster_entry *entry;

	if (g_roster_count >= MAX_ROSTER_VARIANTS || variant_index == NONE)
		return;

	variant_def = actor_variant_definition_get(variant_index);
	if (!variant_def || variant_def->unit_reference.index == NONE || variant_def->actor_reference.index == NONE)
		return;

	raw_name = tag_get_name(variant_index);
	if (!raw_name)
		return;
	lowercase_copy(raw_name, name, sizeof(name));

	if (strstr(name, "cinematic") || strstr(name, "cortana") || strstr(name, "keyes") ||
		strstr(name, "monitor") || strstr(name, "engineer"))
	{
		return;
	}

	entry = &g_roster[g_roster_count];
	csmemset(entry, 0, sizeof(*entry));
	entry->variant_tag_index = variant_index;
	entry->maps_own = tag_is_maps_own(variant_index);
	entry->custom_off = FALSE;
	entry->faction = NONE;
	entry->unit_toggle_bit = NONE;
	entry->tier = _ff_tier_infantry;
	entry->subtype = _ff_sub_other;

	if (strstr(name, "characters\\grunt\\"))
	{
		entry->faction = _firefight_faction_covenant;
		entry->unit_toggle_bit = _ff_unit_covenant_grunts_bit;
		entry->tier = strstr(name, "heavy") ? _ff_tier_commander : _ff_tier_fodder;
		entry->subtype = _ff_sub_grunt;
	}
	else if (strstr(name, "characters\\jackal\\"))
	{
		entry->faction = _firefight_faction_covenant;
		entry->unit_toggle_bit = _ff_unit_covenant_jackals_bit;
		entry->tier = _ff_tier_infantry;
		entry->subtype = _ff_sub_jackal;
	}
	else if (strstr(name, "characters\\elite\\"))
	{
		entry->faction = _firefight_faction_covenant;
		if (strstr(name, "commander") || strstr(name, "sword") || strstr(name, "zealot"))
		{
			entry->unit_toggle_bit = _ff_unit_covenant_zealots_bit;
			entry->tier = _ff_tier_boss_heavy;
			entry->subtype = _ff_sub_elite_zealot;
		}
		else if (strstr(name, "specops") || strstr(name, "stealth"))
		{
			entry->unit_toggle_bit = _ff_unit_covenant_specops_stealth_bit;
			entry->tier = _ff_tier_commander;
			entry->subtype = _ff_sub_elite_stealth;
		}
		else if (strstr(name, "major"))
		{
			entry->unit_toggle_bit = _ff_unit_covenant_elites_bit;
			entry->tier = _ff_tier_commander;
			entry->subtype = _ff_sub_elite_major;
		}
		else
		{
			entry->unit_toggle_bit = _ff_unit_covenant_elites_bit;
			entry->tier = _ff_tier_commander;
			entry->subtype = _ff_sub_elite_minor;
		}
	}
	else if (strstr(name, "characters\\hunter\\"))
	{
		entry->faction = _firefight_faction_covenant;
		entry->unit_toggle_bit = _ff_unit_covenant_hunters_bit;
		entry->tier = _ff_tier_boss_heavy;
		entry->subtype = _ff_sub_hunter;
	}
	else if (strstr(name, "flood_infection") || strstr(name, "flood infection"))
	{
		entry->faction = _firefight_faction_flood;
		entry->unit_toggle_bit = _ff_unit_flood_infection_bit;
		entry->tier = _ff_tier_fodder;
	}
	else if (strstr(name, "floodcombat"))
	{
		entry->faction = _firefight_faction_flood;
		entry->unit_toggle_bit = _ff_unit_flood_combat_bit;
		entry->tier = _ff_tier_infantry;
	}
	else if (strstr(name, "floodcarrier"))
	{
		entry->faction = _firefight_faction_flood;
		entry->unit_toggle_bit = _ff_unit_flood_carrier_bit;
		entry->tier = _ff_tier_commander;
	}
	else if (strstr(name, "characters\\sentinel\\"))
	{
		entry->faction = _firefight_faction_sentinels;
		entry->unit_toggle_bit = _ff_unit_sentinels_bit;
		entry->tier = strstr(name, "major") ? _ff_tier_commander : _ff_tier_infantry;
	}
	else if (strstr(name, "characters\\marine") || strstr(name, "characters\\crewman\\"))
	{
		entry->faction = _firefight_faction_unsc_humans;
		entry->is_human_ally_or_enemy = TRUE;
		if (strstr(name, "armored") || strstr(name, "rocket") || strstr(name, "sniper"))
		{
			entry->unit_toggle_bit = _ff_unit_human_odst_heavies_bit;
			entry->tier = strstr(name, "rocket") ? _ff_tier_boss_heavy : _ff_tier_commander;
		}
		else
		{
			entry->unit_toggle_bit = _ff_unit_human_marines_bit;
			entry->tier = strstr(name, "crewman") ? _ff_tier_fodder : _ff_tier_infantry;
		}
	}

	if (entry->faction != NONE && entry->unit_toggle_bit != NONE)
	{
		g_roster_count++;
	}
}

static boolean point_is_in_valid_bsp(real_point3d const *pt)
{
	struct location loc;

	if (!pt)
		return FALSE;
	if (scenario_leaf_index_from_point(pt) == NONE)
		return FALSE;
	scenario_location_from_point(&loc, pt);
	return loc.cluster_index != NONE;
}

static boolean is_sealed_campaign_intro_spot(real_point3d const *pos)
{
	char const *scen_name;

	if (!pos || global_scenario_index == NONE)
		return FALSE;
	scen_name = tag_get_name(global_scenario_index);
	if (!scen_name)
		return FALSE;

	/* a30 (Halo): skip the sealed Bumblebee lifepod at (-275.3, -730.4, -102.7) */
	if (strstr(scen_name, "a30"))
	{
		if (pos->x > -292.0f && pos->x < -258.0f &&
			pos->y > -748.0f && pos->y < -712.0f)
		{
			return TRUE;
		}
	}
	/* a10 (Pillar of Autumn): skip sealed cryo tubes at (-57.8, 37.9) */
	if (strstr(scen_name, "a10"))
	{
		if (pos->x > -72.0f && pos->x < -45.0f &&
			pos->y > 24.0f && pos->y < 52.0f)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void add_spawn_point(real_point3d const *pos, real facing)
{
	short i;

	if (!pos || g_spawn_point_count >= MAX_SPAWN_POINTS)
		return;
	if (is_sealed_campaign_intro_spot(pos))
		return;

	for (i = 0; i < g_spawn_point_count; i++)
	{
		if (distance_squared3d(&g_spawn_points[i], pos) < 4.0f)
			return;
	}

	g_spawn_points[g_spawn_point_count] = *pos;
	g_spawn_facings[g_spawn_point_count] = facing;
	g_spawn_point_count++;
}

static boolean is_stock_map_stem(char const *stem)
{
	static char const *const k_stock_maps[] = {
		/* Stock Multiplayer (19) */
		"beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout",
		"damnation", "dangercanyon", "deathisland", "gephyrophobia", "hangemhigh",
		"icefields", "infinity", "longest", "prisoner", "putput",
		"ratrace", "sidewinder", "timberland", "wizard",
		/* Stock Campaign (10) */
		"a10", "a30", "a50", "b30", "b40",
		"c10", "c20", "c40", "d20", "d40"
	};
	char lowered[128];
	size_t i;

	if (!stem || !stem[0])
		return FALSE;

	lowercase_copy(stem, lowered, sizeof(lowered));
	for (i = 0; i < NUMBEROF(k_stock_maps); i++)
	{
		if (strcmp(lowered, k_stock_maps[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

static boolean is_default_stock_map(void)
{
	char const *loaded_name = cache_file_loaded_map_name();
	char const *scen_path = (global_scenario_index != NONE) ? tag_get_name(global_scenario_index) : NULL;
	char loaded_stem[128];

	if (loaded_name && loaded_name[0])
	{
		char const *slash = strrchr(loaded_name, '\\');
		char *at_sep;
		if (!slash)
			slash = strrchr(loaded_name, '/');
		lowercase_copy(slash ? (slash + 1) : loaded_name, loaded_stem, sizeof(loaded_stem));
		at_sep = strchr(loaded_stem, '@');
		if (at_sep)
			*at_sep = '\0';

		if (is_stock_map_stem(loaded_stem))
			return TRUE;

		/* If this is a Forge-saved variant (e.g. bloodgulch_forge), check whether its base scenario is a stock map */
		if (strstr(loaded_stem, "_forge") != NULL ||
			(scen_path && (strncmp(scen_path, "levels\\forge\\", 13) == 0 || strncmp(scen_path, "levels/forge/", 13) == 0)))
		{
			if (scen_path && scen_path[0])
			{
				char const *leaf = scen_path;
				char const *p;
				for (p = scen_path; *p; p++)
				{
					if (*p == '\\' || *p == '/')
						leaf = p + 1;
				}
				return is_stock_map_stem(leaf);
			}
		}

		/* Any other custom_maps/<name>.map is a 3rd-party custom map, even if its author kept a stock scenario tag path */
		return FALSE;
	}

	if (scen_path && scen_path[0])
	{
		char const *leaf = scen_path;
		char const *p;
		for (p = scen_path; *p; p++)
		{
			if (*p == '\\' || *p == '/')
				leaf = p + 1;
		}
		return is_stock_map_stem(leaf);
	}

	return TRUE;
}

static boolean ff_marker_matches_round(int round_filter, short round_num, boolean is_bonus)
{
	if (is_bonus)
		return (round_filter == FF_ROUND_ALL || round_filter == FF_ROUND_BONUS);

	switch (round_filter)
	{
	case FF_ROUND_ALL:    return TRUE;
	case FF_ROUND_1:      return (round_num == 1);
	case FF_ROUND_2:      return (round_num == 2);
	case FF_ROUND_3:      return (round_num == 3);
	case FF_ROUND_4:      return (round_num == 4);
	case FF_ROUND_5:      return (round_num == 5);
	case FF_ROUND_1_TO_3: return (round_num >= 1 && round_num <= 3);
	case FF_ROUND_4_TO_5: return (round_num >= 4 && round_num <= 5);
	case FF_ROUND_BONUS:  return FALSE;
	default:              return TRUE;
	}
}

static boolean ff_marker_matches_wave(int wave_filter, short wave_num, boolean is_bonus, boolean is_boss_wave)
{
	if (is_bonus)
		return TRUE;

	switch (wave_filter)
	{
	case FF_WAVE_ALL:    return TRUE;
	case FF_WAVE_1:      return (wave_num == 1);
	case FF_WAVE_2:      return (wave_num == 2);
	case FF_WAVE_3_BOSS: return (wave_num == 3 || is_boss_wave);
	default:             return TRUE;
	}
}

static short count_custom_ff_spawn_markers(unsigned long allowed_type_mask)
{
	struct scenario *scen = global_scenario_try_and_get();
	short count = 0;
	short i;

	if (!scen)
		return 0;

	for (i = 0; i < scen->netgame_flags.count; i++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
			&scen->netgame_flags, i, struct scenario_netgame_flag);
		int stype;
		if (!flag || flag->type != FF_NETGAME_FLAG_FIREFIGHT_SPAWN)
			continue;
		stype = FF_UNPACK_SPAWN_TYPE(flag->team_index);
		if (stype == FF_ROUTE_SPAWN_TYPE)
			continue;
		if (allowed_type_mask == 0xFFFFFFFFUL || (allowed_type_mask & (1UL << stype)) != 0)
			count++;
	}
	return count;
}

static boolean map_has_any_custom_ff_spawns(void)
{
	return count_custom_ff_spawn_markers(0xFFFFFFFFUL) > 0;
}

static short collect_matching_custom_ff_spawns(
	unsigned long allowed_type_mask,
	short round_num,
	short wave_num,
	boolean is_bonus,
	boolean is_boss_wave,
	boolean allow_round_wave_fallback,
	real_point3d *out_pts,
	real *out_facings,
	int *out_types,
	short max_out)
{
	struct scenario *scen = global_scenario_try_and_get();
	short count = 0;
	short pass;
	short i;

	if (!scen || max_out <= 0)
		return 0;

	/* Pass 0: Exact (round + wave) match
	 * Pass 1 (if allow_round_wave_fallback): Exact round match, any wave
	 * Pass 2 (if allow_round_wave_fallback): Any round, any wave */
	for (pass = 0; pass < (allow_round_wave_fallback ? 3 : 1) && count == 0; pass++)
	{
		for (i = 0; i < scen->netgame_flags.count && count < max_out; i++)
		{
			struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
				&scen->netgame_flags, i, struct scenario_netgame_flag);
			int stype, rfilt, wfilt;

			if (!flag || flag->type != FF_NETGAME_FLAG_FIREFIGHT_SPAWN)
				continue;

			stype = FF_UNPACK_SPAWN_TYPE(flag->team_index);
			rfilt = FF_UNPACK_ROUND_FILTER(flag->team_index);
			wfilt = FF_UNPACK_WAVE_FILTER(flag->team_index);

			if ((allowed_type_mask & (1UL << stype)) == 0)
				continue;
			if (pass == 0 && (!ff_marker_matches_round(rfilt, round_num, is_bonus) ||
				!ff_marker_matches_wave(wfilt, wave_num, is_bonus, is_boss_wave)))
			{
				continue;
			}
			if (pass == 1 && !ff_marker_matches_round(rfilt, round_num, is_bonus))
				continue;

			if (out_pts)
				out_pts[count] = flag->position;
			if (out_facings)
				out_facings[count] = flag->facing;
			if (out_types)
				out_types[count] = stype;
			count++;
		}
	}

	return count;
}

static void discover_map_spawn_points(void)
{
	struct scenario *scen = global_scenario_get();
	struct encounter_definition *encounters;
	long encounter_count;
	short i, j;

	g_spawn_point_count = 0;
	if (!scen)
		return;

	/* 1. AI encounter squad starting locations: the map's, as its AI doesn't
	play here and Forge has emptied those squads (port/forge/src/forge_ai.c) */
#if OPENCE_ENABLE_FORGE
	{
		extern struct encounter_definition *forge_ai_map_encounters(long *count);

		encounters = forge_ai_map_encounters(&encounter_count);
	}
#else
	encounters = (struct encounter_definition *)scen->ai_encounters.address;
	encounter_count = scen->ai_encounters.count;
#endif
	for (i = 0; encounters && i < encounter_count && g_spawn_point_count < MAX_SPAWN_POINTS; i++)
	{
		struct encounter_definition *enc = &encounters[i];

		for (j = 0; j < enc->squads.count && g_spawn_point_count < MAX_SPAWN_POINTS; j++)
		{
			struct squad_definition *sq = TAG_BLOCK_GET_ELEMENT(
				&enc->squads, j, struct squad_definition);
			short k;
			if (!sq)
				continue;

			for (k = 0; k < sq->starting_locations.count && g_spawn_point_count < MAX_SPAWN_POINTS; k++)
			{
				struct actor_starting_location *loc = TAG_BLOCK_GET_ELEMENT(
					&sq->starting_locations, k, struct actor_starting_location);
				if (loc)
					add_spawn_point(&loc->position, loc->facing);
			}
		}
	}

	/* 2. Scenario player starting locations */
	for (i = 0; i < scen->players.count && g_spawn_point_count < MAX_SPAWN_POINTS; i++)
	{
		struct player_starting_location *loc = TAG_BLOCK_GET_ELEMENT(
			&scen->players, i, struct player_starting_location);
		if (loc)
			add_spawn_point(&loc->position, loc->facing);
	}

	/* 3. Scenario netgame flags & equipment points (excluding custom Firefight Spawn markers) */
	for (i = 0; i < scen->netgame_flags.count && g_spawn_point_count < MAX_SPAWN_POINTS; i++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
			&scen->netgame_flags, i, struct scenario_netgame_flag);
		if (flag && flag->type != FF_NETGAME_FLAG_FIREFIGHT_SPAWN &&
			flag->type != FF_NETGAME_FLAG_FIREFIGHT_GENERATOR)
			add_spawn_point(&flag->position, flag->facing);
	}
	for (i = 0; i < scen->netgame_equipment.count && g_spawn_point_count < MAX_SPAWN_POINTS; i++)
	{
		struct scenario_netgame_equipment *eq = TAG_BLOCK_GET_ELEMENT(
			&scen->netgame_equipment, i, struct scenario_netgame_equipment);
		if (eq)
			add_spawn_point(&eq->position, eq->facing);
	}
}

static short count_valid_in_bsp_spawn_points(void)
{
	short count = 0;
	short i;

	for (i = 0; i < g_spawn_point_count; i++)
	{
		if (point_is_in_valid_bsp(&g_spawn_points[i]))
			count++;
	}
	return count;
}

static void ensure_active_firefight_structure_bsp(void)
{
	struct scenario *scen = global_scenario_get();
	short current_valid;
	short best_bsp = 0;
	short best_count = 0;
	short bsp_idx;
	short write_idx = 0;
	short i;

	if (!scen)
		return;

	current_valid = count_valid_in_bsp_spawn_points();
	if (current_valid < 4 && scen->structure_bsp_references.count > 1)
	{
		best_bsp = global_structure_bsp_index_get();
		if (best_bsp == NONE)
			best_bsp = 0;
		best_count = current_valid;

		for (bsp_idx = 0; bsp_idx < scen->structure_bsp_references.count; bsp_idx++)
		{
			short c;
			if (bsp_idx != global_structure_bsp_index_get())
				scenario_switch_structure_bsp(bsp_idx);
			c = count_valid_in_bsp_spawn_points();
			if (c > best_count)
			{
				best_count = c;
				best_bsp = bsp_idx;
			}
			if (c >= 12)
				break;
		}
		if (global_structure_bsp_index_get() != best_bsp)
			scenario_switch_structure_bsp(best_bsp);
	}

	/* Keep only points verified inside the active Structure BSP */
	for (i = 0; i < g_spawn_point_count; i++)
	{
		if (point_is_in_valid_bsp(&g_spawn_points[i]))
		{
			g_spawn_points[write_idx] = g_spawn_points[i];
			g_spawn_facings[write_idx] = g_spawn_facings[i];
			write_idx++;
		}
	}
	g_spawn_point_count = write_idx;
}

boolean firefight_select_player_spawn_point(
	long player_index,
	real_point3d *out_position,
	real *out_facing)
{
	struct scenario *scen = global_scenario_get();
	short valid_player_spawns[64];
	short valid_player_count = 0;
	short abs_player = player_index != NONE ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : 0;
	short i;

	if (!out_position || !out_facing || !scen)
		return FALSE;

	if (g_spawn_point_count == 0)
	{
		discover_map_spawn_points();
		ensure_active_firefight_structure_bsp();
	}

	for (i = 0; i < scen->players.count && valid_player_count < 64; i++)
	{
		struct player_starting_location *loc = TAG_BLOCK_GET_ELEMENT(
			&scen->players, i, struct player_starting_location);
		if (!loc)
			continue;
		if (is_sealed_campaign_intro_spot(&loc->position))
			continue;
		if (point_is_in_valid_bsp(&loc->position))
			valid_player_spawns[valid_player_count++] = i;
	}

	if (valid_player_count > 0)
	{
		/* everyone at one base, by the ordnance drop that lands by the first
		living player: the starting locations nearest the first one chosen,
		nearest first. A player's first spawn has a place of its own there, a
		respawn any of them */
		short nearest[FF_PLAYER_BASE_SPAWNS];
		real nearest_distance[FF_PLAYER_BASE_SPAWNS];
		short nearest_count = 0;
		struct player_starting_location *base;
		struct player_starting_location *loc;
		struct player_datum *player = player_index != NONE && player_index != 0 ?
			player_try_and_get(player_index) : NULL;
		short turn = abs_player;

		for (i = 0; i < valid_player_count && valid_player_spawns[i] != g_player_base_location; i++)
			;
		if (i == valid_player_count)
			g_player_base_location = valid_player_spawns[spawner_rand() % (unsigned long)valid_player_count];
		base = TAG_BLOCK_GET_ELEMENT(&scen->players, g_player_base_location, struct player_starting_location);
		for (i = 0; i < valid_player_count; i++)
		{
			real distance;
			short place;

			loc = TAG_BLOCK_GET_ELEMENT(&scen->players, valid_player_spawns[i], struct player_starting_location);
			distance = distance_squared3d(&base->position, &loc->position);
			for (place = nearest_count; place > 0 && nearest_distance[place - 1] > distance; place--)
			{
				if (place < FF_PLAYER_BASE_SPAWNS)
				{
					nearest[place] = nearest[place - 1];
					nearest_distance[place] = nearest_distance[place - 1];
				}
			}
			if (place < FF_PLAYER_BASE_SPAWNS)
			{
				nearest[place] = valid_player_spawns[i];
				nearest_distance[place] = distance;
				if (nearest_count < FF_PLAYER_BASE_SPAWNS)
					nearest_count++;
			}
		}
		if (player && player->statistics.deaths > 0)
			turn = (short)(spawner_rand() % (unsigned long)nearest_count);
		loc = TAG_BLOCK_GET_ELEMENT(&scen->players, nearest[turn % nearest_count], struct player_starting_location);
		*out_position = loc->position;
		*out_facing = loc->facing;
		return TRUE;
	}

	if (g_spawn_point_count > 0)
	{
		short pick = (short)((abs_player * 3 + (short)spawner_rand()) % g_spawn_point_count);
		if (pick < 0)
			pick = 0;
		*out_position = g_spawn_points[pick];
		*out_facing = g_spawn_facings[pick];
		return TRUE;
	}

	return FALSE;
}

static void scan_ordnance_weapon_and_equipment_tags(void)
{
	struct tag_iterator iter;
	long tag_index;

	ff_power_weapon_tag_count = 0;
	ff_standard_weapon_tag_count = 0;
	ff_ammo_tag_count = 0;
	ff_grenade_tag_count = 0;

	tag_iterator_new(&iter, WEAPON_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iter)) != NONE)
	{
		char const *raw_name = tag_get_name(tag_index);
		char name[256];

		if (!raw_name)
			continue;
		lowercase_copy(raw_name, name, sizeof(name));

		/* Exclude vehicle turrets, flags, oddballs, gravity rifle, and AI-only unpickable weapons */
		if (strstr(name, "vehicles\\") || strstr(name, "flag") || strstr(name, "ball") ||
			strstr(name, "gravity") || strstr(name, "energy sword") || strstr(name, "sentinel") ||
			strstr(name, "gun turret") || strstr(name, "needler\\needler"))
		{
			continue;
		}

		if (strstr(name, "rocket launcher") ||
			strstr(name, "sniper rifle") ||
			strstr(name, "shotgun") ||
			strstr(name, "plasma_cannon") ||
			strstr(name, "flamethrower"))
		{
			if (ff_power_weapon_tag_count < MAX_ORDNANCE_TAG_POOL)
				ff_power_weapon_tags[ff_power_weapon_tag_count++] = tag_index;
			if ((strstr(name, "shotgun") || strstr(name, "sniper rifle")) &&
				ff_standard_weapon_tag_count < MAX_ORDNANCE_TAG_POOL)
			{
				ff_standard_weapon_tags[ff_standard_weapon_tag_count++] = tag_index;
			}
		}
		else if (strstr(name, "assault rifle") ||
			strstr(name, "pistol") ||
			strstr(name, "plasma rifle") ||
			strstr(name, "plasma pistol") ||
			strstr(name, "mp_needler"))
		{
			if (ff_standard_weapon_tag_count < MAX_ORDNANCE_TAG_POOL)
				ff_standard_weapon_tags[ff_standard_weapon_tag_count++] = tag_index;
		}
	}

	tag_iterator_new(&iter, EQUIPMENT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iter)) != NONE)
	{
		char const *raw_name = tag_get_name(tag_index);
		char name[256];

		if (!raw_name)
			continue;
		lowercase_copy(raw_name, name, sizeof(name));

		if (strstr(name, "frag grenade") || strstr(name, "plasma grenade"))
		{
			if (ff_grenade_tag_count < MAX_ORDNANCE_TAG_POOL)
				ff_grenade_tags[ff_grenade_tag_count++] = tag_index;
		}
		else if (strstr(name, "ammo") ||
			strstr(name, "health pack") ||
			strstr(name, "shield") ||
			strstr(name, "camouflage") ||
			strstr(name, "double speed"))
		{
			if (ff_ammo_tag_count < MAX_ORDNANCE_TAG_POOL)
				ff_ammo_tags[ff_ammo_tag_count++] = tag_index;
		}
	}
}

/* the variant's profile's tags, from the level's (randomizer.c) */
static void scan_custom_tags(void)
{
	short index;

	ff_custom_enemy_count = 0;
	ff_custom_weapon_count = 0;
	{
		long found[MAX_ROSTER_VARIANTS];
		long count = randomizer_firefight_tags(ACTOR_VARIANT_DEFINITION_TAG, found, MAX_ROSTER_VARIANTS);

		for (index = 0; index < count; index++)
		{
			struct actor_variant_definition *variant_def = actor_variant_definition_get(found[index]);

			if (!variant_def || variant_def->unit_reference.index == NONE || variant_def->actor_reference.index == NONE)
				continue;
			ff_custom_enemy_tags[ff_custom_enemy_count] = found[index];
			ff_custom_enemy_tiers[ff_custom_enemy_count++] = (byte)randomizer_firefight_actor_tier(found[index]);
		}
		count = randomizer_firefight_tags(WEAPON_DEFINITION_TAG, found, MAX_ROSTER_VARIANTS);
		for (index = 0; index < count; index++)
		{
			ff_custom_weapon_tags[ff_custom_weapon_count] = found[index];
			ff_custom_weapon_power[ff_custom_weapon_count++] = randomizer_firefight_weapon_power(found[index]);
		}
	}
	if (ff_custom_enemy_count || ff_custom_weapon_count)
		error(_error_silent, "firefight: the variant's profile adds %d AI and %d weapons", ff_custom_enemy_count,
			ff_custom_weapon_count);
}

/* whether the roster has any entry ONLY MAP lets the waves pick */
static boolean roster_has_maps_own(void)
{
	short i;

	for (i = 0; i < g_roster_count; i++)
	{
		if (g_roster[i].maps_own && !g_roster[i].custom_off)
			return TRUE;
	}
	return FALSE;
}

/* whether ONLY MAP keeps the roster's pick from this entry */
static boolean roster_entry_left_out(struct firefight_roster_entry const *e)
{
	return (g_roster_map_only && !e->maps_own) || e->custom_off;
}

/* one of the profile's AI, or of the map's own (MAP CONTENT), in place of a
stock enemy: half the time, or every time when ONLY MAP leaves the roster
none to pick; of the tier wanted, else of any below the bosses (NONE: the
roster's) */
static long pick_custom_enemy(byte wanted_tier, byte *out_tier)
{
	long tags[MAX_ROSTER_VARIANTS * 2];
	byte tiers[MAX_ROSTER_VARIANTS * 2];
	short pool = 0;
	short candidates[MAX_ROSTER_VARIANTS * 2];
	short count = 0;
	short index;
	boolean always = g_roster_map_only && !roster_has_maps_own();

	for (index = 0; index < ff_custom_enemy_count; index++)
	{
		tags[pool] = ff_custom_enemy_tags[index];
		tiers[pool++] = ff_custom_enemy_tiers[index];
	}
	for (index = 0; index < ff_map_enemy_count; index++)
	{
		tags[pool] = ff_map_enemy_tags[index];
		tiers[pool++] = ff_map_enemy_tiers[index];
	}
	if (!pool || (!always && (spawner_rand() & 1)))
		return NONE;
	for (index = 0; index < pool; index++)
	{
		if (tiers[index] == wanted_tier)
			candidates[count++] = index;
	}
	if (!count)
	{
		for (index = 0; index < pool; index++)
		{
			if (tiers[index] != _ff_tier_boss_heavy || wanted_tier == _ff_tier_boss_heavy)
				candidates[count++] = index;
		}
	}
	/* (all of them bosses, and none wanted: a boss anyway, when there's
	nothing else to pick) */
	if (!count && always)
	{
		for (index = 0; index < pool; index++)
			candidates[count++] = index;
	}
	if (!count)
		return NONE;
	index = candidates[spawner_rand() % (unsigned long)count];
	if (out_tier)
		*out_tier = tiers[index];
	return tags[index];
}

static boolean is_custom_enemy(long variant_tag_index)
{
	short index;

	for (index = 0; index < ff_custom_enemy_count; index++)
	{
		if (ff_custom_enemy_tags[index] == variant_tag_index)
			return TRUE;
	}
	for (index = 0; index < ff_map_enemy_count; index++)
	{
		if (ff_map_enemy_tags[index] == variant_tag_index)
			return TRUE;
	}
	return FALSE;
}

/* one of the profile's weapons for a drop (a heavy one's its power weapons
first), NONE without any */
static long pick_custom_weapon(boolean heavy)
{
	short candidates[MAX_ROSTER_VARIANTS];
	short count = 0;
	short index;

	for (index = 0; heavy && index < ff_custom_weapon_count; index++)
	{
		if (ff_custom_weapon_power[index])
			candidates[count++] = index;
	}
	if (!count)
	{
		for (index = 0; index < ff_custom_weapon_count; index++)
			candidates[count++] = index;
	}
	return count ? ff_custom_weapon_tags[candidates[spawner_rand() % (unsigned long)count]] : NONE;
}

static void scan_map_content(void);

void firefight_spawner_tags_loaded(char const *scenario_name)
{
	(void)scenario_name;
}

void firefight_spawner_initialize_map(void)
{
	struct tag_iterator iter;
	long tag_index;
	short i;

	g_roster_count = 0;
	g_player_base_location = NONE;
	for (i = 0; i < MAX_FRIENDLY_UNITS; i++)
		g_friendly_units[i] = NONE;
	g_friendly_unit_count = 0;
	g_queued_reinforcement_enemies = 0;
	ff_state.enemies_queued = 0;
	g_spawner_rng = (unsigned long)(game_time_get() + 0x5A17C3UL);
	g_last_aggro_refresh_tick = 0;
	for (i = 0; i < FIREFIGHT_MAX_TRACKED_ENEMIES; i++)
		g_generator_attack_units[i] = NONE;

	tag_iterator_new(&iter, ACTOR_VARIANT_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iter)) != NONE)
	{
		classify_actor_variant(tag_index);
	}

	scan_ordnance_weapon_and_equipment_tags();
	scan_custom_tags();
	scan_map_content();
	discover_map_spawn_points();
	ensure_active_firefight_structure_bsp();
}

void firefight_spawner_dispose_map(void)
{
	short i;

	ff_custom_enemy_count = 0;
	ff_custom_weapon_count = 0;
	ff_map_enemy_count = 0;
	ff_map_weapon_count = 0;
	ff_map_vehicle_count = 0;
	g_roster_map_only = FALSE;
	g_roster_count = 0;
	g_spawn_point_count = 0;
	/* (the map's objects went with it) */
	for (i = 0; i < FIREFIGHT_MAX_ORDNANCE_PODS; i++)
		g_pod_objects[i] = NONE;
	g_pod_shown_time = NONE;
	g_drop_area_count = 0;
	g_drop_areas_found = FALSE;
	g_pod_content_count = 0;
	g_pod_held_count = 0;
	g_friendly_unit_count = 0;
	g_queued_reinforcement_enemies = 0;
	ff_state.enemies_queued = 0;
	ff_power_weapon_tag_count = 0;
	ff_standard_weapon_tag_count = 0;
	ff_ammo_tag_count = 0;
	ff_grenade_tag_count = 0;
}

short firefight_queued_enemies_count(void)
{
	short dropship_pending = (ff_state.active_dropship.phase != DROPSHIP_PHASE_NONE &&
		!ff_state.active_dropship.payload_deployed &&
		ff_state.active_dropship.squad_to_spawn > 0) ?
		ff_state.active_dropship.squad_to_spawn : 0;

	return (short)(g_queued_reinforcement_enemies + dropship_pending);
}

/* ---------- Tracked Enemy Helpers */

void firefight_track_enemy_unit(
	long unit_index,
	byte tier)
{
	short i;

	if (unit_index == NONE)
		return;
	for (i = 0; i < FIREFIGHT_MAX_TRACKED_ENEMIES; i++)
	{
		if (ff_state.tracked_enemy_units[i] == NONE)
		{
			ff_state.tracked_enemy_units[i] = unit_index;
			ff_state.tracked_enemy_tiers[i] = tier;
			ff_state.enemies_alive++;
			return;
		}
	}
}

void firefight_prune_dead_tracked_enemies(void)
{
	short alive = 0;
	short i;

	for (i = 0; i < FIREFIGHT_MAX_TRACKED_ENEMIES; i++)
	{
		long u_idx = ff_state.tracked_enemy_units[i];
		if (u_idx != NONE)
		{
			struct unit_datum *u = (struct unit_datum *)object_try_and_get_and_verify_type(
				u_idx, _object_mask_unit);
			if (u && !TEST_FLAG(u->object.damage_flags, _object_dead_bit))
			{
				alive++;
			}
			else
			{
				ff_state.tracked_enemy_units[i] = NONE;
				ff_state.tracked_enemy_tiers[i] = 0;
			}
		}
	}
	ff_state.enemies_alive = alive;
}

/* ---------- Living Player & Spawn Zone Queries */

static short get_living_player_positions(real_point3d *out_positions, long *out_units, short max_count)
{
	struct data_iterator iter;
	struct player_datum *player;
	short count = 0;

	if (!player_data)
		return 0;

	data_iterator_new(&iter, player_data);
	while ((player = data_iterator_next(&iter)) != NULL && count < max_count)
	{
		if (player->unit_index != NONE)
		{
			struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(
				player->unit_index, _object_mask_unit);
			if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			{
				if (out_positions)
					object_get_origin(player->unit_index, &out_positions[count]);
				if (out_units)
					out_units[count] = player->unit_index;
				count++;
			}
		}
	}

	return count;
}

static short get_living_human_player_positions(
	real_point3d *out_positions,
	long *out_units,
	short max_count)
{
	struct data_iterator iter;
	struct player_datum *player;
	short count = 0;

	if (!player_data)
		return 0;

	data_iterator_new(&iter, player_data);
	while ((player = data_iterator_next(&iter)) != NULL && count < max_count)
	{
		/* (a game's bots go where they like: the waves come to the people) */
		if (player->unit_index != NONE && !bots_machine_is_bot(player->network_player_data.machine_index))
		{
			struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(
				player->unit_index, _object_mask_unit);
			if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			{
				if (out_positions)
					object_get_origin(player->unit_index, &out_positions[count]);
				if (out_units)
					out_units[count] = player->unit_index;
				count++;
			}
		}
	}
	return count;
}

/* how far from the players a wave's zones are wanted: a big map's far side
is a walk, and a wave there is one the players go looking for */
#define FF_ZONE_NEAR_DISTANCE 40.0f

/* the one of the players nearest the rest of them, whom the waves and the
drops come to (a player gone off alone doesn't drag them away) */
static short central_player_index(real_point3d const *positions, short count)
{
	real_point3d middle = { 0.0f, 0.0f, 0.0f };
	real best_distance = 0.0f;
	short best = 0;
	short i;

	for (i = 0; i < count; i++)
	{
		middle.x += positions[i].x / (real)count;
		middle.y += positions[i].y / (real)count;
		middle.z += positions[i].z / (real)count;
	}
	for (i = 0; i < count; i++)
	{
		real distance = distance3d(&positions[i], &middle);

		if (i == 0 || distance < best_distance)
		{
			best_distance = distance;
			best = i;
		}
	}
	return best;
}

/* the candidates, nearest first, cut to those near the point: at least one
more than the wave wants, however far (the map's nearest, if none are near).
What's left for the wave to pick from; a small map loses none of them */
static short zones_keep_near(
	real_point3d const *points,
	short *indices,
	short count,
	real_point3d const *anchor,
	short desired)
{
	short kept = 0;
	short i;

	for (i = 1; i < count; i++)
	{
		short index = indices[i];
		real distance = distance3d(&points[index], anchor);
		short j = i;

		while (j > 0 && distance3d(&points[indices[j - 1]], anchor) > distance)
		{
			indices[j] = indices[j - 1];
			j--;
		}
		indices[j] = index;
	}
	while (kept < count && distance3d(&points[indices[kept]], anchor) <= FF_ZONE_NEAR_DISTANCE)
		kept++;
	return MAX(kept, MIN(count, (short)(desired + 1)));
}

static short select_wave_spawn_zones(
	real_point3d *out_zones,
	real *out_facings,
	short desired_zones,
	short round_num,
	short wave_num,
	boolean is_bonus,
	boolean is_boss_wave)
{
	real_point3d player_positions[16];
	short human_count = get_living_human_player_positions(player_positions, NULL, 16);
	struct scenario *scen = global_scenario_get();
	short candidate_indices[MAX_SPAWN_POINTS];
	short candidate_count = 0;
	short selected = 0;
	short i, s;

	/* Custom Forge Firefight Spawn Override:
	 * When the map has placed Firefight Infantry/Dropship Spawn markers ('ffsp' / flag->type == 15),
	 * limit AI wave spawning to the user's placed Firefight Spawn markers! */
	{
		unsigned long infantry_mask =
			(1UL << FF_SPAWN_TYPE_AI_INFANTRY) |
			(1UL << FF_SPAWN_TYPE_AI_AND_VEHICLES) |
			(1UL << FF_SPAWN_TYPE_DROPSHIP_ZONE);
		if (count_custom_ff_spawn_markers(infantry_mask) > 0)
		{
			real_point3d custom_pts[MAX_SPAWN_POINTS];
			real custom_facings[MAX_SPAWN_POINTS];
			int custom_types[MAX_SPAWN_POINTS];
			short custom_count = collect_matching_custom_ff_spawns(
				infantry_mask,
				round_num,
				wave_num,
				is_bonus,
				is_boss_wave,
				TRUE,
				custom_pts,
				custom_facings,
				custom_types,
				MAX_SPAWN_POINTS);

			if (custom_count > 0)
			{
				/* Prefer custom markers >= 5.0u from living players if multiple markers exist */
				for (i = 0; i < custom_count; i++)
				{
					real min_player_dist = 9999.0f;
					short p;
					for (p = 0; p < human_count; p++)
					{
						real d = distance3d(&custom_pts[i], &player_positions[p]);
						if (d < min_player_dist)
							min_player_dist = d;
					}
					if (human_count == 0 || min_player_dist >= 5.0f)
						candidate_indices[candidate_count++] = i;
				}
				if (candidate_count == 0)
				{
					for (i = 0; i < custom_count; i++)
						candidate_indices[candidate_count++] = i;
				}
				if (human_count > 0)
				{
					candidate_count = zones_keep_near(
						custom_pts,
						candidate_indices,
						candidate_count,
						&player_positions[central_player_index(player_positions, human_count)],
						desired_zones);
				}

				/* If a DROPSHIP ZONE marker is among candidates, place it in slot 0 so the Spirit/Pelican targets it first */
				for (i = 0; i < candidate_count; i++)
				{
					short c_idx = candidate_indices[i];
					if (custom_types[c_idx] == FF_SPAWN_TYPE_DROPSHIP_ZONE)
					{
						out_zones[0] = custom_pts[c_idx];
						out_facings[0] = custom_facings[c_idx];
						selected = 1;
						candidate_indices[i] = candidate_indices[--candidate_count];
						break;
					}
				}

				while (selected < desired_zones && candidate_count > 0)
				{
					short pick_slot = (short)(spawner_rand() % (unsigned long)candidate_count);
					short c_idx = candidate_indices[pick_slot];
					boolean too_close = FALSE;

					for (s = 0; s < selected; s++)
					{
						if (distance3d(&out_zones[s], &custom_pts[c_idx]) < 2.5f)
						{
							too_close = TRUE;
							break;
						}
					}

					candidate_indices[pick_slot] = candidate_indices[--candidate_count];
					if (!too_close || candidate_count == 0)
					{
						out_zones[selected] = custom_pts[c_idx];
						out_facings[selected] = custom_facings[c_idx];
						selected++;
					}
				}

				return selected;
			}
		}
	}

	if (g_spawn_point_count == 0)
		discover_map_spawn_points();

	/* Pass 1: Strict safe distance from living players (>= 16u), generators (>= 12u), and player spawns (>= 10u) */
	for (i = 0; i < g_spawn_point_count; i++)
	{
		real min_player_dist = 9999.0f;
		real min_gen_dist = 9999.0f;
		real min_spawn_dist = 9999.0f;
		short p, g;

		if (!point_is_in_valid_bsp(&g_spawn_points[i]))
			continue;

		for (p = 0; p < human_count; p++)
		{
			real d = distance3d(&g_spawn_points[i], &player_positions[p]);
			if (d < min_player_dist)
				min_player_dist = d;
		}

		for (g = 0; g < ff_state.generator_count && g < FIREFIGHT_MAX_GENERATORS; g++)
		{
			if (ff_state.generators[g].alive)
			{
				real d = distance3d(&g_spawn_points[i], &ff_state.generators[g].position);
				if (d < min_gen_dist)
					min_gen_dist = d;
			}
		}

		if (scen)
		{
			for (p = 0; p < scen->players.count && p < 8; p++)
			{
				struct player_starting_location *loc = TAG_BLOCK_GET_ELEMENT(
					&scen->players, p, struct player_starting_location);
				if (loc)
				{
					real d = distance3d(&g_spawn_points[i], &loc->position);
					if (d < min_spawn_dist)
						min_spawn_dist = d;
				}
			}
		}

		if ((human_count == 0 || (min_player_dist >= 16.0f && min_player_dist <= 85.0f)) &&
			min_gen_dist >= 12.0f &&
			min_spawn_dist >= 10.0f)
		{
			candidate_indices[candidate_count++] = i;
		}
	}

	/* Pass 2: Relaxed safe distance on smaller maps (>= 11u from players and >= 9u from generators) */
	if (candidate_count == 0)
	{
		for (i = 0; i < g_spawn_point_count; i++)
		{
			real min_player_dist = 9999.0f;
			real min_gen_dist = 9999.0f;
			short p, g;

			if (!point_is_in_valid_bsp(&g_spawn_points[i]))
				continue;

			for (p = 0; p < human_count; p++)
			{
				real d = distance3d(&g_spawn_points[i], &player_positions[p]);
				if (d < min_player_dist)
					min_player_dist = d;
			}
			for (g = 0; g < ff_state.generator_count && g < FIREFIGHT_MAX_GENERATORS; g++)
			{
				if (ff_state.generators[g].alive)
				{
					real d = distance3d(&g_spawn_points[i], &ff_state.generators[g].position);
					if (d < min_gen_dist)
						min_gen_dist = d;
				}
			}

			if ((human_count == 0 || min_player_dist >= 11.0f) && min_gen_dist >= 9.0f)
			{
				candidate_indices[candidate_count++] = i;
			}
		}
	}

	/* Pass 3: Pick valid BSP spawn points that are farthest from players and generators */
	if (candidate_count == 0)
	{
		short best_idx = NONE;
		real best_clearance = -1.0f;

		for (i = 0; i < g_spawn_point_count; i++)
		{
			real min_dist = 9999.0f;
			short p, g;

			if (!point_is_in_valid_bsp(&g_spawn_points[i]))
				continue;

			for (p = 0; p < human_count; p++)
			{
				real d = distance3d(&g_spawn_points[i], &player_positions[p]);
				if (d < min_dist)
					min_dist = d;
			}
			for (g = 0; g < ff_state.generator_count && g < FIREFIGHT_MAX_GENERATORS; g++)
			{
				if (ff_state.generators[g].alive)
				{
					real d = distance3d(&g_spawn_points[i], &ff_state.generators[g].position);
					if (d < min_dist)
						min_dist = d;
				}
			}
			if (min_dist >= 7.0f)
				candidate_indices[candidate_count++] = i;
			if (min_dist > best_clearance)
			{
				best_clearance = min_dist;
				best_idx = i;
			}
		}
		if (candidate_count == 0 && best_idx != NONE)
			candidate_indices[candidate_count++] = best_idx;
	}

	if (human_count > 0)
	{
		candidate_count = zones_keep_near(
			g_spawn_points,
			candidate_indices,
			candidate_count,
			&player_positions[central_player_index(player_positions, human_count)],
			desired_zones);
	}

	while (selected < desired_zones && candidate_count > 0)
	{
		short pick_slot = (short)(spawner_rand() % (unsigned long)candidate_count);
		short spawn_idx = candidate_indices[pick_slot];
		boolean too_close = FALSE;

		for (s = 0; s < selected; s++)
		{
			if (distance3d(&out_zones[s], &g_spawn_points[spawn_idx]) < 7.0f)
			{
				too_close = TRUE;
				break;
			}
		}

		candidate_indices[pick_slot] = candidate_indices[--candidate_count];

		if (!too_close || candidate_count == 0)
		{
			out_zones[selected] = g_spawn_points[spawn_idx];
			out_facings[selected] = g_spawn_facings[spawn_idx];
			selected++;
		}
	}

	if (selected == 0 && human_count > 0)
	{
		for (s = 0; s < desired_zones; s++)
		{
			real angle = (real)s * (2.0f * 3.14159265f / (real)desired_zones) + spawner_rand_range(0.0f, 1.0f);
			real_point3d candidate = player_positions[0];
			candidate.x += (real)cos(angle) * 20.0f;
			candidate.y += (real)sin(angle) * 20.0f;
			candidate.z += 0.3f;
			out_zones[selected] = candidate;
			out_facings[selected] = angle + 3.14159265f;
			selected++;
		}
	}

	return selected;
}

/* ---------- AI Actor Creation & Target Aggro Direction */

enum
{
	AI_UNIT_EFFECT_MAGIC_SIGHT = 3
};

static void spawn_insertion_beacon_effect(
	real_point3d const *position,
	 long owner_unit_index,
	 byte wave_faction,
	 boolean is_boss_wave)
{
	long effect_tag = NONE;
	char const *marker_names[1];
	real_point3d marker_points[1];
	real_vector3d marker_forwards[1];

	if (!position)
		return;

	if (wave_faction == _firefight_faction_flood)
		effect_tag = tag_loaded(EFFECT_DEFINITION_TAG, "characters\\floodcarrier\\effects\\carrier burst");
	if (effect_tag == NONE && is_boss_wave)
		effect_tag = tag_loaded(EFFECT_DEFINITION_TAG, "weapons\\plasma grenade\\effects\\explosion");
	if (effect_tag == NONE)
		effect_tag = tag_loaded(EFFECT_DEFINITION_TAG, "effects\\coop teleport");
	if (effect_tag == NONE)
		return;

	marker_names[0] = "";
	marker_points[0] = *position;
	marker_points[0].z += 0.35f;
	marker_forwards[0] = *global_up3d;

	skulls_effect_harmless = TRUE;
	effect_new_unattached_from_markers(
		effect_tag,
		owner_unit_index,
		NULL,
		1,
		marker_names,
		marker_points,
		marker_forwards,
		0.0f,
		0.0f,
		NULL,
		NULL,
		TRUE);
	skulls_effect_harmless = FALSE;
}

static void patch_actor_for_firefight_pursuit(
	struct actor_definition *actor_def,
	struct actor_variant_definition *variant_def)
{
	if (actor_def)
	{
		if (actor_def->perception.maximum_vision_distance < 250.0f)
			actor_def->perception.maximum_vision_distance = 250.0f;
		actor_def->perception.central_vision_angle = _pi;
		actor_def->perception.maximum_vision_angle = _pi;
		actor_def->perception.peripheral_vision_angle = _pi;
		if (actor_def->perception.peripheral_distance < 250.0f)
			actor_def->perception.peripheral_distance = 250.0f;
		if (actor_def->perception.hearing_distance < 250.0f)
			actor_def->perception.hearing_distance = 250.0f;

		SET_FLAG(actor_def->flags, _actor_definition_can_see_in_darkness_bit, TRUE);
		SET_FLAG(actor_def->flags, _actor_definition_always_charge_bit, TRUE);
		SET_FLAG(actor_def->flags, _actor_definition_charge_in_attacking_mode_bit, TRUE);
		SET_FLAG(actor_def->flags, _actor_definition_crouch_try_not_to_move_bit, FALSE);
		SET_FLAG(actor_def->flags, _actor_definition_crouch_when_guarding_bit, FALSE);
		/* (not the combat forms: their crouch is the shamble, and they must
		crouch to fire, so charging without it they never shot) */
		if (actor_def->type != _actor_combat_form)
			SET_FLAG(actor_def->flags, _actor_definition_defensive_crouch_while_charging_bit, FALSE);

		/* (not the combat forms: berserk they fire only within their berserk
		range, under 3 units, where they melee instead, so an armed one never
		shot. Without it they still charge, and those with no gun still melee) */
		if (actor_def->type == _actor_hunter ||
			actor_def->type == _actor_carrier_form ||
			actor_def->type == _actor_infection_form)
		{
			SET_FLAG(actor_def->flags, _actor_definition_always_berserk_in_attacking_mode_bit, TRUE);
			if (actor_def->berserk.proximity_berserk_distance < 45.0f)
				actor_def->berserk.proximity_berserk_distance = 45.0f;
		}
	}

	if (variant_def)
	{
		if (variant_def->ranged_combat.maximum_firing_range < 25.0f ||
			variant_def->ranged_combat.maximum_firing_range > 45.0f)
		{
			variant_def->ranged_combat.maximum_firing_range = 35.0f;
		}
		if (variant_def->ranged_combat.modified_vision_range < 350.0f)
			variant_def->ranged_combat.modified_vision_range = 350.0f;
		if (variant_def->ranged_combat.combat_range_lower_bound > 1.5f ||
			variant_def->ranged_combat.combat_range_lower_bound <= 0.0f)
		{
			variant_def->ranged_combat.combat_range_lower_bound = 1.5f;
		}
		if (variant_def->ranged_combat.combat_range_upper_bound > 10.0f ||
			variant_def->ranged_combat.combat_range_upper_bound < 4.0f)
		{
			variant_def->ranged_combat.combat_range_upper_bound = 7.5f;
		}
	}
}

static void direct_actor_toward_unit(long actor_index, long target_unit_index)
{
	struct actor_datum *actor;
	struct object_datum *target_obj;
	long prop_index;
	boolean is_generator_target = FALSE;
	short gen_slot = NONE;

	if (actor_index == NONE || target_unit_index == NONE)
		return;

	actor = actor_try_and_get(actor_index);
	if (!actor)
		return;

	actor->meta.force_active = TRUE;
	actor->meta.dormant_desire = FALSE;
	if (!actor->meta.active)
		actor_set_active(actor_index, TRUE);
	if (actor->meta.active && actor->meta.dormant)
		actor_set_dormant(actor_index, FALSE);
	if (!actor->meta.active)
		return;

	actor->external_orders.defending = FALSE;
	actor->emotions.currently_defending = FALSE;
	actor->emotions.forced_to_charge = TRUE;
	if (actor->state.artificial_combat_status < 4)
		actor->state.artificial_combat_status = 4;

	/* prop_get_base_by_unit_index requires target_unit_index to be a biped unit */
	target_obj = object_try_and_get_and_verify_type(target_unit_index, _object_mask_biped);
	if (!target_obj)
		return;

	is_generator_target = firefight_is_generator_object(target_unit_index, &gen_slot);

	prop_index = prop_get_base_by_unit_index(actor_index, target_unit_index, TRUE, TRUE);
	if (prop_index != NONE)
	{
		struct prop_datum *prop = prop_try_and_get(prop_index);

		/* Resolve orphan props to their base parent prop before magic sight acknowledgment */
		if (prop && prop->state >= _prop_state_uninspected_orphan && prop->state <= _prop_state_inspected_orphan)
		{
			prop_index = prop->parent_prop_index;
			prop = prop_index != NONE ? prop_try_and_get(prop_index) : NULL;
		}
		if (prop && (actor->target.target_prop_index != prop_index || prop->state != _prop_state_acknowledged))
		{
			actor_handle_unit_effect(actor_index, prop_index, AI_UNIT_EFFECT_MAGIC_SIGHT);
			prop = prop_try_and_get(prop_index);
		}
		if (prop && prop->state >= _prop_state_becoming_unacknowledged && prop->state <= _prop_state_acknowledged)
		{
			prop->enemy = TRUE;
			prop->ally = FALSE;
			prop->preferred_target = TRUE;
			prop->ineffability = 3; /* _actor_perception_unmistakable */
			if (!is_generator_target && actor->target.target_prop_index == NONE)
			{
				prop->currently_damaging_me = TRUE;
				prop->ticks_since_damage = 0;
			}
			actor->target.target_prop_index = prop_index;
			actor->target.target_type = 4; /* _actor_target_acknowledged_enemy */
		}
	}

	if (!actor->meta.swarm && actor->meta.unit_index != NONE)
	{
		struct unit_datum *u = (struct unit_datum *)object_try_and_get_and_verify_type(
			actor->meta.unit_index, _object_mask_unit);
		if (u && u->object.parent_object_index == NONE)
		{
			real_point3d u_pos;
			real_point3d t_pos;
			real dist;

			object_get_origin(actor->meta.unit_index, &u_pos);
			object_get_origin(target_unit_index, &t_pos);
			dist = distance3d(&u_pos, &t_pos);

			if (!actor->control.path.path.valid && dist > 3.5f)
			{
				if (prop_index != NONE)
					actor_move_to_prop(actor_index, prop_index, is_generator_target ? 3.5f : 5.0f);
				else
					actor_move_to_point(actor_index, &t_pos, NONE, NONE);
			}

			if (magnitude_squared3d(&u->object.translational_velocity) < 0.01f && dist > 4.0f)
			{
				real_vector3d dir;
				vector_from_points3d(&u_pos, &t_pos, &dir);
				dir.k = 0.0f;
				if (normalize3d(&dir) > 0.0f)
				{
					unit_port_run(actor->meta.unit_index, &dir);
				}
			}

			/* If targeting a generator and within firing range with clear aim, encourage firing at the generator */
			if (is_generator_target && dist <= 22.0f)
			{
				real_vector3d aim_dir;
				t_pos.z += 0.55f;
				vector_from_points3d(&u_pos, &t_pos, &aim_dir);
				if (normalize3d(&aim_dir) > 0.0f)
				{
					u->unit.desired_aiming_vector = aim_dir;
					u->unit.desired_looking_vector = aim_dir;
					u->unit.desired_facing_vector = aim_dir;
					if (dist <= 16.0f && (game_time_get() % 30) < 18)
					{
						SET_FLAG(u->unit.control_flags, _unit_control_weapon_primary_trigger_bit, TRUE);
						u->unit.primary_trigger = 1.0f;
					}
				}
			}
		}
	}
}

/* whether a tracked enemy's slot is of those GENERATOR ATTACKERS sends at the
generators: every other slot for half, and so on, as slots fill from the first */
static boolean generator_attacker_slot(short slot)
{
	static byte const slot_order[4] = { 0, 2, 1, 3 };
	short quarters;

	switch (global_variant.game_engine_variant.firefight.generator_attackers)
	{
	case _firefight_generator_attackers_none: quarters = 0; break;
	case _firefight_generator_attackers_25:   quarters = 1; break;
	case _firefight_generator_attackers_75:   quarters = 3; break;
	case _firefight_generator_attackers_all:  quarters = 4; break;
	default:                                  quarters = 2; break;
	}
	return slot_order[slot & 3] < quarters;
}

boolean firefight_actor_attacks_generator(long actor_index, long unit_index)
{
	short i;

	if (unit_index == NONE || ff_state.generator_count <= 0)
		return FALSE;
	for (i = 0; i < FIREFIGHT_MAX_TRACKED_ENEMIES; i++)
	{
		if (g_generator_attack_units[i] == unit_index && ff_state.tracked_enemy_units[i] != NONE)
		{
			struct unit_datum *u = (struct unit_datum *)object_try_and_get_and_verify_type(
				ff_state.tracked_enemy_units[i], _object_mask_unit);

			if (u && u->unit.actor_index == actor_index)
				return TRUE;
		}
	}
	return FALSE;
}

static long spawn_single_actor_at(
	long variant_tag_index,
	byte tier,
	real_point3d const *base_pos,
	real facing,
	short forced_team_index,
	boolean is_wave_enemy)
{
	struct actor_variant_definition *variant_def;
	struct actor_definition *actor_def;
	struct object_placement_data placement;
	real_point3d pos;
	long unit_index;
	long actor_index;
	boolean saved_swarm_flag;

	if (!ai_globals || !ai_globals->ai_initialized_for_map || variant_tag_index == NONE)
		return NONE;

	variant_def = actor_variant_definition_get(variant_tag_index);
	if (!variant_def || variant_def->unit_reference.index == NONE || variant_def->actor_reference.index == NONE)
		return NONE;

	actor_def = actor_definition_get(variant_def->actor_reference.index);
	if (!actor_def)
		return NONE;

	patch_actor_for_firefight_pursuit(actor_def, variant_def);
	/* (the profile's AI fight the players, whatever team its unit is of) */
	if (forced_team_index == NONE && is_custom_enemy(variant_tag_index))
		forced_team_index = _game_team_covenant;

	pos = *base_pos;
	pos.x += spawner_rand_range(-1.8f, 1.8f);
	pos.y += spawner_rand_range(-1.8f, 1.8f);
	pos.z += 0.25f;

	object_placement_data_new(&placement, variant_def->unit_reference.index, NONE);
	placement.position = pos;
	vector3d_from_angle(&placement.forward, facing);
	if (forced_team_index != NONE)
		placement.owner_team_index = forced_team_index;

	unit_index = object_new(&placement);
	if (unit_index == NONE)
		return NONE;

	if (unit_get(unit_index)->object.type == _object_type_biped)
	{
		biped_fix_position(unit_index, NONE, &placement.position, NULL, 1.0f, TRUE, FALSE, FALSE);
	}

	actor_customize_unit(variant_tag_index, unit_index);

	/* Always create Firefight wave actors as individual non-swarm units so each has a 1-to-1 unit_index
	   and never references an unallocated swarm_datum (#65535). */
	saved_swarm_flag = TEST_FLAG(actor_def->flags, _actor_definition_swarm_actor_bit);
	SET_FLAG(actor_def->flags, _actor_definition_swarm_actor_bit, FALSE);

	actor_index = actor_create_for_unit(
		FALSE,
		unit_index,
		variant_tag_index,
		NONE,
		NONE,
		FALSE,
		NONE,
		FALSE,
		2,
		0,
		NONE,
		0);

	SET_FLAG(actor_def->flags, _actor_definition_swarm_actor_bit, saved_swarm_flag);

	if (actor_index == NONE)
	{
		object_delete(unit_index);
		return NONE;
	}

	if (forced_team_index != NONE)
	{
		struct actor_datum *actor = actor_get(actor_index);
		actor->meta.team_index = forced_team_index;
		actor_set_team(actor_index, forced_team_index);
	}

	/* Properly activate and wake the actor so object header and actor dormancy stay in sync */
	{
		struct actor_datum *actor = actor_get(actor_index);
		actor->meta.force_active = TRUE;
		actor->meta.dormant_desire = FALSE;
		if (!actor->meta.active)
			actor_set_active(actor_index, TRUE);
		if (actor->meta.active && actor->meta.dormant)
			actor_set_dormant(actor_index, FALSE);
	}
	object_set_automatic_deactivation(unit_index, FALSE);
	actor_verify_activation(actor_index);

	if (is_wave_enemy)
	{
		long player_units[16];
		short player_count = get_living_player_positions(NULL, player_units, 16);
		long target_unit = NONE;

		if (player_count > 0)
			target_unit = player_units[spawner_rand() % (unsigned long)player_count];

		if (target_unit != NONE)
			direct_actor_toward_unit(actor_index, target_unit);

		firefight_track_enemy_unit(unit_index, tier);
	}

	return unit_index;
}

/* ---------- Variant Selection for Waves */

static boolean faction_matches_entry(
	byte wave_faction,
	struct firefight_roster_entry const *e)
{
	switch (wave_faction)
	{
	case _firefight_faction_covenant:
		return e->faction == _firefight_faction_covenant;
	case _firefight_faction_flood:
		return e->faction == _firefight_faction_flood;
	case _firefight_faction_grunts:
		return e->unit_toggle_bit == _ff_unit_covenant_grunts_bit;
	case _firefight_faction_elites_heavies:
		return e->faction == _firefight_faction_covenant &&
			(e->tier == _ff_tier_commander || e->tier == _ff_tier_boss_heavy);
	case _firefight_faction_sentinels:
		return e->faction == _firefight_faction_sentinels;
	case _firefight_faction_covenant_flood:
		return e->faction == _firefight_faction_covenant || e->faction == _firefight_faction_flood;
	case _firefight_faction_covenant_sentinels:
		return e->faction == _firefight_faction_covenant || e->faction == _firefight_faction_sentinels;
	case _firefight_faction_flood_sentinels:
		return e->faction == _firefight_faction_flood || e->faction == _firefight_faction_sentinels;
	case _firefight_faction_unsc_humans:
		return e->faction == _firefight_faction_unsc_humans;
	case _firefight_faction_mixed:
	default:
		if (e->is_human_ally_or_enemy && !ff_state.elite_player_mode)
			return FALSE;
		return TRUE;
	}
}

/* the bonus round is the grunts', whatever the variant's enemies are (a map
with none: its own) */
static byte firefight_wave_faction(byte wave_faction, boolean is_bonus)
{
	short i;

	for (i = 0; is_bonus && i < g_roster_count; i++)
	{
		if (g_roster[i].unit_toggle_bit == _ff_unit_covenant_grunts_bit)
			return _firefight_faction_grunts;
	}
	return wave_faction;
}

/* the side a dropship is on: the wave's team, its faction's in a free for all */
static short firefight_faction_team(byte wave_faction)
{
	switch (wave_faction)
	{
	case _firefight_faction_flood:
	case _firefight_faction_flood_sentinels:
		return _game_team_flood;
	case _firefight_faction_sentinels:
		return _game_team_sentinel;
	case _firefight_faction_unsc_humans:
		return _game_team_human;
	default:
		return _game_team_covenant;
	}
}

static boolean firefight_team_fights_players(short team_index)
{
	return team_index == _game_team_covenant || team_index == _game_team_flood ||
		team_index == _game_team_sentinel ||
		(team_index == _game_team_human &&
			global_variant.game_engine_variant.firefight.wave_faction == _firefight_faction_unsc_humans);
}

/* unit_new gives a unit its tag's team only with no game engine running, so
what isn't placed with a team has none here, an enemy of every team: a
Spirit's gunner, Outbreak's Flood, a carrier's infection forms. They get
their tag's, and with the enemies allied the wave's, as spawn_single_actor_at
gives the units it places */
short firefight_actor_unit_team(short owner_team_index, short default_team_index)
{
	struct game_variant_options const *opts;
	short team_index = owner_team_index;

	if (!firefight_active())
		return owner_team_index;
	if ((team_index == NONE || team_index == _game_team_default) && firefight_team_fights_players(default_team_index))
		team_index = default_team_index;
	opts = game_variant_options_get();
	if (firefight_team_fights_players(team_index) &&
		(!opts || opts->firefight_faction_diplomacy == _firefight_diplomacy_allied_against_players))
	{
		team_index = global_variant.game_engine_variant.firefight.wave_faction == _firefight_faction_unsc_humans ?
			_game_team_human : _game_team_covenant;
	}
	return team_index;
}

static long pick_wave_enemy_variant(
	byte wave_faction,
	boolean is_bonus,
	boolean want_heavy_or_boss,
	byte *out_tier)
{
	short candidates[MAX_ROSTER_VARIANTS];
	short candidate_count = 0;
	short i;
	long custom = pick_custom_enemy(
		is_bonus ? _ff_tier_fodder : want_heavy_or_boss ? _ff_tier_boss_heavy : _ff_tier_infantry, out_tier);

	if (custom != NONE)
		return custom;

	for (i = 0; i < g_roster_count; i++)
	{
		struct firefight_roster_entry const *e = &g_roster[i];

		if (roster_entry_left_out(e) || !faction_matches_entry(wave_faction, e))
			continue;
		if (!firefight_unit_toggle_enabled((enum firefight_unit_toggle_bit)e->unit_toggle_bit))
			continue;

		if (is_bonus)
		{
			if (e->tier != _ff_tier_fodder)
				continue;
		}
		else if (want_heavy_or_boss)
		{
			if (e->tier != _ff_tier_boss_heavy && e->tier != _ff_tier_commander)
				continue;
		}
		else
		{
			if (e->tier == _ff_tier_boss_heavy)
				continue;
		}
		candidates[candidate_count++] = i;
	}

	if (candidate_count == 0)
	{
		for (i = 0; i < g_roster_count; i++)
		{
			struct firefight_roster_entry const *e = &g_roster[i];
			if (roster_entry_left_out(e) || !faction_matches_entry(wave_faction, e))
				continue;
			if (!want_heavy_or_boss && e->tier == _ff_tier_boss_heavy)
				continue;
			candidates[candidate_count++] = i;
		}
	}

	if (candidate_count == 0)
	{
		for (i = 0; i < g_roster_count; i++)
		{
			if (!roster_entry_left_out(&g_roster[i]) &&
				(!g_roster[i].is_human_ally_or_enemy || ff_state.elite_player_mode))
			{
				candidates[candidate_count++] = i;
			}
		}
	}

	/* (ONLY MAP: the map's own enemies, whatever their faction, before the
	game's: a map with Covenant alone still plays a Flood variant) */
	if (candidate_count == 0 && g_roster_map_only)
	{
		long custom;

		for (i = 0; i < g_roster_count; i++)
		{
			if (g_roster[i].maps_own && !g_roster[i].custom_off &&
				(!g_roster[i].is_human_ally_or_enemy || ff_state.elite_player_mode ||
					wave_faction == _firefight_faction_unsc_humans))
			{
				candidates[candidate_count++] = i;
			}
		}
		custom = candidate_count == 0 ? pick_custom_enemy(
			is_bonus ? _ff_tier_fodder : want_heavy_or_boss ? _ff_tier_boss_heavy : _ff_tier_infantry, out_tier) : NONE;
		if (custom != NONE)
			return custom;
	}

	if (candidate_count == 0)
		return NONE;

	{
		short idx = candidates[spawner_rand() % (unsigned long)candidate_count];
		if (out_tier)
			*out_tier = g_roster[idx].tier;
		return g_roster[idx].variant_tag_index;
	}
}

static long pick_variant_for_subtype(
	byte desired_subtype,
	byte wave_faction,
	byte *out_tier,
	boolean *out_force_stealth,
	boolean *out_force_sword)
{
	short candidates[MAX_ROSTER_VARIANTS];
	short candidate_count = 0;
	short i;
	byte target_subtype = desired_subtype;

	if (out_force_stealth)
		*out_force_stealth = FALSE;
	if (out_force_sword)
		*out_force_sword = FALSE;

	if (wave_faction == _firefight_faction_grunts)
		target_subtype = _ff_sub_grunt;

	/* 0. The variant's own AI, of the tier the subtype is */
	{
		long custom = pick_custom_enemy(
			target_subtype == _ff_sub_grunt ? _ff_tier_fodder :
			target_subtype == _ff_sub_elite_major || target_subtype == _ff_sub_elite_stealth ? _ff_tier_commander :
			target_subtype == _ff_sub_elite_zealot || target_subtype == _ff_sub_hunter ? _ff_tier_boss_heavy :
			_ff_tier_infantry,
			out_tier);

		if (custom != NONE)
			return custom;
	}

	/* 1. Exact subtype match among enabled faction units */
	for (i = 0; i < g_roster_count; i++)
	{
		struct firefight_roster_entry const *e = &g_roster[i];

		if (roster_entry_left_out(e) || !faction_matches_entry(wave_faction, e))
			continue;
		if (!firefight_unit_toggle_enabled((enum firefight_unit_toggle_bit)e->unit_toggle_bit))
			continue;
		if (e->subtype == target_subtype)
			candidates[candidate_count++] = i;
	}

	if (candidate_count > 0)
	{
		short idx = candidates[spawner_rand() % (unsigned long)candidate_count];
		if (out_tier)
			*out_tier = g_roster[idx].tier;
		if (target_subtype == _ff_sub_elite_stealth && out_force_stealth)
			*out_force_stealth = TRUE;
		if (target_subtype == _ff_sub_elite_zealot && out_force_sword)
			*out_force_sword = TRUE;
		return g_roster[idx].variant_tag_index;
	}

	/* 2. Covenant Elite fallback when a map only has standard minor/major Elite tags loaded */
	if (target_subtype == _ff_sub_elite_stealth ||
		target_subtype == _ff_sub_elite_zealot ||
		target_subtype == _ff_sub_elite_major ||
		target_subtype == _ff_sub_elite_minor)
	{
		for (i = 0; i < g_roster_count; i++)
		{
			struct firefight_roster_entry const *e = &g_roster[i];
			if (roster_entry_left_out(e) || !faction_matches_entry(wave_faction, e))
				continue;
			if (e->subtype == _ff_sub_elite_major || e->subtype == _ff_sub_elite_minor)
				candidates[candidate_count++] = i;
		}
		if (candidate_count > 0)
		{
			short idx = candidates[spawner_rand() % (unsigned long)candidate_count];
			if (out_tier)
				*out_tier = (target_subtype == _ff_sub_elite_zealot) ? _ff_tier_boss_heavy : _ff_tier_commander;
			if (target_subtype == _ff_sub_elite_stealth && out_force_stealth)
				*out_force_stealth = TRUE;
			if (target_subtype == _ff_sub_elite_zealot && out_force_sword)
				*out_force_sword = TRUE;
			return g_roster[idx].variant_tag_index;
		}
	}

	/* 3. Non-Covenant faction or missing unit tag fallback by matching tier (never upgrading fodder/infantry to boss) */
	{
		byte desired_tier = _ff_tier_infantry;
		if (target_subtype == _ff_sub_grunt)
			desired_tier = _ff_tier_fodder;
		else if (target_subtype == _ff_sub_jackal || target_subtype == _ff_sub_elite_minor)
			desired_tier = _ff_tier_infantry;
		else if (target_subtype == _ff_sub_elite_major || target_subtype == _ff_sub_elite_stealth)
			desired_tier = _ff_tier_commander;
		else if (target_subtype == _ff_sub_elite_zealot || target_subtype == _ff_sub_hunter)
			desired_tier = _ff_tier_boss_heavy;

		for (i = 0; i < g_roster_count; i++)
		{
			struct firefight_roster_entry const *e = &g_roster[i];
			if (roster_entry_left_out(e) || !faction_matches_entry(wave_faction, e))
				continue;
			if (!firefight_unit_toggle_enabled((enum firefight_unit_toggle_bit)e->unit_toggle_bit))
				continue;
			if (e->tier == desired_tier)
				candidates[candidate_count++] = i;
		}
		if (candidate_count > 0)
		{
			short idx = candidates[spawner_rand() % (unsigned long)candidate_count];
			if (out_tier)
				*out_tier = g_roster[idx].tier;
			return g_roster[idx].variant_tag_index;
		}

		return pick_wave_enemy_variant(
			wave_faction,
			target_subtype == _ff_sub_grunt,
			desired_tier == _ff_tier_boss_heavy,
			out_tier);
	}
}

extern boolean unit_can_hold_weapon_definition(long unit_index, long weapon_definition_index);

/* ENEMY LOADOUT: any weapon in any enemy's hands, as the randomizer's crazy
weapons (randomizer_borrowed_weapon_label) */
boolean firefight_enemy_weapons_borrowed(void)
{
	struct game_variant_options const *opts;

	if (!firefight_active())
		return FALSE;
	opts = game_variant_options_get();
	return opts && opts->firefight_enemy_loadout != _firefight_enemy_loadout_default;
}

/* the animations of the weapon a unit's actor carries, for a weapon the
labels nearest it don't fit */
char const *firefight_unit_own_weapon_label(long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	short i;

	for (i = 0; unit && i < g_roster_count; i++)
	{
		struct actor_variant_definition *variant = actor_variant_definition_get(g_roster[i].variant_tag_index);

		if (variant->unit_reference.index == unit->definition_index && variant->ranged_combat.reference.index != NONE)
			return weapon_definition_get(variant->ranged_combat.reference.index)->weapon.label;
	}
	return NULL;
}

/* a wave enemy armed by ENEMY LOADOUT, with what it can hold of the choice
(Flood infection forms, carriers and the like hold nothing, and keep what
they have). Humans' first, then the Covenant's */
static void firefight_arm_wave_enemy(long unit_index)
{
	static char const *const weapon_paths[] =
	{
		"weapons\\assault rifle\\assault rifle",
		"weapons\\pistol\\pistol",
		"weapons\\shotgun\\shotgun",
		"weapons\\sniper rifle\\sniper rifle",
		"weapons\\rocket launcher\\rocket launcher",
		"weapons\\flamethrower\\flamethrower",
		"weapons\\plasma pistol\\plasma pistol",
		"weapons\\plasma rifle\\plasma rifle",
		"weapons\\needler\\mp_needler",
		"weapons\\needler\\needler",
		"weapons\\plasma_cannon\\plasma_cannon",
		"weapons\\fuel rod gun\\fuel rod",
	};
	enum { HUMAN_WEAPONS = 6 };
	struct game_variant_options const *opts = game_variant_options_get();
	long held[NUMBEROF(weapon_paths) + MAX_ROSTER_VARIANTS];
	short first = 0;
	short last = (short)NUMBEROF(weapon_paths);
	short count = 0;
	short i;
	struct object_placement_data placement;
	long weapon_index;

	if (!firefight_enemy_weapons_borrowed())
		return;
	if (opts->firefight_enemy_loadout == _firefight_enemy_loadout_human)
		last = HUMAN_WEAPONS;
	else if (opts->firefight_enemy_loadout == _firefight_enemy_loadout_covenant)
		first = HUMAN_WEAPONS;
	/* (MAP CONTENT: the map's own weapons, beside the game's or alone; ONLY
	MAP's the game's if the unit can hold none of the map's) */
	if (map_content_of(FIREFIGHT_MAP_CONTENT_WEAPONS_OFF) != _firefight_map_content_vanilla)
	{
		boolean custom_only = !map_weapons_only();

		for (i = 0; i < ff_map_weapon_count; i++)
		{
			if ((!custom_only || !ff_map_weapon_stock[i]) &&
				unit_can_hold_weapon_definition(unit_index, ff_map_weapon_tags[i]))
			{
				held[count++] = ff_map_weapon_tags[i];
			}
		}
	}
	if (count && map_weapons_only())
		last = first;
	for (i = first; i < last; i++)
	{
		long definition = tag_loaded(WEAPON_DEFINITION_TAG, weapon_paths[i]);

		if (definition != NONE && unit_can_hold_weapon_definition(unit_index, definition))
			held[count++] = definition;
	}
	if (!count)
		return;
	object_placement_data_new(&placement, held[spawner_rand() % (unsigned long)count], NONE);
	object_get_origin(unit_index, &placement.position);
	weapon_index = object_new(&placement);
	if (weapon_index == NONE)
		return;
	unit_delete_all_weapons(unit_index);
	if (!unit_add_weapon_to_inventory(unit_index, weapon_index, _unit_add_weapon_replace))
		object_delete(weapon_index);
}

static long spawn_subtype_actor_at(
	byte subtype,
	byte wave_faction,
	real_point3d const *base_pos,
	real facing,
	short forced_team_index,
	boolean is_wave_enemy)
{
	byte tier = _ff_tier_infantry;
	boolean force_stealth = FALSE;
	boolean force_sword = FALSE;
	long variant = pick_variant_for_subtype(
		subtype,
		wave_faction,
		&tier,
		&force_stealth,
		&force_sword);
	long unit_index;

	if (variant == NONE)
		return NONE;

	unit_index = spawn_single_actor_at(
		variant,
		tier,
		base_pos,
		facing,
		forced_team_index,
		is_wave_enemy);
	if (unit_index == NONE)
		return NONE;

	if (force_stealth)
	{
		struct unit_datum *u = unit_try_and_get(unit_index);
		if (u)
		{
			SET_FLAG(u->unit.flags, _unit_active_camouflaged_bit, TRUE);
			u->unit.active_camouflage = 1.0f;
		}
	}

	if (firefight_enemy_weapons_borrowed())
	{
		firefight_arm_wave_enemy(unit_index);
	}
	else if (force_sword)
	{
		long sword_def = tag_loaded(WEAPON_DEFINITION_TAG, "weapons\\energy sword\\energy sword");
		if (sword_def != NONE && !unit_has_weapon_definition_index(unit_index, sword_def))
		{
			struct object_placement_data w_place;
			long sword_obj;

			object_placement_data_new(&w_place, sword_def, NONE);
			object_get_origin(unit_index, &w_place.position);
			sword_obj = object_new(&w_place);
			if (sword_obj != NONE)
			{
				if (unit_can_use_weapon(unit_index, sword_obj))
				{
					unit_delete_all_weapons(unit_index);
					if (!unit_add_weapon_to_inventory(unit_index, sword_obj, _unit_add_weapon_starting))
						object_delete(sword_obj);
				}
				else
				{
					object_delete(sword_obj);
				}
			}
		}
	}

	return unit_index;
}

/* ---------- Animated Dropship Insertion & Boss Vehicles (Features 3 & 7) */

enum
{
	_ff_unit_seat_ai_enterable_bit = 1,
	_ff_vehicle_ai_driver_enable_bit = 11,
	_ff_vehicle_ai_driver_flying_bit = 12,
	_ff_vehicle_ai_driver_nondirectional_bit = 13,
	_ff_vehicle_ai_driver_hovering_bit = 14
};

struct ff_vehicle_ai_tag_layout
{
	struct unit_definition unit;
	unsigned long flags;
	short type;
	short pad;
	real maximum_forward_speed;
	real maximum_reverse_speed;
	real speed_acceleration;
	real speed_deceleration;
	byte __unknown308[0x78];
	real ai_sideslip_distance;
	real ai_destination_radius;
	real ai_avoidance_distance;
	real ai_pathfinding_radius;
	real ai_charge_repeat_time;
	real ai_strafing_stop_range;
	real ai_oversteer_angle_lower_bound;
	real ai_oversteer_angle_upper_bound;
	real ai_steering_max_angle;
	real ai_steering_max_throttle;
};

/* (vehicles.c's vehicle types) */
enum
{
	FF_VEHICLE_TYPE_HUMAN_PLANE = 3,
	FF_VEHICLE_TYPE_ALIEN_SCOUT = 4,
	FF_VEHICLE_TYPE_ALIEN_FIGHTER = 5,
	FF_VEHICLE_TYPE_TURRET = 6
};

typedef char ff_vehicle_ai_type_offset_assert[
	offsetof(struct ff_vehicle_ai_tag_layout, type) == 0x2F4 ? 1 : -1];
typedef char ff_vehicle_ai_flags_offset_assert[
	offsetof(struct ff_vehicle_ai_tag_layout, flags) == 0x2F0 ? 1 : -1];
typedef char ff_vehicle_ai_max_speed_offset_assert[
	offsetof(struct ff_vehicle_ai_tag_layout, maximum_forward_speed) == 0x2F8 ? 1 : -1];
typedef char ff_vehicle_ai_sideslip_offset_assert[
	offsetof(struct ff_vehicle_ai_tag_layout, ai_sideslip_distance) == 0x380 ? 1 : -1];
typedef char ff_vehicle_ai_dest_radius_offset_assert[
	offsetof(struct ff_vehicle_ai_tag_layout, ai_destination_radius) == 0x384 ? 1 : -1];
typedef char ff_vehicle_ai_charge_offset_assert[
	offsetof(struct ff_vehicle_ai_tag_layout, ai_charge_repeat_time) == 0x390 ? 1 : -1];
typedef char ff_vehicle_ai_max_throttle_offset_assert[
	offsetof(struct ff_vehicle_ai_tag_layout, ai_steering_max_throttle) == 0x3A4 ? 1 : -1];

/* ---------- Wrecks

A dead vehicle is garbage, as a body is, and Firefight's bodies keep the game
collecting it: a wreck would go the moment no one looked at it. So a wreck is
taken off the garbage and kept here, the oldest going when there are too many. */

enum
{
	FF_MAX_WRECKS = 12,
	FF_MAX_ENEMY_VEHICLES = 32
};

static long g_ff_wrecks[FF_MAX_WRECKS];
static short g_ff_next_wreck;
/* (the vehicles the waves brought, which leave a wreck; an empty one of the
map's is only cleared away) */
static long g_ff_enemy_vehicles[FF_MAX_ENEMY_VEHICLES];
static short g_ff_next_enemy_vehicle;

void firefight_wrecks_reset(void)
{
	short i;

	for (i = 0; i < FF_MAX_WRECKS; i++)
		g_ff_wrecks[i] = NONE;
	for (i = 0; i < FF_MAX_ENEMY_VEHICLES; i++)
		g_ff_enemy_vehicles[i] = NONE;
	g_ff_next_wreck = 0;
	g_ff_next_enemy_vehicle = 0;
}

static void enemy_vehicle_note(long vehicle_index)
{
	g_ff_enemy_vehicles[g_ff_next_enemy_vehicle] = vehicle_index;
	g_ff_next_enemy_vehicle = (short)((g_ff_next_enemy_vehicle + 1) % FF_MAX_ENEMY_VEHICLES);
}

static boolean enemy_vehicle_noted(long vehicle_index)
{
	short i;

	for (i = 0; i < FF_MAX_ENEMY_VEHICLES; i++)
	{
		if (g_ff_enemy_vehicles[i] == vehicle_index)
			return TRUE;
	}
	return FALSE;
}

static void wreck_keep(long vehicle_index)
{
	long oldest = g_ff_wrecks[g_ff_next_wreck];

	if (oldest != NONE && oldest != vehicle_index &&
		object_try_and_get_and_verify_type(oldest, _object_mask_vehicle))
	{
		object_delete(oldest);
	}
	g_ff_wrecks[g_ff_next_wreck] = vehicle_index;
	g_ff_next_wreck = (short)((g_ff_next_wreck + 1) % FF_MAX_WRECKS);
	object_set_garbage(vehicle_index, FALSE);
	SET_FLAG(object_get(vehicle_index)->object.flags, _object_cannot_be_garbage_bit, TRUE);
}

/* whether the vehicle's model has a wrecked look (the Scorpion and Warthog don't) */
static boolean vehicle_has_wreck_model(long vehicle_index)
{
	struct object_definition *definition = object_definition_get(object_get(vehicle_index)->definition_index);
	struct model *model;
	short region_index;

	if (definition->object.model.index == NONE)
		return FALSE;
	model = model_definition_get(definition->object.model.index);
	for (region_index = 0; region_index < model->regions.count; region_index++)
	{
		struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
		short permutation_index;

		for (permutation_index = 0; permutation_index < region->permutations.count; permutation_index++)
		{
			struct model_region_permutation *permutation = TAG_BLOCK_GET_ELEMENT(
				&region->permutations, permutation_index, struct model_region_permutation);

			if (!_stricmp(permutation->name, "~damaged"))
				return TRUE;
		}
	}
	return FALSE;
}

/* a vehicle whose driver is gone goes up. Not by damage: an empty Ghost or
Banshee takes none, and an _mp one has no body to lose */
static void vehicle_wreck(long vehicle_index)
{
	object_permute_region(vehicle_index, "~damaged", NONE, TRUE);
	object_deplete_body(vehicle_index);
	unit_died(vehicle_index, FALSE);
}

/* the _mp Ghost and Banshee have no body to lose, so one that's the enemy's
would never leave a wreck: the campaign's goes first where it's loaded */
static boolean enemy_vehicle_tag_superseded(char const *name)
{
	return (!_stricmp(name, "vehicles\\ghost\\ghost_mp") &&
			tag_loaded(VEHICLE_DEFINITION_TAG, "vehicles\\ghost\\ghost") != NONE) ||
		(!_stricmp(name, "vehicles\\banshee\\banshee_mp") &&
			tag_loaded(VEHICLE_DEFINITION_TAG, "vehicles\\banshee\\banshee") != NONE);
}

static long find_loaded_vehicle_tag(char const *const *names, short count)
{
	short i;

	for (i = 0; i < count; i++)
	{
		long idx = tag_loaded(VEHICLE_DEFINITION_TAG, names[i]);
		if (idx != NONE)
			return idx;
	}
	return NONE;
}

static void project_point_to_ground(real_point3d *pt, real hover_offset)
{
	struct collision_result coll;
	real_point3d ray_start;
	real_vector3d ray_down = { 0.0f, 0.0f, -18.0f };

	if (!pt)
		return;
	ray_start = *pt;
	ray_start.z += 2.0f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit), &ray_start, &ray_down, NONE, &coll))
	{
		pt->x = coll.point.x;
		pt->y = coll.point.y;
		pt->z = coll.point.z + hover_offset;
	}
	else
	{
		pt->z += hover_offset;
	}
}

static void patch_vehicle_for_ai_driver(long vehicle_tag_index, boolean is_banshee, boolean is_hover)
{
	struct ff_vehicle_ai_tag_layout *vdef;

	if (vehicle_tag_index == NONE)
		return;
	vdef = (struct ff_vehicle_ai_tag_layout *)tag_get(VEHICLE_DEFINITION_TAG, vehicle_tag_index);
	if (!vdef)
		return;

	SET_FLAG(vdef->flags, _ff_vehicle_ai_driver_enable_bit, TRUE);
	if (is_banshee)
	{
		SET_FLAG(vdef->flags, _ff_vehicle_ai_driver_flying_bit, TRUE);
	}
	else
	{
		SET_FLAG(vdef->flags, _ff_vehicle_ai_driver_nondirectional_bit, TRUE);
		if (is_hover)
			SET_FLAG(vdef->flags, _ff_vehicle_ai_driver_hovering_bit, TRUE);
	}

	if (vdef->ai_sideslip_distance <= 0.0f)
		vdef->ai_sideslip_distance = is_banshee ? 4.0f : 2.5f;
	if (vdef->ai_destination_radius <= 0.0f)
		vdef->ai_destination_radius = is_banshee ? 3.5f : 2.0f;
	if (vdef->ai_avoidance_distance <= 0.0f)
		vdef->ai_avoidance_distance = is_banshee ? 8.0f : 4.0f;
	if (vdef->ai_pathfinding_radius <= 0.0f)
		vdef->ai_pathfinding_radius = 1.5f;
	if (vdef->ai_charge_repeat_time <= 0.0f)
		vdef->ai_charge_repeat_time = 2.5f;
	if (vdef->ai_strafing_stop_range <= 0.0f)
		vdef->ai_strafing_stop_range = is_banshee ? 6.0f : 4.5f;
	if (vdef->ai_steering_max_angle <= 0.0f)
		vdef->ai_steering_max_angle = 0.7853982f;
	if (vdef->ai_steering_max_throttle <= 0.0f)
		vdef->ai_steering_max_throttle = 1.0f;

	if (vdef->unit.unit.seats.count > 0)
	{
		struct unit_seat *seat0 = TAG_BLOCK_GET_ELEMENT(&vdef->unit.unit.seats, 0, struct unit_seat);
		if (seat0)
		{
			SET_FLAG(seat0->flags, _ff_unit_seat_ai_enterable_bit, TRUE);
			SET_FLAG(seat0->flags, _unit_seat_driver_bit, TRUE);
			SET_FLAG(seat0->flags, _unit_seat_gunner_bit, TRUE);
			/* A wraith's driver rides out of sight inside the hull, as its tag has it:
			 * Forge shows every vehicle's driver, and this one has no pose that fits */
			if (tag_get_name(vehicle_tag_index) && strstr(tag_get_name(vehicle_tag_index), "wraith") != NULL)
				SET_FLAG(seat0->flags, _unit_seat_invisible_bit, TRUE);
		}
	}
}

/* a driver with the animations for seat 0, the tag's label changed if not: and
never left on "stand" from a driver who had nothing better, as the tag is
every vehicle of its kind's */
static void vehicle_seat_fit_driver(long driver_unit, long vehicle_index, struct unit_seat *seat)
{
	static char const *const fallback_seat_labels[] = {
		"W-driver", "G-driver", "B-driver", "S-driver", "stand"
	};
	short lbl_idx;

	if (unit_has_animation_to_enter_seat(driver_unit, vehicle_index, 0) && _stricmp(seat->label, "stand") != 0)
		return;
	for (lbl_idx = 0; lbl_idx < (short)NUMBEROF(fallback_seat_labels); lbl_idx++)
	{
		strncpy(seat->label, fallback_seat_labels[lbl_idx], TAG_STRING_LENGTH);
		seat->label[TAG_STRING_LENGTH] = '\0';
		if (unit_has_animation_to_enter_seat(driver_unit, vehicle_index, 0))
			break;
	}
}

/* seats a vehicle's driver. Entering is cut short here, and it's the animation's
end that hides a driver in an invisible seat, so that's done here too */
static void vehicle_seat_driver(long driver_unit, long vehicle_index, struct unit_seat *seat)
{
	struct unit_datum *v_unit = unit_get(vehicle_index);

	if (unit_has_animation_to_enter_seat(driver_unit, vehicle_index, 0) &&
		unit_enter_seat(driver_unit, vehicle_index, 0))
	{
		unit_abort_animation(driver_unit);
		unit_close(vehicle_index);
		v_unit->unit.driver_object_index = driver_unit;
		v_unit->unit.gunner_object_index = driver_unit;
		v_unit->unit.player_index = NONE;
		SET_FLAG(v_unit->unit.flags, _unit_actively_controlled_bit, TRUE);
		if (TEST_FLAG(seat->flags, _unit_seat_invisible_bit))
			object_set_visibility(driver_unit, FALSE);
	}
}

/* how well an Elite variant suits a driver's seat: 0 for one to keep on foot.
Sword carriers have nothing to do in a vehicle, and commanders are the wave's
gold leaders */
static short elite_driver_rank(char const *name)
{
	if (strstr(name, "elite") == NULL || strstr(name, "sword") != NULL || strstr(name, "stealth") != NULL ||
		strstr(name, "special") != NULL || strstr(name, "specops") != NULL)
	{
		return 0;
	}
	if (strstr(name, "major") != NULL)
		return 3;
	return strstr(name, "minor") != NULL ? 2 : 1;
}

static long pick_vehicle_driver_variant(long fallback_variant, byte wave_faction)
{
	short i;
	short best_rank = 0;
	long best_elite = NONE;
	long marine_candidate = NONE;

	/* Prefer an Elite actor variant because Elites have W-driver, B-driver, and G-driver seat animations */
	for (i = 0; i < g_roster_count; i++)
	{
		long v_idx = g_roster[i].variant_tag_index;
		char const *name = v_idx != NONE ? tag_get_name(v_idx) : NULL;
		short rank;
		if (!name)
			continue;
		rank = elite_driver_rank(name);
		if (rank > best_rank)
		{
			best_rank = rank;
			best_elite = v_idx;
		}
		if (strstr(name, "marine") != NULL || strstr(name, "odst") != NULL)
			marine_candidate = v_idx;
	}

	if (wave_faction != _firefight_faction_unsc_humans && best_elite != NONE)
		return best_elite;

	if (wave_faction == _firefight_faction_unsc_humans && marine_candidate != NONE)
		return marine_candidate;

	for (i = 0; i < g_roster_count; i++)
	{
		long v_idx = g_roster[i].variant_tag_index;
		char const *name = v_idx != NONE ? tag_get_name(v_idx) : NULL;
		if (name && strstr(name, "elite") != NULL)
			return v_idx;
	}

	if (marine_candidate != NONE)
		return marine_candidate;

	return fallback_variant;
}

/* (the flight in progress, set up by start_dropship_insertion) */
static struct ff_route_flight g_dropship_flight;

/* a flight to the zone at this spot: on its route if Forge gave it one, which
this says, or else the automatic path */
static boolean dropship_flight_prepare(struct ff_route_flight *flight, real_point3d const *zone, real facing)
{
	struct scenario *scen = global_scenario_try_and_get();
	real best_distance = 2.0f;
	int route_id = 0;
	boolean has_route = FALSE;
	short i;

	csmemset(flight, 0, sizeof(*flight));

	for (i = 0; scen && i < scen->netgame_flags.count; i++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
			&scen->netgame_flags, i, struct scenario_netgame_flag);

		if (flag && flag->type == FF_NETGAME_FLAG_FIREFIGHT_SPAWN &&
			FF_UNPACK_SPAWN_TYPE(flag->team_index) == FF_SPAWN_TYPE_DROPSHIP_ZONE &&
			FF_ROUTE_ID(flag->team_index) != 0)
		{
			real distance = distance3d(&flag->position, zone);

			if (distance < best_distance)
			{
				best_distance = distance;
				route_id = FF_ROUTE_ID(flag->team_index);
			}
		}
	}

	for (i = 0; scen && route_id != 0 && i < scen->netgame_flags.count; i++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
			&scen->netgame_flags, i, struct scenario_netgame_flag);

		if (flag && flag->type == FF_NETGAME_FLAG_FIREFIGHT_SPAWN &&
			FF_UNPACK_SPAWN_TYPE(flag->team_index) == FF_ROUTE_SPAWN_TYPE &&
			FF_ROUTE_ID(flag->team_index) == route_id)
		{
			int leg = FF_ROUTE_POINT_LEG(flag->team_index);

			ff_route_leg_add(
				&flight->legs[leg],
				FF_ROUTE_POINT_ORDER(flag->team_index),
				flag->position.x,
				flag->position.y,
				flag->position.z);
			flight->styles[leg] = FF_ROUTE_POINT_STYLE(flag->team_index);
			has_route = TRUE;
		}
	}

	if (!has_route)
		ff_route_flight_automatic(flight, zone->x, zone->y, zone->z);
	ff_route_flight_finish(flight, zone->x, zone->y, zone->z, facing);
	return has_route;
}

/* a ship for a flight, facing along it and out of everything's way */
static long dropship_new(long dropship_tag, struct ff_route_flight const *flight, short team)
{
	struct ff_route_pose pose = ff_route_flight_pose(flight, 0.0f);
	struct object_placement_data placement;
	struct object_datum *d_obj;
	long dropship_index;

	object_placement_data_new(&placement, dropship_tag, NONE);
	placement.position.x = pose.position.x;
	placement.position.y = pose.position.y;
	placement.position.z = pose.position.z;
	placement.forward.i = cosine(pose.heading);
	placement.forward.j = sine(pose.heading);
	placement.forward.k = 0.0f;
	normalize3d(&placement.forward);
	placement.owner_team_index = team;
	dropship_index = object_new(&placement);
	if (dropship_index == NONE)
		return NONE;

	d_obj = object_try_and_get_and_verify_type(dropship_index, _object_mask_vehicle);
	vehicle_hover(dropship_index, TRUE);
	object_set_automatic_deactivation(dropship_index, FALSE);
	if (d_obj)
	{
		SET_FLAG(d_obj->object.flags, _object_no_collisions_bit, TRUE);
		SET_FLAG(d_obj->object.flags, _object_at_rest_bit, TRUE);
		SET_FLAG(d_obj->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
	}
	return dropship_index;
}

/* a ship's gunner (seated by the AI after the ship is placed) is on the ship's
side, so that it doesn't shoot the squad it brought */
static void dropship_gunner_follow_ship(struct unit_datum *ship)
{
	long child_index = ship->object.first_child_object_index;
	short safety = 0;

	while (child_index != NONE && safety++ < 16)
	{
		struct object_datum *child = object_try_and_get(child_index);

		if (!child)
			break;
		if (TEST_FLAG(_object_mask_unit, child->object.type) &&
			child->object.owner_team_index != ship->object.owner_team_index)
		{
			long actor_index = ((struct unit_datum *)child)->unit.actor_index;

			child->object.owner_team_index = ship->object.owner_team_index;
			if (actor_index != NONE)
			{
				actor_get(actor_index)->meta.team_index = ship->object.owner_team_index;
				actor_set_team(actor_index, ship->object.owner_team_index);
			}
		}
		child_index = child->object.next_object_index;
	}
}

/* puts a ship where its flight has it on this tick; FALSE when it's gone */
static boolean dropship_fly(long vehicle_index, struct ff_route_pose const *pose)
{
	struct unit_datum *ship = vehicle_index != NONE ?
		(struct unit_datum *)object_try_and_get_and_verify_type(vehicle_index, _object_mask_vehicle) : NULL;
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;

	if (!ship)
		return FALSE;

	position.x = pose->position.x;
	position.y = pose->position.y;
	position.z = pose->position.z;
	forward.i = cosine(pose->heading);
	forward.j = sine(pose->heading);
	forward.k = pose->pitch;
	up.i = -forward.i * pose->pitch;
	up.j = -forward.j * pose->pitch;
	up.k = 1.0f;
	normalize3d(&forward);
	normalize3d(&up);

	/* (set before the move: that's what works out its nodes at this size. A
	camouflage left alone wears off a little each tick, so it's held here) */
	ship->object.scale = pose->scale;
	ship->unit.active_camouflage = pose->cloak;
	object_set_position(vehicle_index, &position, &forward, &up);
	vehicle_hover(vehicle_index, TRUE);
	SET_FLAG(ship->object.flags, _object_no_collisions_bit, TRUE);
	SET_FLAG(ship->object.flags, _object_at_rest_bit, TRUE);
	dropship_gunner_follow_ship(ship);
	return TRUE;
}

/* a slipspace end's flash, as a flight starts and just before it finishes */
static void dropship_flash(
	struct ff_route_flight const *flight,
	real previous_tick,
	real tick,
	struct ff_route_pose const *pose,
	byte wave_faction,
	boolean is_boss_wave)
{
	real flash_tick = (real)(ff_route_flight_total_ticks(flight) - FF_ROUTE_FLASH_TICKS_BEFORE_END);
	boolean starting = previous_tick < 0.0f && flight->styles[FF_ROUTE_APPROACH] == FF_ROUTE_STYLE_SLIPSPACE;
	boolean ending = previous_tick < flash_tick && tick >= flash_tick &&
		flight->styles[FF_ROUTE_DEPARTURE] == FF_ROUTE_STYLE_SLIPSPACE;

	if (starting || ending)
	{
		real_point3d position;

		position.x = pose->position.x;
		position.y = pose->position.y;
		position.z = pose->position.z;
		spawn_insertion_beacon_effect(&position, NONE, wave_faction, is_boss_wave);
	}
}

/* ---------- A flight tested in Forge: the same flight, with no one aboard */

static struct
{
	long vehicle_index;
	struct ff_route_flight flight;
	real tick;
} g_dropship_preview = { NONE };

void firefight_dropship_preview_stop(void)
{
	if (g_dropship_preview.vehicle_index != NONE &&
		object_try_and_get_and_verify_type(g_dropship_preview.vehicle_index, _object_mask_vehicle))
	{
		object_delete(g_dropship_preview.vehicle_index);
	}
	g_dropship_preview.vehicle_index = NONE;
}

boolean firefight_dropship_preview_start(real_point3d const *zone, real facing, long dropship_tag)
{
	struct ff_route_pose pose;

	firefight_dropship_preview_stop();
	dropship_flight_prepare(&g_dropship_preview.flight, zone, facing);
	g_dropship_preview.vehicle_index = dropship_new(dropship_tag, &g_dropship_preview.flight, _game_team_covenant);
	if (g_dropship_preview.vehicle_index == NONE)
		return FALSE;

	g_dropship_preview.tick = 0.0f;
	pose = ff_route_flight_pose(&g_dropship_preview.flight, 0.0f);
	dropship_fly(g_dropship_preview.vehicle_index, &pose);
	dropship_flash(&g_dropship_preview.flight, -1.0f, 0.0f, &pose, _firefight_faction_covenant, FALSE);
	return TRUE;
}

/* a frame of the test flight: its phase, or 0 once there's none in the air */
short firefight_dropship_preview_update(real seconds)
{
	struct ff_route_pose pose;
	real previous_tick = g_dropship_preview.tick;

	if (g_dropship_preview.vehicle_index == NONE)
		return 0;

	g_dropship_preview.tick += seconds * (real)FF_ROUTE_TICKS_PER_SECOND;
	if (g_dropship_preview.tick >= (real)ff_route_flight_total_ticks(&g_dropship_preview.flight))
	{
		firefight_dropship_preview_stop();
		return 0;
	}

	pose = ff_route_flight_pose(&g_dropship_preview.flight, g_dropship_preview.tick);
	if (!dropship_fly(g_dropship_preview.vehicle_index, &pose))
	{
		g_dropship_preview.vehicle_index = NONE;
		return 0;
	}
	dropship_flash(
		&g_dropship_preview.flight, previous_tick, g_dropship_preview.tick, &pose, _firefight_faction_covenant, FALSE);
	return (short)pose.phase;
}

/* a client's ship: what the host's state says of how it looks */
void firefight_dropship_apply_look(short absolute_index, real scale, real cloak)
{
	struct object_iterator iterator;
	struct object_datum *object;

	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index) == absolute_index)
		{
			object->object.scale = scale;
			((struct unit_datum *)object)->unit.active_camouflage = cloak;
			/* (a client's HUD marks the ship with these) */
			ff_state.active_dropship.vehicle_index = iterator.index;
			ff_state.active_dropship.phase = DROPSHIP_PHASE_DESCENDING;
			return;
		}
	}
}

static boolean start_dropship_insertion(
	boolean is_pelican,
	real_point3d const *landing_zone,
	real facing,
	byte wave_faction,
	boolean is_boss_wave,
	short squad_to_spawn,
	short forced_team)
{
	static char const *const spirit_tags[] = { "vehicles\\c_dropship\\c_dropship", "vehicles\\pelican\\pelican" };
	static char const *const pelican_tags[] = { "vehicles\\pelican\\pelican", "vehicles\\c_dropship\\c_dropship" };
	long dropship_tag = NONE;
	long dropship_index = NONE;
	struct collision_result coll;
	real_point3d ray_start;
	real_vector3d ray_up = { 0.0f, 0.0f, 10.5f };
	boolean has_sky_clearance = TRUE;
	real approach_angle;
	struct ff_route_pose first_pose;
	boolean has_route;

	if (!landing_zone)
		return FALSE;

	/* Delete any previous dropship before starting a new insertion */
	if (ff_state.active_dropship.vehicle_index != NONE)
	{
		object_delete(ff_state.active_dropship.vehicle_index);
		ff_state.active_dropship.vehicle_index = NONE;
	}
	ff_state.active_dropship.phase = DROPSHIP_PHASE_NONE;
	ff_state.active_dropship.squad_to_spawn = 0;
	ff_state.active_dropship.payload_deployed = TRUE;

	/* Trigger Covenant Teleport / Orbital Insertion Beacon flash + sound at the zone */
	spawn_insertion_beacon_effect(landing_zone, NONE, wave_faction, is_boss_wave);

	/* Require >= 10.0f world units of vertical ceiling clearance above the drop zone via upward raycast
	 * (a placed route was laid out around whatever is overhead, so it is taken as flyable) */
	has_route = dropship_flight_prepare(&g_dropship_flight, landing_zone, facing);
	ray_start = *landing_zone;
	ray_start.z += 0.5f;
	if (!has_route && collision_test_vector(FLAG(_collision_test_structure_bit), &ray_start, &ray_up, NONE, &coll))
	{
		if (coll.t * 10.5f < 10.0f)
			has_sky_clearance = FALSE;
	}

	if (!has_sky_clearance)
		return FALSE;

	dropship_tag = is_pelican ?
		find_loaded_vehicle_tag(pelican_tags, 2) :
		find_loaded_vehicle_tag(spirit_tags, 2);
	if (dropship_tag == NONE)
		return FALSE;

	/* Approach angle from drop_zone out to start_pos; the dropship faces inward toward drop_zone */
	approach_angle = facing + _pi;

	dropship_index = dropship_new(
		dropship_tag, &g_dropship_flight, forced_team != NONE ? forced_team : firefight_faction_team(wave_faction));
	if (dropship_index == NONE)
		return FALSE;

	ff_state.active_dropship.vehicle_index = dropship_index;
	ff_state.active_dropship.drop_zone = *landing_zone;
	ff_state.active_dropship.hover_position.x = landing_zone->x;
	ff_state.active_dropship.hover_position.y = landing_zone->y;
	ff_state.active_dropship.hover_position.z = landing_zone->z + FF_ROUTE_HOVER_HEIGHT;
	ff_state.active_dropship.approach_angle = approach_angle;
	ff_state.active_dropship.phase = DROPSHIP_PHASE_DESCENDING;
	ff_state.active_dropship.elapsed_ticks = 0;
	ff_state.active_dropship.timer_ticks = (short)ff_route_flight_total_ticks(&g_dropship_flight);
	ff_state.active_dropship.squad_to_spawn = squad_to_spawn;
	ff_state.active_dropship.forced_team = forced_team;
	ff_state.active_dropship.wave_faction = wave_faction;
	ff_state.active_dropship.is_boss_wave = is_boss_wave;
	ff_state.active_dropship.is_pelican = is_pelican;
	ff_state.active_dropship.payload_deployed = FALSE;

	first_pose = ff_route_flight_pose(&g_dropship_flight, 0.0f);
	dropship_fly(dropship_index, &first_pose);
	dropship_flash(&g_dropship_flight, -1.0f, 0.0f, &first_pose, wave_faction, is_boss_wave);

	firefight_announce_banner(_ff_banner_dropship_inbound, 0, 3 * TICKS_PER_SECOND);
	return TRUE;
}

static long spawn_boss_vehicle_at(
	real_point3d const *zone,
	real facing,
	long driver_variant,
	short enemy_team,
	boolean under_dropship)
{
	static char const *const dropship_vehicle_tags[] = {
		"vehicles\\wraith\\wraith",
		"vehicles\\ghost\\ghost_mp",
		"vehicles\\ghost\\ghost"
	};
	static char const *const boss_vehicle_tags[] = {
		"vehicles\\wraith\\wraith",
		"vehicles\\banshee\\banshee",
		"vehicles\\banshee\\banshee_mp",
		"vehicles\\ghost\\ghost_mp",
		"vehicles\\ghost\\ghost",
		"vehicles\\scorpion\\scorpion_mp",
		"vehicles\\scorpion\\scorpion"
	};
	long candidates[8];
	short candidate_count = 0;
	long vehicle_tag;
	char const *v_name;
	boolean is_banshee = FALSE;
	boolean is_hover = FALSE;
	struct object_placement_data placement;
	long vehicle_index;
	long chosen_driver_variant;
	long driver_unit;
	short i;

	if (under_dropship)
	{
		for (i = 0; i < (short)NUMBEROF(dropship_vehicle_tags); i++)
		{
			long idx = tag_loaded(VEHICLE_DEFINITION_TAG, dropship_vehicle_tags[i]);
			if (idx != NONE && !enemy_vehicle_tag_superseded(dropship_vehicle_tags[i]))
				candidates[candidate_count++] = idx;
		}
	}
	if (candidate_count == 0)
	{
		for (i = 0; i < (short)NUMBEROF(boss_vehicle_tags); i++)
		{
			long idx = tag_loaded(VEHICLE_DEFINITION_TAG, boss_vehicle_tags[i]);
			if (idx != NONE && !enemy_vehicle_tag_superseded(boss_vehicle_tags[i]))
				candidates[candidate_count++] = idx;
		}
	}
	/* (MAP CONTENT: the map's own vehicles beside the game's, or alone) */
	if (map_content_of(FIREFIGHT_MAP_CONTENT_VEHICLES_OFF) != _firefight_map_content_vanilla && ff_map_vehicle_count > 0)
	{
		boolean only = map_vehicles_only();
		long map_vehicle = pick_map_vehicle(!under_dropship, !only);

		if (only)
			candidate_count = 0;
		if (map_vehicle != NONE && candidate_count < (short)NUMBEROF(candidates))
			candidates[candidate_count++] = map_vehicle;
	}
	if (candidate_count == 0)
		return NONE;

	vehicle_tag = candidates[spawner_rand() % (unsigned long)candidate_count];
	v_name = tag_get_name(vehicle_tag);
	if (v_name)
	{
		is_banshee = vehicle_tag_flies(vehicle_tag);
		is_hover = !is_banshee && vehicle_tag_hovers(vehicle_tag);
	}

	patch_vehicle_for_ai_driver(vehicle_tag, is_banshee, is_hover);

	object_placement_data_new(&placement, vehicle_tag, NONE);
	placement.position = *zone;
	if (is_banshee)
	{
		/* Spawn Banshees 8.0f-12.0f units in the air so they immediately fly in and attack */
		struct collision_result coll;
		real_point3d ray_start = *zone;
		real_vector3d ray_up = { 0.0f, 0.0f, 13.0f };
		real air_z = spawner_rand_range(8.5f, 11.5f);

		ray_start.z += 0.5f;
		if (collision_test_vector(FLAG(_collision_test_structure_bit), &ray_start, &ray_up, NONE, &coll))
		{
			real max_z = coll.t * 13.0f - 1.5f;
			if (max_z < air_z)
				air_z = MAX(2.5f, max_z);
		}
		placement.position.z += air_z;
	}
	else
	{
		project_point_to_ground(&placement.position, 0.65f);
	}
	vector3d_from_angle(&placement.forward, facing);
	placement.owner_team_index = enemy_team;

	vehicle_index = object_new(&placement);
	if (vehicle_index == NONE)
		return NONE;

	enemy_vehicle_note(vehicle_index);
	object_set_automatic_deactivation(vehicle_index, FALSE);
	object_activate(vehicle_index);

	chosen_driver_variant = pick_vehicle_driver_variant(
		driver_variant,
		global_variant.game_engine_variant.firefight.wave_faction);
	driver_unit = spawn_single_actor_at(
		chosen_driver_variant,
		_ff_tier_boss_heavy,
		&placement.position,
		facing,
		enemy_team,
		TRUE);
	if (driver_unit != NONE)
	{
		struct unit_datum *v_unit = unit_get(vehicle_index);
		struct unit_datum *d_unit = unit_try_and_get(driver_unit);
		struct unit_definition *v_def = v_unit ? unit_definition_get(v_unit->definition_index) : NULL;

		if (v_unit && d_unit && v_def && v_def->unit.seats.count > 0)
		{
			struct unit_seat *seat0 = TAG_BLOCK_GET_ELEMENT(&v_def->unit.seats, 0, struct unit_seat);

			SET_FLAG(v_unit->unit.flags, _unit_actively_controlled_bit, FALSE);
			v_unit->object.owner_team_index = enemy_team;

			/* Ensure the driver biped animation graph matches seat 0's label so unit_enter_seat succeeds */
			if (seat0)
			{
				vehicle_seat_fit_driver(driver_unit, vehicle_index, seat0);
				vehicle_seat_driver(driver_unit, vehicle_index, seat0);
			}
		}

		if (d_unit && d_unit->unit.actor_index != NONE)
		{
			struct actor_datum *d_actor = actor_get(d_unit->unit.actor_index);
			long player_units[16];
			short player_count = get_living_player_positions(NULL, player_units, 16);

			if (d_actor && d_unit->object.parent_object_index == vehicle_index)
			{
				d_actor->input.vehicle_index = vehicle_index;
				d_actor->input.vehicle_passenger = FALSE;
				d_actor->input.vehicle_gunner = TRUE;
				if (is_banshee)
				{
					d_actor->state.flying = TRUE;
					d_actor->input.vehicle_driver_type = _actor_vehicle_driver_directional_flying;
					vehicle_hover(vehicle_index, FALSE);
				}
				else
				{
					d_actor->input.vehicle_driver_type = is_hover ?
						_actor_vehicle_driver_hovering_ground :
						_actor_vehicle_driver_nondirectional_ground;
				}
			}
			if (player_count > 0)
				direct_actor_toward_unit(d_unit->unit.actor_index, player_units[0]);
		}
	}

	return vehicle_index;
}

static long spawn_specific_enemy_vehicle_at(
	boolean want_wraith,
	real_point3d const *zone,
	real facing,
	short enemy_team,
	boolean exact_pad)
{
	static char const *const ghost_tags[] = {
		"vehicles\\ghost\\ghost",
		"vehicles\\ghost\\ghost_mp"
	};
	static char const *const wraith_tags[] = {
		"vehicles\\wraith\\wraith",
		"vehicles\\scorpion\\scorpion_mp",
		"vehicles\\scorpion\\scorpion",
		"vehicles\\banshee\\banshee",
		"vehicles\\banshee\\banshee_mp"
	};
	long vehicle_tag = want_wraith ?
		find_loaded_vehicle_tag(wraith_tags, (short)NUMBEROF(wraith_tags)) :
		find_loaded_vehicle_tag(ghost_tags, (short)NUMBEROF(ghost_tags));
	byte driver_tier = _ff_tier_commander;
	/* (MAP CONTENT: the map's own in its place, ONLY MAP's always, INCLUDE
	MAP's half the time) */
	long map_vehicle = map_vehicles_only() ? pick_map_vehicle(want_wraith, FALSE) :
		map_content_of(FIREFIGHT_MAP_CONTENT_VEHICLES_OFF) == _firefight_map_content_include_map && (spawner_rand() & 1) ?
		pick_map_vehicle(want_wraith, TRUE) : NONE;
	long driver_variant = pick_variant_for_subtype(
		_ff_sub_elite_major,
		global_variant.game_engine_variant.firefight.wave_faction,
		&driver_tier,
		NULL,
		NULL);

	if (map_vehicle != NONE)
		vehicle_tag = map_vehicle;
	if (vehicle_tag == NONE)
	{
		return spawn_boss_vehicle_at(zone, facing, driver_variant, enemy_team, !want_wraith);
	}

	{
		boolean is_banshee = vehicle_tag_flies(vehicle_tag);
		boolean is_hover = !is_banshee && vehicle_tag_hovers(vehicle_tag);
		struct object_placement_data placement;
		long vehicle_index;
		long chosen_driver_variant;
		long driver_unit;

		patch_vehicle_for_ai_driver(vehicle_tag, is_banshee, is_hover);
		object_placement_data_new(&placement, vehicle_tag, NONE);
		placement.position = *zone;
		if (!exact_pad)
		{
			placement.position.x += spawner_rand_range(-2.0f, 2.0f);
			placement.position.y += spawner_rand_range(-2.0f, 2.0f);
		}
		if (is_banshee)
			placement.position.z += 9.0f;
		else
			project_point_to_ground(&placement.position, 0.65f);
		vector3d_from_angle(&placement.forward, facing);
		placement.owner_team_index = enemy_team;

		vehicle_index = object_new(&placement);
		if (vehicle_index == NONE)
			return NONE;

		enemy_vehicle_note(vehicle_index);
		object_set_automatic_deactivation(vehicle_index, FALSE);
		object_activate(vehicle_index);

		chosen_driver_variant = pick_vehicle_driver_variant(
			driver_variant,
			global_variant.game_engine_variant.firefight.wave_faction);
		driver_unit = spawn_single_actor_at(
			chosen_driver_variant,
			_ff_tier_boss_heavy,
			&placement.position,
			facing,
			enemy_team,
			TRUE);
		if (driver_unit != NONE)
		{
			struct unit_datum *v_unit = unit_get(vehicle_index);
			struct unit_datum *d_unit = unit_try_and_get(driver_unit);
			struct unit_definition *v_def = v_unit ? unit_definition_get(v_unit->definition_index) : NULL;

			if (v_unit && d_unit && v_def && v_def->unit.seats.count > 0)
			{
				struct unit_seat *seat0 = TAG_BLOCK_GET_ELEMENT(&v_def->unit.seats, 0, struct unit_seat);

				SET_FLAG(v_unit->unit.flags, _unit_actively_controlled_bit, FALSE);
				v_unit->object.owner_team_index = enemy_team;

				if (seat0)
				{
					vehicle_seat_fit_driver(driver_unit, vehicle_index, seat0);
					vehicle_seat_driver(driver_unit, vehicle_index, seat0);
				}
			}

			if (d_unit && d_unit->unit.actor_index != NONE)
			{
				struct actor_datum *d_actor = actor_get(d_unit->unit.actor_index);
				long player_units[16];
				short player_count = get_living_player_positions(NULL, player_units, 16);

				if (d_actor && d_unit->object.parent_object_index == vehicle_index)
				{
					d_actor->input.vehicle_index = vehicle_index;
					d_actor->input.vehicle_passenger = FALSE;
					d_actor->input.vehicle_gunner = TRUE;
					if (is_banshee)
					{
						d_actor->state.flying = TRUE;
						d_actor->input.vehicle_driver_type = _actor_vehicle_driver_directional_flying;
						vehicle_hover(vehicle_index, FALSE);
					}
					else
					{
						d_actor->input.vehicle_driver_type = is_hover ?
							_actor_vehicle_driver_hovering_ground :
							_actor_vehicle_driver_nondirectional_ground;
					}
				}
				if (player_count > 0)
					direct_actor_toward_unit(d_unit->unit.actor_index, player_units[0]);
			}
		}

		return vehicle_index;
	}
}

/* ---------- Wave Spawning Entry Point */

struct firefight_wave_spec
{
	byte grunts;
	byte jackals;
	byte minor_elites;
	byte major_elites;
	byte invis_elites;
	byte sword_elites;
	byte hunters;
	byte ghosts;
	byte wraiths;
};

/* Exact per-round / per-wave enemy composition across 5 Rounds x 3 Waves per Set (scaled to 60% of prior counts):
 * - Round 1 Total: 12 Grunts, 9 Jackals, 6 Minor Elites, 2 Major Elites
 * - Round 2 Total: 18 Grunts, 12 Jackals, 6 Minor Elites, 4 Major Elites, 1 Sword Elite
 * - Round 3 Total: 18 Grunts, 15 Jackals, 6 Minor Elites, 5 Major Elites, 3 Invis Elites, 1 Sword Elite
 * - Round 4 Total: 15 Grunts, 9 Jackals, 6 Minor Elites, 4 Major Elites, 2 Invis Elites, 1 Sword Elite, 3 Hunters, 3 Ghosts
 * - Round 5 Total: 15 Grunts, 9 Jackals, 6 Minor Elites, 5 Major Elites, 3 Invis Elites, 1 Sword Elite, 3 Hunters, 3 Ghosts, 2 Wraiths
 */
static struct firefight_wave_spec const k_firefight_wave_specs[5][3] =
{
	/* Round 1 */
	{
		{ 4, 3, 2, 0, 0, 0, 0, 0, 0 },
		{ 4, 3, 2, 1, 0, 0, 0, 0, 0 },
		{ 4, 3, 2, 1, 0, 0, 0, 0, 0 }
	},
	/* Round 2 */
	{
		{ 6, 4, 2, 1, 0, 0, 0, 0, 0 },
		{ 6, 4, 2, 1, 0, 0, 0, 0, 0 },
		{ 6, 4, 2, 2, 0, 1, 0, 0, 0 }
	},
	/* Round 3 */
	{
		{ 6, 5, 2, 1, 1, 0, 0, 0, 0 },
		{ 6, 5, 2, 2, 1, 0, 0, 0, 0 },
		{ 6, 5, 2, 2, 1, 1, 0, 0, 0 }
	},
	/* Round 4 */
	{
		{ 5, 3, 2, 1, 0, 0, 1, 1, 0 },
		{ 5, 3, 2, 1, 1, 0, 1, 1, 0 },
		{ 5, 3, 2, 2, 1, 1, 1, 1, 0 }
	},
	/* Round 5 */
	{
		{ 5, 3, 2, 1, 1, 0, 1, 1, 0 },
		{ 5, 3, 2, 2, 1, 0, 1, 1, 1 },
		{ 5, 3, 2, 2, 1, 1, 1, 1, 1 }
	}
};

static short count_active_session_players(void)
{
	struct data_iterator iter;
	struct player_datum *player;
	short total = 0;

	if (!player_data)
		return 1;
	data_iterator_new(&iter, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iter)) != NULL)
	{
		if (!player->quit_out_of_game)
			total++;
	}
	return MAX(total, (short)1);
}

static short scale_enemy_count(byte base_count, real combined_mult)
{
	short scaled;

	if (base_count == 0)
		return 0;
	scaled = (short)((real)base_count * combined_mult + 0.5f);
	return MAX(scaled, (short)1);
}

static void append_wave_subtypes(
	byte *out_list,
	short *io_count,
	short max_cap,
	byte subtype,
	short count)
{
	short i;

	for (i = 0; i < count && *io_count < max_cap; i++)
	{
		out_list[(*io_count)++] = subtype;
	}
}

boolean firefight_wave_will_have_heavy_or_boss(
	short set_num,
	short round_num,
	short wave_num)
{
	struct game_variant_options const *opts = game_variant_options_get();
	byte boss_mode = opts ? opts->firefight_boss_wave_mode : _firefight_boss_wave_5_only;

	(void)set_num;
	if (boss_mode == _firefight_boss_rush)
		return TRUE;
	if (boss_mode == _firefight_boss_every_3rd)
		return (wave_num == FIREFIGHT_WAVES_PER_ROUND);
	return (round_num >= 4 && wave_num == FIREFIGHT_WAVES_PER_ROUND);
}

short firefight_spawn_wave(
	short set_num,
	short round_num,
	short wave_num,
	boolean is_bonus)
{
	struct game_engine_firefight_variant const *ff = &global_variant.game_engine_variant.firefight;
	struct game_variant_options const *opts = game_variant_options_get();
	real facings[FIREFIGHT_MAX_SPAWN_ZONES_PER_WAVE];
	short zone_count;
	byte wave_subtypes[256];
	short total_to_spawn = 0;
	short immediate_to_spawn;
	byte wave_faction = firefight_wave_faction(ff->wave_faction, is_bonus);
	short forced_team = NONE;
	short spawned_count = 0;
	boolean is_boss_wave = firefight_wave_will_have_heavy_or_boss(set_num, round_num, wave_num);
	short r_idx = (short)PIN(round_num - 1, 0, 4);
	short w_idx = (short)PIN(wave_num - 1, 0, 2);
	struct firefight_wave_spec const *spec = &k_firefight_wave_specs[r_idx][w_idx];
	short active_players = count_active_session_players();
	short extra_players = (short)MAX(active_players - 1, 0);
	short scale_pct = firefight_server_player_scale_percent_get();
	real player_mult = 1.0f + (real)extra_players * ((real)scale_pct / 100.0f);
	real wave_size_mult = 1.0f;
	real combined_mult;
	short i;

	firefight_prune_dead_tracked_enemies();

	if (opts)
	{
		switch (opts->firefight_wave_size_scale)
		{
		case _firefight_wave_size_50:  wave_size_mult = 0.5f; break;
		case _firefight_wave_size_150: wave_size_mult = 1.5f; break;
		case _firefight_wave_size_200: wave_size_mult = 2.0f; break;
		case _firefight_wave_size_250: wave_size_mult = 2.5f; break;
		case _firefight_wave_size_300: wave_size_mult = 3.0f; break;
		default:                       wave_size_mult = 1.0f; break;
		}
	}
	combined_mult = player_mult * wave_size_mult;

	if (is_bonus)
	{
		append_wave_subtypes(
			wave_subtypes,
			&total_to_spawn,
			(short)NUMBEROF(wave_subtypes),
			_ff_sub_grunt,
			scale_enemy_count(10, combined_mult));
	}
	else
	{
		append_wave_subtypes(wave_subtypes, &total_to_spawn, (short)NUMBEROF(wave_subtypes),
			_ff_sub_grunt, scale_enemy_count(spec->grunts, combined_mult));
		append_wave_subtypes(wave_subtypes, &total_to_spawn, (short)NUMBEROF(wave_subtypes),
			_ff_sub_jackal, scale_enemy_count(spec->jackals, combined_mult));
		append_wave_subtypes(wave_subtypes, &total_to_spawn, (short)NUMBEROF(wave_subtypes),
			_ff_sub_elite_minor, scale_enemy_count(spec->minor_elites, combined_mult));
		append_wave_subtypes(wave_subtypes, &total_to_spawn, (short)NUMBEROF(wave_subtypes),
			_ff_sub_elite_major, scale_enemy_count(spec->major_elites, combined_mult));
		append_wave_subtypes(wave_subtypes, &total_to_spawn, (short)NUMBEROF(wave_subtypes),
			_ff_sub_elite_stealth, scale_enemy_count(spec->invis_elites, combined_mult));
		append_wave_subtypes(wave_subtypes, &total_to_spawn, (short)NUMBEROF(wave_subtypes),
			_ff_sub_elite_zealot, scale_enemy_count(spec->sword_elites, combined_mult));
		append_wave_subtypes(wave_subtypes, &total_to_spawn, (short)NUMBEROF(wave_subtypes),
			_ff_sub_hunter, scale_enemy_count(spec->hunters, combined_mult));
	}

	{
		short available_slots = (short)MAX(0, FIREFIGHT_SAFE_ACTIVE_ACTOR_CAP - ff_state.enemies_alive);
		immediate_to_spawn = MIN(total_to_spawn, available_slots);
		g_queued_reinforcement_enemies = (short)MAX(0, total_to_spawn - immediate_to_spawn);
		for (i = 0; i < g_queued_reinforcement_enemies; i++)
		{
			g_queued_reinforcement_subtypes[i] = wave_subtypes[immediate_to_spawn + i];
		}
		ff_state.enemies_queued = g_queued_reinforcement_enemies;
	}

	zone_count = select_wave_spawn_zones(
		ff_state.wave_spawn_zones,
		facings,
		(wave_num == 1 && !is_bonus) ? 2 : FIREFIGHT_MAX_SPAWN_ZONES_PER_WAVE,
		round_num,
		wave_num,
		is_bonus,
		is_boss_wave);
	ff_state.wave_spawn_zone_count = zone_count;
	for (i = 0; i < zone_count && i < FIREFIGHT_MAX_SPAWN_ZONES_PER_WAVE; i++)
		g_active_wave_facings[i] = facings[i];
	if (zone_count == 0)
		return 0;

	if (!opts || opts->firefight_faction_diplomacy == _firefight_diplomacy_allied_against_players)
	{
		forced_team = (wave_faction == _firefight_faction_unsc_humans) ?
			_game_team_human : _game_team_covenant;
	}

	/* Teleport / Orbital Insertion Beacon effects & Real 3D Flying Dropship on wave spawn zones */
	{
		boolean dropship_spawned = FALSE;
		short dropship_squad_count = 0;

		g_dropship_squad_subtype_count = 0;
		if (!is_bonus && (!opts || opts->firefight_dropship_mode != _firefight_dropship_off))
		{
			byte d_mode = opts ? opts->firefight_dropship_mode : _firefight_dropship_auto;
			if (d_mode == _firefight_dropship_frequent ||
				(d_mode == _firefight_dropship_boss_only && is_boss_wave) ||
				(d_mode == _firefight_dropship_auto))
			{
				short z_idx;
				short zone0_share = (short)((immediate_to_spawn + zone_count - 1) / zone_count);
				dropship_squad_count = PIN(zone0_share, 3, 8);

				dropship_spawned = start_dropship_insertion(
					wave_faction == _firefight_faction_unsc_humans,
					&ff_state.wave_spawn_zones[0],
					facings[0],
					wave_faction,
					is_boss_wave,
					dropship_squad_count,
					forced_team);
				if (!dropship_spawned)
					dropship_squad_count = 0;

				for (z_idx = 1; z_idx < zone_count; z_idx++)
				{
					spawn_insertion_beacon_effect(
						&ff_state.wave_spawn_zones[z_idx],
						NONE,
						wave_faction,
						is_boss_wave);
				}
			}
		}

		for (i = 0; i < immediate_to_spawn; i++)
		{
			short z = (short)(i % zone_count);
			byte subtype = wave_subtypes[i];
			boolean is_heavy_unit = (subtype == _ff_sub_hunter || subtype == _ff_sub_elite_zealot);
			long unit_index;

			/* Hold zone 0's squad onboard the approaching 3D dropship for Phase 2 unload */
			if (dropship_spawned && z == 0 && dropship_squad_count > 0)
			{
				if (g_dropship_squad_subtype_count < (short)NUMBEROF(g_dropship_squad_subtypes))
					g_dropship_squad_subtypes[g_dropship_squad_subtype_count++] = subtype;
				dropship_squad_count--;
				spawned_count++;
				continue;
			}

			unit_index = spawn_subtype_actor_at(
				subtype,
				wave_faction,
				&ff_state.wave_spawn_zones[z],
				facings[z],
				forced_team,
				TRUE);
			if (unit_index != NONE)
			{
				if (i < zone_count && (!opts || opts->firefight_dropship_mode != _firefight_dropship_off))
				{
					real_point3d unit_pos;
					object_get_origin(unit_index, &unit_pos);
					spawn_insertion_beacon_effect(&unit_pos, unit_index, wave_faction, is_heavy_unit);
				}
				spawned_count++;
			}
		}

		/* Spawn Round-Scheduled or Marker-Scheduled Enemy Vehicles:
		 * - Default stock maps without custom Firefight Spawn markers keep normal Round 4/5 vehicle spawns.
		 * - Custom maps OR any map with placed Firefight Spawn markers only spawn enemy vehicles at approved
		 *   vehicle spawn points (AI + VEHICLES, ENEMY VEHICLE, ENEMY GHOST, ENEMY HEAVY VEH) matching the round/wave! */
		if (!is_bonus && zone_count > 0)
		{
			short enemy_team = forced_team != NONE ? forced_team : _game_team_covenant;
			boolean require_approved_veh_markers = (map_has_any_custom_ff_spawns() || !is_default_stock_map());
			short v;

			if (require_approved_veh_markers)
			{
				real_point3d veh_pts[32];
				real veh_facings[32];
				int veh_types[32];
				unsigned long ghost_mask =
					(1UL << FF_SPAWN_TYPE_AI_AND_VEHICLES) |
					(1UL << FF_SPAWN_TYPE_ENEMY_VEHICLE) |
					(1UL << FF_SPAWN_TYPE_ENEMY_GHOST);
				unsigned long wraith_mask =
					(1UL << FF_SPAWN_TYPE_AI_AND_VEHICLES) |
					(1UL << FF_SPAWN_TYPE_ENEMY_VEHICLE) |
					(1UL << FF_SPAWN_TYPE_ENEMY_HEAVY_VEH);
				short ghost_pad_count = collect_matching_custom_ff_spawns(
					ghost_mask, round_num, wave_num, is_bonus, is_boss_wave, FALSE,
					veh_pts, veh_facings, veh_types, 32);
				short ghosts_to_spawn = spec->ghosts;

				if (ghost_pad_count > 0)
				{
					if (ghosts_to_spawn == 0)
					{
						short dedicated_ghosts = 0;
						for (v = 0; v < ghost_pad_count; v++)
						{
							if (veh_types[v] == FF_SPAWN_TYPE_ENEMY_GHOST ||
								veh_types[v] == FF_SPAWN_TYPE_ENEMY_VEHICLE)
							{
								dedicated_ghosts++;
							}
						}
						ghosts_to_spawn = MIN(dedicated_ghosts, (short)2);
					}
					for (v = 0; v < ghosts_to_spawn; v++)
					{
						short p_idx = (short)((v + (short)spawner_rand()) % ghost_pad_count);
						if (spawn_specific_enemy_vehicle_at(
							FALSE,
							&veh_pts[p_idx],
							veh_facings[p_idx],
							enemy_team,
							v == 0 || ghost_pad_count > 1) != NONE)
						{
							spawned_count++;
						}
					}
				}

				{
					short wraith_pad_count = collect_matching_custom_ff_spawns(
						wraith_mask, round_num, wave_num, is_bonus, is_boss_wave, FALSE,
						veh_pts, veh_facings, veh_types, 32);
					short wraiths_to_spawn = spec->wraiths;

					if (wraith_pad_count > 0)
					{
						if (wraiths_to_spawn == 0)
						{
							short dedicated_heavy = 0;
							for (v = 0; v < wraith_pad_count; v++)
							{
								if (veh_types[v] == FF_SPAWN_TYPE_ENEMY_HEAVY_VEH)
									dedicated_heavy++;
							}
							wraiths_to_spawn = MIN(dedicated_heavy, (short)1);
						}
						for (v = 0; v < wraiths_to_spawn; v++)
						{
							short p_idx = (short)((v + (short)spawner_rand()) % wraith_pad_count);
							if (spawn_specific_enemy_vehicle_at(
								TRUE,
								&veh_pts[p_idx],
								veh_facings[p_idx],
								enemy_team,
								v == 0 || wraith_pad_count > 1) != NONE)
							{
								spawned_count++;
							}
						}
					}
				}
			}
			else
			{
				for (v = 0; v < spec->ghosts; v++)
				{
					short z = (short)((zone_count - 1 - (v % zone_count) + zone_count) % zone_count);
					if (spawn_specific_enemy_vehicle_at(
						FALSE,
						&ff_state.wave_spawn_zones[z],
						facings[z],
						enemy_team,
						FALSE) != NONE)
					{
						spawned_count++;
					}
				}
				for (v = 0; v < spec->wraiths; v++)
				{
					short z = (short)(v % zone_count);
					if (spawn_specific_enemy_vehicle_at(
						TRUE,
						&ff_state.wave_spawn_zones[z],
						facings[z],
						enemy_team,
						FALSE) != NONE)
					{
						spawned_count++;
					}
				}
			}
		}
	}

	return (short)(spawned_count + g_queued_reinforcement_enemies);
}

/* ---------- Safe Player Ammo & Heavy Ordnance Drops (Feature 5) */

static void snap_resupply_point_to_ground(real_point3d *pt)
{
	struct collision_result coll;
	real_point3d ray_start;
	real_vector3d ray_down = { 0.0f, 0.0f, -6.0f };

	if (!pt)
		return;
	ray_start = *pt;
	ray_start.z += 1.2f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit), &ray_start, &ray_down, NONE, &coll))
	{
		pt->x = coll.point.x;
		pt->y = coll.point.y;
		pt->z = coll.point.z + 0.25f;
	}
	else
	{
		pt->z += 0.30f;
	}
}

static boolean point_is_safe_from_enemy_zones(real_point3d const *pt, real min_distance)
{
	short z;

	if (!pt)
		return FALSE;
	for (z = 0; z < ff_state.wave_spawn_zone_count && z < FIREFIGHT_MAX_SPAWN_ZONES_PER_WAVE; z++)
	{
		if (distance3d(pt, &ff_state.wave_spawn_zones[z]) < min_distance)
			return FALSE;
	}
	return TRUE;
}

static void calculate_safe_player_resupply_point(
	real_point3d const *player_pos,
	long player_unit_index,
	real_point3d *out_drop_center)
{
	static real const candidate_distances[] = { 6.0f, 7.5f, 5.0f, 9.0f };
	static real const candidate_angle_offsets[] = {
		0.0f, 0.5235988f, -0.5235988f, 1.0471976f, -1.0471976f,
		1.5707963f, -1.5707963f, 3.1415927f
	};
	struct unit_datum *u = player_unit_index != NONE ?
		(struct unit_datum *)object_try_and_get_and_verify_type(player_unit_index, _object_mask_unit) : NULL;
	real base_angle = 0.0f;
	short d_idx, a_idx;

	if (!player_pos || !out_drop_center)
		return;

	if (u && (u->object.forward.i != 0.0f || u->object.forward.j != 0.0f))
		base_angle = (real)atan2(u->object.forward.j, u->object.forward.i);

	/* Pass 1: 5.0 to 9.0 world units in front/around player, inside BSP, and >= 20u from enemy spawn zones */
	for (d_idx = 0; d_idx < (short)NUMBEROF(candidate_distances); d_idx++)
	{
		real dist = candidate_distances[d_idx];
		for (a_idx = 0; a_idx < (short)NUMBEROF(candidate_angle_offsets); a_idx++)
		{
			real ang = base_angle + candidate_angle_offsets[a_idx];
			real_point3d candidate = *player_pos;

			candidate.x += (real)cos(ang) * dist;
			candidate.y += (real)sin(ang) * dist;
			snap_resupply_point_to_ground(&candidate);
			if (point_is_in_valid_bsp(&candidate) && point_is_safe_from_enemy_zones(&candidate, 20.0f))
			{
				*out_drop_center = candidate;
				return;
			}
		}
	}

	/* Pass 2: 5.0 to 9.0 world units in front/around player, inside BSP */
	for (d_idx = 0; d_idx < (short)NUMBEROF(candidate_distances); d_idx++)
	{
		real dist = candidate_distances[d_idx];
		for (a_idx = 0; a_idx < (short)NUMBEROF(candidate_angle_offsets); a_idx++)
		{
			real ang = base_angle + candidate_angle_offsets[a_idx];
			real_point3d candidate = *player_pos;

			candidate.x += (real)cos(ang) * dist;
			candidate.y += (real)sin(ang) * dist;
			snap_resupply_point_to_ground(&candidate);
			if (point_is_in_valid_bsp(&candidate))
			{
				*out_drop_center = candidate;
				return;
			}
		}
	}

	*out_drop_center = *player_pos;
	out_drop_center->z += 0.35f;
}

static long spawn_protected_resupply_item(long tag_index, real_point3d const *center, real offset_x, real offset_y)
{
	struct object_placement_data placement;
	long obj_index;

	if (tag_index == NONE || !center)
		return NONE;

	object_placement_data_new(&placement, tag_index, NONE);
	placement.position = *center;
	placement.position.x += offset_x;
	placement.position.y += offset_y;
	snap_resupply_point_to_ground(&placement.position);
	if (!point_is_in_valid_bsp(&placement.position))
		placement.position = *center;

	obj_index = object_new(&placement);
	if (obj_index != NONE)
	{
		struct object_datum *obj = object_get(obj_index);
		if (obj)
			SET_FLAG(obj->object.flags, _object_cannot_be_garbage_bit, TRUE);
	}
	return obj_index;
}

static long find_loaded_tag_by_substring(unsigned long group_tag, char const *exact_path, char const *sub1, char const *sub2)
{
	struct tag_iterator iter;
	long idx;

	if (exact_path)
	{
		idx = tag_loaded(group_tag, exact_path);
		if (idx != NONE)
			return idx;
	}

	tag_iterator_new(&iter, group_tag);
	while ((idx = tag_iterator_next(&iter)) != NONE)
	{
		char const *raw_name = tag_get_name(idx);
		char lower[256];

		if (!raw_name)
			continue;
		lowercase_copy(raw_name, lower, sizeof(lower));
		if (sub1 && !strstr(lower, sub1))
			continue;
		if (sub2 && !strstr(lower, sub2))
			continue;
		return idx;
	}
	return NONE;
}

static void pod_contents_add(long tag_index);
static void pod_contents_spawn(void);
static boolean ordnance_pod_spot(real_point3d const *anchor, long anchor_unit, boolean at_area,
	real_point3d const *others, short other_count, real_point3d *out_position);
static void pods_shape_choose(void);
static boolean drop_area_nearest(real_point3d const *point, real_point3d *area);

void firefight_ordnance_preview_refusals(char *text, long size);

void firefight_spawn_ordnance_drop(boolean heavy_anti_armor)
{
	real_point3d player_positions[16];
	long player_units[16];
	short player_count = get_living_player_positions(player_positions, player_units, 16);
	short total_players = count_active_session_players();
	short round_num = (short)MAX(1, ff_state.current_round);
	long pistol_weap;
	long ar_weap;
	long shotgun_weap;
	long sniper_weap;
	long rocket_weap;
	real_point3d anchor;
	long anchor_unit;
	short pod_count;
	short i, p;

	if (player_count == 0)
	{
		real fallback_facing = 0.0f;
		/* At tick 0 before players spawn their initial biped, wait for tick 5 resupply */
		if (ff_state.session_elapsed_ticks < 2)
			return;
		if (!firefight_select_player_spawn_point(0, &player_positions[0], &fallback_facing))
			return;
		player_units[0] = NONE;
		player_count = 1;
	}

	if (ff_power_weapon_tag_count == 0 && ff_standard_weapon_tag_count == 0)
		scan_ordnance_weapon_and_equipment_tags();

	pistol_weap = find_loaded_tag_by_substring(WEAPON_DEFINITION_TAG, "weapons\\pistol\\pistol", "pistol", NULL);
	ar_weap = find_loaded_tag_by_substring(WEAPON_DEFINITION_TAG, "weapons\\assault rifle\\assault rifle", "assault rifle", NULL);
	shotgun_weap = find_loaded_tag_by_substring(WEAPON_DEFINITION_TAG, "weapons\\shotgun\\shotgun", "shotgun", NULL);
	sniper_weap = find_loaded_tag_by_substring(WEAPON_DEFINITION_TAG, "weapons\\sniper rifle\\sniper rifle", "sniper rifle", NULL);
	rocket_weap = find_loaded_tag_by_substring(WEAPON_DEFINITION_TAG, "weapons\\rocket launcher\\rocket launcher", "rocket launcher", NULL);

	/* Directly refill all living players' reserve ammunition (except rocket launchers), grenades, and health on resupply */
	for (i = 0; i < player_count; i++)
	{
		struct unit_datum *u = player_units[i] != NONE ?
			(struct unit_datum *)object_try_and_get_and_verify_type(player_units[i], _object_mask_unit) : NULL;
		short slot;

		if (!u)
			continue;
		if (u->object.body_vitality < 1.0f)
			u->object.body_vitality = 1.0f;
		if (u->unit.grenade_counts[_unit_grenade_human_fragmentation] < 4)
			u->unit.grenade_counts[_unit_grenade_human_fragmentation] =
				(signed char)MIN(4, u->unit.grenade_counts[_unit_grenade_human_fragmentation] + 2);
		if (u->unit.grenade_counts[_unit_grenade_covenant_plasma] < 4)
			u->unit.grenade_counts[_unit_grenade_covenant_plasma] =
				(signed char)MIN(4, u->unit.grenade_counts[_unit_grenade_covenant_plasma] + 2);

		for (slot = 0; slot < 4; slot++)
		{
			long weap_idx = u->unit.weapon_object_indices[slot];
			struct weapon_datum *weap;
			struct weapon_definition *weap_def;
			short m;

			if (weap_idx == NONE)
				continue;
			weap = (struct weapon_datum *)object_try_and_get_and_verify_type(weap_idx, _object_mask_weapon);
			if (!weap || weap->definition_index == rocket_weap)
				continue;
			weap->weapon.age = 0.0f;
			weap_def = weapon_definition_get(weap->definition_index);
			if (!weap_def)
				continue;
			for (m = 0; m < weap_def->weapon.magazines.count && m < 2; m++)
			{
				struct weapon_magazine_definition *mag_def = TAG_BLOCK_GET_ELEMENT(
					&weap_def->weapon.magazines, m, struct weapon_magazine_definition);
				if (mag_def && mag_def->rounds_total_maximum > 0)
					weap->weapon.magazines[m].rounds_total = mag_def->rounds_total_maximum;
			}
		}
	}

	/* (the pods come to the player nearest the rest: people, not bots) */
	{
		real_point3d human_positions[16];
		long human_units[16];
		short human_count = get_living_human_player_positions(human_positions, human_units, 16);
		short central;

		if (human_count > 0)
		{
			central = central_player_index(human_positions, human_count);
			anchor = human_positions[central];
			anchor_unit = human_units[central];
		}
		else
		{
			central = central_player_index(player_positions, player_count);
			anchor = player_positions[central];
			anchor_unit = player_units[central];
		}
	}

	/* (pods still in the air with the last drop's: their weapons out now) */
	pod_contents_spawn();
	g_pod_held_count = 0;

	/* What the pods hold, full of ammunition, scaling with player numbers (up
	to 8 players' worth, a weapon a pod):
	 * - Rounds 1-5: 1 Pistol, 1 Assault Rifle, 1 Shotgun
	 * - Rounds 3-5: + 1 Sniper Rifle
	 * - Rounds 4-5 (or a heavy drop): + 1 Rocket Launcher
	 * - the variant's profile's weapon, and INCLUDE MAP's one of the map's own;
	 * ONLY MAP: the map's own weapons in the game's place (3 each, and a power
	 * weapon from round 3), the game's where the map has none */
	for (p = 0; p < MIN(total_players, 8); p++)
	{
		boolean heavy = round_num >= 4 || heavy_anti_armor;

		if (map_weapons_only())
		{
			pod_contents_add(pick_map_weapon(FALSE, FALSE));
			pod_contents_add(pick_map_weapon(FALSE, FALSE));
			pod_contents_add(pick_map_weapon(FALSE, FALSE));
			if (round_num >= 3 || heavy)
				pod_contents_add(pick_map_weapon(TRUE, FALSE));
		}
		else
		{
			pod_contents_add(pistol_weap);
			pod_contents_add(ar_weap);
			pod_contents_add(shotgun_weap);
			if (round_num >= 3)
				pod_contents_add(sniper_weap);
			if (heavy)
				pod_contents_add(rocket_weap);
			if (map_content_of(FIREFIGHT_MAP_CONTENT_WEAPONS_OFF) == _firefight_map_content_include_map)
				pod_contents_add(pick_map_weapon(heavy, TRUE));
		}
		/* (and one of the variant's weapons for each player) */
		pod_contents_add(pick_custom_weapon(heavy));
	}

	/* where each pod lands (ordnance_pod_spot), apart from each other: round
	the drop area nearest the player, when Forge marked any, else near the
	player */
	pod_count = (short)PIN(g_pod_content_count, 1, FIREFIGHT_MAX_ORDNANCE_PODS);
	pods_shape_choose();
	csmemset(g_pod_spot_refusals, 0, sizeof(g_pod_spot_refusals));
	g_pod_spot_blocker = NONE;
	{
		real_point3d area;
		boolean around_area = drop_area_nearest(&anchor, &area);
		short fallen_back = 0;

		for (i = 0; i < pod_count; i++)
		{
			if (around_area && ordnance_pod_spot(&area, NONE, TRUE, ff_state.ordnance_pod_positions, i,
				&ff_state.ordnance_pod_positions[i]))
				continue;
			if (!ordnance_pod_spot(&anchor, anchor_unit, FALSE, ff_state.ordnance_pod_positions, i,
				&ff_state.ordnance_pod_positions[i]))
			{
				/* (nowhere that passes: the old way, before the player) */
				calculate_safe_player_resupply_point(&anchor, anchor_unit, &ff_state.ordnance_pod_positions[i]);
				ff_state.ordnance_pod_positions[i].x += (real)(i % 8) * 1.5f;
				ff_state.ordnance_pod_positions[i].y += (real)(i / 8) * 1.5f;
				snap_resupply_point_to_ground(&ff_state.ordnance_pod_positions[i]);
				fallen_back++;
			}
		}
		if (fallen_back)
		{
			char reasons[256];

			firefight_ordnance_preview_refusals(reasons, sizeof(reasons));
			error(_error_silent, "firefight: %d of %d ordnance pods found no spot (%s): put in a row; refused: %s",
				fallen_back, pod_count, around_area ? "round a drop area, then the player" : "near the player", reasons);
		}
	}
	ff_state.ordnance_pod_count = pod_count;
	ff_state.last_ordnance_position = ff_state.ordnance_pod_positions[0];
	ff_state.ordnance_navpoint_ticks = 20 * TICKS_PER_SECOND;
	ff_state.last_ordnance_is_heavy = (round_num >= 4 || heavy_anti_armor);

	/* (let go now: every machine shows them falling, and their weapons come
	out on them as they land, firefight_ordnance_pod_update) */
	ff_state.ordnance_pod_drop_time = game_time_get();

	firefight_announce_banner(
		(round_num >= 4 || heavy_anti_armor) ? _ff_banner_heavy_ordnance_drop : _ff_banner_ordnance_drop,
		0,
		3 * TICKS_PER_SECOND);
}

/* ---------- The Ordnance Pods

A drop comes down from the sky in pods, a weapon in each: a capsule each (the
game's props: tools/prop_assets.py), else a Covenant storage crate (c40's
scenery\c_storage, from the game modes' pack: randomizer.c's gather_firefight),
round the drop area nearest the player when Forge marked any,
falling for FF_POD_FALL_TICKS onto ordnance_pod_positions, their weapons held
upright against their sides once they land, full of ammunition, till taken.
A pod is scenery, which the netcode doesn't carry: each machine makes its
own, from the time the host let them go and where they land (Firefight's
state), so they fall alike everywhere; the weapons are the host's, as any. */

/* (a pod's half width it is kept from walls and others by, and how far
apart pods land: the crate's, or the capsule's, which is slim) */
#define FF_POD_CRATE_CLEARANCE 1.7f
#define FF_POD_CRATE_APART 4.5f
#define FF_POD_CAPSULE_CLEARANCE 0.8f
#define FF_POD_CAPSULE_APART 1.8f
static real g_pod_clearance = FF_POD_CRATE_CLEARANCE;
static real g_pod_apart = FF_POD_CRATE_APART;
/* an object a pod's spot pays no heed to: a test drop's drop area, which
Firefight would have taken away (NONE: none) */
static long g_pod_spot_ignored = NONE;

/* the pod's tag: the capsule, else the crate (NONE: neither) */
static long pod_tag_get(boolean *capsule)
{
	long tag = tag_loaded('scen', FF_POD_TAG);

	*capsule = tag != NONE;
	return tag != NONE ? tag : tag_loaded('scen', FF_POD_FALLBACK_TAG);
}

/* (the host's, as it drops them) how far apart the pods are, by their shape */
static void pods_shape_choose(void)
{
	boolean capsule;

	pod_tag_get(&capsule);
	g_pod_clearance = capsule ? FF_POD_CAPSULE_CLEARANCE : FF_POD_CRATE_CLEARANCE;
	g_pod_apart = capsule ? FF_POD_CAPSULE_APART : FF_POD_CRATE_APART;
}

/* (each machine's) the level's drop areas, once it plays: where each stands,
and its marker taken away (scenery, which each machine has its own of) */
static void drop_areas_find(void)
{
	long tag;
	struct object_iterator iterator;
	long markers[FF_MAX_DROP_AREAS];
	short marker_count = 0;
	short index;

	if (g_drop_areas_found)
		return;
	g_drop_areas_found = TRUE;
	g_drop_area_count = 0;
	tag = tag_loaded('scen', FF_DROP_AREA_TAG);
	if (tag == NONE)
		return;
	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while (object_iterator_next(&iterator) && marker_count < FF_MAX_DROP_AREAS)
	{
		struct object_datum *object = object_get(iterator.index);

		short pod;

		if (object->definition_index != tag)
			continue;
		for (pod = 0; pod < FIREFIGHT_MAX_ORDNANCE_PODS && g_pod_objects[pod] != iterator.index; pod++)
		{
		}
		if (pod < FIREFIGHT_MAX_ORDNANCE_PODS)
			continue;
		g_drop_areas[g_drop_area_count++] = object->object.position;
		markers[marker_count++] = iterator.index;
	}
	for (index = 0; index < marker_count; index++)
		object_delete(markers[index]);
	if (g_drop_area_count)
		error(_error_silent, "firefight: %d drop areas for the ordnance pods", g_drop_area_count);
}

/* the drop area nearest a point; FALSE when the level has none */
static boolean drop_area_nearest(real_point3d const *point, real_point3d *area)
{
	real best = REAL_MAX;
	short index;

	drop_areas_find();
	for (index = 0; index < g_drop_area_count; index++)
	{
		real distance = distance_squared3d(point, &g_drop_areas[index]);

		if (distance < best)
		{
			best = distance;
			*area = g_drop_areas[index];
		}
	}
	return g_drop_area_count > 0;
}

static boolean pod_ray(unsigned long flags, real_point3d const *start, real_vector3d const *vector,
	struct collision_result *collision)
{
	return collision_test_vector(flags, start, vector, NONE, collision);
}

/* whether a pod may land at the ground under `candidate` (which it becomes):
flat ground of the map's (not a vehicle's roof nor a crate's), seen from
where the player's head is (not behind or in a wall), nothing within its
width on any side, open sky over it (`sky` units at least), no vehicle or
person on it, apart from the drop's other pods */
static boolean pod_spot_passes(
	real_point3d const *eye,
	real_point3d *candidate,
	real sky,
	boolean away_from_enemies,
	real_point3d const *others,
	short other_count)
{
	static unsigned long const structure = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit);
	unsigned long const solid = structure | FLAG(_collision_test_objects_bit) |
		FLAG(_collision_test_objects_vehicles_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_bipeds_bit);
	struct collision_result collision;
	real_point3d ground;
	real_point3d start;
	real_vector3d vector;
	short side;

	/* (the ground: what a ray down meets first must be the map's, and level) */
	start = *candidate;
	start.z = eye->z + 2.0f;
	vector.i = 0.0f;
	vector.j = 0.0f;
	vector.k = -12.0f;
	if (!pod_ray(solid, &start, &vector, &collision))
		return POD_SPOT_REFUSED(_pod_spot_no_ground);
	if (collision.object_index != NONE)
	{
		struct object_datum *under = object_try_and_get(collision.object_index);

		if (g_pod_spot_blocker == NONE && under)
			g_pod_spot_blocker = under->definition_index;
		return POD_SPOT_REFUSED(_pod_spot_object_under);
	}
	if (collision.plane.n.k < 0.8f)
		return POD_SPOT_REFUSED(_pod_spot_slope);
	ground = collision.point;
	/* (a point a little over the floor: one on it can be in no leaf) */
	start = ground;
	start.z += 0.3f;
	if (!point_is_in_valid_bsp(&start))
		return POD_SPOT_REFUSED(_pod_spot_outside_map);
	/* (seen from the player: no wall between, nor the pod inside one) */
	start = ground;
	start.z += 0.8f;
	vector_from_points3d(eye, &start, &vector);
	if (pod_ray(structure, eye, &vector, &collision) && collision.t < 0.98f)
		return POD_SPOT_REFUSED(_pod_spot_unseen);
	/* (its width clear round it, low and high) */
	for (side = 0; side < 8; side++)
	{
		real angle = (real)side * (_pi / 4.0f);
		short level;

		for (level = 0; level < 2; level++)
		{
			start = ground;
			start.z += level ? 1.3f : 0.45f;
			vector.i = (real)cos(angle) * g_pod_clearance;
			vector.j = (real)sin(angle) * g_pod_clearance;
			vector.k = 0.0f;
			if (pod_ray(solid, &start, &vector, &collision))
				return POD_SPOT_REFUSED(_pod_spot_cramped);
		}
	}
	/* (the sky open over it: its fall) */
	start = ground;
	start.z += 0.5f;
	vector.i = 0.0f;
	vector.j = 0.0f;
	vector.k = FF_POD_FALL_HEIGHT;
	if (pod_ray(solid, &start, &vector, &collision) && collision.t * FF_POD_FALL_HEIGHT < sky)
		return POD_SPOT_REFUSED(_pod_spot_roofed);
	/* (no vehicle, person or generator about it: an object's own size too) */
	{
		struct object_iterator iterator;
		struct object_datum *object;

		object_iterator_new(&iterator, _object_mask_vehicle | _object_mask_biped | _object_mask_scenery, 0);
		while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
		{
			real dx = object->object.bounding_sphere_center.x - ground.x;
			real dy = object->object.bounding_sphere_center.y - ground.y;
			real reach = object->object.bounding_sphere_radius + g_pod_clearance + 0.5f;

			if (TEST_FLAG(object->object.damage_flags, _object_dead_bit) && object->object.type == _object_type_biped)
				continue;
			if (iterator.index == g_pod_spot_ignored)
				continue;
			/* (a building of scenery's sphere is all about it: its walls are
			what the rays above find) */
			if (object->object.type == _object_type_scenery && object->object.bounding_sphere_radius > 4.0f)
				continue;
			if (dx * dx + dy * dy < reach * reach &&
				object->object.bounding_sphere_center.z - object->object.bounding_sphere_radius < ground.z + 3.0f &&
				object->object.bounding_sphere_center.z + object->object.bounding_sphere_radius > ground.z - 0.5f)
			{
				if (g_pod_spot_blocker == NONE)
					g_pod_spot_blocker = object->definition_index;
				return POD_SPOT_REFUSED(_pod_spot_crowded);
			}
		}
	}
	for (side = 0; side < other_count; side++)
	{
		if (distance3d(&others[side], &ground) < g_pod_apart)
			return POD_SPOT_REFUSED(_pod_spot_too_near);
	}
	if (away_from_enemies && !point_is_safe_from_enemy_zones(&ground, 20.0f))
		return POD_SPOT_REFUSED(_pod_spot_enemies);
	*candidate = ground;
	return TRUE;
}

/* a pod's landing spot near the player: before them first, a little way
off, then round them and further; open sky and clear of the enemies first,
then a lower ceiling, then nearer the enemies. Round a drop area
(`at_area`): on it first, then out from it. FALSE if nowhere passes */
static boolean ordnance_pod_spot(
	real_point3d const *anchor,
	long anchor_unit,
	boolean at_area,
	real_point3d const *others,
	short other_count,
	real_point3d *out_position)
{
	static real const player_distances[] = { 6.0f, 7.5f, 5.0f, 9.0f, 4.0f, 11.0f, 13.5f };
	static real const area_distances[] = { 0.0f, 1.9f, 3.6f, 5.3f, 7.0f, 8.7f, 10.5f };
	real const *distances = at_area ? area_distances : player_distances;
	static real const turns[] = {
		0.0f, 0.5235988f, -0.5235988f, 1.0471976f, -1.0471976f, 1.5707963f, -1.5707963f,
		2.0943951f, -2.0943951f, 2.6179939f, -2.6179939f, 3.1415927f
	};
	static real const skies[] = { 12.0f, 4.0f, 4.0f };
	struct unit_datum *unit = anchor_unit != NONE ?
		(struct unit_datum *)object_try_and_get_and_verify_type(anchor_unit, _object_mask_unit) : NULL;
	real facing = 0.0f;
	real_point3d eye = *anchor;
	short pass;

	if (unit && (unit->object.forward.i != 0.0f || unit->object.forward.j != 0.0f))
		facing = (real)atan2(unit->object.forward.j, unit->object.forward.i);
	eye.z += 0.6f;
	for (pass = 0; pass < (short)NUMBEROF(skies); pass++)
	{
		short d, t;

		for (d = 0; d < (short)NUMBEROF(player_distances); d++)
		{
			for (t = 0; t < (short)NUMBEROF(turns); t++)
			{
				real_point3d candidate = *anchor;

				candidate.x += (real)cos(facing + turns[t]) * distances[d];
				candidate.y += (real)sin(facing + turns[t]) * distances[d];
				if (pod_spot_passes(&eye, &candidate, skies[pass], pass < 2, others, other_count))
				{
					*out_position = candidate;
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}

static void pod_contents_add(long tag_index)
{
	if (tag_index == NONE || g_pod_content_count >= FF_POD_MAX_CONTENTS)
		return;
	g_pod_contents[g_pod_content_count++] = tag_index;
}

/* a weapon's magazines full, reserve and loaded both */
static void pod_weapon_fill(long weapon_index)
{
	struct weapon_datum *weapon = (struct weapon_datum *)object_try_and_get_and_verify_type(
		weapon_index, _object_mask_weapon);
	struct weapon_definition *definition = weapon ? weapon_definition_get(weapon->definition_index) : NULL;
	short m;

	for (m = 0; definition && m < definition->weapon.magazines.count && m < 2; m++)
	{
		struct weapon_magazine_definition *magazine = TAG_BLOCK_GET_ELEMENT(
			&definition->weapon.magazines, m, struct weapon_magazine_definition);

		if (magazine->rounds_total_maximum > 0)
			weapon->weapon.magazines[m].rounds_total = magazine->rounds_total_maximum;
		if (magazine->rounds_loaded_maximum > 0)
			weapon->weapon.magazines[m].rounds_loaded = magazine->rounds_loaded_maximum;
	}
}

static real pod_yaw(short pod);

/* where a pod's weapon is: in the capsule, standing in its open side; or
against the crate's side that way (a ray in from outside meets its scenery,
and the weapon stands just off it, upright, its back to the pod); without
the pod, so far from where it landed */
static void pod_slot_place(short pod, real_point3d *position, real_vector3d *forward, real_vector3d *up)
{
	real_point3d const *center = &ff_state.ordnance_pod_positions[pod];
	real angle = pod_yaw(pod);
	real_vector3d outward;
	real distance = g_pod_capsule ? FF_POD_CAPSULE_WEAPON_OUT : 1.2f;
	real height = g_pod_capsule ? FF_POD_CAPSULE_WEAPON_HEIGHT : 0.55f;

	outward.i = (real)cos(angle);
	outward.j = (real)sin(angle);
	outward.k = 0.0f;
	if (!g_pod_capsule && g_pod_objects[pod] != NONE && object_try_and_get(g_pod_objects[pod]))
	{
		struct collision_result collision;
		real_point3d start = *center;
		real_vector3d inward;

		start.x += outward.i * 4.0f;
		start.y += outward.j * 4.0f;
		start.z += height;
		inward.i = -outward.i * 4.0f;
		inward.j = -outward.j * 4.0f;
		inward.k = 0.0f;
		if (collision_test_vector(FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
				FLAG(_collision_test_front_facing_surfaces_bit), &start, &inward, NONE, &collision) &&
			collision.object_index == g_pod_objects[pod])
		{
			distance = MAX(0.2f, 4.0f * (1.0f - collision.t)) + 0.14f;
		}
	}
	*position = *center;
	position->x += outward.i * distance;
	position->y += outward.j * distance;
	position->z += height;
	*forward = *global_up3d;
	*up = outward;
}

/* the pods' weapons, one in each (in order), full, and held there
(pod_weapons_hold) */
static void pod_contents_spawn(void)
{
	short pods = (short)PIN(ff_state.ordnance_pod_count, 1, FIREFIGHT_MAX_ORDNANCE_PODS);
	short i;

	for (i = 0; i < g_pod_content_count && i < pods; i++)
	{
		struct object_placement_data placement;
		long object_index;

		object_placement_data_new(&placement, g_pod_contents[i], NONE);
		pod_slot_place(i, &placement.position, &placement.forward, &placement.up);
		object_index = object_new(&placement);
		if (object_index == NONE)
			continue;
		SET_FLAG(object_get(object_index)->object.flags, _object_cannot_be_garbage_bit, TRUE);
		pod_weapon_fill(object_index);
		if (g_pod_held_count < FF_POD_MAX_CONTENTS)
		{
			g_pod_held[g_pod_held_count].object_index = object_index;
			g_pod_held[g_pod_held_count].position = placement.position;
			g_pod_held[g_pod_held_count].forward = placement.forward;
			g_pod_held[g_pod_held_count++].up = placement.up;
		}
	}
	g_pod_content_count = 0;
}

/* (the host's, each tick the pods stand) each weapon kept at its place on its
pod, still: let go once a player has it, or something knocked it away */
static void pod_weapons_hold(boolean release)
{
	short i;

	for (i = 0; i < g_pod_held_count; i++)
	{
		long object_index = g_pod_held[i].object_index;
		struct object_datum *object = object_index != NONE ?
			object_try_and_get_and_verify_type(object_index, _object_mask_weapon) : NULL;

		if (!object || object->object.parent_object_index != NONE || release ||
			distance3d(&object->object.position, &g_pod_held[i].position) > 0.6f)
		{
			g_pod_held[i].object_index = NONE;
			continue;
		}
		object->object.translational_velocity = *global_zero_vector3d;
		object->object.angular_velocity = *global_zero_vector3d;
		object_set_position(object_index, &g_pod_held[i].position, &g_pod_held[i].forward, &g_pod_held[i].up);
	}
	if (release)
		g_pod_held_count = 0;
}

static void pod_effect(char const *effect_name, real_point3d const *position)
{
	long effect_tag = tag_loaded(EFFECT_DEFINITION_TAG, effect_name);
	char const *marker_names[1] = { "" };
	real_point3d marker_points[1];
	real_vector3d marker_forwards[1];

	if (effect_tag == NONE || !position)
		return;
	marker_points[0] = *position;
	marker_points[0].z += 0.2f;
	marker_forwards[0] = *global_up3d;
	skulls_effect_harmless = TRUE;
	effect_new_unattached_from_markers(effect_tag, NONE, NULL, 1, marker_names, marker_points, marker_forwards,
		0.0f, 0.0f, NULL, NULL, TRUE);
	skulls_effect_harmless = FALSE;
}

static void pods_delete_shown(void)
{
	short pod;

	for (pod = 0; pod < FIREFIGHT_MAX_ORDNANCE_PODS; pod++)
	{
		if (g_pod_objects[pod] != NONE && object_try_and_get_and_verify_type(g_pod_objects[pod], _object_mask_scenery))
			object_delete(g_pod_objects[pod]);
		g_pod_objects[pod] = NONE;
	}
}

/* the way a pod is turned, as on every machine */
static real pod_yaw(short pod)
{
	return (real)(((unsigned long)ff_state.ordnance_pod_drop_time + (unsigned long)pod * 977UL) * 2654435761UL %
		6283UL) / 1000.0f;
}

/* where a pod is, so many ticks after it was let go: falling faster and
faster from its height to the ground, turned as on every machine. The
capsule stands on its base (its model's x down), its open side the way it
is turned (its model's z); the crate lies as it is made */
static void pod_position_at(short pod, long elapsed, real_point3d *position, real_vector3d *forward, real_vector3d *up)
{
	real t = PIN((real)elapsed / (real)FF_POD_FALL_TICKS, 0.0f, 1.0f);
	real yaw = pod_yaw(pod);

	*position = ff_state.ordnance_pod_positions[pod];
	position->z += g_pod_fall_heights[pod] * (1.0f - t * t);
	if (g_pod_capsule)
	{
		position->z += FF_POD_CAPSULE_BASE;
		forward->i = 0.0f;
		forward->j = 0.0f;
		forward->k = -1.0f;
		vector3d_from_angle(up, yaw);
	}
	else
	{
		vector3d_from_angle(forward, yaw);
		*up = *global_up3d;
	}
}

void firefight_ordnance_pod_reset(void)
{
	short pod;

	ff_state.ordnance_pod_drop_time = NONE;
	ff_state.ordnance_pod_count = 0;
	for (pod = 0; pod < FIREFIGHT_MAX_ORDNANCE_PODS; pod++)
		g_pod_objects[pod] = NONE;
	g_pod_shown_time = NONE;
	g_pod_content_count = 0;
	g_pod_held_count = 0;
	g_pod_landed = FALSE;
	/* (a game's start: the level's objects are made again) */
	g_drop_area_count = 0;
	g_drop_areas_found = FALSE;
}

void firefight_ordnance_pod_update(void)
{
	long drop_time = ff_state.ordnance_pod_drop_time;
	short pods = (short)PIN(ff_state.ordnance_pod_count, 1, FIREFIGHT_MAX_ORDNANCE_PODS);
	long elapsed;
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	short pod;

	/* (the drop areas' markers go as the level plays) */
	drop_areas_find();
	/* (0: Firefight's state as it was cleared, no drop) */
	if (drop_time == NONE || drop_time <= 0)
	{
		pods_delete_shown();
		g_pod_shown_time = NONE;
		if (!network_game_distributed_client())
			pod_weapons_hold(TRUE);
		return;
	}
	elapsed = MAX(0, game_time_get() - drop_time);

	/* a new drop: each pod let go from as high as the sky over it lets it */
	if (g_pod_shown_time != drop_time)
	{
		long pod_tag = pod_tag_get(&g_pod_capsule);

		pods_delete_shown();
		g_pod_shown_time = drop_time;
		g_pod_landed = FALSE;
		/* (a client's marker of them; the host set its own as it dropped them) */
		if (network_game_distributed_client() && elapsed < FF_POD_FALL_TICKS)
			ff_state.ordnance_navpoint_ticks = 20 * TICKS_PER_SECOND;
		if (pod_tag == NONE)
			error(_error_silent, "firefight: no %s for the ordnance pods: their weapons land without them", FF_POD_TAG);
		for (pod = 0; pod < pods; pod++)
		{
			struct collision_result collision;
			real_point3d ray_start = ff_state.ordnance_pod_positions[pod];
			real_vector3d ray_up = { 0.0f, 0.0f, FF_POD_FALL_HEIGHT };

			g_pod_fall_heights[pod] = FF_POD_FALL_HEIGHT;
			ray_start.z += 1.0f;
			if (collision_test_vector(FLAG(_collision_test_structure_bit), &ray_start, &ray_up, NONE, &collision))
				g_pod_fall_heights[pod] = MAX(3.0f, collision.t * FF_POD_FALL_HEIGHT - 1.5f);
			if (elapsed < FF_POD_FALL_TICKS)
				pod_effect("effects\\coop teleport", &ff_state.ordnance_pod_positions[pod]);
			if (pod_tag != NONE)
			{
				struct object_placement_data placement;

				object_placement_data_new(&placement, pod_tag, NONE);
				pod_position_at(pod, elapsed, &placement.position, &placement.forward, &placement.up);
				g_pod_objects[pod] = object_new(&placement);
				if (g_pod_objects[pod] != NONE)
				{
					SET_FLAG(object_get(g_pod_objects[pod])->object.flags, _object_cannot_be_garbage_bit, TRUE);
					object_set_automatic_deactivation(g_pod_objects[pod], FALSE);
				}
			}
		}
	}

	if (elapsed < FF_POD_FALL_TICKS)
	{
		for (pod = 0; pod < pods; pod++)
		{
			if (g_pod_objects[pod] == NONE)
				continue;
			pod_position_at(pod, elapsed, &position, &forward, &up);
			object_set_position(g_pod_objects[pod], &position, &forward, &up);
		}
	}
	else if (!g_pod_landed)
	{
		g_pod_landed = TRUE;
		for (pod = 0; pod < pods; pod++)
		{
			if (g_pod_objects[pod] != NONE)
			{
				pod_position_at(pod, FF_POD_FALL_TICKS, &position, &forward, &up);
				object_set_position(g_pod_objects[pod], &position, &forward, &up);
			}
			/* (only a late joiner or a replay sought past it misses the blast) */
			if (elapsed < FF_POD_FALL_TICKS + TICKS_PER_SECOND)
			{
				pod_effect("weapons\\frag grenade\\effects\\explosion", &ff_state.ordnance_pod_positions[pod]);
				pod_effect("effects\\coop teleport", &ff_state.ordnance_pod_positions[pod]);
			}
		}
		if (!network_game_distributed_client())
			pod_contents_spawn();
	}
	else if (!network_game_distributed_client())
	{
		pod_weapons_hold(FALSE);
	}

	if (elapsed >= FF_POD_FALL_TICKS + FF_POD_STAY_TICKS)
	{
		pods_delete_shown();
		if (!network_game_distributed_client())
		{
			/* (what's left on them falls to the ground with them gone) */
			pod_weapons_hold(TRUE);
			ff_state.ordnance_pod_drop_time = NONE;
		}
	}
}

/* ---------- The Ordnance Pods' Test Drop (Forge's drop area: TEST DROP)

So many pods let go round a drop area, as a drop would be, on this machine
alone and without weapons: each falls as a drop's does, lands with its blast,
and stands a while. Its pods are scenery of this machine's, as a drop's are. */

enum
{
	FF_TEST_DROP_MAX_PODS = 8,
	FF_TEST_DROP_STAY_TICKS = 12 * TICKS_PER_SECOND
};

static struct
{
	long drop_time;
	short count;
	boolean landed;
	boolean capsule;
	long objects[FF_TEST_DROP_MAX_PODS];
	real_point3d positions[FF_TEST_DROP_MAX_PODS];
	real heights[FF_TEST_DROP_MAX_PODS];
	real yaws[FF_TEST_DROP_MAX_PODS];
} g_test_drop = { NONE };

static void test_drop_pod_place(short pod, long elapsed, real_point3d *position, real_vector3d *forward,
	real_vector3d *up)
{
	real t = PIN((real)elapsed / (real)FF_POD_FALL_TICKS, 0.0f, 1.0f);

	*position = g_test_drop.positions[pod];
	position->z += g_test_drop.heights[pod] * (1.0f - t * t);
	if (g_test_drop.capsule)
	{
		position->z += FF_POD_CAPSULE_BASE;
		forward->i = 0.0f;
		forward->j = 0.0f;
		forward->k = -1.0f;
		vector3d_from_angle(up, g_test_drop.yaws[pod]);
	}
	else
	{
		vector3d_from_angle(forward, g_test_drop.yaws[pod]);
		*up = *global_up3d;
	}
}

void firefight_ordnance_preview_refusals(char *text, long size)
{
	static char const *const names[NUMBER_OF_POD_SPOT_REFUSALS] = {
		"no ground", "object under", "slope", "outside map", "unseen", "cramped", "roofed", "crowded", "too near",
		"enemies"
	};
	short reason;

	if (!text || size <= 0)
		return;
	text[0] = 0;
	for (reason = 0; reason < NUMBER_OF_POD_SPOT_REFUSALS; reason++)
	{
		if (g_pod_spot_refusals[reason])
		{
			snprintf(text + strlen(text), (size_t)size - strlen(text), "%s%s %ld", text[0] ? ", " : "", names[reason],
				g_pod_spot_refusals[reason]);
		}
	}
	if (g_pod_spot_blocker != NONE)
	{
		char const *name = tag_get_name(g_pod_spot_blocker);

		snprintf(text + strlen(text), (size_t)size - strlen(text), " (first object: %s)", name ? name : "?");
	}
	if (!text[0])
		snprintf(text, (size_t)size, "none");
}

void firefight_ordnance_preview_stop(void)
{
	short pod;

	for (pod = 0; pod < g_test_drop.count; pod++)
	{
		if (g_test_drop.objects[pod] != NONE &&
			object_try_and_get_and_verify_type(g_test_drop.objects[pod], _object_mask_scenery))
			object_delete(g_test_drop.objects[pod]);
		g_test_drop.objects[pod] = NONE;
	}
	g_test_drop.count = 0;
	g_test_drop.drop_time = NONE;
}

short firefight_ordnance_preview_start(
	real_point3d const *area,
	long area_object,
	short count)
{
	long pod_tag;
	short pod;

	firefight_ordnance_preview_stop();
	csmemset(g_pod_spot_refusals, 0, sizeof(g_pod_spot_refusals));
	g_pod_spot_blocker = NONE;
	pod_tag = pod_tag_get(&g_test_drop.capsule);
	if (pod_tag == NONE || !area)
	{
		error(_error_silent, "firefight: test drop: no pod tag (%s nor %s) is loaded", FF_POD_TAG, FF_POD_FALLBACK_TAG);
		return 0;
	}
	pods_shape_choose();
	count = (short)PIN(count, 1, FF_TEST_DROP_MAX_PODS);
	for (pod = 0; pod < count; pod++)
	{
		struct collision_result collision;
		real_point3d ray_start;
		real_vector3d ray_up = { 0.0f, 0.0f, FF_POD_FALL_HEIGHT };
		real_point3d *spot = &g_test_drop.positions[g_test_drop.count];

		boolean found;

		g_pod_spot_ignored = area_object;
		found = ordnance_pod_spot(area, NONE, TRUE, g_test_drop.positions, g_test_drop.count, spot);
		g_pod_spot_ignored = NONE;
		if (!found)
			break;
		g_test_drop.heights[g_test_drop.count] = FF_POD_FALL_HEIGHT;
		ray_start = *spot;
		ray_start.z += 1.0f;
		if (collision_test_vector(FLAG(_collision_test_structure_bit), &ray_start, &ray_up, NONE, &collision))
			g_test_drop.heights[g_test_drop.count] = MAX(3.0f, collision.t * FF_POD_FALL_HEIGHT - 1.5f);
		g_test_drop.yaws[g_test_drop.count] = (real)((unsigned long)(game_time_get() + pod * 977) * 2654435761UL %
			6283UL) / 1000.0f;
		g_test_drop.objects[g_test_drop.count] = NONE;
		g_test_drop.count++;
	}
	{
		char reasons[256];

		firefight_ordnance_preview_refusals(reasons, sizeof(reasons));
		error(_error_silent, "firefight: test drop at (%.2f, %.2f, %.2f): %d of %d pods (%s) found room; refused: %s",
			area->x, area->y, area->z, g_test_drop.count, count, g_test_drop.capsule ? "capsule" : "crate", reasons);
	}
	g_test_drop.drop_time = game_time_get();
	g_test_drop.landed = FALSE;
	for (pod = 0; pod < g_test_drop.count; pod++)
	{
		struct object_placement_data placement;

		object_placement_data_new(&placement, pod_tag, NONE);
		test_drop_pod_place(pod, 0, &placement.position, &placement.forward, &placement.up);
		g_test_drop.objects[pod] = object_new(&placement);
		if (g_test_drop.objects[pod] != NONE)
		{
			SET_FLAG(object_get(g_test_drop.objects[pod])->object.flags, _object_cannot_be_garbage_bit, TRUE);
			object_set_automatic_deactivation(g_test_drop.objects[pod], FALSE);
		}
		pod_effect("effects\\coop teleport", &g_test_drop.positions[pod]);
	}
	if (!g_test_drop.count)
		g_test_drop.drop_time = NONE;
	return g_test_drop.count;
}

short firefight_ordnance_preview_update(void)
{
	long elapsed;
	short pod;

	if (g_test_drop.drop_time == NONE)
		return 0;
	elapsed = game_time_get() - g_test_drop.drop_time;
	if (elapsed < 0 || elapsed >= FF_POD_FALL_TICKS + FF_TEST_DROP_STAY_TICKS)
	{
		firefight_ordnance_preview_stop();
		return 0;
	}
	for (pod = 0; pod < g_test_drop.count; pod++)
	{
		real_point3d position;
		real_vector3d forward;
		real_vector3d up;

		if (g_test_drop.objects[pod] == NONE ||
			!object_try_and_get_and_verify_type(g_test_drop.objects[pod], _object_mask_scenery))
			continue;
		if (elapsed > FF_POD_FALL_TICKS && g_test_drop.landed)
			continue;
		test_drop_pod_place(pod, elapsed, &position, &forward, &up);
		object_set_position(g_test_drop.objects[pod], &position, &forward, &up);
	}
	if (elapsed >= FF_POD_FALL_TICKS && !g_test_drop.landed)
	{
		g_test_drop.landed = TRUE;
		for (pod = 0; pod < g_test_drop.count; pod++)
		{
			pod_effect("weapons\\frag grenade\\effects\\explosion", &g_test_drop.positions[pod]);
			pod_effect("effects\\coop teleport", &g_test_drop.positions[pod]);
		}
	}
	return elapsed < FF_POD_FALL_TICKS ? 1 : 2;
}

/* ---------- Friendly AI Reinforcements */

void firefight_spawn_friendly_reinforcements(short count)
{
	real_point3d player_positions[16];
	short player_count = get_living_player_positions(player_positions, NULL, 16);
	short living_friendlies = 0;
	long candidate_variants[MAX_ROSTER_VARIANTS];
	short candidate_count = 0;
	short i;

	if (count <= 0 || player_count == 0)
		return;

	for (i = 0; i < g_friendly_unit_count; i++)
	{
		if (g_friendly_units[i] != NONE)
		{
			struct unit_datum *u = (struct unit_datum *)object_try_and_get_and_verify_type(
				g_friendly_units[i], _object_mask_unit);
			if (u && !TEST_FLAG(u->object.damage_flags, _object_dead_bit))
				living_friendlies++;
			else
				g_friendly_units[i] = NONE;
		}
	}

	if (living_friendlies >= count)
		return;

	for (i = 0; i < g_roster_count; i++)
	{
		if (ff_state.elite_player_mode)
		{
			if (!g_roster[i].is_human_ally_or_enemy &&
				g_roster[i].faction == _firefight_faction_covenant)
			{
				candidate_variants[candidate_count++] = g_roster[i].variant_tag_index;
			}
		}
		else
		{
			if (g_roster[i].is_human_ally_or_enemy)
				candidate_variants[candidate_count++] = g_roster[i].variant_tag_index;
		}
	}

	if (candidate_count == 0)
		return;

	for (i = living_friendlies; i < count; i++)
	{
		long variant = candidate_variants[spawner_rand() % (unsigned long)candidate_count];
		real_point3d spawn_pt = player_positions[0];
		short ally_team = ff_state.elite_player_mode ? _game_team_covenant : _game_team_human;
		long unit_index;

		spawn_pt.x += spawner_rand_range(-3.5f, 3.5f);
		spawn_pt.y += spawner_rand_range(-3.5f, 3.5f);
		unit_index = spawn_single_actor_at(variant, _ff_tier_infantry, &spawn_pt, 0.0f, ally_team, FALSE);
		if (unit_index != NONE && g_friendly_unit_count < MAX_FRIENDLY_UNITS)
		{
			g_friendly_units[g_friendly_unit_count++] = unit_index;
		}
	}
}

/* ---------- Round-Scheduled Player Vehicle Spawning & End-of-Round Despawning (Feature 7) */

static long resolve_late_vehicle_tag(byte late_type);

static long resolve_early_vehicle_tag(byte early_type)
{
	static char const *const warthog_tags[] = {
		"vehicles\\warthog\\mp_warthog",
		"vehicles\\warthog\\warthog",
		"vehicles\\rwarthog\\rwarthog"
	};
	static char const *const rwarthog_tags[] = {
		"vehicles\\rwarthog\\rwarthog",
		"vehicles\\warthog\\mp_warthog",
		"vehicles\\warthog\\warthog"
	};
	static char const *const ghost_tags[] = { "vehicles\\ghost\\ghost_mp", "vehicles\\ghost\\ghost" };

	switch (early_type)
	{
	case _firefight_early_vehicle_warthog:        return find_loaded_vehicle_tag(warthog_tags, 3);
	case _firefight_early_vehicle_ghost:          return find_loaded_vehicle_tag(ghost_tags, 2);
	case _firefight_early_vehicle_rocket_warthog: return find_loaded_vehicle_tag(rwarthog_tags, 3);
	case _firefight_early_vehicle_scorpion:       return resolve_late_vehicle_tag(_firefight_late_vehicle_scorpion);
	case _firefight_early_vehicle_banshee:        return resolve_late_vehicle_tag(_firefight_late_vehicle_banshee);
	case _firefight_early_vehicle_wraith:         return resolve_late_vehicle_tag(_firefight_late_vehicle_wraith);
	default:                                      return NONE;
	}
}

static long resolve_late_vehicle_tag(byte late_type)
{
	static char const *const scorpion_tags[] = {
		"vehicles\\scorpion\\scorpion_mp",
		"vehicles\\scorpion\\scorpion",
		"vehicles\\rwarthog\\rwarthog",
		"vehicles\\warthog\\mp_warthog",
		"vehicles\\warthog\\warthog"
	};
	static char const *const wraith_tags[] = { "vehicles\\wraith\\wraith" };
	static char const *const banshee_tags[] = { "vehicles\\banshee\\banshee_mp", "vehicles\\banshee\\banshee" };
	static char const *const warthog_tags[] = { "vehicles\\warthog\\mp_warthog", "vehicles\\warthog\\warthog" };

	switch (late_type)
	{
	case _firefight_late_vehicle_scorpion: return find_loaded_vehicle_tag(scorpion_tags, 5);
	case _firefight_late_vehicle_wraith:   return find_loaded_vehicle_tag(wraith_tags, 1);
	case _firefight_late_vehicle_banshee:  return find_loaded_vehicle_tag(banshee_tags, 2);
	case _firefight_late_vehicle_warthog:  return find_loaded_vehicle_tag(warthog_tags, 2);
	default:                               return NONE;
	}
}

/* a vehicle placement (custom_edition_objects.c has the fields past the object) */
struct ff_scenario_vehicle
{
	struct scenario_object_datum object;
	byte unused[0x50];
};

typedef char ff_scenario_vehicle_size_assert[
	sizeof(struct ff_scenario_vehicle) == 0x78 ? 1 : -1];

/* a placement players could use: not a campaign level's flyers, scripted
props and turrets, whose spots are in the air or on a wall */
static boolean map_vehicle_tag_drivable(char const *name)
{
	static char const *const unusable[] = {
		"pelican", "dropship", "lifepod", "fighterbomber", "turret", "gun tower",
		"falling_box", "cinematic", "playback", "cryotube", "chair"
	};
	short i;

	if (!name)
		return FALSE;
	for (i = 0; i < (short)NUMBEROF(unusable); i++)
	{
		if (strstr(name, unusable[i]))
			return FALSE;
	}
	return TRUE;
}

static boolean map_vehicle_spot_clear(real_point3d const *position)
{
	struct object_iterator iterator;
	struct object_datum *object;

	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		if (distance3d(&object->object.position, position) < 4.0f)
			return FALSE;
	}
	return TRUE;
}

/* the free spot of the map's own vehicle placements nearest the players, one
of this vehicle's if it has any. Forge's placed vehicles are among them, and a
close quarters map has none: FALSE, and it goes without */
static boolean map_vehicle_spot(long vehicle_tag, real_point3d *out_position, real *out_facing)
{
	struct scenario *scen = global_scenario_try_and_get();
	real_point3d player_positions[16];
	short player_count = get_living_human_player_positions(player_positions, NULL, 16);
	real_point3d anchor;
	short pass;

	if (!scen)
		return FALSE;
	if (player_count > 0)
	{
		anchor = player_positions[central_player_index(player_positions, player_count)];
	}
	else
	{
		real facing = 0.0f;

		if (!firefight_select_player_spawn_point(0, &anchor, &facing))
			return FALSE;
	}

	for (pass = 0; pass < 2; pass++)
	{
		real best_distance = 0.0f;
		boolean found = FALSE;
		short i;

		for (i = 0; i < scen->vehicles.count; i++)
		{
			struct ff_scenario_vehicle *placement = TAG_BLOCK_GET_ELEMENT(
				&scen->vehicles, i, struct ff_scenario_vehicle);
			short palette_index = placement->object.palette_entry_index;
			struct scenario_object_palette_entry *palette;
			real distance;

			if (!VALID_INDEX(palette_index, scen->vehicle_palette.count))
				continue;
			palette = TAG_BLOCK_GET_ELEMENT(
				&scen->vehicle_palette, palette_index, struct scenario_object_palette_entry);
			if (palette->reference.index == NONE ||
				(pass == 0 && palette->reference.index != vehicle_tag) ||
				!map_vehicle_tag_drivable(tag_get_name(palette->reference.index)) ||
				!point_is_in_valid_bsp(&placement->object.position) ||
				!map_vehicle_spot_clear(&placement->object.position))
			{
				continue;
			}
			distance = distance3d(&placement->object.position, &anchor);
			if (!found || distance < best_distance)
			{
				found = TRUE;
				best_distance = distance;
				*out_position = placement->object.position;
				*out_facing = placement->object.rotation.yaw;
			}
		}
		if (found)
			return TRUE;
	}
	return FALSE;
}

/* ---------- MAP CONTENT: the map's own enemies, weapons and vehicles */

/* a tier for one of the map's AI the roster has no place for, by its words */
static byte map_enemy_tier(char const *variant_name, char const *unit_name)
{
	static char const *const boss_words[] = { "hunter", "zealot", "boss", "ultra", "chieftain", "wraith", NULL };
	static char const *const commander_words[] = {
		"elite", "major", "commander", "carrier", "odst", "brute", "captain", "honor", "spec", NULL
	};
	static char const *const fodder_words[] = { "grunt", "infection", "crew", "drone", "bugger", "pup", NULL };
	char lowered[512];

	lowercase_copy(variant_name ? variant_name : "", lowered, 256);
	if (unit_name)
	{
		size_t used = strlen(lowered);

		lowered[used++] = ' ';
		lowercase_copy(unit_name, lowered + used, sizeof(lowered) - used);
	}
	if (name_has_word(lowered, boss_words))
		return _ff_tier_boss_heavy;
	if (name_has_word(lowered, commander_words))
		return _ff_tier_commander;
	if (name_has_word(lowered, fodder_words))
		return _ff_tier_fodder;
	return _ff_tier_infantry;
}

/* whether a weapon is a power weapon for a heavy drop, by its words */
static boolean map_weapon_power(char const *lowered)
{
	static char const *const power_words[] = {
		"rocket", "fuel", "sword", "shotgun", "launcher", "cannon", "hammer", "flame", "laser", "sniper",
		"mortar", "splaser", "railgun", "spartan", NULL
	};

	return name_has_word(lowered, power_words);
}

/* whether a vehicle is one of the game's own, as stock Halo has them */
static boolean vehicle_tag_stock(char const *lowered)
{
	static char const *const stock_prefixes[] = {
		"vehicles\\warthog\\", "vehicles\\rwarthog\\", "vehicles\\scorpion\\", "vehicles\\ghost\\",
		"vehicles\\banshee\\", "vehicles\\wraith\\", "vehicles\\pelican\\", "vehicles\\c_dropship\\",
		"vehicles\\c gun turret\\", "vehicles\\c_gun_turret\\", "vehicles\\lifepod", NULL
	};
	short i;

	for (i = 0; stock_prefixes[i]; i++)
	{
		if (!strncmp(lowered, stock_prefixes[i], strlen(stock_prefixes[i])))
			return TRUE;
	}
	return FALSE;
}

/* whether a weapon is one of the game's own: the campaign's (the
randomizer's pack has it) or stock multiplayer's */
static boolean weapon_tag_stock(char const *name, char const *lowered)
{
	return randomizer_stock_tag_exists(WEAPON_DEFINITION_TAG, name) ||
		(!strncmp(lowered, "weapons\\", 8) && (strstr(lowered, "mp_") || strstr(lowered, "weapons\\pistol\\") ||
			strstr(lowered, "weapons\\assault rifle\\") || strstr(lowered, "weapons\\shotgun\\") ||
			strstr(lowered, "weapons\\sniper rifle\\") || strstr(lowered, "weapons\\rocket launcher\\") ||
			strstr(lowered, "weapons\\plasma") || strstr(lowered, "weapons\\needler\\") ||
			strstr(lowered, "weapons\\flamethrower\\") || strstr(lowered, "weapons\\fuel rod") ||
			strstr(lowered, "weapons\\frag grenade\\") || strstr(lowered, "weapons\\energy sword\\") ||
			strstr(lowered, "weapons\\ball\\") || strstr(lowered, "weapons\\flag\\") ||
			strstr(lowered, "weapons\\gravity rifle\\")));
}

/* (by its name: a weapon players can carry, not a vehicle's, a flag or
ball, nor one only the campaign's AI or scripts hold) */
static boolean map_weapon_name_usable(char const *lowered)
{
	return !(strstr(lowered, "vehicles\\") || strstr(lowered, "flag") || strstr(lowered, "ball") ||
		strstr(lowered, "gravity") || strstr(lowered, "sentinel") || strstr(lowered, "gun turret") ||
		strstr(lowered, "cinematic") || strstr(lowered, "turret") || strstr(lowered, "energy sword\\energy sword") ||
		strstr(lowered, "needler\\needler"));
}

static boolean map_weapon_usable(long weapon_tag, char const *lowered)
{
	struct weapon_definition *definition = weapon_definition_get(weapon_tag);

	return definition && !((definition->weapon.flags >> 3) & 1) && map_weapon_name_usable(lowered);
}

/* (by its name: an actor variant that fights, not the cinematics' nor the
players' allies) */
static boolean map_enemy_name_fights(char const *lowered)
{
	return !(strstr(lowered, "cinematic") || strstr(lowered, "cortana") || strstr(lowered, "keyes") ||
		strstr(lowered, "monitor") || strstr(lowered, "captain") || strstr(lowered, "marine") ||
		strstr(lowered, "crewman") || strstr(lowered, "pilot"));
}

static boolean map_content_custom_off(byte custom_bit, short kind, long tag_index);

static void scan_map_content(void)
{
	byte content = firefight_map_content();
	byte enemies = map_content_of(FIREFIGHT_MAP_CONTENT_ENEMIES_OFF);
	struct tag_iterator iter;
	long tag_index;
	short native_roster = 0;
	short i;

	ff_map_enemy_count = 0;
	ff_map_weapon_count = 0;
	ff_map_vehicle_count = 0;
	g_roster_map_only = FALSE;
	if (content == _firefight_map_content_vanilla)
		return;

	/* the map's enemies: its own of the roster's kinds are its entries; the
	rest here, if they fight (not the cinematics', nor allies') */
	for (i = 0; i < g_roster_count; i++)
	{
		g_roster[i].custom_off = g_roster[i].maps_own && enemies != _firefight_map_content_vanilla &&
			map_content_custom_off(FIREFIGHT_MAP_CONTENT_ENEMIES_CUSTOM, _firefight_map_content_kind_enemies,
				g_roster[i].variant_tag_index);
		if (g_roster[i].maps_own && !g_roster[i].custom_off)
			native_roster++;
	}
	tag_iterator_new(&iter, ACTOR_VARIANT_DEFINITION_TAG);
	while (enemies != _firefight_map_content_vanilla &&
		(tag_index = tag_iterator_next(&iter)) != NONE && ff_map_enemy_count < MAX_ROSTER_VARIANTS)
	{
		struct actor_variant_definition *variant = actor_variant_definition_get(tag_index);
		struct unit_definition *unit;
		char lowered[256];
		boolean listed = FALSE;

		if (!tag_is_maps_own(tag_index) || !variant || variant->unit_reference.index == NONE ||
			variant->actor_reference.index == NONE || !tag_index_is_group(variant->unit_reference.index, BIPED_DEFINITION_TAG))
		{
			continue;
		}
		for (i = 0; i < g_roster_count && !listed; i++)
			listed = g_roster[i].variant_tag_index == tag_index;
		lowercase_copy(tag_get_name(tag_index), lowered, sizeof(lowered));
		if (listed || !map_enemy_name_fights(lowered) ||
			map_content_custom_off(FIREFIGHT_MAP_CONTENT_ENEMIES_CUSTOM, _firefight_map_content_kind_enemies, tag_index))
		{
			continue;
		}
		unit = (struct unit_definition *)tag_get(BIPED_DEFINITION_TAG, variant->unit_reference.index);
		if (!unit || unit->unit.default_team == _game_team_player || unit->unit.default_team == _game_team_human)
			continue;
		ff_map_enemy_tags[ff_map_enemy_count] = tag_index;
		ff_map_enemy_tiers[ff_map_enemy_count++] = map_enemy_tier(tag_get_name(tag_index),
			tag_get_name(variant->unit_reference.index));
	}
	/* (INCLUDE MAP: those beside the game's; ONLY MAP: its own alone, if it
	has any to fight) */
	g_roster_map_only = enemies == _firefight_map_content_only_map && (native_roster > 0 || ff_map_enemy_count > 0);

	tag_iterator_new(&iter, WEAPON_DEFINITION_TAG);
	while (map_content_of(FIREFIGHT_MAP_CONTENT_WEAPONS_OFF) != _firefight_map_content_vanilla &&
		(tag_index = tag_iterator_next(&iter)) != NONE && ff_map_weapon_count < MAX_ROSTER_VARIANTS)
	{
		char lowered[256];

		if (!tag_is_maps_own(tag_index))
			continue;
		lowercase_copy(tag_get_name(tag_index), lowered, sizeof(lowered));
		if (!map_weapon_usable(tag_index, lowered) ||
			map_content_custom_off(FIREFIGHT_MAP_CONTENT_WEAPONS_CUSTOM, _firefight_map_content_kind_weapons, tag_index))
		{
			continue;
		}
		ff_map_weapon_tags[ff_map_weapon_count] = tag_index;
		ff_map_weapon_power[ff_map_weapon_count] = map_weapon_power(lowered);
		ff_map_weapon_stock[ff_map_weapon_count++] = weapon_tag_stock(tag_get_name(tag_index), lowered);
	}

	tag_iterator_new(&iter, VEHICLE_DEFINITION_TAG);
	while (map_content_of(FIREFIGHT_MAP_CONTENT_VEHICLES_OFF) != _firefight_map_content_vanilla &&
		(tag_index = tag_iterator_next(&iter)) != NONE && ff_map_vehicle_count < MAX_MAP_VEHICLES)
	{
		struct ff_vehicle_ai_tag_layout *vehicle = (struct ff_vehicle_ai_tag_layout *)tag_get(VEHICLE_DEFINITION_TAG, tag_index);
		char lowered[256];

		if (!tag_is_maps_own(tag_index) || !vehicle || vehicle->unit.unit.seats.count <= 0 ||
			vehicle->type == FF_VEHICLE_TYPE_TURRET)
		{
			continue;
		}
		lowercase_copy(tag_get_name(tag_index), lowered, sizeof(lowered));
		if (!map_vehicle_tag_drivable(lowered) ||
			map_content_custom_off(FIREFIGHT_MAP_CONTENT_VEHICLES_CUSTOM, _firefight_map_content_kind_vehicles, tag_index))
		{
			continue;
		}
		ff_map_vehicle_tags[ff_map_vehicle_count] = tag_index;
		ff_map_vehicle_stock[ff_map_vehicle_count++] = vehicle_tag_stock(lowered);
	}
	error(_error_silent, "firefight: MAP CONTENT %s: the map's own %d enemies (and %d of the roster's), %d weapons, "
		"%d vehicles%s", content == _firefight_map_content_only_map ? "ONLY MAP" : "INCLUDE MAP", ff_map_enemy_count,
		native_roster, ff_map_weapon_count, ff_map_vehicle_count,
		content == _firefight_map_content_only_map && !g_roster_map_only ? " (no enemies of its own: the game's)" : "");
}

/* ---- CUSTOM's lists: a map's own of each kind, by their names (read from
its file without loading it), and the host's choices of them, per map */

#define FF_MC_LIST_MAX 128
#define FF_MC_NAME_SIZE 128
#define FF_MC_SAVED_MAX 384
#define FF_MC_FILE "d:\\firefight_map_content.txt"

static char const ff_mc_kind_letters[NUMBER_OF_FIREFIGHT_MAP_CONTENT_KINDS] = { 'e', 'w', 'v' };

static struct
{
	char map[128];
	byte kinds;
	short counts[NUMBER_OF_FIREFIGHT_MAP_CONTENT_KINDS];
	char names[NUMBER_OF_FIREFIGHT_MAP_CONTENT_KINDS][FF_MC_LIST_MAX][FF_MC_NAME_SIZE];
} g_mc_list;

/* (the saved file's section for one map: its byte, the tags turned off) */
static struct
{
	char map[96];
	boolean known;
	byte content;
	short count;
	char kinds[FF_MC_SAVED_MAX];
	char names[FF_MC_SAVED_MAX][FF_MC_NAME_SIZE];
} g_mc_saved;

/* a map's name as the file keys it: no folder, lowercase, no Forge '@' part */
static void map_content_stem(char const *map_name, char *stem, size_t size)
{
	char const *slash;
	char *cut;

	map_name = map_name ? map_name : "";
	if ((slash = strrchr(map_name, '\\')) != NULL)
		map_name = slash + 1;
	if ((slash = strrchr(map_name, '/')) != NULL)
		map_name = slash + 1;
	lowercase_copy(map_name, stem, size);
	if ((cut = strchr(stem, '@')) != NULL)
		*cut = 0;
	if ((cut = strstr(stem, ".map")) != NULL && !cut[4])
		*cut = 0;
}

/* (firefight_map_content_kinds' walk of a map's tags) */
static int map_content_kinds_visit(void *context, unsigned long group_tag, char const *name)
{
	short kind = NONE;
	char lowered[256];

	(void)context;
	lowercase_copy(name, lowered, sizeof(lowered));
	if (group_tag == ACTOR_VARIANT_DEFINITION_TAG && map_enemy_name_fights(lowered) &&
		!randomizer_stock_tag_exists(ACTOR_VARIANT_DEFINITION_TAG, name))
	{
		kind = _firefight_map_content_kind_enemies;
	}
	else if (group_tag == WEAPON_DEFINITION_TAG && map_weapon_name_usable(lowered) && !weapon_tag_stock(name, lowered))
		kind = _firefight_map_content_kind_weapons;
	else if (group_tag == VEHICLE_DEFINITION_TAG && map_vehicle_tag_drivable(lowered) && !strstr(lowered, "turret") &&
		!vehicle_tag_stock(lowered))
	{
		kind = _firefight_map_content_kind_vehicles;
	}
	if (kind == NONE)
		return TRUE;
	g_mc_list.kinds |= (byte)(1 << kind);
	if (g_mc_list.counts[kind] < FF_MC_LIST_MAX)
		snprintf(g_mc_list.names[kind][g_mc_list.counts[kind]++], FF_MC_NAME_SIZE, "%s", name);
	return TRUE;
}

/* (the map's lists, read once for each map asked of) */
static void map_content_list_read(char const *map_name)
{
	char const *stem;

	if (!map_name || !map_name[0] || !strcmp(g_mc_list.map, map_name))
		return;
	memset(g_mc_list.counts, 0, sizeof(g_mc_list.counts));
	g_mc_list.kinds = 0;
	/* (the game's own maps have none of their own: only a Custom Edition
	map's file is read, its tags' names alone) */
	stem = tag_name_strip_path(map_name);
	if (custom_edition_level_name(map_name) && !is_stock_map_stem(stem))
		custom_edition_map_tag_names_walk(map_name, map_content_kinds_visit, NULL);
	snprintf(g_mc_list.map, sizeof(g_mc_list.map), "%s", map_name);
	error(_error_silent, "firefight: %s's own content: %d enemies, %d weapons, %d vehicles", map_name,
		g_mc_list.counts[0], g_mc_list.counts[1], g_mc_list.counts[2]);
}

byte firefight_map_content_kinds(char const *map_name)
{
	if (!map_name || !map_name[0])
		return 0;
	map_content_list_read(map_name);
	return g_mc_list.kinds;
}

short firefight_map_content_tag_count(char const *map_name, short kind)
{
	if (kind < 0 || kind >= NUMBER_OF_FIREFIGHT_MAP_CONTENT_KINDS || !map_name || !map_name[0])
		return 0;
	map_content_list_read(map_name);
	return g_mc_list.counts[kind];
}

char const *firefight_map_content_tag_name(char const *map_name, short kind, short index)
{
	if (index < 0 || index >= firefight_map_content_tag_count(map_name, kind))
		return NULL;
	return g_mc_list.names[kind][index];
}

/* (the saved file's section for the map, read once for each map asked of) */
static void map_content_saved_read(char const *map_name)
{
	char stem[96];
	char line[FF_MC_NAME_SIZE + 96];
	boolean ours = FALSE;
	FILE *file;

	map_content_stem(map_name, stem, sizeof(stem));
	if (!stem[0] || !strcmp(g_mc_saved.map, stem))
		return;
	snprintf(g_mc_saved.map, sizeof(g_mc_saved.map), "%s", stem);
	g_mc_saved.known = FALSE;
	g_mc_saved.content = _firefight_map_content_vanilla;
	g_mc_saved.count = 0;
	if ((file = fopen(FF_MC_FILE, "r")) == NULL)
		return;
	while (fgets(line, sizeof(line), file))
	{
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "map ", 4))
		{
			char *space = strrchr(line + 4, ' ');

			if (!space)
			{
				ours = FALSE;
				continue;
			}
			*space = 0;
			ours = !strcmp(line + 4, stem);
			if (ours)
			{
				g_mc_saved.known = TRUE;
				g_mc_saved.content = (byte)strtoul(space + 1, NULL, 10);
			}
		}
		else if (ours && line[0] && line[1] == ' ' && line[2] && g_mc_saved.count < FF_MC_SAVED_MAX)
		{
			g_mc_saved.kinds[g_mc_saved.count] = line[0];
			snprintf(g_mc_saved.names[g_mc_saved.count++], FF_MC_NAME_SIZE, "%s", line + 2);
		}
	}
	fclose(file);
}

/* (the file again: every other map's sections as they were, the map's own
after them) */
static void map_content_saved_write(void)
{
	static char rest[64 * 1024];
	size_t length = 0;
	char line[FF_MC_NAME_SIZE + 96];
	boolean ours = FALSE;
	FILE *file;
	short index;

	if (!g_mc_saved.map[0])
		return;
	if ((file = fopen(FF_MC_FILE, "r")) != NULL)
	{
		while (fgets(line, sizeof(line), file))
		{
			size_t size;

			if (!strncmp(line, "map ", 4))
			{
				char name[sizeof(line)];
				char *space;

				snprintf(name, sizeof(name), "%s", line + 4);
				name[strcspn(name, "\r\n")] = 0;
				space = strrchr(name, ' ');
				if (space)
					*space = 0;
				ours = !strcmp(name, g_mc_saved.map);
			}
			size = strlen(line);
			if (ours || length + size + 1 >= sizeof(rest))
				continue;
			memcpy(rest + length, line, size);
			length += size;
			if (size && line[size - 1] != '\n')
				rest[length++] = '\n';
		}
		fclose(file);
	}
	if ((file = fopen(FF_MC_FILE, "w")) == NULL)
	{
		error(_error_silent, "firefight: could not write %s", FF_MC_FILE);
		return;
	}
	fwrite(rest, 1, length, file);
	fprintf(file, "map %s %u\n", g_mc_saved.map, (unsigned)g_mc_saved.content);
	for (index = 0; index < g_mc_saved.count; index++)
		fprintf(file, "%c %s\n", g_mc_saved.kinds[index], g_mc_saved.names[index]);
	fclose(file);
}

boolean firefight_map_content_saved(char const *map_name, byte *content)
{
	map_content_saved_read(map_name);
	if (g_mc_saved.known && content)
		*content = g_mc_saved.content;
	return g_mc_saved.known;
}

void firefight_map_content_save(char const *map_name, byte content)
{
	map_content_saved_read(map_name);
	if (!g_mc_saved.map[0] || (g_mc_saved.known && g_mc_saved.content == content))
		return;
	g_mc_saved.known = TRUE;
	g_mc_saved.content = content;
	map_content_saved_write();
}

/* (a tag on unless the map's section turns it off, by its name) */
static short map_content_saved_find(short kind, char const *tag_name)
{
	short index;

	for (index = 0; index < g_mc_saved.count; index++)
	{
		if (g_mc_saved.kinds[index] == ff_mc_kind_letters[kind] && !strcmp(g_mc_saved.names[index], tag_name))
			return index;
	}
	return NONE;
}

boolean firefight_map_content_tag_on(char const *map_name, short kind, char const *tag_name)
{
	if (kind < 0 || kind >= NUMBER_OF_FIREFIGHT_MAP_CONTENT_KINDS || !tag_name)
		return TRUE;
	map_content_saved_read(map_name);
	return map_content_saved_find(kind, tag_name) == NONE;
}

void firefight_map_content_tag_set(char const *map_name, short kind, char const *tag_name, boolean on)
{
	short found;

	if (kind < 0 || kind >= NUMBER_OF_FIREFIGHT_MAP_CONTENT_KINDS || !tag_name || !tag_name[0])
		return;
	map_content_saved_read(map_name);
	if (!g_mc_saved.map[0])
		return;
	found = map_content_saved_find(kind, tag_name);
	if (on == (found == NONE))
		return;
	if (on)
	{
		memmove(&g_mc_saved.kinds[found], &g_mc_saved.kinds[found + 1], (size_t)(g_mc_saved.count - found - 1));
		memmove(g_mc_saved.names[found], g_mc_saved.names[found + 1], (size_t)(g_mc_saved.count - found - 1) * FF_MC_NAME_SIZE);
		g_mc_saved.count--;
	}
	else if (g_mc_saved.count < FF_MC_SAVED_MAX)
	{
		g_mc_saved.kinds[g_mc_saved.count] = ff_mc_kind_letters[kind];
		snprintf(g_mc_saved.names[g_mc_saved.count++], FF_MC_NAME_SIZE, "%s", tag_name);
	}
	map_content_saved_write();
}

/* (in the game: whether CUSTOM leaves a tag of the map's out, the host's
choices for the map played) */
static boolean map_content_custom_off(byte custom_bit, short kind, long tag_index)
{
	struct game_variant_options const *opts = game_variant_options_get();
	char const *loaded = cache_file_loaded_map_name();

	if (!opts || !(opts->firefight_map_content & custom_bit) || !loaded || !loaded[0] || tag_index == NONE)
		return FALSE;
	return !firefight_map_content_tag_on(loaded, kind, tag_get_name(tag_index));
}

/* whether MAP CONTENT has the map's weapons in place of the game's: ONLY
MAP, and the map has some */
static boolean map_weapons_only(void)
{
	return map_content_of(FIREFIGHT_MAP_CONTENT_WEAPONS_OFF) == _firefight_map_content_only_map && ff_map_weapon_count > 0;
}

static boolean map_vehicles_only(void)
{
	return map_content_of(FIREFIGHT_MAP_CONTENT_VEHICLES_OFF) == _firefight_map_content_only_map && ff_map_vehicle_count > 0;
}

/* a random loadout's weapon (Fiesta's, a custom loadout's RANDOM) of the
map's own (MAP CONTENT): ONLY MAP's always one of them; INCLUDE MAP's one of
its own beside the game's half the time; other than `other` (the loadout's
other weapon) where it can be; NULL for one of the game's */
char const *firefight_map_loadout_weapon(char const *other)
{
	byte mode = map_content_of(FIREFIGHT_MAP_CONTENT_WEAPONS_OFF);
	short candidates[MAX_ROSTER_VARIANTS];
	short count = 0;
	short index;
	boolean only = mode == _firefight_map_content_only_map;

	if (mode == _firefight_map_content_vanilla || ff_map_weapon_count <= 0)
		return NULL;
	if (!only && (spawner_rand() & 1))
		return NULL;
	for (index = 0; index < ff_map_weapon_count; index++)
	{
		char const *name = tag_get_name(ff_map_weapon_tags[index]);

		if ((only || !ff_map_weapon_stock[index]) && name && (!other || strcmp(other, name)))
			candidates[count++] = index;
	}
	/* (one weapon of its own alone: both hands it) */
	if (!count && only)
		return tag_get_name(ff_map_weapon_tags[spawner_rand() % (unsigned long)ff_map_weapon_count]);
	return count ? tag_get_name(ff_map_weapon_tags[candidates[spawner_rand() % (unsigned long)count]]) : NULL;
}

/* one of the map's weapons: of its own beside the game's (INCLUDE MAP's,
custom_only), a power weapon first for a heavy drop; NONE without any */
static long pick_map_weapon(boolean heavy, boolean custom_only)
{
	short candidates[MAX_ROSTER_VARIANTS];
	short count = 0;
	short pass;
	short i;

	for (pass = heavy ? 0 : 1; pass < 2 && !count; pass++)
	{
		for (i = 0; i < ff_map_weapon_count; i++)
		{
			if ((!custom_only || !ff_map_weapon_stock[i]) && (pass == 1 || ff_map_weapon_power[i]))
				candidates[count++] = i;
		}
	}
	return count ? ff_map_weapon_tags[candidates[spawner_rand() % (unsigned long)count]] : NONE;
}

/* a vehicle type's flying (a plane's or a fighter's) or hovering (a scout's) */
static boolean vehicle_tag_flies(long vehicle_tag)
{
	struct ff_vehicle_ai_tag_layout *vehicle = (struct ff_vehicle_ai_tag_layout *)tag_get(VEHICLE_DEFINITION_TAG, vehicle_tag);
	char const *name = tag_get_name(vehicle_tag);

	return (vehicle && (vehicle->type == FF_VEHICLE_TYPE_HUMAN_PLANE || vehicle->type == FF_VEHICLE_TYPE_ALIEN_FIGHTER)) ||
		(name && strstr(name, "banshee") != NULL);
}

static boolean vehicle_tag_hovers(long vehicle_tag)
{
	struct ff_vehicle_ai_tag_layout *vehicle = (struct ff_vehicle_ai_tag_layout *)tag_get(VEHICLE_DEFINITION_TAG, vehicle_tag);
	char const *name = tag_get_name(vehicle_tag);

	return (vehicle && vehicle->type == FF_VEHICLE_TYPE_ALIEN_SCOUT) ||
		(name && (strstr(name, "wraith") != NULL || strstr(name, "ghost") != NULL));
}

/* a heavy vehicle (a tank's), by its words */
static boolean vehicle_tag_heavy(long vehicle_tag)
{
	static char const *const heavy_words[] = { "wraith", "scorpion", "tank", "mammoth", "elephant", NULL };
	char lowered[256];

	lowercase_copy(tag_get_name(vehicle_tag), lowered, sizeof(lowered));
	return name_has_word(lowered, heavy_words);
}

/* one of the map's vehicles for the enemies: heavy or light as wanted where
it has both kinds; of its own beside the game's (custom_only) */
static long pick_map_vehicle(boolean heavy, boolean custom_only)
{
	short candidates[MAX_MAP_VEHICLES];
	short count = 0;
	short pass;
	short i;

	for (pass = 0; pass < 2 && !count; pass++)
	{
		for (i = 0; i < ff_map_vehicle_count; i++)
		{
			if ((!custom_only || !ff_map_vehicle_stock[i]) && (pass == 1 || vehicle_tag_heavy(ff_map_vehicle_tags[i]) == heavy))
				candidates[count++] = i;
		}
	}
	return count ? ff_map_vehicle_tags[candidates[spawner_rand() % (unsigned long)count]] : NONE;
}

/* ---------- The players' vehicles: there from the start, never taken away */

/* (each slot of deployed_set_vehicles: the three PLAYER VEHICLE rows', then
INCLUDE MAP's of the map's own) */
enum
{
	FF_PLAYER_VEHICLE_ROWS = 3
};

/* the vehicle a PLAYER VEHICLE row's kind is, ONLY MAP's of the map's own:
one whose name has the kind's word, else the map's in turn */
static long player_vehicle_tag(short row)
{
	static char const *const kind_words[NUMBER_OF_FIREFIGHT_EARLY_VEHICLES] = {
		NULL, "warthog", "ghost", "rwarthog", "scorpion", "banshee", "wraith"
	};
	static char const *const late_kind_words[NUMBER_OF_FIREFIGHT_LATE_VEHICLES] = {
		NULL, "scorpion", "wraith", "banshee", "warthog"
	};
	struct game_variant_options const *opts = game_variant_options_get();
	byte kind;
	char const *word;
	short i;

	if (!opts)
		return NONE;
	kind = row == 0 ? opts->firefight_early_vehicle : row == 1 ? opts->firefight_round2_vehicle : opts->firefight_late_vehicle;
	if (kind == 0)
		return NONE;
	if (!map_vehicles_only())
	{
		return row < 2 ? resolve_early_vehicle_tag(kind) : resolve_late_vehicle_tag(kind);
	}
	word = row < 2 ? (kind < NUMBER_OF_FIREFIGHT_EARLY_VEHICLES ? kind_words[kind] : NULL) :
		(kind < NUMBER_OF_FIREFIGHT_LATE_VEHICLES ? late_kind_words[kind] : NULL);
	for (i = 0; word && i < ff_map_vehicle_count; i++)
	{
		char const *name = tag_get_name(ff_map_vehicle_tags[i]);

		if (name && strstr(name, word))
			return ff_map_vehicle_tags[i];
	}
	return ff_map_vehicle_tags[row % ff_map_vehicle_count];
}

/* where a player vehicle goes: a PLAYER VEH PAD (any round's), the nth of
them for the nth vehicle; else the map's own vehicle spots */
static boolean player_vehicle_place(long vehicle_tag, short nth, real_point3d *out_position, real *out_facing)
{
	boolean require_approved_veh_markers = (map_has_any_custom_ff_spawns() || !is_default_stock_map());

	if (require_approved_veh_markers)
	{
		real_point3d pad_pts[16];
		real pad_facings[16];
		int pad_types[16];
		short pad_count = collect_matching_custom_ff_spawns(
			(1UL << FF_SPAWN_TYPE_PLAYER_VEHICLE), 1, 1, FALSE, FALSE, TRUE,
			pad_pts, pad_facings, pad_types, 16);
		short tries;

		for (tries = 0; tries < pad_count; tries++)
		{
			short pick = (short)((nth + tries) % pad_count);
			real_point3d position = pad_pts[pick];

			project_point_to_ground(&position, 0.75f);
			if (map_vehicle_spot_clear(&position))
			{
				*out_position = position;
				*out_facing = pad_facings[pick];
				return TRUE;
			}
		}
	}
	return map_vehicle_spot(vehicle_tag, out_position, out_facing);
}

/* (whether the slot's vehicle is still there to drive: not deleted, not a
wreck) */
static boolean player_vehicle_present(long vehicle_index)
{
	struct object_datum *object = vehicle_index != NONE ?
		object_try_and_get_and_verify_type(vehicle_index, _object_mask_vehicle) : NULL;

	return object && !TEST_FLAG(object->object.damage_flags, _object_dead_bit);
}

void firefight_spawn_player_vehicles(void)
{
	long tags[FIREFIGHT_MAX_SET_VEHICLES];
	short slot;
	short made = 0;
	short nth = 0;

	/* (after the map's own vehicles are cleared away: firefight_update's
	second tick) */
	if (network_game_distributed_client() || ff_state.session_elapsed_ticks < 2)
		return;
	for (slot = 0; slot < FIREFIGHT_MAX_SET_VEHICLES; slot++)
		tags[slot] = slot < FF_PLAYER_VEHICLE_ROWS ? player_vehicle_tag(slot) : NONE;
	/* (INCLUDE MAP: each of the map's own beside the game's, once) */
	if (map_content_of(FIREFIGHT_MAP_CONTENT_VEHICLES_OFF) == _firefight_map_content_include_map)
	{
		short i;

		for (i = 0, slot = FF_PLAYER_VEHICLE_ROWS; i < ff_map_vehicle_count && slot < FIREFIGHT_MAX_SET_VEHICLES; i++)
		{
			if (!ff_map_vehicle_stock[i])
				tags[slot++] = ff_map_vehicle_tags[i];
		}
	}

	ff_state.deployed_set_vehicle_count = 0;
	for (slot = 0; slot < FIREFIGHT_MAX_SET_VEHICLES; slot++)
	{
		struct object_placement_data placement;
		real_point3d position;
		real facing = 0.0f;
		long vehicle_index;

		if (player_vehicle_present(ff_state.deployed_set_vehicles[slot]))
		{
			ff_state.deployed_set_vehicle_count++;
			nth++;
			continue;
		}
		ff_state.deployed_set_vehicles[slot] = NONE;
		if (tags[slot] == NONE)
			continue;
		if (!player_vehicle_place(tags[slot], nth, &position, &facing))
			continue;
		object_placement_data_new(&placement, tags[slot], NONE);
		placement.position = position;
		vector3d_from_angle(&placement.forward, facing);
		placement.owner_team_index = _game_team_player;
		vehicle_index = object_new(&placement);
		if (vehicle_index == NONE)
			continue;
		ff_state.deployed_set_vehicles[slot] = vehicle_index;
		ff_state.deployed_set_vehicle_count++;
		nth++;
		made++;
	}
	if (made)
		firefight_announce_banner(_ff_banner_vehicle_deployed, MAX(1, ff_state.current_round), 3 * TICKS_PER_SECOND);
}

static boolean vehicle_has_player_rider(long vehicle_index)
{
	struct data_iterator iter;
	struct player_datum *player;

	if (!player_data || vehicle_index == NONE)
		return FALSE;

	data_iterator_new(&iter, player_data);
	while ((player = data_iterator_next(&iter)) != NULL)
	{
		if (player->unit_index != NONE)
		{
			long parent = object_get_ultimate_parent(player->unit_index);
			if (parent == vehicle_index)
				return TRUE;
		}
	}

	return FALSE;
}

static void eject_all_vehicle_passengers(long vehicle_index)
{
	struct object_iterator iter;
	struct object_datum *obj;
	long riders[32];
	short rider_count = 0;
	short i;

	if (vehicle_index == NONE)
		return;

	object_iterator_new(&iter, _object_mask_unit, 0);
	while ((obj = (struct object_datum *)object_iterator_next(&iter)) != NULL && rider_count < 32)
	{
		if (iter.index != vehicle_index &&
			(obj->object.parent_object_index == vehicle_index ||
				object_get_ultimate_parent(iter.index) == vehicle_index))
		{
			riders[rider_count++] = iter.index;
		}
	}

	for (i = 0; i < rider_count; i++)
	{
		struct unit_datum *u = (struct unit_datum *)object_try_and_get_and_verify_type(riders[i], _object_mask_unit);
		if (!u)
			continue;
		if (u->unit.parent_seat_index != NONE)
			unit_exit_seat_end(riders[i]);
		if (u->object.parent_object_index != NONE)
			unit_detach_from_parent(riders[i]);
	}
}

static boolean is_tracked_firefight_vehicle(long vehicle_index)
{
	short i;

	if (vehicle_index == NONE)
		return FALSE;
	if (ff_state.active_dropship.vehicle_index == vehicle_index)
		return TRUE;
	if (firefight_is_generator_object(vehicle_index, NULL))
		return TRUE;
	for (i = 0; i < FIREFIGHT_MAX_SET_VEHICLES; i++)
	{
		if (ff_state.deployed_set_vehicles[i] == vehicle_index)
			return TRUE;
	}
	for (i = 0; i < FIREFIGHT_MAX_TRACKED_ENEMIES; i++)
	{
		long u_idx = ff_state.tracked_enemy_units[i];
		if (u_idx != NONE)
		{
			struct object_datum *u_obj = object_try_and_get(u_idx);
			if (u_obj && u_obj->object.parent_object_index == vehicle_index)
				return TRUE;
		}
	}
	return FALSE;
}

void firefight_cleanup_natural_multiplayer_sandbox(boolean clear_weapons_and_equipment)
{
	struct scenario *scen = global_scenario_try_and_get();
	struct object_iterator iter;
	struct object_datum *obj;
	long to_delete[128];
	short delete_count = 0;
	short i;

	if (!scen || scen->type != _scenario_type_multiplayer)
		return;

	/* Delete any naturally spawned unoccupied map vehicles not tracked in Firefight's scheduled round vehicle array */
	object_iterator_new(&iter, _object_mask_vehicle, 0);
	while ((obj = (struct object_datum *)object_iterator_next(&iter)) != NULL && delete_count < 128)
	{
		long obj_index = iter.index;
		if (!is_tracked_firefight_vehicle(obj_index) &&
			!vehicle_has_player_rider(obj_index) &&
			obj->object.first_child_object_index == NONE)
		{
			to_delete[delete_count++] = obj_index;
		}
	}

	/* Clear natural ground weapons & equipment at match start (tick == 1) before Firefight ordnance drops */
	if (clear_weapons_and_equipment)
	{
		object_iterator_new(&iter, _object_mask_item, 0);
		while ((obj = (struct object_datum *)object_iterator_next(&iter)) != NULL && delete_count < 128)
		{
			/* (on the ground: a weapon in a unit's inventory has no parent
			until it is in hand, and is off the map. The first spawn's are
			already there, this tick) */
			if (obj->object.parent_object_index == NONE &&
				TEST_FLAG(obj->object.flags, _object_connected_to_map_bit) &&
				!TEST_FLAG(obj->object.flags, _object_cannot_be_garbage_bit))
			{
				to_delete[delete_count++] = iter.index;
			}
		}
	}

	for (i = 0; i < delete_count; i++)
	{
		if (object_try_and_get_and_verify_type(to_delete[i], 0xFFFFFFFFUL))
			object_delete(to_delete[i]);
	}
}

/* ---------- Per-Tick Dropship & Aggro Update */

void firefight_update_dropships_and_vehicles(void)
{
	short i;
	long now = game_time_get();

	/* Drain queued reinforcement enemies as active alive enemies fall below the safe 28-actor cap */
	if (g_queued_reinforcement_enemies > 0 &&
		ff_state.wave_spawn_zone_count > 0 &&
		ff_state.enemies_alive < FIREFIGHT_SAFE_ACTIVE_ACTOR_CAP - 2)
	{
		struct game_engine_firefight_variant const *ff = &global_variant.game_engine_variant.firefight;
		struct game_variant_options const *opts = game_variant_options_get();
		byte wave_faction = firefight_wave_faction(ff->wave_faction, ff_state.in_bonus_round);
		short forced_team = (!opts || opts->firefight_faction_diplomacy == _firefight_diplomacy_allied_against_players) ?
			((wave_faction == _firefight_faction_unsc_humans) ? _game_team_human : _game_team_covenant) : NONE;
		short slots = (short)MIN(g_queued_reinforcement_enemies, FIREFIGHT_SAFE_ACTIVE_ACTOR_CAP - ff_state.enemies_alive);
		real_point3d player_positions[16];
		short player_count = get_living_human_player_positions(player_positions, NULL, 16);

		/* (the players may have left the wave's zones behind: a queue is
		drained over the whole wave) */
		if (player_count > 0)
		{
			real_point3d const *anchor = &player_positions[central_player_index(player_positions, player_count)];
			boolean zone_near = FALSE;
			short z;

			for (z = 0; z < ff_state.wave_spawn_zone_count; z++)
			{
				if (distance3d(&ff_state.wave_spawn_zones[z], anchor) <= FF_ZONE_NEAR_DISTANCE)
					zone_near = TRUE;
			}
			if (!zone_near)
			{
				real_point3d zones[FIREFIGHT_MAX_SPAWN_ZONES_PER_WAVE];
				real facings[FIREFIGHT_MAX_SPAWN_ZONES_PER_WAVE];
				short zone_count = select_wave_spawn_zones(
					zones,
					facings,
					FIREFIGHT_MAX_SPAWN_ZONES_PER_WAVE,
					ff_state.current_round,
					ff_state.current_wave,
					ff_state.in_bonus_round,
					firefight_wave_will_have_heavy_or_boss(
						ff_state.current_set, ff_state.current_round, ff_state.current_wave));

				for (z = 0; z < zone_count; z++)
				{
					ff_state.wave_spawn_zones[z] = zones[z];
					g_active_wave_facings[z] = facings[z];
				}
				if (zone_count > 0)
					ff_state.wave_spawn_zone_count = zone_count;
			}
		}

		for (i = 0; i < slots && g_queued_reinforcement_enemies > 0; i++)
		{
			short z = (short)((g_queued_reinforcement_enemies + i) % ff_state.wave_spawn_zone_count);
			byte subtype = g_queued_reinforcement_subtypes[0];
			short q;

			for (q = 0; q + 1 < g_queued_reinforcement_enemies; q++)
				g_queued_reinforcement_subtypes[q] = g_queued_reinforcement_subtypes[q + 1];
			g_queued_reinforcement_enemies--;

			if (spawn_subtype_actor_at(
					subtype,
					wave_faction,
					&ff_state.wave_spawn_zones[z],
					g_active_wave_facings[z],
					forced_team,
					TRUE) == NONE)
			{
				break;
			}
		}
		ff_state.enemies_queued = g_queued_reinforcement_enemies;
	}

	if (ff_state.active_dropship.phase != DROPSHIP_PHASE_NONE)
	{
		long v_idx = ff_state.active_dropship.vehicle_index;
		short elapsed = ff_state.active_dropship.elapsed_ticks;
		short flight_end = (short)ff_route_flight_total_ticks(&g_dropship_flight);
		real angle = ff_state.active_dropship.approach_angle;
		real cos_a = cosine(angle);
		real sin_a = sine(angle);
		struct ff_route_pose pose = ff_route_flight_pose(&g_dropship_flight, (real)elapsed);

		ff_state.active_dropship.phase = (short)pose.phase;
		ff_state.active_dropship.scale = pose.scale;
		ff_state.active_dropship.cloak = pose.cloak;
		dropship_flash(
			&g_dropship_flight,
			(real)elapsed - 1.0f < 0.0f ? 0.0f : (real)elapsed - 1.0f,
			(real)elapsed,
			&pose,
			ff_state.active_dropship.wave_faction,
			ff_state.active_dropship.is_boss_wave);

		if (pose.phase == FF_ROUTE_PHASE_HOVER)
		{
			if (!ff_state.active_dropship.payload_deployed)
			{
				long player_units[16];
				short player_count = get_living_player_positions(NULL, player_units, 16);
				real drop_facing = angle + _pi;
				real right_x = sin_a;
				real right_y = -cos_a;
				short s_idx;
				short to_unload = ff_state.active_dropship.squad_to_spawn;

				spawn_insertion_beacon_effect(
					&ff_state.active_dropship.drop_zone,
					NONE,
					ff_state.active_dropship.wave_faction,
					ff_state.active_dropship.is_boss_wave);

				for (s_idx = 0; s_idx < to_unload; s_idx++)
				{
					byte subtype = (s_idx < g_dropship_squad_subtype_count) ?
						g_dropship_squad_subtypes[s_idx] : _ff_sub_grunt;
					real side_sign = (s_idx & 1) ? 1.0f : -1.0f;
					real bay_along = ((real)(s_idx / 2) - 1.0f) * 1.4f;
					real_point3d bay_pos;
					long u_spawned;

					bay_pos.x = ff_state.active_dropship.hover_position.x +
						right_x * (side_sign * 2.1f) - cos_a * bay_along;
					bay_pos.y = ff_state.active_dropship.hover_position.y +
						right_y * (side_sign * 2.1f) - sin_a * bay_along;
					bay_pos.z = ff_state.active_dropship.drop_zone.z;
					project_point_to_ground(&bay_pos, 0.25f);

					u_spawned = spawn_subtype_actor_at(
						subtype,
						ff_state.active_dropship.wave_faction,
						&bay_pos,
						drop_facing,
						ff_state.active_dropship.forced_team,
						TRUE);
					if (u_spawned != NONE && player_count > 0)
					{
						struct unit_datum *u_dat = unit_try_and_get(u_spawned);
						if (u_dat && u_dat->unit.actor_index != NONE)
						{
							direct_actor_toward_unit(
								u_dat->unit.actor_index,
								player_units[s_idx % player_count]);
						}
					}
				}

				g_dropship_squad_subtype_count = 0;
				ff_state.active_dropship.payload_deployed = TRUE;
				ff_state.active_dropship.squad_to_spawn = 0;
			}
		}
		if (v_idx != NONE && !dropship_fly(v_idx, &pose))
			ff_state.active_dropship.vehicle_index = NONE;

		ff_state.active_dropship.elapsed_ticks++;
		if (ff_state.active_dropship.timer_ticks > 0)
			ff_state.active_dropship.timer_ticks--;

		if (ff_state.active_dropship.elapsed_ticks >= flight_end)
		{
			if (ff_state.active_dropship.vehicle_index != NONE)
			{
				object_delete(ff_state.active_dropship.vehicle_index);
				ff_state.active_dropship.vehicle_index = NONE;
			}
			ff_state.active_dropship.phase = DROPSHIP_PHASE_NONE;
			ff_state.active_dropship.payload_deployed = TRUE;
			ff_state.active_dropship.squad_to_spawn = 0;
		}
	}

	/* Clean up any enemy vehicles (Ghosts, Wraiths, Banshees, etc.) whose driver died or fell off
	   so driverless Ghosts never keep driving or firing on their own, and despawn them immediately. */
	{
		struct object_iterator v_iter;
		struct unit_datum *v_unit;
		long dead_vehicles[32];
		short dead_vehicle_count = 0;
		long new_wrecks[32];
		short new_wreck_count = 0;

		object_iterator_new(&v_iter, _object_mask_vehicle, 0);
		while ((v_unit = (struct unit_datum *)object_iterator_next(&v_iter)) != NULL && dead_vehicle_count < 32)
		{
			long v_idx = v_iter.index;
			long drv_idx;
			boolean has_living_driver = FALSE;
			boolean is_player_set_vehicle = FALSE;
			char const *v_name;
			boolean is_ghost = FALSE;
			boolean is_wreck = TEST_FLAG(v_unit->object.damage_flags, _object_dead_bit);
			short sv;

			if (v_idx == ff_state.active_dropship.vehicle_index ||
				firefight_is_generator_object(v_idx, NULL))
			{
				continue;
			}
			if (is_wreck && new_wreck_count < (short)NUMBEROF(new_wrecks) &&
				!TEST_FLAG(v_unit->object.flags, _object_cannot_be_garbage_bit))
			{
				new_wrecks[new_wreck_count++] = v_idx;
			}
			if (vehicle_has_player_rider(v_idx))
				continue;

			for (sv = 0; sv < FIREFIGHT_MAX_SET_VEHICLES; sv++)
			{
				if (ff_state.deployed_set_vehicles[sv] == v_idx)
				{
					is_player_set_vehicle = TRUE;
					break;
				}
			}

			drv_idx = v_unit->unit.driver_object_index;
			if (drv_idx != NONE)
			{
				struct unit_datum *drv = (struct unit_datum *)object_try_and_get_and_verify_type(drv_idx, _object_mask_unit);
				if (drv &&
					!TEST_FLAG(drv->object.damage_flags, _object_dead_bit) &&
					drv->object.parent_object_index == v_idx)
				{
					has_living_driver = TRUE;
				}
			}

			if (!has_living_driver)
			{
				/* Check if any living unit is still parented to this vehicle */
				long child_idx = v_unit->object.first_child_object_index;
				short safety = 0;
				while (child_idx != NONE && safety++ < 16)
				{
					struct object_datum *c_obj = object_try_and_get(child_idx);
					if (!c_obj)
						break;
					if (TEST_FLAG(_object_mask_unit, c_obj->object.type) &&
						!TEST_FLAG(c_obj->object.damage_flags, _object_dead_bit))
					{
						has_living_driver = TRUE;
						break;
					}
					child_idx = c_obj->object.next_object_index;
				}
			}

			if (!has_living_driver)
			{
				/* Immediately stop throttle and weapon triggers on any driverless vehicle */
				v_unit->unit.throttle.i = 0.0f;
				v_unit->unit.throttle.j = 0.0f;
				v_unit->unit.throttle.k = 0.0f;
				v_unit->unit.primary_trigger = 0.0f;
				SET_FLAG(v_unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit, FALSE);
				SET_FLAG(v_unit->unit.control_flags, _unit_control_weapon_secondary_trigger_bit, FALSE);
				SET_FLAG(v_unit->unit.flags, _unit_actively_controlled_bit, FALSE);
				v_unit->unit.driver_object_index = NONE;
				v_unit->unit.gunner_object_index = NONE;

				v_name = tag_get_name(v_unit->definition_index);
				is_ghost = (v_name && strstr(v_name, "ghost") != NULL);

				/* (one that was destroyed stays: wreck_keep) */
				if (!is_wreck && !is_player_set_vehicle &&
					(is_ghost || v_unit->object.owner_team_index != _game_team_player))
				{
					dead_vehicles[dead_vehicle_count++] = v_idx;
				}
			}
			else if (is_wreck)
			{
				dead_vehicles[dead_vehicle_count++] = v_idx;
			}
		}

		for (i = 0; i < dead_vehicle_count; i++)
		{
			eject_all_vehicle_passengers(dead_vehicles[i]);
			if (TEST_FLAG(object_get(dead_vehicles[i])->object.damage_flags, _object_dead_bit))
				continue;
			if (enemy_vehicle_noted(dead_vehicles[i]) && vehicle_has_wreck_model(dead_vehicles[i]))
				vehicle_wreck(dead_vehicles[i]);
			else
				object_delete(dead_vehicles[i]);
		}

		for (i = 0; i < new_wreck_count; i++)
			wreck_keep(new_wrecks[i]);
	}

	/* Drive seated enemy boss vehicles every tick so Wraiths, Banshees, Ghosts, and Scorpions
	   actively steer, fly/drive toward, and fire at players even on Multiplayer maps without BSP pathfinding */
	{
		real_point3d player_positions[16];
		long player_units[16];
		short player_count = get_living_player_positions(player_positions, player_units, 16);

		if (player_count > 0)
		{
			for (i = 0; i < FIREFIGHT_MAX_TRACKED_ENEMIES; i++)
			{
				long u_idx = ff_state.tracked_enemy_units[i];
				struct unit_datum *u;
				struct unit_datum *v_unit;
				long v_idx;

				if (u_idx == NONE)
					continue;
				u = (struct unit_datum *)object_try_and_get_and_verify_type(u_idx, _object_mask_unit);
				if (!u || TEST_FLAG(u->object.damage_flags, _object_dead_bit) || u->object.parent_object_index == NONE)
					continue;

				v_idx = u->object.parent_object_index;
				v_unit = (struct unit_datum *)object_try_and_get_and_verify_type(v_idx, _object_mask_vehicle);
				if (!v_unit || TEST_FLAG(v_unit->object.damage_flags, _object_dead_bit))
					continue;

				if (v_unit->unit.driver_object_index != u_idx)
				{
					v_unit->unit.driver_object_index = u_idx;
					v_unit->unit.gunner_object_index = u_idx;
					SET_FLAG(v_unit->unit.flags, _unit_actively_controlled_bit, TRUE);
				}
				object_activate(v_idx);

				/* Reset triggers each tick before deciding whether to fire */
				SET_FLAG(u->unit.control_flags, _unit_control_weapon_primary_trigger_bit, FALSE);
				SET_FLAG(v_unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit, FALSE);
				u->unit.primary_trigger = 0.0f;
				v_unit->unit.primary_trigger = 0.0f;

				{
					real_point3d v_pos;
					short best_p = 0;
					real best_dist_sq = 999999.0f;
					short p;
					real dist;
					real_vector3d to_target;

					object_get_origin(v_idx, &v_pos);
					for (p = 0; p < player_count; p++)
					{
						real d_sq = distance_squared3d(&v_pos, &player_positions[p]);
						if (d_sq < best_dist_sq)
						{
							best_dist_sq = d_sq;
							best_p = p;
						}
					}

					dist = square_root(MAX(0.0f, best_dist_sq));
					vector_from_points3d(&v_pos, &player_positions[best_p], &to_target);
					to_target.k += 0.35f;
					if (normalize3d(&to_target) > 0.0f)
					{
						char const *v_name = tag_get_name(v_unit->definition_index);
						boolean is_banshee = (v_name && strstr(v_name, "banshee") != NULL);
						real_vector3d move_facing = to_target;

						u->unit.desired_aiming_vector = to_target;
						u->unit.desired_looking_vector = to_target;
						v_unit->unit.desired_aiming_vector = to_target;
						v_unit->unit.desired_looking_vector = to_target;

						if (!is_banshee)
							move_facing.k = 0.0f;
						if (normalize3d(&move_facing) > 0.0f)
						{
							u->unit.desired_facing_vector = move_facing;
							v_unit->unit.desired_facing_vector = move_facing;
						}

						if (magnitude_squared3d(&u->unit.throttle) < 0.01f)
						{
							if (is_banshee)
							{
								u->unit.throttle.i = 1.0f;
								u->unit.throttle.j = 0.0f;
								u->unit.throttle.k = (v_pos.z < player_positions[best_p].z + 4.0f) ? 0.35f : -0.1f;
							}
							else if (dist > 11.0f)
							{
								u->unit.throttle.i = 0.90f;
								u->unit.throttle.j = 0.0f;
								u->unit.throttle.k = 0.0f;
							}
							else if (dist < 5.5f)
							{
								u->unit.throttle.i = -0.45f;
								u->unit.throttle.j = 0.25f;
								u->unit.throttle.k = 0.0f;
							}
							else
							{
								u->unit.throttle.i = 0.20f;
								u->unit.throttle.j = ((now / 45) & 1) ? 0.55f : -0.55f;
								u->unit.throttle.k = 0.0f;
							}
							v_unit->unit.throttle = u->unit.throttle;
						}

						if (dist < 42.0f &&
							dot_product3d(&v_unit->object.forward, &to_target) > 0.55f &&
							(now % 45) < 28)
						{
							SET_FLAG(u->unit.control_flags, _unit_control_weapon_primary_trigger_bit, TRUE);
							SET_FLAG(v_unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit, TRUE);
							u->unit.primary_trigger = 1.0f;
							v_unit->unit.primary_trigger = 1.0f;
						}
					}
				}
			}
		}
	}

	/* Refresh enemy perception and full-map pursuit of nearest living player or generator,
	   staggered across 15 ticks (~2 actors per tick) to eliminate per-second AI pathfinding spikes */
	if (now != g_last_aggro_refresh_tick)
	{
		real_point3d player_positions[16];
		long player_units[16];
		short player_count = get_living_player_positions(player_positions, player_units, 16);
		long open_gen_units[FIREFIGHT_MAX_GENERATORS];
		real_point3d open_gen_positions[FIREFIGHT_MAX_GENERATORS];
		short open_gen_count = 0;
		short g;
		short tick_mod = (short)((now >= 0 ? now : -now) % 15);

		for (g = 0; g < ff_state.generator_count && g < FIREFIGHT_MAX_GENERATORS; g++)
		{
			struct firefight_generator_state const *gen = &ff_state.generators[g];
			if (gen->alive && gen->lockdown_ticks == 0 && gen->object_index != NONE &&
				object_try_and_get(gen->object_index) != NULL)
			{
				open_gen_units[open_gen_count] = gen->object_index;
				open_gen_positions[open_gen_count] = gen->position;
				open_gen_count++;
			}
		}

		g_last_aggro_refresh_tick = now;
		if (player_count > 0)
		{
			for (i = 0; i < FIREFIGHT_MAX_TRACKED_ENEMIES; i++)
			{
				long u_idx;
				if (((i + tick_mod) % 15) != 0)
					continue;
				u_idx = ff_state.tracked_enemy_units[i];
				g_generator_attack_units[i] = NONE;
				if (u_idx != NONE)
				{
					struct unit_datum *u = (struct unit_datum *)object_try_and_get_and_verify_type(
						u_idx, _object_mask_unit);
					if (u && !TEST_FLAG(u->object.damage_flags, _object_dead_bit) && u->unit.actor_index != NONE)
					{
						real_point3d u_pos;
						long chosen_target = NONE;
						short best_p = 0;
						real best_p_dist_sq = 999999.0f;
						short p;

						object_get_origin(u_idx, &u_pos);

						for (p = 0; p < player_count; p++)
						{
							real d = distance_squared3d(&u_pos, &player_positions[p]);
							if (d < best_p_dist_sq)
							{
								best_p_dist_sq = d;
								best_p = p;
							}
						}

						/* GENERATOR ATTACKERS' share goes for the nearest unlocked generator
						   even under fire, turning only on a player right beside it */
						if (open_gen_count > 0 && generator_attacker_slot(i) &&
							best_p_dist_sq > 6.0f * 6.0f)
						{
							short best_g = 0;
							real best_g_dist_sq = 999999.0f;
							for (g = 0; g < open_gen_count; g++)
							{
								real d_sq = distance_squared3d(&u_pos, &open_gen_positions[g]);
								if (d_sq < best_g_dist_sq)
								{
									best_g_dist_sq = d_sq;
									best_g = g;
								}
							}
							chosen_target = open_gen_units[best_g];
							g_generator_attack_units[i] = chosen_target;
						}

						if (chosen_target == NONE)
							chosen_target = player_units[best_p];

						direct_actor_toward_unit(u->unit.actor_index, chosen_target);
					}
				}
			}
		}
	}
}
