#include <stdint.h>
#include <stdlib.h>

#include "messages/common.h"
#include "messages/shared.h"
#include "utils/memory.h"

int32_t make_header_only_message(
    Message *ptrMessage,
    char* command,
    uint16_t commandSize
) {
    ptrMessage->header.magic = mainnet.magic;
    memcpy(ptrMessage->header.command, command, commandSize);
    ptrMessage->header.length = 0;
    calculate_data_checksum(
        ptrMessage->ptrPayload,
        ptrMessage->header.length,
        ptrMessage->header.checksum
    );
    return 0;
}

uint64_t serialize_header_only_message(
    Message *ptrMessage,
    uint8_t *ptrBuffer
) {
    const uint64_t messageHeaderSize = sizeof(ptrMessage->header);
    memcpy(ptrBuffer, ptrMessage, messageHeaderSize);
    return messageHeaderSize + ptrMessage->header.length;
}

static uint64_t parse_iv_payload(
    Byte *ptrBuffer,
    GenericIVPayload *ptrPayload
) {
    Byte *p = ptrBuffer;
    uint64_t count = 0;
    p += parse_varint(p, &count);
    ptrPayload->count = count;
    for (uint64_t index = 0; index < count; index++) {
        p += PARSE_INTO(p, &ptrPayload->inventory[index]);
    }
    return p - ptrBuffer;
}

static uint64_t serialize_iv_payload(
    GenericIVPayload *ptrPayload,
    Byte *ptrBuffer
) {
    Byte *p = ptrBuffer;
    p += serialize_to_varint(ptrPayload->count, p);
    for (uint64_t i = 0; i < ptrPayload->count; i++) {
        InventoryVector iv = ptrPayload->inventory[i];
        p += SERIALIZE_TO(iv.type, p);
        p += SERIALIZE_TO(iv.hash, p);
    }
    return p - ptrBuffer;
}

uint64_t serialize_iv_message(
    Message *ptrMessage,
    uint8_t *ptrBuffer
) {
    Byte *p = ptrBuffer;
    p += SERIALIZE_TO(ptrMessage->header, p);
    p += serialize_iv_payload((GenericIVPayload *)ptrMessage->ptrPayload, p);
    return p - ptrBuffer;
}

int32_t parse_into_iv_message(
    Byte *ptrBuffer,
    Message *ptrMessage
) {
    Header header = get_empty_header();
    GenericIVPayload payload;
    memset(&payload, 0, sizeof(payload));
    parse_message_header(ptrBuffer, &header);
    parse_iv_payload(ptrBuffer + sizeof(header), &payload);
    memcpy(ptrMessage, &header, sizeof(header));
    ptrMessage->ptrPayload = MALLOC(sizeof(GenericIVPayload), "parse_message:payload");
    memcpy(ptrMessage->ptrPayload, &payload, sizeof(payload));
    return 0;
}

char *get_iv_type(uint32_t type) {
    static char result[255] = {0};
    switch (type) {
        case IV_TYPE_ERROR: {
            strcpy(result, "ERROR");
            break;
        }
        case IV_TYPE_MSG_TX: {
            strcpy(result, "MSG_TX");
            break;
        }
        case IV_TYPE_MSG_BLOCK: {
            strcpy(result, "MSG_BLOCK");
            break;
        }
        case IV_TYPE_MSG_FILTERED_BLOCK: {
            strcpy(result, "MSG_FILTERED_BLOCK");
            break;
        }
        case IV_TYPE_MSG_CMPCT_BLOCK: {
            strcpy(result, "MSG_CMPCT_BLOCK");
            break;
        }
        default: {
            strcpy(result, "Unknown");
        }
    }
    return result;
}

void print_iv_message(Message *ptrMessage) {
    print_message_header(ptrMessage->header);
    GenericIVPayload *ptrPayload = (GenericIVPayload *)ptrMessage->ptrPayload;
    printf("payload: count=%llu\n",
           ptrPayload->count
    );
    /*
    for (uint8_t i = 0; i < ptrPayload->count; i++) {
        InventoryVector iv = ptrPayload->inventory[i];
        char *typeString = get_iv_type(iv.type);
        printf("IV type: %s(%u)\n", typeString, iv.type);
    }
    */
}

int32_t make_iv_message(
    Message *ptrMessage,
    GenericIVPayload *ptrPayload,
    Byte *command,
    uint32_t commandSize
) {
    ptrMessage->header.magic = mainnet.magic;
    memcpy(ptrMessage->header.command, command, commandSize);

    ptrMessage->ptrPayload = MALLOC(sizeof(GenericIVPayload), "make_message:payload");
    memcpy(ptrMessage->ptrPayload, ptrPayload, sizeof(GenericIVPayload));

    Byte buffer[MESSAGE_BUFFER_LENGTH] = {0};
    uint64_t payloadLength = serialize_iv_payload(ptrPayload, buffer);
    ptrMessage->header.length = (uint32_t)payloadLength;
    calculate_data_checksum(
        &buffer,
        ptrMessage->header.length,
        ptrMessage->header.checksum
    );
    return 0;
}
