// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAIN_H
#define BITCOIN_CHAIN_H

#include "arith_uint256.h"
#include "consensus/params.h"
#include "pow.h"
#include "primitives/block.h"
#include "tinyformat.h"
#include "uint256.h"
#include "serialize.h"

#include <unordered_map>
#include <vector>

class CBlockFileInfo {
public:
    //!< number of blocks stored in file
    unsigned int nBlocks;       // 区块数量
    //!< number of used bytes of block file
    unsigned int nSize;             // block文件大小
    //!< number of used bytes in the undo file
    unsigned int nUndoSize;     // undo rev文件大小

    //!< lowest height of block in file
    unsigned int nHeightFirst;      // 区块最低高度
    //!< highest height of block in file
    unsigned int nHeightLast;       // 区块最大高度
    //!< earliest time of block in file
    uint64_t nTimeFirst;            // 区块最早时间
    //!< latest time of block in file
    uint64_t nTimeLast;             // 区块最新时间

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSize));
        READWRITE(VARINT(nUndoSize));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
    }

    void SetNull() {
        nBlocks = 0;
        nSize = 0;
        nUndoSize = 0;
        nHeightFirst = 0;
        nHeightLast = 0;
        nTimeFirst = 0;
        nTimeLast = 0;
    }

    CBlockFileInfo() { SetNull(); }

    std::string ToString() const;

    /** update statistics (does not update nSize) */
    void AddBlock(unsigned int nHeightIn, uint64_t nTimeIn) {
        if (nBlocks == 0 || nHeightFirst > nHeightIn) {     // 当前块包没有块，或者，接收到一个更早的块
            nHeightFirst = nHeightIn;
        }
        if (nBlocks == 0 || nTimeFirst > nTimeIn) {    // 当前块包没有块，或者，接受到一个更早的块
            nTimeFirst = nTimeIn;
        }
        nBlocks++;                      // 块数量+1
        if (nHeightIn > nHeightLast) {          //调整nHeightLast
            nHeightLast = nHeightIn;
        }
        if (nTimeIn > nTimeLast) {              // 调整nTimeLast
            nTimeLast = nTimeIn;
        }
    }
};

struct CDiskBlockPos {
    int nFile;          // 文件号
    unsigned int nPos;  // 偏移量

    ADD_SERIALIZE_METHODS;      // 添加序列化方法

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(VARINT(nFile));
        READWRITE(VARINT(nPos));
    }

    CDiskBlockPos() { SetNull(); }      // 默认构造方法

    CDiskBlockPos(int nFileIn, unsigned int nPosIn) {
        nFile = nFileIn;
        nPos = nPosIn;
    }

    friend bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b) {        // 重载==
        return (a.nFile == b.nFile && a.nPos == b.nPos);
    }

    friend bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b) {         // 重载!=， 内部为==
        return !(a == b);
    }

    void SetNull() {        // null 条件
        nFile = -1;     // 文件号为-1
        nPos = 0;          // 偏移量0
    }
    bool IsNull() const { return (nFile == -1); }  // 判断null条件

    std::string ToString() const {
        return strprintf("CBlockDiskPos(nFile=%i, nPos=%i)", nFile, nPos);      //ToString()打印一个块的文件号以及在文件中的偏移量
    }
};

// 枚举类型
enum BlockStatus : uint32_t {           // 一个block状态信息
    //! Unused.
    BLOCK_VALID_UNKNOWN = 0,        // 未知

    //! Parsed, version ok, hash satisfies claimed PoW, 1 <= vtx count <= max,
    //! timestamp not in future
    BLOCK_VALID_HEADER = 1,     // Valid header 满足pow(难度)、txcount数量在1到max之间、时间戳不大于当前时间

    //! All parent headers found, difficulty matches, timestamp >= median
    //! previous, checkpoint. Implies all parents are also at least TREE.
    BLOCK_VALID_TREE = 2,       // 不太理解

    /**
     * Only first tx is coinbase, 2 <= coinbase input script length <= 100,
     * transactions valid, no duplicate txids, sigops, size, merkle root.
     * Implies all parents are at least TREE but not necessarily TRANSACTIONS.
     * When all parent blocks also have TRANSACTIONS, CBlockIndex::nChainTx will
     * be set.
     */
    BLOCK_VALID_TRANSACTIONS = 3,       // 注释

    //! Outputs do not overspend inputs, no double spends, coinbase output ok,
    //! no immature coinbase spends, BIP30.
    //! Implies all parents are also at least CHAIN.
    BLOCK_VALID_CHAIN = 4,

    //! Scripts & signatures ok. Implies all parents are also at least SCRIPTS.
    BLOCK_VALID_SCRIPTS = 5,

    //! All validity bits.
    BLOCK_VALID_MASK = BLOCK_VALID_HEADER | BLOCK_VALID_TREE |
                       BLOCK_VALID_TRANSACTIONS | BLOCK_VALID_CHAIN |
                       BLOCK_VALID_SCRIPTS,         // == 7

    //!< full block available in blk*.dat
    BLOCK_HAVE_DATA = 8,             // blk*.dat
    //!< undo data available in rev*.dat
    BLOCK_HAVE_UNDO = 16,              // rev*.dat
    BLOCK_HAVE_MASK = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO,        // blk*.dat和rev*.dat都存在

    //!< stage after last reached validness failed
    BLOCK_FAILED_VALID = 32,
    //!< descends from failed block
    BLOCK_FAILED_CHILD = 64,
    BLOCK_FAILED_MASK = BLOCK_FAILED_VALID | BLOCK_FAILED_CHILD,
};

/**
 * The block chain is a tree shaped structure starting with the genesis block at
 * the root, with each block potentially having multiple candidates to be the
 * next block. A blockindex may have multiple pprev pointing to it, but at most
 * one of them can be part of the currently active branch.
 */
class CBlockIndex {         // 区块索引，有可能有多个区块指向一个区块，但只有一个分支能成功
public:
    //! pointer to the hash of the block, if any. Memory is owned by this
    //! CBlockIndex
    const uint256 *phashBlock;      // hash

    //! pointer to the index of the predecessor of this block
<<<<<<< HEAD
    CBlockIndex *pprev;		// 父级

    //! pointer to the index of some further predecessor of this block
    CBlockIndex *pskip;		// 爷爷级

    //! height of the entry in the chain. The genesis block has height 0
    int nHeight;				// 区块高度

    //! Which # file this block is stored in (blk?????.dat)
    int nFile;				// 区块文件

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos;	// 区块文件位移

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos;	// 恢复文件索引
=======
    CBlockIndex *pprev;             // prev

    //! pointer to the index of some further predecessor of this block
    CBlockIndex *pskip;             // 指向之前的块，但是具体的规则还没清楚       // todo confirm

    //! height of the entry in the chain. The genesis block has height 0
    int nHeight;            // 高度

    //! Which # file this block is stored in (blk?????.dat)
    int nFile;              // block文件号

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos;      // 偏移量

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos;      // rev文件号
>>>>>>> dev

    //! (memory only) Total amount of work (expected number of hashes) in the
    //! chain up to and including this block
    arith_uint256 nChainWork;       // 当前为止所有的block数量，包括当前区块

    //! Number of transactions in this block.
    //! Note: in a potential headers-first mode, this number cannot be relied
    //! upon
<<<<<<< HEAD
    unsigned int nTx;		// 区块中的交易数量
=======
    unsigned int nTx;           // 交易数量
>>>>>>> dev

    //! (memory only) Number of transactions in the chain up to and including
    //! this block.
    //! This value will be non-zero only if and only if transactions for this
    //! block and all its parents are available. Change to 64-bit type when
    //! necessary; won't happen before 2030
    unsigned int nChainTx;          // 当前为止所有的交易数量，包括当前区块内的交易

    //! Verification status of this block. See enum BlockStatus
<<<<<<< HEAD
    uint32_t nStatus;		// 区块验证状态
=======
    uint32_t nStatus;               // 区块状态
>>>>>>> dev

    //! block header                // 区块头信息
    int32_t nVersion;               // 版本号
    uint256 hashMerkleRoot;     // merkle hash
    uint32_t nTime;                 // 时间戳
    uint32_t nBits;                  // 难度
    uint32_t nNonce;                // 随机数

    //! (memory only) Sequential id assigned to distinguish order in which
    //! blocks are received.
    int32_t nSequenceId;            // 序列号  -> 区分block接收顺序          // todo confirm

    //! (memory only) Maximum nTime in the chain upto and including this block.
    unsigned int nTimeMax;          // 区块中交易的最大时间戳

    void SetNull() {
        phashBlock = nullptr;
        pprev = nullptr;
        pskip = nullptr;
        nHeight = 0;
        nFile = 0;
        nDataPos = 0;
        nUndoPos = 0;
        nChainWork = arith_uint256();       // 64个0的数组
        nTx = 0;
        nChainTx = 0;
        nStatus = 0;
        nSequenceId = 0;
        nTimeMax = 0;

        nVersion = 0;
        hashMerkleRoot = uint256();     // 64个0的数组
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    CBlockIndex() { SetNull(); }        // 默认构造函数

    CBlockIndex(const CBlockHeader &block) {        // 复制构造函数
        SetNull();      // 先清空

        nVersion = block.nVersion;
        hashMerkleRoot = block.hashMerkleRoot;
        nTime = block.nTime;
        nBits = block.nBits;
        nNonce = block.nNonce;
    }

    CDiskBlockPos GetBlockPos() const {     // 得到当前区块文件号以及偏移量
        CDiskBlockPos ret;
        if (nStatus & BLOCK_HAVE_DATA) {     // 条件是当前文件系统中含有该区块的block*.dat
            ret.nFile = nFile;
            ret.nPos = nDataPos;
        }
        return ret;
    }

    CDiskBlockPos GetUndoPos() const {      //得到undo文件的文件号和偏移量
        CDiskBlockPos ret;
        if (nStatus & BLOCK_HAVE_UNDO) {        // 条件是存在当前区块的rev*.dat
            ret.nFile = nFile;
            ret.nPos = nUndoPos;
        }
        return ret;
    }

    CBlockHeader GetBlockHeader() const {       // 得到当前区块的区块头信息
        CBlockHeader block;
        block.nVersion = nVersion;
        if (pprev) {
            block.hashPrevBlock = pprev->GetBlockHash();       // 在CBlockIndex中pprev保存的是区块索引指针，而CBlockHeader中字段为hashPrevBlock，为hash256
        }
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime = nTime;
        block.nBits = nBits;
        block.nNonce = nNonce;
        return block;
    }

    uint256 GetBlockHash() const { return *phashBlock; }        // 返回当前区块hash

<<<<<<< HEAD
    int64_t GetBlockTime() const { return int64_t(nTime); }

    int64_t GetBlockTimeMax() const { return int64_t(nTimeMax); }
=======
    int64_t GetBlockTime() const { return (int64_t)nTime; }     // 返回当前区块时间戳

    int64_t GetBlockTimeMax() const { return (int64_t)nTimeMax; }       // 返回当前区块中最新交易的时间戳  // todo confirm
>>>>>>> dev

    enum { nMedianTimeSpan = 11 };

    // 使用中位数时间的原因：是旷工可以任意修改block中的时间戳，因此，如果以一个块的时间戳为时间依据，不可靠
    // 为什么取11个块，并取中间的数，而不是字节取倒数第6个块？可以注意到，在取完11块之后会进行排序，这是因为
    // block的顺序和时间戳的顺序可能不一致，排序之后会确保不会以一些偏离很大的数为依据
    // 找出当前区块前的10个区块，排序后，返回第5个元素的nTime
    int64_t GetMedianTimePast() const {         // 返回当前区块的中位数时间
        // 11个元素的数组，包括该区块和之前的10个区块
        int64_t pmedian[nMedianTimeSpan];

        // pbegin、pend两个游标(数组游标)指向数组末端
        int64_t *pbegin = &pmedian[nMedianTimeSpan];
        int64_t *pend = &pmedian[nMedianTimeSpan];

        // 区块游标
        const CBlockIndex *pindex = this;
        // 遍历11个区块，pindex游标不断地向前移动
        for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev) {
            // 数组游标向前移动，并将pindex获取的时间戳赋值给数组
            *(--pbegin) = pindex->GetBlockTime();
        }

        // 对数组排序
        std::sort(pbegin, pend);

        // 11个区块去中间的元素，也就是数组下标为5的元素，
        // 因为是奇数个元素，所以不用进行判断下标无效的问题
        return pbegin[(pend - pbegin) / 2];
    }

    std::string ToString() const {
        return strprintf(
            "CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)", pprev,
            nHeight, hashMerkleRoot.ToString(), GetBlockHash().ToString());             // ToString()打印区块索引信息
    }

    //! Check whether this block index entry is valid up to the passed validity
    //! level.
    bool IsValid(enum BlockStatus nUpTo = BLOCK_VALID_TRANSACTIONS) const {     // 含有默认参数
        // Only validity flags allowed.
        assert(!(nUpTo & ~BLOCK_VALID_MASK));           // 参数nUpTo必须在BLOCK_VALID_MASK级别以下
        if (nStatus & BLOCK_FAILED_MASK) {
            return false;                                       // >= nUpTo这个验证级别，是否满足
        }
        return ((nStatus & BLOCK_VALID_MASK) >= nUpTo);        // 这个函数的目的就是判断当前区块有效的范围 >=
    }

    //! Raise the validity level of this block index entry.
    //! Returns true if the validity was changed.
    bool RaiseValidity(enum BlockStatus nUpTo) {    // 提升一个区块的有效性级别，如果级别被改变就返回true
        // Only validity flags allowed.
        assert(!(nUpTo & ~BLOCK_VALID_MASK));
        if (nStatus & BLOCK_FAILED_MASK) {          // 如果Failed为被设置了，那么就直接返回false
            return false;
        }
        if ((nStatus & BLOCK_VALID_MASK) < nUpTo) {             // 当前nStatus在uUpTo级别以下
            nStatus = (nStatus & ~BLOCK_VALID_MASK) | nUpTo;    // 结果是29个0 + nUpTo位
            return true;
        }
        return false;
    }

    //! Build the skiplist pointer for this entry.
    void BuildSkip();

    //! Efficiently find an ancestor of this block.
    CBlockIndex *GetAncestor(int height);           // 找到指定高度的区块指针
    const CBlockIndex *GetAncestor(int height) const;
};

/**
 * Maintain a map of CBlockIndex for all known headers.
 */
struct BlockHasher {
    size_t operator()(const uint256 &hash) const { return hash.GetCheapHash(); }
};

typedef std::unordered_map<uint256, CBlockIndex *, BlockHasher> BlockMap;
extern BlockMap mapBlockIndex;

arith_uint256 GetBlockProof(const CBlockIndex &block);

/**
 * Return the time it would take to redo the work difference between from and
 * to, assuming the current hashrate corresponds to the difficulty at tip, in
 * seconds.
 */

// 返回从一个高度到另一个高度，重新完成工作量证明的时间
int64_t GetBlockProofEquivalentTime(const CBlockIndex &to, const CBlockIndex &from, const CBlockIndex &tip, const Consensus::Params &);

/** Used to marshal pointers into hashes for db storage. */         // 将指针处理成hash便于db存储
class CDiskBlockIndex : public CBlockIndex {            // 继承CBlockIndex
public:
    uint256 hashPrev;               // 储存一个区块的父区块hash

    CDiskBlockIndex() { hashPrev = uint256(); }     // 默认构造函数

    explicit CDiskBlockIndex(const CBlockIndex *pindex) : CBlockIndex(*pindex) {
        hashPrev = (pprev ? pprev->GetBlockHash() : uint256());     // prev hash
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        int nVersion = s.GetVersion();          // 数据流版本号 todo confirm
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(VARINT(nVersion));
        }

        READWRITE(VARINT(nHeight));
        READWRITE(VARINT(nStatus));
        READWRITE(VARINT(nTx));
        if (nStatus & (BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO)) {        // 或 判断。如果有block*.dat或者rev*.dat时都会写入文件号
            READWRITE(VARINT(nFile));
        }
        if (nStatus & BLOCK_HAVE_DATA) {        // 如果有block*.dat时才写入block偏移量
            READWRITE(VARINT(nDataPos));
        }
        if (nStatus & BLOCK_HAVE_UNDO) {        // 如果有rev*.dat时才会写入rev偏移量
            READWRITE(VARINT(nUndoPos));
        }

        // block header
        READWRITE(this->nVersion);          // this关键字指明是当前类中的成员变量nVersion而不是上面定义的nVersion变量
        READWRITE(hashPrev);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    uint256 GetBlockHash() const {
        CBlockHeader block;
        block.nVersion = nVersion;
        block.hashPrevBlock = hashPrev;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime = nTime;
        block.nBits = nBits;
        block.nNonce = nNonce;
        return block.GetHash();         // block的hash其实就是通过以上6个字段的摘要，存在于CBlockHeader中
    }

    std::string ToString() const {      // ToString是返回string对象，并不是在内部输出
        std::string str = "CDiskBlockIndex(";
        str += CBlockIndex::ToString();
        str += strprintf("\n                hashBlock=%s, hashPrev=%s)",
                         GetBlockHash().ToString(), hashPrev.ToString());       // hash的ToString()方法是使用GetHex()
        return str;
    }
};

<<<<<<< HEAD
/**
 * An in-memory indexed chain of blocks.
 */
class CChain {
=======
/** An in-memory indexed chain of blocks. */
class CChain {          // 存在内存中
>>>>>>> dev
private:
    std::vector<CBlockIndex *> vChain;          // 区块索引 链表

public:
    /**
     * Returns the index entry for the genesis block of this chain, or nullptr
     * if none.
     */
    CBlockIndex *Genesis() const {          // 返回创始区块索引
        return vChain.size() > 0 ? vChain[0] : nullptr;
    }

    /**
     * Returns the index entry for the tip of this chain, or nullptr if none.
     */
    CBlockIndex *Tip() const {              // 返回最新的block索引， 应该tip改成top？
        return vChain.size() > 0 ? vChain[vChain.size() - 1] : nullptr;
    }

    /**
     * Returns the index entry at a particular height in this chain, or nullptr
     * if no such height exists.
     */
    CBlockIndex *operator[](int nHeight) const {            // 返回指定高度的区块索引 重载运算符[]
        if (nHeight < 0 || nHeight >= (int)vChain.size()) {
            return nullptr;
        }
        return vChain[nHeight];
    }

    /** Compare two chains efficiently. */                  // 比较两条链的效率，结果返货两个链是否相等
    friend bool operator==(const CChain &a, const CChain &b) {
        return a.vChain.size() == b.vChain.size() &&
               a.vChain[a.vChain.size() - 1] == b.vChain[b.vChain.size() - 1];
    }

    /** Efficiently check whether a block is present in this chain. */
    bool Contains(const CBlockIndex *pindex) const {
        return (*this)[pindex->nHeight] == pindex;          // 当前区块是否含有某一个block，因为区块高度是从0开始的，所以可通过高度直接从vector中取数据
    }

    /**
     * Find the successor of a block in this chain, or nullptr if the given
     * index is not found or is the tip.
     */
    CBlockIndex *Next(const CBlockIndex *pindex) const {        // 返回下一个区块的指针
        if (!Contains(pindex)) {
            return nullptr;
        }

        return (*this)[pindex->nHeight + 1];
    }

    /**
     * Return the maximal height in the chain. Is equal to chain.Tip() ?
     * chain.Tip()->nHeight : -1.
     */
    int Height() const { return vChain.size() - 1; }        // -1

    /** Set/initialize a chain with a given tip. */
    void SetTip(CBlockIndex *pindex);

    /**
     * Return a CBlockLocator that refers to a block in this chain (by default
     * the tip).
     */
    CBlockLocator GetLocator(const CBlockIndex *pindex = nullptr) const;

    /**
     * Find the last common block between this chain and a block index entry.
     */
    const CBlockIndex *FindFork(const CBlockIndex *pindex) const;

    /**
     * Find the earliest block with timestamp equal or greater than the given.
     */
    CBlockIndex *FindEarliestAtLeast(int64_t nTime) const;
};

#endif // BITCOIN_CHAIN_H
