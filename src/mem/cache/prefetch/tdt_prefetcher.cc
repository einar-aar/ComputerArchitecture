#include "mem/cache/prefetch/tdt_prefetcher.hh"

#include "debug/HWPrefetch.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "params/TDTPrefetcher.hh"

// Added
#include <unordered_set>
#include <deque>
#include <unordered_map>

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

namespace {

    // Offsets to choose from
    std::vector<int> offsetTable = {

        1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, 18, 20, 24, 25,
        27, 30, 32, 36, 40, 45, 48, 50, 54, 60, 64, 72, 75, 80, 90, 96

    };

    struct PrefetcherLevel{

        // Current offset index, best offset and round
        int offsetIndex = 0;
        int bestOffset = 1;
        int round = 0;
        
        // Offset scores
        std::vector<int> offsetScores;
        PrefetcherLevel() : offsetScores(offsetTable.size(), 0) {}

        // RR table set for quick lookup during training (unordered set uses hash table)
        // RR table queue holds the last x accesses
        std::unordered_set<Addr> RRTableSet;
        std::deque<Addr> RRTableQueue; 
    };
    
    std::unordered_map<const TDTPrefetcher*, PrefetcherLevel> prefetchers;

    const int maxRound = 100;
    const int maxScore = 30;
    const int RRTableSize = 64;
}

TDTPrefetcher::TDTEntry::TDTEntry(TagExtractor ext)
    : TaggedEntry()
{
    registerTagExtractor(ext);
    invalidate();
}

void
TDTPrefetcher::TDTEntry::invalidate()
{
    TaggedEntry::invalidate();
}

TDTPrefetcher::TDTPrefetcher(const TDTPrefetcherParams &params)
    : Queued(params),
      pcTableInfo(params.table_assoc, params.table_entries,
                  params.table_indexing_policy,
                  params.table_replacement_policy)
    {}

TDTPrefetcher::PCTable&
TDTPrefetcher::findTable(int context)
{
    auto it = pcTables.find(context);
    if (it != pcTables.end())
        return *(it->second);

    return allocateNewContext(context);
}

TDTPrefetcher::PCTable&
TDTPrefetcher::allocateNewContext(int context)
{
    assert(context == 0);
    std::string table_name = name()+".PCTable"+std::to_string(context);
    pcTables[context].reset(new PCTable(
        table_name.c_str(),
        pcTableInfo.numEntries,
        pcTableInfo.assoc,
        pcTableInfo.replacementPolicy,
        pcTableInfo.indexingPolicy,
        TDTEntry(genTagExtractor(pcTableInfo.indexingPolicy))));

    DPRINTF(HWPrefetch, "Adding context %i with tdt4260 entries\n", context);

    return *(pcTables[context]);
}

void
TDTPrefetcher::notifyFill(const CacheAccessProbeArg &arg)
{
    //A cache line has been filled in
}

void
TDTPrefetcher::calculatePrefetch(const PrefetchInfo &pfi,
                                 std::vector<AddrPriority> &addresses,
                                    const CacheAccessor &cache)
{
    if (!pfi.hasPC()) {
        DPRINTF(HWPrefetch, "Ignoring request with no PC.\n");
        return;
    }

    // access_addr is the memory address (of the cache line) requested
    Addr access_addr = pfi.getAddr();
    // access pc is the pc of the inst that requests the cache line
    Addr access_pc = pfi.getPC();

    // context can be ignored
    int context = 0;

    // Currently implemented prefetching algorithm: Next line prefetching
    // TODO: Implement something better!
    // addresses.push_back(AddrPriority(access_addr + blkSize, 0));

    // To be able to have different prefetchers for each cache level
    PrefetcherLevel &prefetcher = prefetchers[this];

    // Get cache line location, define highest score, highest score index and get offset to test
    Addr cacheLine = access_addr / blkSize;
    int highestScore = 0;
    int highestScoreIndex = 0;
    int offsetTest = offsetTable[prefetcher.offsetIndex];

    // Learning phase
    if ((Addr)offsetTest <= cacheLine) {
        Addr recentRequest = cacheLine - offsetTest;
        if (prefetcher.RRTableSet.find(recentRequest) != prefetcher.RRTableSet.end()) {
            prefetcher.offsetScores[prefetcher.offsetIndex]++;
        }
    }

    if (prefetcher.offsetIndex < offsetTable.size() - 1) prefetcher.offsetIndex++;
    else {
        prefetcher.round++;
        prefetcher.offsetIndex = 0;
    }

    for (int i : prefetcher.offsetScores) {
        if (highestScore < i) highestScore = i;
    }

    // Ending learning phase
    if (prefetcher.round >= maxRound || highestScore >= maxScore) {

        for (int i = 1; i < prefetcher.offsetScores.size(); i++) {
            if (prefetcher.offsetScores[highestScoreIndex] < prefetcher.offsetScores[i]) highestScoreIndex = i;
        }

        prefetcher.bestOffset = offsetTable[highestScoreIndex];
        // Reset learning
        prefetcher.round = 0;
        prefetcher.offsetIndex = 0;
        for (int i = 0; i < prefetcher.offsetScores.size(); i++) prefetcher.offsetScores[i] = 0;
    }

    if (prefetcher.RRTableSet.find(cacheLine) == prefetcher.RRTableSet.end()) {

        if (prefetcher.RRTableQueue.size() >= RRTableSize) {

            Addr oldestLine = prefetcher.RRTableQueue.front();
            prefetcher.RRTableSet.erase(oldestLine);
            prefetcher.RRTableQueue.pop_front();
        }
        prefetcher.RRTableSet.insert(cacheLine);
        prefetcher.RRTableQueue.push_back(cacheLine);
    }

    // Calculate what line to prefetch based on best offset
    Addr nextPrefetch = (cacheLine + prefetcher.bestOffset) * blkSize;

    // Prefetch only in same page
    if ((nextPrefetch >> 12) == (access_addr >> 12)) {
        addresses.push_back(AddrPriority(nextPrefetch, 0));
    }

    // Can safely be ignored
    // Get matching storage of entries
    // Context is 0 due to single-threaded application
    PCTable& pcTable = findTable(context);

    // Get matching entry for your given PC from the PC Table
    const TDTEntry::KeyType key{access_pc, false};
    TDTEntry *entry = pcTable.findEntry(key);

    // Check if you have entry
    if (entry != nullptr) {
        // There is an entry for this PC
        // You might want to update information for this entry
        entry->lastAddr = access_addr;
    } else {
        // No entry for this PC
        // You might want to make an entry for this PC

        // The following show you how to add an entry to PCTable for a PC
        // All slots are by default taken, you must replace a previous slot with new data
        // Find replacement victim for your new data, update information   
        TDTEntry* victim = pcTable.findVictim(key);
        victim->lastAddr = access_addr;
        pcTable.insertEntry(key, victim);
    }
}

uint32_t
TDTPrefetcherHashedSetAssociative::extractSet(const KeyType &key) const
{
    const Addr pc = key.address;
    const Addr hash1 = pc >> 1;
    const Addr hash2 = hash1 >> tagShift;
    return (hash1 ^ hash2) & setMask;
}

Addr
TDTPrefetcherHashedSetAssociative::extractTag(const Addr addr) const
{
    return addr;
}

} // namespace prefetch
} // namespace gem5
