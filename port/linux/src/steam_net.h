/*
STEAM_NET.H

What the Steam client sends and reads, below its messages: an HTTPS GET on
every native build (the desktop's update_https_request, the Android,
Switch and PS5 hosts' host_https_get), and protocol buffers, written and
read field by field (steam_cm.c's messages are Steam's, as SteamKit and
DepotDownloader document them).

The browser build has neither Steam's connection servers nor its content
servers (Steam refuses other sites' pages): steam_net_available says so.
*/

#ifndef STEAM_NET_H
#define STEAM_NET_H

#include <stddef.h>
#include <stdint.h>

/* whether this build can reach Steam at all */
int steam_net_available(void);

/* a GET of an https:// url, at most maximum bytes of its body (malloc'd,
NUL terminated past its size: the caller frees it); 1 with the status,
else 0 and why in error */
int steam_https_get(char const *url, unsigned long maximum, unsigned char **body, unsigned long *size, int *status,
	char *error, int error_size);

/* ---------- protocol buffers */

enum
{
	STEAM_PB_VARINT = 0,
	STEAM_PB_FIXED64 = 1,
	STEAM_PB_BYTES = 2,
	STEAM_PB_FIXED32 = 5
};

/* a message being written: bytes grow as needed; failed once memory ran out */
struct steam_pb
{
	unsigned char *bytes;
	size_t size;
	size_t capacity;
	int failed;
};

void steam_pb_free(struct steam_pb *pb);
void steam_pb_varint(struct steam_pb *pb, int field, uint64_t value);
void steam_pb_fixed64(struct steam_pb *pb, int field, uint64_t value);
void steam_pb_fixed32(struct steam_pb *pb, int field, uint32_t value);
void steam_pb_float(struct steam_pb *pb, int field, float value);
void steam_pb_bytes(struct steam_pb *pb, int field, void const *data, size_t size);
void steam_pb_string(struct steam_pb *pb, int field, char const *text);
/* (raw bytes, no field: a header's or a nested message's own) */
void steam_pb_raw(struct steam_pb *pb, void const *data, size_t size);

/* a message being read */
struct steam_pb_reader
{
	unsigned char const *at;
	unsigned char const *end;
	int failed;
};

/* the next field: 1 with its number, its wire type and its value (a varint
or a fixed's in value; bytes' in data and size), 0 at the end or when the
message is malformed (failed then) */
int steam_pb_next(struct steam_pb_reader *reader, int *field, int *wire, uint64_t *value,
	unsigned char const **data, size_t *size);
/* a bytes field's text into text (cut at capacity - 1) */
void steam_pb_text(unsigned char const *data, size_t size, char *text, size_t capacity);

#endif
