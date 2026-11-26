#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "n11_board.h"

enum { BOARD_SLOT = 128, FILTER_START = 96 };

typedef struct {
    uint32_t site, back, skip;
    uint64_t fingerprint;
    uint8_t size, first, resume, skip_path, back_rel, skip_rel;
    uint8_t planet_test[5], planet_test_size;
} BoardSite;

/* HellForge d8a8b46's regional, campaign and defence board stubs. The fingerprints
 * cover their full bodies with the two runtime relative addresses zeroed. */
static const BoardSite sites[] = {
    {0x11e4744, 0x11e474a, 0x11e4821, UINT64_C(0xaa69a3f3f7062798),
        93, 5, 10, 83, 79, 89, {0x83, 0x7c, 0x24, 0x48, 0x00}, 5},
    {0x11e3e67, 0x11e3e6e, 0x11e4002, UINT64_C(0x24fd71e50b96b407),
        70, 6, 11, 65, 61, 66, {0x41, 0x83, 0xfc, 0x00}, 4},
    {0x11e4287, 0x11e428e, 0x11e4429, UINT64_C(0xbf7670bf47a8addd),
        71, 6, 10, 66, 62, 67, {0x83, 0x7c, 0x24, 0x40, 0x00}, 5}
};

typedef struct {
    uintptr_t stub;
    uint8_t original[2];
    uint8_t branch[2];
    uint8_t filter[BOARD_SLOT - FILTER_START];
} BoardEdit;

static uintptr_t relative_target(uintptr_t next, const uint8_t *bytes)
{
    int32_t displacement;
    memcpy(&displacement, bytes, sizeof displacement);
    return next + (intptr_t)displacement;
}

static uint64_t fingerprint(const uint8_t *bytes, size_t size)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * UINT64_C(0x100000001b3);
    return hash;
}

static bool prepare_edit(const HfApi *api, uintptr_t game, const BoardSite *site, BoardEdit *edit)
{
    uint8_t jump[5], stub[BOARD_SLOT];
    uintptr_t entry = game + site->site;
    if (api->read(entry, jump, sizeof jump) || jump[0] != 0xe9) return false;
    edit->stub = relative_target(entry + sizeof jump, jump + 1);
    if (api->read(edit->stub, stub, sizeof stub) ||
        relative_target(edit->stub + site->back_rel + 4, stub + site->back_rel) != game + site->back ||
        relative_target(edit->stub + site->skip_rel + 4, stub + site->skip_rel) != game + site->skip ||
        memcmp(stub + FILTER_START, edit->filter, sizeof edit->filter)) return false;

    memset(stub + site->back_rel, 0, 4);
    memset(stub + site->skip_rel, 0, 4);
    if (fingerprint(stub, site->size) != site->fingerprint) return false;

    /* Only N11 reaches this branch. Use the stock stub's skip path on planet 0.
       Save flags for paths whose next instruction expects the original values. */
    uint8_t *out = edit->filter;
    *out++ = 0x9c; /* pushfq; the stack-relative planet operands include its 8-byte displacement */
    memcpy(out, site->planet_test, site->planet_test_size);
    out += site->planet_test_size;
    *out++ = 0x75; /* jne replay */
    *out++ = 3;
    *out++ = 0x9d;
    *out++ = 0xeb;
    *out = (uint8_t)(site->skip_path - (FILTER_START + (out - edit->filter) + 1));
    ++out;
    *out++ = 0x9d; /* replay the original instructions for planets other than Super Earth */
    size_t replay_size = site->resume - site->first;
    memcpy(out, stub + site->first, replay_size);
    if (site == &sites[0]) { /* The region guard ends in a relocated `ja skip`. */
        out[4] = (uint8_t)(site->skip_path - (FILTER_START + (out - edit->filter) + 5));
    }
    out += replay_size;
    *out++ = 0xeb;
    *out = (uint8_t)(site->resume - (FILTER_START + (out - edit->filter) + 1));

    memcpy(edit->original, stub + site->first, sizeof edit->original);
    edit->branch[0] = 0xeb;
    edit->branch[1] = FILTER_START - site->first - 2;
    return true;
}

static bool install_filters(const HfApi *api, uintptr_t game)
{
    BoardEdit edits[sizeof sites / sizeof *sites] = {0};
    const size_t count = sizeof edits / sizeof *edits;
    for (size_t i = 0; i < count; ++i) {
        if (!prepare_edit(api, game, &sites[i], &edits[i])) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (api->write(edits[i].stub + FILTER_START, edits[i].filter, sizeof edits[i].filter)) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!api->write(edits[i].stub + sites[i].first, edits[i].branch, sizeof edits[i].branch)) continue;
        while (i > 0) {
            --i;
            api->write(edits[i].stub + sites[i].first, edits[i].original, sizeof edits[i].original);
        }
        return false;
    }
    return true;
}

void se_limit_board_difficulty(const HfApi *api, HfMod *mod)
{
    static bool checked;
    if (checked || !api->difficulty_row(11)) return;
    checked = true;
    size_t size = 0;
    uintptr_t game = api->module_base("game.dll", &size);
    if (strcmp(api->build_id(), "25480438") || !game || size <= sites[0].skip || !install_filters(api, game)) {
        api->log(mod, "Super Earth Defense: N11 board restriction unavailable (unsupported board layout or write failure).");
        return;
    }
    api->log(mod, "Super Earth Defense: Super Earth N11 actions suppressed on the war table.");
}
