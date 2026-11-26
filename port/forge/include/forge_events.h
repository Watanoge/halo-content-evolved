/*
FORGE_EVENTS.H

Forge's events (port/forge/src/forge_events.c): things that happen in a game
of the map, placed from the palette's EVENTS tab as markers, each with a
NAME and a number (1 to FORGE_EVENT_MAX_NUMBER, given as it is placed, never
changed) that what waits on it is set to. Its TYPE:

- INTERACT: its marker is a holopad, one of the campaign's control panels
  (its DEVICE), which every machine makes for itself; the game's own device
  prompt ("Press X to " its DEVICE NAME) and use fire it, once each COOLDOWN,
  for a player on its USED BY team (ANY, RED, BLUE; in a Forge game the
  player's own, as TEST OBJECTIVE's);
- GROUP: up to FORGE_EVENT_GROUP_MAX other events (groups too); it fires
  once ANY of them, or ALL of them, have fired at least once this game, then
  is DONE, or STARTS AGAIN (RESETS). Being an event, anything that waits on
  an event can wait on a group;
- TRIGGER VOLUME: a VOLUMES tab volume set to TRIGGER (numbered as AI
  PATROL's), picked by number; it fires as a player of its USED BY team
  ENTERS it or LEAVES it (one first seen in it, or respawned, fires nothing);
- TIMER (GLOBAL TIME): ONCE EVERY its SECONDS, or ONCE AT its SECONDS into the game;
  and with TRIGGER ON START, as the game starts too;
- PLAYER SCORE: by its TEAM (ANY, RED, BLUE: the team of who did it; a free
  for all's players are ANY's alone) and its GAME MODE:
  - ANY: a team's score (each player's, in a free for all) passing another
    multiple of its POINTS;
  - CTF: FLAG CAPTURED, FLAG STOLEN (taken off its stand); here TEAM (its
    row FLAG:) is the flag's team, not who took it;
  - SLAYER: PLAYERS DEAD, each time its count of the team's players has died
    (TEAM here is the dead players' team);
  - KING OF THE HILL: ZONE CAPTURED (a team comes to hold it), ZONE LOST (the
    team holding it no longer does: contested or left), ZONE CHANGED (it
    moves);
  - ODDBALL: BALL SPAWNED (made, or reset to its spawn), BALL DROPPED, BALL
    GRABBED.
  A game mode's triggers come about in its games alone.

TEST EVENT (its row) leaves Forge mode for Spartan mode, counts down
FORGE_EVENT_TEST_DELAY_MS on the screen ("#1 NAME IN 5..."), then fires it as
a game would. A Forge game plays no CTF, King of the Hill or Oddball, so a
CTF flag's, oddball spawn's or hill's marker has TEST OBJECTIVE: in Spartan
mode its flag or ball is out (a hill is a zone), and taking, capturing,
dropping or standing in it tells the events what those games would
(forge_objective_tests_update), the player's team its team (a test flag's
own team for CTF's).

What waits on an event, by its number (0: as ever):

- an AI spawn's RESPAWN ON (its SPAWN SETTINGS): its squad is brought back up
  to its KEEP N ALIVE each time the event fires, within its RESPAWN LIMIT,
  instead of one a RESPAWN DELAY (forge_ai.c);
- a placed weapon's, equipment's, vehicle's or emitter's SPAWN ON: in a match
  it is not there till the event fires, then is made at its place, and made
  again each time the event fires if it is gone from there (one left
  elsewhere goes, unless it is held or ridden). The gametype's vehicle
  respawn leaves it alone. In a Forge session it stays where it is placed,
  to be edited, and is put back on each firing; an emitter is seen in Forge
  mode and kept away in Spartan mode till its event fires (entering Forge
  mode resets it, as a door). The host makes the weapons,
  equipment and vehicles (the network gives them to the clients); every
  machine its own emitters, as scenery is;
- an effect spot's REPEAT: it plays each time the event fires, not every few
  seconds.

The host (or a game with no network) runs the events and what waits on them;
it tells each client of every event fired (distributed message 168,
forge_events_handle_message), whose effect spots and emitters then play it.

A saved map keeps them on EVENT| lines (and its objects' bindings on the
OBJECT_EVENT| line before their OBJECT line, an AI spawn's as |ev=); a Forge
session sends an event's settings as !FX: and its name as !FXN:, and a
binding as !FB: (forge_network.c).
*/

#ifndef __FORGE_EVENTS_H
#define __FORGE_EVENTS_H

#include <stddef.h>
#include <stdint.h>

/* ---------- constants */

#define FORGE_GROUP_EVENT 'fevt'
/* (a saved line's and a peer's of before: its TYPE is its own) */
#define FORGE_EVENT_PATH "event\\trigger"
/* the EVENTS tab's rows, one a TYPE: what each places */
#define FORGE_EVENT_TIMER_PATH "event\\timer"
#define FORGE_EVENT_SCORE_PATH "event\\score"
#define FORGE_EVENT_INTERACT_PATH "event\\interact"
#define FORGE_EVENT_GROUP_PATH "event\\group"
#define FORGE_EVENT_VOLUME_PATH "event\\volume"
/* (a GROUP's events at most) */
#define FORGE_EVENT_GROUP_MAX 8

/* (the number an event has, which a saved line and a packet carry; and its
name's letters: the chat line's 64 bytes carry it whole) */
#define FORGE_EVENT_MAX_NUMBER 63
#define FORGE_EVENT_NAME_MAX 20

enum forge_event_type
{
	/* (TIMER: the global time's) */
	_forge_event_global_time = 0,
	_forge_event_player_score,
	/* INTERACT: the event's marker is a holopad, which a player fires by
	pressing action on it (forge_events_holopads_*) */
	_forge_event_interact,
	/* GROUP: fires once ANY, or ALL, of its EVENTS (up to
	FORGE_EVENT_GROUP_MAX) have fired at least once this game; then it is done,
	or, RESETS, starts counting again */
	_forge_event_group,
	/* TRIGGER VOLUME: fires as a player (of its USED BY team) ENTERS or LEAVES
	a TRIGGER volume (forge_volumes.h), by its number (`value`) */
	_forge_event_volume,
	NUMBER_OF_FORGE_EVENT_TYPES
};

/* GLOBAL TIME's TIME */
enum
{
	_forge_event_time_every = 0,
	_forge_event_time_once_at,
	NUMBER_OF_FORGE_EVENT_TIME_MODES
};

/* PLAYER SCORE's GAME MODE */
enum forge_event_game_mode
{
	_forge_event_mode_any = 0,
	_forge_event_mode_ctf,
	_forge_event_mode_slayer,
	_forge_event_mode_king,
	_forge_event_mode_oddball,
	NUMBER_OF_FORGE_EVENT_GAME_MODES
};

/* PLAYER SCORE's TRIGGER, by its game mode (ANY's and SLAYER's one each) */
enum
{
	_forge_event_ctf_captured = 0,
	_forge_event_ctf_stolen,

	_forge_event_king_captured = 0,
	_forge_event_king_lost,
	_forge_event_king_changed,

	_forge_event_oddball_spawned = 0,
	_forge_event_oddball_dropped,
	_forge_event_oddball_grabbed
};

/* PLAYER SCORE's TEAM */
enum
{
	_forge_event_team_any = 0,
	_forge_event_team_red,
	_forge_event_team_blue,
	NUMBER_OF_FORGE_EVENT_TEAMS
};

/* an event's flags */
enum
{
	/* GLOBAL TIME's TRIGGER ON START */
	FORGE_EVENT_FLAG_ON_START = 1 << 0,
	/* GROUP's RESETS: once fired, it counts its events again */
	FORGE_EVENT_FLAG_RESETS = 1 << 1,
	FORGE_EVENT_FLAGS_ALL = FORGE_EVENT_FLAG_ON_START | FORGE_EVENT_FLAG_RESETS
};

/* the rows of an event's Object Settings (forge_3d_mode.c) */
enum forge_event_row
{
	_forge_event_row_name = 0,
	_forge_event_row_type,
	_forge_event_row_time_mode,
	_forge_event_row_seconds,
	_forge_event_row_on_start,
	_forge_event_row_team,
	_forge_event_row_game_mode,
	_forge_event_row_trigger,
	_forge_event_row_amount,
	/* (INTERACT's DEVICE, the panel's model, and DEVICE NAME, its prompt's
	"Press X to ..." (typed: FORGE_EVENT_PRESS_DEVICE_NAME), the model's name
	when empty) */
	_forge_event_row_device,
	_forge_event_row_device_name,
	/* (GROUP's FIRES WHEN (ANY, ALL: its `trigger`), AFTER IT FIRES (RESETS),
	and its EVENT 1 to FORGE_EVENT_GROUP_MAX) */
	_forge_event_row_group_mode,
	_forge_event_row_group_reset,
	_forge_event_row_member_first,
	_forge_event_row_member_last = _forge_event_row_member_first + FORGE_EVENT_GROUP_MAX - 1,
	/* (TRIGGER VOLUME's VOLUME and WHEN A PLAYER (ENTERS, LEAVES: its `trigger`)) */
	_forge_event_row_volume,
	_forge_event_row_volume_when,
	/* (TEST EVENT: Spartan mode, a countdown, then fired as in a game:
	forge_3d_event_test_begin) */
	_forge_event_row_test,
	_forge_event_row_delete,
	NUMBER_OF_FORGE_EVENT_ROWS
};

/* what the engine tells (forge_events_note: the host's). The engine's files
call it by these numbers, each with a #define of its own: keep them */
enum forge_event_note
{
	_forge_event_note_ctf_captured = 0,
	_forge_event_note_ctf_stolen,
	_forge_event_note_death,
	_forge_event_note_hill_moved,
	_forge_event_note_ball_spawned,
	NUMBER_OF_FORGE_EVENT_NOTES
};

/* forge_event_row_step's TEST EVENT pressed (past changed, 1) */
#define FORGE_EVENT_PRESS_TEST 2
#define FORGE_EVENT_PRESS_DEVICE_NAME 3
/* (TEST EVENT's countdown, milliseconds) */
#define FORGE_EVENT_TEST_DELAY_MS 5000u

/* a marker's TEST OBJECTIVE (forge_objective_test_kind) */
enum
{
	FORGE_OBJECTIVE_TEST_NONE = 0,
	FORGE_OBJECTIVE_TEST_FLAG,
	FORGE_OBJECTIVE_TEST_BALL,
	FORGE_OBJECTIVE_TEST_HILL
};

/* ---------- structures */

struct forge_event
{
	uint8_t used;
	uint8_t number;
	uint8_t type;
	/* GLOBAL TIME's TIME */
	uint8_t time_mode;
	/* FORGE_EVENT_FLAG_* */
	uint8_t flags;
	/* PLAYER SCORE's */
	uint8_t game_mode;
	uint8_t trigger;
	uint8_t team;
	/* GLOBAL TIME's SECONDS; ANY's POINTS; SLAYER's PLAYERS DEAD */
	uint16_t value;
	char name[FORGE_EVENT_NAME_MAX + 1];
	/* INTERACT's DEVICE (its list's index) and DEVICE NAME (empty: the model's) */
	uint8_t model;
	char device_name[FORGE_EVENT_NAME_MAX + 1];
	/* GROUP's events, by number (0: none) */
	uint8_t members[FORGE_EVENT_GROUP_MAX];
};

/* ---------- prototypes/FORGE_EVENTS.C */

/* a live instance's event (NULL: none) */
struct forge_event const *forge_event_get(int32_t live_idx);
int forge_event_is(int32_t live_idx);
/* the live index of the event of this number, -1 for none */
int32_t forge_event_find(int number);
/* the events' numbers, lowest first: how many (at most `max`) */
int forge_events_numbers(int *numbers, int max);
/* an event's number's name, for the rows that wait on it ("#3 FLAG GONE", or
"#3 (MISSING)" when none has it) */
void forge_event_number_label(int number, char *text, unsigned long size);
/* the next binding from `number` (0: none, then each event's number in
order), `dir` -1/1 */
int forge_event_number_step(int number, int dir);

/* an event made for a live instance just spawned (a new number, unless
`number` is a free one: a peer's, a saved line's) */
void forge_event_spawned(int32_t live_idx);
/* a peer's !FX: fields, or a saved line's; its name */
void forge_event_apply(int32_t live_idx, int number, int type, int time_mode, int flags, int game_mode, int trigger,
	int team, int value);
void forge_event_name_set(int32_t live_idx, char const *name, int broadcast);
/* INTERACT's DEVICE and DEVICE NAME set (a row's, a peer's !FXD:, a saved
EVENT_DEVICE| line's); its prompt's text as it shows (the model's name for none) */
void forge_event_device_set(int32_t live_idx, int model, char const *device_name, int broadcast);
void forge_event_device_label(int32_t live_idx, char *text, unsigned long size);
int forge_event_device_packet(int32_t live_idx, char *text, unsigned long size);
/* GROUP's events set (a row's, a peer's !FXG:, a saved EVENT_GROUP| line's) */
void forge_event_members_set(int32_t live_idx, uint8_t const members[FORGE_EVENT_GROUP_MAX], int broadcast);
int forge_event_members_packet(int32_t live_idx, char *text, unsigned long size);
void forge_events_load_group_line(char const *line);
void forge_network_send_event_members(char const *packet);
/* the settings set, and sent to the session's others (broadcast) */
void forge_event_set(int32_t live_idx, struct forge_event const *settings, int broadcast);
/* a level going: every event forgotten */
void forge_events_level_reset(void);

/* the text of an event's rows (forge_3d_mode.c's panel); its rows shown, in
order (room for NUMBER_OF_FORGE_EVENT_ROWS); a row stepped (`dir` -1/1), 1
when it changed something */
void forge_event_row_text(int32_t live_idx, int row, char *label, unsigned long label_size, char *value,
	unsigned long value_size);
int forge_event_rows(int32_t live_idx, int *rows);
int forge_event_row_step(int32_t live_idx, int row, int dir);
/* an event's name and what it does ("FLAG GONE: CTF FLAG STOLEN") */
void forge_event_describe(int32_t live_idx, char *text, unsigned long size);

/* the saved map's lines (forge_cache_builder.c): how many bytes */
int forge_events_save_lines(char *text, unsigned long size);
/* a saved line (forge_runtime_tags.c's EVENT|...): the event made */
void forge_events_load_line(char const *line);
/* (EVENT_DEVICE|number|model|name, after its EVENT line) */
void forge_events_load_device_line(char const *line);

/* the network's (forge_network.c): an event's !FX: and !FXN: packets */
int forge_event_packet(int32_t live_idx, char *text, unsigned long size);
int forge_event_name_packet(int32_t live_idx, char *text, unsigned long size);
void forge_network_send_event(int32_t live_idx);
void forge_network_send_event_name(int32_t live_idx);
/* an INTERACT's !FXD: packet, as forge_event_device_packet wrote it */
void forge_network_send_event_device(char const *packet);
/* a live instance's binding (an object's SPAWN ON, an AI spawn's RESPAWN ON,
an effect's REPEAT) as !FB: */
void forge_network_send_event_binding(int32_t live_idx, int number);

/* each frame of a level (forge_3d_update, in Forge and in a match of the
map): the host's events looked for and fired */
void forge_events_update(void);
/* an event fired now, as in a game (the host's: what waits on it, and the
clients told) */
void forge_events_fire(int number);

/* the engine's (the host's; a client's are left alone): a thing that happened,
and the team of who did it (0 red, 1 blue, NONE: none, or a free for all) */
void forge_events_note(int note, long team_index);
/* a KOTH hill's state each tick (game_engine_king.c): whether a team holds
it (held), and which */
void forge_events_king_hill(long hill_id, int held, long team_index);

/* TEST EVENT (forge_3d_mode.c): back in Spartan mode, the event fired
FORGE_EVENT_TEST_DELAY_MS later, counted down on the screen */
void forge_3d_event_test_begin(int32_t live_idx);

/* TEST OBJECTIVE, a Forge game's (its CTF flags, oddball spawns and hills,
which a Forge game does not play): the kind a marker can test, whether it is
on, on or off, each frame (the host's), and a level's reset. See the code */
int forge_objective_test_kind(int32_t live_idx);
int forge_objective_test_on(int32_t live_idx);
void forge_objective_test_toggle(int32_t live_idx);
void forge_objective_tests_update(void);
void forge_objective_tests_reset(void);
/* the hill tested, to draw (forge_runtime_tags.c, in either mode): its circle's
middle and radius, whether a team holds it and which (0 red, 1 blue, NONE);
FALSE with none tested */
int forge_objective_test_zone(float middle[3], float *radius, int *held, long *team);
#define FORGE_OBJECTIVE_TEST_HILL_HEIGHT 2.5f

/* INTERACT's holopads (every machine's): drawn by `draw_line` (forge_runtime_tags.c's
volume lines: their color, whether highlighted), and the local player's
prompt near one ("USE: #2 OPEN THE GATE"), FALSE with none near */
#define FORGE_HOLOPAD_RADIUS 0.9f
int forge_events_holopad_count(void);
/* each frame, every machine's: the holopads' control panels made where the
INTERACT events stand (each machine its own, as scenery is), moved with them */
void forge_events_holopads_update(void);
/* whether an object is a holopad's panel (Forge leaves it be: not adopted) */
int forge_events_holopad_object(long object_index);
void forge_events_holopads_render(void (*draw_line)(float const *from, float const *to, int color, int highlighted));
/* the engine's (devices.c's device_touched, hud.c's device prompt): a holopad
used (TRUE: it was one, the host fires its event if its USED BY and COOLDOWN
let it); its prompt's text, its DEVICE NAME (NULL: no holopad) */
int forge_events_holopad_touched(long device_index, long unit_index);
wchar_t const *forge_events_holopad_prompt_text(long device_index);
/* INTERACT's DEVICE: the control panels a holopad can be (forge_events.c's
list; those the level cannot read are stepped past) */
int forge_events_holopad_model_count(void);

/* Forge mode's lines between events and what they link (forge_runtime_tags.c):
an object, door, effect or AI spawn to the event it waits on, a GROUP to its
events, a TRIGGER VOLUME event to its volume; drawn where one end is held or
pointed at (holding an event: all of its) */
void forge_events_links_render(void (*draw_line)(float const *from, float const *to));

/* distributed message 168 (network_distributed.c): the host's events fired,
to each client */
int forge_events_handles_message(unsigned short const *message, unsigned short size);
void forge_events_handle_message(long machine_index, unsigned short const *message, unsigned short size);

/* ---------- what waits on an event (forge_3d_mode.c, forge_ai.c) */

/* a live instance's binding, set (the Object Settings row, a peer's !FB:, a
saved map's line) and read */
void forge_3d_event_binding_apply(int32_t live_idx, int number);
int forge_3d_event_binding(int32_t live_idx);
/* the objects and effect spots waiting on an event: a game begun (a match's
alone: those waiting taken away till their event); an event fired (weapons,
equipment and vehicles: the host's; emitters and effect spots: every
machine's). `host`: fired here, not heard of from the host */
void forge_3d_events_game_start(void);
/* each frame (forge_3d_update): those still waiting on their event taken away
again, however they came back (a match's objects made here; a Forge
session's emitters) */
void forge_3d_events_waiting_update(void);
void forge_3d_event_fired(int number, int host);
/* an object the gametype's vehicle respawn leaves alone: one waiting on an
event (game_engine.c) */
int forge_events_object_bound(long object_index);
/* an AI spawn's squads waiting on an event brought back up (the host's) */
void forge_ai_event_fired(int number);

#endif
