#include "level5/client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_FRAME (4 * 1024 * 1024)
typedef char wire_float_is_32_bit[(sizeof(float) == 4) ? 1 : -1];

static int64_t milliseconds(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (int64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static bool fail(embed_client *client, const char *message)
{
    snprintf(client->error, sizeof(client->error), "%s", message);
    embed_close(client);
    return false;
}

static bool ready(embed_client *client, short events, int64_t deadline)
{
    for (;;) {
        int64_t remaining = deadline - milliseconds();
        if (remaining <= 0)
            return fail(client, "embedding service timed out");
        struct pollfd descriptor = {.fd = client->fd, .events = events};
        int result = poll(&descriptor, 1, (int)remaining);
        if (result < 0 && errno == EINTR)
            continue;
        if (result > 0 && descriptor.revents & events)
            return true;
        if (result == 0)
            return fail(client, "embedding service timed out");
        return fail(client, "embedding service disconnected");
    }
}

static bool transfer(embed_client *client, void *data, size_t length, bool sending, int64_t deadline)
{
    char *cursor = data;
    while (length) {
        if (!ready(client, sending ? POLLOUT : POLLIN, deadline))
            return false;
        ssize_t size = sending ? send(client->fd, cursor, length, MSG_NOSIGNAL) :
                                 recv(client->fd, cursor, length, 0);
        if (size < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (size <= 0)
            return fail(client, "embedding service disconnected");
        cursor += size;
        length -= (size_t)size;
    }
    return true;
}

static uint32_t read_u32(const char *bytes)
{
    uint32_t value;
    memcpy(&value, bytes, sizeof(value));
    return ntohl(value);
}

static void write_u32(char *bytes, uint32_t value)
{
    value = htonl(value);
    memcpy(bytes, &value, sizeof(value));
}

static bool exchange(embed_client *client, unsigned role, const char *const *texts,
                     const size_t *lengths, size_t count, float *vectors)
{
    if (count > EMBED_MAX_BATCH || role > 1 || client->fd < 0)
        return fail(client, "invalid embedding request");
    size_t payload = 4;
    for (size_t i = 0; i < count; ++i) {
        if (!lengths[i] || lengths[i] > EMBED_MAX_TEXT || payload > MAX_FRAME - 5 - lengths[i])
            return fail(client, "embedding request exceeds limit");
        payload += 5 + lengths[i];
    }
    char *request = malloc(8 + payload);
    if (!request)
        return fail(client, "embedding request allocation failed");
    memcpy(request, "SFE1", 4);
    write_u32(request + 4, (uint32_t)payload);
    write_u32(request + 8, (uint32_t)count);
    size_t offset = 12;
    for (size_t i = 0; i < count; ++i) {
        request[offset] = (char)role;
        write_u32(request + offset + 1, (uint32_t)lengths[i]);
        memcpy(request + offset + 5, texts[i], lengths[i]);
        offset += 5 + lengths[i];
    }
    int64_t deadline = milliseconds() + client->timeout_ms;
    bool sent = transfer(client, request, 8 + payload, true, deadline);
    free(request);
    if (!sent)
        return false;
    char header[8];
    if (!transfer(client, header, sizeof(header), false, deadline))
        return false;
    size_t response_length = read_u32(header + 4);
    if (memcmp(header, "SFE1", 4) != 0 || response_length < 16 || response_length > MAX_FRAME)
        return fail(client, "invalid embedding response header");
    char *response = malloc(response_length);
    if (!response)
        return fail(client, "embedding response allocation failed");
    if (!transfer(client, response, response_length, false, deadline)) {
        free(response);
        return false;
    }
    size_t status = read_u32(response), dimension = read_u32(response + 4);
    size_t returned = read_u32(response + 8), name_length = read_u32(response + 12);
    bool valid = status == 0 && dimension > 0 && dimension <= EMBED_MAX_DIMENSION &&
        returned == count && name_length > 0 && name_length < sizeof(client->model) &&
        response_length == 16 + name_length + 4 * count * dimension;
    if (valid && client->dimension)
        valid = dimension == client->dimension && strlen(client->model) == name_length &&
                memcmp(client->model, response + 16, name_length) == 0;
    if (valid && !client->dimension) {
        valid = memchr(response + 16, '\0', name_length) == NULL;
        if (valid) {
            client->dimension = dimension;
            memcpy(client->model, response + 16, name_length);
            client->model[name_length] = '\0';
        }
    }
    if (valid && count) {
        for (size_t i = 0; i < count; ++i) {
            double norm = 0;
            for (size_t d = 0; d < dimension; ++d) {
                uint32_t bits = read_u32(response + 16 + name_length + 4 * (i * dimension + d));
                float value;
                memcpy(&value, &bits, sizeof(value));
                vectors[i * dimension + d] = value;
                if (!isfinite(value))
                    valid = false;
                norm += (double)value * value;
            }
            if (!(norm > 0) || !isfinite(norm))
                valid = false;
            if (valid)
                for (size_t d = 0; d < dimension; ++d)
                    vectors[i * dimension + d] /= (float)sqrt(norm);
        }
    }
    free(response);
    return valid || fail(client, "invalid embedding vectors or model changed");
}

bool embed_connect(embed_client *client, const char *path, int timeout_ms)
{
    *client = (embed_client){.fd = -1, .timeout_ms = timeout_ms};
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (!path || strlen(path) >= sizeof(address.sun_path) || timeout_ms <= 0)
        return fail(client, "invalid socket path or timeout");
    strcpy(address.sun_path, path);
    client->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client->fd < 0)
        return fail(client, "cannot create embedding socket");
    if (fcntl(client->fd, F_SETFL, O_NONBLOCK) < 0 || fcntl(client->fd, F_SETFD, FD_CLOEXEC) < 0)
        return fail(client, "cannot configure embedding socket");
    if (connect(client->fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        if (errno != EINPROGRESS)
            return fail(client, "embedding service unavailable");
        if (!ready(client, POLLOUT, milliseconds() + timeout_ms))
            return false;
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(client->fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error)
            return fail(client, "embedding service unavailable");
    }
    return exchange(client, 0, NULL, NULL, 0, NULL);
}

bool embed_vectors(embed_client *client, unsigned role, const char *const *texts,
                   const size_t *lengths, size_t count, float *vectors)
{
    return exchange(client, role, texts, lengths, count, vectors);
}

void embed_close(embed_client *client)
{
    if (client->fd >= 0)
        close(client->fd);
    client->fd = -1;
}
