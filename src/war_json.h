#ifndef SE_WAR_JSON_H
#define SE_WAR_JSON_H

#include <stdbool.h>
#include <stddef.h>

#define SE_BODY_LIMIT (2u * 1024u * 1024u)

typedef enum { SE_ROUTE_NONE, SE_ROUTE_STATUS, SE_ROUTE_INFO } SeRoute;

typedef struct {
    char *data;
    size_t size, capacity;
} SeBuffer;

bool se_buffer_append(SeBuffer *buffer, const char *data, size_t bytes, size_t limit);
void se_buffer_clear(SeBuffer *buffer);
bool se_rewrite(SeRoute route, const char *input, size_t bytes, SeBuffer *result);

#endif
