#pragma once

#include <stdbool.h>
#include "datatypes.h"
#include "parameters.h"

struct MessageCache {
    uint64_t bufferIndex;
    Byte buffer[MAX_MESSAGE_LENGTH];
    uint64_t expectedMessageLength;
};

typedef struct MessageCache MessageCache;

struct HandshakeState {
    uint8_t acceptThem : 1;
    uint8_t acceptUs : 1;
};

struct PeerFlags {
    bool DUMMY;
};

#define REL_MY_SERVER 0
#define REL_MY_CLIENT 1

struct Peer {
    uint32_t index;
    struct HandshakeState handshake;
    uv_tcp_t *socket;
    uv_connect_t *connection;
    time_t connectionStart;
    uint8_t relationship;
    NetworkAddress address;
    MessageCache messageCache;
    uint32_t chain_height;
    struct PeerFlags flags;
};

typedef struct Peer Peer;

