// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "core_io.h"

#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "serialize.h"
#include "streams.h"
#include "util.h"
#include "utilstrencodings.h"
#include "version.h"

#include <univalue.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>

#include <iostream>
#include "utilstrencodings.h"


CScript ParseScript(const std::string &s) {
    CScript result;

    static std::map<std::string, opcodetype> mapOpNames;			// static类型，全局共享

    //初始化mapOpNames，运行时只会执行一次
    if (mapOpNames.empty()) {		//只有第一次执行的是否会进入这个逻辑

    std::cout << "可用的mapOpNames->core_read.cpp" << std::endl;

        for (int op = 0; op <= OP_NOP10; op++) {
            // Allow OP_RESERVED to get into mapOpNames
            if (op < OP_NOP && op != OP_RESERVED) {
                continue;
            }

            const char *name = GetOpName((opcodetype)op);
            std::cout << name << std::endl;					//输出符合条件的opNames
            if (strcmp(name, "OP_UNKNOWN") == 0) {			//OpName不能为规定类型的其他情况
                continue;
            }

            std::string strName(name);
            mapOpNames[strName] = (opcodetype)op;					//没有OP_前缀的版本
            // Convenience: OP_ADD and just ADD are both recognized:
            boost::algorithm::replace_first(strName, "OP_", "");		//含有OP_前缀的版本
            mapOpNames[strName] = (opcodetype)op;					//含有前缀和没有前缀的版本的两个元素，毗邻
        }
    }

	std::cout << "分割线----------------------->core_read.cpp" << std::endl;
    std::vector<std::string> words;
    // 将字符串使用空格分割，并将每个元素存放在vercor中
    // 结果存放在/src/test/data/split_words.md
    boost::algorithm::split(words, s, boost::algorithm::is_any_of(" \t\n"),
                            boost::algorithm::token_compress_on);

    //用于显示boost::algorithm::split()操作结局
    for (auto word : words) {
    		std::cout << "--word:" << word << std::endl;
    }
	std::cout << "分割线输出word完成->core_read.cpp" << std::endl;


    for (std::vector<std::string>::const_iterator w = words.begin();
         w != words.end(); ++w) {
        if (w->empty()) {
            // Empty string, ignore. (boost::split given '' will return one
            // word)
        } else if (all(*w, boost::algorithm::is_digit()) ||
                   (boost::algorithm::starts_with(*w, "-") &&
                    all(std::string(w->begin() + 1, w->end()),
                        boost::algorithm::is_digit()))) {
            // Number
            int64_t n = atoi64(*w);
            result << n;														//push
        } else if (boost::algorithm::starts_with(*w, "0x") &&
                   (w->begin() + 2 != w->end()) &&
                   IsHex(std::string(w->begin() + 2, w->end()))) {
            // Raw hex data, inserted NOT pushed onto stack:
            std::vector<uint8_t> raw =
                ParseHex(std::string(w->begin() + 2, w->end()));
            result.insert(result.end(), raw.begin(), raw.end());				//insert
        } else if (w->size() >= 2 && boost::algorithm::starts_with(*w, "'") &&
                   boost::algorithm::ends_with(*w, "'")) {
            // Single-quoted string, pushed as data. NOTE: this is poor-man's
            // parsing, spaces/tabs/newlines in single-quoted strings won't
            // work.
            std::vector<uint8_t> value(w->begin() + 1, w->end() - 1);
            result << value;													//push
        } else if (mapOpNames.count(*w)) {
            // opcode, e.g. OP_ADD or ADD:
            result << mapOpNames[*w];										//写入以w为key的元素数量
        } else {
            throw std::runtime_error("script parse error");
        }
    }

//    for(auto r : result){
    		std::cout << HexStr(result) << std::endl;
//    }
    std::cout << "分界线-------------------------------->core_read.cpp" << std::endl;
    return result;
}

bool DecodeHexTx(CMutableTransaction &tx, const std::string &strHexTx) {
    if (!IsHex(strHexTx)) {
        return false;
    }

    std::vector<uint8_t> txData(ParseHex(strHexTx));

    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> tx;
        if (!ssData.empty()) {
            return false;
        }
    } catch (const std::exception &) {
        return false;
    }

    return true;
}

bool DecodeHexBlk(CBlock &block, const std::string &strHexBlk) {
    if (!IsHex(strHexBlk)) {
        return false;
    }

    std::vector<uint8_t> blockData(ParseHex(strHexBlk));
    CDataStream ssBlock(blockData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssBlock >> block;
    } catch (const std::exception &) {
        return false;
    }

    return true;
}

uint256 ParseHashUV(const UniValue &v, const std::string &strName) {
    std::string strHex;
    if (v.isStr()) {
        strHex = v.getValStr();
    }

    // Note: ParseHashStr("") throws a runtime_error
    return ParseHashStr(strHex, strName);
}

uint256 ParseHashStr(const std::string &strHex, const std::string &strName) {
    if (!IsHex(strHex)) {
        // Note: IsHex("") is false
        throw std::runtime_error(
            strName + " must be hexadecimal string (not '" + strHex + "')");
    }

    uint256 result;
    result.SetHex(strHex);
    return result;
}

std::vector<uint8_t> ParseHexUV(const UniValue &v, const std::string &strName) {
    std::string strHex;
    if (v.isStr()) {
        strHex = v.getValStr();
    }

    if (!IsHex(strHex)) {
        throw std::runtime_error(
            strName + " must be hexadecimal string (not '" + strHex + "')");
    }

    return ParseHex(strHex);
}
