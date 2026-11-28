/* The host's tag table, as a client has it (network_tag_table.h). */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cseries.h"
#include "cseries/errors.h"
#include "bungie_net/common/message_header.h"
#include "game/game.h"
#include "networking/network_game_globals.h"

#include "network_distributed.h"
#include "network_replay.h"
#include "network_tag_table.h"

/* cache_files.c's: the level's table and its count (NULL, 0 when none) */
extern void *cache_files_tag_instances(long *count);

/* ---------- constants */

enum
{
	/* a message's tags: its header, four longs and this many entries within
	MAXIMUM_MESSAGE_SIZE */
	TAG_TABLE_ENTRIES_PER_MESSAGE = 500,
	/* the most a table may hold: an absolute index is a short
	(tag_index_is_group) */
	TAG_TABLE_MAXIMUM_TAGS = 0x7FFF,
	/* the tags logged that differ, of each side */
	TAG_TABLE_LOGGED_TAGS = 8,
};

/* ---------- structures */

/* a tag of the level's table (cache_files.c's) */
struct table_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

typedef char table_tag_instance_size_assert[sizeof(struct table_tag_instance) == 0x20 ? 1 : -1];

struct tag_table_entry
{
	unsigned long key;
	long datum;
};

typedef char tag_table_entry_size_assert[sizeof(struct tag_table_entry) == 8 ? 1 : -1];

/* a part of the host's table: entries first to first + count of total */
struct tag_table_message
{
	struct distributed_message_header header;
	long total;
	unsigned long fingerprint;
	long first;
	long count;
	struct tag_table_entry entries[TAG_TABLE_ENTRIES_PER_MESSAGE];
};

#define TAG_TABLE_MESSAGE_HEAD_SIZE (offsetof(struct tag_table_message, entries))

typedef char tag_table_message_size_assert[sizeof(struct tag_table_message) <= MAXIMUM_MESSAGE_SIZE ? 1 : -1];

/* a key and the absolute index it is of, sorted by key, then index */
struct tag_table_pair
{
	unsigned long key;
	long index;
};

/* ---------- globals */

/* (a client) the host's table, as it comes, and what it means here */
static struct
{
	long host_count;
	unsigned long host_fingerprint;
	long received;
	struct tag_table_entry *host;
	boolean complete;
	boolean translating;
	/* the host's absolute index: the datum here (NONE: not here) */
	long *host_to_local;
	/* the absolute index here: the host's datum (NONE: not the host's) */
	long *local_to_host;
	long local_count;
	long found;
	long missing;
} tag_table;

/* ---------- private code */

/* a tag's key: its group and its path (FNV-1a), as the tags' fingerprint
hashes each (tag_import_tag_table_fingerprint) */
static unsigned long tag_table_key(
	struct table_tag_instance const *instance)
{
	unsigned long value = 0x811C9DC5UL;
	char const *name = instance->name;
	short byte_index;

	for (byte_index = 0; byte_index < 4; byte_index++)
		value = ((value ^ ((unsigned long)instance->group_tag >> (byte_index * 8) & 0xFF)) * 16777619UL) & 0xFFFFFFFFUL;
	for (; name && *name; name++)
		value = ((value ^ (unsigned char)*name) * 16777619UL) & 0xFFFFFFFFUL;
	return ((value ^ 0xFF) * 16777619UL) & 0xFFFFFFFFUL;
}

static unsigned long tag_table_fingerprint_add(
	unsigned long value,
	unsigned long word_value)
{
	short byte_index;

	for (byte_index = 0; byte_index < 4; byte_index++)
		value = ((value ^ (word_value >> (byte_index * 8) & 0xFF)) * 16777619UL) & 0xFFFFFFFFUL;
	return value;
}

static int tag_table_pair_compare(
	void const *a,
	void const *b)
{
	struct tag_table_pair const *left = (struct tag_table_pair const *)a;
	struct tag_table_pair const *right = (struct tag_table_pair const *)b;

	if (left->key != right->key)
		return left->key < right->key ? -1 : 1;
	return left->index < right->index ? -1 : left->index > right->index ? 1 : 0;
}

static void tag_table_free(
	void)
{
	free(tag_table.host);
	free(tag_table.host_to_local);
	free(tag_table.local_to_host);
	memset(&tag_table, 0, sizeof(tag_table));
}

/* the host's table, all of it come: its tags found here by key. Keys alike
on one side (two tags of one path and group, or two whose hashes meet) are
matched in their order, the first the host has of a key with the first here */
static void tag_table_build(
	void)
{
	long local_count;
	struct table_tag_instance const *instances =
		(struct table_tag_instance const *)cache_files_tag_instances(&local_count);
	struct tag_table_pair *host_pairs = NULL;
	struct tag_table_pair *local_pairs = NULL;
	long index;
	long host_index, local_index;
	long logged;
	long same_until;

	tag_table.complete = TRUE;
	if (!instances || local_count <= 0)
	{
		error(_error_silent, "network tags: the host's table came (%ld tags), and this machine has none loaded",
			tag_table.host_count);
		return;
	}
	if (local_count > TAG_TABLE_MAXIMUM_TAGS)
		local_count = TAG_TABLE_MAXIMUM_TAGS;
	for (same_until = 0; same_until < local_count && same_until < tag_table.host_count; same_until++)
	{
		if (tag_table.host[same_until].key != tag_table_key(&instances[same_until]) ||
			tag_table.host[same_until].datum != instances[same_until].tag_index)
		{
			break;
		}
	}
	if (same_until == local_count && same_until == tag_table.host_count)
	{
		error(_error_silent, "network tags: the host's table is this machine's (%ld tags, %08lx)",
			tag_table.host_count, tag_table.host_fingerprint);
		return;
	}

	host_pairs = (struct tag_table_pair *)malloc((size_t)tag_table.host_count * sizeof(*host_pairs));
	local_pairs = (struct tag_table_pair *)malloc((size_t)local_count * sizeof(*local_pairs));
	tag_table.host_to_local = (long *)malloc((size_t)tag_table.host_count * sizeof(long));
	tag_table.local_to_host = (long *)malloc((size_t)local_count * sizeof(long));
	if (!host_pairs || !local_pairs || !tag_table.host_to_local || !tag_table.local_to_host)
	{
		free(host_pairs);
		free(local_pairs);
		free(tag_table.host_to_local);
		free(tag_table.local_to_host);
		tag_table.host_to_local = NULL;
		tag_table.local_to_host = NULL;
		error(_error_silent, "network tags: no memory for the host's table (%ld tags): its tags are taken as this "
			"machine's", tag_table.host_count);
		return;
	}
	for (index = 0; index < tag_table.host_count; index++)
	{
		host_pairs[index].key = tag_table.host[index].key;
		host_pairs[index].index = index;
		tag_table.host_to_local[index] = NONE;
	}
	for (index = 0; index < local_count; index++)
	{
		local_pairs[index].key = tag_table_key(&instances[index]);
		local_pairs[index].index = index;
		tag_table.local_to_host[index] = NONE;
	}
	qsort(host_pairs, (size_t)tag_table.host_count, sizeof(*host_pairs), tag_table_pair_compare);
	qsort(local_pairs, (size_t)local_count, sizeof(*local_pairs), tag_table_pair_compare);
	host_index = 0;
	local_index = 0;
	while (host_index < tag_table.host_count && local_index < local_count)
	{
		if (host_pairs[host_index].key < local_pairs[local_index].key)
			host_index++;
		else if (host_pairs[host_index].key > local_pairs[local_index].key)
			local_index++;
		else
		{
			long host_absolute = host_pairs[host_index].index;
			long local_absolute = local_pairs[local_index].index;

			tag_table.host_to_local[host_absolute] = instances[local_absolute].tag_index;
			tag_table.local_to_host[local_absolute] = tag_table.host[host_absolute].datum;
			host_index++;
			local_index++;
		}
	}
	free(host_pairs);
	free(local_pairs);
	tag_table.local_count = local_count;
	tag_table.translating = TRUE;
	tag_table.found = 0;
	tag_table.missing = 0;
	for (index = 0; index < tag_table.host_count; index++)
	{
		if (tag_table.host_to_local[index] != NONE)
			tag_table.found++;
		else
			tag_table.missing++;
	}

	error(_error_silent, "network tags: the host's table differs from this machine's from index %ld (%ld tags, "
		"%08lx there; %ld here): %ld of its tags found here by group and path, %ld not",
		same_until, tag_table.host_count, tag_table.host_fingerprint, local_count, tag_table.found,
		tag_table.missing);
	logged = 0;
	for (index = 0; index < tag_table.host_count && logged < TAG_TABLE_LOGGED_TAGS; index++)
	{
		if (tag_table.host_to_local[index] == NONE)
		{
			error(_error_silent, "network tags: the host's tag #%ld (%08lx, key %08lx) is not here", index,
				(unsigned long)tag_table.host[index].datum, tag_table.host[index].key);
			logged++;
		}
	}
	logged = 0;
	for (index = 0; index < local_count && logged < TAG_TABLE_LOGGED_TAGS; index++)
	{
		if (tag_table.local_to_host[index] == NONE)
		{
			char group[5];
			short byte_index;

			for (byte_index = 0; byte_index < 4; byte_index++)
			{
				char c = (char)((unsigned long)instances[index].group_tag >> ((3 - byte_index) * 8) & 0xFF);

				group[byte_index] = c >= 32 && c <= 126 ? c : '?';
			}
			group[4] = 0;
			error(_error_silent, "network tags: this machine's tag #%ld (%s %s) is not the host's", index, group,
				instances[index].name ? instances[index].name : "with no name");
			logged++;
		}
	}
}

/* ---------- public code */

void network_tag_table_reset(
	void)
{
	tag_table_free();
}

void network_tag_table_send(
	long machine_index)
{
	struct tag_table_message message;
	long count;
	struct table_tag_instance const *instances;
	unsigned long fingerprint = 0x811C9DC5UL;
	long index;
	long first;
	short parts = 0;

	if (game_connection() != _game_connection_network_server || machine_index == NONE)
		return;
	instances = (struct table_tag_instance const *)cache_files_tag_instances(&count);
	if (!instances || count <= 0)
		return;
	if (count > TAG_TABLE_MAXIMUM_TAGS)
		count = TAG_TABLE_MAXIMUM_TAGS;
	for (index = 0; index < count; index++)
	{
		fingerprint = tag_table_fingerprint_add(fingerprint, tag_table_key(&instances[index]));
		fingerprint = tag_table_fingerprint_add(fingerprint, (unsigned long)instances[index].tag_index);
	}
	for (first = 0; first < count; first += TAG_TABLE_ENTRIES_PER_MESSAGE)
	{
		long part_count = MIN(TAG_TABLE_ENTRIES_PER_MESSAGE, count - first);

		memset(&message, 0, sizeof(message));
		message.total = count;
		message.fingerprint = fingerprint;
		message.first = first;
		message.count = part_count;
		for (index = 0; index < part_count; index++)
		{
			message.entries[index].key = tag_table_key(&instances[first + index]);
			message.entries[index].datum = instances[first + index].tag_index;
		}
		distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_tag_table, 1,
			(word)(TAG_TABLE_MESSAGE_HEAD_SIZE + part_count * sizeof(struct tag_table_entry)));
		parts++;
	}
	error(_error_silent, "network tags: the level's table sent to machine %ld (%ld tags, %08lx, %d messages)",
		machine_index, count, fingerprint, parts);
}

void network_tag_table_record(
	boolean client)
{
	struct tag_table_message message;
	long first;

	if (!client)
	{
		network_tag_table_send(DISTRIBUTED_RECORDER_MACHINE);
		return;
	}
	if (!tag_table.complete || !tag_table.host)
		return;
	for (first = 0; first < tag_table.host_count; first += TAG_TABLE_ENTRIES_PER_MESSAGE)
	{
		long part_count = MIN(TAG_TABLE_ENTRIES_PER_MESSAGE, tag_table.host_count - first);
		word size = (word)(TAG_TABLE_MESSAGE_HEAD_SIZE + part_count * sizeof(struct tag_table_entry));

		memset(&message, 0, sizeof(message));
		build_message_header(&message.header.header, size, 2, 0);
		message.header.type = _distributed_message_tag_table;
		message.header.count = 1;
		message.header.game_time = game_time_get();
		message.total = tag_table.host_count;
		message.fingerprint = tag_table.host_fingerprint;
		message.first = first;
		message.count = part_count;
		memcpy(message.entries, &tag_table.host[first], (size_t)part_count * sizeof(struct tag_table_entry));
		network_replay_record(_network_replay_reliable, &message, size);
	}
	error(_error_silent, "network tags: the host's table (%ld tags) written to this machine's replay",
		tag_table.host_count);
}

boolean network_tag_table_handles_message(
	unsigned short const *message,
	unsigned short size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	memcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_tag_table;
}

void network_tag_table_handle_message(
	long machine_index,
	unsigned short const *data,
	unsigned short size)
{
	struct tag_table_message message;

	if (machine_index != NONE || !network_game_distributed_client() || size < TAG_TABLE_MESSAGE_HEAD_SIZE ||
		size > sizeof(message))
	{
		return;
	}
	memset(&message, 0, sizeof(message));
	memcpy(&message, data, size);
	if (GET_MESSAGE_TYPE(message.header.header) != 2 || message.count < 0 ||
		message.count > TAG_TABLE_ENTRIES_PER_MESSAGE ||
		size != TAG_TABLE_MESSAGE_HEAD_SIZE + message.count * sizeof(struct tag_table_entry) ||
		message.total <= 0 || message.total > TAG_TABLE_MAXIMUM_TAGS || message.first < 0 ||
		message.first + message.count > message.total)
	{
		return;
	}
	/* (a table's first part: a new one, whatever came before) */
	if (message.first == 0)
	{
		tag_table_free();
		tag_table.host = (struct tag_table_entry *)malloc((size_t)message.total * sizeof(*tag_table.host));
		if (!tag_table.host)
		{
			error(_error_silent, "network tags: no memory for the host's table (%ld tags)", message.total);
			return;
		}
		tag_table.host_count = message.total;
		tag_table.host_fingerprint = message.fingerprint;
	}
	/* (its parts come in order, over the stream: one out of it is another
	table's, or one begun before this machine was listening) */
	if (!tag_table.host || tag_table.complete || message.first != tag_table.received ||
		message.total != tag_table.host_count || message.fingerprint != tag_table.host_fingerprint)
	{
		return;
	}
	memcpy(&tag_table.host[message.first], message.entries, (size_t)message.count * sizeof(struct tag_table_entry));
	tag_table.received += message.count;
	if (tag_table.received == tag_table.host_count)
		tag_table_build();
}

boolean network_tag_table_translating(
	void)
{
	return tag_table.translating;
}

boolean network_tag_table_differs(
	long *host_count,
	long *found,
	long *missing)
{
	if (!tag_table.translating)
		return FALSE;
	*host_count = tag_table.host_count;
	*found = tag_table.found;
	*missing = tag_table.missing;
	return TRUE;
}

long network_tag_from_host(
	long tag_index)
{
	long absolute;

	if (!tag_table.translating || tag_index == NONE)
		return tag_index;
	absolute = (long)(unsigned short)(tag_index & 0xFFFF);
	/* (an index of none of the host's tags, or not as the host has it: none) */
	if (absolute >= tag_table.host_count || tag_table.host[absolute].datum != tag_index)
		return NONE;
	return tag_table.host_to_local[absolute];
}

long network_tag_to_host(
	long tag_index)
{
	long absolute;
	long count;
	struct table_tag_instance const *instances;

	if (!tag_table.translating || tag_index == NONE)
		return tag_index;
	absolute = (long)(unsigned short)(tag_index & 0xFFFF);
	instances = (struct table_tag_instance const *)cache_files_tag_instances(&count);
	if (absolute >= tag_table.local_count || !instances || absolute >= count ||
		instances[absolute].tag_index != tag_index)
	{
		return NONE;
	}
	return tag_table.local_to_host[absolute];
}
