#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "hellforge_mod.h"
#include "war_json.h"

enum {
    CURL_OK = 0,
    CURL_BODY = 1,
    CURL_STATUS_LINE = 4 | 8,
    CURL_EFFECTIVE_URL = 0x100001,
    CURL_RESPONSE_CODE = 0x200002,
    CURL_CONTENT_LENGTH = 0x60000f,
    CURL_EFFECTIVE_METHOD = 0x10003a,
    CURL_CLIENT_WRITE_RVA = 0x3f040,
    MAX_PENDING = 8,
    SHARE_INTERVAL_MS = 250,
    WAITING_FOR_HOST = -1
};

static const uint64_t war_setting_key = UINT64_C(0x5355504552454152);

typedef int (*ClientWrite)(void *easy, int type, const char *data, size_t length);
typedef int (*GetInfo)(void *easy, int info, ...);
typedef void (*Cleanup)(void *easy);

typedef struct {
    void *easy;
    SeRoute route;
    size_t expected, received;
    SeBuffer body;
    bool bypass;
} Pending;

static const HfApi *api;
static HfMod *mod;
static ClientWrite original_write;
static Cleanup original_cleanup;
static GetInfo get_info;
static SRWLOCK pending_lock = SRWLOCK_INIT;
static Pending pending[MAX_PENDING];
static wchar_t settings_file[32768];
static volatile LONG enabled = 1;
static volatile LONG reported_status;
static volatile LONG reported_info;
static volatile LONG reported_failure;
static volatile LONG reported_enabled = -2;
static volatile LONG armed;
static ULONGLONG next_share_poll;

static bool read_setting(void)
{
    if (!settings_file[0]) return InterlockedCompareExchange(&enabled, 0, 0) != 0;
    HANDLE file = CreateFileW(settings_file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        char value[4];
        DWORD length = 0;
        if (ReadFile(file, value, sizeof value, &length, NULL) && length >= 1 && length <= 3 &&
            (value[0] == '0' || value[0] == '1')) {
            bool complete = true;
            for (DWORD i = 1; i < length; ++i) complete &= value[i] == '\r' || value[i] == '\n';
            if (complete) InterlockedExchange(&enabled, value[0] == '1');
        }
        CloseHandle(file);
    }
    return InterlockedCompareExchange(&enabled, 0, 0) != 0;
}

static bool read_shared_setting(uint8_t *value)
{
    return api->share_get(mod, war_setting_key, value, sizeof *value) == sizeof *value && *value <= 1;
}

static int effective_setting(void)
{
    HfSession before = {0};
    api->session(mod, &before);
    if (before.role != HF_SESSION_JOINER) return read_setting();

    uint8_t value = 0;
    if (!read_shared_setting(&value)) return WAITING_FOR_HOST;
    HfSession after = {0};
    api->session(mod, &after);
    return after.role == HF_SESSION_JOINER && before.version == after.version ? value : WAITING_FOR_HOST;
}

static void publish_host_setting(void)
{
    HfSession session = {0};
    api->session(mod, &session);
    if (session.role != HF_SESSION_HOST) return;
    uint8_t preference = (uint8_t)read_setting();
    uint8_t shared = 0;
    if (!read_shared_setting(&shared) || preference != shared) {
        if (!api->share_set(mod, war_setting_key, &preference, sizeof preference))
            api->log(mod, "Super Earth Defense: host shared its %s setting", preference ? "enabled" : "disabled");
    }
}

static void on_game_tick(HfMod *owner, float delta, void *user)
{
    (void)owner;
    (void)delta;
    (void)user;
    if (!InterlockedCompareExchange(&armed, 0, 0)) return;
    ULONGLONG now = GetTickCount64();
    if (now < next_share_poll) return;
    next_share_poll = now + SHARE_INTERVAL_MS;
    publish_host_setting();
}

static SeRoute target_route(const char *url)
{
    static const char prefix[] = "http://127.0.0.1:";
    if (!url || strncmp(url, prefix, sizeof prefix - 1)) return SE_ROUTE_NONE;
    const char *end = url + sizeof prefix - 1;
    unsigned port = 0;
    while (*end >= '0' && *end <= '9') {
        port = port * 10 + (unsigned)(*end++ - '0');
        if (port > 65535) return SE_ROUTE_NONE;
    }
    if (!port || *end != '/') return SE_ROUTE_NONE;
    size_t size = strcspn(end, "?#");
    static const char status[] = "/WarSeason/801/Status";
    static const char info[] = "/WarSeason/801/warinfo";
    if (size == sizeof status - 1 && !_strnicmp(end, status, size)) return SE_ROUTE_STATUS;
    if (size == sizeof info - 1 && !_strnicmp(end, info, size)) return SE_ROUTE_INFO;
    return SE_ROUTE_NONE;
}

static Pending *find_pending(void *easy)
{
    if (!easy) return NULL;
    for (size_t i = 0; i < MAX_PENDING; ++i)
        if (pending[i].easy == easy) return pending + i;
    return NULL;
}

static void clear_pending(void *easy)
{
    AcquireSRWLockExclusive(&pending_lock);
    Pending *entry = find_pending(easy);
    if (entry) {
        se_buffer_clear(&entry->body);
        *entry = (Pending){0};
    }
    ReleaseSRWLockExclusive(&pending_lock);
}

static void begin_response(void *easy)
{
    clear_pending(easy);
    char *url = NULL;
    char *method = NULL;
    if (get_info(easy, CURL_EFFECTIVE_URL, &url) != CURL_OK ||
        get_info(easy, CURL_EFFECTIVE_METHOD, &method) != CURL_OK || !method || strcmp(method, "GET")) return;
    SeRoute route = target_route(url);
    if (route == SE_ROUTE_NONE) return;
    AcquireSRWLockExclusive(&pending_lock);
    for (size_t i = 0; i < MAX_PENDING; ++i) {
        if (!pending[i].easy) {
            pending[i] = (Pending){.easy = easy, .route = route};
            break;
        }
    }
    ReleaseSRWLockExclusive(&pending_lock);
}

static void on_cleanup(void *easy)
{
    if (InterlockedCompareExchange(&armed, 0, 0)) clear_pending(easy);
    original_cleanup(easy);
}

static bool replace_body(Pending *response, SeBuffer *replacement)
{
    size_t json_size = response->body.size;
    while (json_size && response->body.data[json_size - 1] == ' ') --json_size;
    // The stock backend pads these replies. Keep Content-Length and enough spare space for the game's decoder.
    if (response->body.size - json_size < 16 ||
        !se_rewrite(response->route, response->body.data, json_size, replacement) ||
        replacement->size > response->body.size - 16) return false;
    static const char spaces[] = "                                                                ";
    while (replacement->size < response->body.size) {
        size_t count = response->body.size - replacement->size;
        if (count > sizeof spaces - 1) count = sizeof spaces - 1;
        if (!se_buffer_append(replacement, spaces, count, SE_BODY_LIMIT)) return false;
    }
    return true;
}

static int write_completed(void *easy, Pending *response)
{
    SeBuffer replacement = {0};
    bool replaced = replace_body(response, &replacement);
    const SeBuffer *payload = replaced ? &replacement : &response->body;
    if (replaced) {
        volatile LONG *reported = response->route == SE_ROUTE_STATUS ? &reported_status : &reported_info;
        if (!InterlockedExchange(reported, 1)) {
            api->log(mod, "Super Earth Defense: applied to %s, %zu bytes, unchanged content length",
                response->route == SE_ROUTE_STATUS ? "WarSeason Status" : "WarSeason WarInfo", payload->size);
            if (response->route == SE_ROUTE_STATUS) api->set_status(mod, "Super Earth Defense: seven cities available");
        }
    } else if (!InterlockedExchange(&reported_failure, 1)) {
        api->log(mod, "Super Earth Defense: unexpected local war body or padding; original response used");
    }
    int result = original_write(easy, CURL_BODY, payload->data, payload->size);
    se_buffer_clear(&replacement);
    se_buffer_clear(&response->body);
    return result;
}

static int on_write(void *easy, int type, const char *data, size_t length)
{
    if (!InterlockedCompareExchange(&armed, 0, 0) || !data || !length)
        return original_write(easy, type, data, length);
    if (type != CURL_BODY) {
        if (type == CURL_STATUS_LINE && length >= 5 && !memcmp(data, "HTTP/", 5)) begin_response(easy);
        return original_write(easy, type, data, length);
    }

    AcquireSRWLockExclusive(&pending_lock);
    Pending *entry = find_pending(easy);
    if (!entry) {
        ReleaseSRWLockExclusive(&pending_lock);
        return original_write(easy, type, data, length);
    }
    if (!entry->expected && !entry->bypass) {
        long response_code = 0;
        int64_t content_length = 0;
        // The client may cache WarInfo across lobby joins. Register static city definitions on both sides.
        // Only Status grants the campaign and marks the seven cities playable.
        int setting = entry->route == SE_ROUTE_INFO ? 1 : effective_setting();
        if (entry->route == SE_ROUTE_STATUS && InterlockedExchange(&reported_enabled, setting) != setting) {
            const char *state = setting == WAITING_FOR_HOST ? "waiting for the host's setting" : setting ? "enabled" : "disabled";
            api->log(mod, "Super Earth Defense: %s", state);
            if (setting == 1) InterlockedExchange(&reported_status, 0);
            else api->set_status(mod, setting == WAITING_FOR_HOST ? "Super Earth Defense: waiting for the host" : "Super Earth Defense: disabled");
        }
        if (data[0] != '{' || setting != 1 ||
            get_info(easy, CURL_RESPONSE_CODE, &response_code) != CURL_OK || response_code != 200 ||
            get_info(easy, CURL_CONTENT_LENGTH, &content_length) != CURL_OK ||
            content_length <= 0 || (uint64_t)content_length > SE_BODY_LIMIT || length > (size_t)content_length) entry->bypass = true;
        else entry->expected = (size_t)content_length;
    }

    if (entry->bypass || length > entry->expected - entry->received ||
        !se_buffer_append(&entry->body, data, length, SE_BODY_LIMIT)) {
        SeBuffer original = entry->body;
        entry->body = (SeBuffer){0};
        entry->bypass = true;
        ReleaseSRWLockExclusive(&pending_lock);
        int result = original.size ? original_write(easy, type, original.data, original.size) : CURL_OK;
        se_buffer_clear(&original);
        return result != CURL_OK ? result : original_write(easy, type, data, length);
    }
    entry->received += length;
    if (entry->received != entry->expected) {
        ReleaseSRWLockExclusive(&pending_lock);
        return CURL_OK;
    }
    Pending complete = *entry;
    *entry = (Pending){0};
    ReleaseSRWLockExclusive(&pending_lock);
    return write_completed(easy, &complete);
}

static void when_game_loaded(HfMod *owner, HfEvent event, void *user)
{
    (void)event;
    (void)user;
    size_t size = 0;
    uintptr_t curl = api->module_base("libcurl.dll", &size);
    static const char signature[] = "48 83 EC 38 8B C2 83 FA 01 75 2E 80 B9 CD 01 00 00 00";
    if (!curl || size < CURL_CLIENT_WRITE_RVA + 32 || !api->code_matches(curl + CURL_CLIENT_WRITE_RVA, signature)) {
        api->set_status(owner, "Super Earth Defense: unsupported HTTP library");
        api->log(owner, "Super Earth Defense: libcurl 8.4.0 entry differs; original war kept");
        return;
    }
    get_info = (GetInfo)GetProcAddress((HMODULE)curl, "curl_easy_getinfo");
    uintptr_t cleanup = (uintptr_t)GetProcAddress((HMODULE)curl, "curl_easy_cleanup");
    if (!get_info || !cleanup || api->hook(owner, cleanup, (void *)on_cleanup, (void **)&original_cleanup) ||
        api->hook(owner, curl + CURL_CLIENT_WRITE_RVA, (void *)on_write, (void **)&original_write)) {
        api->set_status(owner, "Super Earth Defense: HTTP hook unavailable");
        api->log(owner, "Super Earth Defense: could not register its hooks; original war kept");
        return;
    }
    InterlockedExchange(&armed, 1);
    api->set_status(owner, "Super Earth Defense: waiting for the war table");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi *hf, HfMod *owner)
{
    if (hf->version < 16 || hf->size < offsetof(HfApi, share_get) + sizeof hf->share_get) return 1;
    api = hf;
    mod = owner;
    const wchar_t suffix[] = L"\\CowboyBingus\\Helldivers2\\SuperEarthDefense.enabled";
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", settings_file, (DWORD)(sizeof settings_file / sizeof *settings_file));
    if (size && size + sizeof suffix / sizeof *suffix <= sizeof settings_file / sizeof *settings_file)
        memcpy(settings_file + size, suffix, sizeof suffix);
    else settings_file[0] = 0;
    if (hf->on_game_thread(owner, on_game_tick, NULL) != 1) {
        hf->log(owner, "Super Earth Defense: could not register the lobby setting update");
        return 1;
    }
    return api->on(owner, HF_EVENT_GAME_LOADED, when_game_loaded, NULL);
}
