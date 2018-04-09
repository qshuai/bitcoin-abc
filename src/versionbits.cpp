// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "versionbits.h"

#include "consensus/params.h"

const struct BIP9DeploymentInfo
    VersionBitsDeploymentInfo[Consensus::MAX_VERSION_BITS_DEPLOYMENTS] = {
        {
            /*.name =*/"testdummy",
            /*.gbt_force =*/true,
        },
        {
            /*.name =*/"csv",
            /*.gbt_force =*/true,
        },
};

/**
 * @param pindexPrev
 * @param params 只是为了表明你用的是哪个链的共识规则
 * @param cache
 * @return
 */
ThresholdState AbstractThresholdConditionChecker::GetStateFor(const CBlockIndex *pindexPrev, const Consensus::Params &params, ThresholdConditionCache &cache) const {

    int nPeriod = Period(params);       // 共识规则:目标周期
    int nThreshold = Threshold(params);     // 共识规则:阈值
    int64_t nTimeStart = BeginTime(params);     // 开始时间
    int64_t nTimeTimeout = EndTime(params);     // 过期时间

    // A block's state is always the same as that of the first of its period, so
    // it is computed based on a pindexPrev whose height equals a multiple of
    // nPeriod - 1.
    // 一个区块的状态同这个目标周期的第一个区块的状态是一致的，所以通过period-1倍数的区块去寻找上一目标周期的状态
    if (pindexPrev != nullptr) {
        // 第一次调用GetAncestor()传入2016整数倍的区块高度
        pindexPrev = pindexPrev->GetAncestor(
            // pindexPrev指向当前块的上一个区块, pindexPrev->nHeight + 1也就是当前区块高度
            // 由于求余运算结果为依次上升的不连续直线，故下面这个表达式的结果正好指向当前区块所在目标周期的上一个目标周期的最后一个元素
            pindexPrev->nHeight - ((pindexPrev->nHeight + 1) % nPeriod));
    }

    // Walk backwards in steps of nPeriod to find a pindexPrev whose information is known
    std::vector<const CBlockIndex *> vToCompute;
    while (cache.count(pindexPrev) == 0) {      // 当前缓存中没有这个区块的状态ThresholdState信息
        if (pindexPrev == nullptr) {
            // The genesis block is by definition defined.
            cache[pindexPrev] = THRESHOLD_DEFINED;
            break;
        }
        if (pindexPrev->GetMedianTimePast() < nTimeStart) {
            // Optimization: don't recompute down further, as we know every
            // earlier block will be before the start time
            cache[pindexPrev] = THRESHOLD_DEFINED;
            break;
        }

        // 以下的逻辑条件是：pindexPrev->GetMedianTimePast() < nTimeStart
        vToCompute.push_back(pindexPrev);
        // 第二次调用GetAncestor()函数传入一个当前高度减去2016
        // 减去之后有可能小于0，子函数有判断
        pindexPrev = pindexPrev->GetAncestor(pindexPrev->nHeight - nPeriod);        // 减去2016，继续向前寻找符合条件的上个区块
    }

    // At this point, cache[pindexPrev] is known
    assert(cache.count(pindexPrev));
    ThresholdState state = cache[pindexPrev];

    // Now walk forward and compute the state of descendants of pindexPrev
    // 遍历已经缓存的东西，找到所需要的信息
    while (!vToCompute.empty()) {
        ThresholdState stateNext = state;           // 中间变量，保存状态
        pindexPrev = vToCompute.back();         // 最后一个元素，也就是最靠近前面的区块
        vToCompute.pop_back();              // 删除它

        switch (state) {
            case THRESHOLD_DEFINED: {
                // 取前10个区块的中位数时间，
                // 如果已经超过timeout则表示软分叉失败
                if (pindexPrev->GetMedianTimePast() >= nTimeTimeout) {
                    stateNext = THRESHOLD_FAILED;
                    // 如果已经处在timeStart之后，说明软分叉已经开始
                } else if (pindexPrev->GetMedianTimePast() >= nTimeStart) {
                    stateNext = THRESHOLD_STARTED;
                }
                break;
            }
            case THRESHOLD_STARTED: {
                if (pindexPrev->GetMedianTimePast() >= nTimeTimeout) {
                    stateNext = THRESHOLD_FAILED;
                    break;
                }
                // We need to count
                const CBlockIndex *pindexCount = pindexPrev;
                int count = 0;
                for (int i = 0; i < nPeriod; i++) {
                    if (Condition(pindexCount, params)) {       // 一个目标周期内，符合条件的块数量
                        count++;
                    }
                    pindexCount = pindexCount->pprev;
                }
                if (count >= nThreshold) {      // startTime开始后，在一个目标周期内投票超过了设定阈值，则设置为LOCKED_IN状态
                    stateNext = THRESHOLD_LOCKED_IN;
                }
                break;
            }
            case THRESHOLD_LOCKED_IN: {
                // Always progresses into ACTIVE.
                stateNext = THRESHOLD_ACTIVE;
                break;
            }
            case THRESHOLD_FAILED:
            case THRESHOLD_ACTIVE: {
                // Nothing happens, these are terminal states.
                break;
            }
        }
        cache[pindexPrev] = state = stateNext;
    }

    return state;
}

// 求从哪个区块高度开始，该然分叉的状态已经和当前状态开始了
int AbstractThresholdConditionChecker::GetStateSinceHeightFor(const CBlockIndex *pindexPrev, const Consensus::Params &params, ThresholdConditionCache &cache) const {

    const ThresholdState initialState = GetStateFor(pindexPrev, params, cache);

    // BIP 9 about state DEFINED: "The genesis block is by definition in this
    // state for each deployment."
    if (initialState == THRESHOLD_DEFINED) {
        return 0;
    }

    const int nPeriod = Period(params);

    // A block's state is always the same as that of the first of its period, so
    // it is computed based on a pindexPrev whose height equals a multiple of
    // nPeriod - 1. To ease understanding of the following height calculation,
    // it helps to remember that right now pindexPrev points to the block prior
    // to the block that we are computing for, thus: if we are computing for the
    // last block of a period, then pindexPrev points to the second to last
    // block of the period, and if we are computing for the first block of a
    // period, then pindexPrev points to the last block of the previous period.
    // The parent of the genesis block is represented by nullptr.
    pindexPrev = pindexPrev->GetAncestor(pindexPrev->nHeight - ((pindexPrev->nHeight + 1) % nPeriod));

    const CBlockIndex *previousPeriodParent = pindexPrev->GetAncestor(pindexPrev->nHeight - nPeriod);

    while (previousPeriodParent != nullptr && GetStateFor(previousPeriodParent, params, cache) == initialState) {
        pindexPrev = previousPeriodParent;
        previousPeriodParent = pindexPrev->GetAncestor(pindexPrev->nHeight - nPeriod);
    }

    // Adjust the result because right now we point to the parent block.
    return pindexPrev->nHeight + 1;
}

namespace {
/**
 * Class to implement versionbits logic.
 */
class VersionBitsConditionChecker : public AbstractThresholdConditionChecker {
private:
    const Consensus::DeploymentPos id;

protected:
    int64_t BeginTime(const Consensus::Params &params) const override {
        return params.vDeployments[id].nStartTime;
    }
    int64_t EndTime(const Consensus::Params &params) const override {
        return params.vDeployments[id].nTimeout;
    }
    int Period(const Consensus::Params &params) const override {
        return params.nMinerConfirmationWindow;
    }
    int Threshold(const Consensus::Params &params) const override {
        return params.nRuleChangeActivationThreshold;
    }

<<<<<<< HEAD
    bool Condition(const CBlockIndex *pindex,
                   const Consensus::Params &params) const override {
        return (((pindex->nVersion & VERSIONBITS_TOP_MASK) ==
                 VERSIONBITS_TOP_BITS) &&
=======
    bool Condition(const CBlockIndex *pindex, const Consensus::Params &params) const {
        return ((
                // 说明该version是有效的version设置
                (pindex->nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS) &&

                // 表示块的版本号中的确设置了bit位
>>>>>>> dev
                (pindex->nVersion & Mask(params)) != 0);
    }

public:
    VersionBitsConditionChecker(Consensus::DeploymentPos id_) : id(id_) {}      // 构造方法

    uint32_t Mask(const Consensus::Params &params) const {

        return ((uint32_t)1) << params.vDeployments[id].bit;
    }
};
} // namespace

ThresholdState VersionBitsState(const CBlockIndex *pindexPrev, const Consensus::Params &params, Consensus::DeploymentPos pos, VersionBitsCache &cache) {
    return VersionBitsConditionChecker(pos).GetStateFor(pindexPrev, params, cache.caches[pos]);
}

int VersionBitsStateSinceHeight(const CBlockIndex *pindexPrev, const Consensus::Params &params, Consensus::DeploymentPos pos, VersionBitsCache &cache) {
    return VersionBitsConditionChecker(pos).GetStateSinceHeightFor(pindexPrev, params, cache.caches[pos]);
}

uint32_t VersionBitsMask(const Consensus::Params &params, Consensus::DeploymentPos pos) {
    return VersionBitsConditionChecker(pos).Mask(params);
}

void VersionBitsCache::Clear() {
    for (unsigned int d = 0; d < Consensus::MAX_VERSION_BITS_DEPLOYMENTS; d++) {
        caches[d].clear();
    }
}
