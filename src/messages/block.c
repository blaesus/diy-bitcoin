#include <stdint.h>
#include <stdlib.h>
#include <units.h>

#include "messages/block.h"
#include "utils/memory.h"
#include "utils/datetime.h"
#include "utils/data.h"

uint64_t parse_block_payload_header(Byte *ptrBuffer, BlockPayloadHeader *ptrHeader) {
    Byte *p = ptrBuffer;
    p += PARSE_INTO(p, &ptrHeader->version);
    p += PARSE_INTO(p, &ptrHeader->prev_block);
    p += PARSE_INTO(p, &ptrHeader->merkle_root);
    p += PARSE_INTO(p, &ptrHeader->timestamp);
    p += PARSE_INTO(p, &ptrHeader->target);
    p += PARSE_INTO(p, &ptrHeader->nonce);
    return p - ptrBuffer;
}

uint64_t serialize_block_payload_header(BlockPayloadHeader *ptrHeader, Byte *ptrBuffer) {
    Byte *p = ptrBuffer;
    p += SERIALIZE_TO(ptrHeader->version, p);
    p += SERIALIZE_TO(ptrHeader->prev_block, p);
    p += SERIALIZE_TO(ptrHeader->merkle_root, p);
    p += SERIALIZE_TO(ptrHeader->timestamp, p);
    p += SERIALIZE_TO(ptrHeader->target, p);
    p += SERIALIZE_TO(ptrHeader->nonce, p);
    return p - ptrBuffer;
}

int32_t parse_into_block_payload(Byte *ptrBuffer, BlockPayload *ptrBlock) {
    Byte *p = ptrBuffer;

    p += PARSE_INTO(p, &ptrBlock->header);
    p += parse_varint(p, &ptrBlock->txCount);

    ptrBlock->txs = CALLOC(ptrBlock->txCount, sizeof(TxPayload), "parse_block:txs");
    for (uint64_t i = 0; i < ptrBlock->txCount; i++) {
        p += parse_into_tx_payload(p, &ptrBlock->txs[i]);
    }
    return 0;
}

void release_block(BlockPayload *ptrBlock) {
    for (uint64_t i = 0; i < ptrBlock->txCount; i++) {
        release_items_in_tx(&ptrBlock->txs[i]);
    }
    FREE(ptrBlock->txs, "parse_block:txs");
    FREE(ptrBlock, "block_payload");
}

uint64_t serialize_block_payload(BlockPayload *ptrPayload, Byte *ptrBuffer) {
    Byte *p = ptrBuffer;

    BlockPayloadHeader *ptrHeader = &ptrPayload->header;

    p += serialize_block_payload_header(ptrHeader, p);
    p += serialize_to_varint(ptrPayload->txCount, p);

    for (uint64_t i = 0; i < ptrPayload->txCount; i++) {
        p += serialize_tx_payload(&ptrPayload->txs[i], p);
    }

    return p - ptrBuffer;
}

int32_t make_block_message(Message *ptrMessage, BlockPayload *ptrPayload) {
    ptrMessage->header.magic = mainnet.magic;
    memcpy(ptrMessage->header.command, CMD_BLOCK, sizeof(CMD_BLOCK));

    ptrMessage->ptrPayload = MALLOC(sizeof(BlockPayload), "make_message:payload");
    memcpy(ptrMessage->ptrPayload, ptrPayload, sizeof(BlockPayload));

    Byte buffer[MESSAGE_BUFFER_LENGTH] = {0};
    uint64_t payloadLength = serialize_block_payload(ptrPayload, buffer);
    ptrMessage->header.length = (uint32_t)payloadLength;
    calculate_data_checksum(
        &buffer,
        ptrMessage->header.length,
        ptrMessage->header.checksum
    );
    return 0;
}

uint64_t serialize_block_message(Message *ptrMessage, uint8_t *ptrBuffer) {
    uint64_t messageHeaderSize = sizeof(ptrMessage->header);
    memcpy(ptrBuffer, ptrMessage, messageHeaderSize);
    serialize_block_payload(
        (BlockPayload *)ptrMessage->ptrPayload,
        ptrBuffer+messageHeaderSize
    );
    return messageHeaderSize + ptrMessage->header.length;
}

uint64_t load_block_message(char *path, Message *ptrMessage) {
    FILE *file = fopen(path, "rb");

    fread(ptrMessage, sizeof(ptrMessage->header), 1, file);

    uint64_t payloadLength = ptrMessage->header.length;
    Byte *buffer = MALLOC(payloadLength, "load_block_message:buffer");
    fread(buffer, payloadLength, 1, file);

    ptrMessage->ptrPayload = CALLOC(1, sizeof(BlockPayload), "load_block_message:payload");
    parse_into_block_payload(buffer, ptrMessage->ptrPayload);
    fclose(file);
    FREE(buffer, "load_block_message:buffer");

    return sizeof(ptrMessage->header)+payloadLength;
}

void print_block_message(Message *ptrMessage) {
    print_message_header(ptrMessage->header);
    BlockPayload *ptrPayload = ptrMessage->ptrPayload;
    SHA256_HASH hash = {0};
    hash_block_header(&ptrPayload->header, hash);
    print_hash_with_description("hash = ", hash);
    printf("payload: %s; %llu transactions",
           date_string(ptrPayload->header.timestamp),
           ptrPayload->txCount
    );
    printf("\n");
}

int32_t parse_into_block_message(Byte *ptrBuffer, Message *ptrMessage) {
    Header header = get_empty_header();
    parse_message_header(ptrBuffer, &header);
    memcpy(ptrMessage, &header, sizeof(header));

    ptrMessage->ptrPayload = CALLOC(1, sizeof(BlockPayload), "parse_message:payload");
    parse_into_block_payload(ptrBuffer + sizeof(header), ptrMessage->ptrPayload);
    return 0;
}

// @see https://en.bitcoin.it/wiki/Protocol_rules#.22block.22_messages

bool is_block_legal(BlockPayload *ptrBlock) {

    bool nonEmptyTxList = ptrBlock->txCount > 0;

    bool timestampLegal = ptrBlock->header.timestamp - time(NULL) < mainnet.blockMaxForwardTimestamp;

    bool initialInputIsCoinbase = is_coinbase(&ptrBlock->txs[0].txInputs[0]);

    bool onlyOneCoinbase = true;
    for (uint64_t txIndex = 0; txIndex < ptrBlock->txCount; txIndex++) {
        TxPayload *tx = &ptrBlock->txs[txIndex];
        for (uint64_t inputIndex = 0; inputIndex < tx->txInputCount; inputIndex++) {
            if (txIndex == 0 && inputIndex == 0) {
                continue;
            }
            if (is_coinbase(&ptrBlock->txs[txIndex].txInputs[inputIndex])) {
                onlyOneCoinbase = false;
                break;
            }
        }
    }

    bool allTxLegal = true;
    for (uint64_t i = 0; i < ptrBlock->txCount; i++) {
        if (!is_tx_legal(&ptrBlock->txs[i])) {
            allTxLegal = false;
            break;
        }
    }

    bool hashSatisfiesTarget;
    SHA256_HASH headerHash = {0};
    hash_block_header(&ptrBlock->header, headerHash);
    hashSatisfiesTarget = hash_satisfies_target_compact(headerHash, ptrBlock->header.target);

    bool merkleMatch;
    SHA256_HASH computedMerkle = {0};
    compute_merkle_root(ptrBlock->txs, ptrBlock->txCount, computedMerkle);
    merkleMatch = memcmp(computedMerkle, ptrBlock->header.merkle_root, SHA256_LENGTH) == 0;

    /*
    printf("block legality: %u %u %u %u %u %u %u\n",
           nonEmptyTxList,
           timestampLegal,
           firstTxIsCoinbase,
           onlyOneCoinbase,
           allTxLegal,
           hashSatisfiesTarget,
           merkleMatch
    );
    */

    return nonEmptyTxList
           && timestampLegal
           && initialInputIsCoinbase
           && onlyOneCoinbase
           && allTxLegal
           && hashSatisfiesTarget
           && merkleMatch;
}

bool hash_satisfies_target_compact(const Byte *hash, TargetCompact target) {
    ByteArray32 targetExpanded = {0};
    target_4to32(target, targetExpanded);
    return bytescmp(hash, targetExpanded, SHA256_LENGTH) <= 0;
}

void target_4to32(uint32_t targetBytes, Byte *bytes) {
    int32_t exponentWidth = (targetBytes >> 24) - 3;
    memset(bytes, 0, 32);
    memcpy(bytes + exponentWidth, &targetBytes, TARGET_BITS_MANTISSA_WIDTH);
}

bool is_block_header_legal(BlockPayloadHeader *ptrHeader) {
    bool timestampLegal =
        (int64_t)ptrHeader->timestamp - time(NULL) < mainnet.blockMaxForwardTimestamp;
    return timestampLegal;
}

void hash_block_header(BlockPayloadHeader *ptrHeader, Byte *hash) {
    dsha256(ptrHeader, sizeof(BlockPayloadHeader), hash);
}


void print_block_payload(BlockPayload *ptrBlock) {
    printf("\n----- block -----\n");
    SHA256_HASH blockHash = {0};
    hash_block_header(&ptrBlock->header, blockHash);
    print_hash_with_description("", blockHash);
    for (uint32_t i = 0; i < ptrBlock->txCount; i++) {
        printf("\nTX #%u\n", i);
        print_tx_payload(&ptrBlock->txs[i]);
    }
    printf("----------------\n");
}

bool is_block(Message *ptrMessage) {
    return strcmp((char*)ptrMessage->header.command, CMD_BLOCK) == 0;
}
