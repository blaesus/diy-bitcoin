#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include "openssl/bn.h"

#include "datatypes.h"
#include "peer.h"
#include "globalstate.h"
#include "messages/shared.h"
#include "messages/version.h"
#include "messages/block.h"
#include "messages/blockreq.h"
#include "test/test.h"
#include "mine.h"
#include "hashmap.h"
#include "blockchain.h"
#include "config.h"
#include "persistent.h"

#include "utils/networking.h"
#include "utils/memory.h"
#include "utils/data.h"
#include "utils/integers.h"
#include "utils/strings.h"
#include "utils/random.h"
#include "utils/bignum.h"


static int32_t test_version_messages() {
    Message message = get_empty_message();

    struct sockaddr_in fixtureAddr;
    IP fixtureMyIp = {0};
    IP fixturePeerIp = {0};

    uv_ip4_addr("", mainnet.port, &fixtureAddr);
    convert_ipv4_address_to_ip_array(
        fixtureAddr.sin_addr.s_addr,
        fixtureMyIp
    );
    struct NetworkAddress myAddress = {
        .services = 0x40d,
        .ip = {0},
        .port = htons(0)
    };
    memcpy(myAddress.ip, fixtureMyIp, sizeof(IP));
    global.myAddress = myAddress;

    uv_ip4_addr("138.68.93.0", mainnet.port, &fixtureAddr);
    convert_ipv4_address_to_ip_array(
        fixtureAddr.sin_addr.s_addr,
        fixturePeerIp
    );
    struct Peer fixturePeer = {
        .handshake = {
            .acceptThem = false,
            .acceptUs = false,
        },
        .relationship = PEER_RELATIONSHIP_OUR_SERVER,
        .address = {
            .services = 0x9,
            .ip = {0},
            .port = htons(8333)
        }
    };
    memcpy(fixturePeer.address.ip, fixturePeerIp, sizeof(IP));
    make_version_message(&message, &fixturePeer);

    Byte *messageBuffer = CALLOC(1, MESSAGE_BUFFER_LENGTH, "test_version_messages:buffer");

    uint64_t dataSize = serialize_version_message(&message, messageBuffer);
    print_object(messageBuffer, dataSize);

    FREE(messageBuffer, "test_version_messages:buffer");

    return 0;
}

static void test_genesis() {
    Message message = get_empty_message();
    load_block_message("genesis.dat", &message);
    Byte buffer[10000] = {0};
    BlockPayload *ptrGenesisBlock = message.ptrPayload;
    uint64_t width = serialize_block_payload_header(&ptrGenesisBlock->header, buffer);
    SHA256_HASH hash = {0};
    dsha256(buffer, (uint32_t)width, hash);

    print_object(buffer, width);
    /* expected output:
    0000 - 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0010 - 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    0020 - 00 00 00 00 3b a3 ed fd 7a 7b 12 b2 7a c7 2c 3e
    0030 - 67 76 8f 61 7f c8 1b c3 88 8a 51 32 3a 9f b8 aa
    0040 - 4b 1e 5e 4a 29 ab 5f 49 ff ff 00 1d 1d ac 2b 7c END
    */

    print_object(hash, sizeof(hash));
    /* expected output:
    0000 - 6f e2 8c 0a b6 f1 b3 72 c1 a6 a2 46 ae 63 f7 4f
    0010 - 93 1e 83 65 e1 5a 08 9c 68 d6 19 00 00 00 00 00 END
     */
}

static void test_block() {
    Message message = get_empty_message();
    load_block_message("fixtures/block_7323.dat", &message);
    BlockPayload *ptrBlock = message.ptrPayload;

    print_object(ptrBlock->header.merkle_root, SHA256_LENGTH);
    /*
     * Expected:
     * 0000 - 2c 6e 39 bf 15 34 6c 13 1e 35 b6 7b 59 24 08 ef
     * 0010 - 80 92 e1 fd 92 84 19 d8 b5 2f 0b 6e f2 a6 b8 a7 END
     */

    print_block_payload(ptrBlock);
}

static void test_block_parsing_and_serialization() {
    Message message = get_empty_message();
    load_block_message("fixtures/block_7323.dat", &message);

    print_block_message(&message);

    Byte checksum[4] = {0};
    Byte payloadBuffer[MESSAGE_BUFFER_LENGTH] = {0};
    serialize_block_payload(message.ptrPayload, payloadBuffer);
    calculate_data_checksum(payloadBuffer, message.header.length, checksum);
    printf("Correct checksum:");
    print_object(message.header.checksum, 4);
    printf("Calculated checksum from parsed-and-serialized:");
    print_object(checksum, 4);
    printf("Difference = %u (expecting 0)\n", memcmp(message.header.checksum, checksum, 4));
    free_message_payload(&message);
}

static void test_merkle_on_path(char *path) {
    Message message = get_empty_message();
    load_block_message(path, &message);
    SHA256_HASH merkleRoot = {0};
    BlockPayload *ptrPayload = message.ptrPayload;
    compute_merkle_root(ptrPayload->txs, ptrPayload->txCount, merkleRoot);
    release_block(ptrPayload);
    print_object(merkleRoot, SHA256_LENGTH);
}

static void test_merkles() {
    test_merkle_on_path("genesis.dat");
    /**
     * Expect:
     * 0000 - 3b a3 ed fd 7a 7b 12 b2 7a c7 2c 3e 67 76 8f 61
     * 0010 - 7f c8 1b c3 88 8a 51 32 3a 9f b8 aa 4b 1e 5e 4a END
     */

    test_merkle_on_path("fixtures/block_7323.dat");
    /**
     * Expected:
     * 0000 - 2c 6e 39 bf 15 34 6c 13 1e 35 b6 7b 59 24 08 ef
     * 0010 - 80 92 e1 fd 92 84 19 d8 b5 2f 0b 6e f2 a6 b8 a7 END
     */
}

static void test_mine() {
    Message message = get_empty_message();
    load_block_message("genesis.dat", &message);
    BlockPayload *ptrPayload = message.ptrPayload;

    uint32_t nonce = 0;
    char label[10] = "";
    pid_t childId1 = fork();
    pid_t childId2 = fork();
    pid_t childId3 = fork();
    if (childId1 > 0) {
        if (childId2 > 0) {
            if (childId3 > 0) {
                strcpy(label, "parent");
                nonce = 0;
            }
            else {
                strcpy(label, "child1");
                nonce = UINT32_MAX / 8 * 1;
            }
        }
        else {
            if (childId3 > 0) {
                strcpy(label, "child2");
                nonce = UINT32_MAX / 8 * 2;
            }
            else {
                strcpy(label, "child3");
                nonce = UINT32_MAX / 8 * 3;
            }
        }
    }
    else {
        if (childId2 > 0) {
            if (childId3 > 0) {
                strcpy(label, "child4");
                nonce = UINT32_MAX / 8 * 4;
            }
            else {
                strcpy(label, "child5");
                nonce = UINT32_MAX / 8 * 5;
            }
        }
        else {
            if (childId3 > 0) {
                strcpy(label, "child6");
                nonce = UINT32_MAX / 8 * 6;
            }
            else {
                strcpy(label, "child7");
                nonce = UINT32_MAX / 8 * 7;
            }
        }
    }
    mine_block_header(ptrPayload->header, nonce, label);
}

void test_getheaders() {
    BlockRequestPayload payload = {
        .version = config.protocolVersion,
        .hashCount = 1,
        .hashStop = {0}
    };
    Message genesisMessage = get_empty_message();
    load_block_message("genesis.dat", &genesisMessage);
    BlockPayload *ptrBlock = (BlockPayload*) genesisMessage.ptrPayload;
    SHA256_HASH genesisHash = {0};
    dsha256(&ptrBlock->header, sizeof(ptrBlock->header), genesisHash);
    memcpy(&payload.blockLocatorHash[0], genesisHash, SHA256_LENGTH);

    Message myMessage = get_empty_message();
    make_blockreq_message(&myMessage, &payload, CMD_GETHEADERS, sizeof(CMD_GETHEADERS));
    Byte bufferGenerated[MESSAGE_BUFFER_LENGTH] = {0};
    uint64_t w1 = serialize_blockreq_message(&myMessage, bufferGenerated);

    Byte bufferFixture[MESSAGE_BUFFER_LENGTH] = {0};
    uint64_t w2 = load_file("fixtures/getheaders_initial.dat", &bufferFixture[0]);

    print_object(&bufferGenerated, w1);
    print_object(&bufferFixture, w2);
    printf("\ndiff = %x (expecting 0)", memcmp(bufferGenerated, bufferFixture, w1));
    printf("\npayload diff = %u (expecting 0)",
        memcmp(bufferGenerated + 5, bufferFixture + 5, w1 - sizeof(Header))
    );
}

void test_checksum() {
    Byte payload[] = {
        0x7f ,0x11 ,0x01 ,0x00 ,0x01 ,0x6f ,0xe2 ,0x8c
        ,0x0a ,0xb6 ,0xf1 ,0xb3 ,0x72 ,0xc1 ,0xa6 ,0xa2 ,0x46 ,0xae ,0x63 ,0xf7 ,0x4f ,0x93 ,0x1e ,0x83
        ,0x65 ,0xe1 ,0x5a ,0x08 ,0x9c ,0x68 ,0xd6 ,0x19 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00
        ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00
        ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00
    };
    Byte buffer[4] = {0};
    calculate_data_checksum(&payload, 69, buffer);
    print_object(buffer, 4);
    /*
     * Expect:
     * 0000 - 84 f4 95 8d END
     */
}

#define KEY_COUNT 4096
#define KEY_WIDTH 32
#define VALUE_WIDTH 100

void test_hashmap() {
    Hashmap *ptrHashmap = MALLOC(sizeof(Hashmap), "test_hashmap:hashmap");
    hashmap_init(ptrHashmap, (2 << 24) - 1, KEY_WIDTH);

    Byte keys[KEY_COUNT][KEY_WIDTH];
    memset(&keys, 0, sizeof(keys));
    for (uint16_t i = 0; i < KEY_COUNT; i++) {
        random_bytes(KEY_WIDTH, keys[i]);
    }

    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        Byte valueIn[VALUE_WIDTH] = {0};
        Byte *key = keys[i];
        sprintf((char*)valueIn, "data->%u", combine_uint32(key));
        hashmap_set(ptrHashmap, key, &valueIn, VALUE_WIDTH);
    }
    puts("");

    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        Byte valueIn[VALUE_WIDTH] = {0};
        Byte *key = keys[i];
        sprintf((char*)valueIn, "data->%u", combine_uint32(key));
        uint32_t valueLength = 0;
        Byte valueOut[VALUE_WIDTH];
        Byte *ptr = hashmap_get(ptrHashmap, key, &valueLength);
        if (ptr == NULL) {
            printf("in = %s, out NOT FOUND\n", valueIn);
        }
        else {
            memcpy(valueOut, ptr, valueLength);
            uint32_t diff = memcmp(valueIn, valueOut, 100);
            if (diff) {
                fprintf(stderr, "MISMATCH: in = %s, out = %s, diff = %u\n", valueIn, valueOut, memcmp(valueIn, valueOut, 100));
            }
            else {
                printf("OK ");
            }
        }
    }
    FREE(ptrHashmap, "test_hashmap_hashmap");
}

void test_difficulty() {
    uint32_t target = 0x1d00ffff;

    SHA256_HASH hash2 = {0};
    uint32_t target2 = 0x18009645;
    target_4to32(target2, hash2);
    print_object(hash2, SHA256_LENGTH);

    printf("%i", hash_satisfies_target_compact(hash2, target));
}

void test_print_hash() {
    SHA256_HASH hash = {0};
    Byte data[1] = {""};
    sha256(data, 0, hash);
    print_object(hash, SHA256_LENGTH);
    print_sha256_short(hash);
}

void test_target_conversions() {
    // TargetQuodBytes genesisQuod = {0x1a, 0xb9, 0x08, 0x18};
    TargetCompact genesisQuod = 0x1d00ffff;
    printf("%f\n", target_compact_to_float(genesisQuod));
    BIGNUM *genesisBN = BN_new();
    target_compact_to_bignum(genesisQuod, genesisBN);
    BN_add_word(genesisBN, 1);
    printf("genesisBN=%s\n", BN_bn2dec(genesisBN));

    TargetCompact genesisReconstruct = target_bignum_to_compact(genesisBN);
    printf("Regenerated genesis = %x", genesisReconstruct);
}

void test_db() {
    Message genesis = get_empty_message();
    load_block_message("genesis.dat", &genesis);
    print_block_message(&genesis);

    BlockPayload *ptrBlock = (BlockPayload*) genesis.ptrPayload;
    save_block(ptrBlock);

    SHA256_HASH genesisHash = {0};
    dsha256(&ptrBlock->header, sizeof(ptrBlock->header), genesisHash);
    BlockPayload *ptrBlockLoaded = MALLOC(sizeof(BlockPayload), "test_redis:payload");
    load_block(genesisHash, ptrBlockLoaded);
    print_block_payload(ptrBlockLoaded);
    release_block(ptrBlockLoaded);
    FREE(ptrBlockLoaded, "test_redis:payload");
}

void test_ripe() {
    char *input = "hello";
    RIPEMD_HASH hash = {0};
    sharipe(input, strlen(input), hash);
    print_object(hash, RIPEMD_LENGTH);
    /*
     * Should be:
        0000 - b6 a9 c8 c2 30 72 2b 7c 74 83 31 a8 b4 50 f0 55
        0010 - 66 dc 7d 0f END
     */
}

void test_script() {
    char *targets[] = {
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f", // genesis
        "00000000d1145790a8694403d4063f323d499e655c83426834d4ce2f8dd4a2ee", // 170, first block with real tx
        "00000000b0c5a240b2a61d2e75692224efd4cbecdf6eaf4cc2cf477ca7c270e7", // 496, multiple inputs
        "000000005a4ded781e667e06ceefafb71410b511fe0d5adc3e5a27ecbec34ae6", // 546, using transaction of the same block
        "00000000d50a3cd05e451166e5f618c76cc3273104608fe424835ae5c0d47db9", // 2817, coinbase taking fees
        "000000000000b8c3ad583c44e2655f5384f3e4e6a1f2a932b512ecc69a07cc24", // 110,300, hashtype 0
        "0000000000004939267ff84df0de0b88a7104b0b206e29a4318b3ea5ba6030d6", // 124,276, Tx #4 DER with preceding zeros
        "000000000000018f5ee13ecf9e9595356148c097a2fb5825169fde3f48e8eb8a", // OP_PUSHDATA1
        "0000000000000684e1d3f30ac8cad2c9b5be096bf7c7c77cdd57f8930673cb78", // tx #82 has missing preceding zero
        "00000000000002fce4abfccc009ddb621b28fad22dad8fc31d8620cd4716ef73", // compressed key
        "000000000000074006e067120f51b576e9cfb31f31d8d9212d6416748e650685", // tx #13: OP_CHECKMULTISIG and fancy OPs
        "00000000000004e6d451786f2ddd89c9432a24badf80f02cb8cbbc94317a765c", // hashtype 2, #53
        "0000000000000464217a15db82142b8ff50f4d783ef992534cc2162468b50d97", // hashtype 0x81
        "0000000000000449d509274d2749a62d9e003403746417b21c5d0c0df67417f8", // SIGHASH_SINGLE
        "69216b8aaa35b76d6613e5f527f4858640d986e1046238583bdad79b35e938dc", // tx, SIGHASH_SINGLE, input > output
            // see https://bitcointalk.org/index.php?topic=260595.0
        "0000000000000025d42c1a8e04ece646f7116bb4cac5abfaf16a7264f1a724c3", // OP_IF, OP_SIZE
        "61a078472543e9de9247446076320499c108b52307d8d0fafbe53b5c4e32acc4", // tx, OP_VERIFY, OP_NEGATE
        "00000000000000445a8c19b4ed54590e54a069d7cf8c3b9f7207ed7fb230aa47", // OP_DEPTH, OP_SWAP
        "000000000000000c15dfb68cc1abead192f718cf8b772977d79938f3a4259afa", // OP_1NEGATIVE, OP_LESSTHAN
        "9fb65b7304aaa77ac9580823c2c06b259cc42591e5cce66d76a81b6f51cc5c28", // Very fancy opes
        "aef4cf7abcd4344ae612d5f27735010a26e5102af20a97a5f43802583d72eb78", // OP_TUCK, OP_ROT
        "00000000000000001e038cda661d0d513723fe0e31634a0019718f343a9880c4", // OP_NUMEQUAL
        "00000000000000008205dfa0bef686a2cefe24a1fe138a350215123bc5b20136", // OP_2DUP
        "00000000000000006a2b155c209f0572c0e2470d5349aa60806e9ee5ea696066", // OP_NOT
        "8ea98508efd1f0aada5d734f743d1a2f4d23a7e8428a8be1e820391870eb9a69", // OP_0 hashing
        "2c1462024303955581e74ff750a019ed817f682191eb1ef7e3162d91a17cb633", // OP_NOTIF
        "4fea28ca023cae8498cf10541826f5da8e06eabf0d9cc03c8cdc4b91cb0c49e2", // Complex script, embedded endif
        "fcc78d0f68a3e9b3c7dc81f050714f24c2a71af1f141131d4b4992f863d0f2bc", // OP_CHECKMULTISIG without sig
        "0000000000000000128e40ab4f910b3e40395c4f1de6d2d20dbd2f906b42bcde", // 500+ stack frames
    };

    uint64_t totalTargets = sizeof(targets) / sizeof(targets[0]);
    uint64_t validCount = 0;

    for (uint32_t i = 0; i < sizeof(targets) / sizeof(char*); i ++) {
        const char *targetBlock = targets[i];
        SHA256_HASH targetHash = {0};
        sha256_hex_to_binary(targetBlock, targetHash);
        reverse_bytes(targetHash, SHA256_LENGTH);

        BlockIndex *index = GET_BLOCK_INDEX(targetHash);
        if (!index) {
            print_hash_with_description("Block index not found: ", targetHash);
            return;
        }
        BlockPayload block;
        memset(&block, 0, sizeof(block));
        load_block(targetHash, &block);
        print_block_payload(&block);
        bool valid = is_block_valid(&block, index);
        if (!valid) {
            fprintf(stderr, "\nBlock validation: INVALID %s\n====\n", targetBlock);
        }
        else {
            validCount += 1;
            printf("\nBlock validation: valid %s\n====\n", targetBlock);
        }
    }

    printf("%llu/%llu valid\n", validCount, totalTargets);
}

void test_hash() {
    Message genesis = get_empty_message();
    load_block_message("genesis.dat", &genesis);
    print_block_message(&genesis);
    BlockPayload *ptrBlock = (BlockPayload*) genesis.ptrPayload;
    SHA256_HASH genesisHash = {0};
    dsha256(&ptrBlock->header, sizeof(ptrBlock->header), genesisHash);
    char s[2 * SHA256_LENGTH] = {0};
    hash_binary_to_hex(genesisHash, s);
    printf("s=%s", s);
}

void test_file() {
    init_archive_dir();
    printf("Loading genesis block...\n");
    Message genesis = get_empty_message();
    load_block_message("genesis.dat", &genesis);
    BlockPayload *ptrBlock = (BlockPayload*) genesis.ptrPayload;
    memcpy(&global.genesisBlock, ptrBlock, sizeof(BlockPayload));
    hash_block_header(&ptrBlock->header, global.genesisHash);
    save_block(ptrBlock);

    BlockPayload *ptrBlockReloaded = calloc(1, sizeof(BlockPayload));
    load_block(global.genesisHash, ptrBlockReloaded);
    print_block_payload(ptrBlockReloaded);
}

void test_bignum() {
    Byte data[32] = {0};
    BIGNUM *num = BN_new();
    uint32_t x = 1;
    BN_set_word(num, x);
    BN_set_negative(num, -1);
    uint32_t width = bignum_to_bytes(num, data);
    BIGNUM *num_out = BN_new();
    bytes_to_bignum(data, width, num_out);
    printf("%s -> %s", BN_bn2dec(num), BN_bn2dec(num_out));

    BN_free(num);
    BN_free(num_out);
}

void test() {
    // test_version_messages();
    // test_genesis();
    // test_block();
    // test_block_parsing_and_serialization();
    // test_merkles();
    // test_mine();
    // test_getheaders();
    // test_checksum();
    // test_hashmap();
    // test_difficulty();
    // test_blockchain_validation();
    // test_print_hash();
    // test_target_conversions();
    // test_db();
    // test_ripe();
    // test_script();
    // test_hash();
    // test_file();
    test_bignum();
}
