/*
DELTA_TABLE.C

Delta's signed legacy table (delta-spec's versioning.md, "The signed
table"): the OpenCE network versions Delta's builds are proven to play, and
the layout facts of versions past the ones built in (network_legacy.c),
which follow OpenCE's raises without a release.

- Fetched in the background at start and every four hours (thirty minutes
  on a dedicated server, and thirty minutes after a failure), from
  network.delta_list's /v1/delta/legacy and /v1/delta/legacy.sig, and if
  that gives no valid table from the delta-table branch on GitHub. Nothing
  waits for it: until one arrives the game plays its built-in facts.
  network.delta_list empty, or network.delta off, fetches nothing.
- Checked before a byte is read: at most 16,384 bytes of document, its
  Ed25519 signature (RFC 8032, 128 hex digits) by one of the keys below, the
  serial's epoch (its top byte) no later than that key's last, then a strict
  parse (nesting at most eight deep, no key twice in an object, whole
  numbers where numbers are read), format 1, and a serial (1 to 0xFFFFFFFE)
  newer than the table taken before: an older one can't roll this machine
  back. A table that fails any of it is dropped with one line in the log.
- Kept as delta_legacy.signed in the save root (the signature's 128 hex
  digits, a line feed, then the document), and read at start, checked
  again like any other copy.

What this build takes from it: the layout facts of the versions past those
built in (delta_layout.h's), from the newest wire's row, through
delta_layout_of, within its limits (versioning.md, "Playing a range"). Rows
are by wire, proofs for that wire's builds, which a port has none of, so
their numbers never set this build's range.
*/

#include "platform.h"
#include "port_config.h"
#include "master_list.h"
#include "server_json.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "../game/delta_layout.h"
#include "../game/delta_table.h" /* (and delta_layout.h, the spec's, MIT) */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int halo_server_mode(void);

enum
{
	TABLE_DOCUMENT_MAXIMUM = 16384,
	TABLE_SIGNATURE_HEX = 128,
	TABLE_DEPTH_MAXIMUM = 8,
	TABLE_FACTS_MAXIMUM = 64,
	TABLE_EVERY_SECONDS = 4 * 60 * 60,
	TABLE_SERVER_EVERY_SECONDS = 30 * 60,
	TABLE_RETRY_SECONDS = 30 * 60,
	TABLE_SERIAL_NONE = 0xFFFFFFFFu,
};

/* the public keys that sign tables (versioning.md, "The key"), and the last
epoch each signs */
static const struct
{
	const char *name;
	const char *hex;
	unsigned last_epoch;
} table_keys[] = {
	{ "primary", "d51bd85346bf889b6bc5aa21dcc7a43488657b64f2432e17f6e0e7fe976933ce", 0 },
	{ "recovery", "97cde84d8b9b6ba8a275d412b49422d4c07970653ee53ae54285e96640a5e456", 255 },
};

static const char github_url[] = "https://raw.githubusercontent.com/ChupathingyCE/chupathingyce/delta-table/legacy.json";

static struct
{
	pthread_mutex_t lock;
	int started;
	/* the table taken: its serial (0: none), and the facts past the
	built-in versions, oldest first */
	unsigned long serial;
	int fact_count;
	struct delta_layout_fact facts[TABLE_FACTS_MAXIMUM];
} table = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- reading */

static int hex_value(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex_bytes(const char *text, int digits, unsigned char *bytes)
{
	int index;

	for (index = 0; index < digits / 2; index++)
	{
		int high = hex_value((unsigned char)text[2 * index]), low = hex_value((unsigned char)text[2 * index + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

/* whether any object in the document has a key twice (or more keys than
are read) */
static int repeated_keys(struct server_json_document const *document)
{
	int token;

	for (token = 0; token < document->count; token++)
	{
		int keys[256];
		int count = document->tokens[token].size, index, other, key;

		if (document->tokens[token].type != _server_json_object)
			continue;
		if (count > (int)(sizeof(keys) / sizeof(keys[0])))
			return 1;
		for (index = 0, key = token + 1; index < count; index++, key = server_json_skip(document, key + 1))
		{
			if (key < 0 || key >= document->count)
				return 1;
			keys[index] = key;
		}
		for (index = 0; index < count; index++)
		{
			for (other = index + 1; other < count; other++)
			{
				struct server_json_token const *a = &document->tokens[keys[index]];
				struct server_json_token const *b = &document->tokens[keys[other]];

				if (a->end - a->start == b->end - b->start &&
					!memcmp(document->text + a->start, document->text + b->start, (size_t)(a->end - a->start)))
				{
					return 1;
				}
			}
		}
	}
	return 0;
}

/* whether no object or array in the text nests deeper than maximum (the
document itself is one; the reader counts keys as a level too) */
static int nesting_within(const char *text, int length, int maximum)
{
	int at, depth = 0, in_string = 0;

	for (at = 0; at < length; at++)
	{
		char c = text[at];

		if (in_string)
		{
			if (c == '\\')
				at++;
			else if (c == '"')
				in_string = 0;
		}
		else if (c == '"')
			in_string = 1;
		else if (c == '{' || c == '[')
		{
			if (++depth > maximum)
				return 0;
		}
		else if (c == '}' || c == ']')
			depth--;
	}
	return 1;
}

/* a whole number at the token, 0 to maximum: 1 if it is one */
static int whole(struct server_json_document const *document, int token, double maximum, unsigned long *value)
{
	double number;

	if (token < 0 || document->tokens[token].type != _server_json_primitive ||
		!server_json_get_number(document, token, &number) || number < 0 || number > maximum ||
		number != (double)(unsigned long)number)
	{
		return 0;
	}
	*value = (unsigned long)number;
	return 1;
}

/* a version's facts from a row's "layouts" member (the fields delta_layout.h
reads; others are skipped) */
static void fact_read(struct server_json_document const *document, int token, unsigned version,
	struct delta_layout_fact *fact)
{
	unsigned long value;
	int field, index;

	memset(fact, 0, sizeof(*fact));
	fact->version = version;
	if (whole(document, server_json_member(document, token, "settings_size"), 65535, &value))
		fact->settings_size = (unsigned)value;
	if (whole(document, server_json_member(document, token, "options_size"), 65535, &value))
		fact->options_size = (unsigned)value;
	if (whole(document, server_json_member(document, token, "shield_effects_maximum"), 255, &value))
		fact->shield_effects_maximum = (unsigned)value;
	if (whole(document, server_json_member(document, token, "join_request_extra"), 65535, &value))
	{
		fact->join_request_extra = (unsigned)value;
		fact->join_request_said = 1;
	}
	if (whole(document, server_json_member(document, token, "join_request_version_at"), 65535, &value))
	{
		fact->join_request_version_at = (unsigned)value;
		fact->join_request_version_said = 1;
	}
	{
		char fill[16];
		int field_fill = server_json_member(document, token, "join_request_fill");

		if (field_fill >= 0)
			fact->join_request_fill = server_json_get_string(document, field_fill, fill, sizeof(fill)) &&
				!strcmp(fill, "zeros") ? DELTA_LAYOUT_FILL_ZEROS : DELTA_LAYOUT_FILL_UNKNOWN;
	}
	if (whole(document, server_json_member(document, token, "header_extra"), 255, &value))
		fact->header_extra = (unsigned)value;
	if (whole(document, server_json_member(document, token, "epoch_kind"), 255, &value))
		fact->epoch_kind = (unsigned)value;
	if (whole(document, server_json_member(document, token, "rider_ejection_up"), 65535, &value))
		fact->rider_ejection_up = (unsigned)value;
	/* (every value bounded before it is kept, whatever delta_layout_apply
checks after: a field past its limit is not taken) */
	if (fact->settings_size > DELTA_LAYOUT_SETTINGS_MAXIMUM)
		fact->settings_size = 0;
	if (fact->options_size > DELTA_LAYOUT_OPTIONS_MAXIMUM)
		fact->options_size = 0;
	if (fact->shield_effects_maximum > 15)
		fact->shield_effects_maximum = 0;
	if (fact->join_request_said && fact->join_request_extra > 1024)
		fact->join_request_said = fact->join_request_extra = 0;
	if (fact->join_request_version_said && (fact->join_request_version_at > 1022 ||
		(fact->join_request_said && fact->join_request_version_at + 2 > fact->join_request_extra)))
	{
		fact->join_request_version_said = 0;
		fact->join_request_version_at = 0;
	}
	if (fact->header_extra > 8)
		fact->header_extra = 0;
	if (fact->epoch_kind && (fact->epoch_kind < 93 || fact->epoch_kind > 255))
		fact->epoch_kind = 0;
	if (fact->rider_ejection_up > 999)
		fact->rider_ejection_up = 0;
	field = server_json_member(document, token, "dropped_messages");
	for (index = 0; field >= 0 && document->tokens[field].type == _server_json_array &&
		index < document->tokens[field].size; index++)
	{
		if (whole(document, server_json_element(document, field, index), 255, &value) && value)
			fact->dropped[value / 8] |= (uint8_t)(1u << (value % 8));
	}
	field = server_json_member(document, token, "unit_state_parts");
	for (index = 0; field >= 0 && document->tokens[field].type == _server_json_array &&
		index < document->tokens[field].size; index++)
	{
		int part = server_json_element(document, field, index);
		unsigned long bit, size;
		char at[16];

		if (part < 0 || !whole(document, server_json_member(document, part, "bit"), 7, &bit) || bit < 6 ||
			!whole(document, server_json_member(document, part, "size"), 64, &size) || !size ||
			!server_json_get_string(document, server_json_member(document, part, "at"), at, sizeof(at)))
		{
			continue;
		}
		fact->unit_part_size[bit] = (uint8_t)size;
		fact->unit_part_at[bit] = !strcmp(at, "after_up") ? DELTA_LAYOUT_PART_AFTER_UP :
			!strcmp(at, "last") ? DELTA_LAYOUT_PART_LAST : 0;
	}
}

/* a wire's ID as an order: its number, then its letter (0: not one) */
static long wire_order(struct server_json_document const *document, int token)
{
	char wire[16];
	unsigned number = 0;
	int at;

	if (!server_json_get_string(document, token, wire, sizeof(wire)) || strncmp(wire, "chupa-", 6))
		return 0;
	for (at = 6; wire[at] >= '0' && wire[at] <= '9' && number < 100000; at++)
		number = number * 10 + (unsigned)(wire[at] - '0');
	if (at == 6 || wire[at] < 'a' || wire[at] > 'z' || wire[at + 1])
		return 0;
	return (long)number * 32 + (wire[at] - 'a' + 1);
}

/* the document read: 1 with what this build takes from it, the newest
wire's name and the layout facts of its row past the built-in versions */
static int document_read(const char *text, int length, unsigned long *serial, char *newest_wire, int wire_size,
	struct delta_layout_fact *facts, int *fact_count)
{
	struct server_json_document document;
	unsigned long value;
	int wires, key, member, ok = 0;
	int best_row = -1;
	long best_order = 0;

	if (!nesting_within(text, length, TABLE_DEPTH_MAXIMUM) ||
		!server_json_parse(&document, text, length, 2 * TABLE_DEPTH_MAXIMUM))
	{
		return 0;
	}
	if (document.count < 1 || document.tokens[0].type != _server_json_object || repeated_keys(&document) ||
		!whole(&document, server_json_member(&document, 0, "delta_legacy"), 1, &value) || value != 1 ||
		!whole(&document, server_json_member(&document, 0, "serial"), 4294967294.0, serial) || !*serial)
	{
		goto done;
	}
	*fact_count = 0;
	newest_wire[0] = 0;
	/* (rows are by wire, proofs for that wire's builds: of them this build
	takes only the facts about OpenCE's hosts, the same for every reader,
	from the newest wire's row) */
	wires = server_json_member(&document, 0, "wires");
	if (wires >= 0 && document.tokens[wires].type == _server_json_object)
	{
		for (member = 0, key = wires + 1; member < document.tokens[wires].size;
			member++, key = server_json_skip(&document, key + 1))
		{
			long order = wire_order(&document, key);

			if (order > best_order && document.tokens[key + 1].type == _server_json_object)
			{
				best_order = order;
				best_row = key + 1;
				server_json_get_string(&document, key, newest_wire, wire_size);
			}
		}
	}
	if (best_row >= 0)
	{
		int layouts = server_json_member(&document, best_row, "layouts");
		unsigned version;

		/* (by version, oldest first: each a key of the object) */
		for (version = DELTA_LAYOUT_NEWEST + 1; layouts >= 0 && document.tokens[layouts].type == _server_json_object &&
			version <= 0xFFFF && *fact_count < TABLE_FACTS_MAXIMUM; version++)
		{
			char name[8];
			int fact;

			snprintf(name, sizeof(name), "%u", version);
			fact = server_json_member(&document, layouts, name);
			if (fact >= 0 && document.tokens[fact].type == _server_json_object)
				fact_read(&document, fact, version, &facts[(*fact_count)++]);
		}
	}
	ok = 1;
done:
	server_json_document_free(&document);
	return ok;
}

/* a signed table (signature, line feed, document) checked and taken; source
names it in the log. 1 if taken */
static int table_take(const char *signed_table, unsigned long size, const char *source, int keep)
{
	unsigned char signature[64];
	const char *document;
	unsigned long document_size, serial = 0;
	char newest[16];
	int fact_count = 0, key, taken = 0;
	struct delta_layout_fact facts[TABLE_FACTS_MAXIMUM];

	if (size < TABLE_SIGNATURE_HEX + 1 || signed_table[TABLE_SIGNATURE_HEX] != '\n')
		return 0;
	document = signed_table + TABLE_SIGNATURE_HEX + 1;
	document_size = size - TABLE_SIGNATURE_HEX - 1;
	if (document_size > TABLE_DOCUMENT_MAXIMUM || !hex_bytes(signed_table, TABLE_SIGNATURE_HEX, signature))
	{
		platform_log("Delta: the legacy table from %s is not a signed table: dropped", source);
		return 0;
	}
	for (key = 0; key < (int)(sizeof(table_keys) / sizeof(table_keys[0])); key++)
	{
		unsigned char public_key[32];

		if (hex_bytes(table_keys[key].hex, 64, public_key) &&
			crypto_ed25519_check(signature, public_key, (const unsigned char *)document, document_size) == 0)
		{
			break;
		}
	}
	if (key == (int)(sizeof(table_keys) / sizeof(table_keys[0])))
	{
		platform_log("Delta: the legacy table from %s does not verify: dropped", source);
		return 0;
	}
	if (!document_read(document, (int)document_size, &serial, newest, sizeof(newest), facts, &fact_count))
	{
		platform_log("Delta: the legacy table from %s does not read as one: dropped", source);
		return 0;
	}
	if ((serial >> 24) > table_keys[key].last_epoch)
	{
		platform_log("Delta: the legacy table from %s is of epoch %lu, past the %s key's: dropped", source,
			serial >> 24, table_keys[key].name);
		return 0;
	}
	pthread_mutex_lock(&table.lock);
	if (serial > table.serial)
	{
		table.serial = serial;
		table.fact_count = fact_count;
		memcpy(table.facts, facts, sizeof(facts[0]) * (size_t)fact_count);
		taken = 1;
	}
	pthread_mutex_unlock(&table.lock);
	if (!taken)
		return 0;
	platform_log("Delta: legacy table %lu from %s (the %s key's): %d layout facts past network version %d, "
		"from %s's row", serial, source, table_keys[key].name, fact_count, DELTA_LAYOUT_NEWEST, newest[0] ? newest : "no");
	if (keep)
	{
		char path[1024];
		FILE *file;

		snprintf(path, sizeof(path), "%s/delta_legacy.signed", platform_save_root());
		file = fopen(path, "wb");
		if (file)
		{
			fwrite(signed_table, 1, (size_t)size, file);
			fclose(file);
		}
	}
	return 1;
}

/* ---------- fetching */

/* the two files at url and url.sig, as one signed table: 1 and the table
(the caller's to free) */
static int fetch(const char *url, char **signed_table, unsigned long *size)
{
	char sig_url[600];
	char *document = NULL, *signature = NULL;
	unsigned long document_size = 0, signature_size = 0;
	int status = 0, ok = 0;

	snprintf(sig_url, sizeof(sig_url), "%s.sig", url);
	if (!master_list_https_get(url, &status, &document, &document_size) || status != 200)
		goto done;
	if (!master_list_https_get(sig_url, &status, &signature, &signature_size) || status != 200 ||
		signature_size < TABLE_SIGNATURE_HEX || document_size > TABLE_DOCUMENT_MAXIMUM)
	{
		goto done;
	}
	*signed_table = malloc(TABLE_SIGNATURE_HEX + 1 + document_size);
	if (!*signed_table)
		goto done;
	memcpy(*signed_table, signature, TABLE_SIGNATURE_HEX);
	(*signed_table)[TABLE_SIGNATURE_HEX] = '\n';
	memcpy(*signed_table + TABLE_SIGNATURE_HEX + 1, document, document_size);
	*size = TABLE_SIGNATURE_HEX + 1 + document_size;
	ok = 1;
done:
	if (document)
		master_list_https_free(document);
	if (signature)
		master_list_https_free(signature);
	return ok;
}

static int fetch_and_take(const char *url, const char *source)
{
	char *signed_table = NULL;
	unsigned long size = 0;
	int taken;

	if (!fetch(url, &signed_table, &size))
		return 0;
	taken = table_take(signed_table, size, source, 1);
	free(signed_table);
	return taken;
}

static void *table_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		char url[512];
		const char *list = config_string("network.delta_list");
		int reached = 0;
		unsigned wait;

		if (config_boolean("network.delta") && list && *list)
		{
			snprintf(url, sizeof(url), "%s/v1/delta/legacy", list);
			reached = fetch_and_take(url, "Delta List");
			if (!reached)
				reached = fetch_and_take(github_url, "GitHub");
		}
		wait = !reached ? TABLE_RETRY_SECONDS : halo_server_mode() ? TABLE_SERVER_EVERY_SECONDS : TABLE_EVERY_SECONDS;
		{
			struct timespec rest = { (time_t)wait, 0 };

			nanosleep(&rest, NULL);
		}
	}
	return NULL;
}

static void table_start(void)
{
	char path[1024];
	FILE *file;
	pthread_t thread;

	pthread_mutex_lock(&table.lock);
	if (table.started)
	{
		pthread_mutex_unlock(&table.lock);
		return;
	}
	table.started = 1;
	pthread_mutex_unlock(&table.lock);
	/* (the cache, checked like any copy) */
	snprintf(path, sizeof(path), "%s/delta_legacy.signed", platform_save_root());
	file = fopen(path, "rb");
	if (file)
	{
		static char cached[TABLE_SIGNATURE_HEX + 1 + TABLE_DOCUMENT_MAXIMUM + 1];
		size_t size = fread(cached, 1, sizeof(cached), file);

		fclose(file);
		if (size < sizeof(cached))
			table_take(cached, (unsigned long)size, "the cache", 0);
	}
#if !defined(HALO_WEB)
	if (pthread_create(&thread, NULL, table_thread, NULL) == 0)
		pthread_detach(thread);
#else
	(void)thread;
#endif
}

/* ---------- public */

unsigned long delta_table_serial(void)
{
	unsigned long serial;

	table_start();
	pthread_mutex_lock(&table.lock);
	serial = table.serial;
	pthread_mutex_unlock(&table.lock);
	return serial;
}

int delta_table_layout_of(unsigned version, struct delta_layout *layout)
{
	struct delta_layout_fact facts[TABLE_FACTS_MAXIMUM];
	int count;

	table_start();
	pthread_mutex_lock(&table.lock);
	count = table.fact_count;
	memcpy(facts, table.facts, sizeof(facts[0]) * (size_t)count);
	pthread_mutex_unlock(&table.lock);
	return delta_layout_of(version, facts, count, layout, NULL);
}

int delta_table_check(const char *signed_table, unsigned long size)
{
	return table_take(signed_table, size, "a check", 0);
}
