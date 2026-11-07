/*
 * OpenCE : Faber ("OpenCE - Faber.exe")
 * Interactive In-Engine AI Placement System (source/ai/ integration).
 * Spawns native Halo: Combat Evolved NPC actors & bipeds (Marines, Elites,
 * Grunts, Jackals, Hunters, Flood, Sentinels) assigned to Friendly or Enemy
 * combat teams.
 * Strictly modular inside FORGE CODE/ with zero modifications to source/ or port/.
 */

#include "cseries.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game/game.h"
#include "game/game_allegiance.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "units/units.h"
#include "ai/ai.h"
#include "ai/actors.h"
#include "ai/actor_definitions.h"
#include "ai/actions.h"
#include "ai/ai_scenario_definitions.h"
#include "ai/encounters.h"
#include "game/game_engine.h"
#include "scenario/scenario.h"
#include "math/real_math.h"
#include "scenario/scenario_definitions.h"
#include "physics/collisions.h"
#include "units/unit_definitions.h"
#include "items/items.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "objects/damage.h"
#include "effects/effect_definitions.h"
#include "effects/effects.h"
#include "halo_skulls.h"

#include "forge_engine.h"

/* (the C library's own, not cseries.h's debug allocator: these blocks are
calloc's, and live across maps) */
#undef malloc
#undef free

extern void *cache_files_tag_instances(long *count);
extern boolean unit_can_hold_weapon_definition(long unit_index, long weapon_definition_index);
extern boolean firefight_active(void);

static short map_forge_team_to_engine_team(int16_t forge_team_designator)
{
	switch (forge_team_designator)
	{
	case _forge_ai_team_friendly_human:
		return _game_team_player; /* Friendly to Spartan players (Human/Player/Blue) */
	case _forge_ai_team_enemy_flood:
		return _game_team_flood;
	case _forge_ai_team_enemy_sentinel:
		return _game_team_sentinel;
	case _forge_ai_team_enemy_covenant:
	default:
		return _game_team_covenant; /* Hostile Enemy Covenant / Red */
	}
}

int32_t forge_ai_spawn_unit_live(
	char const *actor_variant_path,
	float x, float y, float z, float yaw,
	int16_t team_designator,
	int32_t *out_unit_index)
{
	long actv_idx = NONE;
	long unit_def_idx = NONE;
	long unit_index = NONE;
	long actor_index = NONE;
	short engine_team = map_forge_team_to_engine_team(team_designator);
	struct object_placement_data placement;

	if (out_unit_index)
		*out_unit_index = NONE;
	if (!actor_variant_path || !actor_variant_path[0])
		return NONE;

	/* Look up the actor_variant ('actv') tag in the active map */
	actv_idx = forge_resolve_engine_tag('actv', actor_variant_path, NULL);
	if (actv_idx != NONE)
	{
		struct actor_variant_definition *actv_def =
			(struct actor_variant_definition *)tag_get('actv', actv_idx);
		if (actv_def && actv_def->unit_reference.index != NONE)
		{
			unit_def_idx = actv_def->unit_reference.index;
		}
	}

	/* Fallback to resolving a biped ('bipd') directly if the current map has not yet baked this 'actv' */
	if (unit_def_idx == NONE)
	{
		unit_def_idx = forge_resolve_engine_tag('bipd', actor_variant_path, NULL);
	}
	if (unit_def_idx == NONE)
		return NONE;

	object_placement_data_new(&placement, unit_def_idx, NONE);
	placement.position.x = x;
	placement.position.y = y;
	placement.position.z = z;
	placement.forward.i = (real)cos((double)yaw);
	placement.forward.j = (real)sin((double)yaw);
	placement.forward.k = 0.0f;
	placement.up.i = 0.0f;
	placement.up.j = 0.0f;
	placement.up.k = 1.0f;
	placement.owner_team_index = engine_team;

	/* Tint friendly vs enemy change-colors so team allegiance is visually clear */
	if (team_designator == _forge_ai_team_friendly_human)
	{
		placement.change_colors[0].red = 0.20f;
		placement.change_colors[0].green = 0.55f;
		placement.change_colors[0].blue = 0.95f;
	}
	else
	{
		placement.change_colors[0].red = 0.90f;
		placement.change_colors[0].green = 0.22f;
		placement.change_colors[0].blue = 0.18f;
	}

	unit_index = object_new(&placement);
	if (unit_index == NONE)
		return NONE;

	if (out_unit_index)
		*out_unit_index = (int32_t)unit_index;

	if (actv_idx != NONE)
	{
		/* Equip the unit with the actor variant's weapon, grenades, shields & vitality */
		actor_customize_unit(actv_idx, unit_index);

		/* Create and attach the live Halo CE AI actor brain */
		actor_index = actor_create_for_unit(
			FALSE,
			unit_index,
			actv_idx,
			NONE,
			NONE,
			FALSE,
			NONE,
			FALSE,
			_actor_action_alert,
			_actor_action_alert,
			NONE,
			NONE);

		if (actor_index != NONE)
		{
			actor_set_team(actor_index, engine_team);
			actor_set_dormant(actor_index, FALSE);
			actor_braindead(actor_index, FALSE);
		}
	}

	return (int32_t)actor_index;
}

/* ---------- AI spawns

A map's AI is its encounters: squads with starting locations, firing positions
to fight from, and the engine's own placing and respawning. Forge shows each
starting location as a marker and the level's scenario plays what the markers
say, rebuilt as the AI starts (forge_ai_encounters_apply).

The rebuild never moves what a script can name: the map's encounters and squads
keep their indices, with their starting locations as the markers left them. A
marker the map did not have, or one changed past what a starting location can
hold (its team, its numbers), gets a squad of its own, in an encounter added
after the map's: a copy of the nearest of the map's, so it has that encounter's
firing positions to move between. A spawn with no firing positions near, as
on a map with no AI at all, gets an encounter made here, with firing positions
found on the ground around it (forge_ai_firing_positions_add). Only a level
with spawns does any of this: a map that uses no AI is left as it is. Nothing
is written to the map's tags, which every level load reads afresh: a saved map
keeps its spawns as lines of its trailer's text, on stock and Custom Edition
maps alike.

The Actors tab is Forge's pack's actors, read as they are placed, as the rest of
its palette is. A saved map names those its spawns use, which any game of it
copies in as it loads, the same on every machine, so a unit the host's
encounters make is the same tag on its clients (randomizer.c). A Forge game
played online copies in all of them for that reason. */

#define FORGE_AI_MAX_ADDED_ENCOUNTERS 32
/* (weapons a level's spawns can name: a starting location's spare flag bits) */
#define FORGE_AI_MAX_WEAPONS 127
#define FORGE_AI_LOCATION_WEAPON_SHIFT 1
#define FORGE_AI_LOCATION_OWN_FLAGS ((1 << FORGE_AI_LOCATION_WEAPON_SHIFT) - 1)
/* a spawn's game modes, a bit each. 0, as older maps and the map's own locations
have, reads as all but Firefight: Firefight plays only what was set to */
#define FORGE_AI_MODE_FIREFIGHT 0x20u
#define FORGE_AI_MODES_NOT_FIREFIGHT 0x1Fu
#define FORGE_AI_MODES_ALL (FORGE_AI_MODES_NOT_FIREFIGHT | FORGE_AI_MODE_FIREFIGHT)
/* a spawn this far from any firing position gets its own (world units) */
#define FORGE_AI_NEAR_FIRING_POSITION 25.0f
/* made firing positions: each spawn's, on rings around it, in an encounter of
at most 512 (encounters.c) */
#define FORGE_AI_FIRING_POSITIONS_PER_SPAWN 25
#define FORGE_AI_MADE_SQUADS_PER_ENCOUNTER 20
/* what a made squad is, as the maps' own are: guarding, all but the two
letters no map's use, its timer running */
#define FORGE_AI_SQUAD_STATE 7
#define FORGE_AI_SQUAD_POSITION_GROUPS 0x03FFFF03u
#define FORGE_AI_SQUAD_POSITION_GROUP_COUNT 6

typedef char forge_ai_encounter_size_assert[sizeof(struct encounter_definition) == 0xB0 ? 1 : -1];
typedef char forge_ai_squad_size_assert[sizeof(struct squad_definition) == 0xE8 ? 1 : -1];

struct forge_ai_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

/* an encounter added for spawns of one team around one of the map's */
struct forge_ai_added_encounter
{
	short team;
	short source_encounter;
	short squad_count;
	short squads_filled;
	boolean respawns;
	struct squad_definition *squads;
	/* (a made encounter's, source_encounter NONE) */
	struct firing_position_definition *firing_positions;
	short firing_position_count;
};

/* ---------- the actors the palette offers

A campaign has an actor for each rank with each weapon. Forge offers a rank
once, and the spawn's WEAPON row arms it: the palette lists these kinds, in
their groups, each from the first of its tags. A rank's other tags stay good in
a saved map's spawn, and show as their kind. */
enum
{
	_forge_ai_group_grunts,
	_forge_ai_group_jackals,
	_forge_ai_group_elites,
	_forge_ai_group_hunters,
	_forge_ai_group_engineers,
	_forge_ai_group_marines,
	_forge_ai_group_crew,
	_forge_ai_group_flood,
	_forge_ai_group_sentinels,
	_forge_ai_group_other,
	NUMBER_OF_FORGE_AI_GROUPS
};

static char const *const s_ai_group_names[NUMBER_OF_FORGE_AI_GROUPS] =
{
	"GRUNTS", "JACKALS", "ELITES", "HUNTERS", "ENGINEERS", "MARINES", "CREW", "FLOOD", "SENTINELS", "OTHER"
};

/* a tag's kind: the first whose prefix its leaf starts with, and that has
`also` in it if it asks for it. A kind with no name is not offered */
static struct forge_ai_kind
{
	short group;
	char const *prefix, *also, *name;
} const s_ai_kinds[] =
{
	{ _forge_ai_group_grunts, "grunt flee", NULL, NULL },
	{ _forge_ai_group_grunts, "grunt minor", NULL, "GRUNT MINOR" },
	{ _forge_ai_group_grunts, "grunt major", NULL, "GRUNT MAJOR" },
	{ _forge_ai_group_grunts, "grunt specops", NULL, "GRUNT SPECOPS" },
	{ _forge_ai_group_jackals, "jackal minor", NULL, "JACKAL MINOR" },
	{ _forge_ai_group_jackals, "jackal major", NULL, "JACKAL MAJOR" },
	{ _forge_ai_group_elites, "elite minor", NULL, "ELITE MINOR" },
	{ _forge_ai_group_elites, "elite major", NULL, "ELITE MAJOR" },
	{ _forge_ai_group_elites, "elite specops", NULL, "ELITE SPECOPS" },
	{ _forge_ai_group_elites, "elite commander", NULL, "ELITE COMMANDER" },
	{ _forge_ai_group_elites, "stealth elite major", NULL, "ELITE STEALTH MAJOR" },
	{ _forge_ai_group_elites, "stealth elite", NULL, "ELITE STEALTH" },
	{ _forge_ai_group_hunters, "hunter major", NULL, "HUNTER MAJOR" },
	{ _forge_ai_group_hunters, "hunter", NULL, "HUNTER" },
	{ _forge_ai_group_engineers, "engineer major", NULL, "ENGINEER MAJOR" },
	{ _forge_ai_group_engineers, "engineer", NULL, "ENGINEER" },
	{ _forge_ai_group_marines, "marine_armored", " major", "ARMORED MARINE MAJOR" },
	{ _forge_ai_group_marines, "marine_armored", NULL, "ARMORED MARINE" },
	{ _forge_ai_group_marines, "marine_wounded", NULL, "WOUNDED MARINE" },
	{ _forge_ai_group_marines, "marine_suicidal", NULL, "SUICIDAL MARINE" },
	{ _forge_ai_group_marines, "marine", " major", "MARINE MAJOR" },
	{ _forge_ai_group_marines, "marine", NULL, "MARINE" },
	{ _forge_ai_group_crew, "crewman", NULL, "CREWMAN" },
	{ _forge_ai_group_flood, "floodcombat elite", NULL, "FLOOD COMBAT ELITE" },
	{ _forge_ai_group_flood, "floodcombat_human", NULL, "FLOOD COMBAT HUMAN" },
	{ _forge_ai_group_flood, "floodcarrier", NULL, "FLOOD CARRIER" },
	{ _forge_ai_group_flood, "flood_infection", NULL, "FLOOD INFECTION" },
	{ _forge_ai_group_sentinels, "sentinel_shielded", " major", "SHIELDED SENTINEL MAJOR" },
	{ _forge_ai_group_sentinels, "sentinel_shielded", NULL, "SHIELDED SENTINEL" },
	{ _forge_ai_group_sentinels, "sentinel_defensive", NULL, "DEFENSIVE SENTINEL" },
	{ _forge_ai_group_sentinels, "sentinel", " major", "SENTINEL MAJOR" },
	{ _forge_ai_group_sentinels, "sentinel", NULL, "SENTINEL" },
};

/* a kind's tag not to arm it from when another is there */
static char const *const s_ai_poor_tags[] = { "unarmed", "airdef", "ranged", "melee", "cinematic" };

static char const *forge_ai_path_leaf(char const *path);

/* the kind of an actor's tag; FORGE_AI_KIND_OTHER for an actor of no kind (a
Custom Edition map's), FORGE_AI_KIND_HIDDEN for one not offered */
int forge_ai_kind_of(char const *path)
{
	char lower[FORGE_TAG_PATH_MAX];
	char const *leaf = forge_ai_path_leaf(path ? path : "");
	size_t i;
	int kind;

	for (i = 0; leaf[i] && i + 1 < sizeof(lower); i++)
		lower[i] = (char)tolower((unsigned char)leaf[i]);
	lower[i] = 0;
	for (kind = 0; kind < (int)NUMBEROF(s_ai_kinds); kind++)
	{
		struct forge_ai_kind const *entry = &s_ai_kinds[kind];

		if (!strncmp(lower, entry->prefix, strlen(entry->prefix)) && (!entry->also || strstr(lower, entry->also)))
			return entry->name ? kind : FORGE_AI_KIND_HIDDEN;
	}
	return FORGE_AI_KIND_OTHER;
}

char const *forge_ai_kind_name(int kind)
{
	return kind >= 0 && kind < (int)NUMBEROF(s_ai_kinds) ? s_ai_kinds[kind].name : NULL;
}

int forge_ai_kind_group(int kind)
{
	return kind >= 0 && kind < (int)NUMBEROF(s_ai_kinds) ? s_ai_kinds[kind].group : _forge_ai_group_other;
}

int forge_ai_group_count(void)
{
	return NUMBER_OF_FORGE_AI_GROUPS;
}

char const *forge_ai_group_name(int group)
{
	return group >= 0 && group < NUMBER_OF_FORGE_AI_GROUPS ? s_ai_group_names[group] : "";
}

/* FORGE_AI_RANDOM_PATH's or FORGE_AI_GROUP_PATH's group, else NONE */
static int forge_ai_path_group(char const *path, char const *prefix)
{
	size_t length = strlen(prefix);

	return path && !strncmp(path, prefix, length) ? atoi(path + length) : NONE;
}

int forge_ai_row_is_random(struct forge_catalog_entry const *entry)
{
	return forge_ai_path_group(entry->tag_path, FORGE_AI_RANDOM_PATH) != NONE;
}

int forge_ai_row_group(struct forge_catalog_entry const *entry)
{
	int group = forge_ai_path_group(entry->tag_path, FORGE_AI_RANDOM_PATH);

	if (group == NONE)
		group = forge_ai_path_group(entry->tag_path, FORGE_AI_GROUP_PATH);
	return group != NONE ? group : forge_ai_kind_group(forge_ai_kind_of(entry->tag_path));
}

int forge_ai_tag_is_poor(char const *path)
{
	int i;

	for (i = 0; i < (int)NUMBEROF(s_ai_poor_tags); i++)
	{
		if (strstr(path, s_ai_poor_tags[i]))
			return 1;
	}
	return 0;
}

/* a spawn of any of its group's ranks: its squad, and the ranks in the palette */
#define FORGE_AI_MAX_RANKS 8
struct forge_ai_random_squad
{
	struct squad_definition const *squad;
	short palette[FORGE_AI_MAX_RANKS];
	short count;
};

/* what the scenario plays in place of its own blocks */
static struct
{
	struct forge_ai_random_squad *randoms;
	int random_count;
	struct encounter_definition *encounters;
	struct squad_definition *squads;
	struct actor_starting_location *locations;
	struct tag_reference *palette;
	struct firing_position_definition *firing_positions;
	void *original_encounters;
	long original_encounter_count;
	void *original_palette;
	long original_palette_count;
	long weapons[FORGE_AI_MAX_WEAPONS];
	short weapon_count;
} s_built;

/* a saved map's spawns, read as its tags load: `fresh` until its AI starts */
static struct
{
	struct forge_ai_spawn_entry *entries;
	int count;
	int replace_own;
	int has_spawns;
	struct forge_ai_actor_entry *actors;
	int actor_count;
	int fresh;
	int applied;
} s_saved;


/* edit_state.ai_spawns stands for the level's spawns */
static int s_markers_scanned;

/* Forge mode: the AI stands still as models on its markers, none of it
playing, the encounters hidden from the engine so none respawn */
static int s_ai_held;
static struct scenario *s_held_scenario;
static long s_held_encounter_count;

static struct forge_ai_tag_instance *forge_ai_tag_instances(long *count)
{
	*count = 0;
	return (struct forge_ai_tag_instance *)cache_files_tag_instances(count);
}

void forge_ai_preview_update(int32_t live_idx);

static long forge_ai_tag_find(uint32_t group_tag, char const *path)
{
	long count, i;
	struct forge_ai_tag_instance *instances = forge_ai_tag_instances(&count);

	if (!path || !path[0])
		return NONE;
	for (i = 0; instances && i < count; i++)
	{
		if ((uint32_t)instances[i].group_tag == group_tag && instances[i].base_address && instances[i].name &&
			!_stricmp(instances[i].name, path))
		{
			return instances[i].tag_index;
		}
	}
	return NONE;
}

/* a tag a spawn names: read from Forge's pack, in a running Forge game, if the
level has not got it */
static long forge_ai_tag_need(uint32_t group_tag, char const *path)
{
	long tag = forge_ai_tag_find(group_tag, path);

	if (tag == NONE && path && path[0] && g_forge_session_enabled && g_forge.live_instance_count > 0)
		tag = forge_resolve_engine_tag(group_tag, path, NULL);
	return tag;
}

static struct encounter_definition *forge_ai_original_encounters(struct scenario const *scen, long *count)
{
	if (s_built.encounters && scen->ai_encounters.address == s_built.encounters)
	{
		*count = s_built.original_encounter_count;
		return (struct encounter_definition *)s_built.original_encounters;
	}
	*count = scen == s_held_scenario ? s_held_encounter_count : scen->ai_encounters.count;
	return (struct encounter_definition *)scen->ai_encounters.address;
}

/* the map's own encounters, whatever the level's AI has made of them: Firefight
finds its spawn points in them, played or not */
struct encounter_definition *forge_ai_map_encounters(long *count)
{
	struct scenario *scen = global_scenario_try_and_get();

	*count = 0;
	return scen ? forge_ai_original_encounters(scen, count) : NULL;
}

static struct tag_reference *forge_ai_original_palette(struct scenario const *scen, long *count)
{
	if (s_built.palette && scen->ai_actor_palette.address == s_built.palette)
	{
		*count = s_built.original_palette_count;
		return (struct tag_reference *)s_built.original_palette;
	}
	*count = scen->ai_actor_palette.count;
	return (struct tag_reference *)scen->ai_actor_palette.address;
}

/* the map's own blocks back, and what stood in for them gone */
static void forge_ai_scenario_restore(struct scenario *scen)
{
	if (s_built.encounters && scen->ai_encounters.address == s_built.encounters)
	{
		scen->ai_encounters.address = s_built.original_encounters;
		scen->ai_encounters.count = s_built.original_encounter_count;
	}
	if (s_built.palette && scen->ai_actor_palette.address == s_built.palette)
	{
		scen->ai_actor_palette.address = s_built.original_palette;
		scen->ai_actor_palette.count = s_built.original_palette_count;
	}
	free(s_built.randoms);
	free(s_built.encounters);
	free(s_built.squads);
	free(s_built.locations);
	free(s_built.palette);
	free(s_built.firing_positions);
	memset(&s_built, 0, sizeof(s_built));
}

int forge_ai_map_editable(void)
{
	struct scenario *scen = global_scenario_try_and_get();

	return scen && scen->type == _scenario_type_multiplayer;
}

int forge_ai_map_supported(void)
{
	struct scenario *scen = global_scenario_try_and_get();
	struct encounter_definition *encounters;
	long count = 0, i;

	if (!forge_ai_map_editable())
		return 0;
	encounters = forge_ai_original_encounters(scen, &count);
	for (i = 0; encounters && i < count; i++)
	{
		if (encounters[i].squads.count > 0)
			return 1;
	}
	return 0;
}

static unsigned int forge_ai_entry_modes(struct forge_ai_spawn_entry const *entry)
{
	unsigned int flags = entry->gametype_flags & ~(unsigned int)FORGE_AI_RANDOM_RANK;

	return flags ? flags : FORGE_AI_MODES_NOT_FIREFIGHT;
}

static int forge_ai_entry_plays(struct forge_ai_spawn_entry const *entry)
{
	unsigned int flags = forge_ai_entry_modes(entry);
	long engine;

	if (!entry->active)
		return 0;
	/* (a Forge game plays them all, to edit them) */
	if (!game_engine_running() || (g_forge_session_enabled && !firefight_active()))
		return 1;
	engine = game_engine_get_variant()->game_engine_index;
	return engine < first_usable_game_engine_index || ((flags >> (engine - first_usable_game_engine_index)) & 1u) != 0;
}

/* a spawn with a squad of its own */
static int forge_ai_entry_own_squad(struct forge_ai_spawn_entry const *entry)
{
	return entry->encounter_index < 0 || entry->detached || (entry->gametype_flags & FORGE_AI_RANDOM_RANK);
}

static short forge_ai_entry_team(struct forge_ai_spawn_entry const *entry, long variant)
{
	if (entry->team == _game_team_default)
	{
		struct actor_variant_definition *definition = (struct actor_variant_definition *)tag_get('actv', variant);

		if (definition->unit_reference.index != NONE)
			return unit_definition_get(definition->unit_reference.index)->unit.default_team;
	}
	return entry->team;
}

/* the map's encounter with a firing position near a point, nearest first,
NONE with none */
static short forge_ai_nearest_encounter(
	struct encounter_definition const *encounters,
	long count,
	struct forge_ai_spawn_entry const *entry)
{
	short nearest = NONE;
	float nearest_distance = FORGE_AI_NEAR_FIRING_POSITION * FORGE_AI_NEAR_FIRING_POSITION;
	long e, p;

	for (e = 0; e < count; e++)
	{
		struct firing_position_definition const *positions =
			(struct firing_position_definition const *)encounters[e].firing_positions.address;

		for (p = 0; p < encounters[e].firing_positions.count; p++)
		{
			float dx = positions[p].position.x - entry->x;
			float dy = positions[p].position.y - entry->y;
			float dz = positions[p].position.z - entry->z;
			float distance = dx * dx + dy * dy + dz * dz;

			if (distance < nearest_distance)
			{
				nearest = (short)e;
				nearest_distance = distance;
			}
		}
	}
	return nearest;
}

/* firing positions for a spawn on a map with none near it: on rings around
it, where there is ground an actor could reach in a straight line. The level's
structure is there as its AI starts (scenario_load) */
static void forge_ai_firing_positions_add(
	struct forge_ai_added_encounter *added,
	struct forge_ai_spawn_entry const *entry)
{
	static float const radii[] = { 0.0f, 3.0f, 6.0f, 10.0f };
	static short const counts[] = { 1, 6, 8, 10 };
	unsigned long flags = FLAG(_collision_test_structure_bit);
	short ring, spot;

	for (ring = 0; ring < (short)NUMBEROF(radii); ring++)
	{
		for (spot = 0; spot < counts[ring]; spot++)
		{
			float angle = (float)spot * (6.283185307f / (float)counts[ring]) + (float)ring * 0.4f;
			float x = entry->x + radii[ring] * (float)cos((double)angle);
			float y = entry->y + radii[ring] * (float)sin((double)angle);
			struct firing_position_definition *position;
			struct collision_result collision;
			struct location location;
			real_point3d start;
			real_vector3d vector;

			start.x = entry->x;
			start.y = entry->y;
			start.z = entry->z + 0.6f;
			vector.i = x - entry->x;
			vector.j = y - entry->y;
			vector.k = 0.0f;
			if (radii[ring] > 0.0f && collision_test_vector(flags, &start, &vector, NONE, &collision))
				continue;
			start.x = x;
			start.y = y;
			start.z = entry->z + 2.0f;
			vector.i = 0.0f;
			vector.j = 0.0f;
			vector.k = -6.0f;
			if (!collision_test_vector(flags, &start, &vector, NONE, &collision) ||
				collision.plane.n.k < 0.7f || collision.surface_index == NONE)
			{
				continue;
			}
			scenario_location_from_point(&location, &collision.point);
			if (location.cluster_index == NONE)
				continue;
			position = &added->firing_positions[added->firing_position_count++];
			memset(position, 0, sizeof(*position));
			position->position = collision.point;
			position->group_index = 0;
			position->cluster_index = location.cluster_index;
			position->surface_index = collision.surface_index;
		}
	}
}

/* the squad of an encounter with a starting location nearest a point: what a
new squad there copies its firing position groups and manner from */
static struct squad_definition const *forge_ai_nearest_squad(
	struct encounter_definition const *encounter,
	struct forge_ai_spawn_entry const *entry)
{
	struct squad_definition const *squads = (struct squad_definition const *)encounter->squads.address;
	struct squad_definition const *nearest = encounter->squads.count > 0 ? &squads[0] : NULL;
	float nearest_distance = -1.0f;
	long s, l;

	for (s = 0; s < encounter->squads.count; s++)
	{
		struct actor_starting_location const *locations =
			(struct actor_starting_location const *)squads[s].starting_locations.address;

		for (l = 0; l < squads[s].starting_locations.count; l++)
		{
			float dx = locations[l].position.x - entry->x;
			float dy = locations[l].position.y - entry->y;
			float dz = locations[l].position.z - entry->z;
			float distance = dx * dx + dy * dy + dz * dz;

			if (nearest_distance < 0.0f || distance < nearest_distance)
			{
				nearest = &squads[s];
				nearest_distance = distance;
			}
		}
	}
	return nearest;
}

static short forge_ai_palette_index(struct tag_reference *palette, long *count, long capacity, long variant)
{
	long i;

	for (i = 0; i < *count; i++)
	{
		if (palette[i].index == variant)
			return (short)i;
	}
	if (*count >= capacity)
		return NONE;
	memset(&palette[*count], 0, sizeof(palette[*count]));
	palette[*count].group_tag = 'actv';
	palette[*count].name = (char *)tag_get_name(variant);
	palette[*count].name_length = (long)strlen(palette[*count].name);
	palette[*count].index = variant;
	return (short)(*count)++;
}

/* an entry's weapon, as a starting location's flag bits carry it */
static byte forge_ai_location_weapon_bits(struct forge_ai_spawn_entry const *entry)
{
	long weapon = forge_ai_tag_need('weap', entry->weapon_path);
	short i;

	if (weapon == NONE)
		return 0;
	for (i = 0; i < s_built.weapon_count; i++)
	{
		if (s_built.weapons[i] == weapon)
			return (byte)((i + 1) << FORGE_AI_LOCATION_WEAPON_SHIFT);
	}
	if (s_built.weapon_count >= FORGE_AI_MAX_WEAPONS)
		return 0;
	s_built.weapons[s_built.weapon_count++] = weapon;
	return (byte)(s_built.weapon_count << FORGE_AI_LOCATION_WEAPON_SHIFT);
}

static void forge_ai_location_write(
	struct actor_starting_location *location,
	struct forge_ai_spawn_entry const *entry,
	short palette_index)
{
	location->position.x = entry->x;
	location->position.y = entry->y;
	location->position.z = entry->z;
	location->facing = entry->yaw;
	location->actor_variant_index = palette_index;
	location->flags = (byte)((location->flags & FORGE_AI_LOCATION_OWN_FLAGS) | forge_ai_location_weapon_bits(entry));
}

/* the spawns the level plays, NULL for the map's own AI as it stands */
static struct forge_ai_spawn_entry const *forge_ai_level_spawns(int *count, int *replace_own)
{
	if (g_forge_session_enabled && s_markers_scanned && g_forge.live_instance_count > 0)
	{
		if (!g_forge.edit_state.ai_spawns_edited)
			return NULL;
		*count = g_forge.edit_state.ai_spawn_count;
		*replace_own = g_forge.edit_state.replace_ai_spawns;
		return g_forge.edit_state.ai_spawns;
	}
	s_markers_scanned = 0;
	s_saved.applied = s_saved.fresh;
	s_saved.fresh = 0;
	if (!s_saved.applied)
		s_saved.actor_count = 0;
	if (!s_saved.applied || !s_saved.has_spawns)
		return NULL;
	*count = s_saved.count;
	*replace_own = s_saved.replace_own;
	return s_saved.entries;
}

/* the group's ranks the level has, one tag of each, as a spawn's squad's */
static void forge_ai_random_tags_need(int group)
{
	int i;

	for (i = 0; i < g_forge.catalog_count; i++)
	{
		struct forge_catalog_entry const *row = &g_forge.catalog[i];

		if (row->group_tag == FORGE_GROUP_AI_SPAWN && !forge_ai_row_is_random(row) && forge_ai_row_group(row) == group)
			forge_ai_tag_need('actv', row->tag_path);
	}
}

static void forge_ai_random_squad_add(
	struct squad_definition const *squad,
	struct forge_ai_spawn_entry const *entry,
	long *palette_count,
	long palette_capacity)
{
	struct forge_ai_random_squad *random = &s_built.randoms[s_built.random_count];
	int group = forge_ai_kind_group(forge_ai_kind_of(entry->actor_variant_path));
	int kinds[FORGE_AI_MAX_RANKS];
	long count, i;
	struct forge_ai_tag_instance *instances;

	forge_ai_random_tags_need(group);
	instances = forge_ai_tag_instances(&count);
	random->squad = squad;
	random->count = 0;
	for (i = 0; instances && i < count && random->count < FORGE_AI_MAX_RANKS; i++)
	{
		int kind, seen;

		if ((uint32_t)instances[i].group_tag != 'actv' || !instances[i].name || !instances[i].base_address)
			continue;
		kind = forge_ai_kind_of(instances[i].name);
		if (kind < 0 || forge_ai_kind_group(kind) != group || forge_ai_tag_is_poor(instances[i].name))
			continue;
		for (seen = 0; seen < random->count && kinds[seen] != kind; seen++)
		{
		}
		if (seen < random->count)
			continue;
		kinds[random->count] = kind;
		random->palette[random->count++] = forge_ai_palette_index(s_built.palette, palette_count, palette_capacity,
			instances[i].tag_index);
	}
	if (random->count > 1)
		s_built.random_count++;
}

/* the actor a squad's next unit is: a random spawn's any of its ranks, each as
likely (encounters.c) */
short forge_ai_squad_palette_index(void const *squad, short palette_index)
{
	int i;

	for (i = 0; i < s_built.random_count; i++)
	{
		if (s_built.randoms[i].squad == squad)
		{
			return s_built.randoms[i].palette[seed_random_range(get_global_random_seed_address(), 0,
				s_built.randoms[i].count)];
		}
	}
	return palette_index;
}

static void forge_ai_encounters_build(void)
{
	struct scenario *scen = global_scenario_try_and_get();
	struct forge_ai_spawn_entry const *entries;
	struct encounter_definition *encounters;
	struct tag_reference *original_palette;
	struct forge_ai_added_encounter added[FORGE_AI_MAX_ADDED_ENCOUNTERS];
	short *entry_added = NULL;
	long *entry_variant = NULL;
	long original_count = 0, original_palette_count = 0, palette_count;
	long palette_room;
	long own_squad_count = 0, squad_cursor = 0, location_cursor = 0, made_squads = 0, firing_cursor = 0;
	int entry_count = 0, replace_own = 0, added_count = 0;
	int firefight = firefight_active();
	long e, s;
	int i;

	if (!scen)
		return;
	forge_ai_scenario_restore(scen);
	encounters = (struct encounter_definition *)scen->ai_encounters.address;
	original_count = scen->ai_encounters.count;
	original_palette = (struct tag_reference *)scen->ai_actor_palette.address;
	original_palette_count = scen->ai_actor_palette.count;
	entries = forge_ai_level_spawns(&entry_count, &replace_own);
	/* (a map that uses no AI is left alone, and a campaign level always is) */
	if (scen->type != _scenario_type_multiplayer || (!entries && (!firefight || original_count == 0)))
		return;
	/* (Firefight has none of the map's AI, only spawns set to play in it: its
	squads stay, empty, for the map's scripts and the encounters' indices) */
	if (firefight)
		replace_own = 1;

	/* (each spawn's actor, and a random one's ranks) */
	palette_room = (long)entry_count * (FORGE_AI_MAX_RANKS + 1);
	for (e = 0; e < original_count; e++)
		own_squad_count += encounters[e].squads.count;
	s_built.encounters = (struct encounter_definition *)calloc(
		(size_t)(original_count + FORGE_AI_MAX_ADDED_ENCOUNTERS), sizeof(*s_built.encounters));
	s_built.squads = (struct squad_definition *)calloc((size_t)(own_squad_count + entry_count + 1), sizeof(*s_built.squads));
	s_built.locations = (struct actor_starting_location *)calloc((size_t)(entry_count + 1), sizeof(*s_built.locations));
	s_built.palette = (struct tag_reference *)calloc((size_t)(original_palette_count + palette_room + 1), sizeof(*s_built.palette));
	s_built.randoms = (struct forge_ai_random_squad *)calloc((size_t)(entry_count + 1), sizeof(*s_built.randoms));
	s_built.random_count = 0;
	entry_added = (short *)malloc((size_t)(entry_count + 1) * sizeof(*entry_added));
	entry_variant = (long *)malloc((size_t)(entry_count + 1) * sizeof(*entry_variant));
	if (!s_built.encounters || !s_built.squads || !s_built.locations || !s_built.palette || !s_built.randoms ||
		!entry_added || !entry_variant)
	{
		platform_log("Forge: out of memory for the level's AI spawns");
		forge_ai_scenario_restore(scen);
		free(entry_added);
		free(entry_variant);
		return;
	}
	if (original_palette_count > 0)
		memcpy(s_built.palette, original_palette, (size_t)original_palette_count * sizeof(*s_built.palette));
	palette_count = original_palette_count;

	/* which spawns play, and the added encounters theirs go in */
	for (i = 0; i < entry_count; i++)
	{
		struct forge_ai_spawn_entry const *entry = &entries[i];
		short team, source;
		int a;

		entry_added[i] = NONE;
		entry_variant[i] = NONE;
		if (!forge_ai_entry_plays(entry))
			continue;
		entry_variant[i] = forge_ai_tag_need('actv', entry->actor_variant_path);
		if (entry_variant[i] == NONE)
		{
			platform_log("Forge: an AI spawn's actor is not in this level: %s", entry->actor_variant_path);
			continue;
		}
		if (!forge_ai_entry_own_squad(entry))
			continue;
		team = forge_ai_entry_team(entry, entry_variant[i]);
		source = forge_ai_nearest_encounter(encounters, original_count, entry);
		for (a = 0; a < added_count; a++)
		{
			if (added[a].team == team && added[a].source_encounter == source &&
				added[a].squad_count < (source == NONE ? FORGE_AI_MADE_SQUADS_PER_ENCOUNTER : MAXIMUM_SQUADS_PER_ENCOUNTER))
			{
				break;
			}
		}
		if (a == added_count)
		{
			if (added_count >= FORGE_AI_MAX_ADDED_ENCOUNTERS)
				continue;
			memset(&added[a], 0, sizeof(added[a]));
			added[a].team = team;
			added[a].source_encounter = source;
			added_count++;
		}
		added[a].squad_count++;
		added[a].respawns = added[a].respawns || entry->respawn_alive > 0;
		entry_added[i] = (short)a;
		if (source == NONE)
			made_squads++;
	}
	if (made_squads > 0)
	{
		s_built.firing_positions = (struct firing_position_definition *)calloc(
			(size_t)made_squads * FORGE_AI_FIRING_POSITIONS_PER_SPAWN, sizeof(*s_built.firing_positions));
		if (!s_built.firing_positions)
		{
			platform_log("Forge: out of memory for the level's AI spawns");
			forge_ai_scenario_restore(scen);
			free(entry_added);
			free(entry_variant);
			return;
		}
	}

	/* the map's encounters and squads, in their places */
	if (original_count > 0)
		memcpy(s_built.encounters, encounters, (size_t)original_count * sizeof(*s_built.encounters));
	if (replace_own)
	{
		for (e = 0; e < original_count; e++)
		{
			struct squad_definition *squads = &s_built.squads[squad_cursor];

			if (encounters[e].squads.count <= 0)
				continue;
			memcpy(squads, encounters[e].squads.address, (size_t)encounters[e].squads.count * sizeof(*squads));
			s_built.encounters[e].squads.address = squads;
			squad_cursor += encounters[e].squads.count;
			for (s = 0; s < encounters[e].squads.count; s++)
			{
				struct squad_definition *squad = &squads[s];
				struct actor_starting_location const *own_locations =
					(struct actor_starting_location const *)squad->starting_locations.address;
				long own_location_count = squad->starting_locations.count;
				struct actor_starting_location *locations = &s_built.locations[location_cursor];
				long count = 0;

				for (i = 0; i < entry_count; i++)
				{
					struct forge_ai_spawn_entry const *entry = &entries[i];

					if (entry->encounter_index != e || entry->squad_index != s || entry_variant[i] == NONE ||
						forge_ai_entry_own_squad(entry) || entry->location_index < 0 ||
						entry->location_index >= own_location_count)
					{
						continue;
					}
					locations[count] = own_locations[entry->location_index];
					{
						short palette_index = forge_ai_palette_index(s_built.palette, &palette_count,
							original_palette_count + palette_room, entry_variant[i]);

						/* (NONE: the squad's own actor) */
						forge_ai_location_write(&locations[count], entry,
							palette_index == squad->actor_palette_index ? (short)NONE : palette_index);
					}
					count++;
				}
				location_cursor += count;
				/* (the rank chosen is the rank placed: no chance of the next one up) */
				if (count > 0)
					squad->major_upgrade = _actor_major_upgrade_none;
				squad->starting_locations.address = locations;
				squad->starting_locations.count = count;
				if (count == 0)
				{
					squad->min_count = 0;
					squad->max_count = 0;
					squad->respawn_min_actors = 0;
					squad->respawn_max_actors = 0;
				}
			}
		}
	}

	/* then the added encounters, each spawn with a squad of its own */
	for (i = 0; i < added_count; i++)
	{
		struct forge_ai_added_encounter *a = &added[i];
		struct encounter_definition *encounter = &s_built.encounters[original_count + i];

		a->squads = &s_built.squads[squad_cursor];
		squad_cursor += a->squad_count;
		if (a->source_encounter != NONE)
		{
			*encounter = encounters[a->source_encounter];
		}
		else
		{
			memset(encounter, 0, sizeof(*encounter));
			encounter->version = 1;
			a->firing_positions = &s_built.firing_positions[firing_cursor];
			firing_cursor += (long)a->squad_count * FORGE_AI_FIRING_POSITIONS_PER_SPAWN;
		}
		snprintf(encounter->name, sizeof(encounter->name), "forge_ai_%d", i);
		encounter->team_index = a->team;
		encounter->flags &= ~(unsigned long)(1 << 0); /* (created with the level) */
		if (a->respawns)
			encounter->flags |= 1 << 1; /* (respawning, each squad's delay alone) */
		encounter->respawn_time_lower_bound = 0.0f;
		encounter->respawn_time_upper_bound = 0.0f;
		encounter->squads.address = a->squads;
		encounter->squads.count = a->squad_count;
		encounter->platoons.address = NULL;
		encounter->platoons.count = 0;
	}
	for (i = 0; i < entry_count; i++)
	{
		struct forge_ai_spawn_entry const *entry = &entries[i];
		struct forge_ai_added_encounter *a;
		struct squad_definition const *pattern;
		struct squad_definition *squad;
		struct actor_starting_location *location;
		short palette_index;

		if (entry_added[i] == NONE)
			continue;
		a = &added[entry_added[i]];
		pattern = a->source_encounter != NONE ? forge_ai_nearest_squad(&encounters[a->source_encounter], entry) : NULL;
		palette_index = forge_ai_palette_index(s_built.palette, &palette_count,
			original_palette_count + palette_room, entry_variant[i]);
		squad = &a->squads[a->squads_filled];
		location = &s_built.locations[location_cursor++];
		if (pattern)
		{
			*squad = *pattern;
		}
		else
		{
			short group;

			memset(squad, 0, sizeof(*squad));
			squad->initial_state = FORGE_AI_SQUAD_STATE;
			squad->default_state = FORGE_AI_SQUAD_STATE;
			squad->flags = FLAG(_squad_timer_starts_immediately_bit);
			for (group = 0; group < FORGE_AI_SQUAD_POSITION_GROUP_COUNT; group++)
				squad->firing_position_groups[group] = FORGE_AI_SQUAD_POSITION_GROUPS;
			forge_ai_firing_positions_add(a, entry);
		}
		snprintf(squad->name, sizeof(squad->name), "forge_ai_%d", a->squads_filled);
		a->squads_filled++;
		squad->actor_palette_index = palette_index;
		/* (the rank chosen is the rank placed: no chance of the next one up) */
		squad->major_upgrade = _actor_major_upgrade_none;
		if (entry->gametype_flags & FORGE_AI_RANDOM_RANK)
			forge_ai_random_squad_add(squad, entry, &palette_count, original_palette_count + palette_room);
		squad->platoon_index = NONE;
		squad->maneuver_squad_index = NONE;
		squad->flags &= ~(unsigned long)FLAG(_squad_delay_forever_bit);
		squad->squad_delay_timer = 0.0f;
		squad->min_count = entry->unit_count;
		squad->max_count = entry->unit_count;
		/* (none at once: each waits out the delay) */
		squad->respawn_min_actors = 0;
		squad->respawn_max_actors = entry->respawn_alive;
		squad->respawn_total_count = entry->respawn_total;
		squad->respawn_time_lower_bound = (real)entry->respawn_delay_seconds;
		squad->respawn_time_upper_bound = (real)entry->respawn_delay_seconds;
		memset(location, 0, sizeof(*location));
		location->cluster_index = NONE;
		location->noncombat_sequence_id = NONE;
		location->default_state = squad->default_state;
		location->initial_state = squad->initial_state;
		location->command_list_index = NONE;
		forge_ai_location_write(location, entry, NONE);
		squad->starting_locations.address = location;
		squad->starting_locations.count = 1;
	}
	for (i = 0; i < added_count; i++)
	{
		if (added[i].source_encounter == NONE)
		{
			s_built.encounters[original_count + i].firing_positions.address = added[i].firing_positions;
			s_built.encounters[original_count + i].firing_positions.count = added[i].firing_position_count;
		}
	}
	free(entry_added);
	free(entry_variant);

	s_built.original_encounters = encounters;
	s_built.original_encounter_count = original_count;
	s_built.original_palette = original_palette;
	s_built.original_palette_count = original_palette_count;
	scen->ai_encounters.address = s_built.encounters;
	scen->ai_encounters.count = original_count + added_count;
	scen->ai_actor_palette.address = s_built.palette;
	scen->ai_actor_palette.count = palette_count;
}

/* (from ai_initialize_for_new_map, the game's type known, every tag in) */
void forge_ai_encounters_apply(void)
{
	struct scenario *scen = global_scenario_try_and_get();

	if (!scen)
		return;
	/* (a new level is never held, and the count kept was the last one's) */
	if (!g_forge_session_enabled || g_forge.live_instance_count == 0)
	{
		s_ai_held = 0;
		s_held_scenario = NULL;
	}
	else if (s_held_scenario == scen)
	{
		scen->ai_encounters.count = s_held_encounter_count;
		s_held_scenario = NULL;
	}
	forge_ai_encounters_build();
	if (s_ai_held)
	{
		s_held_scenario = scen;
		s_held_encounter_count = scen->ai_encounters.count;
		scen->ai_encounters.count = 0;
	}
}

/* ---------- how a spawn's units appear

Each effect's tag is the map's or the campaign's, by these names (all twelve
multiplayer maps have the same effects: those need no map here). The others
are copied in as a spawn's actors are: forge_ai_spawn_effect_wanted for an
online game, forge_ai_saved_tags_collect for a saved map. A level without one
shows the teleport, which every map has */
#define FORGE_AI_EFFECT_TELEPORT "effects\\coop teleport"

enum
{
	_forge_ai_effect_none = 0,
	_forge_ai_effect_teleport,
	_forge_ai_effect_cloak,
};

static struct
{
	char const *label;
	char const *path; /* NULL: not a tag */
	char const *map; /* the campaign level with it, NULL: every map has it */
} const s_ai_spawn_effects[] =
{
	{ "NONE", NULL, NULL },
	{ "TELEPORT", FORGE_AI_EFFECT_TELEPORT, NULL },
	{ "CLOAK", NULL, NULL },
	{ "PLASMA BLAST", "weapons\\plasma grenade\\effects\\explosion", NULL },
	{ "FLOOD BURST", SKULLS_OUTBREAK_BURST_TAG, "c20" },
};

int forge_ai_spawn_effect_wanted(char const *name)
{
	int i;

	for (i = 0; i < (int)NUMBEROF(s_ai_spawn_effects); i++)
	{
		if (s_ai_spawn_effects[i].map && !_stricmp(s_ai_spawn_effects[i].path, name))
			return 1;
	}
	return 0;
}

void forge_ai_spawn_effect_play(int effect, union real_point3d const *point)
{
	char const *marker_names[1];
	real_point3d marker_points[1];
	real_vector3d marker_forwards[1];
	long definition;

	if (effect < 0 || effect >= (int)NUMBEROF(s_ai_spawn_effects) || !s_ai_spawn_effects[effect].path)
		return;
	definition = forge_ai_tag_find('effe', s_ai_spawn_effects[effect].path);
	if (definition == NONE)
		definition = forge_ai_tag_find('effe', FORGE_AI_EFFECT_TELEPORT);
	if (definition == NONE)
		return;
	marker_names[0] = "";
	marker_points[0] = *point;
	marker_forwards[0] = *global_up3d;
	/* (it hurts no one, and leaves nothing to trip over) */
	skulls_effect_harmless = TRUE;
	skulls_effect_no_objects = TRUE;
	effect_new_unattached_from_markers(definition, NONE, NULL, 1, marker_names, marker_points, marker_forwards,
		0.0f, 0.0f, NULL, NULL, TRUE);
	skulls_effect_harmless = FALSE;
	skulls_effect_no_objects = FALSE;
}

/* ---------- a spawn's units as they start

The first at its point, the next around it in a cross and its corners, north
being the way the spawn faces, then the same a step further out. A place in a
wall or with no ground under it is the point's. Its respawns are all the
point's (encounter_place_actor's `spawning`) */
#define FORGE_AI_GRID_SPACING 0.8f
#define FORGE_AI_GRID_PLACES 16

static signed char const s_ai_grid[FORGE_AI_GRID_PLACES][2] =
{
	/* north, east */
	{ 0, 0 },
	{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
	{ 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 },
	{ 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 },
	{ 2, 2 }, { -2, 2 }, { -2, -2 },
};

/* (what the level's ground gave, kept for the preview, which asks each frame) */
struct forge_ai_grid_layout
{
	float x, y, z, yaw;
	short count;
	union real_point3d points[FORGE_AI_GRID_PLACES];
};
static struct forge_ai_grid_layout *s_ai_grid_layouts;
/* the next unit of each spawn to start: a spawn starts them all again each time
the AI does, so this goes round */
static short s_ai_grid_next[FORGE_MAX_AI_SPAWNS];
/* the effect of the unit being placed */
static int s_ai_placing_effect;

static void forge_ai_grid_place(struct forge_ai_spawn_entry const *entry, int index, union real_point3d *point)
{
	unsigned long const flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit);
	float north = (float)s_ai_grid[index][0] * FORGE_AI_GRID_SPACING;
	float east = (float)s_ai_grid[index][1] * FORGE_AI_GRID_SPACING;
	float forward_x = (float)cos((double)entry->yaw), forward_y = (float)sin((double)entry->yaw);
	real_point3d start;
	real_vector3d vector;
	struct collision_result collision;

	point->x = entry->x;
	point->y = entry->y;
	point->z = entry->z;
	if (!index)
		return;
	/* (a clear way there at waist height, then the ground) */
	start.x = entry->x;
	start.y = entry->y;
	start.z = entry->z + 0.5f;
	vector.i = forward_x * north + forward_y * east;
	vector.j = forward_y * north - forward_x * east;
	vector.k = 0.0f;
	memset(&collision, 0, sizeof(collision));
	if (collision_test_vector(flags, &start, &vector, NONE, &collision))
		return;
	start.x += vector.i;
	start.y += vector.j;
	vector.i = 0.0f;
	vector.j = 0.0f;
	vector.k = -2.0f;
	memset(&collision, 0, sizeof(collision));
	if (!collision_test_vector(flags, &start, &vector, NONE, &collision))
		return;
	point->x = start.x;
	point->y = start.y;
	point->z = collision.point.z + 0.05f;
}

static struct forge_ai_grid_layout const *forge_ai_grid_layout(struct forge_ai_spawn_entry const *entry, int slot)
{
	struct forge_ai_grid_layout *layout;
	int i;

	if (!s_ai_grid_layouts)
		s_ai_grid_layouts = (struct forge_ai_grid_layout *)calloc(FORGE_MAX_AI_SPAWNS, sizeof(*s_ai_grid_layouts));
	if (!s_ai_grid_layouts || slot < 0 || slot >= FORGE_MAX_AI_SPAWNS)
		return NULL;
	layout = &s_ai_grid_layouts[slot];
	if (layout->x != entry->x || layout->y != entry->y || layout->z != entry->z || layout->yaw != entry->yaw ||
		layout->count != entry->unit_count)
	{
		layout->x = entry->x;
		layout->y = entry->y;
		layout->z = entry->z;
		layout->yaw = entry->yaw;
		layout->count = entry->unit_count;
		for (i = 0; i < entry->unit_count && i < FORGE_AI_GRID_PLACES; i++)
			forge_ai_grid_place(entry, i, &layout->points[i]);
	}
	return layout;
}

/* the spawns the level is playing, as forge_ai_level_spawns chose them. That is
asked once, as the AI is built: it uses up what a saved map brought */
static struct forge_ai_spawn_entry const *forge_ai_playing_spawns(int *count)
{
	if (g_forge_session_enabled && s_markers_scanned && g_forge.live_instance_count > 0)
	{
		if (!g_forge.edit_state.ai_spawns_edited)
			return NULL;
		*count = g_forge.edit_state.ai_spawn_count;
		return g_forge.edit_state.ai_spawns;
	}
	if (!s_saved.applied || !s_saved.has_spawns)
		return NULL;
	*count = s_saved.count;
	return s_saved.entries;
}

/* (from encounter_place_actor) where a unit of a squad starts: the location, or
`place`, a copy of it where the spawn's layout has this one */
struct actor_starting_location *forge_ai_location_for_unit(
	struct actor_starting_location *location,
	boolean spawning,
	struct actor_starting_location *place)
{
	int count = 0, i;
	struct forge_ai_spawn_entry const *entries = forge_ai_playing_spawns(&count);
	struct forge_ai_grid_layout const *layout;
	int unit;

	s_ai_placing_effect = _forge_ai_effect_none;
	if (!entries)
		return location;
	for (i = 0; i < count && i < FORGE_MAX_AI_SPAWNS; i++)
	{
		if (entries[i].active && entries[i].x == location->position.x && entries[i].y == location->position.y &&
			entries[i].z == location->position.z)
		{
			break;
		}
	}
	if (i == count || i == FORGE_MAX_AI_SPAWNS)
		return location;
	s_ai_placing_effect = entries[i].spawn_effect;
	if (spawning || entries[i].unit_count < 2 || !forge_ai_entry_own_squad(&entries[i]))
		return location;
	unit = s_ai_grid_next[i] % entries[i].unit_count;
	s_ai_grid_next[i] = (short)(unit + 1);
	layout = forge_ai_grid_layout(&entries[i], i);
	if (!unit || unit >= FORGE_AI_GRID_PLACES || !layout)
		return location;
	*place = *location;
	place->position.x = layout->points[unit].x;
	place->position.y = layout->points[unit].y;
	place->position.z = layout->points[unit].z;
	return place;
}

static void forge_ai_weapon_give(long unit_index, long weapon_definition)
{
	struct object_placement_data placement;
	long weapon_index;

	if (!unit_can_hold_weapon_definition(unit_index, weapon_definition))
		return;
	object_placement_data_new(&placement, weapon_definition, unit_index);
	weapon_index = object_new(&placement);
	if (weapon_index != NONE && !unit_add_weapon_to_inventory(unit_index, weapon_index, _unit_add_weapon_replace))
		object_delete(weapon_index);
}

/* (from actor_place, the unit armed as its actor is) */
void forge_ai_unit_placed(long unit_index, struct actor_starting_location const *location)
{
	short slot = (short)(location->flags >> FORGE_AI_LOCATION_WEAPON_SHIFT);

	if (slot > 0 && slot <= s_built.weapon_count)
		forge_ai_weapon_give(unit_index, s_built.weapons[slot - 1]);
	/* (not as the level loads: there is no one to see it, and no clients yet) */
	if (s_ai_placing_effect != _forge_ai_effect_none && game_time_get() > 0)
	{
		if (s_ai_placing_effect == _forge_ai_effect_cloak)
		{
			struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);

			/* (without the flag it fades, in the four seconds the engine takes) */
			if (unit)
				unit->unit.active_camouflage = 1.0f;
		}
		else
		{
			union real_point3d origin;

			object_get_origin(unit_index, &origin);
			forge_ai_spawn_effect_play(s_ai_placing_effect, &origin);
			forge_network_send_spawn_effect(s_ai_placing_effect, &origin);
		}
	}
	s_ai_placing_effect = _forge_ai_effect_none;
}

/* any weapon in any AI's hands: its animations are borrowed, as the campaign
randomizer's crazy weapons do (randomizer_borrowed_weapon_label), in a level
with spawns armed, and while they stand as models in Forge */
int forge_ai_weapons_borrowed(void)
{
	return s_built.weapon_count > 0 || (g_forge_session_enabled && forge_ai_map_editable());
}

/* the animations of the weapon a unit's actor carries, for a weapon none of
the labels nearest it fit: what any armed AI can always hold something with */
char const *forge_ai_unit_own_weapon_label(long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	long count, i;
	struct forge_ai_tag_instance *instances = forge_ai_tag_instances(&count);

	if (!unit)
		return NULL;
	for (i = 0; instances && i < count; i++)
	{
		struct actor_variant_definition *variant;

		if ((uint32_t)instances[i].group_tag != 'actv' || !instances[i].base_address)
			continue;
		variant = (struct actor_variant_definition *)tag_get('actv', instances[i].tag_index);
		if (variant->unit_reference.index == unit->definition_index &&
			variant->ranged_combat.reference.index != NONE)
		{
			return weapon_definition_get(variant->ranged_combat.reference.index)->weapon.label;
		}
	}
	return NULL;
}

/* ---------- saved spawns */

static void forge_ai_saved_tags_parse(char const *text, char const *line, uint32_t group_tag)
{
	char const *cur = text;
	size_t line_size = strlen(line);

	while (s_saved.actors && (cur = strstr(cur, line)) != NULL && s_saved.actor_count < FORGE_MAX_AI_ACTORS)
	{
		struct forge_ai_actor_entry *actor = &s_saved.actors[s_saved.actor_count];

		cur += line_size;
		memset(actor, 0, sizeof(*actor));
		actor->group_tag = group_tag;
		if (sscanf(cur, "%31[^|]|%255[^\r\n]", actor->map, actor->path) == 2)
			s_saved.actor_count++;
	}
}

void forge_ai_saved_parse(char const *text)
{
	char const *cur;
	int replace_own = 0;

	s_saved.count = 0;
	s_saved.actor_count = 0;
	s_saved.has_spawns = 0;
	s_saved.fresh = 1;
	if (!s_saved.actors)
		s_saved.actors = (struct forge_ai_actor_entry *)calloc(FORGE_MAX_AI_ACTORS, sizeof(*s_saved.actors));
	forge_ai_saved_tags_parse(text, "AI_ACTOR|", 'actv');
	forge_ai_saved_tags_parse(text, "AI_WEAPON|", 'weap');
	cur = strstr(text, "AI_SPAWNS_SAVED|");
	if (!cur || sscanf(cur + 16, "%d", &replace_own) != 1)
		return;
	if (!s_saved.entries)
		s_saved.entries = (struct forge_ai_spawn_entry *)calloc(FORGE_MAX_AI_SPAWNS, sizeof(*s_saved.entries));
	if (!s_saved.entries)
		return;
	while ((cur = strstr(cur, "AI_SPAWN|")) != NULL && s_saved.count < FORGE_MAX_AI_SPAWNS)
	{
		struct forge_ai_spawn_entry *entry = &s_saved.entries[s_saved.count];
		int team = 0, count = 0, alive = 0, delay = 0, total = 0, encounter = 0, squad = 0, location = 0, detached = 0;
		unsigned int gametypes = 0;
		int fields;

		memset(entry, 0, sizeof(*entry));
		fields = sscanf(cur + 9, "%f|%f|%f|%f|%d|%d|%d|%d|%d|%u|%d|%d|%d|%d|%255[^|\r\n]|%255[^|\r\n]",
			&entry->x, &entry->y, &entry->z, &entry->yaw, &team, &count, &alive, &delay, &total, &gametypes,
			&encounter, &squad, &location, &detached, entry->actor_variant_path, entry->weapon_path);
		cur += 9;
		if (fields < 15)
			continue;
		/* (last on the line, as a map from before it has none, and a spawn with
		no weapon of its own stops the scan short of it) */
		{
			char const *effect = strstr(cur, "|fx=");

			if (effect && effect < cur + strcspn(cur, "\r\n"))
			{
				int index = atoi(effect + 4);

				if (index > 0 && index < (int)NUMBEROF(s_ai_spawn_effects))
					entry->spawn_effect = (uint8_t)index;
			}
		}
		entry->team = (int16_t)team;
		entry->unit_count = (int16_t)count;
		entry->respawn_alive = (int16_t)alive;
		entry->respawn_delay_seconds = (int16_t)delay;
		entry->respawn_total = (int16_t)total;
		entry->gametype_flags = (uint16_t)gametypes;
		entry->encounter_index = (int16_t)encounter;
		entry->squad_index = (int16_t)squad;
		entry->location_index = (int16_t)location;
		entry->detached = (uint8_t)detached;
		entry->active = 1;
		s_saved.count++;
	}
	s_saved.replace_own = replace_own;
	s_saved.has_spawns = 1;
}

/* a level that is no saved map */
void forge_ai_saved_clear(void)
{
	s_saved.count = 0;
	s_saved.actor_count = 0;
	s_saved.has_spawns = 0;
	s_saved.fresh = 0;
}

int forge_ai_saved_actor_count(void)
{
	return s_saved.actor_count;
}

char const *forge_ai_saved_actor_path(int index)
{
	return s_saved.actors[index].path;
}

char const *forge_ai_saved_actor_map(int index)
{
	return s_saved.actors[index].map;
}

unsigned long forge_ai_saved_actor_group(int index)
{
	return s_saved.actors[index].group_tag;
}

/* ---------- markers */

static int forge_ai_marker_add(int entry_index)
{
	struct forge_ai_spawn_entry const *entry = &g_forge.edit_state.ai_spawns[entry_index];
	struct forge_live_instance *inst;

	if (g_forge.live_instance_count >= FORGE_MAX_LIVE_INSTANCES)
		return 0;
	inst = &g_forge.live_instances[g_forge.live_instance_count++];
	memset(inst, 0, sizeof(*inst));
	inst->active = 1;
	inst->object_index = NONE;
	inst->actor_index = NONE;
	inst->edit_entry_index = entry_index;
	inst->group_tag = FORGE_GROUP_AI_SPAWN;
	inst->is_default_scenario_object = 1;
	inst->netgame_flag_type = -1;
	inst->team_designator = entry->team;
	inst->orig_x = inst->x = entry->x;
	inst->orig_y = inst->y = entry->y;
	inst->orig_z = inst->z = entry->z;
	inst->yaw = entry->yaw;
	snprintf(inst->tag_path, sizeof(inst->tag_path), "%s", entry->actor_variant_path);
	return 1;
}

/* the level's spawns as markers, in the same order on every machine: a saved
map's, else its starting locations as the map has them. A map with more than
Forge can show (a campaign level's) keeps its own, and takes new ones */
void forge_ai_markers_scan(void)
{
	struct forge_scenario_edit_state *edits = &g_forge.edit_state;
	struct scenario *scen = global_scenario_try_and_get();
	struct encounter_definition *encounters;
	struct tag_reference *palette;
	long encounter_count = 0, palette_count = 0, total = 0;
	long e, s, l;
	int i;

	if (!scen || !g_forge_session_enabled || !forge_ai_map_editable())
		return;
	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		if (g_forge.live_instances[i].active && g_forge.live_instances[i].group_tag == FORGE_GROUP_AI_SPAWN)
			return;
	}
	edits->ai_spawn_count = 0;
	edits->ai_spawns_edited = 0;
	edits->replace_ai_spawns = 0;
	s_markers_scanned = 1;
	if (s_saved.applied && s_saved.has_spawns)
	{
		edits->ai_spawn_count = s_saved.count;
		edits->replace_ai_spawns = s_saved.replace_own;
		edits->ai_spawns_edited = 1;
		memcpy(edits->ai_spawns, s_saved.entries, (size_t)s_saved.count * sizeof(*edits->ai_spawns));
	}
	else
	{
		encounters = forge_ai_original_encounters(scen, &encounter_count);
		palette = forge_ai_original_palette(scen, &palette_count);
		for (e = 0; e < encounter_count; e++)
		{
			struct squad_definition const *squads = (struct squad_definition const *)encounters[e].squads.address;

			for (s = 0; s < encounters[e].squads.count; s++)
				total += squads[s].starting_locations.count;
		}
		edits->replace_ai_spawns = total <= FORGE_MAX_AI_SPAWNS;
		if (!edits->replace_ai_spawns)
		{
			platform_log("Forge: %ld AI starting locations are more than Forge shows (%d): the map keeps them",
				total, FORGE_MAX_AI_SPAWNS);
			encounter_count = 0;
		}
		for (e = 0; e < encounter_count; e++)
		{
			struct squad_definition const *squads = (struct squad_definition const *)encounters[e].squads.address;

			for (s = 0; s < encounters[e].squads.count; s++)
			{
				struct squad_definition const *squad = &squads[s];
				struct actor_starting_location const *locations =
					(struct actor_starting_location const *)squad->starting_locations.address;

				for (l = 0; l < squad->starting_locations.count; l++)
				{
					struct forge_ai_spawn_entry *entry = &edits->ai_spawns[edits->ai_spawn_count];
					short index = locations[l].actor_variant_index != NONE ?
						locations[l].actor_variant_index : squad->actor_palette_index;

					if (index < 0 || index >= palette_count || palette[index].index == NONE)
						continue;
					memset(entry, 0, sizeof(*entry));
					snprintf(entry->actor_variant_path, sizeof(entry->actor_variant_path), "%s",
						tag_get_name(palette[index].index));
					entry->x = locations[l].position.x;
					entry->y = locations[l].position.y;
					entry->z = locations[l].position.z;
					entry->yaw = locations[l].facing;
					entry->team = encounters[e].team_index;
					entry->unit_count = squad->min_count;
					entry->respawn_alive = MAX(squad->respawn_min_actors, squad->respawn_max_actors);
					entry->respawn_delay_seconds = (int16_t)squad->respawn_time_upper_bound;
					entry->respawn_total = squad->respawn_total_count;
					entry->encounter_index = (int16_t)e;
					entry->squad_index = (int16_t)s;
					entry->location_index = (int16_t)l;
					entry->active = 1;
					edits->ai_spawn_count++;
				}
			}
		}
	}
	for (i = 0; i < edits->ai_spawn_count; i++)
	{
		if (edits->ai_spawns[i].active && !forge_ai_marker_add(i))
			break;
	}
}

static struct forge_ai_spawn_entry *forge_ai_entry_of(struct forge_live_instance const *inst)
{
	if (inst->group_tag != FORGE_GROUP_AI_SPAWN || inst->edit_entry_index < 0 ||
		inst->edit_entry_index >= g_forge.edit_state.ai_spawn_count)
	{
		return NULL;
	}
	return &g_forge.edit_state.ai_spawns[inst->edit_entry_index];
}

static struct forge_ai_spawn_entry *forge_ai_entry_live(int32_t live_idx)
{
	if (live_idx < 0 || live_idx >= g_forge.live_instance_count || !g_forge.live_instances[live_idx].active)
		return NULL;
	return forge_ai_entry_of(&g_forge.live_instances[live_idx]);
}

/* a new spawn's entry: its index, -1 with no room */
static char const *forge_ai_group_first_path(int group)
{
	int i;

	for (i = 0; i < g_forge.catalog_count; i++)
	{
		struct forge_catalog_entry const *row = &g_forge.catalog[i];

		if (row->group_tag == FORGE_GROUP_AI_SPAWN && !forge_ai_row_is_random(row) && forge_ai_row_group(row) == group)
			return row->tag_path;
	}
	return NULL;
}

int forge_ai_spawn_entry_new(int32_t live_idx, char const *variant_path, float x, float y, float z, float yaw, int16_t team)
{
	struct forge_scenario_edit_state *edits = &g_forge.edit_state;
	struct forge_ai_spawn_entry *entry;
	int random_group = forge_ai_path_group(variant_path, FORGE_AI_RANDOM_PATH);
	int index;

	(void)live_idx;
	for (index = 0; index < edits->ai_spawn_count && edits->ai_spawns[index].active; index++)
	{
	}
	if (index >= FORGE_MAX_AI_SPAWNS)
		return -1;
	if (index == edits->ai_spawn_count)
		edits->ai_spawn_count++;
	entry = &edits->ai_spawns[index];
	memset(entry, 0, sizeof(*entry));
	/* (RANDOM: its group's first rank stands for the group, as model and tag) */
	if (random_group != NONE)
	{
		variant_path = forge_ai_group_first_path(random_group);
		if (!variant_path)
			return -1;
		forge_ai_random_tags_need(random_group);
		entry->gametype_flags = FORGE_AI_RANDOM_RANK;
	}
	/* (Firefight is chosen, on a spawn's GAME MODE row) */
	entry->gametype_flags |= FORGE_AI_MODES_NOT_FIREFIGHT;
	snprintf(entry->actor_variant_path, sizeof(entry->actor_variant_path), "%s", variant_path);
	entry->x = x;
	entry->y = y;
	entry->z = z;
	entry->yaw = yaw;
	entry->team = team;
	entry->unit_count = 1;
	entry->respawn_delay_seconds = 30;
	entry->encounter_index = -1;
	entry->squad_index = -1;
	entry->location_index = -1;
	entry->active = 1;
	edits->ai_spawns_edited = 1;
	return index;
}

void forge_ai_spawn_moved(struct forge_live_instance const *inst)
{
	struct forge_ai_spawn_entry *entry = forge_ai_entry_of(inst);

	if (entry && (entry->x != inst->x || entry->y != inst->y || entry->z != inst->z || entry->yaw != inst->yaw))
	{
		entry->x = inst->x;
		entry->y = inst->y;
		entry->z = inst->z;
		entry->yaw = inst->yaw;
		g_forge.edit_state.ai_spawns_edited = 1;
	}
}

int forge_ai_spawn_settings_get(int32_t live_idx, struct forge_ai_spawn_entry *settings)
{
	struct forge_ai_spawn_entry *entry = forge_ai_entry_live(live_idx);

	if (!entry)
		return 0;
	*settings = *entry;
	return 1;
}

void forge_ai_spawn_settings_set(int32_t live_idx, struct forge_ai_spawn_entry const *settings)
{
	struct forge_ai_spawn_entry *entry = forge_ai_entry_live(live_idx);
	struct forge_live_instance *inst;
	float x, y, z, yaw;

	if (!entry)
		return;
	inst = &g_forge.live_instances[live_idx];
	/* (where it is stays as it is: the object's own to say) */
	x = entry->x;
	y = entry->y;
	z = entry->z;
	yaw = entry->yaw;
	*entry = *settings;
	entry->x = x;
	entry->y = y;
	entry->z = z;
	entry->yaw = yaw;
	snprintf(inst->tag_path, sizeof(inst->tag_path), "%s", entry->actor_variant_path);
	inst->team_designator = entry->team;
	g_forge.edit_state.ai_spawns_edited = 1;
	forge_ai_preview_update(live_idx);
	forge_network_send_ai_spawn(live_idx);
}

void forge_ai_spawn_deleted(struct forge_live_instance const *inst)
{
	struct forge_ai_spawn_entry *entry = forge_ai_entry_of(inst);

	if (entry)
	{
		memset(entry, 0, sizeof(*entry));
		g_forge.edit_state.ai_spawns_edited = 1;
	}
}

/* a copy is a spawn of its own, whatever it was copied from */
void forge_ai_spawn_copy_settings(int source_entry, int32_t live_idx)
{
	struct forge_ai_spawn_entry *entry = forge_ai_entry_live(live_idx);
	struct forge_ai_spawn_entry const *source;

	if (!entry || source_entry < 0 || source_entry >= g_forge.edit_state.ai_spawn_count)
		return;
	source = &g_forge.edit_state.ai_spawns[source_entry];
	snprintf(entry->weapon_path, sizeof(entry->weapon_path), "%s", source->weapon_path);
	entry->team = source->team;
	entry->unit_count = source->unit_count;
	entry->respawn_alive = source->respawn_alive;
	entry->respawn_delay_seconds = source->respawn_delay_seconds;
	entry->respawn_total = source->respawn_total;
	entry->gametype_flags = source->gametype_flags;
	entry->spawn_effect = source->spawn_effect;
	g_forge.live_instances[live_idx].team_designator = entry->team;
	forge_ai_preview_update(live_idx);
	forge_network_send_ai_spawn(live_idx);
}

/* ---------- the properties panel's rows */

static char const *forge_ai_path_leaf(char const *path)
{
	char const *slash = strrchr(path, '\\');

	return slash ? slash + 1 : path;
}

static void forge_ai_upper_copy(char *text, size_t text_size, char const *source)
{
	size_t i;

	for (i = 0; i + 1 < text_size && source[i]; i++)
		text[i] = (char)toupper((unsigned char)source[i]);
	if (text_size)
		text[i] = 0;
}

static char const *forge_ai_team_name(int team)
{
	switch (team)
	{
	case _game_team_default:  return "UNIT'S OWN";
	case _game_team_player:   return "PLAYER";
	case _game_team_human:    return "HUMAN";
	case _game_team_covenant: return "COVENANT";
	case _game_team_flood:    return "FLOOD";
	case _game_team_sentinel: return "SENTINEL";
	default:                  return "OTHER";
	}
}

static uint16_t const s_ai_gametypes[] =
{
	FORGE_AI_MODES_ALL, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, FORGE_AI_MODE_FIREFIGHT, FORGE_AI_MODES_NOT_FIREFIGHT
};
static char const *const s_ai_gametype_names[] =
{
	"ALL MODES", "CTF ONLY", "SLAYER ONLY", "ODDBALL ONLY", "KOTH ONLY", "RACE ONLY", "FIREFIGHT ONLY", "ALL BUT FIREFIGHT"
};
static int16_t const s_ai_delays[] = { 5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300 };
static int16_t const s_ai_totals[] = { 0, 1, 2, 3, 5, 10, 20, 50 };

static int forge_ai_table_index(int16_t const *table, int count, int value)
{
	int i;

	for (i = 0; i < count; i++)
	{
		if (table[i] >= value)
			return i;
	}
	return count - 1;
}

/* a spawn's actor as Forge calls it: its kind, or the group's random */
static void forge_ai_entry_name(struct forge_ai_spawn_entry const *entry, char *text, size_t text_size)
{
	int kind = forge_ai_kind_of(entry->actor_variant_path);

	if (entry->gametype_flags & FORGE_AI_RANDOM_RANK)
		snprintf(text, text_size, "%s: RANDOM", forge_ai_group_name(forge_ai_kind_group(kind)));
	else if (kind >= 0)
		snprintf(text, text_size, "%s", forge_ai_kind_name(kind));
	else
		forge_ai_upper_copy(text, text_size, forge_ai_path_leaf(entry->actor_variant_path));
}

void forge_ai_spawn_row_text(int32_t live_idx, int row, char *label, size_t label_size, char *value, size_t value_size)
{
	struct forge_ai_spawn_entry const *entry = forge_ai_entry_live(live_idx);
	int i;

	label[0] = 0;
	value[0] = 0;
	if (!entry)
		return;
	switch (row)
	{
	case _forge_ai_row_variant:
		snprintf(label, label_size, "AI:");
		forge_ai_entry_name(entry, value, value_size);
		break;
	case _forge_ai_row_weapon:
		snprintf(label, label_size, "WEAPON:");
		if (entry->weapon_path[0])
			forge_ai_upper_copy(value, value_size, forge_ai_path_leaf(entry->weapon_path));
		else
			snprintf(value, value_size, "AI'S OWN");
		break;
	case _forge_ai_row_team:
		snprintf(label, label_size, "TEAM:");
		snprintf(value, value_size, "%s", forge_ai_team_name(entry->team));
		break;
	case _forge_ai_row_start_count:
		snprintf(label, label_size, "AT START:");
		snprintf(value, value_size, "%d", (int)entry->unit_count);
		break;
	case _forge_ai_row_respawn_alive:
		snprintf(label, label_size, "RESPAWN:");
		if (entry->respawn_alive > 0)
			snprintf(value, value_size, "KEEP %d ALIVE", (int)entry->respawn_alive);
		else
			snprintf(value, value_size, "OFF");
		break;
	case _forge_ai_row_respawn_delay:
		snprintf(label, label_size, "RESPAWN DELAY:");
		snprintf(value, value_size, "%d SECONDS", (int)entry->respawn_delay_seconds);
		break;
	case _forge_ai_row_respawn_total:
		snprintf(label, label_size, "RESPAWN LIMIT:");
		if (entry->respawn_total > 0)
			snprintf(value, value_size, "%d", (int)entry->respawn_total);
		else
			snprintf(value, value_size, "ENDLESS");
		break;
	case _forge_ai_row_game_mode:
		snprintf(label, label_size, "GAME MODE:");
		for (i = 0; i < (int)NUMBEROF(s_ai_gametypes) && s_ai_gametypes[i] != forge_ai_entry_modes(entry); i++)
		{
		}
		snprintf(value, value_size, "%s", s_ai_gametype_names[i < (int)NUMBEROF(s_ai_gametypes) ? i : 0]);
		break;
	case _forge_ai_row_spawn_effect:
		snprintf(label, label_size, "SPAWN EFFECT:");
		snprintf(value, value_size, "%s", s_ai_spawn_effects[entry->spawn_effect < NUMBEROF(s_ai_spawn_effects) ?
			entry->spawn_effect : 0].label);
		break;
	}
}

/* the next of the palette's actors or weapons: `dir` on from `path`, wrapping
through none (an empty path) when `none_allowed` */
static void forge_ai_tag_step(uint32_t group_tag, char *path, size_t path_size, int dir, int none_allowed)
{
	uint32_t palette_group = group_tag == 'actv' ? FORGE_GROUP_AI_SPAWN : group_tag;
	int first = -1, last = -1, previous = -1, next = -1, found = 0;
	int i;

	for (i = 0; i < g_forge.catalog_count; i++)
	{
		struct forge_catalog_entry const *entry = &g_forge.catalog[i];

		if (entry->group_tag != palette_group || !forge_catalog_entry_allowed(entry))
			continue;
		if (first < 0)
			first = i;
		last = i;
		if (found && next < 0)
			next = i;
		if (!found && path[0] && !_stricmp(entry->tag_path, path))
			found = 1;
		else if (!found)
			previous = i;
	}
	if (first < 0)
		return;
	if (!found)
	{
		/* (none, or a tag the palette has not got) */
		i = dir > 0 ? first : last;
	}
	else
	{
		i = dir > 0 ? next : previous;
		if (i < 0)
		{
			if (none_allowed)
			{
				path[0] = 0;
				return;
			}
			i = dir > 0 ? first : last;
		}
	}
	snprintf(path, path_size, "%s", g_forge.catalog[i].tag_path);
}

/* the spawn's next actor of its group, RANDOM among them */
static void forge_ai_variant_step(struct forge_ai_spawn_entry *entry, int dir)
{
	int kind = forge_ai_kind_of(entry->actor_variant_path);
	int group = forge_ai_kind_group(kind);
	int random = (entry->gametype_flags & FORGE_AI_RANDOM_RANK) != 0;
	short rows[64];
	int count = 0, place = 0, i;
	struct forge_catalog_entry const *row;

	for (i = 0; i < g_forge.catalog_count && count < (int)NUMBEROF(rows); i++)
	{
		row = &g_forge.catalog[i];
		if (row->group_tag != FORGE_GROUP_AI_SPAWN || !forge_catalog_entry_allowed(row) || forge_ai_row_group(row) != group)
			continue;
		if (forge_ai_row_is_random(row) ? random :
			(!random && (kind >= 0 ? forge_ai_kind_of(row->tag_path) == kind : !_stricmp(row->tag_path, entry->actor_variant_path))))
		{
			place = count;
		}
		rows[count++] = (short)i;
	}
	if (!count)
		return;
	row = &g_forge.catalog[rows[(place + count + dir) % count]];
	if (forge_ai_row_is_random(row))
	{
		char const *first = forge_ai_group_first_path(group);

		if (!first)
			return;
		snprintf(entry->actor_variant_path, sizeof(entry->actor_variant_path), "%s", first);
		entry->gametype_flags |= FORGE_AI_RANDOM_RANK;
		forge_ai_random_tags_need(group);
	}
	else
	{
		snprintf(entry->actor_variant_path, sizeof(entry->actor_variant_path), "%s", row->tag_path);
		entry->gametype_flags &= ~FORGE_AI_RANDOM_RANK;
	}
}

void forge_ai_spawn_row_step(int32_t live_idx, int row, int dir)
{
	struct forge_ai_spawn_entry *entry = forge_ai_entry_live(live_idx);
	struct forge_live_instance *inst;
	int index;

	if (!entry || !dir)
		return;
	inst = &g_forge.live_instances[live_idx];
	switch (row)
	{
	case _forge_ai_row_variant:
		forge_ai_variant_step(entry, dir);
		snprintf(inst->tag_path, sizeof(inst->tag_path), "%s", entry->actor_variant_path);
		break;
	case _forge_ai_row_weapon:
		forge_ai_tag_step('weap', entry->weapon_path, sizeof(entry->weapon_path), dir, 1);
		break;
	case _forge_ai_row_team:
		index = entry->team + dir;
		entry->team = (int16_t)(index < _game_team_default ? _game_team_sentinel :
			index > _game_team_sentinel ? _game_team_default : index);
		inst->team_designator = entry->team;
		entry->detached = 1;
		break;
	case _forge_ai_row_start_count:
		entry->unit_count = (int16_t)((entry->unit_count + 17 + dir) % 17);
		entry->detached = 1;
		break;
	case _forge_ai_row_respawn_alive:
		entry->respawn_alive = (int16_t)((entry->respawn_alive + 17 + dir) % 17);
		entry->detached = 1;
		break;
	case _forge_ai_row_respawn_delay:
		index = forge_ai_table_index(s_ai_delays, NUMBEROF(s_ai_delays), entry->respawn_delay_seconds);
		entry->respawn_delay_seconds = s_ai_delays[(index + (int)NUMBEROF(s_ai_delays) + dir) % (int)NUMBEROF(s_ai_delays)];
		entry->detached = 1;
		break;
	case _forge_ai_row_respawn_total:
		index = forge_ai_table_index(s_ai_totals, NUMBEROF(s_ai_totals), entry->respawn_total);
		entry->respawn_total = s_ai_totals[(index + (int)NUMBEROF(s_ai_totals) + dir) % (int)NUMBEROF(s_ai_totals)];
		entry->detached = 1;
		break;
	case _forge_ai_row_game_mode:
		for (index = 0; index < (int)NUMBEROF(s_ai_gametypes) && s_ai_gametypes[index] != forge_ai_entry_modes(entry); index++)
		{
		}
		if (index == (int)NUMBEROF(s_ai_gametypes))
			index = 0;
		entry->gametype_flags = (uint16_t)((entry->gametype_flags & FORGE_AI_RANDOM_RANK) |
			s_ai_gametypes[(index + (int)NUMBEROF(s_ai_gametypes) + dir) % (int)NUMBEROF(s_ai_gametypes)]);
		break;
	case _forge_ai_row_spawn_effect:
		entry->spawn_effect = (uint8_t)((entry->spawn_effect + (int)NUMBEROF(s_ai_spawn_effects) + dir) %
			(int)NUMBEROF(s_ai_spawn_effects));
		/* (read now, in a solo Forge game, not as the unit appears) */
		if (s_ai_spawn_effects[entry->spawn_effect].path)
			forge_ai_tag_need('effe', s_ai_spawn_effects[entry->spawn_effect].path);
		break;
	default:
		return;
	}
	g_forge.edit_state.ai_spawns_edited = 1;
	forge_ai_preview_update(live_idx);
	forge_network_send_ai_spawn(live_idx);
}

void forge_ai_spawn_label(struct forge_live_instance const *inst, char *text, size_t text_size)
{
	struct forge_ai_spawn_entry const *entry = forge_ai_entry_of(inst);
	char name[64];

	if (entry)
		forge_ai_entry_name(entry, name, sizeof(name));
	else
		snprintf(name, sizeof(name), "%s", forge_ai_path_leaf(inst->tag_path));
	snprintf(text, text_size, "AI: %s", name);
	if (entry && entry->unit_count == 0 && entry->respawn_alive == 0)
		snprintf(text, text_size, "AI: %s (Inactive)", name);
}

/* ---------- what a saved map needs */

int forge_ai_palette_entry_allowed(struct forge_catalog_entry const *entry)
{
	return forge_ai_map_editable() &&
		(entry->group_tag == FORGE_GROUP_AI_SPAWN || entry->group_tag == FORGE_GROUP_AI_MENU);
}

static void forge_ai_saved_tag_add(uint32_t group_tag, char const *path, char const *known_map)
{
	struct forge_scenario_edit_state *edits = &g_forge.edit_state;
	char map[32];
	int i;

	if (known_map)
		snprintf(map, sizeof(map), "%s", known_map);
	else if (!path[0] || !forge_campaign_tag_map(group_tag, path, map, sizeof(map)))
		return;
	for (i = 0; i < edits->ai_actor_count; i++)
	{
		if (edits->ai_actors[i].group_tag == group_tag && !_stricmp(edits->ai_actors[i].path, path))
			return;
	}
	if (edits->ai_actor_count >= FORGE_MAX_AI_ACTORS)
	{
		platform_log("Forge: more than %d actors and weapons are used: %s is left out", FORGE_MAX_AI_ACTORS, path);
		return;
	}
	edits->ai_actors[edits->ai_actor_count].group_tag = group_tag;
	snprintf(edits->ai_actors[edits->ai_actor_count].path, sizeof(edits->ai_actors[0].path), "%s", path);
	snprintf(edits->ai_actors[edits->ai_actor_count].map, sizeof(edits->ai_actors[0].map), "%s", map);
	edits->ai_actor_count++;
}

/* just before a save: the campaign's tags the spawns use, and no others. Those
the map has itself are named too; the level only copies what it has not got */
void forge_ai_saved_tags_collect(void)
{
	struct forge_scenario_edit_state *edits = &g_forge.edit_state;
	int i;

	edits->ai_actor_count = 0;
	for (i = 0; i < edits->ai_spawn_count; i++)
	{
		if (!edits->ai_spawns[i].active)
			continue;
		forge_ai_saved_tag_add('actv', edits->ai_spawns[i].actor_variant_path, NULL);
		forge_ai_saved_tag_add('weap', edits->ai_spawns[i].weapon_path, NULL);
		/* (a random spawn needs every rank it may place) */
		if (edits->ai_spawns[i].gametype_flags & FORGE_AI_RANDOM_RANK)
		{
			int group = forge_ai_kind_group(forge_ai_kind_of(edits->ai_spawns[i].actor_variant_path));
			int row;

			for (row = 0; row < g_forge.catalog_count; row++)
			{
				if (g_forge.catalog[row].group_tag == FORGE_GROUP_AI_SPAWN &&
					!forge_ai_row_is_random(&g_forge.catalog[row]) && forge_ai_row_group(&g_forge.catalog[row]) == group)
				{
					forge_ai_saved_tag_add('actv', g_forge.catalog[row].tag_path, NULL);
				}
			}
		}
		if (edits->ai_spawns[i].spawn_effect < NUMBEROF(s_ai_spawn_effects) &&
			s_ai_spawn_effects[edits->ai_spawns[i].spawn_effect].map)
		{
			forge_ai_saved_tag_add('effe', s_ai_spawn_effects[edits->ai_spawns[i].spawn_effect].path,
				s_ai_spawn_effects[edits->ai_spawns[i].spawn_effect].map);
		}
	}
	platform_log("Forge: the map's spawns use %d campaign tags", (int)edits->ai_actor_count);
}

/* ---------- network */

uint32_t forge_ai_spawn_tag_hash(uint32_t group_tag, char const *path)
{
	return path && path[0] ? forge_catalog_entry_hash(group_tag, path) : 0;
}

/* the settings a network packet gives, what it names looked up by its hash */
static int forge_ai_tag_from_hash(uint32_t group_tag, uint32_t hash, char *path, size_t path_size)
{
	long count, i;
	struct forge_ai_tag_instance *instances = forge_ai_tag_instances(&count);

	for (i = 0; instances && i < count; i++)
	{
		if ((uint32_t)instances[i].group_tag == group_tag && instances[i].name &&
			forge_catalog_entry_hash(group_tag, instances[i].name) == hash)
		{
			snprintf(path, path_size, "%s", instances[i].name);
			return 1;
		}
	}
	return 0;
}

void forge_ai_spawn_net_apply(
	int32_t live_idx,
	uint32_t variant_hash,
	uint32_t weapon_hash,
	int team,
	int unit_count,
	int respawn_alive,
	int respawn_delay_seconds,
	int respawn_total,
	unsigned int gametype_flags,
	int detached,
	int spawn_effect)
{
	struct forge_ai_spawn_entry *entry = forge_ai_entry_live(live_idx);
	struct forge_live_instance *inst;

	if (!entry)
		return;
	inst = &g_forge.live_instances[live_idx];
	if (forge_ai_tag_from_hash('actv', variant_hash, entry->actor_variant_path, sizeof(entry->actor_variant_path)))
		snprintf(inst->tag_path, sizeof(inst->tag_path), "%s", entry->actor_variant_path);
	if (!weapon_hash)
		entry->weapon_path[0] = 0;
	else
		forge_ai_tag_from_hash('weap', weapon_hash, entry->weapon_path, sizeof(entry->weapon_path));
	entry->team = (int16_t)team;
	inst->team_designator = entry->team;
	entry->unit_count = (int16_t)unit_count;
	entry->respawn_alive = (int16_t)respawn_alive;
	entry->respawn_delay_seconds = (int16_t)respawn_delay_seconds;
	entry->respawn_total = (int16_t)respawn_total;
	entry->gametype_flags = (uint16_t)gametype_flags;
	if (spawn_effect >= 0 && spawn_effect < (int)NUMBEROF(s_ai_spawn_effects))
		entry->spawn_effect = (uint8_t)spawn_effect;
	entry->detached = (uint8_t)detached;
	g_forge.edit_state.ai_spawns_edited = 1;
	forge_ai_preview_update(live_idx);
}

int forge_ai_spawn_grid_count(int32_t live_idx)
{
	struct forge_ai_spawn_entry const *entry = forge_ai_entry_live(live_idx);

	return entry && entry->unit_count > 1 && forge_ai_entry_own_squad(entry) ? entry->unit_count : 0;
}

int forge_ai_spawn_grid_point(int32_t live_idx, int index, union real_point3d *point)
{
	struct forge_ai_spawn_entry const *entry = forge_ai_entry_live(live_idx);
	struct forge_ai_grid_layout const *layout;

	if (!entry || index < 0 || index >= FORGE_AI_GRID_PLACES || index >= entry->unit_count)
		return 0;
	layout = forge_ai_grid_layout(entry, (int)(entry - g_forge.edit_state.ai_spawns));
	if (!layout)
		return 0;
	*point = layout->points[index];
	return 1;
}

void forge_ai_spawn_net_settings(
	int32_t live_idx,
	uint32_t *variant_hash,
	uint32_t *weapon_hash,
	struct forge_ai_spawn_entry const **settings)
{
	struct forge_ai_spawn_entry const *entry = forge_ai_entry_live(live_idx);

	*settings = entry;
	if (entry)
	{
		*variant_hash = forge_ai_spawn_tag_hash('actv', entry->actor_variant_path);
		*weapon_hash = forge_ai_spawn_tag_hash('weap', entry->weapon_path);
	}
}

/* ---------- the AI as Forge's mode changes

Forge mode shows each spawn's AI as a model on its marker: a real unit with no
actor, so the engine plays its idle, armed as it will be, and it moves and
goes with the marker as any placed object does. Spartan mode plays the AI, from
the spawns as they stand. The host's mode decides, as it runs the AI. */

/* (actors.c's own: an actor variant's change_colors block) */
struct forge_ai_variant_change_colors
{
	real_rgb_color color_lower_bound;
	real_rgb_color color_upper_bound;
	long pad[2];
};

typedef char forge_ai_variant_change_colors_size_assert[sizeof(struct forge_ai_variant_change_colors) == 0x20 ? 1 : -1];

/* how a tag's object looks with no object made of it, as the Tag Injector's
preview has it (tag_injector.c's preview_change_colors): an actor's change
colors, the middle of each's bounds, and for those it has not, its unit's, the
middle of each's first permutation's; and the shader permutation that is its
rank's armor (actor_customize_unit). actor_path is NULL for any other object */
void forge_model_look_get(long object_definition, char const *actor_path, float colors[4][3], int colored[4],
	short *forced_shader)
{
	struct object_definition const *object = object_definition != NONE ?
		(struct object_definition const *)tag_get(tag_get_group_tag(object_definition), object_definition) : NULL;
	long variant_index = actor_path ? forge_ai_tag_find('actv', actor_path) : NONE;
	struct actor_variant_definition const *variant = variant_index != NONE ?
		(struct actor_variant_definition const *)tag_get('actv', variant_index) : NULL;
	short index, channel;

	memset(colors, 0, sizeof(float) * 4 * 3);
	memset(colored, 0, sizeof(int) * 4);
	*forced_shader = object ? object->object.forced_shader_permutation_index : 0;
	for (index = 0; object && index < MIN(object->object.change_colors.count, 4); index++)
	{
		struct object_change_color_definition const *change_color = TAG_BLOCK_GET_ELEMENT(
			&object->object.change_colors, index, struct object_change_color_definition);
		struct object_change_color_permutation const *permutation;

		if (change_color->permutations.count <= 0)
			continue;
		permutation = TAG_BLOCK_GET_ELEMENT(&change_color->permutations, 0, struct object_change_color_permutation);
		colors[index][0] = (permutation->color_lower_bound.red + permutation->color_upper_bound.red) * 0.5f;
		colors[index][1] = (permutation->color_lower_bound.green + permutation->color_upper_bound.green) * 0.5f;
		colors[index][2] = (permutation->color_lower_bound.blue + permutation->color_upper_bound.blue) * 0.5f;
		colored[index] = 1;
	}
	if (variant)
	{
		if (variant->unit.forced_shader_permutation_index)
			*forced_shader = variant->unit.forced_shader_permutation_index;
		for (index = 0; index < MIN(variant->change_colors.count, 4); index++)
		{
			struct forge_ai_variant_change_colors const *change_colors = TAG_BLOCK_GET_ELEMENT(
				&variant->change_colors, index, struct forge_ai_variant_change_colors);

			colors[index][0] = (change_colors->color_lower_bound.red + change_colors->color_upper_bound.red) * 0.5f;
			colors[index][1] = (change_colors->color_lower_bound.green + change_colors->color_upper_bound.green) * 0.5f;
			colors[index][2] = (change_colors->color_lower_bound.blue + change_colors->color_upper_bound.blue) * 0.5f;
			colored[index] = 1;
		}
	}
	for (index = 0; index < 4; index++)
	{
		/* (a NaN fails every comparison) */
		for (channel = 0; channel < 3; channel++)
		{
			if (!(colors[index][channel] >= 0.0f))
				colors[index][channel] = 0.0f;
			colors[index][channel] = MIN(colors[index][channel], 1.0f);
		}
	}
}

long forge_ai_actor_unit_definition(char const *path)
{
	long variant = forge_ai_tag_need('actv', path);

	if (variant == NONE)
		return NONE;
	return ((struct actor_variant_definition *)tag_get('actv', variant))->unit_reference.index;
}

/* a model answers to Forge's user alone: no unit or vehicle meets it, and no
blast pushes it (taking no damage does not stop that: object_cause_damage) */
void forge_ai_preview_protect(long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);

	if (unit)
	{
		SET_FLAG(unit->object.flags, _object_no_collisions_bit, TRUE);
		SET_FLAG(unit->unit.flags, _unit_impervious_bit, TRUE);
	}
}

static long forge_ai_preview_new(struct forge_ai_spawn_entry const *entry)
{
	long unit_definition = forge_ai_actor_unit_definition(entry->actor_variant_path);
	long variant = forge_ai_tag_find('actv', entry->actor_variant_path);
	long weapon = forge_ai_tag_need('weap', entry->weapon_path);
	struct object_placement_data placement;
	struct object_datum *object;
	long unit_index;

	if (unit_definition == NONE)
		return NONE;
	object_placement_data_new(&placement, unit_definition, NONE);
	placement.position.x = entry->x;
	placement.position.y = entry->y;
	placement.position.z = entry->z + 0.1f;
	placement.forward.i = (real)cos((double)entry->yaw);
	placement.forward.j = (real)sin((double)entry->yaw);
	placement.forward.k = 0.0f;
	placement.up.i = 0.0f;
	placement.up.j = 0.0f;
	placement.up.k = 1.0f;
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	unit_index = object_new(&placement);
	if (unit_index == NONE)
		return NONE;
	actor_customize_unit(variant, unit_index);
	if (weapon != NONE)
		forge_ai_weapon_give(unit_index, weapon);
	object = (struct object_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	if (object)
	{
		/* (object_cannot_take_damage is the scripts', of an object list) */
		SET_FLAG(object->object.damage_flags, _object_cannot_take_damage_bit, TRUE);
		SET_FLAG(object->object.flags, _object_cannot_be_garbage_bit, TRUE);
		SET_FLAG(object->object.flags, _object_deleted_when_deactivated_bit, FALSE);
		object_set_automatic_deactivation(unit_index, FALSE);
		forge_ai_preview_protect(unit_index);
	}
	return unit_index;
}

/* a marker's model as its settings have it, while the AI is held */
void forge_ai_preview_update(int32_t live_idx)
{
	struct forge_ai_spawn_entry const *entry = forge_ai_entry_live(live_idx);
	struct forge_live_instance *inst;

	if (!entry || !s_ai_held || game_connection() == _game_connection_network_client)
		return;
	inst = &g_forge.live_instances[live_idx];
	if (inst->object_index != NONE)
		object_delete(inst->object_index);
	inst->object_index = forge_ai_preview_new(entry);
	if (live_idx == g_forge.grabbed_live_index)
		g_forge.grabbed_object_index = inst->object_index;
}

static void forge_ai_previews_set(int shown)
{
	int i;

	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		struct forge_live_instance *inst = &g_forge.live_instances[i];

		if (!inst->active || inst->group_tag != FORGE_GROUP_AI_SPAWN)
			continue;
		if (shown)
		{
			forge_ai_preview_update(i);
		}
		else if (inst->object_index != NONE)
		{
			object_delete(inst->object_index);
			inst->object_index = NONE;
		}
	}
}

static int forge_ai_object_is_placed(long object_index)
{
	int i;

	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		if (g_forge.live_instances[i].object_index == object_index)
			return 1;
	}
	return 0;
}

/* what the AI left: dead units no player or placed object is */
static void forge_ai_corpses_delete(void)
{
	struct object_iterator iterator;
	struct unit_datum *unit;

	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((unit = (struct unit_datum *)object_iterator_next(&iterator)) != NULL)
	{
		if (unit->unit.player_index != NONE || !TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		if (!forge_ai_object_is_placed(iterator.index))
			object_delete(iterator.index);
	}
}

/* and the weapons dropped: loose ones a unit carried, the map's own waiting
to be taken never were. Not a flag or ball, which the game needs, nor a placed
object */
static void forge_ai_dropped_weapons_delete(void)
{
	struct object_iterator iterator;
	struct item_datum *item;

	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while ((item = (struct item_datum *)object_iterator_next(&iterator)) != NULL)
	{
		if (!TEST_FLAG(item->item.flags, _item_has_been_held_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit) ||
			item->object.parent_object_index != NONE || weapon_is_flag(iterator.index))
		{
			continue;
		}
		if (!forge_ai_object_is_placed(iterator.index))
			object_delete(iterator.index);
	}
}

/* (a map with no AI and no spawns has nothing to stop or start, but the AI
still counts as held: the first spawn placed there shows its model at once) */
void forge_ai_forge_mode_changed(int forge_mode)
{
	if (game_connection() == _game_connection_network_client || !forge_ai_map_editable())
		return;
	if (!forge_ai_map_supported() && g_forge.edit_state.ai_spawn_count == 0)
	{
		s_ai_held = forge_mode;
		return;
	}
	if (forge_mode)
	{
		ai_erase(NONE, NONE, NONE, FALSE);
		s_ai_held = 1;
		ai_dispose_from_old_map();
		ai_initialize_for_new_map();
		forge_ai_corpses_delete();
		forge_ai_dropped_weapons_delete();
		forge_ai_previews_set(1);
	}
	else
	{
		forge_ai_previews_set(0);
		s_ai_held = 0;
		ai_erase(NONE, NONE, NONE, FALSE);
		ai_dispose_from_old_map();
		ai_initialize_for_new_map();
		ai_place();
	}
}
