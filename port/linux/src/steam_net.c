/*
STEAM_NET.C

steam_net.h's: the HTTPS GET on each build, and protocol buffers.
*/

#include "steam_net.h"
#include "platform.h"
#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int steam_net_available(void)
{
#ifdef HALO_WEB
	return 0;
#else
	return 1;
#endif
}

#if defined(HALO_WEB)
int steam_https_get(char const *url, unsigned long maximum, unsigned char **body, unsigned long *size, int *status,
	char *error, int error_size)
{
	(void)url; (void)maximum;
	*body = NULL;
	*size = 0;
	*status = 0;
	snprintf(error, (size_t)error_size, "the browser build cannot reach Steam");
	return 0;
}
#elif defined(HALO_ANDROID) || defined(HALO_PS5)
/* the app's host (port/android/host/host_https.c; the Switch's,
port/switch/host/host_https.c), as master_list.c declares it */
struct host_https_sizes
{
	int headers_size;
	int body_capacity;
	int error_size;
	int status;
	int body_size;
};
int host_https_get(const char *url, char *headers, char *body, char *error, struct host_https_sizes *sizes);

int steam_https_get(char const *url, unsigned long maximum, unsigned char **body, unsigned long *size, int *status,
	char *error, int error_size)
{
	enum { HEADERS_SIZE = 4096 };
	char *headers = malloc(HEADERS_SIZE);
	struct host_https_sizes sizes;

	*body = malloc(maximum + 1);
	*size = 0;
	*status = 0;
	sizes.headers_size = HEADERS_SIZE;
	sizes.body_capacity = (int)maximum;
	sizes.error_size = error_size;
	sizes.status = 0;
	sizes.body_size = 0;
	if (!headers || !*body)
	{
		free(headers);
		free(*body);
		*body = NULL;
		snprintf(error, (size_t)error_size, "out of memory");
		return 0;
	}
	error[0] = 0;
	if (!host_https_get(url, headers, (char *)*body, error, &sizes))
	{
		free(headers);
		free(*body);
		*body = NULL;
		return 0;
	}
	free(headers);
	(*body)[sizes.body_size] = 0;
	*size = (unsigned long)sizes.body_size;
	*status = sizes.status;
	return 1;
}
#else
int steam_https_get(char const *url, unsigned long maximum, unsigned char **body, unsigned long *size, int *status,
	char *error, int error_size)
{
	struct update_response response;

	*body = NULL;
	*size = 0;
	*status = 0;
	if (!update_https_request("GET", url, NULL, NULL, 0, maximum, &response, error, error_size))
		return 0;
	*body = (unsigned char *)response.body;
	*size = response.body_size;
	*status = response.status;
	return 1;
}
#endif

/* ---------- protocol buffers */

void steam_pb_free(struct steam_pb *pb)
{
	free(pb->bytes);
	memset(pb, 0, sizeof(*pb));
}

static void pb_put(struct steam_pb *pb, void const *data, size_t size)
{
	if (pb->failed)
		return;
	if (pb->size + size > pb->capacity)
	{
		size_t capacity = pb->capacity ? pb->capacity : 256;
		unsigned char *bytes;

		while (capacity < pb->size + size)
			capacity *= 2;
		bytes = realloc(pb->bytes, capacity);
		if (!bytes)
		{
			pb->failed = 1;
			return;
		}
		pb->bytes = bytes;
		pb->capacity = capacity;
	}
	memcpy(pb->bytes + pb->size, data, size);
	pb->size += size;
}

static void pb_put_varint(struct steam_pb *pb, uint64_t value)
{
	unsigned char bytes[10];
	int count = 0;

	while (value > 127)
	{
		bytes[count++] = (unsigned char)((value & 127) | 128);
		value >>= 7;
	}
	bytes[count++] = (unsigned char)value;
	pb_put(pb, bytes, (size_t)count);
}

void steam_pb_varint(struct steam_pb *pb, int field, uint64_t value)
{
	pb_put_varint(pb, (uint64_t)field << 3 | STEAM_PB_VARINT);
	pb_put_varint(pb, value);
}

void steam_pb_fixed64(struct steam_pb *pb, int field, uint64_t value)
{
	unsigned char bytes[8];
	int i;

	for (i = 0; i < 8; i++)
		bytes[i] = (unsigned char)(value >> (8 * i));
	pb_put_varint(pb, (uint64_t)field << 3 | STEAM_PB_FIXED64);
	pb_put(pb, bytes, 8);
}

void steam_pb_fixed32(struct steam_pb *pb, int field, uint32_t value)
{
	unsigned char bytes[4];
	int i;

	for (i = 0; i < 4; i++)
		bytes[i] = (unsigned char)(value >> (8 * i));
	pb_put_varint(pb, (uint64_t)field << 3 | STEAM_PB_FIXED32);
	pb_put(pb, bytes, 4);
}

void steam_pb_float(struct steam_pb *pb, int field, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, 4);
	steam_pb_fixed32(pb, field, bits);
}

void steam_pb_bytes(struct steam_pb *pb, int field, void const *data, size_t size)
{
	pb_put_varint(pb, (uint64_t)field << 3 | STEAM_PB_BYTES);
	pb_put_varint(pb, size);
	pb_put(pb, data, size);
}

void steam_pb_string(struct steam_pb *pb, int field, char const *text)
{
	steam_pb_bytes(pb, field, text, strlen(text));
}

void steam_pb_raw(struct steam_pb *pb, void const *data, size_t size)
{
	pb_put(pb, data, size);
}

static int pb_get_varint(struct steam_pb_reader *reader, uint64_t *value)
{
	uint64_t result = 0;
	int shift = 0;

	while (reader->at < reader->end && shift < 64)
	{
		unsigned char byte = *reader->at++;

		result |= (uint64_t)(byte & 127) << shift;
		if (!(byte & 128))
		{
			*value = result;
			return 1;
		}
		shift += 7;
	}
	reader->failed = 1;
	return 0;
}

int steam_pb_next(struct steam_pb_reader *reader, int *field, int *wire, uint64_t *value,
	unsigned char const **data, size_t *size)
{
	uint64_t tag;
	int i;

	*value = 0;
	*data = NULL;
	*size = 0;
	if (reader->failed || reader->at >= reader->end)
		return 0;
	if (!pb_get_varint(reader, &tag))
		return 0;
	*field = (int)(tag >> 3);
	*wire = (int)(tag & 7);
	switch (*wire)
	{
	case STEAM_PB_VARINT:
		return pb_get_varint(reader, value);
	case STEAM_PB_FIXED64:
	case STEAM_PB_FIXED32:
	{
		int count = *wire == STEAM_PB_FIXED64 ? 8 : 4;

		if (reader->end - reader->at < count)
			break;
		for (i = 0; i < count; i++)
			*value |= (uint64_t)reader->at[i] << (8 * i);
		reader->at += count;
		return 1;
	}
	case STEAM_PB_BYTES:
	{
		uint64_t length;

		if (!pb_get_varint(reader, &length) || length > (uint64_t)(reader->end - reader->at))
			break;
		*data = reader->at;
		*size = (size_t)length;
		reader->at += length;
		return 1;
	}
	default:
		break;
	}
	reader->failed = 1;
	return 0;
}

void steam_pb_text(unsigned char const *data, size_t size, char *text, size_t capacity)
{
	if (!capacity)
		return;
	if (size > capacity - 1)
		size = capacity - 1;
	if (data)
		memcpy(text, data, size);
	text[data ? size : 0] = 0;
}
