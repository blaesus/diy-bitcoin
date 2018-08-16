#include <stdint.h>
#include "shared.h"
#include "util.h"

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

uint8_t parse_varint(
    uint8_t *ptrBuffer,
    uint64_t *result
) {
    uint8_t firstByte = ptrBuffer[0];
    switch (firstByte) {
        case VAR_INT_PREFIX_16: {
            *result = combine_uint16(ptrBuffer+1);
            return 3;
        }
        case VAR_INT_PREFIX_32: {
            *result = combine_uint32(ptrBuffer+1);
            return 5;
        }
        case VAR_INT_PREFIX_64: {
            *result = combine_uint64(ptrBuffer+1);
            return 9;
        }
        default: {
            *result = firstByte;
            return 1;
        }
    }
}

uint64_t serialize_varstr(
    struct VariableLengthString *ptrVarStr,
    uint8_t *ptrBuffer
) {
    uint8_t varintLength = serialize_to_varint(ptrVarStr->length, ptrBuffer);
    memcpy(ptrBuffer+varintLength, ptrVarStr->string, ptrVarStr->length);
    return varintLength + ptrVarStr->length;
}

uint64_t parse_as_varstr(
    uint8_t *ptrBuffer,
    struct VariableLengthString *ptrResult
) {
    uint64_t strLength = 0;
    uint8_t lengthWidth = parse_varint(ptrBuffer, &strLength);
    ptrResult->length = strLength;
    const uint8_t markerWidth = sizeof(uint8_t);
    memcpy(ptrResult->string, ptrBuffer + markerWidth, strLength);
    return lengthWidth + strLength;
}


uint64_t serialize_network_address(
    struct NetworkAddress *ptrAddress,
    uint8_t *ptrBuffer,
    uint32_t bufferSize
) {
    //TODO: Check buffer overflow
    uint8_t *p = ptrBuffer;
    memcpy(p, &ptrAddress->services, sizeof(ptrAddress->services));
    p += sizeof(ptrAddress->services);

    memcpy(p, &ptrAddress->ip, sizeof(ptrAddress->ip));
    p += sizeof(ptrAddress->ip);

    memcpy(p, &ptrAddress->port, sizeof(ptrAddress->port));
    p += sizeof(ptrAddress->port);

    return p - ptrBuffer;
}

uint64_t parse_network_address(
    uint8_t *ptrBuffer,
    struct NetworkAddress *ptrAddress
) {
    uint8_t *p = ptrBuffer;
    memcpy(&ptrAddress->services, p, sizeof(ptrAddress->services));
    p += sizeof(ptrAddress->services);

    memcpy(&ptrAddress->ip, p, sizeof(ptrAddress->ip));
    p += sizeof(ptrAddress->ip);

    memcpy(&ptrAddress->port, p, sizeof(ptrAddress->port));
    p += sizeof(ptrAddress->port);

    return p - ptrBuffer;
}

uint64_t parse_network_address_with_time(
    uint8_t *ptrBuffer,
    struct NetworkAddressWithTime *ptrAddress
) {
    uint8_t *p = ptrBuffer;

    memcpy(&ptrAddress->time, p, sizeof(ptrAddress->time));
    p += sizeof(ptrAddress->time);

    memcpy(&ptrAddress->services, p, sizeof(ptrAddress->services));
    p += sizeof(ptrAddress->services);

    memcpy(&ptrAddress->ip, p, sizeof(ptrAddress->ip));
    p += sizeof(ptrAddress->ip);

    memcpy(&ptrAddress->port, p, sizeof(ptrAddress->port));
    p += sizeof(ptrAddress->port);

    return p - ptrBuffer;
}

bool begins_with_header(void *p) {
    return combine_uint32(p) == parameters.magic;
}

Message get_empty_message() {
    Message message = {
        .header = {0},
        .payload = NULL
    };
    return message;
}