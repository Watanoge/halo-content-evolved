/*
SOCIAL_MENU.C

The Social screen's lists and what their rows do (social_menu.h), from the
community server's answers.

- The server is asked one thing at a time for the screen (the friends and
  requests, the activity, a player), and once more for the players muted
  and blocked, which the chat needs whether the screen is open or not
  (social_chat_unheard_set). An action (a friend request, its answer, a
  mute, a block, a report) is one request too; once answered, what it
  changed is asked for again. All of it is taken in social_menu_update, from
  the main loop, so that an answer that comes after the screen closed still
  counts.
- The friends and requests are asked for again every
  SOCIAL_MENU_REFRESH_MILLISECONDS while a list of them is shown, and
  whenever the key of them the server sends with the chat's lines and the
  presence changes (social_menu_friends_key), which changes with what the
  friends are doing too (online, playing, an invite). What changed since the last
  list is said in the chat: a request to you (those waiting too, once), one
  of yours accepted or declined (friends_notice).
- An action that cannot be taken back at once (removing a friend, a block,
  a report) wants A pressed twice.
- A friend's game is joined by its invite (social.instant_join on its side,
  an internet game it hosts): menu_functions.c joins it as Direct Link's
  PASTE LINK does, and shows Direct Link, where it appears.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "rasterizer/rasterizer.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_lookup.h"
#include "interface/interface.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "interface/virtual_keyboard.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"

#include "campaign_medals.h"
#include "campaign_score.h"
#include "social.h"
#include "social_chat.h"
#include "social_creator.h"
#include "social_menu.h"
#include "social_sync.h"
#include "platform_icons.h"
#include "social_extensions.h"
#include "halo_firefight.h"

long halo_screen_ui_offset_current(void);
#include "../src/community_net.h"
#include "../src/p2p.h"
#include "../src/port_config.h"
#include "../src/server_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);

/* ---------- constants */

#define SOCIAL_MENU_CODE_SIZE 16
#define SOCIAL_MENU_NAME_SIZE 48
#define SOCIAL_MENU_LABEL_SIZE 64
#define SOCIAL_MENU_TIME_SIZE 32
#define SOCIAL_MENU_INVITE_SIZE 168
#define SOCIAL_MENU_HOST_KEY_SIZE 40
/* what is kept of the server's lists */
#define SOCIAL_MENU_FRIENDS 100
#define SOCIAL_MENU_REQUESTS 48
#define SOCIAL_MENU_ACTIVITY 30
#define SOCIAL_MENU_MATCHES 20
#define SOCIAL_MENU_MODERATED 64
#define SOCIAL_MENU_RECENT 32
/* the lists opened one from another, at most */
#define SOCIAL_MENU_DEPTH 8
#define SOCIAL_MENU_ROWS 160
/* the friends asked for again while shown; a list not answered asked for
again; the players muted and blocked asked for again after a failure */
#define SOCIAL_MENU_REFRESH_MILLISECONDS 20000
#define SOCIAL_MENU_RETRY_MILLISECONDS 30000
/* how long the last thing done is said, and a second A press waited for */
#define SOCIAL_MENU_MESSAGE_MILLISECONDS 6000
#define SOCIAL_MENU_CONFIRM_MILLISECONDS 4000
/* the keyboard's caption (virtual_keyboard.c's first: a name) and room
(a code is 9 characters; room for spaces around it) */
#define SOCIAL_MENU_KEYBOARD_CAPTION 8
#define SOCIAL_MENU_TYPED_LENGTH 24

/* a sync code (an account's, for another install to take) is asked for again
this often while its screen is shown: the server drops it after 45 seconds
without being asked */
#define SOCIAL_MENU_SYNC_MILLISECONDS 15000
#define SOCIAL_MENU_SYNC_ANSWER_SIZE 1024

/* the lists (social_menu.h) */
enum
{
	_list_home,
	_list_account,
	_list_friendlist,
	_list_activity_menu,
	_list_identity,
	_list_friends,
	_list_requests,
	_list_activity,
	_list_recent,
	_list_card,
	_list_player,
	_list_matches,
	_list_moderation,
	_list_report,
	_list_sync,
	_list_sync_code,
	_list_setup,
	NUMBER_OF_LISTS
};

/* a row's kind; its value is an index into its list's data */
enum
{
	_row_note,
	_row_my_account,
	_row_friendlist_menu,
	_row_activity_menu,
	_row_settings_menu,
	_row_you,
	_row_name,
	_row_name_color,
	_row_friends,
	_row_requests,
	_row_activity,
	_row_recent,
	_row_add_friend,
	_row_moderation,
	_row_friend,
	/* (under a friend whose game may be joined: it joined) */
	_row_friend_join,
	_row_incoming,
	_row_outgoing,
	_row_activity_entry,
	_row_recent_player,
	_row_matches,
	_row_join,
	_row_message,
	_row_friend_add,
	_row_friend_accept,
	_row_friend_decline,
	_row_friend_cancel,
	_row_friend_remove,
	_row_mute,
	_row_unmute,
	_row_block,
	_row_unblock,
	_row_report,
	_row_match,
	_row_moderated,
	_row_reason,
	_row_code,
	_row_sync,
	_row_sync_create,
	_row_sync_input,
	_row_sync_code,
	_row_settings,
	_row_setup_name,
	_row_setup_color,
	_row_setup_done,
	_row_clear_rank,
};

/* a note row's words */
enum
{
	_note_loading,
	_note_offline,
	_note_failed,
	_note_none,
};

/* what the screen asked the server for */
enum
{
	_fetch_none,
	_fetch_friends,
	_fetch_activity,
	_fetch_player,
	_fetch_friend_prefetch,
};

/* the keyboard: what for */
enum
{
	_typing_none,
	_typing_friend_code,
	_typing_sync_code,
	_typing_name,
	_typing_setup_name,
};

/* a new install waits this long for the server's word before asking the
player (who has no profile to play with): offline, it is asked all the same */
#define SOCIAL_SETUP_WAIT_MILLISECONDS 5000
/* (the screen asked for again this long after, if it did not open) */
#define SOCIAL_SETUP_ASK_MILLISECONDS 2000

/* the colours a chat name may be: its label and RRGGBB (the first, none:
the line's own colour) */
static struct
{
	char const *label;
	char const *hex;
} const social_menu_name_colors[] =
{
	{ "DEFAULT", "" },
	{ "RED", "ff5a4f" },
	{ "ORANGE", "ff9d3d" },
	{ "YELLOW", "ffe14d" },
	{ "GREEN", "6fe06f" },
	{ "CYAN", "4fd8e8" },
	{ "BLUE", "5b8cff" },
	{ "PURPLE", "b07bff" },
	{ "PINK", "ff7bc8" },
};

/* ---------- structures */

/* what a player is doing, as the server lets the one asking see it */
struct social_menu_presence
{
	boolean online;
	boolean host;
	char state[16];
	char platform[16];
	char map[SOCIAL_MENU_LABEL_SIZE];
	char gametype[SOCIAL_MENU_LABEL_SIZE];
	char game[SOCIAL_MENU_LABEL_SIZE];
	long players;
	long maximum;
	char invite[SOCIAL_MENU_INVITE_SIZE];
	/* a public game with a password: its listing's key (its host's key
	hash, hex), which the server browser finds it by */
	char host_key[SOCIAL_MENU_HOST_KEY_SIZE];
	boolean locked;
	/* a private game (no invite for friends): said so */
	boolean private_game;
	char last_seen[SOCIAL_MENU_TIME_SIZE];
};

struct social_menu_person
{
	char code[SOCIAL_MENU_CODE_SIZE];
	char name[SOCIAL_MENU_NAME_SIZE];
	char at[SOCIAL_MENU_TIME_SIZE];
	struct social_menu_presence presence;
	/* (the moderation list's) */
	boolean muted;
	boolean blocked;
};

/* a match or a level, as the server keeps it (the activity's with its
player's code and name) */
struct social_menu_match
{
	char code[SOCIAL_MENU_CODE_SIZE];
	char player[SOCIAL_MENU_NAME_SIZE];
	char kind[16];
	char result[8];
	/* (a match left before its end: why, quit, ended or disconnected) */
	char end_reason[16];
	char map[SOCIAL_MENU_LABEL_SIZE];
	char gametype[SOCIAL_MENU_LABEL_SIZE];
	char network[8];
	char played_at[SOCIAL_MENU_TIME_SIZE];
	long score;
	long kills;
	long deaths;
	long assists;
	long suicides;
	long betrayals;
	long seconds;
	long players;
	long place;
	long difficulty;
	long medals;
	boolean teams;
};

struct social_menu_totals
{
	long matches;
	long wins;
	long losses;
	long ties;
	long quits;
	long score;
	long kills;
	long deaths;
	long assists;
	long suicides;
	long betrayals;
	long seconds;
	long medals;
};

/* Firefight's totals: one's own from the career, another's from the server */
struct social_menu_firefight
{
	long matches;
	long sets;
	long waves;
	long kills;
	long boss_kills;
	long deaths;
	long assists;
	long best_spree;
	long score;
	long seconds;
};

struct social_menu_player
{
	char code[SOCIAL_MENU_CODE_SIZE];
	char name[SOCIAL_MENU_NAME_SIZE];
	/* its records had (else asked for, or not answered) */
	boolean had;
	char created_at[SOCIAL_MENU_TIME_SIZE];
	struct social_menu_totals multiplayer;
	struct social_menu_totals campaign;
	struct social_menu_firefight firefight;
	/* what they made (social_creator.h's fields): had (else never counted,
	or a server before it), and since when it is counted */
	boolean creator_had;
	long creator[NUMBER_OF_SOCIAL_CREATOR_FIELDS];
	char creator_since[SOCIAL_MENU_TIME_SIZE];
	long medal_counts[NUMBER_OF_CAMPAIGN_MEDALS];
	struct social_menu_presence presence;
	/* self, friends, incoming, outgoing, none */
	char relation[12];
	boolean muted;
	boolean blocked;
	/* it takes friend requests */
	boolean requests;
	struct social_menu_match matches[SOCIAL_MENU_MATCHES];
	short match_count;
};

struct social_menu_row
{
	short kind;
	short value;
};

/* ---------- globals */

static struct
{
	boolean open;
	short opening;
	/* (_social_menu_open_player's player: its code and name) */
	char opening_code[SOCIAL_MENU_CODE_SIZE];
	char opening_name[SOCIAL_MENU_NAME_SIZE];
	/* the list shown, its first row shown and the one chosen, and those it
	was opened from */
	short list;
	short list_serial;
	short first;
	short chosen;
	short depth;
	struct
	{
		short list;
		short first;
		short chosen;
	} stack[SOCIAL_MENU_DEPTH];
	struct social_menu_row rows[SOCIAL_MENU_ROWS];
	short count;
	/* the last thing done, and a second A press waited for */
	char message[160];
	unsigned long message_time;
	short confirming_kind;
	unsigned long confirming_time;
	/* the sync code: its request, the code the server gave, when it was last
	asked for, and why it was not given */
	int sync_ticket;
	char sync_code[16];
	unsigned long sync_at;
	int sync_status;
	boolean sync_joined;
	char sync_answer[SOCIAL_MENU_SYNC_ANSWER_SIZE];
	/* the keyboard */
	short typing;
	wchar_t typed[SOCIAL_MENU_TYPED_LENGTH + 1];
	/* the screen's request, what it is, and the action's */
	int fetch_ticket;
	short fetching;
	int action_ticket;
	short action_kind;
	/* the action was the chat's (its player's names): what it did said there */
	boolean action_from_chat;
	char action_name[SOCIAL_MENU_NAME_SIZE];
	/* the friends and requests: had, when, and whether it failed */
	boolean friends_had;
	boolean friends_failed;
	boolean friends_enabled;
	unsigned long friends_at;
	struct social_menu_person friends[SOCIAL_MENU_FRIENDS];
	short friend_count;
	struct social_menu_person incoming[SOCIAL_MENU_REQUESTS];
	short incoming_count;
	struct social_menu_person outgoing[SOCIAL_MENU_REQUESTS];
	short outgoing_count;
	/* the friendships' key the server said last, and whether the lists are
	to be asked for again for it; the account the lists had are of (what
	changed is said against them), and the requests waiting said once */
	char friends_key[SOCIAL_MENU_CODE_SIZE * 2];
	boolean friends_due;
	char friends_account[SOCIAL_MENU_CODE_SIZE];
	boolean incoming_said;
	/* your code shown (with FRIEND INVITES at CODE ONLY it is hidden until
	asked for: own_code) */
	boolean code_shown;
	/* the action's player's code */
	char action_code[SOCIAL_MENU_CODE_SIZE];
	/* the friends' activity */
	boolean activity_had;
	boolean activity_failed;
	struct social_menu_match activity[SOCIAL_MENU_ACTIVITY];
	short activity_count;
	/* the player shown, and whether asking for it failed */
	struct social_menu_player player;
	boolean player_failed;
	boolean player_wanted;
	char prefetch_code[SOCIAL_MENU_CODE_SIZE];
	char prefetch_name[SOCIAL_MENU_NAME_SIZE];
	/* the chat's players, as the screen opened */
	char recent_codes[SOCIAL_MENU_RECENT][16];
	wchar_t recent_names[SOCIAL_MENU_RECENT][32];
	short recent_count;
	/* the players muted and blocked: had, asked for, and when again */
	boolean moderation_had;
	boolean moderation_due;
	boolean moderation_failed;
	int moderation_ticket;
	unsigned long moderation_at;
	struct social_menu_person moderated[SOCIAL_MENU_MODERATED];
	short moderated_count;
	/* the invite of the game joined */
	char join_invite[SOCIAL_MENU_INVITE_SIZE];
	/* ... or, a password's game, its listing's key */
	char join_host_key[SOCIAL_MENU_HOST_KEY_SIZE];
	/* (the friend whose game it is) */
	char join_name[SOCIAL_MENU_NAME_SIZE];
	/* the card shows what the player made (CREATOR PROFILE), not their
	service record; where its button was last drawn, and when */
	boolean card_creator;
	/* (its button has the focus: Up from the button bar, Down back) */
	boolean card_button_focused;
	rectangle2d card_button;
	unsigned long card_button_drawn_at;
	char answer[COMMUNITY_NET_ANSWER_MAXIMUM];
} social_menu;

static char const *const social_menu_reasons[] = {
	"SPAM", "ABUSIVE LANGUAGE", "CHEATING", "OFFENSIVE NAME", "OTHER" };
static char const *const social_menu_reason_keys[] = { "spam", "abuse", "cheating", "name", "other" };
static char const *const social_menu_difficulties[] = { "EASY", "NORMAL", "HEROIC", "LEGENDARY" };

/* ---------- text */

static void message_set(
	char const *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(social_menu.message, sizeof(social_menu.message), format, arguments);
	va_end(arguments);
	social_menu.message_time = system_milliseconds();
}

/* text appended to a buffer of size (cut short where it is full) */
static void text_add(
	char *text,
	long size,
	char const *format,
	...)
{
	long length = (long)strlen(text);
	va_list arguments;

	if (length >= size - 1)
		return;
	va_start(arguments, format);
	vsnprintf(text + length, (size_t)(size - length), format, arguments);
	va_end(arguments);
}

/* a name as the game's wide text (the basic plane of UTF-8; '?' past it) */
static void wide_from_utf8(
	char const *text,
	wchar_t *wide,
	long count)
{
	long length = 0;
	unsigned char const *cursor = (unsigned char const *)text;

	while (*cursor && length < count - 1)
	{
		unsigned long character = *cursor++;

		if (character >= 0xE0 && character < 0xF0 && (cursor[0] & 0xC0) == 0x80 && (cursor[1] & 0xC0) == 0x80)
		{
			character = ((character & 0x0F) << 12) | ((cursor[0] & 0x3F) << 6) | (cursor[1] & 0x3F);
			cursor += 2;
		}
		else if (character >= 0xC0 && character < 0xE0 && (cursor[0] & 0xC0) == 0x80)
		{
			character = ((character & 0x1F) << 6) | (cursor[0] & 0x3F);
			cursor++;
		}
		else if (character >= 0x80)
		{
			while ((*cursor & 0xC0) == 0x80)
				cursor++;
			character = '?';
		}
		wide[length++] = (wchar_t)character;
	}
	wide[length] = 0;
}

/* the game's wide text (a name of the chat's, a code typed) as UTF-8 */
static void utf8_from_wide(
	wchar_t const *wide,
	char *text,
	long size)
{
	long length = 0;
	long index;

	for (index = 0; wide[index] && length < size - 1; index++)
	{
		unsigned long character = (unsigned short)wide[index];

		if (character < 0x80)
		{
			text[length++] = (char)character;
		}
		else if (character < 0x800 && length + 2 < size)
		{
			text[length++] = (char)(0xC0 | (character >> 6));
			text[length++] = (char)(0x80 | (character & 0x3F));
		}
		else if (length + 3 < size)
		{
			text[length++] = (char)(0xE0 | (character >> 12));
			text[length++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[length++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			break;
		}
	}
	text[length] = 0;
}

static void upper_text(
	char *text)
{
	for (; *text; text++)
	{
		if (*text >= 'a' && *text <= 'z')
			*text = (char)(*text - 'a' + 'A');
	}
}

/* a code as typed: its letters and digits in capitals, the dash put back
after the fourth (the on-screen keyboard may have no "-": "abcd1234" is
"ABCD-1234"); else as typed, for the server to refuse */
static void code_normalize(
	char *code)
{
	char kept[16];
	long length = 0;
	char const *cursor;

	for (cursor = code; *cursor && length < (long)sizeof(kept) - 1; cursor++)
	{
		char character = *cursor;

		if (character >= 'a' && character <= 'z')
			character = (char)(character - 'a' + 'A');
		if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9'))
			kept[length++] = character;
	}
	kept[length] = 0;
	if (length == 8)
		snprintf(code, 10, "%.4s-%.4s", kept, kept + 4);
	else
		upper_text(code);
}

/* a day of the calendar as days since 1970-01-01 (Howard Hinnant's) */
static long days_from_civil(
	long year,
	long month,
	long day)
{
	long era;
	long year_of_era;
	long day_of_year;
	long day_of_era;

	year -= month <= 2;
	era = (year >= 0 ? year : year - 399) / 400;
	year_of_era = year - era * 400;
	day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
	return era * 146097 + day_of_era - 719468;
}

/* how long ago a time the server said was ("2026-11-01T12:00:00.000Z"):
"JUST NOW", "5M AGO", "3H AGO", "2D AGO", else its day; "" for none */
static void ago_text(
	char const *iso,
	char *text,
	long size)
{
	int year;
	int month;
	int day;
	int hour = 0;
	int minute = 0;
	int second = 0;
	long seconds;
	long now = (long)time(NULL);

	text[0] = 0;
	if (!iso[0] || sscanf(iso, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 3)
		return;
	seconds = now - (days_from_civil(year, month, day) * 86400L + hour * 3600L + minute * 60L + second);
	if (seconds < 60)
		snprintf(text, (size_t)size, "JUST NOW");
	else if (seconds < 3600)
		snprintf(text, (size_t)size, "%ldM AGO", seconds / 60);
	else if (seconds < 86400)
		snprintf(text, (size_t)size, "%ldH AGO", seconds / 3600);
	else if (seconds < 30 * 86400L)
		snprintf(text, (size_t)size, "%ldD AGO", seconds / 86400);
	else
		snprintf(text, (size_t)size, "%04d-%02d-%02d", year, month, day);
}

/* a time played, "1H 05M" or "12M 30S" */
static void duration_text(
	long seconds,
	char *text,
	long size)
{
	if (seconds >= 3600)
		snprintf(text, (size_t)size, "%ldH %02ldM", seconds / 3600, (seconds / 60) % 60);
	else
		snprintf(text, (size_t)size, "%ldM %02ldS", seconds / 60, seconds % 60);
}

/* kills and assists for each death, as "1.75" */
static void kda_text(
	long kills,
	long deaths,
	long assists,
	char *text,
	long size)
{
	long hundredths = (kills + assists) * 100 / MAX(deaths, 1);

	snprintf(text, (size_t)size, "%ld.%02ld", hundredths / 100, hundredths % 100);
}

/* ---------- the server's answers */

static void json_text(
	struct server_json_document const *document,
	int token,
	char const *key,
	char *text,
	int size)
{
	int member = token >= 0 ? server_json_member(document, token, key) : -1;

	text[0] = 0;
	if (member < 0 || !server_json_get_string(document, member, text, size))
		text[0] = 0;
}

static long json_number(
	struct server_json_document const *document,
	int token,
	char const *key,
	long otherwise)
{
	int member = token >= 0 ? server_json_member(document, token, key) : -1;
	double value;

	return member >= 0 && server_json_get_number(document, member, &value) ? (long)value : otherwise;
}

static boolean json_boolean(
	struct server_json_document const *document,
	int token,
	char const *key)
{
	int member = token >= 0 ? server_json_member(document, token, key) : -1;
	int value = 0;

	return member >= 0 && server_json_get_boolean(document, member, &value) && value;
}

/* the answer parsed, its data's token (-1: not one the server sent whole) */
static int answer_data(
	struct server_json_document *document)
{
	if (!server_json_parse(document, social_menu.answer, (int)strlen(social_menu.answer), 16))
		return -2;
	return server_json_member(document, 0, "data");
}

static void presence_read(
	struct server_json_document const *document,
	int token,
	struct social_menu_presence *presence)
{
	csmemset(presence, 0, sizeof(*presence));
	presence->online = json_boolean(document, token, "online");
	presence->host = json_boolean(document, token, "host");
	json_text(document, token, "state", presence->state, sizeof(presence->state));
	json_text(document, token, "platform", presence->platform, sizeof(presence->platform));
	json_text(document, token, "map", presence->map, sizeof(presence->map));
	json_text(document, token, "gametype", presence->gametype, sizeof(presence->gametype));
	json_text(document, token, "game", presence->game, sizeof(presence->game));
	json_text(document, token, "invite", presence->invite, sizeof(presence->invite));
	json_text(document, token, "hostKey", presence->host_key, sizeof(presence->host_key));
	presence->locked = json_boolean(document, token, "locked");
	presence->private_game = json_boolean(document, token, "private");
	json_text(document, token, "lastSeenAt", presence->last_seen, sizeof(presence->last_seen));
	presence->players = json_number(document, token, "players", 0);
	presence->maximum = json_number(document, token, "maximum", 0);
	/* (an older game's main menu, its scenario ui, said as a campaign) */
	if (!strcmp(presence->state, "campaign") && !strcmp(presence->map, "ui"))
	{
		snprintf(presence->state, sizeof(presence->state), "menus");
		presence->map[0] = 0;
	}
}

static short people_read(
	struct server_json_document const *document,
	int array,
	struct social_menu_person *people,
	short maximum)
{
	short count = 0;
	int index;

	for (index = 0; array >= 0 && count < maximum; index++)
	{
		int element = server_json_element(document, array, index);
		struct social_menu_person *person = &people[count];

		if (element < 0)
			break;
		csmemset(person, 0, sizeof(*person));
		json_text(document, element, "code", person->code, sizeof(person->code));
		json_text(document, element, "name", person->name, sizeof(person->name));
		json_text(document, element, "at", person->at, sizeof(person->at));
		if (!person->at[0])
			json_text(document, element, "since", person->at, sizeof(person->at));
		presence_read(document, element, &person->presence);
		if (person->code[0])
		{
			social_person_note(document, element, person->code, person->name);
			count++;
		}
	}
	return count;
}

static void match_read(
	struct server_json_document const *document,
	int token,
	struct social_menu_match *match)
{
	int medals = server_json_member(document, token, "medals");
	int index;

	csmemset(match, 0, sizeof(*match));
	json_text(document, token, "code", match->code, sizeof(match->code));
	json_text(document, token, "player", match->player, sizeof(match->player));
	json_text(document, token, "kind", match->kind, sizeof(match->kind));
	json_text(document, token, "result", match->result, sizeof(match->result));
	json_text(document, token, "endReason", match->end_reason, sizeof(match->end_reason));
	json_text(document, token, "map", match->map, sizeof(match->map));
	json_text(document, token, "gametype", match->gametype, sizeof(match->gametype));
	json_text(document, token, "network", match->network, sizeof(match->network));
	json_text(document, token, "playedAt", match->played_at, sizeof(match->played_at));
	match->score = json_number(document, token, "score", 0);
	match->kills = json_number(document, token, "kills", 0);
	match->deaths = json_number(document, token, "deaths", 0);
	match->assists = json_number(document, token, "assists", 0);
	match->suicides = json_number(document, token, "suicides", 0);
	match->betrayals = json_number(document, token, "betrayals", 0);
	match->seconds = json_number(document, token, "seconds", 0);
	match->players = json_number(document, token, "players", 0);
	match->place = json_number(document, token, "place", 0);
	match->difficulty = json_number(document, token, "difficulty", 0);
	match->teams = json_boolean(document, token, "teams");
	/* (the medals won in it, all told) */
	for (index = 0; medals >= 0 && index < NUMBER_OF_CAMPAIGN_MEDALS; index++)
		match->medals += json_number(document, medals, campaign_medal_descriptions[index].key, 0);
}

static void totals_read(
	struct server_json_document const *document,
	int token,
	struct social_menu_totals *totals)
{
	totals->matches = json_number(document, token, "matches", 0);
	totals->wins = json_number(document, token, "wins", 0);
	totals->losses = json_number(document, token, "losses", 0);
	totals->ties = json_number(document, token, "ties", 0);
	totals->quits = json_number(document, token, "quits", 0);
	totals->score = json_number(document, token, "score", 0);
	totals->kills = json_number(document, token, "kills", 0);
	totals->deaths = json_number(document, token, "deaths", 0);
	totals->assists = json_number(document, token, "assists", 0);
	totals->suicides = json_number(document, token, "suicides", 0);
	totals->betrayals = json_number(document, token, "betrayals", 0);
	totals->seconds = json_number(document, token, "seconds", 0);
	totals->medals = json_number(document, token, "medals", 0);
}

/* your code as shown: hidden with FRIEND INVITES at CODE ONLY, until YOUR
CODE's A shows it */
static boolean own_code_hidden(
	void)
{
	return !config_boolean("social.friend_requests") && !social_menu.code_shown;
}

static char const *own_code(
	void)
{
	return !social_account_code()[0] ? "..." : own_code_hidden() ? "HIDDEN" : social_account_code();
}

static boolean people_have(
	struct social_menu_person const *people,
	short count,
	char const *code)
{
	short index;

	for (index = 0; index < count; index++)
	{
		if (!strcmp(people[index].code, code))
			return TRUE;
	}
	return FALSE;
}

/* the lists had again: said in the chat, against those had before (were),
a request to you new (each waiting, the first time), one of yours gone:
accepted (a friend now) or declined */
static void friends_notice(
	boolean compare,
	struct social_menu_person const *were_incoming,
	short were_incoming_count,
	struct social_menu_person const *were_outgoing,
	short were_outgoing_count)
{
	char text[SOCIAL_MENU_NAME_SIZE + 96];
	short index;

	if (!config_boolean("social.friends"))
		return;
	for (index = 0; index < social_menu.incoming_count; index++)
	{
		struct social_menu_person const *person = &social_menu.incoming[index];

		if (compare ? people_have(were_incoming, were_incoming_count, person->code) : social_menu.incoming_said)
			continue;
		snprintf(text, sizeof(text), "%s sent you a friend request.", person->name);
		social_chat_notice_text(text);
	}
	social_menu.incoming_said = TRUE;
	if (!compare)
		return;
	for (index = 0; index < were_outgoing_count; index++)
	{
		struct social_menu_person const *person = &were_outgoing[index];

		if (people_have(social_menu.outgoing, social_menu.outgoing_count, person->code))
			continue;
		if (people_have(social_menu.friends, social_menu.friend_count, person->code))
			snprintf(text, sizeof(text), "%s accepted your friend request.", person->name);
		else
			snprintf(text, sizeof(text), "%s declined your friend request.", person->name);
		social_chat_notice_text(text);
	}
}

void social_menu_friends_key(
	char const *key)
{
	if (!key[0] || !strcmp(key, social_menu.friends_key))
		return;
	/* (the first key said: the lists had already are of it) */
	if (social_menu.friends_key[0] || !social_menu.friends_had)
		social_menu.friends_due = TRUE;
	snprintf(social_menu.friends_key, sizeof(social_menu.friends_key), "%s", key);
}

/* the player shown, a friend: what they are doing as the friends say it
(newer than their records', asked for once as their card opened) */
static void friends_presence_to_player(
	void)
{
	short index;

	if (!social_menu.player.had || strcmp(social_menu.player.relation, "friends"))
		return;
	for (index = 0; index < social_menu.friend_count; index++)
	{
		if (!strcmp(social_menu.friends[index].code, social_menu.player.code))
		{
			social_menu.player.presence = social_menu.friends[index].presence;
			break;
		}
	}
}

static void friends_take(
	boolean answered)
{
	static struct social_menu_person were_incoming[SOCIAL_MENU_REQUESTS];
	static struct social_menu_person were_outgoing[SOCIAL_MENU_REQUESTS];
	struct server_json_document document;
	short were_incoming_count = social_menu.incoming_count;
	short were_outgoing_count = social_menu.outgoing_count;
	boolean compare = social_menu.friends_had && !strcmp(social_menu.friends_account, social_account_code());
	int data;

	social_menu.friends_at = system_milliseconds();
	social_menu.friends_failed = !answered;
	if (!answered)
		return;
	data = answer_data(&document);
	if (data == -2)
	{
		social_menu.friends_failed = TRUE;
		return;
	}
	if (data >= 0)
	{
		csmemcpy(were_incoming, social_menu.incoming, sizeof(were_incoming));
		csmemcpy(were_outgoing, social_menu.outgoing, sizeof(were_outgoing));
		social_menu.friends_enabled = json_boolean(&document, data, "enabled");
		social_menu.friend_count = people_read(&document, server_json_member(&document, data, "friends"),
			social_menu.friends, SOCIAL_MENU_FRIENDS);
		social_menu.incoming_count = people_read(&document, server_json_member(&document, data, "incoming"),
			social_menu.incoming, SOCIAL_MENU_REQUESTS);
		social_menu.outgoing_count = people_read(&document, server_json_member(&document, data, "outgoing"),
			social_menu.outgoing, SOCIAL_MENU_REQUESTS);
		social_menu.friends_had = TRUE;
		friends_presence_to_player();
		friends_notice(compare, were_incoming, were_incoming_count, were_outgoing, were_outgoing_count);
		snprintf(social_menu.friends_account, sizeof(social_menu.friends_account), "%s", social_account_code());
	}
	else
	{
		social_menu.friends_failed = TRUE;
	}
	server_json_document_free(&document);
}

static void activity_take(
	boolean answered)
{
	struct server_json_document document;
	int data;
	int array;
	int index;

	social_menu.activity_failed = !answered;
	if (!answered)
		return;
	data = answer_data(&document);
	if (data == -2)
	{
		social_menu.activity_failed = TRUE;
		return;
	}
	array = data >= 0 ? server_json_member(&document, data, "activity") : -1;
	social_menu.activity_count = 0;
	for (index = 0; array >= 0 && social_menu.activity_count < SOCIAL_MENU_ACTIVITY; index++)
	{
		int element = server_json_element(&document, array, index);

		if (element < 0)
			break;
		match_read(&document, element, &social_menu.activity[social_menu.activity_count++]);
	}
	social_menu.activity_had = data >= 0;
	social_menu.activity_failed = data < 0;
	server_json_document_free(&document);
}

static void firefight_read(
	struct server_json_document const *document,
	int token,
	struct social_menu_firefight *totals)
{
	totals->matches = json_number(document, token, "matches", 0);
	totals->sets = json_number(document, token, "sets", 0);
	totals->waves = json_number(document, token, "waves", 0);
	totals->kills = json_number(document, token, "kills", 0);
	totals->boss_kills = json_number(document, token, "bossKills", 0);
	totals->deaths = json_number(document, token, "deaths", 0);
	totals->assists = json_number(document, token, "assists", 0);
	totals->best_spree = json_number(document, token, "bestSpree", 0);
	totals->score = json_number(document, token, "score", 0);
	totals->seconds = json_number(document, token, "seconds", 0);
}

static void prefetch_take(
	boolean answered,
	int status)
{
	struct server_json_document document;
	int data;
	int token;

	if (!answered || status != 200)
	{
		if (social_menu.prefetch_code[0])
			social_extensions_cache_person_stats(social_menu.prefetch_code, social_menu.prefetch_name, -1L, NULL);
		return;
	}
	data = answer_data(&document);
	if (data == -2)
		return;
	token = data >= 0 ? server_json_member(&document, data, "player") : -1;
	if (token >= 0)
	{
		char code[SOCIAL_MENU_CODE_SIZE];
		char name[SOCIAL_MENU_NAME_SIZE];

		json_text(&document, token, "code", code, sizeof(code));
		json_text(&document, token, "name", name, sizeof(name));
		if (!name[0])
			snprintf(name, sizeof(name), "%s", social_menu.prefetch_name[0] ? social_menu.prefetch_name : code);
		if (code[0])
			social_person_note(&document, token, code, name);
	}
	server_json_document_free(&document);
}

static void player_take(
	boolean answered,
	int status)
{
	struct social_menu_player *player = &social_menu.player;
	struct server_json_document document;
	int data;
	int token;
	int medals;
	int matches;
	short index;

	social_menu.player_failed = !answered || status != 200;
	if (social_menu.player_failed)
	{
		if (answered && status == 404 && !strstr(social_menu.answer, "\"not_found\""))
			message_set("No player has the code %s.", player->code);
		else if (answered && status == 404)
			message_set("The community server has no player pages yet:\nit needs updating.");
		return;
	}
	data = answer_data(&document);
	if (data == -2)
	{
		social_menu.player_failed = TRUE;
		return;
	}
	token = data >= 0 ? server_json_member(&document, data, "player") : -1;
	if (token >= 0)
	{
		char code[SOCIAL_MENU_CODE_SIZE];
		int stats = server_json_member(&document, token, "stats");

		/* (the answer of a player asked for before another was) */
		json_text(&document, token, "code", code, sizeof(code));
		if (!strcmp(code, player->code))
		{
			json_text(&document, token, "name", player->name, sizeof(player->name));
			if (!player->name[0])
				snprintf(player->name, sizeof(player->name), "%s", player->code);
			json_text(&document, token, "createdAt", player->created_at, sizeof(player->created_at));
			json_text(&document, token, "relation", player->relation, sizeof(player->relation));
			player->muted = json_boolean(&document, token, "muted");
			player->blocked = json_boolean(&document, token, "blocked");
			player->requests = json_boolean(&document, token, "requests");
			presence_read(&document, token, &player->presence);
			totals_read(&document, stats >= 0 ? server_json_member(&document, stats, "multiplayer") : -1,
				&player->multiplayer);
			totals_read(&document, stats >= 0 ? server_json_member(&document, stats, "campaign") : -1,
				&player->campaign);
			firefight_read(&document, stats >= 0 ? server_json_member(&document, stats, "firefight") : -1,
				&player->firefight);
			{
				int creator = server_json_member(&document, token, "creator");
				short field;

				player->creator_had = creator >= 0 && !server_json_is_null(&document, creator);
				for (field = 0; field < NUMBER_OF_SOCIAL_CREATOR_FIELDS; field++)
				{
					player->creator[field] = player->creator_had ?
						json_number(&document, creator, social_creator_fields[field], 0) : 0;
				}
				player->creator_since[0] = 0;
				if (player->creator_had)
					json_text(&document, creator, "since", player->creator_since, sizeof(player->creator_since));
			}
			medals = server_json_member(&document, token, "medals");
			for (index = 0; index < NUMBER_OF_CAMPAIGN_MEDALS; index++)
				player->medal_counts[index] = json_number(&document, medals, campaign_medal_descriptions[index].key, 0);
			matches = server_json_member(&document, data, "matches");
			player->match_count = 0;
			for (index = 0; matches >= 0 && player->match_count < SOCIAL_MENU_MATCHES; index++)
			{
				int element = server_json_element(&document, matches, index);

				if (element < 0)
					break;
				match_read(&document, element, &player->matches[player->match_count++]);
			}
			player->had = TRUE;
			social_person_note(&document, token, player->code, player->name);
		}
	}
	else
	{
		social_menu.player_failed = TRUE;
	}
	server_json_document_free(&document);
}

/* the players muted and blocked, one row each (both, once), and the chat's
names of them */
static void moderation_take(
	void)
{
	struct server_json_document document;
	struct social_menu_person blocked[SOCIAL_MENU_MODERATED];
	wchar_t names[SOCIAL_MENU_MODERATED][32];
	int data = answer_data(&document);
	short blocked_count;
	short index;

	if (data == -2)
		return;
	if (data >= 0)
	{
		social_menu.moderated_count = people_read(&document, server_json_member(&document, data, "muted"),
			social_menu.moderated, SOCIAL_MENU_MODERATED);
		for (index = 0; index < social_menu.moderated_count; index++)
			social_menu.moderated[index].muted = TRUE;
		blocked_count = people_read(&document, server_json_member(&document, data, "blocked"), blocked,
			SOCIAL_MENU_MODERATED);
		for (index = 0; index < blocked_count; index++)
		{
			short other;

			for (other = 0; other < social_menu.moderated_count; other++)
			{
				if (!strcmp(social_menu.moderated[other].code, blocked[index].code))
					break;
			}
			if (other == social_menu.moderated_count)
			{
				if (social_menu.moderated_count >= SOCIAL_MENU_MODERATED)
					continue;
				social_menu.moderated[social_menu.moderated_count++] = blocked[index];
			}
			social_menu.moderated[other].blocked = TRUE;
		}
		social_menu.moderation_had = TRUE;
		for (index = 0; index < social_menu.moderated_count; index++)
			wide_from_utf8(social_menu.moderated[index].name, names[index], NUMBEROF(names[index]));
		social_chat_unheard_set((wchar_t const (*)[32])names, social_menu.moderated_count);
	}
	server_json_document_free(&document);
}

/* ---------- asking */

static boolean social_menu_online(
	void)
{
	return social_enabled() && social_account_code()[0];
}

/* the screen's request: the friends, the activity or the player shown, if
none is asked already */
static void fetch(
	short what)
{
	char path[64];

	if (social_menu.fetch_ticket || !social_menu_online())
		return;
	if (what == _fetch_friends)
		snprintf(path, sizeof(path), "/friends");
	else if (what == _fetch_activity)
		snprintf(path, sizeof(path), "/activity?limit=%d", SOCIAL_MENU_ACTIVITY);
	else if (what == _fetch_friend_prefetch)
		snprintf(path, sizeof(path), "/players/%s?matches=0", social_menu.prefetch_code);
	else
		snprintf(path, sizeof(path), "/players/%s?matches=%d", social_menu.player.code, SOCIAL_MENU_MATCHES);
	social_menu.fetch_ticket = social_send(path, NULL);
	if (social_menu.fetch_ticket)
		social_menu.fetching = what;
	else if (what == _fetch_friends)
		social_menu.friends_at = system_milliseconds();
	if (what == _fetch_player)
		social_menu.player_wanted = !social_menu.fetch_ticket;
}

/* the colour picked as an index into social_menu_name_colors (0: none) */
static short name_color_index(
	void)
{
	real red;
	real green;
	real blue;
	short index;

	if (!social_account_name_color(&red, &green, &blue))
		return 0;
	for (index = 1; index < (short)NUMBEROF(social_menu_name_colors); index++)
	{
		long value = strtol(social_menu_name_colors[index].hex, NULL, 16);

		if ((long)(red * 255.0f + 0.5f) == ((value >> 16) & 0xFF) && (long)(green * 255.0f + 0.5f) == ((value >> 8) & 0xFF) &&
			(long)(blue * 255.0f + 0.5f) == (value & 0xFF))
		{
			return index;
		}
	}
	return 0;
}

/* POST /account/name: the chat name typed, or the colour (NULL for the
other) */
static void name_send(
	char const *name,
	char const *color)
{
	struct server_json json;

	social_menu.action_from_chat = FALSE;
	if (!social_menu_online())
	{
		message_set("The community server is not reached: Settings > Gameplay > Community's ONLINE.");
		return;
	}
	if (social_menu.action_ticket)
	{
		message_set("Still waiting for the server: try again.");
		return;
	}
	server_json_begin(&json);
	server_json_object_begin(&json, NULL);
	if (name)
		server_json_string(&json, "name", name);
	if (color)
		server_json_string(&json, "color", color);
	server_json_object_end(&json);
	social_menu.action_ticket = social_send("/account/name", server_json_text(&json));
	server_json_free(&json);
	if (!social_menu.action_ticket)
	{
		message_set("The server could not be asked: try again.");
		return;
	}
	social_menu.action_kind = name ? _row_name : _row_name_color;
	snprintf(social_menu.action_name, sizeof(social_menu.action_name), "%s", name ? name : "");
	social_menu.action_code[0] = 0;
	message_set("Asking the server...");
}

/* ---------- the player's first setup

its three cases, as the server says the account (social_menu_setup_tick):
- no profile, no emblem on the account: a new install. The screen, and DONE
  makes the profile.
- a profile, no emblem on the account: the screen, with what the player has.
- an emblem on the account: no screen (a device without a profile gets one
  made by the account's name, which social_account_apply fills in). */
static struct
{
	/* no more of it until the game starts again */
	boolean finished;
	boolean wanted;
	boolean closing;
	char name[SOCIAL_MENU_NAME_SIZE];
	short color;
	unsigned long first_at;
	unsigned long asked_at;
} social_setup;

boolean social_menu_has_profile(
	void)
{
	word count = 1;
	long profile = NONE;

	player_profiles_enumerate_available_to_local_player_index(NONE, &count, &profile, FALSE);
	return (short)count > 0;
}

/* a profile made by a name, player 1's: NONE if it could not be */
static long social_setup_profile_make(
	char const *name)
{
	struct player_profile profile;
	wchar_t wide[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH];
	long index;

	wide_from_utf8(name, wide, NUMBEROF(wide));
	index = player_profile_new(0, wide);
	if (index == NONE)
	{
		/* (a name the saved games do not take) */
		saved_game_file_get_useable_untitled_profile_name(wide);
		index = player_profile_new(0, wide);
	}
	if (index == NONE || !player_profile_get(index, &profile))
	{
		platform_log("social: a profile could not be made");
		return NONE;
	}
	player_ui_set_active_player_profile(0, index, &profile);
	player_ui_remember_player1_profile(TRUE);
	return index;
}

/* the profile the player plays with: NONE for none */
static long social_setup_profile_index(
	void)
{
	long index = player_ui_get_active_player_profile_index(0);

	return index != NONE ? index : player_ui_get_player1_last_used_profile_index();
}

/* the name as a profile takes it: as many characters as it holds */
static void social_setup_name_set(
	char const *name)
{
	wchar_t wide[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH];

	wide_from_utf8(name, wide, NUMBEROF(wide));
	utf8_from_wide(wide, social_setup.name, sizeof(social_setup.name));
}

/* (the screen opening) what the player has */
static void social_setup_begin(
	void)
{
	char name[SOCIAL_MENU_NAME_SIZE];

	name[0] = 0;
	social_account_name(name, sizeof(name));
	social_setup_name_set(name);
	social_setup.color = name_color_index();
}

static void social_setup_finish(
	void)
{
	social_setup.finished = TRUE;
	social_setup.wanted = FALSE;
	social_setup.closing = TRUE;
}

/* DONE: the profile made, or named anew, then all of it told to the server */
static void social_setup_done(
	void)
{
	struct firefight_emblem_info emblem;
	struct server_json json;

	if (!social_setup.name[0])
	{
		message_set("Type your name first.");
		return;
	}
	if (social_menu.action_ticket)
	{
		message_set("Still waiting for the server: try again.");
		return;
	}
	/* (chosen before there was a profile to keep it) */
	firefight_stats_get_emblem(NONE, &emblem);
	if (!social_menu_has_profile())
	{
		long index = social_setup_profile_make(social_setup.name);

		if (index == NONE)
		{
			message_set("Your profile could not be made: try again.");
			return;
		}
		firefight_stats_set_emblem(index, &emblem);
	}
	else
	{
		struct player_profile profile;
		long index = social_setup_profile_index();
		wchar_t wide[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH];

		wide_from_utf8(social_setup.name, wide, NUMBEROF(wide));
		if (index != NONE && player_profile_get(index, &profile) && ustrcmp(profile.player_name, wide))
		{
			ustrncpy(profile.player_name, wide, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH - 1);
			profile.player_name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH - 1] = 0;
			player_profile_save(index, &profile);
			if (player_ui_get_active_player_profile_index(0) == index)
				player_ui_set_active_player_profile(0, index, &profile);
		}
	}
	if (!social_enabled())
	{
		social_setup_finish();
		return;
	}
	server_json_begin(&json);
	server_json_object_begin(&json, NULL);
	server_json_string(&json, "name", social_setup.name);
	server_json_string(&json, "color", social_menu_name_colors[social_setup.color].hex);
	social_emblem_json(&json, "emblem", &emblem);
	server_json_object_end(&json);
	social_menu.action_ticket = social_send("/account/setup", server_json_text(&json));
	server_json_free(&json);
	social_menu.action_from_chat = FALSE;
	if (!social_menu.action_ticket)
	{
		social_setup_finish();
		return;
	}
	social_menu.action_kind = _row_setup_done;
	social_menu.action_code[0] = 0;
	message_set("Saving...");
}

boolean social_menu_setup_tick(
	void)
{
	unsigned long now = system_milliseconds();
	boolean profile = social_menu_has_profile();
	boolean known = social_enabled() && social_account_known();

	if (!social_setup.first_at)
		social_setup.first_at = now | 1;
	if (social_menu.open)
		return FALSE;
	if (known && social_account_ranks_served() && social_account_emblem(NULL))
	{
		social_setup.wanted = FALSE;
		if (!profile)
		{
			char name[SOCIAL_MENU_NAME_SIZE];

			social_account_name(name, sizeof(name));
			if (social_setup_profile_make(name[0] ? name : "Spartan") != NONE)
				social_account_apply();
		}
		social_rank_catch_up();
		return FALSE;
	}
	if (social_setup.finished)
		return FALSE;
	if (!profile)
	{
		social_setup.wanted = known || !social_enabled() ||
			(long)(now - social_setup.first_at) >= SOCIAL_SETUP_WAIT_MILLISECONDS;
	}
	else
		social_setup.wanted = known && social_account_ranks_served();
	if (!social_setup.wanted || (social_setup.asked_at && (long)(now - social_setup.asked_at) < SOCIAL_SETUP_ASK_MILLISECONDS))
		return FALSE;
	social_setup.asked_at = now | 1;
	return TRUE;
}

boolean social_menu_setup_wanted(
	void)
{
	return social_setup.wanted;
}

boolean social_menu_setup_closing(
	void)
{
	boolean closing = social_setup.closing;

	social_setup.closing = FALSE;
	return closing;
}

/* CLEAR RANK: on the server first, whose answer says it was (without one,
this device's alone) */
static void rank_clear(
	void)
{
	struct server_json json;

	if (!social_enabled())
	{
		firefight_stats_set_career_xp(0L);
		message_set("Your rank is cleared.");
		return;
	}
	if (social_menu.action_ticket)
	{
		message_set("Still waiting for the server: try again.");
		return;
	}
	server_json_begin(&json);
	server_json_object_begin(&json, NULL);
	server_json_boolean(&json, "reset", 1);
	server_json_object_end(&json);
	social_menu.action_ticket = social_send("/account/rank", server_json_text(&json));
	server_json_free(&json);
	social_menu.action_from_chat = FALSE;
	if (!social_menu.action_ticket)
	{
		message_set("The server could not be asked: try again.");
		return;
	}
	social_menu.action_kind = _row_clear_rank;
	social_menu.action_code[0] = 0;
	message_set("Asking the server...");
}

/* an action: POST path of {code, ...}, said as done once answered */
static void act_as(
	short kind,
	char const *path,
	char const *code,
	char const *name,
	char const *action,
	char const *reason,
	boolean from_chat)
{
	struct server_json json;

	social_menu.action_from_chat = from_chat;

	if (!social_menu_online())
	{
		message_set("The community server is not reached: Settings > Gameplay > Community's ONLINE.");
		return;
	}
	if (social_menu.action_ticket)
	{
		message_set("Still waiting for the server: try again.");
		return;
	}
	server_json_begin(&json);
	server_json_object_begin(&json, NULL);
	server_json_string(&json, "code", code);
	if (action)
		server_json_string(&json, "action", action);
	if (reason)
		server_json_string(&json, "reason", reason);
	server_json_object_end(&json);
	social_menu.action_ticket = social_send(path, server_json_text(&json));
	server_json_free(&json);
	if (!social_menu.action_ticket)
	{
		message_set("The server could not be asked: try again.");
		return;
	}
	social_menu.action_kind = kind;
	snprintf(social_menu.action_name, sizeof(social_menu.action_name), "%s", name && name[0] ? name : code);
	snprintf(social_menu.action_code, sizeof(social_menu.action_code), "%s", code);
	message_set("Asking the server...");
}

static void act(
	short kind,
	char const *path,
	char const *code,
	char const *name,
	char const *action,
	char const *reason)
{
	act_as(kind, path, code, name, action, reason, FALSE);
}

/* an action answered: what it did, or why not; what it changed asked for again */
static void action_take(
	boolean answered,
	int status)
{
	char const *name = social_menu.action_name;
	char const *answer = social_menu.answer;

	/* (the setup is never held up by the server: unsaid, it is asked again
	the next time the game starts) */
	if (social_menu.action_kind == _row_setup_done)
	{
		if (answered && status == 200)
			social_account_answer(answer);
		else
			platform_log("social: the setup was not taken (%d)", status);
		social_setup_finish();
	}
	else if (!answered)
		message_set("The server did not answer: try again.");
	else if (social_menu.action_kind == _row_clear_rank && status != 200)
		message_set("The server could not clear your rank (%d).", status);
	else if (social_menu.action_kind == _row_sync_input && status != 200)
	{
		if (status == 404)
			message_set("That code is not good: it expired, or its screen was closed.");
		else if (status == 409)
			message_set("This device is already synced, or its account already has devices.");
		else if (status == 422 && strstr(answer, "same_account"))
			message_set("That code is this account's own.");
		else if (status == 422)
			message_set("That is not a sync code (XXXX-XXXX).");
		else if (status == 429)
			message_set("Too many wrong codes: wait a minute.");
		else
			message_set("The server refused it (%d).", status);
	}
	else if ((social_menu.action_kind == _row_name || social_menu.action_kind == _row_name_color) && status != 200)
	{
		if (status == 404)
			message_set("The community server cannot set a chat name yet: it needs updating.");
		else if (status == 429)
			message_set("You changed your name a moment ago: wait a few seconds.");
		else
			message_set("That name or colour is not allowed.");
	}
	else if (status == 404 && social_menu.action_kind == _row_code)
		message_set("The community server cannot draw a new code yet: it needs updating.");
	else if (status == 429 && social_menu.action_kind == _row_code)
		message_set("A new code was drawn a moment ago: wait a minute.");
	else if (status == 404 && strstr(answer, "\"not_found\""))
		message_set("The community server has no friends yet: it needs updating.");
	else if (status == 404)
		message_set(social_menu.action_kind == _row_friend_accept ? "That request is gone." : "No player has that code.");
	else if (status == 403 && strstr(answer, "not_accepting"))
		message_set("%s takes friend requests by their code alone.", name);
	else if (status == 403 && strstr(answer, "friends_off"))
		message_set("Your friend list is off: SETTINGS > FRIENDS on this screen.");
	else if (status == 403 && strstr(answer, "\"blocked\""))
		message_set("You blocked %s: unblock them first.", name);
	else if (status == 403)
		message_set("%s cannot be added.", name);
	else if (status == 422 && strstr(answer, "own code"))
		message_set("That is your own code.");
	else if (status == 422 && strstr(answer, "too_many"))
		message_set("Your list is full.");
	else if (status == 422)
		message_set("That is not a player's code (XXXX-XXXX).");
	else if (status == 429)
		message_set("Too many at once: wait a moment.");
	else if (status != 200)
		message_set("The server refused it (%d).", status);
	else
	{
		switch (social_menu.action_kind)
		{
		case _row_add_friend:
		case _row_friend_add:
			message_set(strstr(answer, "\"friends\"") ? "%s is now your friend." : "Asked %s to be your friend.", name);
			break;
		case _row_friend_accept:
			message_set("%s is now your friend.", name);
			break;
		case _row_friend_decline:
			message_set("Declined %s's request.", name);
			break;
		case _row_friend_cancel:
			message_set("Took back the request to %s.", name);
			break;
		case _row_friend_remove:
			message_set("%s is no longer your friend.", name);
			break;
		case _row_sync_input:
			social_account_code_take(answer);
			social_sync_pull_soon();
			social_menu.friends_had = FALSE;
			social_menu.outgoing_count = 0;
			message_set("Synced: this device is now part of account %s.", social_account_code());
			break;
		case _row_name:
		case _row_name_color:
			social_account_name_take(answer);
			{
				char chat_name[SOCIAL_MENU_NAME_SIZE];

				social_account_name(chat_name, sizeof(chat_name));
				if (social_menu.action_kind == _row_name)
					message_set("Your chat name is %s.", chat_name);
				else
					message_set("Your name's colour is %s.", social_menu_name_colors[name_color_index()].label);
			}
			break;
		case _row_clear_rank:
			firefight_stats_set_career_xp(0L);
			social_account_answer(answer);
			message_set("Your rank is cleared. The main menu counts your records again.");
			break;
		case _row_code:
			if (!social_account_code_take(answer))
				message_set("The server did not say the new code: try again.");
			else if (own_code_hidden())
				message_set("You have a new code (hidden: YOUR CODE shows it). The old one no longer works.");
			else
				message_set("Your new code is %s. The old one no longer works.", social_account_code());
			break;
		case _row_mute:
			message_set("Muted %s: their lines are hidden.", name);
			break;
		case _row_unmute:
			message_set("%s is no longer muted.", name);
			break;
		case _row_block:
			message_set("Blocked %s.", name);
			break;
		case _row_unblock:
			message_set("%s is no longer blocked.", name);
			break;
		case _row_reason:
			message_set(strstr(answer, "\"duplicate\":true") ? "You reported %s already (and muted them)." :
				"Reported %s, and muted them. Thank you.", name);
			break;
		}
	}
	/* (said in the chat too, when it asked) */
	if (social_menu.action_from_chat)
		social_chat_notice_text(social_menu.message);
	social_menu.action_from_chat = FALSE;
	/* (what it changed; a request of yours taken back, or gone with a
	block, not said as declined) */
	if (answered && status == 200)
	{
		short index;

		for (index = 0; index < social_menu.outgoing_count; index++)
		{
			if (!strcmp(social_menu.outgoing[index].code, social_menu.action_code))
			{
				social_menu.outgoing[index] = social_menu.outgoing[--social_menu.outgoing_count];
				break;
			}
		}
	}
	social_menu.friends_at = 0;
	social_menu.friends_had = social_menu.friends_had && answered;
	social_menu.moderation_due = TRUE;
	if (social_menu.list == _list_card || social_menu.list == _list_player || social_menu.depth)
		social_menu.player_wanted = TRUE;
	if (social_menu.list == _list_report && answered && status == 200)
	{
		/* (back to the player reported) */
		social_menu_back();
	}
}

static void social_menu_join_update(unsigned long now);

/* the sync code shown (its screen open, the account online) is asked for
every SOCIAL_MENU_SYNC_MILLISECONDS; with the screen left, the menu closed or
the account offline it is cancelled, which the server also does by itself
45 seconds after the last time it was asked for */
static void sync_code_update(
	unsigned long now)
{
	boolean shown = social_menu.open && social_menu.list == _list_sync_code && social_menu_online();

	if (social_menu.sync_ticket)
	{
		int status = 0;
		int result = community_net_poll(social_menu.sync_ticket, &status, social_menu.sync_answer,
			sizeof(social_menu.sync_answer));

		if (result == _community_net_waiting)
			return;
		social_menu.sync_ticket = 0;
		if (!shown)
			return;
		social_menu.sync_status = result == _community_net_answered ? status : -1;
		if (status == 200 && result == _community_net_answered)
		{
			struct server_json_document document;
			char code[sizeof(social_menu.sync_code)];
			boolean was_synced = social_sync_active();

			code[0] = 0;
			if (server_json_parse(&document, social_menu.sync_answer, (int)strlen(social_menu.sync_answer), 16))
			{
				int token = server_json_member(&document, 0, "data");

				token = token >= 0 ? server_json_member(&document, token, "code") : -1;
				if (token < 0 || !server_json_get_string(&document, token, code, sizeof(code)))
					code[0] = 0;
				server_json_document_free(&document);
			}
			social_sync_answer(social_menu.sync_answer);
			if (social_sync_active() && !was_synced)
				social_menu.sync_joined = TRUE;
			snprintf(social_menu.sync_code, sizeof(social_menu.sync_code), "%s", code);
		}
		else
			social_menu.sync_code[0] = 0;
		return;
	}
	if (!shown)
	{
		if (social_menu.sync_code[0] || social_menu.sync_at)
		{
			community_net_forget(social_send("/account/sync/cancel", "{}"));
			social_menu.sync_code[0] = 0;
			social_menu.sync_at = 0;
		}
		return;
	}
	if (!social_menu.sync_at || now - social_menu.sync_at >= SOCIAL_MENU_SYNC_MILLISECONDS)
	{
		struct server_json json;

		server_json_begin(&json);
		server_json_object_begin(&json, NULL);
		if (social_menu.sync_code[0])
			server_json_string(&json, "code", social_menu.sync_code);
		social_sync_skulls_write(&json, "skulls");
		server_json_object_end(&json);
		social_menu.sync_ticket = social_send("/account/sync/code", server_json_text(&json));
		server_json_free(&json);
		social_menu.sync_at = now ? now : 1;
	}
}

void social_menu_update(
	void)
{
	unsigned long now = system_milliseconds();

	social_menu_join_update(now);
	sync_code_update(now);
	if (!social_menu_online())
		return;
	if (social_menu.fetch_ticket)
	{
		int status = 0;
		int result = community_net_poll(social_menu.fetch_ticket, &status, social_menu.answer,
			sizeof(social_menu.answer));
		boolean answered = result == _community_net_answered && status == 200;

		if (result != _community_net_waiting)
		{
			short what = social_menu.fetching;

			social_menu.fetch_ticket = 0;
			social_menu.fetching = _fetch_none;
			if (what == _fetch_friends)
				friends_take(answered);
			else if (what == _fetch_activity)
				activity_take(answered);
			else if (what == _fetch_player)
				player_take(result == _community_net_answered, status);
			else if (what == _fetch_friend_prefetch)
				prefetch_take(result == _community_net_answered, status);
		}
	}
	if (social_menu.action_ticket)
	{
		int status = 0;
		int result = community_net_poll(social_menu.action_ticket, &status, social_menu.answer,
			sizeof(social_menu.answer));

		if (result != _community_net_waiting)
		{
			social_menu.action_ticket = 0;
			action_take(result == _community_net_answered, status);
		}
	}
	/* (a player asked for, by the screen or the chat; and the friends once,
	which the chat's names ask about) */
	if (social_menu.player_wanted && social_menu.player.code[0])
		fetch(_fetch_player);
	if (!social_menu.friends_had && !social_menu.friends_at)
		fetch(_fetch_friends);
	/* (and again once the server's key of them changed) */
	if (social_menu.friends_due && !social_menu.fetch_ticket && !social_menu.action_ticket)
	{
		fetch(_fetch_friends);
		social_menu.friends_due = social_menu.fetch_ticket == 0;
	}
	/* Prefetch friend stats in background so their real Service Rank shows in the main menu list */
	if (!social_menu.fetch_ticket && !social_menu.action_ticket && social_menu.friends_had)
	{
		short fi;
		for (fi = 0; fi < social_menu.friend_count; fi++)
		{
			if (social_menu.friends[fi].code[0] &&
				!social_extensions_has_cached_person(social_menu.friends[fi].code))
			{
				snprintf(social_menu.prefetch_code, sizeof(social_menu.prefetch_code), "%s", social_menu.friends[fi].code);
				snprintf(social_menu.prefetch_name, sizeof(social_menu.prefetch_name), "%s", social_menu.friends[fi].name);
				fetch(_fetch_friend_prefetch);
				break;
			}
		}
	}
	/* (the players muted and blocked: the chat's, whether the screen is open) */
	if (social_menu.moderation_ticket)
	{
		int status = 0;
		int result = community_net_poll(social_menu.moderation_ticket, &status, social_menu.answer,
			sizeof(social_menu.answer));

		if (result != _community_net_waiting)
		{
			social_menu.moderation_ticket = 0;
			social_menu.moderation_failed = result != _community_net_answered || status != 200;
			if (!social_menu.moderation_failed)
				moderation_take();
			social_menu.moderation_at = now ? now : 1;
		}
	}
	else if (social_menu.moderation_due ||
		((!social_menu.moderation_had || social_menu.moderation_failed) &&
			(!social_menu.moderation_at || now - social_menu.moderation_at > SOCIAL_MENU_RETRY_MILLISECONDS)))
	{
		social_menu.moderation_ticket = social_send("/moderation", NULL);
		if (social_menu.moderation_ticket)
			social_menu.moderation_due = FALSE;
		social_menu.moderation_at = now ? now : 1;
	}
}

/* ---------- the lists */

static void row_add(
	short kind,
	short value)
{
	if (social_menu.count >= SOCIAL_MENU_ROWS)
		return;
	social_menu.rows[social_menu.count].kind = kind;
	social_menu.rows[social_menu.count].value = value;
	social_menu.count++;
}

static struct social_menu_row const *row_get(
	short row)
{
	return row >= 0 && row < social_menu.count ? &social_menu.rows[row] : NULL;
}

/* a list's note when it has nothing: the server not reached, asked, or none */
static void note_add(
	boolean had,
	boolean failed)
{
	row_add(_row_note, !social_menu_online() ? _note_offline : failed ? _note_failed : had ? _note_none : _note_loading);
}

/* a friend's game has a JOIN GAME: one that may be joined (its invite, or
its listing's key), or a match said to be private (A says so) */
static boolean presence_join_shown(
	struct social_menu_presence const *presence)
{
	return presence->online && (presence->invite[0] || presence->host_key[0] ||
		(presence->private_game && (!strcmp(presence->state, "multiplayer") || !strcmp(presence->state, "lobby"))));
}

static void rows_build(
	void)
{
	struct social_menu_player const *player = &social_menu.player;
	short index;

	social_menu.count = 0;
	switch (social_menu.list)
	{
	case _list_home:
		row_add(_row_my_account, 0);
		if (social_enabled())
		{
			row_add(_row_friendlist_menu, 0);
			row_add(_row_activity_menu, 0);
			row_add(_row_settings_menu, 0);
		}
		else
			row_add(_row_settings, 0);
		break;
	case _list_account:
		if (social_enabled())
			row_add(_row_you, 0);
		social_extensions_add_home_rows(row_add);
		if (social_enabled())
			row_add(_row_sync, 0);
		row_add(_row_clear_rank, 0);
		break;
	case _list_friendlist:
		row_add(_row_code, 0);
		row_add(_row_requests, 0);
		row_add(_row_add_friend, 0);
		row_add(_row_friends, 0);
		row_add(_row_moderation, 0);
		break;
	case _list_activity_menu:
		row_add(_row_recent, 0);
		row_add(_row_activity, 0);
		break;
	case _list_identity:
		row_add(_row_name, 0);
		row_add(_row_name_color, 0);
		break;
	case _list_setup:
		row_add(_row_setup_name, 0);
		row_add(_row_setup_color, 0);
		row_add(_social_ext_row_emblem, 0);
		row_add(_row_setup_done, 0);
		break;
	case _list_friends:
		for (index = 0; index < social_menu.friend_count; index++)
		{
			row_add(_row_friend, index);
			if (presence_join_shown(&social_menu.friends[index].presence))
				row_add(_row_friend_join, index);
		}
		if (!social_menu.friend_count)
			note_add(social_menu.friends_had, social_menu.friends_failed);
		break;
	case _list_requests:
		for (index = 0; index < social_menu.incoming_count; index++)
			row_add(_row_incoming, index);
		for (index = 0; index < social_menu.outgoing_count; index++)
			row_add(_row_outgoing, index);
		if (!social_menu.incoming_count && !social_menu.outgoing_count)
			note_add(social_menu.friends_had, social_menu.friends_failed);
		break;
	case _list_activity:
		for (index = 0; index < social_menu.activity_count; index++)
			row_add(_row_activity_entry, index);
		if (!social_menu.activity_count)
			note_add(social_menu.activity_had, social_menu.activity_failed);
		break;
	case _list_recent:
		for (index = 0; index < social_menu.recent_count; index++)
			row_add(_row_recent_player, index);
		if (!social_menu.recent_count)
			row_add(_row_note, _note_none);
		break;
	case _list_card:
		/* (the card is drawn where the rows are: none; its OPTIONS button,
		social_menu_card_options, opens what can be done) */
		break;
	case _list_player:
		row_add(_row_matches, 0);
		if (!player->had || !strcmp(player->relation, "self"))
			break;
		if (presence_join_shown(&player->presence))
			row_add(_row_join, 0);
		if (config_boolean("social.chat") && config_boolean("social.private_chat") && !player->blocked)
			row_add(_row_message, 0);
		if (!strcmp(player->relation, "friends"))
			row_add(_row_friend_remove, 0);
		else if (!strcmp(player->relation, "incoming"))
		{
			row_add(_row_friend_accept, 0);
			row_add(_row_friend_decline, 0);
		}
		else if (!strcmp(player->relation, "outgoing"))
			row_add(_row_friend_cancel, 0);
		else if (!player->blocked)
			row_add(_row_friend_add, 0);
		row_add(player->muted ? _row_unmute : _row_mute, 0);
		row_add(player->blocked ? _row_unblock : _row_block, 0);
		row_add(_row_report, 0);
		break;
	case _list_matches:
		for (index = 0; index < player->match_count; index++)
			row_add(_row_match, index);
		if (!player->match_count)
			note_add(player->had, social_menu.player_failed);
		break;
	case _list_moderation:
		for (index = 0; index < social_menu.moderated_count; index++)
			row_add(_row_moderated, index);
		if (!social_menu.moderated_count)
			note_add(social_menu.moderation_had, FALSE);
		break;
	case _list_sync:
		row_add(_row_sync_create, 0);
		row_add(_row_sync_input, 0);
		break;
	case _list_sync_code:
		row_add(_row_sync_code, 0);
		break;
	case _list_report:
		for (index = 0; index < (short)NUMBEROF(social_menu_reasons); index++)
			row_add(_row_reason, index);
		break;
	default:
		if (social_extensions_is_ext_list(social_menu.list))
			social_extensions_build_list_rows(social_menu.list, row_add);
		break;
	}
}

/* a list opened from the one shown (B comes back to it as it was) */
static void list_open(
	short list)
{
	if (social_menu.depth < SOCIAL_MENU_DEPTH)
	{
		social_menu.stack[social_menu.depth].list = social_menu.list;
		social_menu.stack[social_menu.depth].first = social_menu.first;
		social_menu.stack[social_menu.depth].chosen = social_menu.chosen;
		social_menu.depth++;
	}
	social_menu.list = list;
	social_menu.list_serial++;
	social_menu.first = 0;
	social_menu.chosen = 0;
	social_menu.confirming_kind = NONE;
	if (list == _list_friendlist || list == _list_friends || list == _list_requests)
		fetch(_fetch_friends);
	else if (list == _list_activity)
	{
		social_menu.activity_had = FALSE;
		fetch(_fetch_activity);
	}
	else if (list == _list_moderation)
		social_menu.moderation_due = TRUE;
	else if (list == _list_recent)
		social_menu.recent_count = social_chat_recent_players(social_menu.recent_codes, social_menu.recent_names,
			SOCIAL_MENU_RECENT);
}

/* a player's list (its records asked for), by its code; its name shown
until they come */
static void player_open(
	char const *code,
	char const *name)
{
	struct social_menu_player *player = &social_menu.player;

	if (!code[0])
		return;
	if (strcmp(player->code, code))
	{
		csmemset(player, 0, sizeof(*player));
		snprintf(player->code, sizeof(player->code), "%s", code);
		snprintf(player->name, sizeof(player->name), "%s", name && name[0] ? name : code);
	}
	social_menu.player_failed = FALSE;
	social_menu.card_creator = FALSE;
	social_menu.card_button_focused = FALSE;
	list_open(_list_card);
	social_menu.player_wanted = TRUE;
}

void social_menu_open_at(
	short where)
{
	social_menu.opening = where;
}

void social_menu_open(
	void)
{
	social_menu.open = TRUE;
	social_menu.depth = 0;
	social_menu.list = _list_home;
	social_menu.list_serial++;
	social_menu.first = 0;
	social_menu.chosen = 0;
	social_menu.message[0] = 0;
	social_menu.confirming_kind = NONE;
	social_menu.typing = _typing_none;
	social_menu.friends_at = 0;
	if (social_menu.opening == _social_menu_open_moderation)
	{
		list_open(_list_moderation);
		/* (B leaves the screen, back to Settings) */
		social_menu.depth = 0;
	}
	else if (social_menu.opening == _social_menu_open_player && social_menu.opening_code[0])
	{
		player_open(social_menu.opening_code, social_menu.opening_name);
		social_menu.depth = 0;
	}
	else if (social_menu.opening == _social_menu_open_identity)
	{
		list_open(_list_identity);
		social_menu.depth = 0;
	}
	else if (social_menu.opening == _social_menu_open_setup)
	{
		social_setup_begin();
		list_open(_list_setup);
		social_menu.depth = 0;
	}
	else if (social_menu.opening == _social_menu_open_profile && social_account_code()[0])
	{
		char me[SOCIAL_MENU_NAME_SIZE];

		social_account_name(me, sizeof(me));
		player_open(social_account_code(), me);
		social_menu.depth = 0;
	}
	social_menu.opening = _social_menu_open_home;
}

void social_menu_close(
	void)
{
	social_menu.open = FALSE;
	if (social_menu.typing != _typing_none && virtual_keyboard_active())
		virtual_keyboard_close();
	social_menu.typing = _typing_none;
}

static void keyboard_open(
	short typing)
{
	csmemset(social_menu.typed, 0, sizeof(social_menu.typed));
	virtual_keyboard_allow_any_name();
	if (!virtual_keyboard_launch(social_menu.typed, (word)sizeof(social_menu.typed), SOCIAL_MENU_KEYBOARD_CAPTION))
	{
		message_set("The keyboard did not open.");
		return;
	}
	social_menu.typing = typing;
}

void social_menu_poll(
	void)
{
	unsigned long now = system_milliseconds();

	/* (a friend's code typed, once the keyboard has closed) */
	if (social_menu.typing != _typing_none && !virtual_keyboard_active())
	{
		short wanted = social_menu.typing;

		social_menu.typing = _typing_none;
		if (virtual_keyboard_last_exit_saved_text())
		{
			char code[SOCIAL_MENU_TYPED_LENGTH * 3 + 1];

			utf8_from_wide(social_menu.typed, code, sizeof(code));
			if (wanted == _typing_name || wanted == _typing_setup_name)
			{
				/* (a name, not a code: as typed, its ends trimmed) */
				char *start = code;
				size_t length;

				while (*start == ' ')
					start++;
				length = strlen(start);
				while (length && start[length - 1] == ' ')
					start[--length] = 0;
				if (wanted == _typing_setup_name)
				{
					if (start[0])
						social_setup_name_set(start);
				}
				else if (start[0])
					name_send(start, NULL);
				code[0] = 0;
			}
			else
				code_normalize(code);
			if (code[0] && wanted == _typing_sync_code)
				act(_row_sync_input, "/account/sync/redeem", code, code, NULL, NULL);
			else if (code[0])
				act(_row_add_friend, "/friends", code, code, NULL, NULL);
		}
	}
	if (social_menu.message[0] && now - social_menu.message_time > SOCIAL_MENU_MESSAGE_MILLISECONDS &&
		!social_menu.action_ticket)
	{
		social_menu.message[0] = 0;
	}
	if (social_menu.confirming_kind != NONE && now - social_menu.confirming_time > SOCIAL_MENU_CONFIRM_MILLISECONDS)
		social_menu.confirming_kind = NONE;
	/* (what the lists shown want: the friends again a while after, the
	player asked for) */
	if (social_menu.list == _list_home || social_menu.list == _list_friendlist ||
		social_menu.list == _list_friends || social_menu.list == _list_requests ||
		((social_menu.list == _list_card || social_menu.list == _list_player) &&
			!strcmp(social_menu.player.relation, "friends")))
	{
		if (!social_menu.friends_at || now - social_menu.friends_at > SOCIAL_MENU_REFRESH_MILLISECONDS)
			fetch(_fetch_friends);
	}
	/* (the row chosen kept as rows come and go in the same list: a friend's
	JOIN GAME) */
	{
		static short built_serial = NONE;
		struct social_menu_row const *was = built_serial == social_menu.list_serial ? row_get(social_menu.chosen) : NULL;
		short kind = was ? was->kind : NONE;
		short value = was ? was->value : NONE;
		short index;

		rows_build();
		built_serial = social_menu.list_serial;
		was = row_get(social_menu.chosen);
		if (kind != NONE && (!was || was->kind != kind || was->value != value))
		{
			for (index = 0; index < social_menu.count; index++)
			{
				if (social_menu.rows[index].kind == kind && social_menu.rows[index].value == value)
				{
					social_menu.chosen = index;
					break;
				}
			}
		}
	}
	if (social_menu.chosen >= social_menu.count)
		social_menu.chosen = (short)MAX(0, social_menu.count - 1);
}

short social_menu_list(
	void)
{
	return social_menu.list_serial;
}

short social_menu_count(
	void)
{
	return social_menu.count;
}

short *social_menu_first(
	void)
{
	return &social_menu.first;
}

short *social_menu_chosen(
	void)
{
	return &social_menu.chosen;
}

/* ---------- what the rows say */

/* a player's state in a row: IN MENUS, LOBBY 3/16, SLAYER 3/16, PRIVATE
MATCH, SOLO, OFFLINE... (ONLINE when its activity is hidden) */
static void presence_state_plain(
	struct social_menu_presence const *presence,
	char *text,
	long size)
{
	boolean match = !strcmp(presence->state, "multiplayer");
	boolean lobby = !strcmp(presence->state, "lobby");

	if (!presence->online)
		snprintf(text, (size_t)size, "OFFLINE");
	else if ((match || lobby) && presence->private_game && !presence->invite[0] && !presence->host_key[0])
		snprintf(text, (size_t)size, "PRIVATE MATCH");
	else if (match || lobby)
	{
		char kind[16];
		long index;

		/* (the gametype's name, short enough for the row, in capitals) */
		snprintf(kind, sizeof(kind), "%.11s", lobby ? "LOBBY" : presence->gametype[0] ? presence->gametype : "PLAYING");
		for (index = 0; kind[index]; index++)
			kind[index] = (char)(kind[index] >= 'a' && kind[index] <= 'z' ? kind[index] - 'a' + 'A' : kind[index]);
		if (presence->maximum > 0)
			snprintf(text, (size_t)size, "%s %ld/%ld", kind, presence->players, presence->maximum);
		else
			snprintf(text, (size_t)size, "%s", kind);
	}
	else if (!strcmp(presence->state, "campaign"))
		snprintf(text, (size_t)size, presence->maximum > 0 ? "CO-OP" : "SOLO");
	else if (!strcmp(presence->state, "theater"))
		snprintf(text, (size_t)size, "THEATER");
	else if (!strcmp(presence->state, "menus"))
		snprintf(text, (size_t)size, "IN MENUS");
	else
		snprintf(text, (size_t)size, "ONLINE");
}

/* (and where it is said in words, the platform after it) */
static void presence_state(
	struct social_menu_presence const *presence,
	char *text,
	long size)
{
	presence_state_plain(presence, text, size);
	if (presence->online)
	{
		char const *platform = platform_icon_label(presence->platform);

		if (platform)
			text_add(text, size, " | %s", platform);
	}
}

/* what a player is doing, as a line of the details */
static void presence_line(
	struct social_menu_presence const *presence,
	char *text,
	long size)
{
	char ago[SOCIAL_MENU_TIME_SIZE];

	if (!presence->online)
	{
		ago_text(presence->last_seen, ago, sizeof(ago));
		text_add(text, size, ago[0] ? "Offline, last seen %s.\n" : "Offline.\n", ago);
		return;
	}
	if (!strcmp(presence->state, "multiplayer"))
	{
		text_add(text, size, "Playing %s%s%s", presence->gametype[0] ? presence->gametype : "multiplayer",
			presence->map[0] ? " on " : "", presence->map);
		if (presence->maximum > 0)
			text_add(text, size, " (%ld/%ld)", presence->players, presence->maximum);
		text_add(text, size, presence->host ? ", hosting %s.\n" : ".\n", presence->game);
		if (presence->invite[0])
			text_add(text, size, "JOIN GAME joins it.\n");
		else if (presence->host_key[0])
			text_add(text, size, "A public match with a password.\n");
		else if (presence->private_game)
			text_add(text, size, "A private match.\n");
	}
	else if (!strcmp(presence->state, "lobby"))
	{
		text_add(text, size, presence->host ? "In the lobby of %s, hosting it" : "In the lobby of %s",
			presence->game[0] ? presence->game : "a match");
		if (presence->maximum > 0)
			text_add(text, size, " (%ld/%ld)", presence->players, presence->maximum);
		text_add(text, size, ".\n");
		if (presence->invite[0])
			text_add(text, size, "JOIN GAME joins it.\n");
		else if (presence->host_key[0])
			text_add(text, size, "A public match with a password.\n");
		else if (presence->private_game)
			text_add(text, size, "A private match.\n");
	}
	else if (!strcmp(presence->state, "campaign"))
	{
		text_add(text, size, "Playing the campaign%s%s.\n", presence->map[0] ? ": " : "", presence->map);
	}
	else if (!strcmp(presence->state, "theater"))
	{
		text_add(text, size, "Watching a replay.\n");
	}
	else
	{
		text_add(text, size, presence->state[0] ? "Online, in the menus.\n" : "Online.\n");
	}
}

/* a friend's JOIN GAME's state: the players, PASSWORD or PRIVATE */
static void join_state(
	struct social_menu_presence const *presence,
	char *text,
	long size)
{
	if (presence->invite[0] && presence->maximum > 0)
		snprintf(text, (size_t)size, "%ld/%ld", presence->players, presence->maximum);
	else if (!presence->invite[0] && presence->host_key[0])
		snprintf(text, (size_t)size, "PASSWORD");
	else if (!presence->invite[0])
		snprintf(text, (size_t)size, "PRIVATE");
}

/* ... and its details' last lines: what A does */
static void join_details(
	struct social_menu_presence const *presence,
	char *text,
	long size)
{
	if (presence->invite[0])
		text_add(text, size, "\nA: joins their game: it shows in\nDirect Link, where A joins it.");
	else if (presence->host_key[0])
		text_add(text, size, "\nA: finds their game in the server\nbrowser, which asks for its password.");
	else
		text_add(text, size, "\nTheir match is private: it is joined\nby its host's invite alone.");
}

/* A on a friend's JOIN GAME: its invite (Direct Link), its listing's key
(the server browser), or the match said to be private */
static short presence_join(
	struct social_menu_presence const *presence,
	char const *name)
{
	social_menu.join_invite[0] = social_menu.join_host_key[0] = 0;
	snprintf(social_menu.join_name, sizeof(social_menu.join_name), "%s", name);
	if (presence->invite[0])
	{
		snprintf(social_menu.join_invite, sizeof(social_menu.join_invite), "%s", presence->invite);
		return _social_menu_join;
	}
	if (presence->host_key[0])
	{
		snprintf(social_menu.join_host_key, sizeof(social_menu.join_host_key), "%s", presence->host_key);
		return _social_menu_join_listed;
	}
	if (presence->online && (!strcmp(presence->state, "multiplayer") || !strcmp(presence->state, "lobby")))
		message_set("%s is in a private match: it cannot be joined.", name);
	return _social_menu_stays;
}

/* a match in a row: its result and what it was */
static void match_name(
	struct social_menu_match const *match,
	boolean with_player,
	char *text,
	long size)
{
	char result[16];

	/* (one left before its end: QUIT, ENDED or DISCONNECTED) */
	snprintf(result, sizeof(result), "%s", match->end_reason[0] ? match->end_reason : match->result);
	upper_text(result);
	text[0] = 0;
	if (with_player)
		text_add(text, size, "%s: ", match->player);
	if (!strcmp(match->kind, "campaign"))
	{
		text_add(text, size, "%s %s", match->map, match->difficulty >= 0 && match->difficulty <= 3 ?
			social_menu_difficulties[match->difficulty] : "");
	}
	else
	{
		text_add(text, size, "%s%s%s", result[0] ? result : "", result[0] ? " " : "",
			match->gametype[0] ? match->gametype : "MATCH");
	}
}

static void match_details(
	struct social_menu_match const *match,
	char *text,
	long size)
{
	char line[32];

	if (!strcmp(match->kind, "campaign"))
	{
		text_add(text, size, "Campaign level %s won on %s.\n", match->map, match->difficulty >= 0 &&
			match->difficulty <= 3 ? social_menu_difficulties[match->difficulty] : "?");
		text_add(text, size, "Score %ld. Kills %ld, deaths %ld.\n", match->score, match->kills, match->deaths);
	}
	else
	{
		text_add(text, size, "%s on %s%s, %ld players.\n", match->gametype[0] ? match->gametype : "A match",
			match->map[0] ? match->map : "?", match->teams ? " (teams)" : "", match->players);
		if (!strcmp(match->end_reason, "quit"))
			text_add(text, size, "Quit before its end.\n");
		else if (!strcmp(match->end_reason, "disconnected"))
			text_add(text, size, "Left before its end: the connection was lost.\n");
		else if (match->end_reason[0])
			text_add(text, size, "Ended by its host before its end.\n");
		else if (match->result[0])
			text_add(text, size, "Result: %s.\n", match->result);
		if (match->place > 0)
			text_add(text, size, "Place: %ld.\n", match->place);
		kda_text(match->kills, match->deaths, match->assists, line, sizeof(line));
		text_add(text, size, "Score %ld. K %ld, D %ld, A %ld (KDA %s).\n", match->score, match->kills, match->deaths,
			match->assists, line);
		if (match->suicides || match->betrayals)
			text_add(text, size, "Suicides %ld, betrayals %ld.\n", match->suicides, match->betrayals);
	}
	if (match->medals)
		text_add(text, size, "Medals: %ld.\n", match->medals);
	if (match->seconds)
	{
		char duration[24];

		duration_text(match->seconds, duration, sizeof(duration));
		text_add(text, size, "Time: %s.\n", duration);
	}
	ago_text(match->played_at, line, sizeof(line));
	if (line[0])
		text_add(text, size, "Played %s.\n", line);
}

/* ---------- the player's card

drawn over the screen's rows while a player's card is shown (_list_card), as
the level's summary is drawn (campaign_score.c's panels), in units of the
screen's 480 lines from the card's top left: the player's name, code and
what they are doing; their multiplayer and campaign totals side by side;
and every medal's icon, those won with how many times, the others dimmed */

enum
{
	/* (the least it is, for its three totals: wider, as its room is when it
	is made smaller; a screen's room narrower than this makes it smaller) */
	SOCIAL_CARD_WIDTH = 780,
	SOCIAL_CARD_HEIGHT = 335,
	SOCIAL_CARD_PADDING = 14,
	SOCIAL_CARD_GAP = 8,
	/* (between the three totals, and one's two columns) */
	SOCIAL_CARD_HALF_GAP = 24,
	SOCIAL_CARD_COLUMN_GAP = 16,
	SOCIAL_CARD_MEDAL_SIZE = 22,
	/* (the least a medal and its count take: the row's room shared out
	among as many as fit) */
	SOCIAL_CARD_MEDAL_SPACE = 56,
	SOCIAL_CARD_TOTALS_ROWS = 6,
	/* (the button at its top right: CREATOR PROFILE, SERVICE RECORD) */
	SOCIAL_CARD_BUTTON_WIDTH = 160,
};

/* the card is drawn on the Social screen (its button shown, and the
creator profile when chosen), not in the chat's VIEW PROFILE */
static boolean card_interactive;

/* the card's fonts: its text, and the name's */
struct social_card_fonts
{
	struct campaign_score_panel text;
	struct campaign_score_panel title;
};

static void card_color(
	real_argb_color *color,
	real red,
	real green,
	real blue)
{
	color->alpha = 1.0f;
	color->red = red;
	color->green = green;
	color->blue = blue;
}

/* a label at the left of a column and its value at its right */
static void card_pair(
	struct campaign_score_panel const *panel,
	boolean draw,
	real x0,
	real x1,
	real y,
	char const *label,
	char const *value,
	boolean highlight)
{
	real_argb_color label_color;
	real_argb_color value_color;

	if (!draw || !label[0])
		return;
	card_color(&label_color, 0.55f, 0.72f, 1.0f);
	if (highlight)
		card_color(&value_color, 1.0f, 0.84f, 0.35f);
	else
		card_color(&value_color, 0.95f, 0.95f, 0.95f);
	campaign_score_panel_text(panel, label, NULL, x0, x1, y, 1.0f, _campaign_score_text_left, &label_color);
	campaign_score_panel_text(panel, value, NULL, x0, x1, y, 1.0f, _campaign_score_text_right, &value_color);
}

/* a mode's totals: two columns of label and value, rows of
pairs[row][column][label, value] (the first highlighted's value gold) */
static void card_totals(
	struct campaign_score_panel const *panel,
	boolean draw,
	real x0,
	real x1,
	real y,
	real row_height,
	char const *title,
	char pairs[SOCIAL_CARD_TOTALS_ROWS][2][2][24],
	short highlight_row,
	short highlight_column)
{
	real const middle = (x0 + x1) / 2.0f;
	real_argb_color color;
	short row;

	if (!draw)
		return;
	card_color(&color, 0.95f, 0.95f, 0.95f);
	campaign_score_panel_text(panel, title, NULL, x0, x1, y, 1.0f, _campaign_score_text_center, &color);
	y += row_height + 2.0f;
	for (row = 0; row < SOCIAL_CARD_TOTALS_ROWS; row++)
	{
		card_pair(panel, draw, x0, middle - SOCIAL_CARD_COLUMN_GAP / 2.0f, y, pairs[row][0][0], pairs[row][0][1],
			row == highlight_row && highlight_column == 0);
		card_pair(panel, draw, middle + SOCIAL_CARD_COLUMN_GAP / 2.0f, x1, y, pairs[row][1][0], pairs[row][1][1],
			row == highlight_row && highlight_column == 1);
		y += row_height;
	}
}

static void card_pair_set(
	char pair[2][24],
	char const *label,
	char const *format,
	...)
{
	va_list arguments;

	snprintf(pair[0], 24, "%s", label);
	va_start(arguments, format);
	vsnprintf(pair[1], 24, format, arguments);
	va_end(arguments);
}

/* the card's button at its top right (right, y: its top): what A on it
(Up from the button bar gives it the focus), or a click, turns the card to,
lit while it has the focus; where it was drawn kept for the click
(social_menu_card_button_hit) */
static void card_button(
	struct social_card_fonts const *fonts,
	boolean draw,
	real right,
	real y)
{
	struct campaign_score_panel const *panel = &fonts->text;
	real const height = fonts->title.line_height;
	real const x0 = right - SOCIAL_CARD_BUTTON_WIDTH;
	real const line = panel->line_height;
	boolean focused = social_menu.card_button_focused;
	real_argb_color color;

	if (!draw || !card_interactive)
		return;
	campaign_score_panel_quad(panel, x0, y, right, y + height, focused ? 0xC02896FF : 0x402896FF);
	campaign_score_panel_frame(panel, x0, y, right, y + height, focused ? 0xFFFFFFFF : CAMPAIGN_SCORE_PANEL_EDGE);
	if (focused)
		card_color(&color, 1.0f, 1.0f, 1.0f);
	else
		card_color(&color, 0.8f, 0.86f, 0.95f);
	campaign_score_panel_text(panel, social_menu.card_creator ? "SERVICE RECORD" : "CREATOR PROFILE", NULL,
		x0, right, y + (height - line) / 2.0f, 1.0f, _campaign_score_text_center, &color);
	social_menu.card_button.x0 = (short)(panel->left + x0 * panel->scale);
	social_menu.card_button.y0 = (short)(panel->top + y * panel->scale);
	social_menu.card_button.x1 = (short)(panel->left + right * panel->scale);
	social_menu.card_button.y1 = (short)(panel->top + (y + height) * panel->scale);
	social_menu.card_button_drawn_at = system_milliseconds() | 1;
}

/* a column of label and value rows the width of one of the card's three
(rows[row][label, value], the first's value gold), under its title */
static void card_column(
	struct campaign_score_panel const *panel,
	boolean draw,
	real x0,
	real x1,
	real y,
	real row_height,
	char const *title,
	char rows[SOCIAL_CARD_TOTALS_ROWS][2][24],
	short count)
{
	real_argb_color color;
	short row;

	if (!draw)
		return;
	card_color(&color, 0.95f, 0.95f, 0.95f);
	campaign_score_panel_text(panel, title, NULL, x0, x1, y, 1.0f, _campaign_score_text_center, &color);
	y += row_height + 2.0f;
	for (row = 0; row < count && row < SOCIAL_CARD_TOTALS_ROWS; row++)
	{
		card_pair(panel, draw, x0, x1, y, rows[row][0], rows[row][1], row == 0);
		y += row_height;
	}
}

/* the creator profile, in place of the totals and the medals (as tall as
they are): what the player made in Forge and the Tag Injector, and the file
share's place, to come; its bottom */
static real card_creator_layout(
	struct campaign_score_panel const *panel,
	boolean draw,
	boolean self,
	real left,
	real right,
	real third,
	real row,
	real y)
{
	struct social_menu_player const *player = &social_menu.player;
	real const second = left + third + SOCIAL_CARD_HALF_GAP;
	real const last = second + third + SOCIAL_CARD_HALF_GAP;
	long made[NUMBER_OF_SOCIAL_CREATOR_FIELDS];
	char const *since = player->creator_since;
	boolean counted = player->creator_had;
	char rows[SOCIAL_CARD_TOTALS_ROWS][2][24];
	char text[48];
	real_argb_color color;

	csmemcpy(made, player->creator, sizeof(made));
	/* (one's own as this game counts it, what the server has not heard yet
	too) */
	if (self)
	{
		long own[NUMBER_OF_SOCIAL_CREATOR_FIELDS];

		if (social_creator_totals(own) || !counted)
		{
			csmemcpy(made, own, sizeof(made));
			if (social_creator_since()[0])
				since = social_creator_since();
		}
		counted = TRUE;
	}

	csmemset(rows, 0, sizeof(rows));
	duration_text(made[_social_creator_forge_seconds], text, sizeof(text));
	card_pair_set(rows[0], "TIME", "%s", text);
	card_pair_set(rows[1], "MAPS MADE", "%ld", made[_social_creator_forge_maps]);
	card_column(panel, draw, left, left + third, y, row, "FORGE", rows, 2);

	csmemset(rows, 0, sizeof(rows));
	card_pair_set(rows[0], "TAG PROFILES", "%ld", made[_social_creator_tag_profiles]);
	card_pair_set(rows[1], "CAMPAIGN PROFILES", "%ld", made[_social_creator_campaign_profiles]);
	card_pair_set(rows[2], "MULTIPLAYER PROFILES", "%ld", made[_social_creator_multiplayer_profiles]);
	duration_text(made[_social_creator_range_seconds], text, sizeof(text));
	card_pair_set(rows[3], "FIRING RANGE", "%s", text);
	card_column(panel, draw, second, second + third, y, row, "TAG INJECTOR", rows, 4);

	csmemset(rows, 0, sizeof(rows));
	card_pair_set(rows[0], "CREATIONS", "%ld", made[_social_creator_forge_maps] + made[_social_creator_tag_profiles] +
		made[_social_creator_campaign_profiles] + made[_social_creator_multiplayer_profiles]);
	duration_text(made[_social_creator_forge_seconds] + made[_social_creator_range_seconds], text, sizeof(text));
	card_pair_set(rows[1], "TIME CREATING", "%s", text);
	ago_text(since, text, sizeof(text));
	card_pair_set(rows[2], "COUNTED SINCE", "%s", text[0] ? text : counted ? "-" : "NOT YET");
	card_column(panel, draw, last, right, y, row, "TOTAL", rows, 3);
	if (draw)
	{
		real const bottom = y + row * (SOCIAL_CARD_TOTALS_ROWS + 1);
		real const gap = SOCIAL_CARD_HALF_GAP / 2.0f;

		campaign_score_panel_quad(panel, second - gap, y, second - gap + 1.0f, bottom, CAMPAIGN_SCORE_CARD_RULE);
		campaign_score_panel_quad(panel, last - gap, y, last - gap + 1.0f, bottom, CAMPAIGN_SCORE_CARD_RULE);
	}
	y += row * (SOCIAL_CARD_TOTALS_ROWS + 1) + 2.0f;
	y += SOCIAL_CARD_GAP - 4.0f;
	if (draw)
		campaign_score_panel_quad(panel, left, y, right, y + 1.0f, CAMPAIGN_SCORE_CARD_RULE);
	y += SOCIAL_CARD_GAP;

	/* the file share, where the medals are on the service record (as tall) */
	{
		short per_row = (short)MAX((right - left) / SOCIAL_CARD_MEDAL_SPACE, 1);
		real const area = ((NUMBER_OF_CAMPAIGN_MEDALS + per_row - 1) / per_row) * (SOCIAL_CARD_MEDAL_SIZE + 4.0f);

		if (draw)
		{
			card_color(&color, 0.95f, 0.95f, 0.95f);
			campaign_score_panel_text(panel, "FILE SHARE", NULL, left, right, y, 1.0f, _campaign_score_text_left, &color);
		}
		y += row + 2.0f;
		if (draw)
		{
			card_color(&color, 0.6f, 0.66f, 0.75f);
			campaign_score_panel_text(panel, "COMING SOON", NULL, left, right, y + (area - panel->line_height) / 2.0f,
				1.0f, _campaign_score_text_center, &color);
		}
		y += area;
	}
	return y;
}

/* the card, from its top: its height, drawn or not */
static real card_layout(
	struct social_card_fonts const *fonts,
	boolean draw)
{
	struct social_menu_player const *player = &social_menu.player;
	struct campaign_score_panel const *panel = &fonts->text;
	real const left = SOCIAL_CARD_PADDING;
	real const right = panel->width - SOCIAL_CARD_PADDING;
	real const third = (right - left - SOCIAL_CARD_HALF_GAP * 2.0f) / 3.0f;
	real const line = panel->line_height;
	real const row = line + 4.0f;
	real_argb_color color;
	char text[160];
	char other[64];
	real y = SOCIAL_CARD_PADDING;
	boolean self = player->had && !strcmp(player->relation, "self");
	/* (the emblem left of the head's lines: the name, whose friend, and
	another's what they are doing) */
	real const emblem = player->had ? fonts->title.line_height + row * (self ? 1.0f : 2.0f) : 0.0f;
	real const head_left = player->had ? left + emblem + 8.0f : left;
	short index;

	if (draw && player->had)
		social_extensions_card_draw_emblem(panel, self, player->code, player->name, left, y, emblem);
	/* the name, and (one's own alone: a code is given by its player) the
	friend code */
	if (draw)
	{
		card_color(&color, 0.95f, 0.95f, 0.95f);
		campaign_score_panel_text(&fonts->title, player->name, NULL, head_left, right, y, 1.0f, _campaign_score_text_left,
			&color);
		/* (the button where the code was, the code under it) */
		snprintf(other, sizeof(other), "%s%s", self ? "CODE " : "", self ? own_code() : "");
		card_color(&color, 0.6f, 0.66f, 0.75f);
		campaign_score_panel_text(panel, other, NULL, left, right,
			card_interactive && player->had ? y + fonts->title.line_height + 2.0f :
			y + (fonts->title.line_height - line) / 2.0f, 1.0f, _campaign_score_text_right, &color);
	}
	if (player->had)
		card_button(fonts, draw, right, y);
	y += fonts->title.line_height + 2.0f;
	/* whose friend */
	text[0] = 0;
	if (self)
		snprintf(text, sizeof(text), "THIS IS YOU");
	else if (player->had)
	{
		snprintf(text, sizeof(text), "%s", !strcmp(player->relation, "friends") ? "YOUR FRIEND" :
			!strcmp(player->relation, "incoming") ? "WANTS TO BE YOUR FRIEND" :
			!strcmp(player->relation, "outgoing") ? "FRIEND REQUEST SENT" : "PLAYER");
		presence_state(&player->presence, other, sizeof(other));
		text_add(text, sizeof(text), " - %s%s%s", other, player->muted ? " - MUTED" : "",
			player->blocked ? " - BLOCKED" : "");
	}
	if (draw)
	{
		card_color(&color, 1.0f, 0.84f, 0.35f);
		campaign_score_panel_text(panel, text, NULL, head_left, right, y, 1.0f, _campaign_score_text_left, &color);
	}
	y += row;
	/* (what they are doing, its first line) */
	if (player->had && !self)
	{
		char *end;

		text[0] = 0;
		presence_line(&player->presence, text, sizeof(text));
		if ((end = strchr(text, '\n')) != NULL)
			*end = 0;
		if (draw)
		{
			card_color(&color, 0.6f, 0.66f, 0.75f);
			campaign_score_panel_text(panel, text, NULL, head_left, right, y, 1.0f, _campaign_score_text_left, &color);
		}
		y += row;
	}
	y += SOCIAL_CARD_GAP - 4.0f;
	if (draw)
		campaign_score_panel_quad(panel, left, y, right, y + 1.0f, CAMPAIGN_SCORE_CARD_RULE);
	y += SOCIAL_CARD_GAP;
	if (self || player->had)
	{
		y = social_extensions_card_draw_section(panel, draw, self, player->code, player->name, left, right, y, row);
		if (draw)
			campaign_score_panel_quad(panel, left, y, right, y + 1.0f, CAMPAIGN_SCORE_CARD_RULE);
		y += SOCIAL_CARD_GAP;
	}
	if (!player->had)
	{
		/* (asked for, or why not: what the server said, on a line) */
		snprintf(text, sizeof(text), "%s", !social_menu.player_failed ? "ASKING THE SERVER..." :
			social_menu.message[0] ? social_menu.message : "THE SERVER DID NOT ANSWER");
		for (index = 0; text[index]; index++)
		{
			if (text[index] == '\n')
				text[index] = ' ';
		}
		upper_text(text);
		if (draw)
		{
			card_color(&color, 0.6f, 0.66f, 0.75f);
			campaign_score_panel_text(panel, text, NULL, left, right, y + row, 1.0f, _campaign_score_text_center,
				&color);
		}
		return y + row * 3.0f + SOCIAL_CARD_PADDING;
	}

	/* (what they made, in place of all that follows) */
	if (card_interactive && social_menu.card_creator)
		return card_creator_layout(panel, draw, self, left, right, third, row, y) + SOCIAL_CARD_PADDING - 4.0f;

	/* the totals: multiplayer's, the campaign's and Firefight's */
	{
		struct social_menu_totals const *multiplayer = &player->multiplayer;
		struct social_menu_totals const *campaign = &player->campaign;
		struct social_menu_firefight firefight = player->firefight;
		real const second = left + third + SOCIAL_CARD_HALF_GAP;
		real const last = second + third + SOCIAL_CARD_HALF_GAP;
		char pairs[SOCIAL_CARD_TOTALS_ROWS][2][2][24];
		char duration[24];

		csmemset(pairs, 0, sizeof(pairs));
		card_pair_set(pairs[0][0], "MATCHES", "%ld", multiplayer->matches);
		card_pair_set(pairs[0][1], "WON", "%ld", multiplayer->wins);
		card_pair_set(pairs[1][0], "LOST", "%ld", multiplayer->losses);
		card_pair_set(pairs[1][1], "TIED", "%ld", multiplayer->ties);
		card_pair_set(pairs[2][0], "QUIT", "%ld", multiplayer->quits);
		kda_text(multiplayer->kills, multiplayer->deaths, multiplayer->assists, other, sizeof(other));
		card_pair_set(pairs[2][1], "KDA", "%s", other);
		card_pair_set(pairs[3][0], "KILLS", "%ld", multiplayer->kills);
		card_pair_set(pairs[3][1], "DEATHS", "%ld", multiplayer->deaths);
		card_pair_set(pairs[4][0], "ASSISTS", "%ld", multiplayer->assists);
		card_pair_set(pairs[4][1], "SUICIDES", "%ld", multiplayer->suicides);
		card_pair_set(pairs[5][0], "BETRAYALS", "%ld", multiplayer->betrayals);
		duration_text(multiplayer->seconds, duration, sizeof(duration));
		card_pair_set(pairs[5][1], "TIME", "%s", duration);
		card_totals(panel, draw, left, left + third, y, row, "MULTIPLAYER", pairs, 2, 1);

		csmemset(pairs, 0, sizeof(pairs));
		card_pair_set(pairs[0][0], "LEVELS WON", "%ld", campaign->matches);
		card_pair_set(pairs[0][1], "SCORE", "%ld", campaign->score);
		card_pair_set(pairs[1][0], "KILLS", "%ld", campaign->kills);
		card_pair_set(pairs[1][1], "DEATHS", "%ld", campaign->deaths);
		kda_text(campaign->kills, campaign->deaths, campaign->assists, other, sizeof(other));
		card_pair_set(pairs[2][0], "KDA", "%s", other);
		duration_text(campaign->seconds, duration, sizeof(duration));
		card_pair_set(pairs[2][1], "TIME", "%s", duration);
		/* (and the medals, and how long they have played here) */
		card_pair_set(pairs[4][0], "MEDALS", "%ld", multiplayer->medals + campaign->medals);
		ago_text(player->created_at, duration, sizeof(duration));
		card_pair_set(pairs[4][1], "JOINED", "%s", duration[0] ? duration : "-");
		card_totals(panel, draw, second, second + third, y, row, "CAMPAIGN", pairs, 0, 1);

		/* (one's own from the career, up to date offline too) */
		if (self)
		{
			struct firefight_career_stats career;

			if (firefight_stats_get_career(NONE, &career))
			{
				firefight.matches = career.ff_matches;
				firefight.sets = career.total_sets_completed;
				firefight.waves = career.total_waves_cleared;
				firefight.kills = career.ff_kills;
				firefight.boss_kills = career.total_boss_kills;
				firefight.deaths = career.ff_deaths;
				firefight.assists = career.ff_assists;
				firefight.best_spree = career.ff_best_spree;
				firefight.score = career.ff_score;
				firefight.seconds = career.ff_seconds;
			}
		}
		csmemset(pairs, 0, sizeof(pairs));
		card_pair_set(pairs[0][0], "MATCHES", "%ld", firefight.matches);
		card_pair_set(pairs[0][1], "SCORE", "%ld", firefight.score);
		card_pair_set(pairs[1][0], "SETS", "%ld", firefight.sets);
		card_pair_set(pairs[1][1], "WAVES", "%ld", firefight.waves);
		card_pair_set(pairs[2][0], "KILLS", "%ld", firefight.kills);
		card_pair_set(pairs[2][1], "DEATHS", "%ld", firefight.deaths);
		kda_text(firefight.kills, firefight.deaths, firefight.assists, other, sizeof(other));
		card_pair_set(pairs[3][0], "KDA", "%s", other);
		card_pair_set(pairs[3][1], "ASSISTS", "%ld", firefight.assists);
		card_pair_set(pairs[4][0], "BOSSES", "%ld", firefight.boss_kills);
		card_pair_set(pairs[4][1], "BEST SPREE", "%ld", firefight.best_spree);
		duration_text(firefight.seconds, duration, sizeof(duration));
		card_pair_set(pairs[5][0], "TIME", "%s", duration);
		card_totals(panel, draw, last, right, y, row, "FIREFIGHT", pairs, 0, 1);
		if (draw)
		{
			real const bottom = y + row * (SOCIAL_CARD_TOTALS_ROWS + 1);
			real const gap = SOCIAL_CARD_HALF_GAP / 2.0f;

			campaign_score_panel_quad(panel, second - gap, y, second - gap + 1.0f, bottom, CAMPAIGN_SCORE_CARD_RULE);
			campaign_score_panel_quad(panel, last - gap, y, last - gap + 1.0f, bottom, CAMPAIGN_SCORE_CARD_RULE);
		}
		y += row * (SOCIAL_CARD_TOTALS_ROWS + 1) + 2.0f;
	}
	y += SOCIAL_CARD_GAP - 4.0f;
	if (draw)
		campaign_score_panel_quad(panel, left, y, right, y + 1.0f, CAMPAIGN_SCORE_CARD_RULE);
	y += SOCIAL_CARD_GAP;

	/* every medal: those won with how many, the others dimmed */
	{
		long bitmap_index = campaign_medals_bitmap();
		short per_row = (short)MAX((right - left) / SOCIAL_CARD_MEDAL_SPACE, 1);
		real space = (right - left) / per_row;
		short kinds = 0;

		for (index = 0; index < NUMBER_OF_CAMPAIGN_MEDALS; index++)
			kinds += player->medal_counts[index] > 0;
		if (draw)
		{
			card_color(&color, 0.95f, 0.95f, 0.95f);
			campaign_score_panel_text(panel, "MEDALS", NULL, left, right, y, 1.0f, _campaign_score_text_left, &color);
			snprintf(text, sizeof(text), "%d OF %d", (int)kinds, (int)NUMBER_OF_CAMPAIGN_MEDALS);
			card_color(&color, 0.6f, 0.66f, 0.75f);
			campaign_score_panel_text(panel, text, NULL, left, right, y, 1.0f, _campaign_score_text_right, &color);
		}
		y += row + 2.0f;
		for (index = 0; index < NUMBER_OF_CAMPAIGN_MEDALS; index++)
		{
			real x = left + (index % per_row) * space;
			real top = y + (index / per_row) * (SOCIAL_CARD_MEDAL_SIZE + 4.0f);
			boolean won = player->medal_counts[index] > 0;
			struct bitmap_data *bitmap;

			if (!draw)
				continue;
			bitmap = bitmap_index != NONE ? bitmap_group_try_and_get_bitmap(bitmap_index, index) : NULL;
			if (bitmap)
			{
				rectangle2d bounds;

				bounds.x0 = (short)(panel->left + x * panel->scale);
				bounds.y0 = (short)(panel->top + top * panel->scale);
				bounds.x1 = (short)(panel->left + (x + SOCIAL_CARD_MEDAL_SIZE) * panel->scale);
				bounds.y1 = (short)(panel->top + (top + SOCIAL_CARD_MEDAL_SIZE) * panel->scale);
				draw_bitmap_in_rect(bitmap, &bounds, NULL, NULL, won ? 0xFFFFFFFF : 0x38FFFFFF, NULL, TRUE);
			}
			if (won)
			{
				snprintf(text, sizeof(text), "x%ld", player->medal_counts[index]);
				card_color(&color, 0.95f, 0.95f, 0.95f);
				campaign_score_panel_text(panel, text, NULL, x + SOCIAL_CARD_MEDAL_SIZE + 3.0f, x + space,
					top + (SOCIAL_CARD_MEDAL_SIZE - line) / 2.0f, 1.0f, _campaign_score_text_left, &color);
			}
		}
		y += ((NUMBER_OF_CAMPAIGN_MEDALS + per_row - 1) / per_row) * (SOCIAL_CARD_MEDAL_SIZE + 4.0f);
	}
	return y + SOCIAL_CARD_PADDING - 4.0f;
}

static boolean card_font(
	char const *name,
	struct campaign_score_panel *panel)
{
	struct font_header *font;

	/* (a map without the menus' fonts: the terminal's, as the chat's) */
	panel->font_index = tag_loaded('font', name);
	if (panel->font_index == NONE)
		panel->font_index = interface_get_tag_index(_interface_font_terminal);
	if (panel->font_index == NONE)
		return FALSE;
	font = font_definition_get(panel->font_index);
	panel->line_height = (short)(font->ascending_height + font->descending_height);
	return panel->line_height > 0;
}

/* the player's card in its room from x0 to x1, from y0 down, `unit` pixels
to one of the screen's 480 lines: its bottom (y0 for none drawn) */
static short card_draw(
	short x0,
	short y0,
	short x1,
	real unit,
	boolean interactive)
{
	struct social_card_fonts fonts;
	real room = (real)(x1 - x0);
	real scale;
	real height;

	card_interactive = interactive;
	if (x1 <= x0)
		return y0;
	if (!card_font("ui\\small_ui", &fonts.text) || !card_font("ui\\large_ui", &fonts.title))
		return y0;
	/* (the room's width, at least SOCIAL_CARD_WIDTH in its units, and
	SOCIAL_CARD_HEIGHT lines at most; a card taller than that is made
	smaller, and as much wider in its units, so that it still fills the
	room's width) */
	fonts.text.width = MAX(room / unit, SOCIAL_CARD_WIDTH);
	scale = room / fonts.text.width;
	fonts.text.scale = scale;
	height = card_layout(&fonts, FALSE);
	if (height * scale > SOCIAL_CARD_HEIGHT * unit)
	{
		scale = SOCIAL_CARD_HEIGHT * unit / height;
		fonts.text.scale = scale;
		fonts.text.width = room / scale;
		height = card_layout(&fonts, FALSE);
	}
	fonts.text.scale = fonts.title.scale = scale;
	fonts.text.left = fonts.title.left = x0;
	fonts.text.top = fonts.title.top = y0;
	fonts.title.width = fonts.text.width;
	campaign_score_panel_quad(&fonts.text, 0.0f, 0.0f, fonts.text.width, height, CAMPAIGN_SCORE_CARD_FILL);
	campaign_score_panel_frame(&fonts.text, 0.0f, 0.0f, fonts.text.width, height, CAMPAIGN_SCORE_PANEL_EDGE);
	card_layout(&fonts, TRUE);
	return (short)(y0 + height * scale);
}

void social_menu_card_draw(
	short x0,
	short y0,
	short x1,
	short y1)
{
	(void)y1;
	if (social_menu.open && social_menu.list == _list_card)
	{
		/* (its room is the margins of a wide screen too, either side of the
		menus' 640 columns) */
		short half = (short)(MIN(x1 - x0 + halo_screen_ui_offset_current() * 2, SOCIAL_MENU_CARD_MOST_WIDTH) / 2);
		short middle = (short)((x0 + x1) / 2);

		card_draw((short)(middle - half), y0, (short)(middle + half), 1.0f, TRUE);
	}
}

short social_menu_profile_card_draw(
	char const *code,
	short x0,
	short y0,
	short x1,
	real unit)
{
	if (strcmp(social_menu.player.code, code))
		return y0;
	return card_draw(x0, y0, x1, unit, FALSE);
}

boolean social_menu_card_shown(
	void)
{
	return social_menu.open && social_menu.list == _list_card;
}

boolean social_menu_card_creator_toggle(
	void)
{
	if (!social_menu_card_shown() || !social_menu.player.had)
		return FALSE;
	social_menu.card_creator = !social_menu.card_creator;
	return TRUE;
}

boolean social_menu_card_button_focused(
	void)
{
	return social_menu.card_button_focused && social_menu_card_shown() && social_menu.player.had;
}

boolean social_menu_card_button_focus(
	boolean focused)
{
	if (focused && (!social_menu_card_shown() || !social_menu.player.had))
		return FALSE;
	social_menu.card_button_focused = focused;
	return TRUE;
}

boolean social_menu_card_button_hit(
	short x,
	short y)
{
	rectangle2d const *button = &social_menu.card_button;

	return social_menu_card_shown() && social_menu.player.had && social_menu.card_button_drawn_at &&
		(long)(system_milliseconds() - social_menu.card_button_drawn_at) < 500 &&
		x >= button->x0 && x < button->x1 && y >= button->y0 && y < button->y1;
}

boolean social_menu_card_has_options(
	void)
{
	return social_menu_card_shown() && strcmp(social_menu.player.code, social_account_code());
}

void social_menu_card_options(
	void)
{
	if (social_menu_card_has_options())
		list_open(_list_player);
}

boolean social_menu_card_has_join(
	void)
{
	struct social_menu_presence const *presence = &social_menu.player.presence;

	return social_menu_card_has_options() && social_menu.player.had && presence->online &&
		(presence->invite[0] || presence->host_key[0]);
}

short social_menu_card_join(
	void)
{
	if (!social_menu_card_has_join())
		return _social_menu_stays;
	return presence_join(&social_menu.player.presence, social_menu.player.name);
}

/* ---------- the main menu's social column

drawn over its social_home text box (menu_functions.c, which keeps the
column's focus and the row chosen): your multiplayer totals, then your
friends (online first, as the server sorts them), a row each with its
presence's dot and state, scrolled to the row chosen; where each row was
drawn kept for a click. The friends are asked for again every
SOCIAL_MENU_REFRESH_MILLISECONDS while it is shown */

enum
{
	SOCIAL_HOME_PADDING = 6,
	SOCIAL_HOME_DOT = 5,
	SOCIAL_HOME_PLATFORM_ICON = 14,
	SOCIAL_HOME_ROWS = 16,
};

static struct
{
	/* the rows drawn: the first's friend, how many, and their tops; the
	row's height and the column's rectangle (the menus' 640) */
	short first;
	short count;
	short tops[SOCIAL_HOME_ROWS];
	short row_height;
	rectangle2d bounds;
} social_home;

short social_menu_home_rows(
	void)
{
	return social_enabled() && social_menu.friends_had && config_boolean("social.friends") ?
		social_menu.friend_count : 0;
}

void social_menu_home_friend_open(
	short row)
{
	if (row < 0 || row >= social_menu_home_rows())
		return;
	social_menu_player_open_next(social_menu.friends[row].code, social_menu.friends[row].name);
}

long social_menu_requests_waiting(
	void)
{
	long friends_online = 0;
	long requests = 0;

	if (!social_enabled() || !config_boolean("social.friends"))
		return 0;
	if (social_menu.friends_had)
		return social_menu.incoming_count;
	social_presence_counts(&friends_online, &requests);
	return requests;
}

void social_menu_player_open_next(
	char const *code,
	char const *name)
{
	social_menu.opening = _social_menu_open_player;
	snprintf(social_menu.opening_code, sizeof(social_menu.opening_code), "%s", code);
	snprintf(social_menu.opening_name, sizeof(social_menu.opening_name), "%s", name);
}

short social_menu_home_hit(
	short x,
	short y)
{
	short index;

	if (x < social_home.bounds.x0 || x >= social_home.bounds.x1)
		return NONE;
	for (index = 0; index < social_home.count; index++)
	{
		if (y >= social_home.tops[index] && y < social_home.tops[index] + social_home.row_height)
			return (short)(social_home.first + index);
	}
	return NONE;
}

boolean social_menu_home_inside(
	short x,
	short y)
{
	return x >= social_home.bounds.x0 && x < social_home.bounds.x1 && y >= social_home.bounds.y0 &&
		y < social_home.bounds.y1;
}

/* a presence's dot: green online (joinable), amber playing, grey offline */
static pixel32 home_dot_color(
	struct social_menu_presence const *presence)
{
	if (!presence->online)
		return 0xFF707070;
	if (!presence->invite[0] && !presence->host_key[0] && (!strcmp(presence->state, "multiplayer") || !strcmp(presence->state, "campaign") ||
		!strcmp(presence->state, "theater")))
	{
		return 0xFFFFB030;
	}
	return 0xFF50E060;
}

void social_menu_home_draw(
	short x0,
	short y0,
	short x1,
	short y1,
	boolean focused,
	short chosen)
{
	struct social_card_fonts fonts;
	struct campaign_score_panel const *panel = &fonts.text;
	unsigned long now = system_milliseconds();
	real_argb_color color;
	char text[96];
	char other[48];
	real left = SOCIAL_HOME_PADDING;
	real right;
	real y = SOCIAL_HOME_PADDING;
	real line;
	short rows;
	short room;
	short index;
	long matches;
	long kills;
	long deaths;
	long assists;

	social_home.bounds.x0 = x0;
	social_home.bounds.y0 = y0;
	social_home.bounds.x1 = x1;
	social_home.bounds.y1 = y1;
	social_home.count = 0;
	if (x1 <= x0 || y1 <= y0 || !card_font("ui\\small_ui", &fonts.text) || !card_font("ui\\large_ui", &fonts.title))
		return;
	/* (the friends again a while after, as the Social screen's lists ask) */
	if (social_enabled() && !social_menu.open &&
		(!social_menu.friends_at || now - social_menu.friends_at > SOCIAL_MENU_REFRESH_MILLISECONDS))
	{
		fetch(_fetch_friends);
	}
	fonts.text.scale = fonts.title.scale = 1.0f;
	fonts.text.left = fonts.title.left = x0;
	fonts.text.top = fonts.title.top = y0;
	fonts.text.width = fonts.title.width = (real)(x1 - x0);
	right = fonts.text.width - SOCIAL_HOME_PADDING;
	line = panel->line_height;
	campaign_score_panel_quad(panel, 0.0f, 0.0f, fonts.text.width, (real)(y1 - y0), CAMPAIGN_SCORE_CARD_FILL);
	campaign_score_panel_frame(panel, 0.0f, 0.0f, fonts.text.width, (real)(y1 - y0),
		focused && chosen < social_menu_home_rows() ? 0xFFA0D0FF : CAMPAIGN_SCORE_PANEL_EDGE);

	/* your rank, emblem & KDA on the left, and the requests waiting (else the friends online) on the right */
	{
		struct firefight_mcc_rank_info my_rk;
		struct firefight_emblem_info my_emb;
		short hdr_icon_sz = (short)MAX(12, (short)line + 1);
		short hdr_x = (short)(x0 + left);
		short hdr_y = (short)(y0 + y + (line - hdr_icon_sz) / 2.0f);
		real hdr_text_left = left + (real)(hdr_icon_sz * 2 + 7);

		firefight_stats_get_mcc_rank(NONE, &my_rk);
		firefight_stats_get_emblem(NONE, &my_emb);
		firefight_draw_mcc_rank_icon(hdr_x, hdr_y, hdr_icon_sz, my_rk.tour_number, my_rk.rank_in_tour, 1.0f);
		firefight_draw_player_emblem((short)(hdr_x + hdr_icon_sz + 3), hdr_y, hdr_icon_sz, &my_emb, 1.0f);

		if (social_account_totals(&matches, &kills, &deaths, &assists))
		{
			kda_text(kills, deaths, assists, other, sizeof(other));
			snprintf(text, sizeof(text), "KDA %s", other);
		}
		else
		{
			struct firefight_career_stats career;
			if (firefight_stats_get_career(NONE, &career))
			{
				kda_text(career.total_kills, career.total_deaths, career.total_assists, other, sizeof(other));
				snprintf(text, sizeof(text), "KDA %s", other);
			}
			else
			{
				snprintf(text, sizeof(text), "KDA 0.00");
			}
		}
		card_color(&color, 0.95f, 0.95f, 0.95f);
		campaign_score_panel_text(panel, text, NULL, hdr_text_left, right, y, 1.0f, _campaign_score_text_left, &color);
	}
	other[0] = 0;
	if (social_menu.friends_had && social_menu.incoming_count)
	{
		snprintf(other, sizeof(other), "%d REQUEST%s", (int)social_menu.incoming_count,
			social_menu.incoming_count == 1 ? "" : "S");
		card_color(&color, 1.0f, 0.84f, 0.35f);
	}
	else if (social_menu.friends_had)
	{
		long online = 0;

		for (index = 0; index < social_menu.friend_count; index++)
			online += social_menu.friends[index].presence.online;
		snprintf(other, sizeof(other), "%ld ONLINE", online);
		card_color(&color, 0.55f, 0.72f, 1.0f);
	}
	campaign_score_panel_text(panel, other, NULL, left, right, y, 1.0f, _campaign_score_text_right, &color);
	y += line + 3.0f;
	campaign_score_panel_quad(panel, left, y, right, y + 1.0f, CAMPAIGN_SCORE_CARD_RULE);
	y += 4.0f;

	/* (no friends to show: why) */
	rows = social_menu_home_rows();
	if (!rows)
	{
		char const *first = !social_enabled() ? "SOCIAL IS OFF" : !social_account_code()[0] ? "SIGNING IN..." :
			!config_boolean("social.friends") ? "YOUR FRIEND LIST IS OFF" :
			!social_menu.friends_had ? (social_menu.friends_failed ? "THE SERVER DID NOT ANSWER" : "ASKING THE SERVER...") :
			"NO FRIENDS YET";
		char const *second = !social_enabled() ? "Y: Social, then SETTINGS" :
			social_menu.friends_had && config_boolean("social.friends") ?
			"Y: Social, then ADD FRIEND" : "";

		card_color(&color, 0.6f, 0.66f, 0.75f);
		campaign_score_panel_text(panel, first, NULL, left, right, y + line, 1.0f, _campaign_score_text_center, &color);
		campaign_score_panel_text(panel, second, NULL, left, right, y + line * 2.0f + 2.0f, 1.0f,
			_campaign_score_text_center, &color);
		return;
	}

	/* the friends, scrolled to the row chosen */
	social_home.row_height = (short)(line + 3.0f);
	room = (short)MIN(MAX(((y1 - y0) - (short)y - SOCIAL_HOME_PADDING) / social_home.row_height, 1), SOCIAL_HOME_ROWS);
	if (focused && chosen < rows)
	{
		if (chosen < social_home.first)
			social_home.first = chosen;
		else if (chosen >= social_home.first + room)
			social_home.first = (short)(chosen - room + 1);
	}
	social_home.first = (short)PIN(social_home.first, 0, MAX(rows - room, 0));
	for (index = social_home.first; index < rows && social_home.count < room; index++)
	{
		struct social_menu_person const *friend = &social_menu.friends[index];
		real top = y + social_home.count * social_home.row_height;
		short platform = friend->presence.online ? platform_icon_for_name(friend->presence.platform) : _platform_icon_none;
		struct firefight_mcc_rank_info f_rk;
		struct firefight_emblem_info f_emb;
		short f_icon_sz = (short)MAX(11, (short)line);
		real icon_left = left + SOCIAL_HOME_DOT + 4.0f;
		short f_x = (short)(x0 + icon_left);
		short f_y = (short)(y0 + top + (line - f_icon_sz) / 2.0f);
		real name_left = icon_left + (real)(f_icon_sz * 2 + 6);

		social_home.tops[social_home.count++] = (short)(y0 + top);
		if (focused && index == chosen)
			campaign_score_panel_quad(panel, left - 2.0f, top - 1.0f, right + 2.0f, top + line + 2.0f, 0x802896FF);
		campaign_score_panel_quad(panel, left, top + (line - SOCIAL_HOME_DOT) / 2.0f, left + SOCIAL_HOME_DOT,
			top + (line + SOCIAL_HOME_DOT) / 2.0f, home_dot_color(&friend->presence));
		social_extensions_get_person_rank_and_emblem(friend->code, friend->name, index, &f_rk, &f_emb);
		firefight_draw_mcc_rank_icon(f_x, f_y, f_icon_sz, f_rk.tour_number, f_rk.rank_in_tour, 1.0f);
		firefight_draw_player_emblem((short)(f_x + f_icon_sz + 2), f_y, f_icon_sz, &f_emb, 1.0f);
		if (platform != _platform_icon_none)
		{
			rectangle2d icon;

			icon.x0 = (short)(x0 + name_left);
			icon.y0 = (short)(y0 + top + (line - SOCIAL_HOME_PLATFORM_ICON) / 2.0f);
			icon.x1 = (short)(icon.x0 + SOCIAL_HOME_PLATFORM_ICON);
			icon.y1 = (short)(icon.y0 + SOCIAL_HOME_PLATFORM_ICON);
			platform_icon_draw(platform, &icon, 0xFFFFFFFF);
			name_left += SOCIAL_HOME_PLATFORM_ICON + 4.0f;
		}
		if (friend->presence.online)
			card_color(&color, 0.95f, 0.95f, 0.95f);
		else
			card_color(&color, 0.6f, 0.6f, 0.6f);
		campaign_score_panel_text(panel, friend->name, NULL, name_left, right, top, 1.0f,
			_campaign_score_text_left, &color);
		presence_state(&friend->presence, other, sizeof(other));
		card_color(&color, 0.55f, 0.72f, 1.0f);
		campaign_score_panel_text(panel, other, NULL, left, right, top, 1.0f, _campaign_score_text_right, &color);
	}
}

short social_menu_row_platform(
	short row)
{
	struct social_menu_row const *entry = row_get(row);
	struct social_menu_presence const *presence;

	if (!entry || entry->kind != _row_friend)
		return _platform_icon_none;
	presence = &social_menu.friends[entry->value].presence;
	return presence->online ? platform_icon_for_name(presence->platform) : _platform_icon_none;
}

boolean social_menu_row_emblem(
	short row,
	struct firefight_emblem_info *out_emblem,
	short *out_part)
{
	struct social_menu_row const *entry = row_get(row);

	return entry && social_extensions_row_emblem(social_menu.list, entry->kind, entry->value, out_emblem, out_part);
}

boolean social_menu_row_person_icons(
	short row,
	struct firefight_mcc_rank_info *out_rank,
	struct firefight_emblem_info *out_emblem)
{
	struct social_menu_row const *entry = row_get(row);

	if (!entry)
		return FALSE;
	if (entry->kind == _row_friend && entry->value >= 0 && entry->value < social_menu.friend_count)
	{
		social_extensions_get_person_rank_and_emblem(
			social_menu.friends[entry->value].code,
			social_menu.friends[entry->value].name,
			entry->value,
			out_rank,
			out_emblem);
		return TRUE;
	}
	if (entry->kind == _row_incoming && entry->value >= 0 && entry->value < social_menu.incoming_count)
	{
		social_extensions_get_person_rank_and_emblem(
			social_menu.incoming[entry->value].code,
			social_menu.incoming[entry->value].name,
			entry->value,
			out_rank,
			out_emblem);
		return TRUE;
	}
	if (entry->kind == _row_outgoing && entry->value >= 0 && entry->value < social_menu.outgoing_count)
	{
		social_extensions_get_person_rank_and_emblem(
			social_menu.outgoing[entry->value].code,
			social_menu.outgoing[entry->value].name,
			entry->value,
			out_rank,
			out_emblem);
		return TRUE;
	}
	if (entry->kind == _row_recent_player && entry->value >= 0 && entry->value < social_menu.recent_count)
	{
		char name[SOCIAL_MENU_NAME_SIZE];
		utf8_from_wide(social_menu.recent_names[entry->value], name, sizeof(name));
		social_extensions_get_person_rank_and_emblem(
			social_menu.recent_codes[entry->value],
			name,
			entry->value,
			out_rank,
			out_emblem);
		return TRUE;
	}
	return FALSE;
}

void social_menu_row(
	short row,
	char *name,
	long name_size,
	char *state,
	long state_size)
{
	struct social_menu_row const *entry = row_get(row);
	struct social_menu_player const *player = &social_menu.player;
	long friends_online = 0;
	long requests = 0;
	short index;

	name[0] = 0;
	state[0] = 0;
	if (!entry)
		return;
	social_presence_counts(&friends_online, &requests);
	if (social_menu.friends_had)
	{
		friends_online = 0;
		for (index = 0; index < social_menu.friend_count; index++)
			friends_online += social_menu.friends[index].presence.online;
		requests = social_menu.incoming_count;
	}
	switch (entry->kind)
	{
	case _row_note:
		snprintf(name, (size_t)name_size, "%s", entry->value == _note_loading ? "ASKING THE SERVER..." :
			entry->value == _note_offline ? "OFFLINE" : entry->value == _note_failed ? "NOT ANSWERED" : "NONE YET");
		break;
	case _row_my_account:
		snprintf(name, (size_t)name_size, "MY ACCOUNT");
		break;
	case _row_friendlist_menu:
		snprintf(name, (size_t)name_size, "FRIENDLIST");
		break;
	case _row_activity_menu:
		snprintf(name, (size_t)name_size, "ACTIVITY");
		break;
	case _row_settings_menu:
		snprintf(name, (size_t)name_size, "SETTINGS");
		break;
	case _row_you:
		snprintf(name, (size_t)name_size, "ACCOUNT SUMMARY");
		social_account_name(state, state_size);
		break;
	case _row_name:
		snprintf(name, (size_t)name_size, "CHAT NAME");
		social_account_name(state, state_size);
		break;
	case _row_name_color:
		snprintf(name, (size_t)name_size, "CHAT COLOR");
		snprintf(state, (size_t)state_size, "%s", social_menu_name_colors[name_color_index()].label);
		break;
	case _row_setup_name:
		snprintf(name, (size_t)name_size, "NAME");
		snprintf(state, (size_t)state_size, "%s", social_setup.name[0] ? social_setup.name : "A: TYPE IT");
		break;
	case _row_setup_color:
		snprintf(name, (size_t)name_size, "CHAT COLOR");
		snprintf(state, (size_t)state_size, "%s", social_menu_name_colors[social_setup.color].label);
		break;
	case _row_setup_done:
		snprintf(name, (size_t)name_size, "DONE");
		break;
	case _row_clear_rank:
		snprintf(name, (size_t)name_size, "CLEAR RANK");
		snprintf(state, (size_t)state_size, "TESTING");
		break;
	case _row_code:
		snprintf(name, (size_t)name_size, "YOUR CODE");
		snprintf(state, (size_t)state_size, "%s", own_code());
		break;
	case _row_sync:
		snprintf(name, (size_t)name_size, "SYNC ACCOUNT");
		if (social_sync_active())
			snprintf(state, (size_t)state_size, "SYNCED");
		break;
	case _row_sync_create:
		snprintf(name, (size_t)name_size, "CREATE CODE");
		break;
	case _row_sync_input:
		snprintf(name, (size_t)name_size, "INPUT CODE");
		break;
	case _row_sync_code:
		snprintf(name, (size_t)name_size, "SYNC CODE");
		if (!social_menu_online())
			snprintf(state, (size_t)state_size, "OFFLINE");
		else
			snprintf(state, (size_t)state_size, "%s", social_menu.sync_code[0] ? social_menu.sync_code :
				social_menu.sync_status && social_menu.sync_status != 200 ? "NOT GIVEN" : "ASKING...");
		break;
	case _row_settings:
		snprintf(name, (size_t)name_size, "SETTINGS");
		if (!social_enabled())
			snprintf(state, (size_t)state_size, "SOCIAL OFF");
		break;
	case _row_friends:
		snprintf(name, (size_t)name_size, "FRIENDS");
		if (!config_boolean("social.friends"))
			snprintf(state, (size_t)state_size, "OFF");
		else
			snprintf(state, (size_t)state_size, "%ld ONLINE", friends_online);
		break;
	case _row_requests:
		snprintf(name, (size_t)name_size, "FRIEND REQUESTS");
		if (requests)
			snprintf(state, (size_t)state_size, "%ld NEW", requests);
		break;
	case _row_activity:
		snprintf(name, (size_t)name_size, "MATCH HISTORY");
		break;
	case _row_recent:
		snprintf(name, (size_t)name_size, "RECENT PLAYERS");
		break;
	case _row_add_friend:
		snprintf(name, (size_t)name_size, "ADD FRIEND");
		break;
	case _row_moderation:
		snprintf(name, (size_t)name_size, "BLOCKED & MUTED");
		if (social_menu.moderation_had && social_menu.moderated_count)
			snprintf(state, (size_t)state_size, "%d", (int)social_menu.moderated_count);
		break;
	case _row_friend:
		snprintf(name, (size_t)name_size, "%s", social_menu.friends[entry->value].name);
		/* (its platform is the icon beside it: social_menu_row_platform) */
		presence_state_plain(&social_menu.friends[entry->value].presence, state, state_size);
		break;
	case _row_friend_join:
		{
			struct social_menu_presence const *presence = &social_menu.friends[entry->value].presence;

			snprintf(name, (size_t)name_size, "   JOIN GAME");
			join_state(presence, state, state_size);
		}
		break;
	case _row_incoming:
		snprintf(name, (size_t)name_size, "%s", social_menu.incoming[entry->value].name);
		snprintf(state, (size_t)state_size, "ASKED YOU");
		break;
	case _row_outgoing:
		snprintf(name, (size_t)name_size, "%s", social_menu.outgoing[entry->value].name);
		snprintf(state, (size_t)state_size, "SENT");
		break;
	case _row_activity_entry:
		match_name(&social_menu.activity[entry->value], TRUE, name, name_size);
		ago_text(social_menu.activity[entry->value].played_at, state, state_size);
		break;
	case _row_recent_player:
		utf8_from_wide(social_menu.recent_names[entry->value], name, name_size);
		break;
	case _row_matches:
		snprintf(name, (size_t)name_size, "LATEST MATCHES");
		if (player->had)
			snprintf(state, (size_t)state_size, "%d", (int)player->match_count);
		break;
	case _row_join:
		snprintf(name, (size_t)name_size, "JOIN GAME");
		join_state(&player->presence, state, state_size);
		break;
	case _row_message:
		snprintf(name, (size_t)name_size, "SEND MESSAGE");
		break;
	case _row_friend_add:
		snprintf(name, (size_t)name_size, "ADD FRIEND");
		if (!player->requests)
			snprintf(state, (size_t)state_size, "CODE ONLY");
		break;
	case _row_friend_accept:
		snprintf(name, (size_t)name_size, "ACCEPT REQUEST");
		break;
	case _row_friend_decline:
		snprintf(name, (size_t)name_size, "DECLINE REQUEST");
		break;
	case _row_friend_cancel:
		snprintf(name, (size_t)name_size, "TAKE BACK REQUEST");
		break;
	case _row_friend_remove:
		snprintf(name, (size_t)name_size, "REMOVE FRIEND");
		break;
	case _row_mute:
		snprintf(name, (size_t)name_size, "MUTE");
		break;
	case _row_unmute:
		snprintf(name, (size_t)name_size, "UNMUTE");
		snprintf(state, (size_t)state_size, "MUTED");
		break;
	case _row_block:
		snprintf(name, (size_t)name_size, "BLOCK");
		break;
	case _row_unblock:
		snprintf(name, (size_t)name_size, "UNBLOCK");
		snprintf(state, (size_t)state_size, "BLOCKED");
		break;
	case _row_report:
		snprintf(name, (size_t)name_size, "REPORT");
		break;
	case _row_match:
		match_name(&player->matches[entry->value], FALSE, name, name_size);
		if (!strcmp(player->matches[entry->value].kind, "campaign"))
			snprintf(state, (size_t)state_size, "%ld PTS", player->matches[entry->value].score);
		else
			snprintf(state, (size_t)state_size, "%ld-%ld", player->matches[entry->value].kills,
				player->matches[entry->value].deaths);
		break;
	case _row_moderated:
		snprintf(name, (size_t)name_size, "%s", social_menu.moderated[entry->value].name);
		snprintf(state, (size_t)state_size, "%s", social_menu.moderated[entry->value].blocked ?
			(social_menu.moderated[entry->value].muted ? "BLOCKED, MUTED" : "BLOCKED") : "MUTED");
		break;
	case _row_reason:
		snprintf(name, (size_t)name_size, "%s", social_menu_reasons[entry->value]);
		if (social_menu.confirming_kind == _row_reason && social_menu.chosen == row)
			snprintf(state, (size_t)state_size, "A AGAIN");
		break;
	default:
		if (social_extensions_is_ext_row(entry->kind))
			social_extensions_row_text(social_menu.list, entry->kind, entry->value, name, name_size, state, state_size);
		break;
	}
	if (entry->kind == social_menu.confirming_kind && entry->kind != _row_reason)
		snprintf(state, (size_t)state_size, "A AGAIN");
}

/* the player's head of the details: its name, code and what it is doing */
static void player_head(
	char *title,
	long title_size,
	char *text,
	long text_size)
{
	struct social_menu_player const *player = &social_menu.player;

	snprintf(title, (size_t)title_size, "%s", player->name);
	/* (one's own code alone: another's is theirs to give) */
	if (!strcmp(player->code, social_account_code()))
		text_add(text, text_size, "Code: %s\n", own_code());
	if (social_menu.player_failed && !player->had)
	{
		text_add(text, text_size, "The server did not answer.\n");
		return;
	}
	if (!player->had)
	{
		text_add(text, text_size, "Asking the server...\n");
		return;
	}
	if (!strcmp(player->relation, "self"))
		text_add(text, text_size, "This is you: give your code to\nfriends to be added.\n");
	else
	{
		if (!strcmp(player->relation, "friends"))
			text_add(text, text_size, "Your friend.\n");
		else if (!strcmp(player->relation, "incoming"))
			text_add(text, text_size, "Wants to be your friend.\n");
		else if (!strcmp(player->relation, "outgoing"))
			text_add(text, text_size, "You asked to be friends.\n");
		presence_line(&player->presence, text, text_size);
		if (player->muted || player->blocked)
			text_add(text, text_size, "%s%s%s.\n", player->muted ? "Muted" : "", player->muted && player->blocked ?
				", " : "", player->blocked ? (player->muted ? "blocked" : "Blocked") : "");
	}
}

void social_menu_details(
	short row,
	char *title,
	long title_size,
	char *text,
	long text_size)
{
	struct social_menu_row const *entry = row_get(row);
	struct social_menu_player const *player = &social_menu.player;

	title[0] = 0;
	text[0] = 0;
	if (social_menu.message[0])
		text_add(text, text_size, "%s\n\n", social_menu.message);
	if (entry && social_extensions_is_ext_row(entry->kind))
	{
		social_extensions_row_details(social_menu.list, entry->kind, entry->value, title, title_size, text, text_size);
		return;
	}
	if (entry && entry->kind == _row_my_account)
	{
		snprintf(title, (size_t)title_size, "MY ACCOUNT");
		text_add(text, text_size, "Your account summary, emblem\ncustomisation, and device sync.");
		return;
	}
	if (!social_enabled())
	{
		snprintf(title, (size_t)title_size, "SOCIAL");
		text_add(text, text_size, !community_net_enabled() ? "Offline. Turn on Settings > Gameplay >\nCommunity's "
			"ONLINE, then SOCIAL\nin SETTINGS here, to see your\nfriends." : "Social is off. Turn on SOCIAL in\nSETTINGS "
			"to see your friends and\nthe fork's chat.");
		return;
	}
	if (!social_account_code()[0])
	{
		snprintf(title, (size_t)title_size, "SOCIAL");
		text_add(text, text_size, "Signing in to the community server...");
		return;
	}
	if (!entry)
		return;
	switch (entry->kind)
	{
	case _row_note:
		snprintf(title, (size_t)title_size, "SOCIAL");
		text_add(text, text_size, entry->value == _note_loading ? "Asking the server..." :
			entry->value == _note_failed ? "The server did not answer: it is\nasked again in a while." :
			entry->value == _note_offline ? "The community server is not reached." :
			social_menu.list == _list_activity ? "Your friends' matches and levels\nshow here (those who show\ntheir activity)." :
			social_menu.list == _list_recent ? "The players you meet in the chat\n(in a game, or who wrote to you)\nshow here." :
			social_menu.list == _list_moderation ? "No one is muted or blocked.\nA player's MUTE and BLOCK are\nin their page." :
			"None yet.");
		break;
	case _row_my_account:
		snprintf(title, (size_t)title_size, "MY ACCOUNT");
		text_add(text, text_size, "Your account summary and the code\nto sync it to another device.");
		break;
	case _row_friendlist_menu:
		snprintf(title, (size_t)title_size, "FRIENDLIST");
		text_add(text, text_size, "Your code, friend requests, friends,\nand the players you muted or\nblocked.");
		break;
	case _row_activity_menu:
		snprintf(title, (size_t)title_size, "ACTIVITY");
		text_add(text, text_size, "Players you recently met and your\nfriends' match history.");
		break;
	case _row_settings_menu:
		snprintf(title, (size_t)title_size, "SETTINGS");
		text_add(text, text_size, "Social, Record Matches, Chat, and\nFriends settings.");
		break;
	case _row_you:
		{
			char me[SOCIAL_MENU_NAME_SIZE];

			social_account_name(me, sizeof(me));
			snprintf(title, (size_t)title_size, "%s", me[0] ? me : "YOU");
			text_add(text, text_size, "Your code: %s\n\nFriends add you by it. A shows\nyour records: totals, matches\nand "
				"medals.", own_code());
		}
		break;
	case _row_name:
		snprintf(title, (size_t)title_size, "CHAT NAME");
		text_add(text, text_size, "The name the chat shows for you,\nthe same on every device and\nplatform of this account. "
			"A types\na new one (up to 20 characters);\nuntil you do, your profile's name is\nused.");
		break;
	case _row_name_color:
		snprintf(title, (size_t)title_size, "CHAT COLOR");
		text_add(text, text_size, "The colour of your name in the\nchat, for everyone who sees it.\nA picks the next one.");
		break;
	case _row_setup_name:
		snprintf(title, (size_t)title_size, "WELCOME");
		text_add(text, text_size, "Choose how other players see you.\n\nYour name, up to 11 characters,\nis your profile's and the chat's.\nA types it.");
		break;
	case _row_setup_color:
		snprintf(title, (size_t)title_size, "CHAT COLOR");
		text_add(text, text_size, "The colour of your name in the\nchat, for everyone who sees it.\nA or Left and Right change it.");
		break;
	case _row_setup_done:
		snprintf(title, (size_t)title_size, "DONE");
		text_add(text, text_size, social_menu_has_profile() ? "Saves these on your account." :
			"Saves these on your account and\nmakes your profile, ready to\nplay.");
		break;
	case _row_clear_rank:
		snprintf(title, (size_t)title_size, "CLEAR RANK");
		text_add(text, text_size, "For testing: A twice takes your\nrank back to nothing, here and on\nthe server. "
			"Your records are then\ncounted again at the main menu,\nas a new account's are.");
		break;
	case _row_code:
		snprintf(title, (size_t)title_size, "YOUR CODE");
		text_add(text, text_size, "%s\n\nGive it to friends: they add you\nby it. ", own_code());
		text_add(text, text_size, config_boolean("social.friend_requests") ? "Friend invites are open to\nanyone, so "
			"it is shown. A twice draws a new\ncode; the old one stops working." :
			own_code_hidden() ? "Hidden: A shows it. A twice draws\na new code; the old one stops\nworking." :
			"Only those with it can ask to\nbe your friend. A twice draws a\nnew code; the old one stops\nworking.");
		break;
	case _row_sync:
		snprintf(title, (size_t)title_size, "SYNC ACCOUNT");
		text_add(text, text_size, "Use this account on another\ndevice: its friends, records,\nmedals, leaderboard "
			"scores and\nskulls. Profiles, controls and\ncampaign progress stay on each\ndevice.%s", social_sync_active() ?
			"\n\nThis account is synced." : "");
		break;
	case _row_sync_create:
		snprintf(title, (size_t)title_size, "CREATE CODE");
		text_add(text, text_size, "A code for your other device's\nINPUT CODE. It works while its\nscreen is open and you are "
			"online.");
		break;
	case _row_sync_input:
		snprintf(title, (size_t)title_size, "INPUT CODE");
		text_add(text, text_size, "Type the code another device\nshows, to bring this device into\nthat account. This "
			"device's own\nfriends and records are added to\nit. It cannot be undone.");
		break;
	case _row_sync_code:
		snprintf(title, (size_t)title_size, "SYNC CODE");
		if (social_menu.sync_joined)
			text_add(text, text_size, "A device joined this account.\nYour skulls are shared now.");
		else
			text_add(text, text_size, "%s\n\nType it on the other device (SYNC\nACCOUNT, INPUT CODE). It stops "
				"working when you leave this screen or go offline.", social_menu.sync_code[0] ? social_menu.sync_code :
				"...");
		break;
	case _row_settings:
		snprintf(title, (size_t)title_size, "SETTINGS");
		text_add(text, text_size, "Social on or off, your records,\nthe fork's chat, and your friend\nlist: what others "
			"see of you,\nand who you hear.");
		break;
	case _row_friends:
		snprintf(title, (size_t)title_size, "FRIENDS");
		if (!config_boolean("social.friends"))
			text_add(text, text_size, "Your friend list is off: SETTINGS >\nFRIENDS here.\n\n");
		text_add(text, text_size, social_menu.friends_had ? "%d friends.\nWho is online, what they play,\nand joining "
			"their games." : "Your friends: who is online, what\nthey play, and joining their games.",
			(int)social_menu.friend_count);
		break;
	case _row_requests:
		snprintf(title, (size_t)title_size, "FRIEND REQUESTS");
		text_add(text, text_size, "Players who asked to be your\nfriend (%d), and those you asked (%d).",
			(int)social_menu.incoming_count, (int)social_menu.outgoing_count);
		break;
	case _row_activity:
		snprintf(title, (size_t)title_size, "MATCH HISTORY");
		text_add(text, text_size, "Your friends' latest matches and\nlevels.");
		break;
	case _row_recent:
		snprintf(title, (size_t)title_size, "RECENT PLAYERS");
		text_add(text, text_size, "The players of the chat: those in\nyour game, and those who wrote to\nyou or you "
			"wrote to.");
		break;
	case _row_add_friend:
		snprintf(title, (size_t)title_size, "ADD FRIEND");
		text_add(text, text_size, "Type a player's code (XXXX-XXXX,\nthe dash can be left out): they\nbecome your "
			"friend once they\naccept.\n\nYours is %s.", own_code());
		break;
	case _row_moderation:
		snprintf(title, (size_t)title_size, "BLOCKED & MUTED");
		text_add(text, text_size, "The players you muted (their lines\nhidden) and blocked (neither of you\nhears the "
			"other, no requests).");
		break;
	case _row_friend:
	case _row_incoming:
	case _row_outgoing:
	case _row_moderated:
		{
			struct social_menu_person const *person =
				entry->kind == _row_friend ? &social_menu.friends[entry->value] :
				entry->kind == _row_incoming ? &social_menu.incoming[entry->value] :
				entry->kind == _row_outgoing ? &social_menu.outgoing[entry->value] :
				&social_menu.moderated[entry->value];
			char ago[SOCIAL_MENU_TIME_SIZE];

			snprintf(title, (size_t)title_size, "%s", person->name);
			if (entry->kind == _row_friend)
				presence_line(&person->presence, text, text_size);
			ago_text(person->at, ago, sizeof(ago));
			if (entry->kind == _row_incoming)
				text_add(text, text_size, "Asked to be your friend %s.\n", ago);
			else if (entry->kind == _row_outgoing)
				text_add(text, text_size, "You asked %s.\n", ago);
			text_add(text, text_size, "\nA: their page.");
		}
		break;
	case _row_friend_join:
		snprintf(title, (size_t)title_size, "%s", social_menu.friends[entry->value].name);
		presence_line(&social_menu.friends[entry->value].presence, text, text_size);
		join_details(&social_menu.friends[entry->value].presence, text, text_size);
		break;
	case _row_activity_entry:
		snprintf(title, (size_t)title_size, "%s", social_menu.activity[entry->value].player);
		match_details(&social_menu.activity[entry->value], text, text_size);
		text_add(text, text_size, "\nA: their page.");
		break;
	case _row_recent_player:
		utf8_from_wide(social_menu.recent_names[entry->value], title, title_size);
		text_add(text, text_size, "A: their page.");
		break;
	case _row_matches:
		player_head(title, title_size, text, text_size);
		if (player->had && !player->match_count &&
			strcmp(player->relation, "self"))
		{
			text_add(text, text_size, "\nTheir matches are not shown\n(their activity is hidden), or they\nhave "
				"none yet.");
		}
		break;
	case _row_join:
		player_head(title, title_size, text, text_size);
		join_details(&player->presence, text, text_size);
		break;
	case _row_message:
		player_head(title, title_size, text, text_size);
		text_add(text, text_size, "\nOpens the chat to write to them\nalone.");
		break;
	case _row_friend_add:
		player_head(title, title_size, text, text_size);
		text_add(text, text_size, player->requests ? "\nAsks them to be your friend." :
			"\nThey take friend requests by their\ncode alone: ask them for it.");
		break;
	case _row_friend_accept:
	case _row_friend_decline:
	case _row_friend_cancel:
	case _row_friend_remove:
		player_head(title, title_size, text, text_size);
		break;
	case _row_mute:
	case _row_unmute:
		player_head(title, title_size, text, text_size);
		text_add(text, text_size, "\nA muted player's lines are hidden\nfrom you, in the fork's chat and\n(by their "
			"name) the game's.");
		break;
	case _row_block:
	case _row_unblock:
		player_head(title, title_size, text, text_size);
		text_add(text, text_size, "\nBlocked, neither of you hears the\nother, no private lines or friend\nrequests "
			"either way; no longer\nfriends.");
		break;
	case _row_report:
		player_head(title, title_size, text, text_size);
		text_add(text, text_size, "\nReports them, and mutes them.\nPlayers reported by many are\nhidden from "
			"everyone's chat.");
		break;
	case _row_match:
		snprintf(title, (size_t)title_size, "%s", player->name);
		match_details(&player->matches[entry->value], text, text_size);
		break;
	case _row_reason:
		snprintf(title, (size_t)title_size, "REPORT %s", player->name);
		text_add(text, text_size, "Why: %s.\n\nPress A twice to report them.\nThey are muted for you too.",
			social_menu_reasons[entry->value]);
		break;
	}
}

/* ---------- choosing */

/* an action that wants A twice: TRUE the second time */
static boolean confirmed(
	short kind,
	char const *what)
{
	if (social_menu.confirming_kind == kind &&
		system_milliseconds() - social_menu.confirming_time <= SOCIAL_MENU_CONFIRM_MILLISECONDS)
	{
		social_menu.confirming_kind = NONE;
		return TRUE;
	}
	social_menu.confirming_kind = kind;
	social_menu.confirming_time = system_milliseconds();
	message_set("Press A again to %s.", what);
	return FALSE;
}

static void moderate(
	short kind,
	char const *action,
	char const *reason)
{
	act(kind, "/moderation", social_menu.player.code, social_menu.player.name, action, reason);
}

short social_menu_choose(
	short row)
{
	struct social_menu_row const *entry = row_get(row);
	struct social_menu_player *player = &social_menu.player;
	char what[SOCIAL_MENU_NAME_SIZE + 32];

	if (!entry)
		return _social_menu_stays;
	if (entry->kind != social_menu.confirming_kind)
		social_menu.confirming_kind = NONE;
	switch (entry->kind)
	{
	case _row_my_account:
		list_open(_list_account);
		break;
	case _row_friendlist_menu:
		list_open(_list_friendlist);
		break;
	case _row_activity_menu:
		list_open(_list_activity_menu);
		break;
	case _row_settings_menu:
		return _social_menu_settings;
	case _row_you:
		player_open(social_account_code(), NULL);
		break;
	case _row_name:
		keyboard_open(_typing_name);
		break;
	case _row_name_color:
		name_send(NULL, social_menu_name_colors[(name_color_index() + 1) % NUMBEROF(social_menu_name_colors)].hex);
		break;
	case _row_setup_name:
		keyboard_open(_typing_setup_name);
		break;
	case _row_setup_color:
		social_setup.color = (short)((social_setup.color + 1) % NUMBEROF(social_menu_name_colors));
		break;
	case _row_setup_done:
		social_setup_done();
		break;
	case _row_clear_rank:
		if (confirmed(_row_clear_rank, "clear your rank"))
			rank_clear();
		break;
	case _row_code:
		if (!config_boolean("social.friend_requests"))
			social_menu.code_shown = TRUE;
		if (!social_menu_online())
			message_set("The community server is not reached.");
		else
		{
			if (confirmed(_row_code, "draw a new code"))
				act(_row_code, "/account/code", "", NULL, NULL, NULL);
		}
		break;
	case _row_sync:
		list_open(_list_sync);
		break;
	case _row_sync_create:
		if (!social_menu_online())
			message_set("The community server is not reached.");
		else
		{
			social_menu.sync_code[0] = 0;
			social_menu.sync_status = 0;
			social_menu.sync_joined = FALSE;
			social_menu.sync_at = 0;
			list_open(_list_sync_code);
		}
		break;
	case _row_sync_input:
		if (!social_menu_online())
		{
			message_set("The community server is not reached.");
			break;
		}
		keyboard_open(_typing_sync_code);
		return social_menu.typing != _typing_none ? _social_menu_typing : _social_menu_stays;
	case _row_sync_code:
		break;
	case _row_settings:
		return _social_menu_settings;
	case _row_friends:
		list_open(_list_friends);
		break;
	case _row_requests:
		list_open(_list_requests);
		break;
	case _row_activity:
		list_open(_list_activity);
		break;
	case _row_recent:
		list_open(_list_recent);
		break;
	case _row_moderation:
		list_open(_list_moderation);
		break;
	case _row_add_friend:
		if (!social_menu_online())
		{
			message_set("The community server is not reached.");
			break;
		}
		keyboard_open(_typing_friend_code);
		return social_menu.typing != _typing_none ? _social_menu_typing : _social_menu_stays;
	case _row_friend:
		player_open(social_menu.friends[entry->value].code, social_menu.friends[entry->value].name);
		break;
	case _row_incoming:
		player_open(social_menu.incoming[entry->value].code, social_menu.incoming[entry->value].name);
		break;
	case _row_outgoing:
		player_open(social_menu.outgoing[entry->value].code, social_menu.outgoing[entry->value].name);
		break;
	case _row_moderated:
		player_open(social_menu.moderated[entry->value].code, social_menu.moderated[entry->value].name);
		break;
	case _row_activity_entry:
		player_open(social_menu.activity[entry->value].code, social_menu.activity[entry->value].player);
		break;
	case _row_recent_player:
		{
			char name[SOCIAL_MENU_NAME_SIZE];

			utf8_from_wide(social_menu.recent_names[entry->value], name, sizeof(name));
			player_open(social_menu.recent_codes[entry->value], name);
		}
		break;
	case _row_matches:
		list_open(_list_matches);
		break;
	case _row_join:
		return presence_join(&player->presence, player->name);
	case _row_friend_join:
		return presence_join(&social_menu.friends[entry->value].presence, social_menu.friends[entry->value].name);
	case _row_message:
		{
			wchar_t name[32];

			wide_from_utf8(player->name, name, NUMBEROF(name));
			social_chat_open_private(player->code, name);
		}
		break;
	case _row_friend_add:
		act(entry->kind, "/friends", player->code, player->name, NULL, NULL);
		break;
	case _row_friend_accept:
		act(entry->kind, "/friends/accept", player->code, player->name, NULL, NULL);
		break;
	case _row_friend_decline:
		act(entry->kind, "/friends/remove", player->code, player->name, NULL, NULL);
		break;
	case _row_friend_cancel:
		act(entry->kind, "/friends/remove", player->code, player->name, NULL, NULL);
		break;
	case _row_friend_remove:
		snprintf(what, sizeof(what), "remove %s from your friends", player->name);
		if (confirmed(entry->kind, what))
			act(entry->kind, "/friends/remove", player->code, player->name, NULL, NULL);
		break;
	case _row_mute:
		moderate(entry->kind, "mute", NULL);
		break;
	case _row_unmute:
		moderate(entry->kind, "unmute", NULL);
		break;
	case _row_block:
		snprintf(what, sizeof(what), "block %s", player->name);
		if (confirmed(entry->kind, what))
			moderate(entry->kind, "block", NULL);
		break;
	case _row_unblock:
		moderate(entry->kind, "unblock", NULL);
		break;
	case _row_report:
		list_open(_list_report);
		break;
	case _row_reason:
		snprintf(what, sizeof(what), "report %s (%s)", player->name, social_menu_reasons[entry->value]);
		if (confirmed(entry->kind, what))
			moderate(_row_reason, "report", social_menu_reason_keys[entry->value]);
		break;
	default:
		if (social_extensions_is_ext_row(entry->kind))
		{
			short next_list = social_extensions_choose(social_menu.list, entry->kind, entry->value);
			if (next_list > 0)
				list_open(next_list);
		}
		break;
	}
	return _social_menu_stays;
}

boolean social_menu_step(
	short row,
	short delta)
{
	struct social_menu_row const *entry = row_get(row);

	if (entry && entry->kind == _row_setup_color)
	{
		short count = (short)NUMBEROF(social_menu_name_colors);

		social_setup.color = (short)((social_setup.color + delta + count) % count);
		return TRUE;
	}
	if (!entry || !social_extensions_is_ext_row(entry->kind))
		return FALSE;
	return social_extensions_step_row(social_menu.list, entry->kind, entry->value, delta);
}

void social_menu_details_overlay_draw(
	short row,
	rectangle2d const *bounds)
{
	struct social_menu_row const *entry = row_get(row);

	if (!social_menu.open || !bounds)
		return;
	social_extensions_draw_details_overlay(social_menu.list, entry ? entry->kind : NONE, entry ? entry->value : NONE, bounds);
}

boolean social_menu_back(
	void)
{
	social_menu.confirming_kind = NONE;
	/* (the setup is left by its DONE) */
	if (!social_menu.depth && social_menu.list == _list_setup && !social_setup.finished)
	{
		message_set("Choose DONE to start playing.");
		return TRUE;
	}
	if (!social_menu.depth)
		return FALSE;
	social_menu.depth--;
	social_menu.list = social_menu.stack[social_menu.depth].list;
	social_menu.first = social_menu.stack[social_menu.depth].first;
	social_menu.chosen = social_menu.stack[social_menu.depth].chosen;
	social_menu.list_serial++;
	return TRUE;
}

char const *social_menu_join_host_key(
	void)
{
	return social_menu.join_host_key;
}

char const *social_menu_join_invite(
	void)
{
	return social_menu.join_invite;
}

void social_menu_join_failed(
	void)
{
	message_set("That game could not be joined: internet play\n(network.online) is off, or the invite is old.");
}

/* ---------- a friend's game being joined

A panel over every screen (social_menu_join_draw, from the chat's draw)
while the join goes on behind it: Direct Link (its friend's game chosen
and joined as soon as it shows: menu_functions.c's social_join_direct_link)
or the server browser (which finds a game with a password and asks for it).
It goes once the game's lobby is reached, the screen behind it is left (B),
or the join failed (said for a while) */

enum
{
	SOCIAL_JOIN_TIMEOUT = 30000,
	/* (after the A press posted: the game's own join's) */
	SOCIAL_JOIN_POSTED_TIMEOUT = 15000,
	SOCIAL_JOIN_FAILURE_SHOWN = 4000,
	SOCIAL_JOIN_PANEL_WIDTH = 360,
};

static struct
{
	boolean active;
	short kind;
	char name[SOCIAL_MENU_NAME_SIZE];
	/* (Direct Link's: the host's key hash, hex, as the invite holds it) */
	char host_key[2 * P2P_LISTING_KEY_HASH_SIZE + 1];
	unsigned long began;
	unsigned long posted_at;
	boolean posted;
	/* (why it failed, shown until failed_at + SOCIAL_JOIN_FAILURE_SHOWN) */
	char failure[96];
	unsigned long failed_at;
} social_join;

/* why a join failed (p2p_join_progress's word), as the panel says it */
static char const *social_join_reason_text(
	char const *reason)
{
	static struct
	{
		char const *reason;
		char const *text;
	} const reasons[] =
	{
		{ "no_answer", "The host did not answer." },
		{ "no_route", "No connection to the host could be made." },
		{ "full", "The game is full." },
		{ "closed", "The game is closed to new players." },
		{ "banned", "The host banned you from the game." },
		{ "password", "The game asks for a password." },
		{ "version", "The game is of another version." },
		{ "token", "The invite is no longer valid." },
	};
	short index;

	for (index = 0; index < NUMBEROF(reasons); index++)
	{
		if (!strcmp(reason, reasons[index].reason))
			return reasons[index].text;
	}
	return "The game could not be joined.";
}

void social_menu_join_begin(
	short kind)
{
	unsigned long now = system_milliseconds();

	csmemset(&social_join, 0, sizeof(social_join));
	social_join.active = TRUE;
	social_join.kind = kind;
	social_join.began = now ? now : 1;
	snprintf(social_join.name, sizeof(social_join.name), "%s", social_menu.join_name[0] ? social_menu.join_name : "your friend");
	/* (halo://join/<host's key hash><token>) */
	if (kind == _social_menu_join && strlen(social_menu.join_invite) > 12 + 2 * P2P_LISTING_KEY_HASH_SIZE)
	{
		snprintf(social_join.host_key, sizeof(social_join.host_key), "%.*s", 2 * P2P_LISTING_KEY_HASH_SIZE,
			social_menu.join_invite + 12);
	}
}

void social_menu_join_end(
	char const *failure)
{
	unsigned long now = system_milliseconds();

	if (!social_join.active || social_join.failure[0])
		return;
	if (!failure)
	{
		social_join.active = FALSE;
		return;
	}
	platform_log("social: joining %s's game failed: %s", social_join.name, failure);
	snprintf(social_join.failure, sizeof(social_join.failure), "%s", failure);
	social_join.failed_at = now ? now : 1;
}

boolean social_menu_join_active(
	void)
{
	return social_join.active && !social_join.failure[0];
}

char const *social_menu_join_seeking(
	void)
{
	return social_menu_join_active() && social_join.kind == _social_menu_join && !social_join.posted ?
		social_join.host_key : NULL;
}

void social_menu_join_posted(
	void)
{
	unsigned long now = system_milliseconds();

	social_join.posted = TRUE;
	social_join.posted_at = now ? now : 1;
}

/* (each frame) the lobby reached, the join's failure, its time up */
static void social_menu_join_update(
	unsigned long now)
{
	struct network_game_client *client = global_network_game_client_get();
	char reason[16];
	int stage = P2P_JOIN_NONE;

	if (!social_join.active)
		return;
	if (social_join.failure[0])
	{
		if (now - social_join.failed_at > SOCIAL_JOIN_FAILURE_SHOWN)
			social_join.active = FALSE;
		return;
	}
	/* (the game's lobby: pregame, or a game under way joined) */
	if (client && network_game_client_get_state(client, NULL) >= 2)
	{
		social_join.active = FALSE;
		return;
	}
	reason[0] = 0;
	if (social_join.kind == _social_menu_join)
		p2p_join_progress(&stage, reason, sizeof(reason));
	if (stage == P2P_JOIN_FAILED)
		social_menu_join_end(social_join_reason_text(reason));
	else if (social_join.posted && now - social_join.posted_at > SOCIAL_JOIN_POSTED_TIMEOUT)
		social_join.active = FALSE;
	else if (!social_join.posted && now - social_join.began > SOCIAL_JOIN_TIMEOUT)
		social_menu_join_end("Their game did not show up in time.");
}

void social_menu_join_draw(
	short screen_width,
	short screen_height)
{
	struct campaign_score_panel text;
	struct campaign_score_panel title;
	real_argb_color white;
	real_argb_color blue;
	real_argb_color grey;
	char line[128];
	char status[96];
	real scale;
	real height;
	real y;
	int stage = P2P_JOIN_NONE;
	short dots = (short)((system_milliseconds() / 400) % 4);

	if (!social_join.active || screen_width <= 0 || screen_height <= 0 ||
		!card_font("ui\\small_ui", &text) || !card_font("ui\\large_ui", &title))
	{
		return;
	}
	card_color(&white, 1.0f, 1.0f, 1.0f);
	card_color(&blue, 0.55f, 0.72f, 1.0f);
	card_color(&grey, 0.65f, 0.65f, 0.65f);
	/* (the screen behind dimmed) */
	text.scale = 1.0f;
	text.left = text.top = 0.0f;
	text.width = screen_width;
	campaign_score_panel_quad(&text, 0.0f, 0.0f, screen_width, screen_height, 0xB0000000);
	/* (the panel, in the middle, in the screen's 480 lines) */
	scale = (real)screen_height / 480.0f;
	height = 14.0f + title.line_height + 10.0f + 2.0f * text.line_height + 12.0f + text.line_height + 12.0f;
	text.scale = title.scale = scale;
	text.width = title.width = SOCIAL_JOIN_PANEL_WIDTH;
	text.left = title.left = (screen_width - SOCIAL_JOIN_PANEL_WIDTH * scale) / 2.0f;
	text.top = title.top = (screen_height - height * scale) / 2.0f;
	campaign_score_panel_quad(&text, 0.0f, 0.0f, SOCIAL_JOIN_PANEL_WIDTH, height, CAMPAIGN_SCORE_CARD_FILL);
	campaign_score_panel_frame(&text, 0.0f, 0.0f, SOCIAL_JOIN_PANEL_WIDTH, height, CAMPAIGN_SCORE_PANEL_EDGE);
	y = 14.0f;
	snprintf(line, sizeof(line), "JOINING %s", social_join.name);
	campaign_score_panel_text(&title, line, NULL, 16.0f, SOCIAL_JOIN_PANEL_WIDTH - 16.0f, y, 1.0f,
		_campaign_score_text_center, &white);
	y += title.line_height + 10.0f;
	if (social_join.failure[0])
	{
		campaign_score_panel_text(&text, "Their game could not be joined.", NULL, 16.0f, SOCIAL_JOIN_PANEL_WIDTH - 16.0f,
			y, 1.0f, _campaign_score_text_center, &blue);
		campaign_score_panel_text(&text, social_join.failure, NULL, 16.0f, SOCIAL_JOIN_PANEL_WIDTH - 16.0f,
			y + text.line_height, 1.0f, _campaign_score_text_center, &white);
		return;
	}
	if (social_join.kind == _social_menu_join_listed)
		snprintf(status, sizeof(status), "Finding their game in the server browser");
	else
	{
		char reason[16];

		p2p_join_progress(&stage, reason, sizeof(reason));
		if (social_join.posted)
			snprintf(status, sizeof(status), "Joining their game");
		else if (stage == P2P_JOIN_CONNECTED)
			snprintf(status, sizeof(status), "Connected. Waiting for their game");
		else if (stage == P2P_JOIN_ANSWERED)
			snprintf(status, sizeof(status), "Connecting to the host");
		else
			snprintf(status, sizeof(status), "Asking for the host");
	}
	snprintf(line, sizeof(line), "%s%.*s", status, dots, "...");
	campaign_score_panel_text(&text, line, NULL, 16.0f, SOCIAL_JOIN_PANEL_WIDTH - 16.0f, y, 1.0f,
		_campaign_score_text_center, &blue);
	/* (how far along: a bar of the steps) */
	{
		real done = social_join.posted ? 0.9f : social_join.kind == _social_menu_join_listed ? 0.5f :
			stage == P2P_JOIN_CONNECTED ? 0.7f : stage == P2P_JOIN_ANSWERED ? 0.4f : 0.15f;
		real bar_top = y + text.line_height + 6.0f;

		campaign_score_panel_quad(&text, 40.0f, bar_top, SOCIAL_JOIN_PANEL_WIDTH - 40.0f, bar_top + 4.0f, 0x60FFFFFF);
		campaign_score_panel_quad(&text, 40.0f, bar_top, 40.0f + (SOCIAL_JOIN_PANEL_WIDTH - 80.0f) * done,
			bar_top + 4.0f, 0xFF2896FF);
	}
	y += 2.0f * text.line_height + 12.0f;
	campaign_score_panel_text(&text, "B: CANCEL", NULL, 16.0f, SOCIAL_JOIN_PANEL_WIDTH - 16.0f, y, 1.0f,
		_campaign_score_text_center, &grey);
}

/* ---------- the chat's (a player's name clicked: social_chat.c) */

void social_menu_player_act(
	char const *code,
	char const *name,
	short action)
{
	static char const *const actions[] = { NULL, "mute", "unmute", "report" };
	static short const kinds[] = { _row_friend_add, _row_mute, _row_unmute, _row_reason };

	if (action < 0 || action >= (short)NUMBEROF(kinds))
		return;
	act_as(kinds[action], action == _social_menu_act_friend ? "/friends" : "/moderation", code, name,
		actions[action], action == _social_menu_act_report ? "chat" : NULL, TRUE);
	/* (not asked: why, in the chat) */
	if (!social_menu.action_ticket)
	{
		social_chat_notice_text(social_menu.message);
		social_menu.action_from_chat = FALSE;
	}
}

void social_menu_profile_ask(
	char const *code,
	char const *name)
{
	struct social_menu_player *player = &social_menu.player;

	if (strcmp(player->code, code))
	{
		csmemset(player, 0, sizeof(*player));
		snprintf(player->code, sizeof(player->code), "%s", code);
		snprintf(player->name, sizeof(player->name), "%s", name && name[0] ? name : code);
	}
	social_menu.player_failed = FALSE;
	social_menu.player_wanted = TRUE;
}

short social_menu_profile_text(
	char const *code,
	char *text,
	long size)
{
	struct social_menu_player const *player = &social_menu.player;
	char line[32];

	text[0] = 0;
	if (strcmp(player->code, code))
		return 0;
	if (!player->had)
	{
		if (!social_menu.player_failed)
			return 0;
		text_add(text, size, "%s", social_menu.message[0] ? social_menu.message : "The server did not answer.");
		return -1;
	}
	if (!strcmp(player->relation, "friends"))
		text_add(text, size, "Your friend.\n");
	else if (!strcmp(player->relation, "incoming"))
		text_add(text, size, "Wants to be your friend.\n");
	else if (!strcmp(player->relation, "outgoing"))
		text_add(text, size, "You asked to be friends.\n");
	if (strcmp(player->relation, "self"))
		presence_line(&player->presence, text, size);
	kda_text(player->multiplayer.kills, player->multiplayer.deaths, player->multiplayer.assists, line, sizeof(line));
	text_add(text, size, "Multiplayer: %ld matches, %ld won.\nKills %ld, deaths %ld, KDA %s.\n",
		player->multiplayer.matches, player->multiplayer.wins, player->multiplayer.kills,
		player->multiplayer.deaths, line);
	text_add(text, size, "Campaign: %ld levels won.\nMedals: %ld.\n", player->campaign.matches,
		player->multiplayer.medals + player->campaign.medals);
	if (player->muted || player->blocked)
		text_add(text, size, "%s.\n", player->blocked ? "Blocked" : "Muted");
	return 1;
}

boolean social_menu_is_friend(
	char const *code)
{
	short index;

	for (index = 0; index < social_menu.friend_count; index++)
	{
		if (!strcmp(social_menu.friends[index].code, code))
			return TRUE;
	}
	return FALSE;
}

boolean social_menu_is_muted(
	char const *code)
{
	short index;

	for (index = 0; index < social_menu.moderated_count; index++)
	{
		if (!strcmp(social_menu.moderated[index].code, code))
			return social_menu.moderated[index].muted;
	}
	return FALSE;
}
