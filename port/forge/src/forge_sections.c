/*
FORGE_SECTIONS.C

A campaign level's sections in Forge and in a match of a map made on one
(forge_sections.h): its structure BSPs, the one a map plays (MAP SETTINGS'
LEVEL SECTION), and the seals where it stops, made of the level's BSP switch
trigger volumes.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game/game.h"
#include "game/players.h"
#include "main/main.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "networking/network_game_globals.h"
#include "forge_engine.h"
#include "forge_map_settings.h"
#include "forge_sections.h"

/* ---------- constants */

#define MAXIMUM_SEALS 64
/* a seal stands higher than the doorway's trigger volume, which reaches
only a little over a player's head, so none can jump over it, and a little
under its floor */
#define SEAL_RAISE 2.5f
#define SEAL_LOWER 0.3f
/* (a seal's side a stopped move is set on, just outside it) */
#define SEAL_SKIN 0.001f
/* a landing point a little over the place found */
#define LANDING_LIFT 0.25f

/* the scenario's BSP switch trigger volume (players.c's) */
struct section_switch_volume
{
	short trigger_volume_index;
	short source_structure_bsp_index;
	short destination_structure_bsp_index;
	short cutscene_flag_index;
};

/* ---------- structures */

struct section_seal
{
	short source;
	short destination;
	float middle[3];
	/* (its axes, unit: the trigger volume's forward, left and up) */
	float axis[3][3];
	float half[3];
};

/* ---------- globals */

static struct
{
	int stripped;
	int count;
	int seal_count;
	struct section_seal seals[MAXIMUM_SEALS];
	/* a Forge session's switch asked for: the section, and the Monitor to
	be taken into it once it is loaded */
	int switching;
	short wanted;
	int land;
} sections;

/* ---------- private code */

static float dot3(float const *a, float const *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void normalize3(float *v)
{
	float length = (float)sqrt((double)dot3(v, v));

	if (length > 0.000001f)
	{
		v[0] /= length;
		v[1] /= length;
		v[2] /= length;
	}
}

static int section_point_inside(float const point[3])
{
	real_point3d p;

	if (global_structure_bsp_index == NONE)
		return 0;
	p.x = point[0];
	p.y = point[1];
	p.z = point[2];
	return scenario_leaf_index_from_point(&p) != NONE;
}

/* a point in the seal (each side `margin` further out): its place on the
seal's axes, in `local` */
static int seal_contains(struct section_seal const *seal, float const point[3], float margin, float local[3])
{
	float d[3];
	int axis, inside = 1;

	d[0] = point[0] - seal->middle[0];
	d[1] = point[1] - seal->middle[1];
	d[2] = point[2] - seal->middle[2];
	/* (every axis's place, in it or not: forge_sections_seal_move reads them) */
	for (axis = 0; axis < 3; axis++)
	{
		local[axis] = dot3(d, seal->axis[axis]);
		if (fabs((double)local[axis]) > seal->half[axis] + margin)
			inside = 0;
	}
	return inside;
}

static int point_in_a_seal(float const point[3])
{
	int k;
	float local[3];

	for (k = 0; k < sections.seal_count; k++)
	{
		if (sections.seals[k].source == global_structure_bsp_index && seal_contains(&sections.seals[k], point, 0.0f, local))
			return 1;
	}
	return 0;
}

/* a place inside the section loaded to stand in: the doorways into it from
its neighbours (in both, where they meet, and not in its seals), then its
seals' middles, then the inside point nearest its bounds' middle */
static int section_landing(float out[3])
{
	int pass, k;

	if (global_structure_bsp_index == NONE)
		return 0;
	for (pass = 0; pass < 2; pass++)
	{
		for (k = 0; k < sections.seal_count; k++)
		{
			struct section_seal const *seal = &sections.seals[k];
			float point[3];

			if (pass == 0 ? seal->destination != global_structure_bsp_index : seal->source != global_structure_bsp_index)
				continue;
			/* (its floor's height, a little over it: the seal reaches lower) */
			point[0] = seal->middle[0] - seal->axis[2][0] * (seal->half[2] - SEAL_LOWER - LANDING_LIFT);
			point[1] = seal->middle[1] - seal->axis[2][1] * (seal->half[2] - SEAL_LOWER - LANDING_LIFT);
			point[2] = seal->middle[2] - seal->axis[2][2] * (seal->half[2] - SEAL_LOWER - LANDING_LIFT);
			if (section_point_inside(point) && (pass == 1 || !point_in_a_seal(point)))
			{
				out[0] = point[0];
				out[1] = point[1];
				out[2] = point[2];
				return 1;
			}
		}
	}
	{
		struct structure_bsp *bsp = global_structure_bsp_get();
		float middle[3], best_distance = -1.0f;
		int i, j, n;

		if (!bsp)
			return 0;
		middle[0] = (bsp->world_bounds.x0 + bsp->world_bounds.x1) * 0.5f;
		middle[1] = (bsp->world_bounds.y0 + bsp->world_bounds.y1) * 0.5f;
		middle[2] = (bsp->world_bounds.z0 + bsp->world_bounds.z1) * 0.5f;
		for (i = 0; i <= 8; i++)
		{
			for (j = 0; j <= 8; j++)
			{
				for (n = 0; n <= 4; n++)
				{
					float point[3], dx, dy, dz, distance;

					point[0] = bsp->world_bounds.x0 + (bsp->world_bounds.x1 - bsp->world_bounds.x0) * (float)i / 8.0f;
					point[1] = bsp->world_bounds.y0 + (bsp->world_bounds.y1 - bsp->world_bounds.y0) * (float)j / 8.0f;
					point[2] = bsp->world_bounds.z0 + (bsp->world_bounds.z1 - bsp->world_bounds.z0) * (float)n / 4.0f;
					if (!section_point_inside(point))
						continue;
					dx = point[0] - middle[0];
					dy = point[1] - middle[1];
					dz = point[2] - middle[2];
					distance = dx * dx + dy * dy + dz * dz;
					if (best_distance < 0.0f || distance < best_distance)
					{
						best_distance = distance;
						out[0] = point[0];
						out[1] = point[1];
						out[2] = point[2];
					}
				}
			}
		}
		return best_distance >= 0.0f;
	}
}

/* the local player (the Monitor in Forge mode) taken into the section */
static void section_land_player(void)
{
	long unit_index = player_control_get_unit_index(0);
	float point[3];
	real_point3d position;
	char name[64];

	forge_sections_name(global_structure_bsp_index, name, sizeof(name));
	if (!section_landing(point))
	{
		forge_set_status("Section %s loaded: no place inside it was found to put you", name);
		return;
	}
	position.x = point[0];
	position.y = point[1];
	position.z = point[2];
	if (unit_index != NONE)
		object_set_position(unit_index, &position, NULL, NULL);
	g_forge.monitor_cam_initialized = 0;
	{
		int seals = forge_sections_seal_count(global_structure_bsp_index);

		forge_set_status("Section %s (%d of %d): %d doorway%s sealed (red)%s", name, global_structure_bsp_index + 1,
			sections.count, seals, seals == 1 ? "" : "s",
			global_structure_bsp_index != 0 ? "; place PLAYER SPAWNs in it" : "");
	}
}

/* ---------- public code */

void forge_sections_level_reset(void)
{
	memset(&sections, 0, sizeof(sections));
}

void forge_sections_level_stripped(struct scenario *scenario)
{
	struct section_switch_volume const *switches;
	struct scenario_trigger_volume const *triggers;
	long k;

	forge_sections_level_reset();
	if (!scenario)
		return;
	sections.stripped = 1;
	sections.count = (int)scenario->structure_bsp_references.count;
	switches = (struct section_switch_volume const *)scenario->bsp_switch_trigger_volumes.address;
	triggers = (struct scenario_trigger_volume const *)scenario->trigger_volumes.address;
	for (k = 0; switches && triggers && k < scenario->bsp_switch_trigger_volumes.count &&
		sections.seal_count < MAXIMUM_SEALS; k++)
	{
		struct section_switch_volume const *volume = &switches[k];
		struct scenario_trigger_volume const *trigger;
		struct section_seal *seal;
		float extents[3];
		int axis;

		if (volume->trigger_volume_index < 0 || volume->trigger_volume_index >= scenario->trigger_volumes.count ||
			volume->source_structure_bsp_index < 0 || volume->source_structure_bsp_index >= sections.count)
		{
			continue;
		}
		trigger = &triggers[volume->trigger_volume_index];
		seal = &sections.seals[sections.seal_count];
		seal->source = volume->source_structure_bsp_index;
		seal->destination = volume->destination_structure_bsp_index;
		seal->axis[0][0] = trigger->forward.i;
		seal->axis[0][1] = trigger->forward.j;
		seal->axis[0][2] = trigger->forward.k;
		seal->axis[2][0] = trigger->up.i;
		seal->axis[2][1] = trigger->up.j;
		seal->axis[2][2] = trigger->up.k;
		normalize3(seal->axis[0]);
		normalize3(seal->axis[2]);
		/* (left = up x forward, as the engine's trigger volumes have it) */
		seal->axis[1][0] = seal->axis[2][1] * seal->axis[0][2] - seal->axis[2][2] * seal->axis[0][1];
		seal->axis[1][1] = seal->axis[2][2] * seal->axis[0][0] - seal->axis[2][0] * seal->axis[0][2];
		seal->axis[1][2] = seal->axis[2][0] * seal->axis[0][1] - seal->axis[2][1] * seal->axis[0][0];
		extents[0] = trigger->extents.i;
		extents[1] = trigger->extents.j;
		extents[2] = trigger->extents.k;
		for (axis = 0; axis < 3; axis++)
		{
			seal->middle[axis] = (&trigger->position.x)[axis] + 0.5f * (seal->axis[0][axis] * extents[0] +
				seal->axis[1][axis] * extents[1] + seal->axis[2][axis] * extents[2]);
			seal->half[axis] = (float)fabs((double)extents[axis]) * 0.5f;
		}
		/* (taller: SEAL_RAISE over it, SEAL_LOWER under it) */
		for (axis = 0; axis < 3; axis++)
			seal->middle[axis] += seal->axis[2][axis] * (SEAL_RAISE - SEAL_LOWER) * 0.5f;
		seal->half[2] += (SEAL_RAISE + SEAL_LOWER) * 0.5f;
		if (seal->half[0] <= 0.0f || seal->half[1] <= 0.0f)
			continue;
		sections.seal_count++;
	}
	error(_error_silent, "forge: the campaign level's %d sections, %d doorways between them sealed",
		sections.count, sections.seal_count);
	/* (none switched to by the engine: players.c's zones) */
	scenario->bsp_switch_trigger_volumes.count = 0;
}

int forge_sections_available(void)
{
	return sections.stripped && sections.count > 1;
}

int forge_sections_count(void)
{
	return sections.stripped ? sections.count : 0;
}

void forge_sections_name(int section, char *text, unsigned long size)
{
	struct scenario *scenario = global_scenario_try_and_get();
	char const *name = NULL;

	if (!text || !size)
		return;
	if (scenario && section >= 0 && section < scenario->structure_bsp_references.count)
	{
		struct scenario_structure_bsp_reference const *reference = TAG_BLOCK_GET_ELEMENT(
			&scenario->structure_bsp_references, section, struct scenario_structure_bsp_reference);

		if (reference->structure_bsp.index != NONE)
			name = tag_get_name(reference->structure_bsp.index);
	}
	if (name)
	{
		char const *slash = strrchr(name, '\\');

		snprintf(text, size, "%s", slash ? slash + 1 : name);
	}
	else
		snprintf(text, size, "#%d", section + 1);
}

int forge_sections_seal_count(int section)
{
	int k, count = 0;

	for (k = 0; k < sections.seal_count; k++)
	{
		if (sections.seals[k].source == section)
			count++;
	}
	return count;
}

int forge_sections_loaded(void)
{
	return global_structure_bsp_index;
}

int forge_sections_switching(void)
{
	return sections.switching;
}

short forge_sections_first_structure_bsp(void)
{
	int section = forge_map_settings_get()->section;

	if (!sections.stripped || section <= 0 || section >= sections.count)
		return 0;
	error(_error_silent, "forge: the map plays the level's section %d", section);
	return (short)section;
}

void forge_sections_update(void)
{
	int section = forge_map_settings_get()->section;

	if (!sections.stripped || global_structure_bsp_index == NONE)
		return;
	if (sections.switching)
	{
		/* (main.c switches it at the frame's end) */
		if (global_structure_bsp_index != sections.wanted)
			return;
		sections.switching = 0;
		if (sections.land)
			section_land_player();
		sections.land = 0;
		return;
	}
	/* (a Forge session alone: every machine of a network game plays the
	section its map file names, from the level's start) */
	if (!g_forge_session_enabled || network_game_is_active() || section < 0 || section >= sections.count ||
		section == global_structure_bsp_index)
	{
		return;
	}
	sections.switching = 1;
	sections.wanted = (short)section;
	sections.land = 1;
	main_switch_structure_bsp((short)section);
}

int forge_sections_point_outside(union real_point3d const *point)
{
	float p[3];

	if (!sections.stripped || !point || global_structure_bsp_index == NONE)
		return 0;
	p[0] = point->x;
	p[1] = point->y;
	p[2] = point->z + LANDING_LIFT;
	return !section_point_inside(p);
}

struct player_starting_location const *forge_sections_spawn_location(struct player_starting_location const *chosen,
	struct player_starting_location *scratch)
{
	float point[3];

	if (!sections.stripped || !scratch || global_structure_bsp_index == NONE ||
		(chosen && !forge_sections_point_outside(&chosen->position)))
	{
		return chosen;
	}
	if (!section_landing(point))
		return chosen;
	if (chosen)
		*scratch = *chosen;
	else
		memset(scratch, 0, sizeof(*scratch));
	scratch->position.x = point[0];
	scratch->position.y = point[1];
	scratch->position.z = point[2];
	return scratch;
}

int forge_sections_seal_move(float const from[3], float to[3], float velocity[3], float radius, float out_normal[3],
	float *out_d)
{
	int k;

	if (!sections.stripped || !sections.seal_count || global_structure_bsp_index == NONE)
		return 0;
	for (k = 0; k < sections.seal_count; k++)
	{
		struct section_seal const *seal = &sections.seals[k];
		float local_to[3], local_from[3], excess = 0.0f, into;
		int axis, side = -1;

		if (seal->source != global_structure_bsp_index || !seal_contains(seal, to, radius, local_to))
			continue;
		/* (already in it, as a spawn there: let out, not held) */
		if (seal_contains(seal, from, radius, local_from))
			continue;
		/* (the side it came through: the axis it was furthest out on) */
		for (axis = 0; axis < 3; axis++)
		{
			float out = ((float)fabs((double)local_from[axis]) - (seal->half[axis] + radius)) /
				(seal->half[axis] + radius);

			if (side < 0 || out > excess)
			{
				side = axis;
				excess = out;
			}
		}
		{
			float sign = local_from[side] < 0.0f ? -1.0f : 1.0f;
			float push = sign * (seal->half[side] + radius + SEAL_SKIN) - local_to[side];

			to[0] += seal->axis[side][0] * push;
			to[1] += seal->axis[side][1] * push;
			to[2] += seal->axis[side][2] * push;
			out_normal[0] = seal->axis[side][0] * sign;
			out_normal[1] = seal->axis[side][1] * sign;
			out_normal[2] = seal->axis[side][2] * sign;
		}
		into = dot3(velocity, out_normal);
		if (into < 0.0f)
		{
			velocity[0] -= out_normal[0] * into;
			velocity[1] -= out_normal[1] * into;
			velocity[2] -= out_normal[2] * into;
		}
		*out_d = dot3(out_normal, to) - radius;
		return 1;
	}
	return 0;
}

void forge_sections_render(
	void (*draw_line)(float const *from, float const *to, int color, int highlighted),
	void (*draw_face)(float const *corner, float const *side_a, float const *side_b, int color, int highlighted))
{
	int k;

	if (!sections.stripped || !draw_line || global_structure_bsp_index == NONE)
		return;
	for (k = 0; k < sections.seal_count; k++)
	{
		struct section_seal const *seal = &sections.seals[k];
		float corners[8][3], sides[3][3];
		int c, axis;

		if (seal->source != global_structure_bsp_index)
			continue;
		for (c = 0; c < 8; c++)
		{
			for (axis = 0; axis < 3; axis++)
			{
				corners[c][axis] = seal->middle[axis] +
					seal->axis[0][axis] * seal->half[0] * ((c & 1) ? 1.0f : -1.0f) +
					seal->axis[1][axis] * seal->half[1] * ((c & 2) ? 1.0f : -1.0f) +
					seal->axis[2][axis] * seal->half[2] * ((c & 4) ? 1.0f : -1.0f);
			}
		}
		for (c = 0; c < 8; c++)
		{
			int bit;

			for (bit = 1; bit < 8; bit <<= 1)
			{
				if (!(c & bit))
					draw_line(corners[c], corners[c | bit], 1, 1);
			}
		}
		if (!draw_face)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			sides[axis][0] = seal->axis[axis][0] * seal->half[axis] * 2.0f;
			sides[axis][1] = seal->axis[axis][1] * seal->half[axis] * 2.0f;
			sides[axis][2] = seal->axis[axis][2] * seal->half[axis] * 2.0f;
		}
		/* (each pair of faces: from the low corner, and from the high one
		with its sides turned back) */
		for (axis = 0; axis < 3; axis++)
		{
			int a = (axis + 1) % 3, b = (axis + 2) % 3;
			float back_a[3], back_b[3];

			draw_face(corners[0], sides[a], sides[b], 1, 0);
			back_a[0] = -sides[a][0];
			back_a[1] = -sides[a][1];
			back_a[2] = -sides[a][2];
			back_b[0] = -sides[b][0];
			back_b[1] = -sides[b][1];
			back_b[2] = -sides[b][2];
			draw_face(corners[7], back_b, back_a, 1, 0);
		}
	}
}
