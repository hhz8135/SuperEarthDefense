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

typedef enum { SE_REPLACE, SE_EARTH_HEALTH, SE_INVASION_EFFECT } Change;
typedef struct { const char *key, *index, *rows; Change change; } Overlay;

static bool replace_array(SeBuffer *out, const Json *json, const Token *array, const Overlay *overlay)
{
    if (array->kind != '[' || !text(out, "[")) return false;
    bool comma = false, found = false;
    for (const Token *row = array + 1; row < json->tokens + array->next; row = json->tokens + row->next) {
        bool earth = equal(json, field(json, row, overlay->index), "0");
        if (earth && overlay->change == SE_REPLACE) continue;
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
    if (overlay->rows[0] && !found) {
        if ((comma && !text(out, ",")) || !text(out, overlay->rows)) return false;
    }
    return text(out, "]");
}

bool se_rewrite(SeRoute route, const char *input, size_t bytes, SeBuffer *result)
{
    static const Overlay status[] = {
        {"\"planetStatus\"", "\"index\"", "", SE_EARTH_HEALTH},
        {"\"campaigns\"", "\"planetIndex\"", SE_PRESET_CAMPAIGNS, SE_REPLACE},
        {"\"planetRegions\"", "\"planetIndex\"", SE_PRESET_REGIONS, SE_REPLACE},
        {"\"planetEvents\"", "\"planetIndex\"", "", SE_REPLACE},
        {"\"planetActiveEffects\"", "\"index\"", "{\"index\":0,\"galacticEffectId\":1413}", SE_INVASION_EFFECT},
    };
    static const Overlay info[] = {
        {"\"planetRegions\"", "\"planetIndex\"", SE_PRESET_DEFINITIONS, SE_REPLACE},
    };
    if (!input || !bytes || bytes > SE_BODY_LIMIT || route == SE_ROUTE_NONE || result->size) return false;
    const Overlay *changes = route == SE_ROUTE_STATUS ? status : info;
    size_t total = route == SE_ROUTE_STATUS ? sizeof status / sizeof *status : sizeof info / sizeof *info;
    size_t max_tokens = bytes < 100000 ? bytes : 100000;
    Token *tokens = malloc(max_tokens * sizeof *tokens);
    if (!tokens) return false;
    Json json = {input, bytes, 0, tokens, 0, max_tokens};
    bool ok = value(&json, 0);
    space(&json);
    ok = ok && json.cursor == bytes && equal(&json, field(&json, tokens, "\"warId\""), "801");
    size_t written = 0, replaced = 0;
    if (ok) for (const Token *key = tokens + 1; key < tokens + tokens[0].next; ) {
        const Token *array = key + 1;
        for (size_t i = 0; i < total; ++i) {
            if (!equal(&json, key, changes[i].key)) continue;
            ok = copy_span(result, &json, written, array->start) && replace_array(result, &json, array, changes + i);
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
