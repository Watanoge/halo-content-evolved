/*
FORGE_SLIM_MAP.C

A Forge map saved on a campaign level, written slim (forge_slim_map.h).

An Xbox map (version 5) is its 0x800-byte header and one zlib stream of the
rest: its structure BSPs, the sounds' samples and the bitmaps' pixels, then
its tag data, last. The tag data, loaded at XBOX_TAG_BASE, names the rest by
file offset: the scenario's structure BSP entries, each sound permutation's
samples and each bitmap's pixels (every byte of a retail level's is one of
these, or zeroes between them). So the slim map is those it keeps, in their
order, each at its alignment, and the tag data after them with those offsets
made the new ones'; nothing else in it moves.
*/

#ifndef HALO_RELEASE
#define HALO_RELEASE
#endif

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "forge_slim_map.h"

/* (the game's zlib, which deflates too: the port's is an inflater alone) */
#include "../../../source/memory/zlib/zlib.h"

#undef malloc
#undef realloc
#undef free
#undef memcpy
#undef memset
#undef memcmp

#include <stdlib.h>
#include <string.h>

/* ---------- constants */

#define SLIM_HEADER_SIZE 0x800u
#define SLIM_HEADER_SIGNATURE 0x68656164u /* 'head' */
#define SLIM_CACHE_VERSION_XBOX 5
#define SLIM_TAGS_SIGNATURE 0x74616773u /* 'tags' */
#define SLIM_TAG_BASE 0x803A6000u
#define SLIM_TAG_INSTANCE_SIZE 0x20u
#define SLIM_STREAM_CHUNK 0x40000u
/* (zlib's fastest: a campaign level's 150 MB, on a phone too; its next
levels save only a few percent more) */
#define SLIM_DEFLATE_LEVEL 1

#define GROUP_SCENARIO 0x73636E72u /* 'scnr' */
#define GROUP_SOUND 0x736E6421u /* 'snd!' */
#define GROUP_BITMAP 0x6269746Du /* 'bitm' */
#define GROUP_STRUCTURE_BSP 0x73627370u /* 'sbsp' */

/* the scenario's structure BSPs (scenario_definitions.h), an entry's
file offset, size, address, and its tag reference's index */
#define SCENARIO_STRUCTURE_BSPS_OFFSET 0x5A4u
#define STRUCTURE_BSP_ENTRY_SIZE 0x20u
#define STRUCTURE_BSP_ENTRY_TAG_INDEX_OFFSET 0x1Cu
/* a sound's pitch ranges and their permutations (sound_definitions.h), a
permutation's samples: its size, then its file offset */
#define SOUND_PITCH_RANGES_OFFSET 0x98u
#define SOUND_PITCH_RANGE_SIZE 0x48u
#define SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET 0x3Cu
#define SOUND_PERMUTATION_SIZE 0x7Cu
#define SOUND_PERMUTATION_SAMPLES_OFFSET 0x40u
#define TAG_DATA_FILE_OFFSET_OFFSET 0x08u
/* a bitmap group's bitmaps (bitmap_group.h), a bitmap's pixels: its
offset, then its size */
#define BITMAP_GROUP_BITMAPS_OFFSET 0x60u
#define BITMAP_DATA_SIZE 0x30u
#define BITMAP_DATA_PIXELS_OFFSET 0x18u

/* the alignment of what is kept: the BSPs' and the tag data's, a sector's
of the cache (the reads round them up to it); the rest, as the levels have */
#define SLIM_ALIGN_LARGE 0x800u
#define SLIM_ALIGN_SMALL 0x200u

/* ---------- structures */

/* a range of the base kept, and where the tag data names it */
struct slim_reference
{
	uint32_t offset;
	uint32_t size;
	uint32_t align;
	/* (in the tag data: the file offset to make the new one) */
	uint32_t field;
	uint32_t new_offset;
};

/* the base's bytes kept together, as the new file has them */
struct slim_segment
{
	uint32_t offset;
	uint32_t end;
	uint32_t new_offset;
};

/* the base's stream, inflated in order */
struct slim_reader
{
	FILE *file;
	uint32_t left;
	uint32_t position;
	int ended;
	z_stream stream;
	uint8_t *input;
	uint8_t *scratch;
};

/* the new file's stream, deflated in order */
struct slim_writer
{
	FILE *file;
	uint32_t position;
	uint32_t written;
	int failed;
	z_stream stream;
	uint8_t *output;
};

struct slim_state
{
	uint8_t *tags;
	uint32_t tags_size;
	struct slim_reference *references;
	long reference_count;
	long reference_capacity;
	long dropped_sounds;
	long dropped_bitmaps;
	uint32_t dropped_bytes;
};

/* ---------- private code */

static uint32_t read_u32(uint8_t const *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t align_up(uint32_t value, uint32_t align)
{
	return (value + align - 1u) & ~(align - 1u);
}

/* the tag data's bytes at an address, `size` of them; NULL out of it */
static uint8_t *tag_bytes(struct slim_state const *state, uint32_t address, uint32_t size)
{
	uint32_t offset;

	if (address < SLIM_TAG_BASE)
		return NULL;
	offset = address - SLIM_TAG_BASE;
	if (offset > state->tags_size || size > state->tags_size - offset)
		return NULL;
	return state->tags + offset;
}

/* a block's elements (a tag block at `block`: its count, then address) */
static uint8_t *tag_block(struct slim_state const *state, uint8_t const *block, uint32_t element_size, long *count)
{
	long n = (long)read_u32(block);
	uint8_t *elements;

	*count = 0;
	if (n <= 0 || n > 0x10000)
		return NULL;
	elements = tag_bytes(state, read_u32(block + 4), (uint32_t)n * element_size);
	if (elements)
		*count = n;
	return elements;
}

static char const *tag_name(struct slim_state const *state, uint8_t const *instance)
{
	uint8_t const *name = tag_bytes(state, read_u32(instance + 0x10), 1);
	uint32_t room;

	if (!name)
		return "";
	room = state->tags_size - (uint32_t)(name - state->tags);
	return memchr(name, 0, room) ? (char const *)name : "";
}

/* a sound no stripped level plays: its music, its cinematics' and its
mission's and cutscenes' dialogue (a10, x10, ...: the characters' own too,
the Chief's, Johnson's, Keyes'); the combat dialogue of the AI that Forge
places stays */
static int sound_dropped(char const *name)
{
	static char const *const combat[] = { "marines", "grunt", "elite", "jackal", "hunter", "flood" };
	char const *folder;
	size_t length;
	int k;

	if (_strnicmp(name, "sound\\music\\", 12) == 0 || _strnicmp(name, "sound\\sinomatixx", 16) == 0)
		return 1;
	if (_strnicmp(name, "sound\\dialog\\", 13) != 0)
		return 0;
	folder = name + 13;
	length = strcspn(folder, "\\");
	if (!folder[length])
		return 0;
	for (k = 0; k < (int)(sizeof(combat) / sizeof(combat[0])); k++)
	{
		if (length == strlen(combat[k]) && _strnicmp(folder, combat[k], length) == 0)
			return 0;
	}
	return 1;
}

static int reference_add(struct slim_state *state, uint32_t offset, uint32_t size, uint32_t align, uint8_t const *field)
{
	if (state->reference_count == state->reference_capacity)
	{
		long capacity = state->reference_capacity ? state->reference_capacity * 2 : 4096;
		struct slim_reference *grown =
			(struct slim_reference *)realloc(state->references, (size_t)capacity * sizeof(*grown));

		if (!grown)
			return 0;
		state->references = grown;
		state->reference_capacity = capacity;
	}
	state->references[state->reference_count].offset = offset;
	state->references[state->reference_count].size = size;
	state->references[state->reference_count].align = align;
	state->references[state->reference_count].field = (uint32_t)(field - state->tags);
	state->references[state->reference_count].new_offset = 0;
	state->reference_count++;
	return 1;
}

static int reference_compare(void const *a, void const *b)
{
	struct slim_reference const *x = (struct slim_reference const *)a;
	struct slim_reference const *y = (struct slim_reference const *)b;

	if (x->offset != y->offset)
		return x->offset < y->offset ? -1 : 1;
	return x->size < y->size ? -1 : x->size > y->size;
}

/* -- the streams */

static int reader_begin(struct slim_reader *reader, FILE *file, uint32_t base_size)
{
	memset(reader, 0, sizeof(*reader));
	reader->file = file;
	reader->left = base_size - SLIM_HEADER_SIZE;
	reader->position = SLIM_HEADER_SIZE;
	reader->input = (uint8_t *)malloc(SLIM_STREAM_CHUNK);
	reader->scratch = (uint8_t *)malloc(SLIM_STREAM_CHUNK);
	if (!reader->input || !reader->scratch || fseek(file, (long)SLIM_HEADER_SIZE, SEEK_SET) != 0 ||
		inflateInit(&reader->stream) != Z_OK)
	{
		free(reader->input);
		free(reader->scratch);
		reader->input = reader->scratch = NULL;
		return 0;
	}
	return 1;
}

static void reader_end(struct slim_reader *reader)
{
	if (reader->input)
		inflateEnd(&reader->stream);
	free(reader->input);
	free(reader->scratch);
	reader->input = reader->scratch = NULL;
}

/* the next `size` bytes of the inflated base, into `buffer` */
static int reader_read(struct slim_reader *reader, uint8_t *buffer, uint32_t size)
{
	reader->stream.next_out = buffer;
	reader->stream.avail_out = size;
	while (reader->stream.avail_out > 0)
	{
		int result;

		if (reader->ended)
			return 0;
		if (reader->stream.avail_in == 0)
		{
			uint32_t part = reader->left < SLIM_STREAM_CHUNK ? reader->left : SLIM_STREAM_CHUNK;

			if (part == 0 || fread(reader->input, 1, part, reader->file) != part)
				return 0;
			reader->left -= part;
			reader->stream.next_in = reader->input;
			reader->stream.avail_in = part;
		}
		result = inflate(&reader->stream, Z_NO_FLUSH);
		if (result == Z_STREAM_END)
			reader->ended = 1;
		else if (result != Z_OK)
			return 0;
	}
	reader->position += size;
	return 1;
}

/* on to `position` of the inflated base, what is passed thrown away */
static int reader_skip_to(struct slim_reader *reader, uint32_t position)
{
	while (reader->position < position)
	{
		uint32_t part = position - reader->position;

		if (part > SLIM_STREAM_CHUNK)
			part = SLIM_STREAM_CHUNK;
		if (!reader_read(reader, reader->scratch, part))
			return 0;
	}
	return reader->position == position;
}

static int writer_begin(struct slim_writer *writer, FILE *file)
{
	static uint8_t const header_room[SLIM_HEADER_SIZE];

	memset(writer, 0, sizeof(*writer));
	writer->file = file;
	writer->position = SLIM_HEADER_SIZE;
	writer->output = (uint8_t *)malloc(SLIM_STREAM_CHUNK);
	if (!writer->output || fseek(file, 0, SEEK_SET) != 0 ||
		fwrite(header_room, 1, sizeof(header_room), file) != sizeof(header_room) ||
		deflateInit(&writer->stream, SLIM_DEFLATE_LEVEL) != Z_OK)
	{
		free(writer->output);
		writer->output = NULL;
		return 0;
	}
	writer->written = SLIM_HEADER_SIZE;
	return 1;
}

/* `size` bytes deflated into the file (with Z_FINISH, the stream ended) */
static int writer_deflate(struct slim_writer *writer, uint8_t const *bytes, uint32_t size, int flush)
{
	writer->stream.next_in = (Bytef *)bytes;
	writer->stream.avail_in = size;
	do
	{
		uint32_t produced;
		int result;

		writer->stream.next_out = writer->output;
		writer->stream.avail_out = SLIM_STREAM_CHUNK;
		result = deflate(&writer->stream, flush);
		if (result == Z_STREAM_ERROR)
		{
			writer->failed = 1;
			return 0;
		}
		produced = SLIM_STREAM_CHUNK - writer->stream.avail_out;
		if (produced && fwrite(writer->output, 1, produced, writer->file) != produced)
		{
			writer->failed = 1;
			return 0;
		}
		writer->written += produced;
	} while (writer->stream.avail_out == 0);
	return writer->stream.avail_in == 0;
}

static int writer_write(struct slim_writer *writer, uint8_t const *bytes, uint32_t size)
{
	if (size == 0)
		return 1;
	if (!writer_deflate(writer, bytes, size, Z_NO_FLUSH))
		return 0;
	writer->position += size;
	return 1;
}

/* zeroes on to `position` of the new file */
static int writer_pad_to(struct slim_writer *writer, uint8_t *zeros, uint32_t position)
{
	while (writer->position < position)
	{
		uint32_t part = position - writer->position;

		if (part > SLIM_STREAM_CHUNK)
			part = SLIM_STREAM_CHUNK;
		if (!writer_write(writer, zeros, part))
			return 0;
	}
	return writer->position == position;
}

static int writer_end(struct slim_writer *writer)
{
	int ok = writer->output && !writer->failed && writer_deflate(writer, NULL, 0, Z_FINISH);

	if (writer->output)
		deflateEnd(&writer->stream);
	free(writer->output);
	writer->output = NULL;
	return ok && !writer->failed;
}

/* -- what is kept */

/* the tag data's ranges, kept and left out: 0 when it is not a level's this
knows */
static int slim_plan(struct slim_state *state, int *section_in_out, uint32_t tag_data_offset, uint32_t *kept_bsp_size)
{
	uint8_t const *header = state->tags;
	uint8_t const *instances;
	uint8_t const *scenario_instance;
	uint8_t *scenario;
	uint8_t *entries;
	uint8_t *kept_entry;
	long tag_count, entry_count, k;
	char const *kept_name;
	int section = *section_in_out;

	if (state->tags_size < 0x28u || read_u32(header + 0x20) != SLIM_TAGS_SIGNATURE)
		return 0;
	tag_count = (long)read_u32(header + 0x0C);
	instances = tag_bytes(state, read_u32(header), (uint32_t)tag_count * SLIM_TAG_INSTANCE_SIZE);
	if (!instances || tag_count <= 0 || tag_count > 0xFFFF || (long)(read_u32(header + 0x04) & 0xFFFFu) >= tag_count)
		return 0;
	scenario_instance = instances + (read_u32(header + 0x04) & 0xFFFFu) * SLIM_TAG_INSTANCE_SIZE;
	scenario = read_u32(scenario_instance) == GROUP_SCENARIO ?
		tag_bytes(state, read_u32(scenario_instance + 0x14), SCENARIO_STRUCTURE_BSPS_OFFSET + 12u) : NULL;
	if (!scenario)
		return 0;

	/* (the section's BSP, which every entry names from now on) */
	entries = tag_block(state, scenario + SCENARIO_STRUCTURE_BSPS_OFFSET, STRUCTURE_BSP_ENTRY_SIZE, &entry_count);
	if (!entries)
		return 0;
	if (section < 0 || section >= entry_count)
		section = 0;
	*section_in_out = section;
	kept_entry = entries + (uint32_t)section * STRUCTURE_BSP_ENTRY_SIZE;
	{
		uint32_t kept_index = read_u32(kept_entry + STRUCTURE_BSP_ENTRY_TAG_INDEX_OFFSET) & 0xFFFFu;

		kept_name = (long)kept_index < tag_count ?
			tag_name(state, instances + kept_index * SLIM_TAG_INSTANCE_SIZE) : "";
	}
	*kept_bsp_size = read_u32(kept_entry + 4);
	if (*kept_bsp_size < 0x18u)
		return 0;
	for (k = 0; k < entry_count; k++)
	{
		uint8_t *entry = entries + (uint32_t)k * STRUCTURE_BSP_ENTRY_SIZE;

		if (entry != kept_entry)
		{
			if (read_u32(entry) != read_u32(kept_entry))
				state->dropped_bytes += read_u32(entry + 4);
			memcpy(entry, kept_entry, STRUCTURE_BSP_ENTRY_SIZE);
		}
		if (!reference_add(state, read_u32(kept_entry), *kept_bsp_size, SLIM_ALIGN_LARGE, entry))
			return 0;
	}

	for (k = 0; k < tag_count; k++)
	{
		uint8_t const *instance = instances + (uint32_t)k * SLIM_TAG_INSTANCE_SIZE;
		uint32_t group = read_u32(instance);
		uint32_t base = read_u32(instance + 0x14);
		char const *name;

		if (group != GROUP_SOUND && group != GROUP_BITMAP)
			continue;
		name = tag_name(state, instance);
		if (group == GROUP_SOUND)
		{
			uint8_t *sound = tag_bytes(state, base, SOUND_PITCH_RANGES_OFFSET + 12u);
			int dropped = sound_dropped(name);
			long range_count, range;
			uint8_t *ranges = sound ? tag_block(state, sound + SOUND_PITCH_RANGES_OFFSET, SOUND_PITCH_RANGE_SIZE,
				&range_count) : NULL;

			for (range = 0; ranges && range < range_count; range++)
			{
				long permutation_count, permutation;
				uint8_t *permutations = tag_block(state, ranges + (uint32_t)range * SOUND_PITCH_RANGE_SIZE +
					SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET, SOUND_PERMUTATION_SIZE, &permutation_count);

				for (permutation = 0; permutations && permutation < permutation_count; permutation++)
				{
					uint8_t *samples = permutations + (uint32_t)permutation * SOUND_PERMUTATION_SIZE +
						SOUND_PERMUTATION_SAMPLES_OFFSET;
					uint32_t size = read_u32(samples);

					if ((int32_t)size <= 0)
						continue;
					if (dropped)
					{
						/* (none: the sound cache loads none of it,
						xbox_sound_cache.c) */
						state->dropped_bytes += size;
						write_u32(samples, 0);
						write_u32(samples + TAG_DATA_FILE_OFFSET_OFFSET, 0);
					}
					else if (!reference_add(state, read_u32(samples + TAG_DATA_FILE_OFFSET_OFFSET), size,
						SLIM_ALIGN_SMALL, samples + TAG_DATA_FILE_OFFSET_OFFSET))
					{
						return 0;
					}
				}
			}
			if (dropped)
				state->dropped_sounds++;
		}
		else
		{
			uint8_t *group_bytes = tag_bytes(state, base, BITMAP_GROUP_BITMAPS_OFFSET + 12u);
			/* (a lightmap: its BSP's name, a BSP of the level's, not the
			section's) */
			int dropped = 0;
			long bitmap_count, bitmap;
			uint8_t *bitmaps;

			if (_stricmp(name, kept_name) != 0)
			{
				long j;

				for (j = 0; j < tag_count && !dropped; j++)
				{
					uint8_t const *other = instances + (uint32_t)j * SLIM_TAG_INSTANCE_SIZE;

					dropped = read_u32(other) == GROUP_STRUCTURE_BSP && _stricmp(tag_name(state, other), name) == 0;
				}
			}
			bitmaps = group_bytes ? tag_block(state, group_bytes + BITMAP_GROUP_BITMAPS_OFFSET, BITMAP_DATA_SIZE,
				&bitmap_count) : NULL;
			for (bitmap = 0; bitmaps && bitmap < bitmap_count; bitmap++)
			{
				uint8_t *pixels = bitmaps + (uint32_t)bitmap * BITMAP_DATA_SIZE + BITMAP_DATA_PIXELS_OFFSET;
				uint32_t size = read_u32(pixels + 4);

				if ((int32_t)size <= 0)
					continue;
				/* (the section's BSP's bytes instead, which its size fits in:
				no BSP the map loads draws it) */
				if (dropped && size <= *kept_bsp_size)
				{
					state->dropped_bytes += size;
					if (!reference_add(state, read_u32(kept_entry), size, SLIM_ALIGN_LARGE, pixels))
						return 0;
				}
				else if (!reference_add(state, read_u32(pixels), size, SLIM_ALIGN_SMALL, pixels))
					return 0;
			}
			if (dropped)
				state->dropped_bitmaps++;
		}
	}

	/* (every range in the base, before its tag data) */
	for (k = 0; k < state->reference_count; k++)
	{
		struct slim_reference const *reference = &state->references[k];

		if (reference->offset < SLIM_HEADER_SIZE || reference->offset > tag_data_offset ||
			reference->size > tag_data_offset - reference->offset)
		{
			error(_error_silent, "forge: slim save: a range %08lx (%lu bytes) outside the map's data",
				(unsigned long)reference->offset, (unsigned long)reference->size);
			return 0;
		}
	}
	return 1;
}

/* the kept ranges laid out: touching or overlapping ones as one segment;
the new file's size before its tag data in `*data_end` */
static struct slim_segment *slim_layout(struct slim_state *state, long *segment_count, uint32_t *data_end)
{
	struct slim_segment *segments;
	uint32_t position = SLIM_HEADER_SIZE;
	long count = 0, k;

	qsort(state->references, (size_t)state->reference_count, sizeof(*state->references), reference_compare);
	segments = (struct slim_segment *)malloc((size_t)(state->reference_count + 1) * sizeof(*segments));
	if (!segments)
		return NULL;
	for (k = 0; k < state->reference_count; k++)
	{
		struct slim_reference *reference = &state->references[k];
		uint32_t end = reference->offset + reference->size;

		if (count == 0 || reference->offset >= segments[count - 1].end)
		{
			position = align_up(position, reference->align);
			segments[count].offset = reference->offset;
			segments[count].end = end;
			segments[count].new_offset = position;
			count++;
		}
		else if (end > segments[count - 1].end)
		{
			segments[count - 1].end = end;
		}
		reference->new_offset = segments[count - 1].new_offset + (reference->offset - segments[count - 1].offset);
		position = segments[count - 1].new_offset + (segments[count - 1].end - segments[count - 1].offset);
		write_u32(state->tags + reference->field, reference->new_offset);
	}
	*segment_count = count;
	*data_end = position;
	return segments;
}

/* ---------- public code */

int forge_slim_map_header_is_slim(uint8_t const *header)
{
	return header && read_u32(header + FORGE_SLIM_MAP_SIGNATURE_OFFSET) == FORGE_SLIM_MAP_SIGNATURE;
}

int forge_slim_map_write(FILE *in_file, uint32_t base_size, int section, FILE *out_file, uint8_t header[0x800],
	uint32_t *out_end)
{
	struct slim_state state;
	struct slim_reader reader;
	struct slim_writer writer;
	struct slim_segment *segments = NULL;
	uint8_t *zeros = NULL;
	uint32_t tag_data_offset, tag_data_size, data_end, new_tag_data_offset, base_length;
	uint32_t kept_bsp_size = 0;
	long segment_count = 0, k;
	int ok = 0;

	if (!in_file || !out_file || !header || !out_end || base_size <= SLIM_HEADER_SIZE)
		return 0;
	memset(&state, 0, sizeof(state));
	if (fseek(in_file, 0, SEEK_SET) != 0 || fread(header, 1, SLIM_HEADER_SIZE, in_file) != SLIM_HEADER_SIZE ||
		read_u32(header) != SLIM_HEADER_SIGNATURE || read_u32(header + 4) != SLIM_CACHE_VERSION_XBOX)
	{
		return 0;
	}
	base_length = read_u32(header + 0x08);
	tag_data_offset = read_u32(header + 0x10);
	tag_data_size = read_u32(header + 0x14);
	if (tag_data_offset < SLIM_HEADER_SIZE || tag_data_size == 0 || tag_data_size > 0x4000000u ||
		tag_data_offset > base_length || tag_data_size > base_length - tag_data_offset)
	{
		return 0;
	}

	/* the tag data, the base's last: inflated through to it */
	state.tags_size = tag_data_size;
	state.tags = (uint8_t *)malloc(tag_data_size);
	if (!state.tags || !reader_begin(&reader, in_file, base_size))
	{
		free(state.tags);
		return 0;
	}
	ok = reader_skip_to(&reader, tag_data_offset) && reader_read(&reader, state.tags, tag_data_size);
	reader_end(&reader);
	if (!ok)
	{
		error(_error_silent, "forge: slim save: the base's tag data can't be read");
		free(state.tags);
		return 0;
	}

	ok = slim_plan(&state, &section, tag_data_offset, &kept_bsp_size) &&
		(segments = slim_layout(&state, &segment_count, &data_end)) != NULL;
	if (!ok)
	{
		error(_error_silent, "forge: slim save: the base is not a level this can make slim");
		goto done;
	}
	new_tag_data_offset = align_up(data_end, SLIM_ALIGN_LARGE);

	/* the kept bytes, in their order, then the tag data */
	zeros = (uint8_t *)calloc(1, SLIM_STREAM_CHUNK);
	ok = zeros && reader_begin(&reader, in_file, base_size);
	if (ok)
	{
		ok = writer_begin(&writer, out_file);
		for (k = 0; ok && k < segment_count; k++)
		{
			uint32_t position;

			ok = writer_pad_to(&writer, zeros, segments[k].new_offset) &&
				reader_skip_to(&reader, segments[k].offset);
			for (position = segments[k].offset; ok && position < segments[k].end;)
			{
				uint32_t part = segments[k].end - position;

				if (part > SLIM_STREAM_CHUNK)
					part = SLIM_STREAM_CHUNK;
				ok = reader_read(&reader, reader.scratch, part) && writer_write(&writer, reader.scratch, part);
				position += part;
			}
		}
		ok = ok && writer_pad_to(&writer, zeros, new_tag_data_offset) &&
			writer_write(&writer, state.tags, tag_data_size);
		ok = writer_end(&writer) && ok;
		reader_end(&reader);
	}
	if (!ok)
	{
		error(_error_silent, "forge: slim save: the slim map can't be written");
		goto done;
	}

	write_u32(header + 0x08, new_tag_data_offset + tag_data_size);
	write_u32(header + 0x10, new_tag_data_offset);
	/* (a saved map's trailer, its offsets the base's: written again) */
	memset(header + 0x1E4u, 0, 0x1Cu);
	write_u32(header + FORGE_SLIM_MAP_SIGNATURE_OFFSET, FORGE_SLIM_MAP_SIGNATURE);
	write_u32(header + FORGE_SLIM_MAP_SECTION_OFFSET, (uint32_t)section);
	*out_end = writer.written;
	error(_error_silent, "forge: slim save: section %d kept; %ld sounds and %ld lightmaps left out, %lu MB of %lu MB "
		"inflated, %lu MB written", section, state.dropped_sounds, state.dropped_bitmaps,
		(unsigned long)((new_tag_data_offset + tag_data_size) >> 20), (unsigned long)(base_length >> 20),
		(unsigned long)(writer.written >> 20));

done:
	free(zeros);
	free(segments);
	free(state.references);
	free(state.tags);
	return ok;
}
