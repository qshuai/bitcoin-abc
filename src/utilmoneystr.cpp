// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilmoneystr.h"

#include "primitives/transaction.h"
#include "tinyformat.h"
#include "utilstrencodings.h"

std::string FormatMoney(const Amount &amt) {
    // Note: not using straight sprintf here because we do NOT want localized
    // number formatting.
    int64_t n = amt.GetSatoshis();
    int64_t n_abs = (n > 0 ? n : -n);
    int64_t quotient = n_abs / COIN.GetSatoshis();
    int64_t remainder = n_abs % COIN.GetSatoshis();
    std::string str = strprintf("%d.%08d", quotient, remainder);

    // Right-trim excess zeros before the decimal point:
    int nTrim = 0;
    for (int i = str.size() - 1; (str[i] == '0' && isdigit(str[i - 2])); --i)
        ++nTrim;
    if (nTrim) str.erase(str.size() - nTrim, nTrim);

    if (n < 0) str.insert((unsigned int)0, 1, '-');
    return str;
}

bool ParseMoney(const std::string &str, Amount &nRet) {
    return ParseMoney(str.c_str(), nRet);           // 转化成c语言风格字符串，函数重载
}

bool ParseMoney(const char *pszIn, Amount &nRet) {
    std::string strWhole;
    int64_t nUnits = 0;
    const char *p = pszIn;
    while (isspace(*p))     // 如果是空格就继续移动指针，目的就是去除数据中的空格
        p++;

    for (; *p; p++) {
        if (*p == '.') {            // 遇到小数点
            p++;            // 指向下一个字符
            int64_t nMult = 10 * CENT.GetSatoshis();
            while (isdigit(*p) && (nMult > 0)) {
                nUnits += nMult * (*p++ - '0');
                nMult /= 10;
            }
            break;
        }
        if (isspace(*p)) break;     // 如果数字中含有空格则终止for循环
        if (!isdigit(*p)) return false;         // 不是数字就返回false，解析失败
        strWhole.insert(strWhole.end(), *p);        // 将数字插入string对象中
    }

    for (; *p; p++)
        if (!isspace(*p)) return false;
    // guard against 63 bit overflow
    if (strWhole.size() > 10) return false;
    if (nUnits < 0 || nUnits > COIN.GetSatoshis()) return false;
    int64_t nWhole = atoi64(strWhole);
    Amount nValue = nWhole * COIN + Amount(nUnits);

    nRet = nValue;
    return true;
}
