/**
 * @file SVAdapter.cpp
 * @brief SV 发布器适配器实现
 */

#ifdef HAS_LIBIEC61850

#include "SVAdapter.h"
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <chrono>

namespace IPC {

SVAdapter::SVAdapter(const SVCommConfig& config)
    : config_(config)
    , publisher_(nullptr)
    , enabled_(true)
    , setupComplete_(false)
{
}

SVAdapter::~SVAdapter() {
    destroy();
}

SVAdapter* SVAdapter::create(const SVCommConfig& config) {
    SVAdapter* adapter = new SVAdapter(config);
    if (adapter->initialize()) {
        return adapter;
    }
    delete adapter;
    return nullptr;
}

void SVAdapter::destroy() {
    if (publisher_) {
        SVPublisher_destroy(publisher_);
        publisher_ = nullptr;
    }
    cleanupASDUs();
}

bool SVAdapter::initialize() {
    // 创建通信参数
    CommParameters params;
    params.vlanPriority = config_.vlanPriority;
    params.vlanId = config_.vlanId;
    params.appId = config_.appId;
    memcpy(params.dstAddress, config_.dstAddress, 6);

    // 创建 SV 发布器
    if (config_.useVlanTag) {
        publisher_ = SVPublisher_createEx(&params, config_.interfaceName.c_str(), true);
    } else {
        publisher_ = SVPublisher_create(&params, config_.interfaceName.c_str());
    }

    if (!publisher_) {
        std::cerr << "Failed to create SVPublisher" << std::endl;
        return false;
    }

    std::cout << "SVAdapter initialized on " << config_.interfaceName
              << " with svID=" << config_.svID
              << ", datset=" << config_.datset
              << ", smpRate=" << config_.smpRate << std::endl;

    return true;
}

SVPublisher_ASDU SVAdapter::addASDU(const std::string& svID,
                                    const std::string& datset,
                                    uint32_t confRev) {
    if (!publisher_) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);

    char* svIDStr = strdup(svID.c_str());
    char* datsetStr = strdup(datset.c_str());

    SVPublisher_ASDU asdu = SVPublisher_addASDU(publisher_, svIDStr, datsetStr, confRev);

    free(svIDStr);
    free(datsetStr);

    if (!asdu) {
        std::cerr << "Failed to create ASDU for " << svID << std::endl;
        return nullptr;
    }

    int asduIndex = static_cast<int>(asdus_.size());
    asdus_.push_back(asdu);
    members_.emplace_back();  // 添加空的成员列表

    std::cout << "Added ASDU: " << svID
              << " at index " << asduIndex << std::endl;

    return asdu;
}

bool SVAdapter::addASDUMember(int asduIndex, const SVASDUMember& member) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        std::cerr << "Invalid ASDU index: " << asduIndex << std::endl;
        return false;
    }

    if (asduIndex >= static_cast<int>(members_.size())) {
        std::cerr << "No ASDU at index " << asduIndex << std::endl;
        return false;
    }

    SVPublisher_ASDU asdu = asdus_[asduIndex];
    if (!asdu) {
        return false;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);

    // 根据类型在 ASDU 中分配空间
    int offset = -1;
    switch (member.type) {
        case SVDataType::INT8:
            offset = SVPublisher_ASDU_addINT8(asdu);
            break;
        case SVDataType::INT32:
            offset = SVPublisher_ASDU_addINT32(asdu);
            break;
        case SVDataType::INT64:
            offset = SVPublisher_ASDU_addINT64(asdu);
            break;
        case SVDataType::FLOAT:
            offset = SVPublisher_ASDU_addFLOAT(asdu);
            break;
        case SVDataType::FLOAT64:
            offset = SVPublisher_ASDU_addFLOAT64(asdu);
            break;
        case SVDataType::TIMESTAMP:
            offset = SVPublisher_ASDU_addTimestamp(asdu);
            break;
        case SVDataType::QUALITY:
            offset = SVPublisher_ASDU_addQuality(asdu);
            break;
        default:
            std::cerr << "Unknown SVDataType: " << static_cast<int>(member.type) << std::endl;
            return false;
    }

    if (offset < 0) {
        return false;
    }

    SVASDUMember m = member;
    m.index = member.index;
    m.bufferOffset = offset;
    members_[asduIndex].push_back(m);

    std::cout << "Added ASDU member: " << member.name
              << " at offset " << offset << std::endl;

    return true;
}

int SVAdapter::addASDUMembers(int asduIndex, const std::vector<SVASDUMember>& members) {
    int count = 0;
    for (const auto& member : members) {
        if (addASDUMember(asduIndex, member)) {
            count++;
        }
    }
    return count;
}

void SVAdapter::setINT8Value(int asduIndex, int index, int8_t value) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setINT8(asdus_[asduIndex], index, value);
}

void SVAdapter::setINT32Value(int asduIndex, int index, int32_t value) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setINT32(asdus_[asduIndex], index, value);
}

void SVAdapter::setINT64Value(int asduIndex, int index, int64_t value) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setINT64(asdus_[asduIndex], index, value);
}

void SVAdapter::setFLOATValue(int asduIndex, int index, float value) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setFLOAT(asdus_[asduIndex], index, value);
}

void SVAdapter::setFLOAT64Value(int asduIndex, int index, double value) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setFLOAT64(asdus_[asduIndex], index, value);
}

void SVAdapter::setTimestampValue(int asduIndex, int index, Timestamp value) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setTimestamp(asdus_[asduIndex], index, value);
}

void SVAdapter::setQualityValue(int asduIndex, int index, Quality value) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setQuality(asdus_[asduIndex], index, value);
}

void SVAdapter::setSmpCnt(int asduIndex, uint16_t smpCnt) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setSmpCnt(asdus_[asduIndex], smpCnt);
}

void SVAdapter::increaseSmpCnt(int asduIndex) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_increaseSmpCnt(asdus_[asduIndex]);
}

void SVAdapter::setRefrTm(int asduIndex, uint64_t refrTm) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_enableRefrTm(asdus_[asduIndex]);
    SVPublisher_ASDU_setRefrTm(asdus_[asduIndex], refrTm);
}

void SVAdapter::setRefrTmNs(int asduIndex, nsSinceEpoch refrTmNs) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_enableRefrTm(asdus_[asduIndex]);
    SVPublisher_ASDU_setRefrTmNs(asdus_[asduIndex], refrTmNs);
}

void SVAdapter::setSmpMod(int asduIndex, uint8_t smpMod) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    // 必须在 setupComplete() 之前调用
    if (setupComplete_) {
        std::cerr << "Cannot set SmpMod after setupComplete()" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setSmpMod(asdus_[asduIndex], smpMod);
}

void SVAdapter::setSmpRate(int asduIndex, uint16_t smpRate) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    // 必须在 setupComplete() 之前调用
    if (setupComplete_) {
        std::cerr << "Cannot set SmpRate after setupComplete()" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setSmpRate(asdus_[asduIndex], smpRate);
}

void SVAdapter::setSmpSynch(int asduIndex, uint16_t smpSynch) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    // 必须在 setupComplete() 之前调用
    if (setupComplete_) {
        std::cerr << "Cannot set SmpSynch after setupComplete()" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_setSmpSynch(asdus_[asduIndex], smpSynch);
}

bool SVAdapter::publish() {
    if (!publisher_ || !enabled_) {
        return false;
    }

    // 调用发布回调
    if (publishCallback_) {
        for (size_t i = 0; i < asdus_.size(); ++i) {
            uint16_t smpCnt = getSmpCnt(static_cast<int>(i));
            publishCallback_(getSVId(), smpCnt);
        }
    }

    // 发布所有 ASDU
    SVPublisher_publish(publisher_);

    std::cout << "SV published successfully, ASDU count: "
              << asdus_.size() << std::endl;
    return true;
}

void SVAdapter::setupComplete() {
    if (!publisher_) {
        return;
    }

    SVPublisher_setupComplete(publisher_);
    setupComplete_ = true;

    std::cout << "SV setup completed" << std::endl;
}

void SVAdapter::resetASDUBuffer(int asduIndex) {
    if (asduIndex < 0 || asduIndex >= static_cast<int>(asdus_.size())) {
        return;
    }

    std::lock_guard<std::mutex> lock(asduMutex_);
    SVPublisher_ASDU_resetBuffer(asdus_[asduIndex]);
}

size_t SVAdapter::getMemberCount(int asduIndex) const {
    if (asduIndex >= 0 && asduIndex < static_cast<int>(members_.size())) {
        return members_[asduIndex].size();
    }
    return 0;
}

uint16_t SVAdapter::getSmpCnt(int asduIndex) const {
    if (asduIndex >= 0 && asduIndex < static_cast<int>(asdus_.size())) {
        return SVPublisher_ASDU_getSmpCnt(asdus_[asduIndex]);
    }
    return 0;
}

void SVAdapter::cleanupASDUs() {
    for (auto asdu : asdus_) {
        if (asdu) {
            // ASDU 会由 SVPublisher_destroy 释放
        }
    }
    asdus_.clear();
    members_.clear();
    setupComplete_ = false;
}

} // namespace IPC

#endif // HAS_LIBIEC61850
