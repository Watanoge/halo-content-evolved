/*
STEAM_DOWNLOADS.C

steam_downloads.h's model. Two threads of its own reach Steam: the
session's (signing in, the Workshop's pages) and the downloads' (one at a
time: the shared sets or an item), the connection server taken in turn
(session_lock); the content servers are the downloads' alone. The screen
reads a copy of their state under state_lock.

The session's file (steam/session.txt of the data root): "account=",
"token=" (the refresh token the QR sign-in gave) and "seed=" (32 hex
digits: this install's identity, from which the logon's machine id is
made). What was downloaded is in steam/downloads.txt: "shared <manifest>"
and "<item id> <manifest>" lines, so an item updated since shows UPDATE.
*/

#include "steam_downloads.h"
#include "steam_cm.h"
#include "steam_crypto.h"
#include "steam_depot.h"
#include "steam_net.h"
#include "platform.h"
#include "posix.h"

/* (the QR encoder, a copy of its own under other names: not every build
links port/third_party/qrcodegen, and those that do have it as it is) */
#define qrcodegen_calcSegmentBufferSize steam_qrcodegen_calcSegmentBufferSize
#define qrcodegen_encodeBinary steam_qrcodegen_encodeBinary
#define qrcodegen_encodeSegments steam_qrcodegen_encodeSegments
#define qrcodegen_encodeSegmentsAdvanced steam_qrcodegen_encodeSegmentsAdvanced
#define qrcodegen_encodeText steam_qrcodegen_encodeText
#define qrcodegen_getModule steam_qrcodegen_getModule
#define qrcodegen_getSize steam_qrcodegen_getSize
#define qrcodegen_isAlphanumeric steam_qrcodegen_isAlphanumeric
#define qrcodegen_isNumeric steam_qrcodegen_isNumeric
#define qrcodegen_makeAlphanumeric steam_qrcodegen_makeAlphanumeric
#define qrcodegen_makeBytes steam_qrcodegen_makeBytes
#define qrcodegen_makeEci steam_qrcodegen_makeEci
#define qrcodegen_makeNumeric steam_qrcodegen_makeNumeric
#include "../../third_party/qrcodegen/qrcodegen.c"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

unsigned long system_milliseconds(void);

enum
{
	WORKSHOP_PAGE_ITEMS = 20,
	/* the fixed rows: the account, the shared sets, the sort */
	ROW_ACCOUNT = 0,
	ROW_SHARED,
	ROW_SORT,
	FIXED_ROWS,
	MAXIMUM_INSTALLED = 512,
	QR_VERSION = 10,
	STEAM_HALO1_DEPOT = 976733
};

#define WORKSHOP_TAG "Halo1"
#define DEVICE_NAME "Halo Content Evolved"

enum session_state
{
	SESSION_OUT,
	SESSION_CONNECTING,
	SESSION_QR,
	SESSION_SIGNING_IN,
	SESSION_IN,
	SESSION_UNAVAILABLE
};

static struct shared_file
{
	char const *from;
	char const *folder;
	char const *name;
} const shared_files[] =
{
	{ "halo1/maps/bitmaps.map", "_shared/singleplayer", "bitmaps.map" },
	{ "halo1/maps/custom_edition/bitmaps.map", "_shared/multiplayer", "bitmaps.map" },
	{ "halo1/maps/custom_edition/sounds.map", "_shared/multiplayer", "sounds.map" },
};
#define SHARED_FILES ((int)(sizeof(shared_files) / sizeof(shared_files[0])))

static char const *const sort_names[] = { "TRENDING", "MOST SUBSCRIBED", "NEWEST", "TOP RATED" };
static enum steam_workshop_sort const sorts[] =
	{ STEAM_WORKSHOP_TREND, STEAM_WORKSHOP_SUBSCRIPTIONS, STEAM_WORKSHOP_NEWEST, STEAM_WORKSHOP_VOTES };

struct installed
{
	uint64_t id;
	uint64_t manifest;
};

static struct
{
	pthread_mutex_t state_lock;
	pthread_cond_t wake;
	pthread_mutex_t session_lock;
	int threads_started;

	/* the session thread's work (set by the screen) */
	int want_resume;
	int want_sign_in;
	int want_sign_out;
	int want_page;
	int cancel_sign_in;
	/* the downloads thread's (0: none; -1 the shared sets; else an item) */
	int want_download;
	struct steam_workshop_file download_item;
	int cancel_download;

	/* what is shown */
	enum session_state state;
	char account[STEAM_ACCOUNT_SIZE];
	char message[256];
	char qr_url[STEAM_URL_SIZE];
	unsigned qr_serial;
	uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
	int qr_ready;
	int sort;
	int page;
	int loading;
	uint32_t total;
	int item_count;
	struct steam_workshop_file items[WORKSHOP_PAGE_ITEMS];
	/* the download under way: 0 none, -1 shared, 1 item (its id) */
	int downloading;
	uint64_t downloading_id;
	uint64_t done;
	uint64_t size;
	char downloading_name[96];
	int rescan;
	/* -1 not known, 0 no, 1 yes */
	int owns_halo1;
	struct installed installed[MAXIMUM_INSTALLED];
	int installed_count;
	uint64_t shared_manifest;

	/* the screen's */
	short first;
	short chosen;
	int open;

	/* the session itself (session_lock) */
	struct steam_cm *cm;
	char token[STEAM_TOKEN_SIZE];
	unsigned char seed[16];
	int seeded;
} steam =
{
	PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
};

/* ---------- files */

static void data_path(char *path, size_t size, char const *relative)
{
	snprintf(path, size, "%s/%s", platform_data_root(), relative);
}

static void mcc_root(char *path, size_t size)
{
	char found[256];

	if (posix_find_entry_case_insensitive(platform_data_root(), "mcc_maps", found, sizeof(found)))
		snprintf(path, size, "%s/%s", platform_data_root(), found);
	else
		snprintf(path, size, "%s/mcc_maps", platform_data_root());
}

static int path_exists(char const *path, int *directory)
{
	struct posix_file_information information;

	if (posix_stat(path, &information) != 0)
		return 0;
	if (directory)
		*directory = (information.flags & _posix_file_is_directory) != 0;
	return 1;
}

/* the folders of path made, as far as its last '/' (or the whole of it) */
static void make_folders(char const *path, int whole)
{
	char partial[1024];
	size_t i, length = strlen(path);

	if (length >= sizeof(partial))
		return;
	for (i = 1; i <= length; i++)
	{
		if (path[i] == '/' || path[i] == '\\' || (whole && i == length))
		{
			memcpy(partial, path, i);
			partial[i] = 0;
			if (!path_exists(partial, NULL))
				posix_make_directory(partial);
		}
	}
}

/* a folder and what is in it, gone */
static void remove_tree(char const *path)
{
	void *directory = posix_directory_open(path);
	char name[256];

	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char child[1024];
		int is_directory = 0;

		snprintf(child, sizeof(child), "%s/%s", path, name);
		if (!path_exists(child, &is_directory))
			continue;
		if (is_directory)
			remove_tree(child);
		else if (!posix_mcc_share_remove(child, 0))
			remove(child);
	}
	if (directory)
		posix_directory_close(directory);
	if (!posix_mcc_share_remove(path, 1))
		remove(path);
}

/* from to to, which must not be there (posix_mcc_share_publish's, else a rename) */
static int move_folder(char const *from, char const *to)
{
	if (path_exists(to, NULL))
		return 0;
	return posix_mcc_share_publish(from, to) || rename(from, to) == 0;
}

static void session_save(void)
{
	char path[1024];
	char seed[33];
	FILE *file;

	data_path(path, sizeof(path), "steam");
	make_folders(path, 1);
	data_path(path, sizeof(path), "steam/session.txt");
	file = fopen(path, "w");
	if (!file)
		return;
	steam_hex(steam.seed, sizeof(steam.seed), seed);
	fprintf(file, "account=%s\ntoken=%s\nseed=%s\n", steam.account, steam.token, seed);
	fclose(file);
}

static void session_load(void)
{
	char path[1024];
	char line[STEAM_TOKEN_SIZE + 16];
	FILE *file;

	data_path(path, sizeof(path), "steam/session.txt");
	file = fopen(path, "r");
	while (file && fgets(line, sizeof(line), file))
	{
		char *value = strchr(line, '=');
		size_t length;

		if (!value)
			continue;
		*value++ = 0;
		length = strcspn(value, "\r\n");
		value[length] = 0;
		if (!strcmp(line, "account"))
			snprintf(steam.account, sizeof(steam.account), "%s", value);
		else if (!strcmp(line, "token"))
			snprintf(steam.token, sizeof(steam.token), "%s", value);
		else if (!strcmp(line, "seed") && length == 32)
		{
			int i;

			for (i = 0; i < 16; i++)
			{
				unsigned byte = 0;

				sscanf(value + 2 * i, "%2x", &byte);
				steam.seed[i] = (unsigned char)byte;
			}
			steam.seeded = 1;
		}
	}
	if (file)
		fclose(file);
	if (!steam.seeded)
	{
		steam_random(steam.seed, sizeof(steam.seed));
		steam.seeded = 1;
	}
}

static void installed_save(void)
{
	char path[1024];
	FILE *file;
	int i;

	data_path(path, sizeof(path), "steam");
	make_folders(path, 1);
	data_path(path, sizeof(path), "steam/downloads.txt");
	file = fopen(path, "w");
	if (!file)
		return;
	if (steam.shared_manifest)
		fprintf(file, "shared %llu\n", (unsigned long long)steam.shared_manifest);
	for (i = 0; i < steam.installed_count; i++)
		fprintf(file, "%llu %llu\n", (unsigned long long)steam.installed[i].id,
			(unsigned long long)steam.installed[i].manifest);
	fclose(file);
}

static void installed_load(void)
{
	char path[1024];
	char line[128];
	FILE *file;

	steam.installed_count = 0;
	data_path(path, sizeof(path), "steam/downloads.txt");
	file = fopen(path, "r");
	while (file && fgets(line, sizeof(line), file))
	{
		unsigned long long first = 0, second = 0;

		if (!strncmp(line, "shared ", 7))
			steam.shared_manifest = strtoull(line + 7, NULL, 10);
		else if (sscanf(line, "%llu %llu", &first, &second) == 2 && steam.installed_count < MAXIMUM_INSTALLED)
		{
			steam.installed[steam.installed_count].id = first;
			steam.installed[steam.installed_count].manifest = second;
			steam.installed_count++;
		}
	}
	if (file)
		fclose(file);
}

/* (state_lock held) */
static void installed_set(uint64_t id, uint64_t manifest)
{
	int i;

	for (i = 0; i < steam.installed_count; i++)
	{
		if (steam.installed[i].id == id)
		{
			steam.installed[i].manifest = manifest;
			return;
		}
	}
	if (steam.installed_count < MAXIMUM_INSTALLED)
	{
		steam.installed[steam.installed_count].id = id;
		steam.installed[steam.installed_count].manifest = manifest;
		steam.installed_count++;
	}
}

/* 0 not here, 1 here and current, 2 here but older (state_lock held) */
static int item_installed(struct steam_workshop_file const *item)
{
	char path[1024];
	char folder[32];
	int directory = 0;
	int i;

	mcc_root(path, sizeof(path));
	snprintf(folder, sizeof(folder), "/%llu", (unsigned long long)item->id);
	strncat(path, folder, sizeof(path) - strlen(path) - 1);
	if (!path_exists(path, &directory) || !directory)
		return 0;
	for (i = 0; i < steam.installed_count; i++)
		if (steam.installed[i].id == item->id)
			return steam.installed[i].manifest == item->manifest ? 1 : 2;
	return 1;
}

static int shared_installed(void)
{
	char root[1024];
	int i;

	mcc_root(root, sizeof(root));
	for (i = 0; i < SHARED_FILES; i++)
	{
		char path[1200];

		snprintf(path, sizeof(path), "%s/%s/%s", root, shared_files[i].folder, shared_files[i].name);
		if (!path_exists(path, NULL))
			return 0;
	}
	return 1;
}

/* ---------- state for the screen */

static void say(char const *format, char const *detail)
{
	pthread_mutex_lock(&steam.state_lock);
	snprintf(steam.message, sizeof(steam.message), format, detail ? detail : "");
	pthread_mutex_unlock(&steam.state_lock);
	platform_log("steam: %s", steam.message);
}

static void set_state(enum session_state state)
{
	pthread_mutex_lock(&steam.state_lock);
	steam.state = state;
	pthread_mutex_unlock(&steam.state_lock);
}

static void qr_set(char const *url)
{
	uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];

	pthread_mutex_lock(&steam.state_lock);
	if (strcmp(steam.qr_url, url))
	{
		snprintf(steam.qr_url, sizeof(steam.qr_url), "%s", url);
		steam.qr_ready = url[0] && qrcodegen_encodeText(url, scratch, steam.qr, qrcodegen_Ecc_MEDIUM,
			qrcodegen_VERSION_MIN, QR_VERSION, qrcodegen_Mask_AUTO, true);
		steam.qr_serial++;
	}
	pthread_mutex_unlock(&steam.state_lock);
}

/* ---------- the session (session_lock held) */

static void session_drop(void)
{
	steam_cm_close(steam.cm);
	steam.cm = NULL;
}

/* connected and signed in with the saved token: 1 */
static int session_ensure(char *error, int error_size)
{
	unsigned char machine_id[256];
	size_t machine_id_size;
	int result;

	if (steam.cm && steam_cm_connected(steam.cm))
		return 1;
	session_drop();
	if (!steam.token[0])
	{
		snprintf(error, (size_t)error_size, "Sign in with Steam first.");
		return 0;
	}
	steam.cm = steam_cm_open(error, error_size);
	if (!steam.cm)
		return 0;
	machine_id_size = steam_cm_machine_id(steam.seed, machine_id, sizeof(machine_id));
	result = steam_cm_logon_token(steam.cm, steam.token, DEVICE_NAME, machine_id, machine_id_size);
	if (result != STEAM_RESULT_OK)
	{
		snprintf(error, (size_t)error_size, "%s", steam_cm_error(steam.cm));
		session_drop();
		/* (a token Steam no longer takes: signed out) */
		if (result == STEAM_RESULT_ACCESS_DENIED || result == STEAM_RESULT_EXPIRED || result == 5 || result == 84)
		{
			steam.token[0] = 0;
			session_save();
			set_state(SESSION_OUT);
			snprintf(error, (size_t)error_size, "Steam no longer accepts this sign-in: sign in again.");
		}
		return 0;
	}
	return 1;
}

static void do_sign_in(void)
{
	char error[256];
	char token[STEAM_TOKEN_SIZE];
	char account[STEAM_ACCOUNT_SIZE];
	struct steam_qr qr;
	struct steam_cm *cm;

	set_state(SESSION_CONNECTING);
	say("Connecting to Steam...%s", NULL);
	cm = steam_cm_open(error, sizeof(error));
	if (!cm)
	{
		set_state(SESSION_OUT);
		say("%s", error);
		return;
	}
	if (!steam_cm_qr_begin(cm, DEVICE_NAME, &qr))
	{
		set_state(SESSION_OUT);
		say("%s", steam_cm_error(cm));
		steam_cm_close(cm);
		return;
	}
	qr_set(qr.url);
	set_state(SESSION_QR);
	say("%s", "");
	for (;;)
	{
		unsigned long until = system_milliseconds() + (unsigned long)(qr.interval * 1000.0f);
		int result;
		int cancelled = 0;

		while (!cancelled && (long)(until - system_milliseconds()) > 0)
		{
			struct timespec pause = { 0, 100000000L };

			nanosleep(&pause, NULL);
			pthread_mutex_lock(&steam.state_lock);
			cancelled = steam.cancel_sign_in || !steam.open;
			pthread_mutex_unlock(&steam.state_lock);
		}
		if (cancelled)
		{
			steam_cm_close(cm);
			qr_set("");
			set_state(SESSION_OUT);
			say("Signing in was cancelled.%s", NULL);
			return;
		}
		result = steam_cm_qr_poll(cm, &qr, token, sizeof(token), account, sizeof(account));
		if (result < 0)
		{
			say("The sign-in ran out or was refused: try again. (%s)", steam_cm_error(cm));
			steam_cm_close(cm);
			qr_set("");
			set_state(SESSION_OUT);
			return;
		}
		qr_set(qr.url);
		if (result > 0)
			break;
	}
	steam_cm_close(cm);
	qr_set("");
	set_state(SESSION_SIGNING_IN);
	pthread_mutex_lock(&steam.session_lock);
	snprintf(steam.token, sizeof(steam.token), "%s", token);
	pthread_mutex_lock(&steam.state_lock);
	snprintf(steam.account, sizeof(steam.account), "%s", account);
	pthread_mutex_unlock(&steam.state_lock);
	session_save();
	session_drop();
	if (!session_ensure(error, sizeof(error)))
	{
		pthread_mutex_unlock(&steam.session_lock);
		set_state(SESSION_OUT);
		say("%s", error);
		return;
	}
	pthread_mutex_unlock(&steam.session_lock);
	set_state(SESSION_IN);
	say("Signed in.%s", NULL);
	pthread_mutex_lock(&steam.state_lock);
	steam.want_page = 1;
	pthread_mutex_unlock(&steam.state_lock);
}

static void do_resume(void)
{
	char error[256];
	int ok;

	pthread_mutex_lock(&steam.session_lock);
	if (!steam.token[0])
	{
		pthread_mutex_unlock(&steam.session_lock);
		set_state(SESSION_OUT);
		return;
	}
	set_state(SESSION_SIGNING_IN);
	ok = session_ensure(error, sizeof(error));
	pthread_mutex_unlock(&steam.session_lock);
	if (!ok)
	{
		/* (still signed in when Steam could not be reached: tried again
		at the next page) */
		pthread_mutex_lock(&steam.state_lock);
		steam.state = steam.token[0] ? SESSION_IN : SESSION_OUT;
		snprintf(steam.message, sizeof(steam.message), "%s", error);
		pthread_mutex_unlock(&steam.state_lock);
		return;
	}
	set_state(SESSION_IN);
	pthread_mutex_lock(&steam.state_lock);
	steam.want_page = 1;
	steam.message[0] = 0;
	pthread_mutex_unlock(&steam.state_lock);
}

static void do_sign_out(void)
{
	pthread_mutex_lock(&steam.session_lock);
	session_drop();
	steam.token[0] = 0;
	pthread_mutex_lock(&steam.state_lock);
	steam.account[0] = 0;
	steam.item_count = 0;
	steam.total = 0;
	steam.state = SESSION_OUT;
	snprintf(steam.message, sizeof(steam.message), "Signed out.");
	pthread_mutex_unlock(&steam.state_lock);
	session_save();
	pthread_mutex_unlock(&steam.session_lock);
}

static void do_page(void)
{
	static struct steam_workshop_file files[WORKSHOP_PAGE_ITEMS];
	char error[256];
	uint32_t total = 0;
	int count = -1;
	int page, sort;

	pthread_mutex_lock(&steam.state_lock);
	page = steam.page < 1 ? 1 : steam.page;
	sort = steam.sort;
	steam.loading = 1;
	pthread_mutex_unlock(&steam.state_lock);
	pthread_mutex_lock(&steam.session_lock);
	if (session_ensure(error, sizeof(error)))
	{
		count = steam_cm_workshop_query(steam.cm, STEAM_APP_MCC, WORKSHOP_TAG, "", sorts[sort], page, files,
			WORKSHOP_PAGE_ITEMS, &total);
		if (count < 0)
		{
			snprintf(error, sizeof(error), "%s", steam_cm_error(steam.cm));
			session_drop();
		}
	}
	pthread_mutex_unlock(&steam.session_lock);
	pthread_mutex_lock(&steam.state_lock);
	steam.loading = 0;
	if (count >= 0)
	{
		int i, kept = 0;

		/* (the items with files: an item without a manifest has none to download) */
		for (i = 0; i < count; i++)
			if (files[i].manifest)
				steam.items[kept++] = files[i];
		steam.item_count = kept;
		steam.total = total;
		steam.message[0] = 0;
	}
	else
		snprintf(steam.message, sizeof(steam.message), "The Workshop could not be listed: %s", error);
	pthread_mutex_unlock(&steam.state_lock);
}

static void *session_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		int resume, sign_in, sign_out, page;

		pthread_mutex_lock(&steam.state_lock);
		while (!steam.want_resume && !steam.want_sign_in && !steam.want_sign_out && !steam.want_page)
			pthread_cond_wait(&steam.wake, &steam.state_lock);
		resume = steam.want_resume;
		sign_in = steam.want_sign_in;
		sign_out = steam.want_sign_out;
		page = steam.want_page;
		steam.want_resume = steam.want_sign_in = steam.want_sign_out = steam.want_page = 0;
		steam.cancel_sign_in = 0;
		pthread_mutex_unlock(&steam.state_lock);
		if (sign_out)
			do_sign_out();
		else if (sign_in)
			do_sign_in();
		else if (resume)
			do_resume();
		if (page && !sign_out)
		{
			pthread_mutex_lock(&steam.state_lock);
			page = steam.state == SESSION_IN;
			pthread_mutex_unlock(&steam.state_lock);
			if (page)
				do_page();
		}
		/* (a page asked for by a sign-in just done) */
		pthread_mutex_lock(&steam.state_lock);
		page = steam.want_page && steam.state == SESSION_IN;
		if (page)
			steam.want_page = 0;
		pthread_mutex_unlock(&steam.state_lock);
		if (page)
			do_page();
	}
	return NULL;
}

/* ---------- downloads */

static void download_progress(uint64_t done, void *context)
{
	uint64_t base = *(uint64_t const *)context;

	pthread_mutex_lock(&steam.state_lock);
	steam.done = base + done;
	pthread_mutex_unlock(&steam.state_lock);
}

static int download_cancelled(void *context)
{
	int cancelled;

	(void)context;
	pthread_mutex_lock(&steam.state_lock);
	cancelled = steam.cancel_download;
	pthread_mutex_unlock(&steam.state_lock);
	return cancelled;
}

/* a depot's key and a manifest of it, from the session */
static int depot_open(uint32_t depot, uint64_t manifest, unsigned char key[32], struct steam_depot_manifest *files,
	char *error, int error_size)
{
	uint64_t code = 0;
	int result = STEAM_RESULT_FAIL;

	pthread_mutex_lock(&steam.session_lock);
	if (session_ensure(error, error_size))
	{
		result = steam_cm_depot_key(steam.cm, depot, STEAM_APP_MCC, key);
		if (result == STEAM_RESULT_OK)
			code = steam_cm_manifest_code(steam.cm, STEAM_APP_MCC, depot, manifest);
		if (!code && result == STEAM_RESULT_OK)
			snprintf(error, (size_t)error_size, "%s", steam_cm_error(steam.cm));
		else if (result != STEAM_RESULT_OK)
			snprintf(error, (size_t)error_size, "Steam gives this account no key to these files (result %d): it "
				"must own Halo: The Master Chief Collection%s.", result,
				depot == STEAM_HALO1_DEPOT ? " and Halo: Combat Evolved Anniversary" : "");
	}
	pthread_mutex_unlock(&steam.session_lock);
	if (!code)
		return 0;
	return steam_depot_manifest(depot, manifest, code, key, files, error, error_size);
}

/* the public manifest of a depot of the app's information: 0 if none */
static uint64_t public_manifest(char const *info, uint32_t depot)
{
	char name[32];
	char const *at;
	char const *gid;

	snprintf(name, sizeof(name), "\"%u\"", (unsigned)depot);
	at = strstr(info, name);
	if (!at || !(at = strstr(at, "\"manifests\"")) || !(at = strstr(at, "\"public\"")))
		return 0;
	at += 8;
	while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')
		at++;
	/* ("public" "<gid>", or "public" { "gid" "<gid>" ... }) */
	if (*at == '{')
	{
		gid = strstr(at, "\"gid\"");
		if (!gid)
			return 0;
		at = gid + 5;
		while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')
			at++;
	}
	return *at == '"' ? strtoull(at + 1, NULL, 10) : 0;
}

static void download_shared(void)
{
	char error[256];
	unsigned char key[32];
	struct steam_depot_manifest files;
	char *info = NULL;
	uint64_t manifest = 0;
	uint64_t base = 0, total = 0;
	char root[1024];
	int i;

	pthread_mutex_lock(&steam.session_lock);
	if (session_ensure(error, sizeof(error)))
	{
		info = steam_cm_app_info(steam.cm, STEAM_APP_MCC);
		if (!info)
			snprintf(error, sizeof(error), "%s", steam_cm_error(steam.cm));
	}
	pthread_mutex_unlock(&steam.session_lock);
	if (info)
	{
		manifest = public_manifest(info, STEAM_HALO1_DEPOT);
		free(info);
		if (!manifest)
			snprintf(error, sizeof(error), "Steam's information names no files of Halo: CE Anniversary.");
	}
	if (!manifest || !depot_open(STEAM_HALO1_DEPOT, manifest, key, &files, error, sizeof(error)))
	{
		pthread_mutex_lock(&steam.state_lock);
		if (strstr(error, "no key"))
			steam.owns_halo1 = 0;
		pthread_mutex_unlock(&steam.state_lock);
		say("The shared files could not be downloaded: %s", error);
		return;
	}
	pthread_mutex_lock(&steam.state_lock);
	steam.owns_halo1 = 1;
	pthread_mutex_unlock(&steam.state_lock);
	for (i = 0; i < SHARED_FILES; i++)
	{
		struct steam_depot_file const *file = steam_depot_find(&files, shared_files[i].from);

		if (!file)
		{
			steam_depot_manifest_free(&files);
			say("Halo: CE Anniversary's files have no %s.", shared_files[i].from);
			return;
		}
		total += file->size;
	}
	pthread_mutex_lock(&steam.state_lock);
	steam.size = total;
	steam.done = 0;
	pthread_mutex_unlock(&steam.state_lock);
	mcc_root(root, sizeof(root));
	for (i = 0; i < SHARED_FILES; i++)
	{
		struct steam_depot_file const *file = steam_depot_find(&files, shared_files[i].from);
		struct steam_depot_progress progress;
		char path[1200];

		snprintf(path, sizeof(path), "%s/%s/%s", root, shared_files[i].folder, shared_files[i].name);
		make_folders(path, 0);
		pthread_mutex_lock(&steam.state_lock);
		snprintf(steam.downloading_name, sizeof(steam.downloading_name), "%s", shared_files[i].from);
		pthread_mutex_unlock(&steam.state_lock);
		progress.progress = download_progress;
		progress.cancelled = download_cancelled;
		progress.context = &base;
		if (!steam_depot_download(STEAM_HALO1_DEPOT, key, file, path, &progress, error, sizeof(error)))
		{
			steam_depot_manifest_free(&files);
			say(strcmp(error, "cancelled") ? "The shared files could not be downloaded: %s" : "The download was %s.",
				error);
			return;
		}
		base += file->size;
	}
	steam_depot_manifest_free(&files);
	pthread_mutex_lock(&steam.state_lock);
	steam.shared_manifest = manifest;
	steam.rescan = 1;
	installed_save();
	pthread_mutex_unlock(&steam.state_lock);
	say("MCC's shared files are in mcc_maps/_shared.%s", NULL);
}

/* where a file of an item goes in its package: its path there
(lowercase), or 0 when the package does not keep it */
static int item_path(char const *name, char *path, size_t size)
{
	char lower[260];
	char const *leaf;
	size_t length = strlen(name), i;
	int map;

	if (!length || length >= sizeof(lower))
		return 0;
	for (i = 0; i <= length; i++)
		lower[i] = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + 32) : name[i];
	/* (no part a path could climb or hide with) */
	if (lower[0] == '/' || strstr(lower, "..") || strstr(lower, "//") || strchr(lower, ':'))
		return 0;
	map = length > 4 && !strcmp(lower + length - 4, ".map");
	if (map)
	{
		/* (a level's or resource's map: of maps/ or halo1/maps/, at the
		package's top) */
		if (!strncmp(lower, "maps/", 5))
			leaf = lower + 5;
		else if (!strncmp(lower, "halo1/maps/", 11))
			leaf = lower + 11;
		else
			return 0;
		if (strchr(leaf, '/') || !*leaf)
			return 0;
		snprintf(path, size, "%s", leaf);
		return 1;
	}
	if ((length > 5 && !strcmp(lower + length - 5, ".json")) || (length > 4 && !strcmp(lower + length - 4, ".png")))
	{
		snprintf(path, size, "%s", lower);
		return 1;
	}
	return 0;
}

static void download_item(struct steam_workshop_file const *item)
{
	char error[256];
	unsigned char key[32];
	struct steam_depot_manifest files;
	char staging[1024];
	char root[1024];
	char destination[1100];
	char old[1100];
	char folder[32];
	uint64_t base = 0, total = 0;
	int i, maps = 0;
	FILE *sidecar;

	if (!depot_open(STEAM_APP_MCC, item->manifest, key, &files, error, sizeof(error)))
	{
		say("The item could not be downloaded: %s", error);
		return;
	}
	for (i = 0; i < files.file_count; i++)
	{
		char path[260];

		if (!files.files[i].directory && item_path(files.files[i].name, path, sizeof(path)))
		{
			total += files.files[i].size;
			maps += strstr(path, ".map") != NULL;
		}
	}
	if (!maps)
	{
		steam_depot_manifest_free(&files);
		say("This item has no Halo 1 maps to install.%s", NULL);
		return;
	}
	snprintf(folder, sizeof(folder), "%llu", (unsigned long long)item->id);
	data_path(staging, sizeof(staging), "mcc_staging/steam_");
	strncat(staging, folder, sizeof(staging) - strlen(staging) - 1);
	remove_tree(staging);
	make_folders(staging, 1);
	pthread_mutex_lock(&steam.state_lock);
	steam.size = total;
	steam.done = 0;
	pthread_mutex_unlock(&steam.state_lock);
	for (i = 0; i < files.file_count; i++)
	{
		struct steam_depot_file const *file = &files.files[i];
		struct steam_depot_progress progress;
		char relative[260];
		char path[1300];

		if (file->directory || !item_path(file->name, relative, sizeof(relative)))
			continue;
		snprintf(path, sizeof(path), "%s/%s", staging, relative);
		make_folders(path, 0);
		pthread_mutex_lock(&steam.state_lock);
		snprintf(steam.downloading_name, sizeof(steam.downloading_name), "%s", relative);
		pthread_mutex_unlock(&steam.state_lock);
		progress.progress = download_progress;
		progress.cancelled = download_cancelled;
		progress.context = &base;
		if (!steam_depot_download(STEAM_APP_MCC, key, file, path, &progress, error, sizeof(error)))
		{
			steam_depot_manifest_free(&files);
			remove_tree(staging);
			say(strcmp(error, "cancelled") ? "The item could not be downloaded: %s" : "The download was %s.", error);
			return;
		}
		base += file->size;
	}
	steam_depot_manifest_free(&files);
	/* (the item's id and title, as the browser's importer writes them:
	mcc_workshop.c's sidecar) */
	snprintf(destination, sizeof(destination), "%s/workshop_id.txt", staging);
	sidecar = fopen(destination, "w");
	if (sidecar)
	{
		char title[41];
		size_t length = 0;
		char const *at;

		for (at = item->title; *at && length < sizeof(title) - 1; at++)
			if (*at >= ' ' && *at <= '~')
				title[length++] = *at;
		title[length] = 0;
		fprintf(sidecar, "%s\n%s\n", folder, title);
		fclose(sidecar);
	}
	/* in place: an older copy moved aside first, then removed */
	mcc_root(root, sizeof(root));
	make_folders(root, 1);
	snprintf(destination, sizeof(destination), "%s/%s", root, folder);
	snprintf(old, sizeof(old), "%s.old", staging);
	remove_tree(old);
	if (path_exists(destination, NULL) && !move_folder(destination, old))
	{
		remove_tree(staging);
		say("The older copy in mcc_maps/%s could not be replaced.", folder);
		return;
	}
	if (!move_folder(staging, destination))
	{
		move_folder(old, destination);
		remove_tree(staging);
		say("The item could not be put in mcc_maps/%s.", folder);
		return;
	}
	remove_tree(old);
	pthread_mutex_lock(&steam.state_lock);
	installed_set(item->id, item->manifest);
	installed_save();
	steam.rescan = 1;
	pthread_mutex_unlock(&steam.state_lock);
	say("Installed in mcc_maps/%s.", folder);
}

static void *download_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		int what;
		struct steam_workshop_file item;

		pthread_mutex_lock(&steam.state_lock);
		while (!steam.want_download)
			pthread_cond_wait(&steam.wake, &steam.state_lock);
		what = steam.want_download;
		item = steam.download_item;
		steam.want_download = 0;
		steam.cancel_download = 0;
		steam.downloading = what;
		steam.downloading_id = what > 0 ? item.id : 0;
		steam.done = steam.size = 0;
		steam.downloading_name[0] = 0;
		pthread_mutex_unlock(&steam.state_lock);
		if (what < 0)
			download_shared();
		else
			download_item(&item);
		pthread_mutex_lock(&steam.state_lock);
		steam.downloading = 0;
		steam.downloading_id = 0;
		pthread_mutex_unlock(&steam.state_lock);
	}
	return NULL;
}

/* ---------- the screen's */

static void wake(void)
{
	pthread_cond_broadcast(&steam.wake);
}

void steam_downloads_open(void)
{
	pthread_mutex_lock(&steam.state_lock);
	steam.open = 1;
	if (!steam_net_available())
	{
		steam.state = SESSION_UNAVAILABLE;
		pthread_mutex_unlock(&steam.state_lock);
		return;
	}
	if (!steam.threads_started)
	{
		pthread_t thread;

		steam.threads_started = 1;
		pthread_mutex_unlock(&steam.state_lock);
		pthread_mutex_lock(&steam.session_lock);
		session_load();
		pthread_mutex_unlock(&steam.session_lock);
		pthread_mutex_lock(&steam.state_lock);
		installed_load();
		steam.owns_halo1 = -1;
		steam.page = 1;
		if (pthread_create(&thread, NULL, session_thread, NULL) == 0)
			pthread_detach(thread);
		if (pthread_create(&thread, NULL, download_thread, NULL) == 0)
			pthread_detach(thread);
	}
	steam.first = 0;
	steam.chosen = 0;
	if (steam.state == SESSION_OUT && steam.token[0])
		steam.want_resume = 1;
	else if (steam.state == SESSION_IN && !steam.item_count)
		steam.want_page = 1;
	wake();
	pthread_mutex_unlock(&steam.state_lock);
}

void steam_downloads_close(void)
{
	pthread_mutex_lock(&steam.state_lock);
	steam.open = 0;
	steam.cancel_sign_in = 1;
	pthread_mutex_unlock(&steam.state_lock);
}

int steam_downloads_poll(void)
{
	int rescan;

	pthread_mutex_lock(&steam.state_lock);
	rescan = steam.rescan;
	steam.rescan = 0;
	pthread_mutex_unlock(&steam.state_lock);
	return rescan;
}

/* (state_lock held) */
static int rows_count(void)
{
	if (steam.state != SESSION_IN)
		return 1;
	return FIXED_ROWS + steam.item_count + (steam.page > 1) + ((uint32_t)steam.page * WORKSHOP_PAGE_ITEMS < steam.total);
}

int steam_downloads_count(void)
{
	int count;

	pthread_mutex_lock(&steam.state_lock);
	count = rows_count();
	pthread_mutex_unlock(&steam.state_lock);
	return count;
}

short *steam_downloads_first(void)
{
	return &steam.first;
}

short *steam_downloads_chosen(void)
{
	return &steam.chosen;
}

/* what a row is: one of the fixed, an item (*item its index), or a page's */
enum row_kind { ROW_KIND_ACCOUNT, ROW_KIND_SHARED, ROW_KIND_SORT, ROW_KIND_ITEM, ROW_KIND_PREVIOUS, ROW_KIND_NEXT,
	ROW_KIND_NONE };

static enum row_kind row_kind(int place, int *item)
{
	*item = -1;
	if (place == ROW_ACCOUNT)
		return ROW_KIND_ACCOUNT;
	if (steam.state != SESSION_IN || place < 0)
		return ROW_KIND_NONE;
	if (place == ROW_SHARED)
		return ROW_KIND_SHARED;
	if (place == ROW_SORT)
		return ROW_KIND_SORT;
	place -= FIXED_ROWS;
	if (place < steam.item_count)
	{
		*item = place;
		return ROW_KIND_ITEM;
	}
	place -= steam.item_count;
	if (steam.page > 1 && place-- == 0)
		return ROW_KIND_PREVIOUS;
	if ((uint32_t)steam.page * WORKSHOP_PAGE_ITEMS < steam.total && place == 0)
		return ROW_KIND_NEXT;
	return ROW_KIND_NONE;
}

static void size_text(uint64_t bytes, char *text, int size)
{
	if (bytes >= 1024ull * 1024 * 1024)
		snprintf(text, (size_t)size, "%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
	else if (bytes >= 1024 * 1024)
		snprintf(text, (size_t)size, "%.0f MB", (double)bytes / (1024.0 * 1024));
	else
		snprintf(text, (size_t)size, "%.0f KB", (double)bytes / 1024.0);
}

static void percent_text(char *text, int size)
{
	unsigned percent = steam.size ? (unsigned)(steam.done * 100 / steam.size) : 0;

	snprintf(text, (size_t)size, "%u%%", percent > 100 ? 100 : percent);
}

void steam_downloads_row(int place, char *name, int name_size, char *state, int state_size)
{
	int item;

	name[0] = state[0] = 0;
	pthread_mutex_lock(&steam.state_lock);
	switch (row_kind(place, &item))
	{
	case ROW_KIND_ACCOUNT:
		snprintf(name, (size_t)name_size, "STEAM ACCOUNT");
		snprintf(state, (size_t)state_size, "%s",
			steam.state == SESSION_UNAVAILABLE ? "UNAVAILABLE" :
			steam.state == SESSION_CONNECTING ? "CONNECTING" :
			steam.state == SESSION_QR ? "SCAN THE CODE" :
			steam.state == SESSION_SIGNING_IN ? "SIGNING IN" :
			steam.state == SESSION_IN ? (steam.account[0] ? steam.account : "SIGNED IN") : "SIGN IN");
		break;
	case ROW_KIND_SHARED:
		snprintf(name, (size_t)name_size, "MCC SHARED FILES");
		if (steam.downloading < 0)
			percent_text(state, state_size);
		else
			snprintf(state, (size_t)state_size, "%s", shared_installed() ? "INSTALLED" :
				steam.owns_halo1 == 0 ? "NOT OWNED" : "DOWNLOAD");
		break;
	case ROW_KIND_SORT:
		snprintf(name, (size_t)name_size, "SORT");
		snprintf(state, (size_t)state_size, "%s", sort_names[steam.sort]);
		break;
	case ROW_KIND_ITEM:
	{
		struct steam_workshop_file const *file = &steam.items[item];
		int installed;

		snprintf(name, (size_t)name_size, "%s", file->title[0] ? file->title : "(untitled)");
		if (steam.downloading > 0 && steam.downloading_id == file->id)
			percent_text(state, state_size);
		else if ((installed = item_installed(file)) == 1)
			snprintf(state, (size_t)state_size, "INSTALLED");
		else if (installed == 2)
			snprintf(state, (size_t)state_size, "UPDATE");
		else
			size_text(file->size, state, state_size);
		break;
	}
	case ROW_KIND_PREVIOUS:
		snprintf(name, (size_t)name_size, "PREVIOUS PAGE");
		snprintf(state, (size_t)state_size, "%d", steam.page - 1);
		break;
	case ROW_KIND_NEXT:
		snprintf(name, (size_t)name_size, "NEXT PAGE");
		snprintf(state, (size_t)state_size, "%d OF %u", steam.page + 1,
			(unsigned)((steam.total + WORKSHOP_PAGE_ITEMS - 1) / WORKSHOP_PAGE_ITEMS));
		break;
	default:
		break;
	}
	pthread_mutex_unlock(&steam.state_lock);
}

void steam_downloads_details(int place, char *title, int title_size, char *text, int text_size)
{
	int item;
	char body[1024];

	body[0] = 0;
	title[0] = 0;
	pthread_mutex_lock(&steam.state_lock);
	switch (row_kind(place, &item))
	{
	case ROW_KIND_ACCOUNT:
		snprintf(title, (size_t)title_size, "Steam account");
		if (steam.state == SESSION_UNAVAILABLE)
			snprintf(body, sizeof(body), "This build cannot reach Steam. Use the launcher's MCC package import instead.");
		else if (steam.state == SESSION_QR)
			snprintf(body, sizeof(body), "In the Steam mobile app, open Steam Guard and scan this code. "
				"B cancels.");
		else if (steam.state == SESSION_IN)
			snprintf(body, sizeof(body), "Signed in%s%s. Browse the Halo 1 Workshop below and choose an item to "
				"install it in mcc_maps. A signs out.", steam.account[0] ? " as " : "", steam.account);
		else if (steam.state == SESSION_CONNECTING || steam.state == SESSION_SIGNING_IN)
			snprintf(body, sizeof(body), "Reaching Steam...");
		else
			snprintf(body, sizeof(body), "Sign in with the Steam mobile app to download MCC's shared files and "
				"Halo 1 Workshop maps. The account must own Halo: The Master Chief Collection. A shows the code "
				"to scan.");
		break;
	case ROW_KIND_SHARED:
		snprintf(title, (size_t)title_size, "MCC shared files");
		if (steam.downloading < 0)
		{
			char done[32], size[32];

			size_text(steam.done, done, sizeof(done));
			size_text(steam.size, size, sizeof(size));
			snprintf(body, sizeof(body), "Downloading %s: %s of %s. A cancels.", steam.downloading_name, done, size);
		}
		else
			snprintf(body, sizeof(body), "Halo 1's bitmaps and sounds from your MCC, which many Workshop maps "
				"need: into mcc_maps/_shared/singleplayer and /multiplayer. Needs Halo: CE Anniversary "
				"(MCC's Halo 1).%s", shared_installed() ? " Installed: A downloads them again." : "");
		break;
	case ROW_KIND_SORT:
		snprintf(title, (size_t)title_size, "Sort");
		snprintf(body, sizeof(body), "The Workshop's order: %s. A changes it. %u Halo 1 items.%s",
			sort_names[steam.sort], (unsigned)steam.total, steam.loading ? " Loading..." : "");
		break;
	case ROW_KIND_ITEM:
	{
		struct steam_workshop_file const *file = &steam.items[item];
		char size[32];
		int installed = item_installed(file);

		snprintf(title, (size_t)title_size, "%s", file->title[0] ? file->title : "(untitled)");
		size_text(file->size, size, sizeof(size));
		if (steam.downloading > 0 && steam.downloading_id == file->id)
		{
			char done[32], total[32];

			size_text(steam.done, done, sizeof(done));
			size_text(steam.size, total, sizeof(total));
			snprintf(body, sizeof(body), "Downloading %s: %s of %s. A cancels.", steam.downloading_name, done, total);
		}
		else
			snprintf(body, sizeof(body), "%s, %u subscribers. %s %.600s", size, (unsigned)file->subscriptions,
				installed == 1 ? "Installed in mcc_maps (A downloads it again)." :
				installed == 2 ? "An update is out: A downloads it." : "A downloads it into mcc_maps.",
				file->description);
		break;
	}
	case ROW_KIND_PREVIOUS:
	case ROW_KIND_NEXT:
		snprintf(title, (size_t)title_size, "Page %d", steam.page);
		snprintf(body, sizeof(body), "%u Halo 1 items.%s", (unsigned)steam.total, steam.loading ? " Loading..." : "");
		break;
	default:
		break;
	}
	if (steam.message[0])
		snprintf(text, (size_t)text_size, "%s\n\n%s", body, steam.message);
	else
		snprintf(text, (size_t)text_size, "%s", body);
	pthread_mutex_unlock(&steam.state_lock);
}

void steam_downloads_choose(int place)
{
	int item;

	pthread_mutex_lock(&steam.state_lock);
	switch (row_kind(place, &item))
	{
	case ROW_KIND_ACCOUNT:
		if (steam.state == SESSION_OUT)
			steam.want_sign_in = 1;
		else if (steam.state == SESSION_IN)
			steam.want_sign_out = 1;
		else if (steam.state == SESSION_QR)
			steam.cancel_sign_in = 1;
		break;
	case ROW_KIND_SHARED:
		if (steam.downloading < 0)
			steam.cancel_download = 1;
		else if (steam.downloading)
			snprintf(steam.message, sizeof(steam.message), "Wait for the download under way, or cancel it first.");
		else
			steam.want_download = -1;
		break;
	case ROW_KIND_SORT:
		steam.sort = (steam.sort + 1) % (int)(sizeof(sorts) / sizeof(sorts[0]));
		steam.page = 1;
		steam.want_page = 1;
		break;
	case ROW_KIND_ITEM:
		if (steam.downloading > 0 && steam.downloading_id == steam.items[item].id)
			steam.cancel_download = 1;
		else if (steam.downloading)
			snprintf(steam.message, sizeof(steam.message), "Wait for the download under way, or cancel it first.");
		else
		{
			steam.download_item = steam.items[item];
			steam.want_download = 1;
		}
		break;
	case ROW_KIND_PREVIOUS:
		steam.page--;
		steam.want_page = 1;
		steam.chosen = FIXED_ROWS;
		break;
	case ROW_KIND_NEXT:
		steam.page++;
		steam.want_page = 1;
		steam.chosen = FIXED_ROWS;
		break;
	default:
		break;
	}
	wake();
	pthread_mutex_unlock(&steam.state_lock);
}

int steam_downloads_back(void)
{
	int taken = 0;

	pthread_mutex_lock(&steam.state_lock);
	if (steam.state == SESSION_QR)
	{
		steam.cancel_sign_in = 1;
		taken = 1;
	}
	pthread_mutex_unlock(&steam.state_lock);
	return taken;
}

int steam_downloads_qr_size(unsigned *serial)
{
	int size;

	pthread_mutex_lock(&steam.state_lock);
	size = steam.state == SESSION_QR && steam.qr_ready ? qrcodegen_getSize(steam.qr) : 0;
	*serial = steam.qr_serial;
	pthread_mutex_unlock(&steam.state_lock);
	return size;
}

int steam_downloads_qr_dark(int x, int y)
{
	int dark;

	pthread_mutex_lock(&steam.state_lock);
	dark = steam.qr_ready && qrcodegen_getModule(steam.qr, x, y);
	pthread_mutex_unlock(&steam.state_lock);
	return dark;
}
