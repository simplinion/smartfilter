#ifndef SMARTFILTER_EMBED_CLIENT_H
#define SMARTFILTER_EMBED_CLIENT_H
#include <stdbool.h>
#include <stddef.h>

#define EMBED_MAX_BATCH 32
#define EMBED_MAX_DIMENSION 4096
#define EMBED_MAX_TEXT 65536

typedef struct {
    int fd;
    int timeout_ms;
    size_t dimension;
    char model[256];
    char error[160];
} embed_client;

bool embed_connect(embed_client *client, const char *path, int timeout_ms);
bool embed_vectors(embed_client *client, unsigned role, const char *const *texts,
                   const size_t *lengths, size_t count, float *vectors);
void embed_close(embed_client *client);
#endif
