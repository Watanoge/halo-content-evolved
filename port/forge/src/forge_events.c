/*
FORGE_EVENTS.C

Forge's events (forge_events.h): what each is, kept by its live instance's
index; their rows in Object Settings, their saved lines and packets; the
host's watch on the game (its time, its scores, what the engine tells:
forge_events_note) and the events it fires; the clients told of each one
(distributed message 168).
*/

#include "cseries.h"
#include "cseries/errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "memory/data.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/weapons.h"
#include "networking/network_game_globals.h"
#include "networking/network_connection.h"
#include "network_distributed.h"

#include "forge_engine.h"
#include "forge_events.h"

/* ---------- constants */

/* (the things the engine told, waiting for the next update: a tick has a
handful at most) */
#define MAXIMUM_PENDING_NOTES 64
/* (the KOTH hills followed at once: Multi-Hill's) */
#define MAXIMUM_FOLLOWED_HILLS 16
/* (the players' scores followed in a free for all, by absolute index) */
#define MAXIMUM_SCORED_PLAYERS 128

/* the notes past the engine's (forge_events_note), of the hills and the ball
followed here */
enum
{
	_note_king_captured = NUMBER_OF_FORGE_EVENT_NOTES,
	_note_king_lost,
	_note_ball_dropped,
	_note_ball_grabbed
};

/* ---------- globals */

/* (one per live instance: on the heap, FORGE_HEAP_STATIC, which the Android
image has no room for) */
struct forge_events_table
{
	struct forge_event events[FORGE_MAX_LIVE_INSTANCES];
};
FORGE_HEAP_STATIC(struct forge_events_table, s_events_table)

static struct
{
	/* the game the events are watching (its first tick; NONE: none yet), and
	the last tick seen */
	long start_tick;
	long last_tick;
	int started;
	/* (the scores as last seen: each team's, each player's) */
	int scores_known;
	long team_scores[2];
	long player_scores[MAXIMUM_SCORED_PLAYERS];
	uint8_t player_scored[MAXIMUM_SCORED_PLAYERS];
	/* (SLAYER's PLAYERS DEAD counted so far, by event number) */
	uint16_t deaths[FORGE_EVENT_MAX_NUMBER + 1];
	/* (the balls' carriers, and their teams) */
	long ball_carrier[MAXIMUM_ODDBALLS];
	long ball_team[MAXIMUM_ODDBALLS];
	/* (the hills, by their ids, and who holds them) */
	struct
	{
		long hill_id;
		int held;
		long team;
	} hills[MAXIMUM_FOLLOWED_HILLS];
	int hill_count;
	/* (what the engine told since the last update) */
	struct
	{
		int note;
		long team;
	} pending[MAXIMUM_PENDING_NOTES];
	int pending_count;
} s_watch = { NONE, NONE };

static char const *const s_type_names[NUMBER_OF_FORGE_EVENT_TYPES] = { "GLOBAL TIME", "PLAYER SCORE" };
static char const *const s_time_mode_names[NUMBER_OF_FORGE_EVENT_TIME_MODES] = { "ONCE EVERY", "ONCE AT" };
static char const *const s_game_mode_names[NUMBER_OF_FORGE_EVENT_GAME_MODES] = {
	"ANY", "CTF", "SLAYER", "KING OF THE HILL", "ODDBALL"
};
static char const *const s_team_names[NUMBER_OF_FORGE_EVENT_TEAMS] = { "ANY", "RED TEAM", "BLUE TEAM" };
static char const *const s_ctf_trigger_names[] = { "FLAG CAPTURED", "FLAG STOLEN" };
static char const *const s_king_trigger_names[] = { "ZONE CAPTURED", "ZONE LOST", "ZONE CHANGED" };
static char const *const s_oddball_trigger_names[] = { "BALL SPAWNED", "BALL DROPPED", "BALL GRABBED" };

/* SECONDS', POINTS' and PLAYERS DEAD's steps */
static int const s_seconds_steps[] = {
	5, 10, 15, 20, 30, 45, 60, 90, 120, 150, 180, 240, 300, 420, 600, 900, 1200, 1800, 2700, 3600
};
static int const s_points_steps[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 40, 50, 75, 100, 150, 200 };
static int const s_deaths_steps[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15, 20, 25, 30, 40, 50 };

/* ---------- private code */

static struct forge_event *events(void)
{
	return FORGE_HEAP(s_events_table).events;
}

static struct forge_live_instance *event_instance(int32_t live_idx)
{
	struct forge_live_instance *inst;

	if (live_idx < 0 || live_idx >= g_forge.live_instance_count || live_idx >= FORGE_MAX_LIVE_INSTANCES)
		return NULL;
	inst = &g_forge.live_instances[live_idx];
	return inst->active && inst->group_tag == FORGE_GROUP_EVENT && events()[live_idx].used ? inst : NULL;
}

static int number_taken(int number, int32_t except)
{
	int32_t i;

	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		if (i != except && event_instance(i) && events()[i].number == number)
			return 1;
	}
	return 0;
}

static int free_number(int32_t except)
{
	int number;

	for (number = 1; number <= FORGE_EVENT_MAX_NUMBER; number++)
	{
		if (!number_taken(number, except))
			return number;
	}
	return 0;
}

static int step_table(int const *table, int count, int value, int dir)
{
	int k, at = 0;

	for (k = 0; k < count; k++)
	{
		if (table[k] <= value)
			at = k;
	}
	return table[(at + count + dir) % count];
}

static int trigger_count(int game_mode)
{
	switch (game_mode)
	{
	case _forge_event_mode_ctf: return (int)NUMBEROF(s_ctf_trigger_names);
	case _forge_event_mode_king: return (int)NUMBEROF(s_king_trigger_names);
	case _forge_event_mode_oddball: return (int)NUMBEROF(s_oddball_trigger_names);
	default: return 1;
	}
}

static char const *trigger_name(int game_mode, int trigger)
{
	if (trigger < 0 || trigger >= trigger_count(game_mode))
		trigger = 0;
	switch (game_mode)
	{
	case _forge_event_mode_ctf: return s_ctf_trigger_names[trigger];
	case _forge_event_mode_king: return s_king_trigger_names[trigger];
	case _forge_event_mode_oddball: return s_oddball_trigger_names[trigger];
	case _forge_event_mode_slayer: return "PLAYERS DEAD";
	default: return "SCORE";
	}
}

/* whether a trigger is of who did it (ZONE CHANGED and BALL SPAWNED are no
one's) */
static int trigger_has_team(struct forge_event const *event)
{
	return !((event->game_mode == _forge_event_mode_king && event->trigger == _forge_event_king_changed) ||
		(event->game_mode == _forge_event_mode_oddball && event->trigger == _forge_event_oddball_spawned));
}

/* the default amount of what the event counts */
static int default_value(int type, int game_mode)
{
	if (type == _forge_event_global_time)
		return 60;
	return game_mode == _forge_event_mode_slayer ? 5 : 10;
}

/* an event's settings kept within their ranges */
static void event_clamp(struct forge_event *event)
{
	if (event->type >= NUMBER_OF_FORGE_EVENT_TYPES)
		event->type = _forge_event_global_time;
	if (event->time_mode >= NUMBER_OF_FORGE_EVENT_TIME_MODES)
		event->time_mode = _forge_event_time_every;
	if (event->game_mode >= NUMBER_OF_FORGE_EVENT_GAME_MODES)
		event->game_mode = _forge_event_mode_any;
	if (event->trigger >= trigger_count(event->game_mode))
		event->trigger = 0;
	if (event->team >= NUMBER_OF_FORGE_EVENT_TEAMS)
		event->team = _forge_event_team_any;
	event->flags &= FORGE_EVENT_FLAGS_ALL;
	if (event->value == 0)
		event->value = (uint16_t)default_value(event->type, event->game_mode);
}

static void seconds_text(int seconds, char *text, unsigned long size)
{
	if (seconds >= 60)
		snprintf(text, size, "%d:%02d", seconds / 60, seconds % 60);
	else
		snprintf(text, size, "%d SECONDS", seconds);
}

/* whether a team (0 red, 1 blue, NONE) is the one an event wants */
/* (a test objective's team, forge_objective_tests_update: whose it is is
known in a Forge game, which has no teams) */
#define TEST_TEAM_BIT 0x100L

static int team_matches(struct forge_event const *event, long team_index)
{
	if (event->team == _forge_event_team_any || !trigger_has_team(event))
		return 1;
	if (team_index != NONE && (team_index & TEST_TEAM_BIT))
		return (team_index & 0xFF) == (long)(event->team - _forge_event_team_red);
	if (!game_engine_running() || !game_engine_has_teams())
		return 0;
	return team_index == (long)(event->team - _forge_event_team_red);
}

/* whether this machine runs the events: the host, or a game with no network */
static int events_host(void)
{
	return game_connection() != _game_connection_network_client;
}

static int game_engine_index_now(void)
{
	return game_engine_running() ? (int)game_engine_get_variant()->game_engine_index : NONE;
}

/* a thing that happened, to the events that wait on it */
static void note_fire(int note, long team_index)
{
	int32_t i;

	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		struct forge_event *event;
		int wanted = 0;

		if (!event_instance(i))
			continue;
		event = &events()[i];
		if (event->type != _forge_event_player_score)
			continue;
		switch (event->game_mode)
		{
		case _forge_event_mode_ctf:
			wanted = (note == _forge_event_note_ctf_captured && event->trigger == _forge_event_ctf_captured) ||
				(note == _forge_event_note_ctf_stolen && event->trigger == _forge_event_ctf_stolen);
			break;
		case _forge_event_mode_slayer:
			/* (a Slayer game's: the other modes' triggers are theirs alone too) */
			if (note == _forge_event_note_death && game_engine_index_now() == game_engine_slayer &&
				team_matches(event, team_index) && event->number <= FORGE_EVENT_MAX_NUMBER)
			{
				/* (PLAYERS DEAD: each time its count is reached) */
				if (++s_watch.deaths[event->number] >= MAX(1, event->value))
				{
					s_watch.deaths[event->number] = 0;
					forge_events_fire(event->number);
				}
			}
			continue;
		case _forge_event_mode_king:
			wanted = (note == _note_king_captured && event->trigger == _forge_event_king_captured) ||
				(note == _note_king_lost && event->trigger == _forge_event_king_lost) ||
				(note == _forge_event_note_hill_moved && event->trigger == _forge_event_king_changed);
			break;
		case _forge_event_mode_oddball:
			wanted = (note == _forge_event_note_ball_spawned && event->trigger == _forge_event_oddball_spawned) ||
				(note == _note_ball_dropped && event->trigger == _forge_event_oddball_dropped) ||
				(note == _note_ball_grabbed && event->trigger == _forge_event_oddball_grabbed);
			break;
		default:
			break;
		}
		if (wanted && team_matches(event, team_index))
			forge_events_fire(event->number);
	}
}

static void note_queue(int note, long team_index)
{
	if (!events_host() || s_watch.pending_count >= MAXIMUM_PENDING_NOTES)
		return;
	s_watch.pending[s_watch.pending_count].note = note;
	s_watch.pending[s_watch.pending_count].team = team_index;
	s_watch.pending_count++;
}

static long player_team(long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	if (!player || !game_engine_running() || !game_engine_has_teams())
		return NONE;
	return player->team_index;
}

/* the scores as they are now: each team's (0 and 1), or each player's */
static void scores_read(long team_scores[2], long *player_scores, uint8_t *player_scored)
{
	struct data_iterator iterator;
	struct player_datum *player;

	team_scores[0] = team_scores[1] = 0;
	memset(player_scored, 0, MAXIMUM_SCORED_PLAYERS);
	if (!game_engine_running() || !game_engine)
		return;
	if (game_engine_has_teams())
	{
		team_scores[0] = game_engine_get_team_score(0);
		team_scores[1] = game_engine_get_team_score(1);
		return;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		if (absolute < 0 || absolute >= MAXIMUM_SCORED_PLAYERS)
			continue;
		player_scores[absolute] = game_engine->get_player_score(iterator.datum_index, _get_score_individual);
		player_scored[absolute] = 1;
	}
}

/* a score from `was` to `now` past another multiple of an event's POINTS */
static int score_passed(struct forge_event const *event, long was, long now)
{
	long step = MAX(1, (long)event->value);

	return now > was && now / step > was / step;
}

/* ANY's scores, looked at once a frame */
static void scores_watch(void)
{
	long team_scores[2];
	long player_scores[MAXIMUM_SCORED_PLAYERS];
	uint8_t player_scored[MAXIMUM_SCORED_PLAYERS];
	int32_t i;

	memcpy(player_scores, s_watch.player_scores, sizeof(player_scores));
	scores_read(team_scores, player_scores, player_scored);
	if (s_watch.scores_known)
	{
		for (i = 0; i < g_forge.live_instance_count; i++)
		{
			struct forge_event const *event;
			int fired = 0;
			int k;

			if (!event_instance(i))
				continue;
			event = &events()[i];
			if (event->type != _forge_event_player_score || event->game_mode != _forge_event_mode_any)
				continue;
			if (game_engine_running() && game_engine_has_teams())
			{
				for (k = 0; k < 2 && !fired; k++)
				{
					if (team_matches(event, k) && score_passed(event, s_watch.team_scores[k], team_scores[k]))
						fired = 1;
				}
			}
			else if (event->team == _forge_event_team_any)
			{
				for (k = 0; k < MAXIMUM_SCORED_PLAYERS && !fired; k++)
				{
					if (player_scored[k] && s_watch.player_scored[k] &&
						score_passed(event, s_watch.player_scores[k], player_scores[k]))
					{
						fired = 1;
					}
				}
			}
			if (fired)
				forge_events_fire(event->number);
		}
	}
	s_watch.team_scores[0] = team_scores[0];
	s_watch.team_scores[1] = team_scores[1];
	memcpy(s_watch.player_scores, player_scores, sizeof(player_scores));
	memcpy(s_watch.player_scored, player_scored, sizeof(player_scored));
	s_watch.scores_known = 1;
}

/* Oddball's balls: who carries each, grabbed and dropped */
static void balls_watch(void)
{
	short index;

	if (game_engine_index_now() != game_engine_oddball)
		return;
	for (index = 0; index < MAXIMUM_ODDBALLS; index++)
	{
		long carrier = game_engine_oddball_carrier(index);

		if (carrier != NONE && !player_try_and_get(carrier))
			carrier = NONE;
		if (carrier == s_watch.ball_carrier[index])
			continue;
		if (s_watch.ball_carrier[index] != NONE)
			note_fire(_note_ball_dropped, s_watch.ball_team[index]);
		s_watch.ball_carrier[index] = carrier;
		s_watch.ball_team[index] = player_team(carrier);
		if (carrier != NONE)
			note_fire(_note_ball_grabbed, s_watch.ball_team[index]);
	}
}

/* GLOBAL TIME's, from the last tick seen to this one */
static void time_watch(long was, long now)
{
	int32_t i;

	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		struct forge_event const *event;
		long period, from, to;

		if (!event_instance(i))
			continue;
		event = &events()[i];
		if (event->type != _forge_event_global_time)
			continue;
		period = MAX(1, (long)event->value) * TICKS_PER_SECOND;
		from = was - s_watch.start_tick;
		to = now - s_watch.start_tick;
		if (event->time_mode == _forge_event_time_once_at)
		{
			if (from < period && to >= period)
				forge_events_fire(event->number);
		}
		else if (to / period > MAX(from, 0) / period)
		{
			forge_events_fire(event->number);
		}
	}
}

static void watch_reset(long now)
{
	int k;

	s_watch.start_tick = now;
	s_watch.last_tick = now;
	s_watch.started = 0;
	s_watch.scores_known = 0;
	s_watch.hill_count = 0;
	memset(s_watch.deaths, 0, sizeof(s_watch.deaths));
	for (k = 0; k < MAXIMUM_ODDBALLS; k++)
	{
		s_watch.ball_carrier[k] = NONE;
		s_watch.ball_team[k] = NONE;
	}
}

/* ---------- public code */

struct forge_event const *forge_event_get(int32_t live_idx)
{
	return event_instance(live_idx) ? &events()[live_idx] : NULL;
}

int forge_event_is(int32_t live_idx)
{
	return event_instance(live_idx) != NULL;
}

int32_t forge_event_find(int number)
{
	int32_t i;

	if (number <= 0)
		return -1;
	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		if (event_instance(i) && events()[i].number == number)
			return i;
	}
	return -1;
}

int forge_events_numbers(int *numbers, int max)
{
	int count = 0;
	int number;

	for (number = 1; number <= FORGE_EVENT_MAX_NUMBER && count < max; number++)
	{
		if (forge_event_find(number) >= 0)
			numbers[count++] = number;
	}
	return count;
}

void forge_event_number_label(int number, char *text, unsigned long size)
{
	int32_t live_idx = forge_event_find(number);

	if (live_idx < 0)
		snprintf(text, size, "#%d (MISSING)", number);
	else if (events()[live_idx].name[0])
		snprintf(text, size, "#%d %s", number, events()[live_idx].name);
	else
		snprintf(text, size, "EVENT #%d", number);
}

int forge_event_number_step(int number, int dir)
{
	int numbers[FORGE_EVENT_MAX_NUMBER + 1];
	int count = 1 + forge_events_numbers(numbers + 1, FORGE_EVENT_MAX_NUMBER);
	int k, at = 0;

	numbers[0] = 0;
	for (k = 0; k < count; k++)
	{
		if (numbers[k] == number)
			at = k;
	}
	return numbers[(at + count + dir) % count];
}

void forge_event_spawned(int32_t live_idx)
{
	struct forge_live_instance *inst;
	struct forge_event *event;
	int wanted;

	if (live_idx < 0 || live_idx >= g_forge.live_instance_count || live_idx >= FORGE_MAX_LIVE_INSTANCES)
		return;
	inst = &g_forge.live_instances[live_idx];
	event = &events()[live_idx];
	/* (its number: the one it comes with, a peer's or a saved line's, when
	it is free) */
	wanted = inst->team_designator;
	memset(event, 0, sizeof(*event));
	event->used = 1;
	event->number = (uint8_t)(wanted > 0 && wanted <= FORGE_EVENT_MAX_NUMBER && !number_taken(wanted, live_idx) ?
		wanted : free_number(live_idx));
	event->type = _forge_event_global_time;
	event->time_mode = _forge_event_time_every;
	event->value = (uint16_t)default_value(event->type, event->game_mode);
	snprintf(event->name, sizeof(event->name), "EVENT %d", (int)event->number);
	inst->team_designator = (int16_t)event->number;
	if (!event->number)
	{
		event->used = 0;
		forge_set_status("Event Limit Reached (%d)!", FORGE_EVENT_MAX_NUMBER);
	}
}

void forge_event_apply(int32_t live_idx, int number, int type, int time_mode, int flags, int game_mode, int trigger,
	int team, int value)
{
	struct forge_event *event;

	if (!event_instance(live_idx))
		return;
	event = &events()[live_idx];
	if (number > 0 && number <= FORGE_EVENT_MAX_NUMBER && !number_taken(number, live_idx))
	{
		event->number = (uint8_t)number;
		g_forge.live_instances[live_idx].team_designator = (int16_t)number;
	}
	event->type = (uint8_t)PIN(type, 0, 255);
	event->time_mode = (uint8_t)PIN(time_mode, 0, 255);
	event->flags = (uint8_t)PIN(flags, 0, 255);
	event->game_mode = (uint8_t)PIN(game_mode, 0, 255);
	event->trigger = (uint8_t)PIN(trigger, 0, 255);
	event->team = (uint8_t)PIN(team, 0, 255);
	event->value = (uint16_t)PIN(value, 0, 65535);
	event_clamp(event);
}

void forge_event_name_set(int32_t live_idx, char const *name, int broadcast)
{
	struct forge_event *event;
	int k, length = 0;

	if (!event_instance(live_idx) || !name)
		return;
	event = &events()[live_idx];
	/* (letters, digits and the keyboard's marks; never the lines' '|' nor
	the packets' ':') */
	for (k = 0; name[k] && length < FORGE_EVENT_NAME_MAX; k++)
	{
		char c = name[k];

		if (c == '|' || c == ':' || c == '\r' || c == '\n' || (unsigned char)c < 0x20)
			continue;
		event->name[length++] = c;
	}
	event->name[length] = 0;
	if (!event->name[0])
		snprintf(event->name, sizeof(event->name), "EVENT %d", (int)event->number);
	if (broadcast)
		forge_network_send_event_name(live_idx);
}

void forge_event_set(int32_t live_idx, struct forge_event const *settings, int broadcast)
{
	if (!settings || !event_instance(live_idx))
		return;
	forge_event_apply(live_idx, settings->number, settings->type, settings->time_mode, settings->flags,
		settings->game_mode, settings->trigger, settings->team, settings->value);
	if (strcmp(events()[live_idx].name, settings->name) != 0)
		forge_event_name_set(live_idx, settings->name, broadcast);
	if (broadcast)
		forge_network_send_event(live_idx);
}

void forge_events_level_reset(void)
{
	memset(events(), 0, sizeof(struct forge_event) * FORGE_MAX_LIVE_INSTANCES);
	/* (what the engine told as the level began, its first ball, is kept: the
	queue is emptied each frame a game runs) */
	watch_reset(NONE);
	forge_objective_tests_reset();
	s_watch.start_tick = NONE;
	s_watch.last_tick = NONE;
}

int forge_event_rows(int32_t live_idx, int *rows)
{
	struct forge_event const *event = forge_event_get(live_idx);
	int count = 0;

	if (!event)
		return 0;
	rows[count++] = _forge_event_row_name;
	rows[count++] = _forge_event_row_type;
	if (event->type == _forge_event_global_time)
	{
		rows[count++] = _forge_event_row_time_mode;
		rows[count++] = _forge_event_row_seconds;
		rows[count++] = _forge_event_row_on_start;
	}
	else
	{
		rows[count++] = _forge_event_row_game_mode;
		if (trigger_count(event->game_mode) > 1)
			rows[count++] = _forge_event_row_trigger;
		if (trigger_has_team(event))
			rows[count++] = _forge_event_row_team;
		if (event->game_mode == _forge_event_mode_any || event->game_mode == _forge_event_mode_slayer)
			rows[count++] = _forge_event_row_amount;
	}
	rows[count++] = _forge_event_row_test;
	rows[count++] = _forge_event_row_delete;
	return count;
}

void forge_event_row_text(int32_t live_idx, int row, char *label, unsigned long label_size, char *value,
	unsigned long value_size)
{
	struct forge_event const *event = forge_event_get(live_idx);

	label[0] = 0;
	value[0] = 0;
	if (!event)
		return;
	switch (row)
	{
	case _forge_event_row_name:
		snprintf(label, label_size, "NAME:");
		snprintf(value, value_size, "%s", event->name);
		break;
	case _forge_event_row_type:
		snprintf(label, label_size, "TYPE:");
		snprintf(value, value_size, "%s", s_type_names[event->type]);
		break;
	case _forge_event_row_time_mode:
		snprintf(label, label_size, "TIME:");
		snprintf(value, value_size, "%s", s_time_mode_names[event->time_mode]);
		break;
	case _forge_event_row_seconds:
		snprintf(label, label_size, event->time_mode == _forge_event_time_once_at ? "AT:" : "EVERY:");
		seconds_text(event->value, value, value_size);
		break;
	case _forge_event_row_on_start:
		snprintf(label, label_size, "TRIGGER ON START:");
		snprintf(value, value_size, "%s", (event->flags & FORGE_EVENT_FLAG_ON_START) ? "ON" : "OFF");
		break;
	case _forge_event_row_team:
		snprintf(label, label_size, event->game_mode == _forge_event_mode_slayer ? "DEAD PLAYERS' TEAM:" : "TEAM:");
		snprintf(value, value_size, "%s", s_team_names[event->team]);
		break;
	case _forge_event_row_game_mode:
		snprintf(label, label_size, "GAME MODE:");
		snprintf(value, value_size, "%s", s_game_mode_names[event->game_mode]);
		break;
	case _forge_event_row_trigger:
		snprintf(label, label_size, "TRIGGER:");
		snprintf(value, value_size, "%s", trigger_name(event->game_mode, event->trigger));
		break;
	case _forge_event_row_amount:
		if (event->game_mode == _forge_event_mode_slayer)
		{
			snprintf(label, label_size, "PLAYERS DEAD:");
			snprintf(value, value_size, "%d", (int)event->value);
		}
		else
		{
			snprintf(label, label_size, "SCORE:");
			snprintf(value, value_size, "EVERY %d POINT%s", (int)event->value, event->value == 1 ? "" : "S");
		}
		break;
	case _forge_event_row_test:
		snprintf(label, label_size, "TEST EVENT");
		break;
	case _forge_event_row_delete:
		snprintf(label, label_size, "DELETE OBJECT");
		break;
	default:
		break;
	}
}

int forge_event_row_step(int32_t live_idx, int row, int dir)
{
	struct forge_event *event;
	struct forge_event before;

	if (!event_instance(live_idx))
		return 0;
	event = &events()[live_idx];
	before = *event;
	switch (row)
	{
	case _forge_event_row_type:
		event->type = (uint8_t)((event->type + NUMBER_OF_FORGE_EVENT_TYPES + dir) % NUMBER_OF_FORGE_EVENT_TYPES);
		event->value = (uint16_t)default_value(event->type, event->game_mode);
		break;
	case _forge_event_row_time_mode:
		event->time_mode = (uint8_t)((event->time_mode + NUMBER_OF_FORGE_EVENT_TIME_MODES + dir) %
			NUMBER_OF_FORGE_EVENT_TIME_MODES);
		break;
	case _forge_event_row_seconds:
		event->value = (uint16_t)step_table(s_seconds_steps, (int)NUMBEROF(s_seconds_steps), event->value, dir);
		break;
	case _forge_event_row_on_start:
		event->flags ^= FORGE_EVENT_FLAG_ON_START;
		break;
	case _forge_event_row_team:
		event->team = (uint8_t)((event->team + NUMBER_OF_FORGE_EVENT_TEAMS + dir) % NUMBER_OF_FORGE_EVENT_TEAMS);
		break;
	case _forge_event_row_game_mode:
		event->game_mode = (uint8_t)((event->game_mode + NUMBER_OF_FORGE_EVENT_GAME_MODES + dir) %
			NUMBER_OF_FORGE_EVENT_GAME_MODES);
		event->trigger = 0;
		event->value = (uint16_t)default_value(event->type, event->game_mode);
		break;
	case _forge_event_row_trigger:
		event->trigger = (uint8_t)((event->trigger + trigger_count(event->game_mode) + dir) %
			trigger_count(event->game_mode));
		break;
	case _forge_event_row_amount:
		if (event->game_mode == _forge_event_mode_slayer)
			event->value = (uint16_t)step_table(s_deaths_steps, (int)NUMBEROF(s_deaths_steps), event->value, dir);
		else
			event->value = (uint16_t)step_table(s_points_steps, (int)NUMBEROF(s_points_steps), event->value, dir);
		break;
	case _forge_event_row_test:
		if (!events_host())
		{
			forge_set_status("Only the Host Tests Events");
			return 0;
		}
		return FORGE_EVENT_PRESS_TEST;
	default:
		return 0;
	}
	event_clamp(event);
	if (memcmp(&before, event, sizeof(before)) == 0)
		return 0;
	forge_network_send_event(live_idx);
	return 1;
}

void forge_event_describe(int32_t live_idx, char *text, unsigned long size)
{
	struct forge_event const *event = forge_event_get(live_idx);
	char what[64];

	if (!event)
	{
		snprintf(text, size, "EVENT");
		return;
	}
	if (event->type == _forge_event_global_time)
	{
		char seconds[24];

		seconds_text(event->value, seconds, sizeof(seconds));
		snprintf(what, sizeof(what), "%s %s%s", s_time_mode_names[event->time_mode], seconds,
			(event->flags & FORGE_EVENT_FLAG_ON_START) ? " + START" : "");
	}
	else if (event->game_mode == _forge_event_mode_any)
	{
		snprintf(what, sizeof(what), "%s SCORE EVERY %d", s_team_names[event->team], (int)event->value);
	}
	else if (event->game_mode == _forge_event_mode_slayer)
	{
		snprintf(what, sizeof(what), "%d %s DEAD", (int)event->value,
			event->team == _forge_event_team_any ? "PLAYERS" : s_team_names[event->team]);
	}
	else
	{
		snprintf(what, sizeof(what), "%s %s", s_game_mode_names[event->game_mode],
			trigger_name(event->game_mode, event->trigger));
	}
	snprintf(text, size, "#%d %s: %s", (int)event->number, event->name, what);
}

int forge_events_save_lines(char *text, unsigned long size)
{
	int length = 0;
	int32_t i;

	if (!text || !size)
		return 0;
	text[0] = 0;
	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		struct forge_live_instance const *inst = event_instance(i);
		struct forge_event const *event = &events()[i];
		int written;

		if (!inst)
			continue;
		written = snprintf(text + length, size - (unsigned long)length,
			"EVENT|%d|%d|%d|%d|%d|%d|%d|%d|%.4f|%.4f|%.4f|%.4f|%s\r\n",
			(int)event->number, (int)event->type, (int)event->time_mode, (int)event->flags, (int)event->game_mode,
			(int)event->trigger, (int)event->team, (int)event->value, inst->x, inst->y, inst->z, inst->yaw,
			event->name);
		if (written <= 0 || length + written >= (int)size)
			break;
		length += written;
	}
	return length;
}

void forge_events_load_line(char const *line)
{
	int number = 0, type = 0, time_mode = 0, flags = 0, game_mode = 0, trigger = 0, team = 0, value = 0;
	float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f;
	char name[FORGE_EVENT_NAME_MAX + 8];
	int32_t live_idx;

	if (!line || strncmp(line, "EVENT|", 6))
		return;
	name[0] = 0;
	if (sscanf(line + 6, "%d|%d|%d|%d|%d|%d|%d|%d|%f|%f|%f|%f|%27[^\r\n]", &number, &type, &time_mode, &flags,
		&game_mode, &trigger, &team, &value, &x, &y, &z, &yaw, name) < 12)
	{
		return;
	}
	live_idx = forge_3d_spawn_object_at(FORGE_GROUP_EVENT, FORGE_EVENT_PATH, 1, x, y, z, yaw, 0.0f, 0.0f,
		(int16_t)number, 0);
	if (live_idx < 0)
		return;
	forge_event_apply(live_idx, number, type, time_mode, flags, game_mode, trigger, team, value);
	forge_event_name_set(live_idx, name, 0);
}

int forge_event_packet(int32_t live_idx, char *text, unsigned long size)
{
	struct forge_event const *event = forge_event_get(live_idx);

	if (!event)
		return 0;
	snprintf(text, size, "!FX:%d:%d:%d:%d:%d:%d:%d:%d:%d", (int)live_idx, (int)event->number, (int)event->type,
		(int)event->time_mode, (int)event->flags, (int)event->game_mode, (int)event->trigger, (int)event->team,
		(int)event->value);
	return 1;
}

int forge_event_name_packet(int32_t live_idx, char *text, unsigned long size)
{
	struct forge_event const *event = forge_event_get(live_idx);

	if (!event)
		return 0;
	snprintf(text, size, "!FXN:%d:%s", (int)live_idx, event->name);
	return 1;
}

void forge_events_fire(int number)
{
	int32_t live_idx = forge_event_find(number);

	if (live_idx < 0 || !events_host())
		return;
	if (g_forge_session_enabled)
	{
		char what[96];

		forge_event_describe(live_idx, what, sizeof(what));
		forge_set_status("Event: %s", what);
	}
	error(_error_silent, "forge events: #%d (%s) fired at tick %ld", number, events()[live_idx].name,
		(long)game_time_get());
	forge_3d_event_fired(number, 1);
	forge_ai_event_fired(number);
	/* (the clients' effect spots) */
	if (game_connection() == _game_connection_network_server)
	{
		struct
		{
			struct distributed_message_header header;
			word number;
			word reserved;
		} message;

		memset(&message, 0, sizeof(message));
		message.number = (word)number;
		distributed_send(&message, _distributed_message_forge_event, 1, sizeof(message),
			_distributed_to_clients_reliably);
	}
}

void forge_events_update(void)
{
	long now = game_time_get();
	int k;

	/* (a new game: the time gone back, or none watched yet) */
	if (s_watch.start_tick == NONE || now < s_watch.last_tick)
		watch_reset(now);
	if (!s_watch.started)
	{
		int32_t i;

		/* (every machine's: a client's emitters are its own) */
		s_watch.started = 1;
		forge_3d_events_game_start();
		for (i = 0; events_host() && i < g_forge.live_instance_count; i++)
		{
			if (event_instance(i) && events()[i].type == _forge_event_global_time &&
				(events()[i].flags & FORGE_EVENT_FLAG_ON_START))
			{
				forge_events_fire(events()[i].number);
			}
		}
	}
	/* (the rest is the host's: a client hears of each event fired) */
	if (!events_host())
	{
		s_watch.pending_count = 0;
		s_watch.last_tick = now;
		return;
	}
	/* (none once the game is over) */
	if (game_engine_running() && !game_engine_can_score())
	{
		s_watch.pending_count = 0;
		s_watch.last_tick = now;
		return;
	}
	if (now > s_watch.last_tick)
		time_watch(s_watch.last_tick, now);
	s_watch.last_tick = now;
	for (k = 0; k < s_watch.pending_count; k++)
		note_fire(s_watch.pending[k].note, s_watch.pending[k].team);
	s_watch.pending_count = 0;
	scores_watch();
	balls_watch();
}

void forge_events_note(int note, long team_index)
{
	note_queue(note, team_index);
}

void forge_events_king_hill(long hill_id, int held, long team_index)
{
	int k;

	if (!events_host())
		return;
	for (k = 0; k < s_watch.hill_count && s_watch.hills[k].hill_id != hill_id; k++)
	{
	}
	if (k == s_watch.hill_count)
	{
		if (k >= MAXIMUM_FOLLOWED_HILLS)
			return;
		s_watch.hill_count++;
		s_watch.hills[k].hill_id = hill_id;
		s_watch.hills[k].held = 0;
		s_watch.hills[k].team = NONE;
	}
	held = held ? 1 : 0;
	if (!game_engine_has_teams())
		team_index = NONE;
	if (held == s_watch.hills[k].held && (!held || team_index == s_watch.hills[k].team))
		return;
	if (s_watch.hills[k].held)
		note_queue(_note_king_lost, s_watch.hills[k].team);
	if (held)
		note_queue(_note_king_captured, team_index);
	s_watch.hills[k].held = held;
	s_watch.hills[k].team = held ? team_index : NONE;
}

int forge_events_handles_message(unsigned short const *message, unsigned short size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	memcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_forge_event;
}

/* (the host's alone, over its stream, whole) */
void forge_events_handle_message(long machine_index, unsigned short const *data, unsigned short size)
{
	struct
	{
		struct distributed_message_header header;
		word number;
		word reserved;
	} message;

	if (machine_index != NONE || game_connection() != _game_connection_network_client ||
		size != sizeof(message))
		return;
	memcpy(&message, data, sizeof(message));
	if (message.header.type != _distributed_message_forge_event || message.header.count != 1 ||
		message.number == 0 || message.number > FORGE_EVENT_MAX_NUMBER)
		return;
	forge_3d_event_fired((int)message.number, 0);
}

/* ---------- objectives tested in Forge (forge_events.h's TEST OBJECTIVE)

A Forge game plays no CTF, King of the Hill or Oddball, so a CTF flag's,
hill's or oddball spawn's marker set to TEST OBJECTIVE plays its part here,
in Spartan mode (taken away in Forge mode, made again out of it), and tells
the events what the engine would: a flag made on its stand, STOLEN as it is
taken off it, CAPTURED brought to another flag's stand, back on its own once
it lies 15 seconds; a ball made at its spawn (SPAWNED), GRABBED and DROPPED,
back at its spawn once it lies 20 seconds; a hill (all its markers, by its
number) CAPTURED as one player's team stands in it alone, LOST as it no longer
does, and CHANGED as another hill's test takes its place. Its team is the
player's (red or blue: a Forge game's player has team 0, red, the host). The
host's alone; nothing of it is saved */

#define MAXIMUM_OBJECTIVE_TESTS 32
#define TEST_FLAG_RETURN_TICKS (15 * TICKS_PER_SECOND)
#define TEST_BALL_RETURN_TICKS (20 * TICKS_PER_SECOND)
/* (how near a stand a carried flag is captured, and a hill's least radius) */
#define TEST_CAPTURE_RADIUS 2.0f
#define TEST_HILL_MINIMUM_RADIUS 3.0f
#define TEST_HILL_HEIGHT 2.5f

extern long get_flag_definition_index(void);
extern long get_ball_definition_index(void);

static struct
{
	int32_t live_idx;
	int kind;
	long object_index;
	int at_home;
	long carrier;
	long carrier_team;
	long lying_since;
} s_objective_tests[MAXIMUM_OBJECTIVE_TESTS];
static int s_objective_test_count;
/* (the hill tested: its number, NONE for none, and who holds it) */
static long s_test_hill = NONE;
static int s_test_hill_held;
static long s_test_hill_team = NONE;

static int objective_test_find(int32_t live_idx)
{
	int k;

	for (k = 0; k < s_objective_test_count; k++)
	{
		if (s_objective_tests[k].live_idx == live_idx)
			return k;
	}
	return -1;
}

/* a player's team for the events: red or blue, marked a test's */
static long objective_test_team(long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	if (!player || player->team_index < 0 || player->team_index > 1)
		return NONE;
	return player->team_index | TEST_TEAM_BIT;
}

static void objective_test_object_delete(int k)
{
	if (s_objective_tests[k].object_index != NONE &&
		object_try_and_get_and_verify_type(s_objective_tests[k].object_index, _object_mask_weapon))
	{
		object_delete(s_objective_tests[k].object_index);
	}
	s_objective_tests[k].object_index = NONE;
	s_objective_tests[k].carrier = NONE;
	s_objective_tests[k].at_home = 1;
	s_objective_tests[k].lying_since = NONE;
}

/* its flag or ball made on its marker */
static void objective_test_object_make(int k)
{
	struct forge_live_instance const *inst = &g_forge.live_instances[s_objective_tests[k].live_idx];
	long definition_index = s_objective_tests[k].kind == FORGE_OBJECTIVE_TEST_FLAG ? get_flag_definition_index() :
		get_ball_definition_index();
	struct object_placement_data placement;
	struct weapon_datum *weapon;

	objective_test_object_delete(k);
	if (definition_index == NONE)
		return;
	object_placement_data_new(&placement, definition_index, NONE);
	placement.position.x = inst->x;
	placement.position.y = inst->y;
	placement.position.z = inst->z + 0.2f;
	s_objective_tests[k].object_index = object_new(&placement);
	weapon = (struct weapon_datum *)object_try_and_get_and_verify_type(s_objective_tests[k].object_index,
		_object_mask_weapon);
	if (!weapon)
	{
		s_objective_tests[k].object_index = NONE;
		return;
	}
	/* (a flag in its team's colour) */
	if (s_objective_tests[k].kind == FORGE_OBJECTIVE_TEST_FLAG)
		weapon->object.owner_team_index = inst->team_designator == 1 ? 1 : 0;
	object_set_automatic_deactivation(s_objective_tests[k].object_index, FALSE);
	if (s_objective_tests[k].kind == FORGE_OBJECTIVE_TEST_BALL)
		note_queue(_forge_event_note_ball_spawned, NONE);
}

/* the player carrying an object, NONE for none */
static long objective_test_carrier(long object_index)
{
	struct object_datum *object = object_try_and_get_and_verify_type(object_index, _object_mask_weapon);
	struct unit_datum *unit;

	if (!object || object->object.parent_object_index == NONE)
		return NONE;
	unit = (struct unit_datum *)object_try_and_get_and_verify_type(object->object.parent_object_index, _object_mask_unit);
	return unit ? unit->unit.player_index : NONE;
}

/* a carried flag at another flag's stand */
static int objective_test_at_other_stand(int k, long player_index)
{
	struct player_datum *player = player_try_and_get(player_index);
	struct object_datum *unit = player ? object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit) : NULL;
	int32_t i;

	if (!unit)
		return 0;
	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		struct forge_live_instance const *inst = &g_forge.live_instances[i];
		float dx, dy, dz;

		if (i == s_objective_tests[k].live_idx || !inst->active || forge_objective_test_kind(i) != FORGE_OBJECTIVE_TEST_FLAG)
			continue;
		dx = unit->object.position.x - inst->x;
		dy = unit->object.position.y - inst->y;
		dz = unit->object.position.z - inst->z;
		if (dx * dx + dy * dy + dz * dz < TEST_CAPTURE_RADIUS * TEST_CAPTURE_RADIUS)
			return 1;
	}
	return 0;
}

static void objective_test_flag_update(int k, long now)
{
	long carrier;

	if (!object_try_and_get_and_verify_type(s_objective_tests[k].object_index, _object_mask_weapon))
	{
		objective_test_object_make(k);
		return;
	}
	carrier = objective_test_carrier(s_objective_tests[k].object_index);
	if (carrier != NONE)
	{
		s_objective_tests[k].lying_since = NONE;
		if (s_objective_tests[k].carrier == NONE && s_objective_tests[k].at_home)
		{
			note_queue(_forge_event_note_ctf_stolen, objective_test_team(carrier));
			forge_set_status("Test flag stolen");
		}
		s_objective_tests[k].at_home = 0;
		s_objective_tests[k].carrier = carrier;
		if (objective_test_at_other_stand(k, carrier))
		{
			note_queue(_forge_event_note_ctf_captured, objective_test_team(carrier));
			forge_set_status("Test flag captured");
			objective_test_object_make(k);
		}
		return;
	}
	s_objective_tests[k].carrier = NONE;
	if (s_objective_tests[k].at_home)
		return;
	if (s_objective_tests[k].lying_since == NONE)
		s_objective_tests[k].lying_since = now;
	else if (now - s_objective_tests[k].lying_since >= TEST_FLAG_RETURN_TICKS)
	{
		forge_set_status("Test flag returned");
		objective_test_object_make(k);
	}
}

static void objective_test_ball_update(int k, long now)
{
	long carrier;

	if (!object_try_and_get_and_verify_type(s_objective_tests[k].object_index, _object_mask_weapon))
	{
		objective_test_object_make(k);
		return;
	}
	carrier = objective_test_carrier(s_objective_tests[k].object_index);
	if (carrier != s_objective_tests[k].carrier)
	{
		if (s_objective_tests[k].carrier != NONE)
			note_queue(_note_ball_dropped, s_objective_tests[k].carrier_team);
		if (carrier != NONE)
			note_queue(_note_ball_grabbed, objective_test_team(carrier));
		s_objective_tests[k].carrier = carrier;
		s_objective_tests[k].carrier_team = objective_test_team(carrier);
		s_objective_tests[k].at_home = 0;
		s_objective_tests[k].lying_since = NONE;
	}
	if (carrier != NONE || s_objective_tests[k].at_home)
		return;
	if (s_objective_tests[k].lying_since == NONE)
		s_objective_tests[k].lying_since = now;
	else if (now - s_objective_tests[k].lying_since >= TEST_BALL_RETURN_TICKS)
		objective_test_object_make(k);
}

/* the hill tested: who stands in it (its markers' middle, out to the
furthest of them) */
static void objective_test_hill_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	float middle[3] = { 0.0f, 0.0f, 0.0f };
	float radius = TEST_HILL_MINIMUM_RADIUS;
	int points = 0, held = 0, contested = 0;
	long team = NONE;
	int32_t i;

	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		struct forge_live_instance const *inst = &g_forge.live_instances[i];

		if (inst->active && forge_objective_test_kind(i) == FORGE_OBJECTIVE_TEST_HILL && inst->team_designator == s_test_hill)
		{
			middle[0] += inst->x;
			middle[1] += inst->y;
			middle[2] += inst->z;
			points++;
		}
	}
	if (!points)
	{
		s_test_hill = NONE;
		return;
	}
	middle[0] /= (float)points;
	middle[1] /= (float)points;
	middle[2] /= (float)points;
	for (i = 0; i < g_forge.live_instance_count; i++)
	{
		struct forge_live_instance const *inst = &g_forge.live_instances[i];
		float dx = inst->x - middle[0], dy = inst->y - middle[1];

		if (inst->active && forge_objective_test_kind(i) == FORGE_OBJECTIVE_TEST_HILL && inst->team_designator == s_test_hill &&
			dx * dx + dy * dy > radius * radius)
		{
			radius = (float)sqrt((double)(dx * dx + dy * dy));
		}
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(player->unit_index,
			_object_mask_unit);
		float dx, dy, dz;
		long its;

		if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		/* (the Monitor stands in nothing) */
		if (forge_is_unit_in_monitor_mode(player->unit_index))
			continue;
		dx = unit->object.position.x - middle[0];
		dy = unit->object.position.y - middle[1];
		dz = unit->object.position.z - middle[2];
		if (dx * dx + dy * dy > radius * radius || dz < -TEST_HILL_HEIGHT || dz > TEST_HILL_HEIGHT)
			continue;
		its = objective_test_team(iterator.datum_index);
		if (!held)
			team = its;
		else if (its != team)
			contested = 1;
		held = 1;
	}
	held = held && !contested;
	if (held == s_test_hill_held && (!held || team == s_test_hill_team))
		return;
	if (s_test_hill_held)
	{
		note_queue(_note_king_lost, s_test_hill_team);
		forge_set_status("Test zone lost");
	}
	if (held)
	{
		note_queue(_note_king_captured, team);
		forge_set_status("Test zone captured");
	}
	s_test_hill_held = held;
	s_test_hill_team = held ? team : NONE;
}

int forge_objective_test_kind(int32_t live_idx)
{
	struct forge_live_instance const *inst;

	if (live_idx < 0 || live_idx >= g_forge.live_instance_count)
		return FORGE_OBJECTIVE_TEST_NONE;
	inst = &g_forge.live_instances[live_idx];
	if (!inst->active || inst->group_tag != 'flag')
		return FORGE_OBJECTIVE_TEST_NONE;
	switch (inst->netgame_flag_type)
	{
	case 0: return FORGE_OBJECTIVE_TEST_FLAG;     /* (_netgame_flag_ctf_flag) */
	case 2: return FORGE_OBJECTIVE_TEST_BALL;     /* (_netgame_flag_oddball_ball_spawn) */
	case 8: return FORGE_OBJECTIVE_TEST_HILL;     /* (_netgame_flag_hill_flag) */
	default: return FORGE_OBJECTIVE_TEST_NONE;
	}
}

int forge_objective_test_on(int32_t live_idx)
{
	int kind = forge_objective_test_kind(live_idx);

	if (kind == FORGE_OBJECTIVE_TEST_HILL)
		return s_test_hill != NONE && g_forge.live_instances[live_idx].team_designator == s_test_hill;
	return kind != FORGE_OBJECTIVE_TEST_NONE && objective_test_find(live_idx) >= 0;
}

void forge_objective_test_toggle(int32_t live_idx)
{
	int kind = forge_objective_test_kind(live_idx);
	int k;

	if (kind == FORGE_OBJECTIVE_TEST_NONE)
		return;
	if (!events_host())
	{
		forge_set_status("Only the Host Tests Objectives");
		return;
	}
	if (kind == FORGE_OBJECTIVE_TEST_HILL)
	{
		long hill = g_forge.live_instances[live_idx].team_designator;

		if (s_test_hill == hill)
		{
			if (s_test_hill_held)
				note_queue(_note_king_lost, s_test_hill_team);
			s_test_hill = NONE;
			forge_set_status("Test zone off");
		}
		else
		{
			/* (another hill's test: the zone moves) */
			if (s_test_hill != NONE)
			{
				if (s_test_hill_held)
					note_queue(_note_king_lost, s_test_hill_team);
				note_queue(_forge_event_note_hill_moved, NONE);
			}
			s_test_hill = hill;
			forge_set_status("Test zone #%ld on: stand in it in Spartan mode", hill);
		}
		s_test_hill_held = 0;
		s_test_hill_team = NONE;
		return;
	}
	k = objective_test_find(live_idx);
	if (k >= 0)
	{
		objective_test_object_delete(k);
		s_objective_tests[k] = s_objective_tests[--s_objective_test_count];
		forge_set_status("Test objective off");
		return;
	}
	if (s_objective_test_count >= MAXIMUM_OBJECTIVE_TESTS)
		return;
	k = s_objective_test_count++;
	memset(&s_objective_tests[k], 0, sizeof(s_objective_tests[k]));
	s_objective_tests[k].live_idx = live_idx;
	s_objective_tests[k].kind = kind;
	s_objective_tests[k].object_index = NONE;
	s_objective_tests[k].carrier = NONE;
	s_objective_tests[k].carrier_team = NONE;
	s_objective_tests[k].at_home = 1;
	s_objective_tests[k].lying_since = NONE;
	forge_set_status("Test %s on: it is out in Spartan mode", kind == FORGE_OBJECTIVE_TEST_FLAG ? "flag" : "ball");
}

void forge_objective_tests_update(void)
{
	long now = game_time_get();
	int k;

	if (!g_forge_session_enabled || !events_host())
		return;
	for (k = 0; k < s_objective_test_count; k++)
	{
		/* (its marker gone or changed: the test with it) */
		if (forge_objective_test_kind(s_objective_tests[k].live_idx) != s_objective_tests[k].kind)
		{
			objective_test_object_delete(k);
			s_objective_tests[k--] = s_objective_tests[--s_objective_test_count];
			continue;
		}
		/* (none in Forge mode: there, what is placed is edited) */
		if (g_forge.mode == _forge_mode_3d_monitor)
		{
			objective_test_object_delete(k);
			continue;
		}
		if (s_objective_tests[k].kind == FORGE_OBJECTIVE_TEST_FLAG)
			objective_test_flag_update(k, now);
		else
			objective_test_ball_update(k, now);
	}
	if (s_test_hill != NONE)
		objective_test_hill_update();
}

void forge_objective_tests_reset(void)
{
	s_objective_test_count = 0;
	s_test_hill = NONE;
	s_test_hill_held = 0;
	s_test_hill_team = NONE;
}
