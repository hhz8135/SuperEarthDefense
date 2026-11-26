#ifndef SE_WAR_JSON_H
#define SE_WAR_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SE_VARIANTS_DEFAULT UINT16_MAX
#define SE_VARIANTS_WIRE_MASK 0x0fffu
/* Keep the retired Hive Lord slot reserved in existing settings and co-op packets. */
#define SE_HIVE_LORD_SLOT 6u
#define SE_VARIANTS_ALL (SE_VARIANTS_WIRE_MASK & ~(1u << SE_HIVE_LORD_SLOT))

static inline uint16_t se_allowed_variants(uint16_t mask)
{
    return mask == SE_VARIANTS_DEFAULT ? mask : (uint16_t)(mask & SE_VARIANTS_ALL);
}

#define SE_BODY_LIMIT (2u * 1024u * 1024u)

typedef enum { SE_ROUTE_NONE, SE_ROUTE_STATUS, SE_ROUTE_INFO } SeRoute;

typedef struct {
    char *data;
    size_t size, capacity;
} SeBuffer;

bool se_buffer_append(SeBuffer *buffer, const char *data, size_t bytes, size_t limit);
void se_buffer_clear(SeBuffer *buffer);
bool se_rewrite(SeRoute route, const char *input, size_t bytes, const char cities[7], uint16_t variants, SeBuffer *result);

#endif
