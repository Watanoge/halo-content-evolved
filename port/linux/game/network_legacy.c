/*
NETWORK_LEGACY.C

Joining a host of another network version than this build's (a range, as
Delta's clients join: delta-spec's versioning.md, "Layout facts by network
version"). The game itself is this build's: what differs between OpenCE's
versions on the wire is a handful of layout facts (delta_layout.c, the
spec's reference code, MIT: LICENSES/MIT.txt), and this unit plays a host by
its version's:

- the game settings record's size and its options' (13,120 bytes and 0x1C to
  34, 13,136 and 0x2C to 41, 13,200 from 42), made this build's record by
  delta_layout_convert, the options an older host has none of their
  defaults;
- the join request's bytes past its first 0x70 (none before 40, 234 to 41,
  236 with the host's version in a word at 234 from 42);
- the distributed messages' headers: 4 bytes longer from 42, with the
  checkpoint epoch. A host before 42 has no epoch nor boundary, so this
  machine's messages to it go without the 4 bytes (a batch's and each one's
  in it), and the host's are taken as of the one world it has.

A version is joined only when its facts are known (the built-in 11 to 45,
or past them a signed table's: delta_table.c) and each fact is one of
those this unit plays. Every other is still refused as a newer or older
version's, and a host of this build's own version is played as always.
*/

#include "cseries.h"
#include "bungie_net/common/message_header.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "networking/network_messages.h"
#include "network_distributed.h"
#include "network_legacy.h"
#include "delta_layout.h"
#include "delta_table.h"
#include "halo_port_limits.h"

#include <string.h>

enum
{
	/* this build's join request's bytes past 0x70, and where its version word is */
	LEGACY_JOIN_REQUEST_EXTRA = 236,
	LEGACY_JOIN_REQUEST_VERSION_AT = 234,
	/* the epoch's bytes in a distributed message's header */
	LEGACY_EPOCH_SIZE = 4,
	/* the epoch the messages of a host without one are taken as of */
	LEGACY_HOST_EPOCH = 1,
};

static struct
{
	/* the host joined, when not of this build's version (0: this build's) */
	unsigned version;
	struct delta_layout layout;
} legacy;

/* the facts of a version, the signed table's past the built-in ones: 1 if
it has them */
static int legacy_layout(unsigned version, struct delta_layout *layout)
{
	if (version > DELTA_LAYOUT_NEWEST + DELTA_LAYOUT_PRESUMED && version > delta_table_newest_version())
		return 0;
	if (!delta_layout_built_in(version, layout))
		return 0;
	delta_table_apply_layouts(layout);
	return 1;
}

/* whether this unit plays every fact of the layout */
static int legacy_layout_played(struct delta_layout const *layout)
{
	struct delta_layout ours;
	int bit;

	if (!delta_layout_built_in(HALO_PORT_NETWORK_VERSION, &ours))
		return 0;
	if (layout->join_request_extra != 0 && layout->join_request_extra != LEGACY_JOIN_REQUEST_EXTRA - 2 &&
		!(layout->join_request_extra == LEGACY_JOIN_REQUEST_EXTRA &&
			layout->join_request_version_at == LEGACY_JOIN_REQUEST_VERSION_AT))
	{
		return 0;
	}
	if (layout->join_request_extra == LEGACY_JOIN_REQUEST_EXTRA - 2 && layout->join_request_version_at >= 0)
		return 0;
	if (layout->header_extra != 0 && !(layout->header_extra == ours.header_extra && layout->epoch_kind == ours.epoch_kind))
		return 0;
	if (layout->settings_size > HALO_PORT_NETWORK_GAME_SIZE || layout->options_size > ours.options_size)
		return 0;
	/* (a unit state part this build doesn't read, or reads in another place) */
	for (bit = 0; bit < 8; bit++)
	{
		if (layout->unit_part_size[bit] &&
			(layout->unit_part_size[bit] != ours.unit_part_size[bit] || layout->unit_part_at[bit] != ours.unit_part_at[bit]))
		{
			return 0;
		}
	}
	return 1;
}

boolean network_legacy_joinable(unsigned version)
{
	struct delta_layout layout;

	if (version == HALO_PORT_NETWORK_VERSION)
		return TRUE;
	return legacy_layout(version, &layout) && legacy_layout_played(&layout);
}

void network_legacy_set_host(unsigned version)
{
	if (version != HALO_PORT_NETWORK_VERSION && legacy_layout(version, &legacy.layout) &&
		legacy_layout_played(&legacy.layout))
	{
		legacy.version = version;
		network_event("legacy: playing the host by network version %u's layout (a record of %u bytes, %u bytes "
			"past the join request's 0x70, %u past a distributed header's 8)", version, legacy.layout.settings_size,
			legacy.layout.join_request_extra, legacy.layout.header_extra);
	}
	else
	{
		legacy.version = 0;
	}
}

unsigned network_legacy_host_version(void)
{
	return legacy.version ? legacy.version : HALO_PORT_NETWORK_VERSION;
}

boolean network_legacy_host_without_epoch(void)
{
	return legacy.version && !legacy.layout.header_extra;
}

long network_legacy_join_request_cut(void)
{
	if (!legacy.version)
		return 0;
	return LEGACY_JOIN_REQUEST_EXTRA - (long)legacy.layout.join_request_extra;
}

long network_legacy_settings_size(void)
{
	return legacy.version ? (long)legacy.layout.settings_size : 0;
}

boolean network_legacy_settings_to_native(void const *record, long size, void *game, long game_size)
{
	struct delta_layout ours;

	if (!legacy.version || size != (long)legacy.layout.settings_size || game_size != HALO_PORT_NETWORK_GAME_SIZE ||
		!delta_layout_built_in(HALO_PORT_NETWORK_VERSION, &ours))
	{
		return FALSE;
	}
	/* (this build's record: 44's, with its options 0x58 bytes, OpenCE's 0x2C
	and its own 0x2C, which an OpenCE host's game leaves zero) */
	return delta_layout_convert((uint8_t const *)record, (size_t)size, legacy.layout.options_size, (uint8_t *)game,
		(size_t)game_size, HALO_PORT_NETWORK_GAME_SIZE - DELTA_LAYOUT_HEAD_SIZE - DELTA_LAYOUT_LOCAL_DATA_SIZE -
			delta_layout_trailing_size(&ours), NULL) == 0;
}

/* ---------- the distributed messages' headers */

/* a message from this machine to a host without the epoch: the 4 bytes
left out of its header and, in a batch, out of each message's */
word network_legacy_outgoing(void *message, word size)
{
	byte *bytes = (byte *)message;
	struct distributed_message_header header;
	word read, written;

	if (!network_legacy_host_without_epoch() || size < sizeof(header))
		return size;
	csmemcpy(&header, bytes, sizeof(header));
	written = sizeof(header) - LEGACY_EPOCH_SIZE;
	read = sizeof(header);
	if (header.type == _distributed_message_batch)
	{
		while (read + sizeof(word) <= size)
		{
			word length;

			csmemcpy(&length, bytes + read, sizeof(word));
			if (length < sizeof(header) - sizeof(message_header) || read + sizeof(word) + length > size)
				break;
			length -= LEGACY_EPOCH_SIZE;
			csmemcpy(bytes + written, &length, sizeof(word));
			/* (its type, count and time, then its entries past the epoch) */
			memmove(bytes + written + sizeof(word), bytes + read + sizeof(word), 6);
			memmove(bytes + written + sizeof(word) + 6, bytes + read + sizeof(word) + 6 + LEGACY_EPOCH_SIZE,
				(size_t)(length - 6));
			written += (word)(sizeof(word) + length);
			read += (word)(sizeof(word) + length + LEGACY_EPOCH_SIZE);
		}
	}
	else
	{
		memmove(bytes + written, bytes + read, (size_t)(size - read));
		written += (word)(size - read);
	}
	build_message_header((word *)bytes, written, GET_MESSAGE_TYPE(header.header), GET_MESSAGE_FLAGS(header.header));
	return written;
}

/* a message from a host without the epoch, in this build's layout (the
epoch of the one world such a host has) into buffer; its size, 0 if it is
not one */
word network_legacy_incoming(word const *message, word size, void *buffer, word buffer_size)
{
	byte const *bytes = (byte const *)message;
	byte *out = (byte *)buffer;
	byte type;
	unsigned long epoch = LEGACY_HOST_EPOCH;
	word read, written;
	message_header header_word;

	if (size < sizeof(struct distributed_message_header) - LEGACY_EPOCH_SIZE ||
		size + LEGACY_EPOCH_SIZE > buffer_size)
	{
		return 0;
	}
	csmemcpy(&header_word, bytes, sizeof(header_word));
	type = bytes[sizeof(message_header)];
	read = sizeof(struct distributed_message_header) - LEGACY_EPOCH_SIZE;
	csmemcpy(out, bytes, read);
	csmemcpy(out + read, &epoch, LEGACY_EPOCH_SIZE);
	written = sizeof(struct distributed_message_header);
	if (type == _distributed_message_batch)
	{
		while (read + sizeof(word) <= size)
		{
			word length;

			csmemcpy(&length, bytes + read, sizeof(word));
			if (length < 6 || read + sizeof(word) + length > size ||
				written + sizeof(word) + length + LEGACY_EPOCH_SIZE > buffer_size)
			{
				break;
			}
			length += LEGACY_EPOCH_SIZE;
			csmemcpy(out + written, &length, sizeof(word));
			csmemcpy(out + written + sizeof(word), bytes + read + sizeof(word), 6);
			csmemcpy(out + written + sizeof(word) + 6, &epoch, LEGACY_EPOCH_SIZE);
			csmemcpy(out + written + sizeof(word) + 6 + LEGACY_EPOCH_SIZE, bytes + read + sizeof(word) + 6,
				(size_t)(length - LEGACY_EPOCH_SIZE - 6));
			written += (word)(sizeof(word) + length);
			read += (word)(sizeof(word) + length - LEGACY_EPOCH_SIZE);
		}
	}
	else
	{
		csmemcpy(out + written, bytes + read, (size_t)(size - read));
		written += (word)(size - read);
	}
	build_message_header((word *)out, written, GET_MESSAGE_TYPE(header_word), GET_MESSAGE_FLAGS(header_word));
	return written;
}
