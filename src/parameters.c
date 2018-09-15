#include <stdint.h>
#include "parameters.h"
#include "units.h"

const struct ChainParameters mainnet = {
    .magic = MAIN_NET_MAGIC,
    .minimalPeerVersion = 31800,
    .port = MAIN_NET_PORT,
    .dnsSeeds = {
        "seed.bitcoin.sipa.be",
        "dnsseed.bluematt.me",
        "dnsseed.bitcoin.dashjr.org",
        "seed.bitcoinstats.com",
        "seed.bitcoin.jonasschnelli.ch",
        "seed.btc.petertodd.org",
    },
    .genesisHeight = 0,
    .retargetPeriod = 2016,
    .retargetLookBackPeriod = 2015,
    .desiredRetargetPeriod = DAY_TO_SECOND(14),
    .blockMaxForwardTimestamp = HOUR_TO_SECOND(2),
    .scriptSigSizeLower = 2,
    .scriptSigSizeUpper = 100,
    .retargetBound = 4,
};
