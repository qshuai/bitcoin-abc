// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "chain.h"
#include "coins.h"
#include "dbwrapper.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockIndex;
class CCoinsViewDBCursor;
class uint256;

//! Compensate for extra memory peak (x1.5-x1.9) at flush time.
static constexpr int DB_PEAK_USAGE_FACTOR = 2;
//! No need to periodic flush if at least this much space still available.
static constexpr int MAX_BLOCK_COINSDB_USAGE = 200 * DB_PEAK_USAGE_FACTOR;
//! Always periodic flush if less than this much space still available.
static constexpr int MIN_BLOCK_COINSDB_USAGE = 50 * DB_PEAK_USAGE_FACTOR;

// 这部分代码对数据库的缓存大小进行了边界处理。关于最大缓存大小和最小缓存大小是一个常量。
// 可以看到默认的缓存大小是450兆，最大的缓存是根据编译器来决定的。如果编译器大于4byte，
// 则最大是16384兆，否则是1024兆。最小缓存是4兆。
//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 450;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void *) > 4 ? 16384 : 1024;
//! min. -dbcache (MiB)
static const int64_t nMinDbCache = 4;
//! Max memory allocated to block tree DB specific cache, if no -txindex (MiB)
static const int64_t nMaxBlockDBCache = 2;
//! Max memory allocated to block tree DB specific cache, if -txindex (MiB)
// Unlike for the UTXO database, for the txindex scenario the leveldb cache make
// a meaningful difference:
// https://github.com/bitcoin/bitcoin/pull/8273#issuecomment-229601991
static const int64_t nMaxBlockDBAndTxIndexCache = 1024;
//! Max memory allocated to coin DB specific cache (MiB)
static const int64_t nMaxCoinsDBCache = 8;

struct CDiskTxPos : public CDiskBlockPos {
    unsigned int nTxOffset; // after header    // tx偏移量

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(*(CDiskBlockPos *)this);          // 先序列化block
        READWRITE(VARINT(nTxOffset));               // 序列化之后结果包含 block文件号、block偏移量、tx偏移量
    }

    CDiskTxPos(const CDiskBlockPos &blockIn, unsigned int nTxOffsetIn)
        : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn) {}

    CDiskTxPos() { SetNull(); }

    void SetNull() {
        CDiskBlockPos::SetNull();       // 文件号为-1 ，偏移量都为0
        nTxOffset = 0;
    }
};

/** CCoinsView backed by the coin database (chainstate/) */
<<<<<<< HEAD
class CCoinsViewDB final : public CCoinsView {
=======
class CCoinsViewDB : public CCoinsView {            // utxo存储后端
>>>>>>> dev
protected:
    CDBWrapper db;

public:
    CCoinsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) override;
    CCoinsViewCursor *Cursor() const override;

    //! Attempt to update from an older database format.
    //! Returns whether an error occurred.
    bool Upgrade();
    size_t EstimateSize() const override;
};

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor : public CCoinsViewCursor {    // CCoinsViewDB迭代器
public:
    ~CCoinsViewDBCursor() {}

    bool GetKey(COutPoint &key) const override;
    bool GetValue(Coin &coin) const override;
    unsigned int GetValueSize() const override;

<<<<<<< HEAD
    bool Valid() const override;
    void Next() override;
=======
    bool Valid() const;
    void Next();            // 游标
>>>>>>> dev

private:
    CCoinsViewDBCursor(CDBIterator *pcursorIn, const uint256 &hashBlockIn) : CCoinsViewCursor(hashBlockIn), pcursor(pcursorIn) {}
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CCoinsViewDB;          // 友元类可以访问本类中成员
};

/** Access to the block database (blocks/index/) */         // 区块索引文件
class CBlockTreeDB : public CDBWrapper {
public:
    CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CBlockTreeDB(const CBlockTreeDB &);
    void operator=(const CBlockTreeDB &);

public:
    bool WriteBatchSync(
        const std::vector<std::pair<int, const CBlockFileInfo *>> &fileInfo,
        int nLastFile, const std::vector<const CBlockIndex *> &blockinfo);

    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos>> &list);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    bool LoadBlockIndexGuts(std::function<CBlockIndex *(const uint256 &)> insertBlockIndex);
};

#endif // BITCOIN_TXDB_H
