/* network_tag_table.c's world: the level's table on the host and on a client,
and the host's messages handed to the client as they were sent. */
#include <stddef.h>
#include "harness.h"
typedef unsigned short word;
typedef word message_header;
enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server };
enum { _error_silent };
#include "message_ids.inc"
#define MAXIMUM_MESSAGE_SIZE 0xFFF
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define GET_MESSAGE_TYPE(h) (((h)>>2)&3)
struct distributed_message_header { message_header header; byte type, count; long game_time; unsigned long checkpoint_epoch; };
static int connection = _game_connection_network_server;
static int game_connection(void) { return connection; }
static boolean network_game_distributed_client(void) { return connection == _game_connection_network_client; }
static void error(int level, char const *format, ...) { (void)level; (void)format; }
#define SENT_MAXIMUM 8
static byte sent[SENT_MAXIMUM][16384];
static word sent_sizes[SENT_MAXIMUM];
static int sent_count;
static long sent_machine = NONE;
static void distributed_send_to_machine_reliably(long machine, void *data, byte type, int count, word size)
{
    struct distributed_message_header *header = (struct distributed_message_header *)sent[sent_count];
    CHECK(sent_count < SENT_MAXIMUM && size <= sizeof(sent[0]), "too many or too large");
    memcpy(sent[sent_count], data, size);
    header->header = 8; header->type = type; header->count = (byte)count;
    sent_sizes[sent_count++] = size;
    sent_machine = machine;
}
/* (a replay's recording: its records kept as they are written) */
#define DISTRIBUTED_RECORDER_MACHINE 16
enum { _network_replay_datagram = 1, _network_replay_reliable };
static long game_time_get(void) { return 77; }
static void build_message_header(word *header, word length, byte type, byte flags)
{ (void)length; (void)flags; *header = (word)(type << 2); }
static byte recorded[SENT_MAXIMUM][16384];
static word recorded_sizes[SENT_MAXIMUM];
static int recorded_count;
static void network_replay_record(byte kind, void const *data, long size)
{
    CHECK(kind == _network_replay_reliable && recorded_count < SENT_MAXIMUM && size <= (long)sizeof(recorded[0]),
        "record");
    memcpy(recorded[recorded_count], data, (size_t)size);
    recorded_sizes[recorded_count++] = (word)size;
}
#include "under_test.inc"

/* the tables: each tag a group, a path and a datum */
#define TABLE_MAXIMUM 1300
static struct table_tag_instance host_table[TABLE_MAXIMUM], client_table[TABLE_MAXIMUM];
static long host_count, client_count;
static char names[2][TABLE_MAXIMUM][48];
static struct table_tag_instance *loaded;
static long loaded_count;
void *cache_files_tag_instances(long *count) { *count = loaded_count; return loaded; }

static void tag_set(struct table_tag_instance *table, int side, long index, long group, char const *name, unsigned salt)
{
    snprintf(names[side][index], sizeof(names[side][index]), "%s", name);
    table[index].group_tag = group;
    table[index].name = names[side][index];
    table[index].tag_index = (long)(((unsigned long)salt << 16) | (unsigned long)index);
    table[index].base_address = (void *)1;
}

/* the host's table: n tags; the client's the same, or another disc's (a tag
more early on, one of the host's gone, two swapped) */
static void tables(long n, boolean other_disc)
{
    long index, client_index = 0;
    char name[48];

    host_count = n;
    for (index = 0; index < n; index++)
    {
        snprintf(name, sizeof(name), "levels\\test\\tag %ld", index);
        tag_set(host_table, 0, index, index % 3 ? 'bitm' : 'snd!', name, 0xE174 + index);
    }
    for (index = 0; index < n; index++)
    {
        if (other_disc && index == 2)
            tag_set(client_table, 1, client_index++, 'DeLa', "ui\\shell\\pausebox4_left", 0xE174 + 2);
        if (other_disc && index == 5)
            continue; /* (the host's #5: none here) */
        snprintf(name, sizeof(name), "%s", host_table[index].name);
        tag_set(client_table, 1, client_index, host_table[index].group_tag, name, 0xE174 + client_index);
        client_index++;
    }
    if (other_disc)
    {
        struct table_tag_instance swap = client_table[700];
        long datum_700 = client_table[700].tag_index, datum_701 = client_table[701].tag_index;

        client_table[700] = client_table[701];
        client_table[701] = swap;
        client_table[700].tag_index = datum_700;
        client_table[701].tag_index = datum_701;
    }
    client_count = client_index;
}

/* the host sends its table; the client takes its messages, in this order */
static void send_and_receive(int const *order, int order_count, long from)
{
    int index;

    sent_count = 0;
    loaded = host_table; loaded_count = host_count; connection = _game_connection_network_server;
    network_tag_table_send(3);
    CHECK(sent_count == (host_count + 499) / 500 && sent_machine == 3, "parts: %d", sent_count);
    loaded = client_table; loaded_count = client_count; connection = _game_connection_network_client;
    network_tag_table_reset();
    for (index = 0; index < (order ? order_count : sent_count); index++)
    {
        int part = order ? order[index] : index;

        CHECK(network_tag_table_handles_message((word *)sent[part], sent_sizes[part]), "dispatch");
        network_tag_table_handle_message(from, (word *)sent[part], sent_sizes[part]);
    }
}

/* the client's tag of the host's path and group (NONE when none) */
static long client_datum_of(long host_index)
{
    long index;

    for (index = 0; index < client_count; index++)
    {
        if (client_table[index].group_tag == host_table[host_index].group_tag &&
            !strcmp(client_table[index].name, host_table[host_index].name))
        {
            return client_table[index].tag_index;
        }
    }
    return NONE;
}

int main(int argc, char **argv)
{
    long index;

    CHECK(argc == 2, "case");
    char const *case_name = argv[1];
    CASE("identity") {
        tables(1200, FALSE);
        send_and_receive(NULL, 0, NONE);
        CHECK(!network_tag_table_translating(), "the same table translated");
        CHECK(network_tag_from_host(host_table[9].tag_index) == host_table[9].tag_index, "changed");
        return 0;
    }
    CASE("other-disc") {
        long host_count_seen, found, missing;

        tables(1200, TRUE);
        send_and_receive(NULL, 0, NONE);
        CHECK(network_tag_table_translating(), "not translating");
        CHECK(network_tag_table_differs(&host_count_seen, &found, &missing) && host_count_seen == 1200 &&
            found == 1199 && missing == 1, "counts %ld %ld %ld", host_count_seen, found, missing);
        for (index = 0; index < host_count; index++)
        {
            long expected = client_datum_of(index);

            CHECK(network_tag_from_host(host_table[index].tag_index) == expected, "host #%ld", index);
            if (expected != NONE)
                CHECK(network_tag_to_host(expected) == host_table[index].tag_index, "back #%ld", index);
        }
        CHECK(network_tag_from_host(host_table[5].tag_index) == NONE, "the host's own");
        CHECK(network_tag_to_host(client_table[2].tag_index) == NONE, "this machine's own");
        CHECK(network_tag_from_host(NONE) == NONE && network_tag_to_host(NONE) == NONE, "none");
        CHECK(network_tag_from_host(host_table[9].tag_index ^ 0x10000) == NONE, "another salt");
        CHECK(network_tag_from_host(0x7FFF0000L | 1250) == NONE, "past the host's table");
        CHECK(network_tag_to_host(client_table[9].tag_index ^ 0x10000) == NONE, "another salt here");
        network_tag_table_reset();
        CHECK(!network_tag_table_translating() &&
            network_tag_from_host(host_table[9].tag_index) == host_table[9].tag_index, "reset");
        return 0;
    }
    CASE("duplicates") {
        tables(600, TRUE);
        /* (two tags of one path and group on each side: the first with the first) */
        tag_set(host_table, 0, 10, 'bitm', "dup", 0xE174 + 10);
        tag_set(host_table, 0, 20, 'bitm', "dup", 0xE174 + 20);
        tag_set(client_table, 1, 30, 'bitm', "dup", 0xE174 + 30);
        tag_set(client_table, 1, 40, 'bitm', "dup", 0xE174 + 40);
        send_and_receive(NULL, 0, NONE);
        CHECK(network_tag_from_host(host_table[10].tag_index) == client_table[30].tag_index, "first");
        CHECK(network_tag_from_host(host_table[20].tag_index) == client_table[40].tag_index, "second");
        return 0;
    }
    CASE("order") {
        /* (a part before its table's first is no part of it; one again, or
        out of order, is dropped, and the table is whole with the rest) */
        int early[] = { 1, 0 };
        int order[] = { 0, 1, 1, 2 };

        tables(1200, TRUE);
        send_and_receive(early, 2, NONE);
        CHECK(!network_tag_table_translating(), "a table of parts out of order taken");
        send_and_receive(order, 4, NONE);
        CHECK(network_tag_table_translating(), "the table not whole");
        for (index = 0; index < host_count; index++)
            CHECK(network_tag_from_host(host_table[index].tag_index) == client_datum_of(index), "host #%ld", index);
        return 0;
    }
    CASE("replay") {
        int part;

        tables(1200, TRUE);
        /* (a client's recording begun before the host's table came: none of
        it written then) */
        loaded = client_table; loaded_count = client_count; connection = _game_connection_network_client;
        network_tag_table_reset();
        network_tag_table_record(TRUE);
        CHECK(recorded_count == 0, "a table not come written");
        /* (begun after: the host's, as it came, played back on this machine) */
        send_and_receive(NULL, 0, NONE);
        network_tag_table_record(TRUE);
        CHECK(recorded_count == 3, "parts recorded: %d", recorded_count);
        network_tag_table_reset();
        for (part = 0; part < recorded_count; part++)
        {
            CHECK(network_tag_table_handles_message((word *)recorded[part], recorded_sizes[part]), "dispatch");
            network_tag_table_handle_message(NONE, (word *)recorded[part], recorded_sizes[part]);
        }
        CHECK(network_tag_table_translating(), "the recording's table not taken");
        for (index = 0; index < host_count; index++)
            CHECK(network_tag_from_host(host_table[index].tag_index) == client_datum_of(index), "host #%ld", index);
        /* (the host's recording: its own table, to the recorder) */
        connection = _game_connection_network_server; loaded = host_table; loaded_count = host_count;
        sent_count = 0;
        network_tag_table_record(FALSE);
        CHECK(sent_count == 3 && sent_machine == DISTRIBUTED_RECORDER_MACHINE, "the recorder not sent it");
        return 0;
    }
    CASE("authority") {
        tables(1200, TRUE);
        send_and_receive(NULL, 0, 0);
        CHECK(!network_tag_table_translating(), "a client's table taken");
        connection = _game_connection_network_client;
        loaded = host_table; loaded_count = host_count; sent_count = 0;
        network_tag_table_send(3);
        CHECK(sent_count == 0, "a client sent its table");
        return 0;
    }
    CHECK(0, "no case %s", case_name);
    return 1;
}
