/*
SOCIAL_CREATOR.C

What the account made (social_creator.h), counted on the community server
(api-opence's database/halo_social.js, the account's `creator`):

- Time: each second a Forge level is played (a Forge session's level, not
  the main menu) counts as forgeSeconds, and each one the Tag Injector's
  firing range is played as rangeSeconds. Counted from this build on (the
  time before it was never kept).
- Made: a Forge map saved under a name no map had (forgeMaps), a tag profile
  made or copied (tagProfiles), a campaign profile (campaignProfiles) and a
  multiplayer one (multiplayerProfiles). Profiles taken from a game's share
  or imported are not counted.
- The first time: an account the server has no `creator` for (its answer
  says null) has what is already on disk counted, at the main menu (the
  Forge maps in custom_maps, the profiles in tag_injector), and sent as its
  seed: the server takes it only when no other install of the account sent
  one first.

What was made goes as POST /account/creator {add}, a few seconds after it
was made, and the time every SOCIAL_CREATOR_TIME_MILLISECONDS while it is
counted (and as it stops); what was not sent stays until it is, as long as
the game runs. Nothing is sent with social.enabled off, nor kept with
social.record_matches off.
*/
#include "cseries.h"
#include "game/game.h"
#include "interface/ui_widget.h"
#include "custom_edition_maps.h"
#include "firing_range.h"
#include "social.h"
#include "social_creator.h"
#include "../src/community_net.h"
#include "../src/port_config.h"
#include "../src/server_json.h"
#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
int forge_custom_map_is_forge_made(short index);
long port_tag_profiles_scan(void);
long port_tag_profiles_count(void);
unsigned long port_tag_profiles_scans(void);
long port_tag_mp_profiles_scan(void);
long port_tag_mp_profiles_count(void);
unsigned long port_tag_mp_profiles_scans(void);
long port_tag_campaign_profiles_scan(void);
long port_tag_campaign_profiles_count(void);
unsigned long port_tag_campaign_profiles_scans(void);
extern int g_forge_session_enabled; /* port/forge/src/forge_runtime_tags.c */

enum
{
	/* what was made sent this soon after; the time this often while it is
	counted; a failure tried again this long after */
	SOCIAL_CREATOR_MADE_MILLISECONDS = 5000,
	SOCIAL_CREATOR_TIME_MILLISECONDS = 60000,
	SOCIAL_CREATOR_RETRY_MILLISECONDS = 60000,
	/* (a frame longer than this, a stall or the system asleep, counts
	only this much) */
	SOCIAL_CREATOR_FRAME_MOST_MILLISECONDS = 1000,
	SOCIAL_CREATOR_ANSWER_SIZE = 8192,
	SOCIAL_CREATOR_SINCE_SIZE = 40,
};

char const *const social_creator_fields[NUMBER_OF_SOCIAL_CREATOR_FIELDS] =
{
	"forgeSeconds",
	"rangeSeconds",
	"forgeMaps",
	"tagProfiles",
	"campaignProfiles",
	"multiplayerProfiles",
};

static struct
{
	/* the server's word: whether it said (a server before it never does),
	whether it has counted any (null: the seed is due), its counts and since
	when */
	boolean served;
	boolean had;
	long server[NUMBER_OF_SOCIAL_CREATOR_FIELDS];
	char since[SOCIAL_CREATOR_SINCE_SIZE];
	/* what was made since it last heard, and what of it is being sent */
	long pending[NUMBER_OF_SOCIAL_CREATOR_FIELDS];
	long sending[NUMBER_OF_SOCIAL_CREATOR_FIELDS];
	boolean seeding;
	/* the time counted: the milliseconds short of a second, the last frame
	counted and what it counted (FALSE: none) */
	unsigned long counted_at;
	short counting;
	unsigned long milliseconds;
	/* the request asked, and when the next may go */
	int ticket;
	unsigned long made_at;
	unsigned long sent_at;
	unsigned long retry_at;
	char answer[SOCIAL_CREATOR_ANSWER_SIZE];
} social_creator;

static boolean social_creator_recording(
	void)
{
	return social_enabled() && config_boolean("social.record_matches");
}

/* ---------- the server's word */

static void social_creator_read(
	struct server_json_document const *document,
	int creator)
{
	short field;
	int member;
	double value;

	for (field = 0; field < NUMBER_OF_SOCIAL_CREATOR_FIELDS; field++)
	{
		member = server_json_member(document, creator, social_creator_fields[field]);
		social_creator.server[field] = member >= 0 && server_json_get_number(document, member, &value) ? (long)value : 0;
	}
	member = server_json_member(document, creator, "since");
	social_creator.since[0] = 0;
	if (member >= 0)
		server_json_get_string(document, member, social_creator.since, sizeof(social_creator.since));
}

void social_creator_take(
	struct server_json_document const *document,
	int account)
{
	int creator = server_json_member(document, account, "creator");

	if (creator < 0)
		return;
	social_creator.served = TRUE;
	social_creator.had = !server_json_is_null(document, creator);
	if (social_creator.had)
		social_creator_read(document, creator);
}

/* POST /account/creator's answer: its account's creator, taken */
static void social_creator_answer(
	void)
{
	struct server_json_document document;
	int token;

	if (!server_json_parse(&document, social_creator.answer, (int)strlen(social_creator.answer), 16))
		return;
	token = server_json_member(&document, 0, "data");
	token = token >= 0 ? server_json_member(&document, token, "account") : -1;
	if (token >= 0)
		social_creator_take(&document, token);
	server_json_document_free(&document);
}

/* ---------- what is made */

void social_creator_add(
	short field,
	long count)
{
	if (field < 0 || field >= NUMBER_OF_SOCIAL_CREATOR_FIELDS || count <= 0 || !social_creator_recording())
		return;
	social_creator.pending[field] += count;
	if (!social_creator.made_at)
		social_creator.made_at = system_milliseconds() | 1;
	platform_log("social: made %ld %s", count, social_creator_fields[field]);
}

boolean social_creator_totals(
	long totals[NUMBER_OF_SOCIAL_CREATOR_FIELDS])
{
	short field;

	for (field = 0; field < NUMBER_OF_SOCIAL_CREATOR_FIELDS; field++)
		totals[field] = social_creator.server[field] + social_creator.pending[field];
	return social_creator.had;
}

char const *social_creator_since(
	void)
{
	return social_creator.since;
}

/* which time a frame counts: a Forge level's, the firing range's, or none */
static short social_creator_counting(
	void)
{
	if (!game_in_progress() || main_menu_is_active())
		return NONE;
	if (firing_range_active())
		return _social_creator_range_seconds;
	if (g_forge_session_enabled)
		return _social_creator_forge_seconds;
	return NONE;
}

static void social_creator_count_time(
	unsigned long now)
{
	short counting = social_creator_recording() ? social_creator_counting() : NONE;
	unsigned long elapsed;

	if (counting != social_creator.counting)
	{
		/* (stopped, or another: its time so far sent now) */
		if (social_creator.counting != NONE && social_creator.pending[social_creator.counting] > 0)
			social_creator.made_at = now | 1;
		social_creator.counting = counting;
		social_creator.milliseconds = 0;
		social_creator.counted_at = now;
		if (counting != NONE && !social_creator.sent_at)
			social_creator.sent_at = now | 1;
		return;
	}
	if (counting == NONE)
		return;
	elapsed = now - social_creator.counted_at;
	social_creator.counted_at = now;
	social_creator.milliseconds += MIN(elapsed, (unsigned long)SOCIAL_CREATOR_FRAME_MOST_MILLISECONDS);
	if (social_creator.milliseconds >= 1000)
	{
		social_creator.pending[counting] += (long)(social_creator.milliseconds / 1000);
		social_creator.milliseconds %= 1000;
	}
}

/* ---------- the first count: what is on disk */

static long social_creator_forge_maps_on_disk(
	void)
{
	short count = custom_edition_maps_count(FALSE);
	short index;
	long made = 0;

	for (index = 0; index < count; index++)
		made += forge_custom_map_is_forge_made(index) ? 1 : 0;
	return made;
}

static void social_creator_seed(
	long seed[NUMBER_OF_SOCIAL_CREATOR_FIELDS])
{
	csmemset(seed, 0, sizeof(long) * NUMBER_OF_SOCIAL_CREATOR_FIELDS);
	/* (profiles found once, by whoever looks first) */
	if (!port_tag_profiles_scans())
		port_tag_profiles_scan();
	if (!port_tag_mp_profiles_scans())
		port_tag_mp_profiles_scan();
	if (!port_tag_campaign_profiles_scans())
		port_tag_campaign_profiles_scan();
	seed[_social_creator_forge_maps] = social_creator_forge_maps_on_disk();
	seed[_social_creator_tag_profiles] = port_tag_profiles_count();
	seed[_social_creator_campaign_profiles] = port_tag_campaign_profiles_count();
	seed[_social_creator_multiplayer_profiles] = port_tag_mp_profiles_count();
}

/* ---------- the requests */

static void social_creator_counts_json(
	struct server_json *json,
	char const *key,
	long const counts[NUMBER_OF_SOCIAL_CREATOR_FIELDS])
{
	short field;

	server_json_object_begin(json, key);
	for (field = 0; field < NUMBER_OF_SOCIAL_CREATOR_FIELDS; field++)
	{
		if (counts[field] > 0)
			server_json_integer(json, social_creator_fields[field], counts[field]);
	}
	server_json_object_end(json);
}

static boolean social_creator_any(
	long const counts[NUMBER_OF_SOCIAL_CREATOR_FIELDS])
{
	short field;

	for (field = 0; field < NUMBER_OF_SOCIAL_CREATOR_FIELDS; field++)
	{
		if (counts[field] > 0)
			return TRUE;
	}
	return FALSE;
}

static void social_creator_send(
	unsigned long now)
{
	struct server_json json;
	long seed[NUMBER_OF_SOCIAL_CREATOR_FIELDS];
	boolean seeding = social_creator.served && !social_creator.had;
	boolean made_due = social_creator.made_at && (long)(now - social_creator.made_at) >= SOCIAL_CREATOR_MADE_MILLISECONDS;
	boolean time_due = social_creator.sent_at && (long)(now - social_creator.sent_at) >= SOCIAL_CREATOR_TIME_MILLISECONDS;
	short field;

	if (!social_creator.served || (social_creator.retry_at && (long)(now - social_creator.retry_at) < 0))
		return;
	/* (the seed counted at the main menu, where the map list and the
	profiles may be looked at) */
	if (seeding && !main_menu_is_active())
		return;
	if (!seeding && !made_due && !time_due)
		return;
	if (!seeding && !social_creator_any(social_creator.pending))
	{
		social_creator.made_at = 0;
		social_creator.sent_at = social_creator.counting != NONE ? now | 1 : 0;
		return;
	}
	/* (the seed counts what is on disk, what was made since with it) */
	if (seeding)
	{
		social_creator_seed(seed);
		social_creator.pending[_social_creator_forge_maps] = 0;
		social_creator.pending[_social_creator_tag_profiles] = 0;
		social_creator.pending[_social_creator_campaign_profiles] = 0;
		social_creator.pending[_social_creator_multiplayer_profiles] = 0;
	}
	for (field = 0; field < NUMBER_OF_SOCIAL_CREATOR_FIELDS; field++)
		social_creator.sending[field] = social_creator.pending[field];
	server_json_begin(&json);
	server_json_object_begin(&json, NULL);
	if (seeding)
		social_creator_counts_json(&json, "seed", seed);
	social_creator_counts_json(&json, "add", social_creator.sending);
	server_json_object_end(&json);
	social_creator.ticket = social_send("/account/creator", server_json_text(&json));
	server_json_free(&json);
	if (!social_creator.ticket)
	{
		social_creator.retry_at = now + SOCIAL_CREATOR_RETRY_MILLISECONDS;
		return;
	}
	social_creator.seeding = seeding;
	if (seeding)
	{
		platform_log("social: counting what this account made: %ld Forge maps, %ld tag profiles, "
			"%ld campaign and %ld multiplayer", seed[_social_creator_forge_maps], seed[_social_creator_tag_profiles],
			seed[_social_creator_campaign_profiles], seed[_social_creator_multiplayer_profiles]);
	}
	social_creator.made_at = 0;
	social_creator.sent_at = social_creator.counting != NONE ? now | 1 : 0;
}

static void social_creator_poll(
	unsigned long now)
{
	int status = 0;
	int result = community_net_poll(social_creator.ticket, &status, social_creator.answer,
		sizeof(social_creator.answer));
	short field;

	if (result == _community_net_waiting)
		return;
	social_creator.ticket = 0;
	if (result == _community_net_answered && status == 200)
	{
		for (field = 0; field < NUMBER_OF_SOCIAL_CREATOR_FIELDS; field++)
			social_creator.pending[field] -= social_creator.sending[field];
		social_creator.retry_at = 0;
		social_creator_answer();
		/* (a server that took it but answered none: not asked again) */
		social_creator.had = TRUE;
	}
	else
	{
		/* (a seed not taken is counted again, what was made since with it) */
		social_creator.retry_at = now + SOCIAL_CREATOR_RETRY_MILLISECONDS;
		if (social_creator.seeding)
			platform_log("social: what this account made was not counted (%d); asked again later", status);
	}
	csmemset(social_creator.sending, 0, sizeof(social_creator.sending));
	social_creator.seeding = FALSE;
}

void social_creator_update(
	unsigned long now)
{
	static boolean begun = FALSE;

	if (!begun)
	{
		begun = TRUE;
		social_creator.counting = NONE;
	}
	social_creator_count_time(now);
	if (social_creator.ticket)
	{
		social_creator_poll(now);
		return;
	}
	if (social_enabled())
		social_creator_send(now);
}
