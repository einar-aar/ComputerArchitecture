#include "tdt4260/cache_lab/cache_impl/simple_cache.hh"

#include <bits/stdc++.h>

#include "base/trace.hh"
#include "debug/AllAddr.hh"
#include "debug/AllCacheLines.hh"
#include "debug/TDTSimpleCache.hh"

namespace gem5
{

SimpleCache::SimpleCache(int size, int blockSize, int associativity,
                         statistics::Group *parent, const char *name)
    : size(size), blockSize(blockSize), associativity(associativity), cacheName(name),
      stats(parent, name)
{
    numEntries = this->size / this->blockSize;
    numSets = this->numEntries / this->associativity;

    // allocate entries for all sets and ways
    for (int i = 0; i < this->numSets; i++) {
        std::vector<Entry *> vec;

        // TODO: Associative: Allocate as many entries as there are ways
        // i.e. replace vector of single entry with vector of way number of entries 
        /*vec.push_back(new Entry());

        entries.push_back(vec);*/
        for (int i = 0; i < associativity; i++) vec.push_back(new Entry());
        
        entries.push_back(vec);
    }
}

SimpleCache::SimpleCacheStats::SimpleCacheStats(
    statistics::Group *parent, const char *name)
    : statistics::Group(parent, name),
    ADD_STAT(reqsReceived, statistics::units::Count::get(),
        "Number of requests received from cpu side"),
    ADD_STAT(reqsServiced, statistics::units::Count::get(),
        "Number of requests serviced at this cache level"),
    ADD_STAT(respsReceived, statistics::units::Count::get(),
        "Number of responses received from mem side") {}

void
SimpleCache::recvReq(Addr req, int size)
{
    ++stats.reqsReceived;

    int index = calculateIndex(req);
    int tag = calculateTag(req);

    DPRINTF(TDTSimpleCache, "Debug: Addr: %#x, index: %d, tag: %d, in %s\n",
            req, index, tag, cacheName);
    DPRINTF(AllAddr, "%#x\n", req);
    DPRINTF(AllCacheLines, "%#x\n", req >> ((int) std::log2(blockSize)));

    // if cache line already in cache
    if (hasLine(index, tag)) {
        ++stats.reqsServiced;
        int way = lineWay(index, tag);
        DPRINTF(TDTSimpleCache, "Hit: way: %d\n", way);

        // TODO: Associative: Update LRU info for line in entries

        entries.at(index).at(way)->lastUsed = useCounter;
        useCounter++;

        sendResp(req);
    } else{
        sendReq(req, size);
    }
}

void
SimpleCache::recvResp(Addr resp)
{
    ++stats.respsReceived;

    int index = calculateIndex(resp);
    int tag = calculateTag(resp);

    // there should never be a request (and thus a response) for a line already in the cache
    assert(!hasLine(index, tag));

    int way = oldestWay(index);
    DPRINTF(TDTSimpleCache, "Miss: Replaced way: %d\n", way);
    // TODO: Direct-Mapped: Record new cache line in entries

    // entries.at(index).at(0)->tag = (Addr)tag;

    // TODO: Associative: Record LRU info for new line in entries
    entries.at(index).at(way)->tag = (Addr)tag;
    entries.at(index).at(way)->lastUsed = useCounter;
    useCounter++;

    sendResp(resp);
}

int
SimpleCache::calculateTag(Addr req)
{
    // TODO: Direct-Mapped: Calculate tag
    // hint: req >> ((int)std::log2(...
    int tag = req >> ((int)std::log2(blockSize) + (int)std::log2(numSets));

    return tag;
}

int
SimpleCache::calculateIndex(Addr req)
{
    // TODO: Direct-Mapped: Calculate index

    int index = req >> (int)std::log2(blockSize);
    index = index & ((1 << (int)std::log2(numSets)) - 1);

    return index;
}

bool
SimpleCache::hasLine(int index, int tag)
{
    // TODO: Direct-Mapped: Check if line is already in cache

    /*return (int)entries.at(index).at(0)->tag == tag &&
            entries.at(index).at(0)->tag != MaxAddr;*/

    // TODO: Associative: Check all possible ways

    for (int i = 0; i < associativity; i++) {

        if ((int)entries.at(index).at(i)->tag == tag
        && entries.at(index).at(i)->tag != MaxAddr) {

            return true;
        }
    }
    return false;
}

int
SimpleCache::lineWay(int index, int tag)
{
    // TODO: Associative: Find in which way a cache line is stored
    int way = 4;

    for (int i = 0; i < associativity; i++) {

        if ((int)entries.at(index).at(i)->tag == tag
        && entries.at(index).at(i)->tag != MaxAddr) {

            way = i;
        }
    }
    return way;
}

int
SimpleCache::oldestWay(int index)
{
    // TODO: Associative: Determine the oldest way
    int oldest_way = 0;
    int its_since_use = entries.at(index).at(0)->lastUsed;

    for (int i = 1; i < associativity; i++) {

        if (entries.at(index).at(i)->lastUsed < its_since_use) {

            oldest_way = i;
            its_since_use = entries.at(index).at(i)->lastUsed;
        }
    }

    return oldest_way;
}

void
SimpleCache::sendReq(Addr req, int size)
{
    memSide->recvReq(req, size);
}

void
SimpleCache::sendResp(Addr resp)
{
    cpuSide->recvResp(resp);
}

}
