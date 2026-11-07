/*
FIREFIGHT_NETWORK.C

Host -> Client Firefight state synchronization:
- Synchronizes Set, Round, Wave, Phase, Session Elapsed Time, Next Wave Timer,
  Shared Lives Pool, Active Skulls, Team Score, Enemies Alive, Generator Health/Shield/Lockdown,
  and Per-Player Scores/Kills/Assists.
*/

#include <string.h>

#include "cseries/cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"

#include "halo_firefight.h"
#include "halo_skulls.h"

/* ---------- structures */

struct firefight_net_packet
{
	short current_set;
	short current_round;
	short current_wave;
	short phase_state;
	long phase_timer_ticks;
	long session_elapsed_ticks;
	long next_wave_timer_ticks;
	short shared_lives_remaining;
	short enemies_alive;
	unsigned long active_skulls;
	unsigned long temporary_skulls;
	long team_score;
	real active_multiplier;
	real set_vitality_scale;
	byte in_bonus_round;
	byte match_won;
	byte match_lost;
	byte generator_count;
	short gen_health[FIREFIGHT_MAX_GENERATORS];
	short gen_max_health[FIREFIGHT_MAX_GENERATORS];
	short gen_shield[FIREFIGHT_MAX_GENERATORS];
	short gen_max_shield[FIREFIGHT_MAX_GENERATORS];
	short gen_lockdown_ticks[FIREFIGHT_MAX_GENERATORS];
	byte gen_alive[FIREFIGHT_MAX_GENERATORS];
	short banner_type;
	short banner_data;
	short banner_timer_ticks;
	short dropship_object; /* its absolute index in flight, or NONE */
	byte dropship_scale;
	byte dropship_cloak;
	long player_score[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short player_kills[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	short player_assists[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
};

/* ---------- public code */

long firefight_write_network_state(
	byte *buffer,
	long size)
{
	struct firefight_net_packet pkt;
	short i;

	if (!buffer || size < (long)sizeof(pkt))
		return 0;

	csmemset(&pkt, 0, sizeof(pkt));
	pkt.current_set = ff_state.current_set;
	pkt.current_round = ff_state.current_round;
	pkt.current_wave = ff_state.current_wave;
	pkt.phase_state = ff_state.phase_state;
	pkt.phase_timer_ticks = ff_state.phase_timer_ticks;
	pkt.session_elapsed_ticks = ff_state.session_elapsed_ticks;
	pkt.next_wave_timer_ticks = ff_state.next_wave_timer_ticks;
	pkt.shared_lives_remaining = ff_state.shared_lives_remaining;
	pkt.enemies_alive = ff_state.enemies_alive;
	pkt.active_skulls = ff_state.active_skulls;
	pkt.temporary_skulls = ff_state.temporary_skulls;
	pkt.team_score = ff_state.team_score;
	pkt.active_multiplier = ff_state.active_multiplier;
	pkt.set_vitality_scale = ff_state.set_vitality_scale;
	pkt.in_bonus_round = ff_state.in_bonus_round ? 1 : 0;
	pkt.match_won = ff_state.match_won ? 1 : 0;
	pkt.match_lost = ff_state.match_lost ? 1 : 0;
	pkt.generator_count = (byte)PIN(ff_state.generator_count, 0, FIREFIGHT_MAX_GENERATORS);

	for (i = 0; i < FIREFIGHT_MAX_GENERATORS; i++)
	{
		pkt.gen_health[i] = ff_state.generators[i].health;
		pkt.gen_max_health[i] = ff_state.generators[i].max_health;
		pkt.gen_shield[i] = ff_state.generators[i].shield;
		pkt.gen_max_shield[i] = ff_state.generators[i].max_shield;
		pkt.gen_lockdown_ticks[i] = ff_state.generators[i].lockdown_ticks;
		pkt.gen_alive[i] = ff_state.generators[i].alive ? 1 : 0;
	}

	pkt.banner_type = ff_state.banner_type;
	pkt.banner_data = ff_state.banner_data;
	pkt.banner_timer_ticks = ff_state.banner_timer_ticks;

	pkt.dropship_object = NONE;
	if (ff_state.active_dropship.phase != 0 && ff_state.active_dropship.vehicle_index != NONE)
	{
		pkt.dropship_object = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(ff_state.active_dropship.vehicle_index);
		pkt.dropship_scale = (byte)(PIN(ff_state.active_dropship.scale, 0.0f, 1.0f) * 255.0f);
		pkt.dropship_cloak = (byte)(PIN(ff_state.active_dropship.cloak, 0.0f, 1.0f) * 255.0f);
	}

	for (i = 0; i < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; i++)
	{
		pkt.player_score[i] = ff_state.player_stats[i].score;
		pkt.player_kills[i] = ff_state.player_stats[i].kills;
		pkt.player_assists[i] = ff_state.player_stats[i].assists;
	}

	csmemcpy(buffer, &pkt, sizeof(pkt));
	return (long)sizeof(pkt);
}

boolean firefight_read_network_state(
	byte const *buffer,
	long size,
	boolean first)
{
	struct firefight_net_packet pkt;
	short i;

	(void)first;
	if (!buffer || size < (long)sizeof(pkt))
		return FALSE;

	csmemcpy(&pkt, buffer, sizeof(pkt));

	ff_state.current_set = pkt.current_set;
	ff_state.current_round = pkt.current_round;
	ff_state.current_wave = pkt.current_wave;
	ff_state.phase_state = pkt.phase_state;
	ff_state.phase_timer_ticks = pkt.phase_timer_ticks;
	ff_state.session_elapsed_ticks = pkt.session_elapsed_ticks;
	ff_state.next_wave_timer_ticks = pkt.next_wave_timer_ticks;
	ff_state.shared_lives_remaining = pkt.shared_lives_remaining;
	ff_state.enemies_alive = pkt.enemies_alive;
	ff_state.active_skulls = pkt.active_skulls;
	ff_state.temporary_skulls = pkt.temporary_skulls;
	ff_state.team_score = pkt.team_score;
	ff_state.active_multiplier = pkt.active_multiplier;
	ff_state.set_vitality_scale = pkt.set_vitality_scale;
	ff_state.in_bonus_round = pkt.in_bonus_round != 0;
	ff_state.match_won = pkt.match_won != 0;
	ff_state.match_lost = pkt.match_lost != 0;
	ff_state.generator_count = PIN(pkt.generator_count, 0, FIREFIGHT_MAX_GENERATORS);
	ff_state.generators_alive = 0;

	for (i = 0; i < FIREFIGHT_MAX_GENERATORS; i++)
	{
		ff_state.generators[i].health = pkt.gen_health[i];
		ff_state.generators[i].max_health = pkt.gen_max_health[i];
		ff_state.generators[i].shield = pkt.gen_shield[i];
		ff_state.generators[i].max_shield = pkt.gen_max_shield[i];
		ff_state.generators[i].lockdown_ticks = pkt.gen_lockdown_ticks[i];
		ff_state.generators[i].alive = pkt.gen_alive[i] != 0;
		ff_state.generators[i].active = (i < ff_state.generator_count);
		if (ff_state.generators[i].active && ff_state.generators[i].alive)
			ff_state.generators_alive++;
	}

	if (pkt.banner_timer_ticks > 0 && pkt.banner_type != _ff_banner_none)
	{
		ff_state.banner_type = pkt.banner_type;
		ff_state.banner_data = pkt.banner_data;
		ff_state.banner_timer_ticks = pkt.banner_timer_ticks;
	}

	if (pkt.dropship_object != NONE)
	{
		firefight_dropship_apply_look(
			pkt.dropship_object,
			MAX((real)pkt.dropship_scale / 255.0f, 0.02f),
			(real)pkt.dropship_cloak / 255.0f);
	}
	else
	{
		ff_state.active_dropship.vehicle_index = NONE;
		ff_state.active_dropship.phase = 0;
	}

	for (i = 0; i < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; i++)
	{
		ff_state.player_stats[i].score = pkt.player_score[i];
		ff_state.player_stats[i].kills = pkt.player_kills[i];
		ff_state.player_stats[i].assists = pkt.player_assists[i];
	}

	return TRUE;
}

long game_engine_firefight_write_network_state(
	byte *buffer,
	long size)
{
	return firefight_write_network_state(buffer, size);
}

boolean game_engine_firefight_read_network_state(
	byte const *buffer,
	long size,
	boolean first)
{
	return firefight_read_network_state(buffer, size, first);
}
