// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex) {
    if (pindex == nullptr) {        // tip为null说明chain没有block数据
        vChain.clear();
        return;
    }

    vChain.resize(pindex->nHeight + 1);     // 高度从0开始，vector的容量比高度多1,但是高度和vector的下标一样
    while (pindex && vChain[pindex->nHeight] != pindex) {   //
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;         // 这个语句 是判断语句vChain[pindex->nHeight] != pindex成立
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {     // 不太理解
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex) {
        pindex = Tip();     // 如果pindex为null，则默认为tip元素
    }
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0) {         // genesis block 会终止循环，但是genesis block已经加入到vHave中了
            break;
        }
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10) {
            nStep *= 2;
        }
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == nullptr) {
        return nullptr;
    }
    if (pindex->nHeight > Height()) {
        pindex = pindex->GetAncestor(Height());
    }
    while (pindex && !Contains(pindex)) {
        pindex = pindex->pprev;
    }
    return pindex;
}

CBlockIndex *CChain::FindEarliestAtLeast(int64_t nTime) const {
    std::vector<CBlockIndex *>::const_iterator lower =
        std::lower_bound(vChain.begin(), vChain.end(), nTime,
                         [](CBlockIndex *pBlock, const int64_t &time) -> bool {
                             return pBlock->GetBlockTimeMax() < time;
                         });
    return (lower == vChain.end() ? nullptr : *lower);
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'.
 */
static inline int InvertLowestOne(int n) {
    return n & (n - 1);
}

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
static inline int GetSkipHeight(int height) {
    if (height < 2) {
        return 0;
    }

    // Determine which height to jump back to. Any number strictly lower than
    // height is acceptable, but the following expression seems to perform well
    // in simulations (max 110 steps to go back up to 2**18 blocks).这里的2**18表示覆盖的块，而不是块的高度
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

// 获取指定高度的区块索引
CBlockIndex *CBlockIndex::GetAncestor(int height) {
    // 给定的区块高度(故应该低于当前区块高度)
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    // 游标
    CBlockIndex *pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != nullptr &&
            (heightSkip == height || (heightSkip > height && !(heightSkipPrev < heightSkip - 2 && heightSkipPrev >= height)))) {

            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

const CBlockIndex *CBlockIndex::GetAncestor(int height) const {
    return const_cast<CBlockIndex *>(this)->GetAncestor(height);
}

void CBlockIndex::BuildSkip() {
    if (pprev) {
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
    }
}

arith_uint256 GetBlockProof(const CBlockIndex &block) {
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0) {
        return 0;
    }
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as
    // large as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) /
    // (bnTarget+1)) + 1, or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

// 逻辑不太清楚 todo confirm
int64_t GetBlockProofEquivalentTime(const CBlockIndex &to, const CBlockIndex &from, const CBlockIndex &tip, const Consensus::Params &params) {
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);           // tip就是为了得到当前的算力
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}
