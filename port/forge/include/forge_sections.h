/*
FORGE_SECTIONS.H

A campaign level played as a multiplayer map (forge_hooks.c's
forge_campaign_level_strip) is made of sections: its structure BSPs, of
which the game holds one at a time. The campaign went from one to the next
through its BSP switch trigger volumes; a multiplayer map never switches
(every machine plays the one), so those volumes are taken from the engine as
the level loads and kept here as seals instead: the doorways where the
section loaded stops. Players can't walk into them (a wall to bipeds,
forge_sections_seal_move); Forge mode draws them as red boxes.

The section a map plays is MAP SETTINGS' LEVEL SECTION (forge_map_settings.c's
section, saved as MAP_SECTION|n): loaded first as the level loads
(forge_sections_first_structure_bsp, scenario.c), and in a Forge session
alone (no network game) switched to at once as it is changed, the Monitor
taken into it (forge_sections_update). A player whose start is in another
section starts inside this one (forge_sections_spawn_location).
*/

#ifndef __FORGE_SECTIONS_H
#define __FORGE_SECTIONS_H

struct scenario;
struct player_starting_location;
union real_point3d;

/* ---------- prototypes/FORGE_SECTIONS.C */

/* a level going (menu_tags_unloaded): no sections */
void forge_sections_level_reset(void);
/* a campaign level made a multiplayer map's as its tags load
(forge_campaign_level_strip, before it empties the scenario's blocks): its
sections and seals kept, and its BSP switch trigger volumes taken from it */
void forge_sections_level_stripped(struct scenario *scenario);

/* the level is one of those, with more than one section (LEVEL SECTION
shows) */
int forge_sections_available(void);
int forge_sections_count(void);
/* a section's name, its BSP's last part ("a10b") */
void forge_sections_name(int section, char *text, unsigned long size);
/* the seals of a section: the doorways out of it */
int forge_sections_seal_count(int section);
/* the section loaded (NONE while none is), and one being switched to */
int forge_sections_loaded(void);
int forge_sections_switching(void);

/* the structure BSP a level loads first (scenario.c's scenario_load, and a
map reset): the map's LEVEL SECTION, else 0 */
short forge_sections_first_structure_bsp(void);

/* each frame of a level (forge_3d_update): a Forge session's LEVEL SECTION
switched to, and the Monitor taken into it once it is loaded */
void forge_sections_update(void);

/* a player's start (players.c's player_spawn): `chosen`, or, when it is
outside the section loaded (or there is none), a place inside it written to
`scratch` */
struct player_starting_location const *forge_sections_spawn_location(struct player_starting_location const *chosen,
	struct player_starting_location *scratch);
/* a start outside the section loaded (players.c's choice of one skips it) */
int forge_sections_point_outside(union real_point3d const *point);

/* a biped's move (bipeds.c, after the pill's move, as the ONE WAY floors'):
TRUE when a seal of the section loaded stops it coming in, `to` set on the
seal's side and `velocity`'s part into it taken away, and that side's plane */
int forge_sections_seal_move(float const from[3], float to[3], float velocity[3], float radius, float out_normal[3],
	float *out_d);

/* the seals of the section loaded, in Forge mode (forge_runtime_tags.c's
world pass, with the volumes' line and face drawers: color 1, red) */
void forge_sections_render(
	void (*draw_line)(float const *from, float const *to, int color, int highlighted),
	void (*draw_face)(float const *corner, float const *side_a, float const *side_b, int color, int highlighted));

#endif
