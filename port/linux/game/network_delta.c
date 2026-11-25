/* Delta cross-play (network.delta): a client's half, joining a game whose
host speaks Delta Peer (Chupathingy's hosts, and any other that follows the
open network spec, github.com/ChupathingyCE/delta-spec: ports.md, "The
short version"). The game itself is the plain game protocol at this build's
network version, so nothing here is needed to play: it tells the host what
this machine is, so that the host keeps the game to its limits.

- The advertisement's reserved bytes say whether a host speaks Delta (a tag
  at their end, or an older host's layout: delta_ref.c's dref_advert_read).
- Joining such a host, the join request's machine name is marked past its
  end (peer.md, "The join mark"), so the host waits for this machine's
  HELLO before it sends the game's settings.
- Once the host has given this machine its index, HELLO goes from a socket
  of its own to the host's address, at the host's game port plus 10 (5160),
  every 500 milliseconds, offering the platform capability alone, with this
  machine's platform key and network version. A WELCOME within 4 seconds and
  this machine speaks Delta Peer for the game; LEGACY, BYE or nothing, and
  it plays plainly, as it would have anyway. The game never waits on it.
- Leaving, a host that welcomed it is told BYE.

And a host's half (ports.md, "If you host games from your port"): a host of
a stock game (never a modded one, whose advertisement is all this build's)
opens the Delta socket on its game port plus 10, and while it is open says
so with a tag of layout 2 at its advertisement's end, naming Content
Evolved's project number (registry.md, "Projects") and the mod's network
version as the project's own. It answers a HELLO from a machine of its game
(that machine's address, with the index the host gave it) with WELCOME,
agreeing on the platform capability alone, sends each Delta machine the
roster with the room's player limit (the smallest of this host's and each
Delta machine's join limit), and takes no more players than that. Every
join request's name has the join mark cleared; a marked machine is held
from the game's settings until its HELLO comes, or 4 seconds. The record it
then gets is this build's own network version's, the only one it has, so a
machine that can't read it drops out on its own (ports.md's step 7). A
machine without Delta sees nothing of this.

delta_ref.c and delta_ref.h are the spec's reference code (MIT,
LICENSES/MIT.txt), as they are in the spec's reference/ folder. */
#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "bungie_net/network/transport.h"
#include "bungie_net/network/transport_endpoint_winsock.h"
#include "memory/byte_swapping.h"
#include "networking/network_messages.h"
#include "delta_ref.h"
#include "network_delta.h"
#include "halo_port_limits.h"
#include <stdio.h>
#include <string.h>

int config_boolean(char const *name);
unsigned long config_changes(void);
extern const int halo_build_number;

enum
{
	/* network_client_manager.c's MAXIMUM_NETWORK_ADVERTISED_GAMES, at least */
	DELTA_ADVERTISED_GAMES = 16,
	/* the machine name's 16-bit characters (MAXIMUM_MACHINE_NAME_LENGTH) */
	DELTA_MACHINE_NAME_LENGTH = 32,
	/* the host's: the game's machines (MAXIMUM_NETWORK_MACHINE_COUNT) */
	DELTA_HOST_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	/* milliseconds: a marked machine's wait for its HELLO, the roster's
	pace (on a change, at most this often; and every 5 seconds), a socket
	that would not open tried again, and a new session from one machine */
	DELTA_MARK_WAIT = 4000,
	DELTA_ROSTER_SOONEST = 250,
	DELTA_ROSTER_EVERY = 5000,
	DELTA_SOCKET_RETRY = 5000,
	DELTA_SESSION_EVERY = 1000,
	/* the messages a host takes from one machine: 10 a second, 20 at once */
	DELTA_BUDGET_RATE = 10,
	DELTA_BUDGET_MOST = 20,
	/* datagrams read a frame */
	DELTA_HOST_READS = 64,
};

/* the capabilities this host speaks in full */
#define DELTA_HOST_CAPABILITIES (1u << DREF_CAP_PLATFORM)

/* this platform's number (registry.md, "Platforms"): per platform, not per
project; 0 where the registry has none for this build yet */
#if defined(HALO_MACOS)
#define DELTA_PLATFORM 2
#elif defined(HALO_SWITCH)
#define DELTA_PLATFORM 9
#elif defined(HALO_WEB) || defined(HALO_PS5)
#define DELTA_PLATFORM 0
#elif defined(HALO_ANDROID)
#define DELTA_PLATFORM 4
#elif defined(_WIN32)
#define DELTA_PLATFORM 1
#else
#define DELTA_PLATFORM 3
#endif

static struct
{
	boolean speaks[DELTA_ADVERTISED_GAMES];
	/* ... with a tag of layout 2: another project's host, whose kind byte is
	the game's kind in any game */
	boolean project[DELTA_ADVERTISED_GAMES];
	/* the game being joined speaks Delta (and network.delta is on) */
	boolean joining;
	boolean started;
	SOCKET socket;
	/* the host's address and Delta port, in network byte order */
	unsigned long host_address;
	word host_port;
	int told_state;
	struct dref_client client;
} delta = { { 0 }, { 0 }, FALSE, FALSE, INVALID_SOCKET };

/* the host's half */
struct delta_host_machine
{
	/* accepted into the game, from this address (network byte order) */
	boolean present;
	unsigned long address;
	/* the port its HELLO came from (network byte order), where it is answered */
	word hello_port;
	/* its record held for its HELLO (a marked join), since */
	boolean waiting;
	unsigned long joined_time;
	/* welcomed: its session, its HELLO and what both agreed */
	boolean speaks;
	uint32_t session;
	struct dref_hello hello;
	uint32_t agreed;
	unsigned long session_time;
	/* its message budget, and when it was last topped up */
	long budget;
	unsigned long budget_time;
};

static struct
{
	SOCKET socket;
	boolean failed;
	unsigned long failed_time;
	/* the room's player limit for its Delta machines (0: none below this
	host's own) */
	long room_players;
	boolean roster_due;
	unsigned long roster_time;
	struct delta_host_machine machines[DELTA_HOST_MACHINES];
} delta_host = { INVALID_SOCKET };

unsigned char network_game_server_port_machine_joined(long machine_index);
boolean network_game_server_port_machine_local(long machine_index);
boolean network_extensions_host_machine_held(long machine_index);
boolean network_extensions_server_send_settings(long machine_index);
int halo_server_mode(void);

static boolean delta_enabled(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean enabled = TRUE;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("network.delta") != 0;
	}
	return enabled;
}

void network_delta_advertised(long game_index, byte const *reserved, long size)
{
	struct dref_advert advert;

	if (game_index < 0 || game_index >= DELTA_ADVERTISED_GAMES)
		return;
	delta.speaks[game_index] = dref_advert_read(reserved, (int)size, &advert) && advert.speaks_delta;
	delta.project[game_index] = delta.speaks[game_index] && advert.project;
}

boolean network_delta_project_host(long game_index)
{
	return network_delta_speaks(game_index) && delta.project[game_index];
}

boolean network_delta_speaks(long game_index)
{
	return game_index >= 0 && game_index < DELTA_ADVERTISED_GAMES && delta.speaks[game_index];
}

void network_delta_join_started(long game_index)
{
	network_delta_stop();
	delta.joining = network_delta_speaks(game_index) && delta_enabled();
}

void network_delta_mark_join(wchar_t *machine_name, long count)
{
	uint16_t name[DELTA_MACHINE_NAME_LENGTH];
	long index;

	if (!delta.joining || count != DELTA_MACHINE_NAME_LENGTH)
		return;
	for (index = 0; index < count; index++)
		name[index] = (uint16_t)machine_name[index];
	if (!dref_join_mark(name, (int)count))
		return;
	for (index = 0; index < count; index++)
		machine_name[index] = (wchar_t)name[index];
}

/* this machine's HELLO or WELCOME: the platform capability, its network
version, its platform key and its build */
static void delta_mine(struct dref_hello *mine, uint8_t machine_index)
{
	csmemset(mine, 0, sizeof(*mine));
	mine->capabilities = DELTA_HOST_CAPABILITIES;
	mine->legacy_version = HALO_PORT_NETWORK_VERSION;
	mine->machine_index = machine_index;
	mine->table_serial = DREF_NO_TABLES;
	mine->key.platform = DELTA_PLATFORM;
	mine->key.version = 1;
	mine->key.host_players = HALO_PORT_MAXIMUM_NETWORK_PLAYERS;
	mine->key.join_players = HALO_PORT_MAXIMUM_NETWORK_PLAYERS;
	mine->key.join_players_opt_in = HALO_PORT_MAXIMUM_NETWORK_PLAYERS;
	snprintf(mine->build, sizeof(mine->build), "content-evolved %d", halo_build_number);
}

static void delta_send(void *user, const uint8_t *data, int size)
{
	struct sockaddr_in to;

	(void)user;
	if (delta.socket == INVALID_SOCKET)
		return;
	csmemset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = delta.host_address;
	to.sin_port = delta.host_port;
	sendto(delta.socket, (char const *)data, size, 0, (struct sockaddr *)&to, sizeof(to));
}

void network_delta_accepted(short machine_index, unsigned long host_address, word host_port)
{
	struct sockaddr_in local;
	unsigned long nonblocking = 1;
	struct dref_hello mine;
	unsigned long session = 0;

	if (!delta.joining || delta.started)
		return;
	delta.socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (delta.socket == INVALID_SOCKET)
		return;
	/* (bound, so that internet play's tunnel brings the host's answers to it) */
	csmemset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	if (bind(delta.socket, (struct sockaddr *)&local, sizeof(local)) != 0 ||
		ioctlsocket(delta.socket, FIONBIO, &nonblocking) != 0)
	{
		closesocket(delta.socket);
		delta.socket = INVALID_SOCKET;
		return;
	}
	delta.host_address = SWAP4(host_address);
	delta.host_port = (word)SWAP2((word)(host_port + DREF_PORT_OFFSET));

	delta_mine(&mine, (uint8_t)machine_index);
	XNetRandom((unsigned char *)&session, sizeof(session));
	delta.started = TRUE;
	delta.told_state = DREF_CLIENT_WAITING;
	network_event("delta: saying HELLO to the host (platform %d, network version %d)", DELTA_PLATFORM,
		HALO_PORT_NETWORK_VERSION);
	dref_client_start(&delta.client, &mine, (uint32_t)session, (uint32_t)system_milliseconds(), delta_send, NULL);
}

void network_delta_update(void)
{
	uint8_t datagram[DREF_MAXIMUM_DATAGRAM];
	uint32_t now;
	int count;

	if (!delta.started)
		return;
	now = (uint32_t)system_milliseconds();
	/* (every datagram this frame: those not from the host's Delta port are dropped) */
	for (count = 0; count < 16; count++)
	{
		struct sockaddr_in from;
		int from_size = sizeof(from);
		int size = recvfrom(delta.socket, (char *)datagram, sizeof(datagram), 0, (struct sockaddr *)&from, &from_size);

		if (size <= 0)
			break;
		if (from_size < (int)sizeof(from) || from.sin_family != AF_INET ||
			from.sin_addr.s_addr != delta.host_address || from.sin_port != delta.host_port)
		{
			continue;
		}
		dref_client_receive(&delta.client, now, datagram, size);
	}
	dref_client_tick(&delta.client, now);
	if (delta.client.state != delta.told_state)
	{
		delta.told_state = delta.client.state;
		if (delta.client.state == DREF_CLIENT_DELTA)
			network_event("delta: the host welcomed this machine (%s, capabilities %08lx)", delta.client.host.build,
				(unsigned long)delta.client.agreed);
		else if (delta.client.state == DREF_CLIENT_LEGACY)
			network_event("delta: no WELCOME from the host; playing plainly");
	}
}

void network_delta_stop(void)
{
	if (delta.started)
	{
		dref_client_stop(&delta.client);
		delta.started = FALSE;
	}
	if (delta.socket != INVALID_SOCKET)
	{
		closesocket(delta.socket);
		delta.socket = INVALID_SOCKET;
	}
	delta.joining = FALSE;
}

/* ---------- the host */

/* the most players of a game a platform joins (peer.md, "Platform
policy": its row's, or its opted-in player's); unknown numbers as unknown */
static long delta_platform_join_limit(struct dref_platform_key const *key)
{
	static struct
	{
		unsigned char platform, join_players, opted_in;
	} const rows[] = {
		{ 6, 32, 128 }, { 7, 32, 128 }, { 8, 32, 128 }, { 9, 16, 128 }, { 10, 16, 128 }, { 11, 16, 128 },
		{ 14, 16, 128 }, { 15, 16, 128 }, { 16, 16, 128 }, { 17, 8, 16 }, { 18, 16, 128 }, { 19, 16, 128 },
		{ 22, 8, 16 },
	};
	long row = 128, row_opted_in = 128, claim;
	int index;

	for (index = 0; index < (int)NUMBEROF(rows); index++)
	{
		if (rows[index].platform == key->platform)
		{
			row = rows[index].join_players;
			row_opted_in = rows[index].opted_in;
		}
	}
	/* (a key may claim less than its row, never more) */
	if (key->flags & 0x01)
	{
		claim = key->join_players_opt_in < key->join_players ? key->join_players : key->join_players_opt_in;
		return claim && claim < row_opted_in ? claim : row_opted_in;
	}
	claim = key->join_players;
	return claim && claim < row ? claim : row;
}

static void delta_host_send(long machine_index, uint8_t const *data, int size)
{
	struct sockaddr_in to;
	struct delta_host_machine const *machine = &delta_host.machines[machine_index];

	if (delta_host.socket == INVALID_SOCKET || size <= 0)
		return;
	csmemset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = machine->address;
	to.sin_port = machine->hello_port;
	sendto(delta_host.socket, (char const *)data, size, 0, (struct sockaddr *)&to, sizeof(to));
}

/* the host's own machine's index in the game (DREF_NO_MACHINE: a dedicated
server's, which has none) */
static uint8_t delta_host_machine_index(void)
{
	long index;

	if (halo_server_mode())
		return DREF_NO_MACHINE;
	for (index = 0; index < DELTA_HOST_MACHINES && index < DREF_NO_MACHINE; index++)
	{
		if (network_game_server_port_machine_local(index))
			return (uint8_t)index;
	}
	return DREF_NO_MACHINE;
}

/* a marked machine's wait over (its HELLO came, or the time is up): the
game's settings it was held from, unless the game's capabilities hold it
still (network_extensions.c sends them then) */
static void delta_host_release(long machine_index)
{
	if (!delta_host.machines[machine_index].waiting)
		return;
	delta_host.machines[machine_index].waiting = FALSE;
	if (!network_extensions_host_machine_held(machine_index))
		network_extensions_server_send_settings(machine_index);
}

static void delta_host_forget(long machine_index, boolean say_bye)
{
	struct delta_host_machine *machine = &delta_host.machines[machine_index];

	if (machine->speaks && say_bye)
	{
		uint8_t datagram[DREF_HEADER_SIZE];

		delta_host_send(machine_index, datagram,
			dref_empty_write(datagram, sizeof(datagram), DREF_MAJOR, DREF_BYE, machine->session));
	}
	if (machine->speaks)
		delta_host.roster_due = TRUE;
	machine->speaks = FALSE;
	machine->session = 0;
}

static void delta_host_close(void)
{
	long index;

	for (index = 0; index < DELTA_HOST_MACHINES; index++)
	{
		delta_host_forget(index, TRUE);
		delta_host.machines[index].present = FALSE;
		delta_host.machines[index].waiting = FALSE;
	}
	if (delta_host.socket != INVALID_SOCKET)
	{
		closesocket(delta_host.socket);
		delta_host.socket = INVALID_SOCKET;
		network_event("delta: the host's Delta socket closed");
	}
	delta_host.room_players = 0;
}

static void delta_host_open(void)
{
	struct sockaddr_in local;
	unsigned long nonblocking = 1;
	unsigned long now = system_milliseconds();

	if (delta_host.failed && now - delta_host.failed_time < DELTA_SOCKET_RETRY)
		return;
	delta_host.socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (delta_host.socket != INVALID_SOCKET)
	{
		/* (INADDR_ANY: the platform layer binds it where the game's sockets
		are, network.address, and internet play's tunnel reaches it) */
		csmemset(&local, 0, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = INADDR_ANY;
		local.sin_port = (word)SWAP2((word)DREF_HOST_PORT);
		if (bind(delta_host.socket, (struct sockaddr *)&local, sizeof(local)) == 0 &&
			ioctlsocket(delta_host.socket, FIONBIO, &nonblocking) == 0)
		{
			delta_host.failed = FALSE;
			delta_host.roster_due = TRUE;
			network_event("delta: hosting with Delta on port %d", DREF_HOST_PORT);
			return;
		}
		closesocket(delta_host.socket);
		delta_host.socket = INVALID_SOCKET;
	}
	/* (another copy of the game holds the port: this one hosts without Delta) */
	if (!delta_host.failed)
		network_event("delta: port %d is taken: hosting without Delta", DREF_HOST_PORT);
	delta_host.failed = TRUE;
	delta_host.failed_time = now ? now : 1;
}

void network_delta_host_tag(byte *reserved, long size)
{
	if (delta_host.socket == INVALID_SOCKET || size < DREF_RESERVED_SIZE)
		return;
	/* (a modded game's version word is 0xFF00 or more: no tag is written) */
	dref_advert_write_project(reserved, reserved[0] | (reserved[1] << 8), reserved[2], reserved[3],
		DREF_PROJECT_CONTENT_EVOLVED, HALO_PORT_MOD_NETWORK_VERSION);
}

void network_delta_host_mark_join(wchar_t *machine_name, long count, boolean *marked)
{
	uint16_t name[DELTA_MACHINE_NAME_LENGTH];
	long index;

	*marked = FALSE;
	if (count != DELTA_MACHINE_NAME_LENGTH)
		return;
	for (index = 0; index < count; index++)
		name[index] = (uint16_t)machine_name[index];
	/* (cleared whether this host speaks Delta or not, so no other machine sees it) */
	*marked = dref_join_mark_read(name, (int)count) && delta_host.socket != INVALID_SOCKET;
	for (index = 0; index < count; index++)
		machine_name[index] = (wchar_t)name[index];
}

void network_delta_host_machine_accepted(long machine_index, unsigned long address, boolean marked)
{
	struct delta_host_machine *machine;

	if (machine_index < 0 || machine_index >= DELTA_HOST_MACHINES)
		return;
	machine = &delta_host.machines[machine_index];
	delta_host_forget(machine_index, FALSE);
	csmemset(machine, 0, sizeof(*machine));
	machine->present = TRUE;
	machine->address = SWAP4(address);
	machine->waiting = marked;
	machine->joined_time = system_milliseconds();
	machine->budget = DELTA_BUDGET_MOST;
	machine->budget_time = machine->joined_time;
	delta_host.roster_due = TRUE;
	if (marked)
		network_event("delta: machine #%ld marked its join: its settings wait for its HELLO", machine_index);
}

boolean network_delta_host_holds(long machine_index)
{
	return machine_index >= 0 && machine_index < DELTA_HOST_MACHINES && delta_host.machines[machine_index].present &&
		delta_host.machines[machine_index].waiting;
}

long network_delta_host_room_players(void)
{
	return delta_host.room_players;
}

static boolean delta_host_takes(struct delta_host_machine *machine, unsigned long now)
{
	long topped = (long)((now - machine->budget_time) * DELTA_BUDGET_RATE / MILLISECONDS_PER_SECOND);

	if (topped > 0)
	{
		machine->budget = MIN(DELTA_BUDGET_MOST, machine->budget + topped);
		machine->budget_time = now;
	}
	if (machine->budget <= 0)
		return FALSE;
	machine->budget--;
	return TRUE;
}

static void delta_host_hello(long machine_index, uint32_t session, uint8_t major, uint8_t const *payload, int size,
	word port, unsigned long now)
{
	struct delta_host_machine *machine = &delta_host.machines[machine_index];
	uint8_t datagram[DREF_MAXIMUM_DATAGRAM];
	struct dref_hello hello, welcome;

	machine->hello_port = port;
	if (major != DREF_MAJOR)
	{
		delta_host_send(machine_index, datagram,
			dref_empty_write(datagram, sizeof(datagram), DREF_MAJOR, DREF_LEGACY, session));
		return;
	}
	if (!dref_hello_read(payload, size, &hello) || !session)
		return;
	/* (the machine index the host gave the address; a HELLO again of the
	session is answered again, a new one at most once a second) */
	if (hello.machine_index != machine_index)
		return;
	if (!machine->speaks || machine->session != session)
	{
		if (machine->session_time && now - machine->session_time < DELTA_SESSION_EVERY)
			return;
		machine->session_time = now ? now : 1;
		machine->session = session;
		machine->hello = hello;
		machine->agreed = DELTA_HOST_CAPABILITIES & dref_capabilities_clean(hello.capabilities);
		if (!machine->speaks)
		{
			network_event("delta: machine #%ld speaks Delta (%s, platform %d, network version %d)", machine_index,
				hello.build, hello.key.platform, hello.legacy_version);
		}
		machine->speaks = TRUE;
		delta_host.roster_due = TRUE;
	}
	delta_mine(&welcome, delta_host_machine_index());
	welcome.agreed = machine->agreed;
	if (halo_server_mode())
		welcome.key.flags |= 0x02;
	delta_host_send(machine_index, datagram, dref_welcome_write(datagram, sizeof(datagram), session, &welcome));
	/* (its record: this build has only its own number's, which every
	machine gets, whatever number its HELLO says) */
	delta_host_release(machine_index);
}

static void delta_host_receive(unsigned long now)
{
	uint8_t datagram[DREF_MAXIMUM_DATAGRAM];
	int count;

	for (count = 0; count < DELTA_HOST_READS; count++)
	{
		struct sockaddr_in from;
		int from_size = sizeof(from);
		int size = recvfrom(delta_host.socket, (char *)datagram, sizeof(datagram), 0, (struct sockaddr *)&from,
			&from_size);
		struct dref_header header;
		long machine_index = NONE, index;

		if (size <= 0)
			break;
		if (from_size < (int)sizeof(from) || from.sin_family != AF_INET)
			continue;
		/* (only a machine of the game is heard: by its address, then by the
		HELLO's machine index or the session) */
		for (index = 0; index < DELTA_HOST_MACHINES && machine_index == NONE; index++)
		{
			if (delta_host.machines[index].present && delta_host.machines[index].address == from.sin_addr.s_addr)
				machine_index = index;
		}
		if (machine_index == NONE || !dref_header_read(datagram, size, &header))
			continue;
		if (header.type == DREF_HELLO && header.length >= 7 && header.major == DREF_MAJOR)
		{
			uint8_t said = datagram[DREF_HEADER_SIZE + 6];

			if (said >= DELTA_HOST_MACHINES || !delta_host.machines[said].present ||
				delta_host.machines[said].address != from.sin_addr.s_addr)
			{
				continue;
			}
			machine_index = said;
		}
		else
		{
			for (index = 0; index < DELTA_HOST_MACHINES; index++)
			{
				if (delta_host.machines[index].present && delta_host.machines[index].speaks &&
					delta_host.machines[index].session == header.session &&
					delta_host.machines[index].address == from.sin_addr.s_addr)
				{
					machine_index = index;
					break;
				}
			}
		}
		if (!delta_host_takes(&delta_host.machines[machine_index], now))
			continue;
		if (header.type == DREF_HELLO)
		{
			delta_host_hello(machine_index, header.session, header.major, datagram + DREF_HEADER_SIZE, header.length,
				from.sin_port, now);
		}
		else if (header.type == DREF_BYE && delta_host.machines[machine_index].speaks &&
			delta_host.machines[machine_index].session == header.session)
		{
			network_event("delta: machine #%ld said BYE", machine_index);
			delta_host_forget(machine_index, FALSE);
		}
	}
}

/* the room's player limit, and the roster to each Delta machine */
static void delta_host_roster(unsigned long now)
{
	struct dref_roster_entry entries[32];
	uint8_t datagram[DREF_MAXIMUM_DATAGRAM];
	struct dref_roster roster;
	uint8_t host_index = delta_host_machine_index();
	long room = HALO_PORT_MAXIMUM_NETWORK_PLAYERS;
	uint32_t room_capabilities = DELTA_HOST_CAPABILITIES;
	long index, receiver;
	boolean any = FALSE;

	for (index = 0; index < DELTA_HOST_MACHINES; index++)
	{
		struct delta_host_machine const *machine = &delta_host.machines[index];

		if (!machine->present)
			continue;
		if (!machine->speaks)
		{
			room_capabilities = 0;
			continue;
		}
		any = TRUE;
		room_capabilities &= machine->agreed;
		room = MIN(room, delta_platform_join_limit(&machine->hello.key));
	}
	delta_host.room_players = room < HALO_PORT_MAXIMUM_NETWORK_PLAYERS ? room : 0;
	if (!any || (!delta_host.roster_due && now - delta_host.roster_time < DELTA_ROSTER_EVERY) ||
		now - delta_host.roster_time < DELTA_ROSTER_SOONEST)
	{
		return;
	}
	delta_host.roster_due = FALSE;
	delta_host.roster_time = now;
	roster.room_capabilities = room_capabilities;
	roster.room_players = (uint8_t)MIN(room, 255);
	for (receiver = 0; receiver < DELTA_HOST_MACHINES; receiver++)
	{
		struct delta_host_machine const *to = &delta_host.machines[receiver];
		int count = 0;

		if (!to->present || !to->speaks)
			continue;
		/* (every machine of the game, 32 a datagram; a machine's platform
		only where it and the receiver both agreed to it) */
		for (index = 0; index < DELTA_HOST_MACHINES; index++)
		{
			struct delta_host_machine const *machine = &delta_host.machines[index];
			boolean host = index == host_index;
			struct dref_hello mine;

			if (!host && !machine->present)
				continue;
			csmemset(&entries[count], 0, sizeof(entries[count]));
			entries[count].machine_index = (uint8_t)index;
			if (host)
			{
				delta_mine(&mine, host_index);
				entries[count].flags = 0x01 | ((to->agreed & DELTA_HOST_CAPABILITIES) ? 0x02 : 0);
				entries[count].capabilities = to->agreed;
				entries[count].key = mine.key;
			}
			else if (machine->speaks)
			{
				entries[count].flags = 0x01 | ((machine->agreed & to->agreed & (1u << DREF_CAP_PLATFORM)) ? 0x02 : 0);
				entries[count].capabilities = machine->agreed;
				entries[count].key = machine->hello.key;
			}
			if (++count == (int)NUMBEROF(entries))
			{
				roster.count = (uint8_t)count;
				delta_host_send(receiver, datagram,
					dref_roster_write(datagram, sizeof(datagram), to->session, &roster, entries, count));
				count = 0;
			}
		}
		if (count)
		{
			roster.count = (uint8_t)count;
			delta_host_send(receiver, datagram,
				dref_roster_write(datagram, sizeof(datagram), to->session, &roster, entries, count));
		}
	}
}

void network_delta_host_update(boolean hosting)
{
	unsigned long now = system_milliseconds();
	long index;

	if (!hosting || !delta_enabled())
	{
		if (delta_host.socket != INVALID_SOCKET)
			delta_host_close();
		return;
	}
	if (delta_host.socket == INVALID_SOCKET)
	{
		delta_host_open();
		if (delta_host.socket == INVALID_SOCKET)
			return;
	}
	delta_host_receive(now);
	for (index = 0; index < DELTA_HOST_MACHINES; index++)
	{
		struct delta_host_machine *machine = &delta_host.machines[index];

		if (!machine->present)
			continue;
		/* (the host's own machine joins its game too, and is known as its own
		once its client has its index: the roster's host entry, never a
		machine without Delta) */
		if (network_game_server_port_machine_local(index))
		{
			machine->present = FALSE;
			machine->waiting = FALSE;
			delta_host.roster_due = TRUE;
			continue;
		}
		/* (one that left: told BYE if it spoke Delta, and forgotten) */
		if (!network_game_server_port_machine_joined(index))
		{
			delta_host_forget(index, TRUE);
			machine->present = FALSE;
			machine->waiting = FALSE;
			delta_host.roster_due = TRUE;
			continue;
		}
		if (machine->waiting && now - machine->joined_time >= DELTA_MARK_WAIT)
		{
			network_event("delta: no HELLO from machine #%ld in time: its settings go out", index);
			delta_host_release(index);
		}
	}
	delta_host_roster(now);
}

void network_delta_host_stop(void)
{
	delta_host_close();
}
