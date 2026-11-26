#include "war_json.h"
#include "war_preset.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool se_buffer_append(SeBuffer *buffer, const char *data, size_t bytes, size_t limit)
{
    if (buffer->size > limit || bytes > limit - buffer->size) return false;
    if (!bytes) return true;
    size_t needed = buffer->size + bytes;
    if (needed > buffer->capacity) {
        size_t grown = buffer->capacity ? buffer->capacity * 2 : 4096;
        if (grown < needed) grown = needed;
        if (grown > limit) grown = limit;
        char *storage = realloc(buffer->data, grown);
        if (!storage) return false;
        buffer->data = storage;
        buffer->capacity = grown;
    }
    memcpy(buffer->data + buffer->size, data, bytes);
    buffer->size = needed;
    return true;
}

void se_buffer_clear(SeBuffer *buffer)
{
    free(buffer->data);
    *buffer = (SeBuffer){0};
}

typedef struct { uint32_t start, end, next; char kind; } Token;
typedef struct {
    const char *text;
    size_t size, cursor;
    Token *tokens;
    size_t count, capacity;
} Json;

static void space(Json *json)
{
    while (json->cursor < json->size && strchr(" \r\n\t", json->text[json->cursor]) && json->text[json->cursor])
        ++json->cursor;
}

static bool accept(Json *json, char byte)
{
    space(json);
    if (json->cursor == json->size || json->text[json->cursor] != byte) return false;
    ++json->cursor;
    return true;
}

static bool string_end(Json *json)
{
    ++json->cursor;
    while (json->cursor < json->size) {
        unsigned char ch = (unsigned char)json->text[json->cursor++];
        if (ch == '"') return true;
        if (ch < 0x20) return false;
        if (ch == '\\') {
            if (json->cursor == json->size) return false;
            ch = (unsigned char)json->text[json->cursor++];
            if (ch == 'u') {
                if (json->size - json->cursor < 4) return false;
                for (unsigned i = 0; i < 4; ++i) {
                    unsigned char hex = (unsigned char)json->text[json->cursor++];
                    if (!hex || !strchr("0123456789abcdefABCDEF", hex)) return false;
                }
            } else if (!ch || !strchr("\"\\/bfnrt", ch)) return false;
        }
    }
    return false;
}

static bool digit(Json *json)
{
    if (json->cursor == json->size || json->text[json->cursor] < '0' || json->text[json->cursor] > '9') return false;
    ++json->cursor;
    return true;
}

static bool number_end(Json *json)
{
    if (json->text[json->cursor] == '-') ++json->cursor;
    if (json->cursor == json->size) return false;
    if (json->text[json->cursor] == '0') ++json->cursor;
    else {
        if (!digit(json)) return false;
        while (digit(json)) {}
    }
    if (json->cursor < json->size && json->text[json->cursor] == '.') {
        ++json->cursor;
        if (!digit(json)) return false;
        while (digit(json)) {}
    }
    if (json->cursor < json->size && (json->text[json->cursor] == 'e' || json->text[json->cursor] == 'E')) {
        ++json->cursor;
        if (json->cursor < json->size && (json->text[json->cursor] == '+' || json->text[json->cursor] == '-')) ++json->cursor;
        if (!digit(json)) return false;
        while (digit(json)) {}
    }
    return true;
}

static bool value(Json *json, unsigned depth)
{
    space(json);
    if (depth > 32 || json->cursor == json->size || json->count == json->capacity) return false;
    size_t at = json->count++;
    char type = json->text[json->cursor];
    json->tokens[at] = (Token){(uint32_t)json->cursor, 0, 0, type};
    if (type == '{' || type == '[') {
        ++json->cursor;
        char end = type == '{' ? '}' : ']';
        if (!accept(json, end)) {
            do {
                if (type == '{') {
                    space(json);
                    if (json->cursor == json->size || json->text[json->cursor] != '"' ||
                        !value(json, depth + 1) || !accept(json, ':')) return false;
                }
                if (!value(json, depth + 1)) return false;
            } while (accept(json, ','));
            if (!accept(json, end)) return false;
        }
    } else if (type == '"') {
        if (!string_end(json)) return false;
    } else if (type == '-' || (type >= '0' && type <= '9')) {
        if (!number_end(json)) return false;
    } else {
        const char *literal = type == 't' ? "true" : type == 'f' ? "false" : type == 'n' ? "null" : NULL;
        if (!literal || strlen(literal) > json->size - json->cursor ||
            memcmp(json->text + json->cursor, literal, strlen(literal))) return false;
        json->cursor += strlen(literal);
    }
    json->tokens[at].end = (uint32_t)json->cursor;
    json->tokens[at].next = (uint32_t)json->count;
    return true;
}

static bool equal(const Json *json, const Token *token, const char *literal)
{
    size_t length = strlen(literal);
    return token && token->end - token->start == length && !memcmp(json->text + token->start, literal, length);
}

static const Token *field(const Json *json, const Token *object, const char *quoted_key)
{
    if (!object || object->kind != '{') return NULL;
    for (const Token *key = object + 1; key < json->tokens + object->next;) {
        const Token *item = key + 1;
        if (equal(json, key, quoted_key)) return item;
        key = json->tokens + item->next;
    }
    return NULL;
}

static bool copy_span(SeBuffer *out, const Json *json, size_t first, size_t end)
{
    return se_buffer_append(out, json->text + first, end - first, SE_BODY_LIMIT);
}

static bool text(SeBuffer *out, const char *data)
{
    return se_buffer_append(out, data, strlen(data), SE_BODY_LIMIT);
}

static bool replace_scalars(SeBuffer *out, const Json *json, const Token *object)
{
    const Token *owner = field(json, object, "\"owner\"");
    const Token *health = field(json, object, "\"health\"");
    if (!owner || !health) return false;
    const Token *first = owner->start < health->start ? owner : health;
    const Token *last = first == owner ? health : owner;
    return copy_span(out, json, object->start, first->start) && text(out, first == owner ? "1" : "6200000") &&
        copy_span(out, json, first->end, last->start) && text(out, first == owner ? "6200000" : "1") &&
        copy_span(out, json, last->end, object->end);
}

/* Coordinates are JSON numbers. A zero mantissa stays zero for every legal exponent. */
static bool zero_coord(const Json *json, const Token *number)
{
    if (!number || (number->kind != '-' && number->kind != '0')) return false;
    for (uint32_t i = number->start; i < number->end; ++i) {
        char c = json->text[i];
        if (c == 'e' || c == 'E') break;
        if (c >= '1' && c <= '9') return false;
    }
    return true;
}

static unsigned placeholder_bit(const Json *json, const Token *index)
{
    if (equal(json, index, "264")) return 1;
    if (equal(json, index, "265")) return 2;
    return 0;
}

static bool at_origin(const Json *json, const Token *row)
{
    const Token *pos = field(json, row, "\"position\"");
    return zero_coord(json, field(json, pos, "\"x\"")) && zero_coord(json, field(json, pos, "\"y\""));
}

static unsigned overlaps_at_earth(const Json *json, const Token *array)
{
    if (!array || array->kind != '[') return 0;
    bool earth = false;
    unsigned mask = 0;
    for (const Token *row = array + 1; row < json->tokens + array->next; row = json->tokens + row->next) {
        const Token *index = field(json, row, "\"index\"");
        if (!at_origin(json, row)) continue;
        if (equal(json, index, "0")) earth = true;
        mask |= placeholder_bit(json, index);
    }
    return earth ? mask : 0;
}

static bool hide_placeholders(SeBuffer *out, const Json *json, const Token *array, unsigned overlapping)
{
    size_t start = array->start;
    if (overlapping) {
        for (const Token *row = array + 1; row < json->tokens + array->next; row = json->tokens + row->next) {
            unsigned bit = placeholder_bit(json, field(json, row, "\"index\""));
            if (!(overlapping & bit) || !at_origin(json, row) ||
                !equal(json, field(json, row, "\"planetNameId32\""), "1441504339") ||
                !equal(json, field(json, row, "\"settingsHash\""), bit == 1 ? "3117259984" : "3060137094")) continue;
            const Token *disabled = field(json, row, "\"disabled\"");
            if (!equal(json, disabled, "false")) continue;
            if (!copy_span(out, json, start, disabled->start) || !text(out, "true")) return false;
            start = disabled->end;
        }
    }
    return copy_span(out, json, start, array->end);
}

typedef enum { SE_REPLACE, SE_CAMPAIGNS, SE_CITY_STATUS, SE_EARTH_HEALTH, SE_INVASION_EFFECT } Change;
typedef struct { const char *key, *index, *rows; Change change; } Overlay;

/* SDK 16's galactic-war-effects.json, in the same bit order as the menu file.
 * A variant can also carry a separate war-table flag. SEAF allies are independent. */
static const struct { const char *id, *flag; } enemy_variants[] = {
    {"1202","1203"}, {"1243","1245"}, {"1244",NULL},   {"1248","1249"},
    {"1303","1310"}, {"1306","1309"}, {"1307","1308"}, {"1360","1361"},
    {"1377","1378"}, {"1380","1379"}, {"1402","1403"}, {"1413",NULL}
};

static bool matches_variant(const Json *json, const Token *id, size_t slot)
{
    return equal(json, id, enemy_variants[slot].id) ||
        (enemy_variants[slot].flag && equal(json, id, enemy_variants[slot].flag));
}

static bool is_variant_effect(const Json *json, const Token *id)
{
    for (size_t i = 0; i < sizeof enemy_variants / sizeof *enemy_variants; ++i) {
        if (matches_variant(json, id, i)) return true;
    }
    return false;
}

static bool append_effect(SeBuffer *out, bool comma, const char *id)
{
    return (!comma || text(out, ",")) && text(out, "{\"index\":0,\"galacticEffectId\":") &&
        text(out, id) && text(out, "}");
}

static bool replace_array(SeBuffer *out, const Json *json, const Token *array, const Overlay *overlay,
    const char cities[7], unsigned overlapping, uint16_t variants)
{
    if (array->kind != '[' || !text(out, "[")) return false;
    bool comma = false, found = false;
    bool select_variants = overlay->change == SE_INVASION_EFFECT && variants != SE_VARIANTS_DEFAULT;
    for (const Token *row = array + 1; row < json->tokens + array->next; row = json->tokens + row->next) {
        bool earth = equal(json, field(json, row, overlay->index), "0");
        if (earth && overlay->change == SE_INVASION_EFFECT) {
            const Token *id = field(json, row, "\"galacticEffectId\"");
            if (select_variants ? is_variant_effect(json, id) : matches_variant(json, id, SE_HIVE_LORD_SLOT)) continue;
        }
        if (earth && (overlay->change == SE_REPLACE || overlay->change == SE_CAMPAIGNS || overlay->change == SE_CITY_STATUS)) continue;
        if (overlapping && overlay->change == SE_CAMPAIGNS) {
            unsigned bit = placeholder_bit(json, field(json, row, overlay->index));
            if ((bit & overlapping) && equal(json, field(json, row, "\"id\""), bit == 1 ? "900264" : "900265") &&
                equal(json, field(json, row, "\"type\""), "0") && equal(json, field(json, row, "\"race\""), "1") &&
                equal(json, field(json, row, "\"count\""), "1")) continue;
        }
        if (comma && !text(out, ",")) return false;
        if (earth && overlay->change == SE_EARTH_HEALTH) {
            if (found || !replace_scalars(out, json, row)) return false;
            found = true;
        } else {
            if (earth && overlay->change == SE_INVASION_EFFECT && equal(json, field(json, row, "\"galacticEffectId\""), "1413"))
                found = true;
            if (!copy_span(out, json, row->start, row->end)) return false;
        }
        comma = true;
    }
    if (overlay->change == SE_EARTH_HEALTH && !found) return false;
    if (overlay->change == SE_CITY_STATUS) {
        for (size_t i = 0; i < 7; ++i) {
            if (!cities || cities[i] < '2' || cities[i] > '4') return false;
            if ((comma && !text(out, ",")) || !text(out, se_city_rows[i][cities[i] - '2'])) return false;
            comma = true;
        }
    }
    if (select_variants) {
        for (size_t i = 0; i < sizeof enemy_variants / sizeof *enemy_variants; ++i) {
            if (!(variants & (1u << i))) continue;
            if (!append_effect(out, comma, enemy_variants[i].id)) return false;
            comma = true;
            if (enemy_variants[i].flag && !append_effect(out, true, enemy_variants[i].flag)) return false;
        }
    } else if (overlay->rows[0] && !found) {
        if ((comma && !text(out, ",")) || !text(out, overlay->rows)) return false;
    }
    return text(out, "]");
}

bool se_rewrite(SeRoute route, const char *input, size_t bytes, const char cities[7], uint16_t variants, SeBuffer *result)
{
    static const Overlay status[] = {
        {"\"planetStatus\"", "\"index\"", "", SE_EARTH_HEALTH},
        {"\"campaigns\"", "\"planetIndex\"", SE_PRESET_CAMPAIGNS, SE_CAMPAIGNS},
        {"\"planetRegions\"", "\"planetIndex\"", "", SE_CITY_STATUS},
        {"\"planetEvents\"", "\"planetIndex\"", "", SE_REPLACE},
        {"\"planetActiveEffects\"", "\"index\"", "{\"index\":0,\"galacticEffectId\":1413}", SE_INVASION_EFFECT},
    };
    static const Overlay info[] = {
        {"\"planetRegions\"", "\"planetIndex\"", SE_PRESET_DEFINITIONS, SE_REPLACE},
    };
    if (!input || !bytes || bytes > SE_BODY_LIMIT || route == SE_ROUTE_NONE || result->size) return false;
    if (route == SE_ROUTE_STATUS && variants != SE_VARIANTS_DEFAULT && variants > SE_VARIANTS_WIRE_MASK) return false;
    variants = se_allowed_variants(variants);
    const Overlay *changes = route == SE_ROUTE_STATUS ? status : info;
    size_t total = route == SE_ROUTE_STATUS ? sizeof status / sizeof *status : sizeof info / sizeof *info;
    size_t max_tokens = bytes < 100000 ? bytes : 100000;
    Token *tokens = malloc(max_tokens * sizeof *tokens);
    if (!tokens) return false;
    Json json = {input, bytes, 0, tokens, 0, max_tokens};
    bool ok = value(&json, 0);
    space(&json);
    ok = ok && json.cursor == bytes && equal(&json, field(&json, tokens, "\"warId\""), "801");
    const Token *planets = ok ? field(&json, tokens, route == SE_ROUTE_INFO ? "\"planetInfos\"" : "\"planetStatus\"") : NULL;
    unsigned overlapping = overlaps_at_earth(&json, planets);
    size_t written = 0, replaced = 0;
    if (ok) for (const Token *key = tokens + 1; key < tokens + tokens[0].next; ) {
        const Token *array = key + 1;
        if (route == SE_ROUTE_INFO && array == planets && overlapping) {
            ok = copy_span(result, &json, written, array->start) && hide_placeholders(result, &json, array, overlapping);
            if (!ok) break;
            written = array->end;
        }
        for (size_t i = 0; i < total; ++i) {
            if (!equal(&json, key, changes[i].key)) continue;
            ok = copy_span(result, &json, written, array->start) && replace_array(result, &json, array, changes + i, cities, overlapping, variants);
            written = array->end;
            ++replaced;
            break;
        }
        if (!ok) break;
        key = tokens + array->next;
    }
    ok = ok && replaced == total && copy_span(result, &json, written, bytes);
    free(tokens);
    if (!ok) se_buffer_clear(result);
    return ok;
}
