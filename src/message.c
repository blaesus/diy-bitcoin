#include <string.h>
#include <stdio.h>
#include <time.h>

#include "hash.h"
#include "message.h"
#include "command.h"
#include "globalstate.h"
#include "util.h"

#define VAR_INT_CHECKPOINT_8  0xFD
#define VAR_INT_PREFIX_16  0xFD
#define VAR_INT_CHECKPOINT_16  0xFFFF
#define VAR_INT_PREFIX_32  0xFE
#define VAR_INT_CHECKPIONT_32  0xFFFFFFFF
#define VAR_INT_PREFIX_64  0xFF

uint8_t calc_number_varint_width(uint64_t number) {
    if (number < VAR_INT_CHECKPOINT_8) {
        return 1;
    }
    else if (number <= VAR_INT_CHECKPOINT_16) {
        return 3;
    }
    else if (number <= VAR_INT_CHECKPIONT_32) {
        return 5;
    }
    else {
        return 9;
    }
}


uint8_t serialize_to_varint(
        uint64_t data,
        uint8_t *ptrBuffer
) {
    if (data < VAR_INT_CHECKPOINT_8) {
        ptrBuffer[0] = (uint8_t)data;
        return 1;
    }
    else if (data <= VAR_INT_CHECKPOINT_16) {
        ptrBuffer[0] = VAR_INT_PREFIX_16;
        memcpy(ptrBuffer+1, &data, 2);
        return 3;
    }
    else if (data <= VAR_INT_CHECKPIONT_32) {
        ptrBuffer[0] = VAR_INT_PREFIX_32;
        memcpy(ptrBuffer+1, &data, 4);
        return 5;
    }
    else {
        ptrBuffer[0] = VAR_INT_PREFIX_64;
        memcpy(ptrBuffer+1, &data, 8);
        return 9;
    }
}

void calculate_payload_checksum(void *ptrPayload, uint32_t length, uint8_t *ptrResult) {
    SHA256_HASH hash = {0};
    dsha256(ptrPayload, length, hash);
    memcpy(ptrResult, hash, CHECKSUM_SIZE);
}

void make_verack_message(struct Message *ptrMessage) {
    ptrMessage->magic = parameters.magic;
    memcpy(ptrMessage->command, CMD_VERACK, sizeof(CMD_VERACK));
    ptrMessage->length = 0;
    calculate_payload_checksum(
            ptrMessage->payload,
            ptrMessage->length,
            ptrMessage->checksum
    );
}

uint32_t make_version_payload_to_peer(
        struct Peer *ptrPeer,
        struct VersionPayload *ptrPayload
) {
    struct NetworkAddress recipientAddress = ptrPeer->address;
    uint64_t nonce = random_uint64();

    uint32_t userAgentDataLength = (uint32_t)strlen((char *)parameters.userAgent);

    ptrPayload->version = parameters.protocolVersion;
    ptrPayload->services = parameters.services;
    ptrPayload->timestamp = time(NULL);
    ptrPayload->addr_recv = recipientAddress;
    ptrPayload->addr_from = global.myAddress;
    ptrPayload->nonce = nonce;
    ptrPayload->user_agent.length = userAgentDataLength;
    memcpy(ptrPayload->user_agent.string, parameters.userAgent, userAgentDataLength);
    ptrPayload->relay = true;

    uint8_t userAgentLengthWidth = calc_number_varint_width(userAgentDataLength);

    return userAgentDataLength + userAgentLengthWidth + 85;
}



uint64_t serialize_varstr(
        struct VariableLengthString *ptrVarStr,
        uint8_t *ptrBuffer
) {
    uint8_t varintLength = serialize_to_varint(ptrVarStr->length, ptrBuffer);
    memcpy(ptrBuffer+varintLength, ptrVarStr->string, ptrVarStr->length);
    return varintLength + ptrVarStr->length;
}

uint64_t serialize_network_address(
        struct NetworkAddress *ptrAddress,
        uint8_t *ptrBuffer,
        uint32_t bufferSize
) {
    //TODO: Cehck buffer overflow
    uint8_t *p = ptrBuffer;
    memcpy(p, &ptrAddress->services, sizeof(ptrAddress->services));
    p += sizeof(ptrAddress->services);

    memcpy(p, &ptrAddress->ip, sizeof(ptrAddress->ip));
    p += sizeof(ptrAddress->ip);

    memcpy(p, &ptrAddress->port, sizeof(ptrAddress->port));
    p += sizeof(ptrAddress->port);

    return p - ptrBuffer;
}

uint64_t serialize_version_payload(
        struct VersionPayload *ptrPayload,
        uint8_t *ptrBuffer,
        uint32_t bufferSize
) {
    if (bufferSize < 0) {
        // TODO: Check buffer overflow in the following procedures;
        return 0;
    }
    uint8_t *p = ptrBuffer;
    uint64_t offset = 0;

    memcpy(p, &ptrPayload->version, sizeof(ptrPayload->version));
    p += sizeof(ptrPayload->version);

    memcpy(p, &ptrPayload->services, sizeof(ptrPayload->services));
    p += sizeof(ptrPayload->services);

    memcpy(p, &ptrPayload->timestamp, sizeof(ptrPayload->timestamp));
    p += sizeof(ptrPayload->timestamp);

    offset = serialize_network_address(&ptrPayload->addr_recv, p, bufferSize);
    p += offset;

    offset = serialize_network_address(&ptrPayload->addr_from, p, bufferSize);
    p += offset;

    memcpy(p, &ptrPayload->nonce, sizeof(ptrPayload->nonce));
    p += sizeof(ptrPayload->nonce);

    uint64_t varStrLength = serialize_varstr(&ptrPayload->user_agent, p);
    p += varStrLength;

    memcpy(p, &global.blockchainHeight, sizeof(global.blockchainHeight));
    p += sizeof(global.blockchainHeight);

    bool relay = ptrPayload->relay;
    memcpy(p, &relay, sizeof(relay));
    p += sizeof(relay);

    return (p - ptrBuffer);
}

void make_version_message(
        struct Message *ptrMessage,
        struct VersionPayload *ptrPayload,
        uint32_t payloadLength
) {
    // FIXME: redundant serialization
    uint8_t checksumCalculationBuffer[MESSAGE_BUFFER_SIZE] = {0};
    serialize_version_payload(ptrPayload, checksumCalculationBuffer, MESSAGE_BUFFER_SIZE);
    ptrMessage->magic = parameters.magic;
    memcpy(ptrMessage->command, CMD_VERSION, sizeof(CMD_VERSION));
    ptrMessage->length = payloadLength;
    ptrMessage->payload = (Payload *)ptrPayload;
    calculate_payload_checksum(
            checksumCalculationBuffer,
            ptrMessage->length,
            ptrMessage->checksum
    );
}
uint64_t serialize_version_message(
        struct Message *ptrMessage,
        uint8_t *ptrBuffer,
        uint32_t bufferSize
) {
    if (bufferSize < 0) {
        // TODO: Check buffer overflow
        return 0;
    }
    const uint64_t messageHeaderSize =
            sizeof(ptrMessage->magic)
            + sizeof(ptrMessage->command)
            + sizeof(ptrMessage->length)
            + sizeof(ptrMessage->checksum);
    memcpy(ptrBuffer, ptrMessage, messageHeaderSize);
    serialize_version_payload(
        (struct VersionPayload *)ptrMessage->payload,
        ptrBuffer+messageHeaderSize,
        1000
    );
    return messageHeaderSize + ptrMessage->length;
}
