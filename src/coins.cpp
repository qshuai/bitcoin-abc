// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "consensus/consensus.h"
#include "memusage.h"
#include "random.h"

#include <cassert>

bool CCoinsView::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return false;
}
bool CCoinsView::HaveCoin(const COutPoint &outpoint) const {
    return false;
}
uint256 CCoinsView::GetBestBlock() const {
    return uint256();
}
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) {
    return false;
}
CCoinsViewCursor *CCoinsView::Cursor() const {
    return nullptr;
}

CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) {}
bool CCoinsViewBacked::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return base->GetCoin(outpoint, coin);
}
bool CCoinsViewBacked::HaveCoin(const COutPoint &outpoint) const {
    return base->HaveCoin(outpoint);
}
uint256 CCoinsViewBacked::GetBestBlock() const {
    return base->GetBestBlock();
}
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) {
    base = &viewIn;
}
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) {
    return base->BatchWrite(mapCoins, hashBlock);
}
CCoinsViewCursor *CCoinsViewBacked::Cursor() const {
    return base->Cursor();
}
size_t CCoinsViewBacked::EstimateSize() const {
    return base->EstimateSize();
}

SaltedOutpointHasher::SaltedOutpointHasher()
    : k0(GetRand(std::numeric_limits<uint64_t>::max())),
      k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), cachedCoinsUsage(0) {}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}


// 如果当前缓存和后端缓存都没有此条目，则返回空。如果当前缓存有，这直接返回当前缓存
// 如果当前缓存没有，后端缓存有，则从后端同步到当前缓存，并返回当前添加item的迭代器
CCoinsMap::iterator CCoinsViewCache::FetchCoin(const COutPoint &outpoint) const {
    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end()) {
        return it;
    }

    // 以下从backed中获取coin
    Coin tmp;
    if (!base->GetCoin(outpoint, tmp)) {
        return cacheCoins.end();
    }

    // 如果后代含有这个条目，则会更新父代
    CCoinsMap::iterator ret = cacheCoins.emplace(std::piecewise_construct,
                                                 std::forward_as_tuple(outpoint),
                                                 std::forward_as_tuple(std::move(tmp))).first;
    if (ret->second.coin.IsSpent()) {
        // The parent only has an empty entry for this outpoint; we can consider
        // our version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;            // 已经花完，标记为FRESH, 因为父代不含有此item
    }
    cachedCoinsUsage += ret->second.coin.DynamicMemoryUsage();
    return ret;
}

bool CCoinsViewCache::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) {
        return false;
    }
    coin = it->second.coin;
    return true;
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin coin, bool possible_overwrite) {
    assert(!coin.IsSpent());        // 只添加未花费输出
    if (coin.GetTxOut().scriptPubKey.IsUnspendable()) {     // 无法花费
        return;
    }
    CCoinsMap::iterator it;
    bool inserted;

    // 如果要插入的元素已经存在，则返回存在元素的迭代器
    // 如果不存在则插入一个空的 CCoinsCacheEntry flags为0
    std::tie(it, inserted) = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!inserted) {    // 如果没有插入成功，说明此缓存中已经有这个item了
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    }

    // coins_tests.cpp 如果没有花完一定不会进入，如果已经花完，则有9/10的概率进入
    if (!possible_overwrite) {
        if (!it->second.coin.IsSpent()) {   // 如果已经存在的条目没有被花费，则报错
            throw std::logic_error("Adding new coin that replaces non-pruned entry");
        }
        fresh = !(it->second.flags & CCoinsCacheEntry::DIRTY);  // 其实就是 it->second.flags == CCoinsCacheEntry::FRESH
    }
    it->second.coin = std::move(coin);
    it->second.flags |= CCoinsCacheEntry::DIRTY | (fresh ? CCoinsCacheEntry::FRESH : 0); // flags 为1或者3
    cachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
}

void AddCoins(CCoinsViewCache &cache, const CTransaction &tx, int nHeight) {
    bool fCoinbase = tx.IsCoinBase();
    const uint256 &txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        // Pass fCoinbase as the possible_overwrite flag to AddCoin, in order to
        // correctly deal with the pre-BIP30 occurrances of duplicate coinbase
        // transactions.
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase), fCoinbase);
    }
}

bool CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin *moveout) {
    CCoinsMap::iterator it = FetchCoin(outpoint);       // 查找未花费输出
    if (it == cacheCoins.end()) {
        return false;
    }

    cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();      // 减去花费的输出占用的大小
    if (moveout) {
        *moveout = std::move(it->second.coin);
    }
    if (it->second.flags & CCoinsCacheEntry::FRESH) {       // 如果为fresh就删除
        cacheCoins.erase(it);                               // 说明后端和当前缓存结果一直，下次获取可以通过后端获取
    } else {
        it->second.flags |= CCoinsCacheEntry::DIRTY;        // 如果为dirty就标记为Dirty并清空(null)
        it->second.coin.Clear();                            //!?       相当于把钱花完
    }
    return true;            // 表明已经花完了
}

static const Coin coinEmpty;

const Coin &CCoinsViewCache::AccessCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) {
        return coinEmpty;
    }
    return it->second.coin;
}

bool CCoinsViewCache::HaveCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    return it != cacheCoins.end() && !it->second.coin.IsSpent();        //存在且未花费
}

bool CCoinsViewCache::HaveCoinInCache(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = cacheCoins.find(outpoint);
    return it != cacheCoins.end();              // 当前cache是否存在
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull()) {
        hashBlock = base->GetBestBlock();           // 往下一层搜索
    }
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

// mapCoins为Child
// 当前CCoinsViewCache为parent

// 从child向parent写数据，并依次删除child
bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlockIn) {
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        // Ignore non-dirty entries (optimization).
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {          // DIRTY数据
            CCoinsMap::iterator itUs = cacheCoins.find(it->first); // 当前缓冲查找
            if (itUs == cacheCoins.end()) {     // 当前缓存不存在
                // The parent cache does not have an entry, while the child does
                // We can ignore it if it's both FRESH and pruned in the child
                if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent())) {       // FRESH且已花费
                    // Otherwise we will need to create it in the parent and
                    // move the data up and mark it as dirty
                    CCoinsCacheEntry &entry = cacheCoins[it->first];    // 在当前缓存创建               // todo: 如果key不存在，在产生的CoinsCacheEntry内部零值，都是什么
                    entry.coin = std::move(it->second.coin);
                    cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY;
                    // We can mark it FRESH in the parent if it was FRESH in the
                    // child. Otherwise it might have just been flushed from the
                    // parent's cache and already exist in the grandparent
                    if (it->second.flags & CCoinsCacheEntry::FRESH)
                        entry.flags |= CCoinsCacheEntry::FRESH;
                    //std::cout << entry.flags << std::endl;                  //经测试，这里的flags可能是3
                }
            } else {
                // Assert that the child cache entry was not marked FRESH if the
                // parent cache entry has unspent outputs. If this ever happens,
                // it means the FRESH flag was misapplied and there is a logic
                // error in the calling code.
<<<<<<< HEAD
                if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coin.IsSpent())   // 上层cache有没有花费的输出，下层cache的flags为Fresh则逻辑错误
=======
                if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coin.IsSpent())
>>>>>>> dev
                    throw std::logic_error("FRESH flag misapplied to cache "
                                           "entry for base transaction with "
                                           "spendable outputs");

                // Found the entry in the parent cache
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coin.IsSpent()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                } else {
                    // A normal modification.
                    cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                    itUs->second.coin = std::move(it->second.coin);
                    cachedCoinsUsage += itUs->second.coin.DynamicMemoryUsage();
                    itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                    // NOTE: It is possible the child has a FRESH flag here in
                    // the event the entry we found in the parent is pruned. But
                    // we must not copy that FRESH flag to the parent as that
                    // pruned state likely still needs to be communicated to the
                    // grandparent.
                }
            }
        }
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }
    hashBlock = hashBlockIn;
    return true;
}

bool CCoinsViewCache::Flush() {     //! Flush函数每次调用只会调用一次BatchWrite()函数，但是调用哪一个是不定的，没有弄明白
    bool fOk = base->BatchWrite(cacheCoins, hashBlock);
    cacheCoins.clear();
    cachedCoinsUsage = 0;
    return fOk;
}

//! flages什么时间会等于0
void CCoinsViewCache::Uncache(const COutPoint &outpoint) {
    CCoinsMap::iterator it = cacheCoins.find(outpoint);         // 只在当前缓存中查找
    if (it != cacheCoins.end() && it->second.flags == 0) {      // 存在且flags为0，则删除       默认构造的CCoinsCacheEntry flags为0
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();   // 减去即将删除的元素占用的内存
        cacheCoins.erase(it);       // 从当前缓存中删除
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

const CTxOut &CCoinsViewCache::GetOutputFor(const CTxIn &input) const {
    const Coin &coin = AccessCoin(input.prevout);
    assert(!coin.IsSpent());
    return coin.GetTxOut();
}

Amount CCoinsViewCache::GetValueIn(const CTransaction &tx) const {
    if (tx.IsCoinBase()) {
        return Amount(0);
    }

    Amount nResult(0);
    for (size_t i = 0; i < tx.vin.size(); i++) {
        nResult += GetOutputFor(tx.vin[i]).nValue;
    }

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction &tx) const {
    if (tx.IsCoinBase()) {
        return true;
    }

    for (size_t i = 0; i < tx.vin.size(); i++) {
        if (!HaveCoin(tx.vin[i].prevout)) {
            return false;
        }
    }

    return true;
}

double CCoinsViewCache::GetPriority(const CTransaction &tx, int nHeight,
                                    Amount &inChainInputValue) const {
    inChainInputValue = Amount(0);
    if (tx.IsCoinBase()) {
        return 0.0;
    }
    double dResult = 0.0;
    for (const CTxIn &txin : tx.vin) {
        const Coin &coin = AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            continue;
        }
        if (int64_t(coin.GetHeight()) <= nHeight) {
            dResult += double(coin.GetTxOut().nValue.GetSatoshis()) *
                       (nHeight - coin.GetHeight());
            inChainInputValue += coin.GetTxOut().nValue;
        }
    }
    return tx.ComputePriority(dResult);
}

// TODO: merge with similar definition in undo.h.
static const size_t MAX_OUTPUTS_PER_TX = MAX_TX_SIZE / ::GetSerializeSize(CTxOut(), SER_NETWORK, PROTOCOL_VERSION);

const Coin &AccessByTxid(const CCoinsViewCache &view, const uint256 &txid) {
    COutPoint iter(txid, 0);
    while (iter.n < MAX_OUTPUTS_PER_TX) {
        const Coin &alternate = view.AccessCoin(iter);
        if (!alternate.IsSpent()) {     // 直到找到一个未花费输出
            return alternate;
        }
        ++iter.n;
    }

    return coinEmpty;
}