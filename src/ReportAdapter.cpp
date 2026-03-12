/**
 * @file ReportAdapter.cpp
 * @brief 报告控制块适配器实现
 */

#ifdef HAS_LIBIEC61850

#include "ReportAdapter.h"
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <chrono>

namespace IPC {

ReportAdapter::ReportAdapter(IedServer* server, const ReportControlConfig& config)
    : config_(config)
    , server_(server)
    , rcb_(nullptr)
    , enabled_(false)
{
}

ReportAdapter::~ReportAdapter() {
    destroy();
}

ReportAdapter* ReportAdapter::create(IedServer* server, const ReportControlConfig& config) {
    ReportAdapter* adapter = new ReportAdapter(server, config);
    if (adapter->initialize()) {
        return adapter;
    }
    delete adapter;
    return nullptr;
}

void ReportAdapter::destroy() {
    if (rcb_) {
        // 报告控制块由服务器管理，不需要单独销毁
        rcb_ = nullptr;
    }
    cleanupEntries();
}

bool ReportAdapter::initialize() {
    if (!server_) {
        std::cerr << "Invalid server parameter" << std::endl;
        return false;
    }

    // 根据类型创建或获取报告控制块
    switch (config_.type) {
        case ReportType::BUFFERED:
            rcb_ = IedServer_getBufferedReportControlBlock(server_, config_.rptId.c_str());
            break;

        case ReportType::UNBUFFERED:
            rcb_ = IedServer_getUnbufferedReportControlBlock(server_, config_.rptId.c_str());
            break;

        default:
            std::cerr << "Unsupported report type: " << static_cast<int>(config_.type) << std::endl;
            return false;
    }

    if (!rcb_) {
        std::cerr << "Failed to get report control block for " << config_.rptId << std::endl;
        return false;
    }

    // 配置可选域
    MmsValue* optFlds = MmsValue_newBitString(8, static_cast<uint8_t>(config_.optFlds));
    IedServer_setRCBBufferedConfRev(server_, rcb_, config_.confRev);
    IedServer_setRCBOptFlds(server_, rcb_, optFlds);
    IedServer_setRCBTrgOps(server_, rcb_, config_.trgOps);
    IedServer_setRCBBufTm(server_, rcb_, config_.bufTime);
    IedServer_setRCBIntgPd(server_, rcb_, config_.intgPd);

    MmsValue_delete(optFlds);

    std::cout << "ReportAdapter initialized: " << config_.rptId
              << " (type=" << (config_.type == ReportType::BUFFERED ? "BRCB" : "URCB")
              << ")" << std::endl;

    return true;
}

bool ReportAdapter::enable(bool enable) {
    if (!rcb_) {
        return false;
    }

    enabled_ = enable;

    // 注意：libiec61850 报告控制块默认是启用的
    // 这里主要控制我们是否会响应报告请求

    std::cout << "Report control block " << (enable ? "enabled" : "disabled")
              << " for " << config_.rptId << std::endl;
    return true;
}

bool ReportAdapter::trigger(uint8_t trigger) {
    if (!enabled_ || !rcb_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(entriesMutex_);

    // 清空缓冲区
    IedServer_clearRCBEntryBuffer(server_, rcb_);

    // 添加所有条目到报告
    for (const auto& entry : entries_) {
        if (!entry.value) {
            std::cerr << "Null value in report entry for " << entry.objectRef << std::endl;
            continue;
        }

        // 解析对象引用
        char* objectRef = const_cast<char*>(entry.objectRef.c_str());

        // 根据值类型获取/设置数据值
        bool valueSet = false;
        switch (MmsValue_getType(entry.value)) {
            case MMS_BOOLEAN:
                if (MmsValue_getBoolean(entry.value)) {
                    IedServer_updateBooleanAttributeValue(server_, objectRef, MmsValue_getBoolean(entry.value));
                    valueSet = true;
                }
                break;

            case MMS_INTEGER:
                if (MmsValue_getInteger(entry.value)) {
                    IedServer_updateInt32AttributeValue(server_, objectRef, MmsValue_getInteger(entry.value));
                    valueSet = true;
                }
                break;

            case MMS_FLOAT:
                if (MmsValue_getFloat(entry.value)) {
                    IedServer_updateFloatAttributeValue(server_, objectRef, MmsValue_getFloat(entry.value));
                    valueSet = true;
                }
                break;

            case MMS_UNSIGNED:
                if (MmsValue_getUnsigned(entry.value)) {
                    IedServer_updateUnsignedAttributeValue(server_, objectRef, MmsValue_getUnsigned(entry.value));
                    valueSet = true;
                }
                break;

            case MMS_BIT_STRING: {
                if (MmsValue_getBitString(entry.value)) {
                    IedServer_updateBitStringAttributeValue(server_, objectRef, MmsValue_getBitString(entry.value));
                    valueSet = true;
                }
                break;

            default:
                std::cerr << "Unsupported value type in report: " << MmsValue_getType(entry.value) << std::endl;
                break;
        }

        // 添加可选域值
        if (valueSet) {
            // 时间戳
            IedServer_setRCBEntryTime(server_, rcb_, entry.timestamp);

            // 原因代码
            char reasonCode[9] = {0, 0, 0, 0};
            IedServer_setRCBEntryReasonForCode(server_, rcb_, reasonCode);
        }
    }

    // 触发报告
    bool result = IedServer_triggerRCBReport(server_, rcb_) == IEC61850_ERROR_OK;

    if (result) {
        std::cout << "Report triggered successfully for " << config_.rptId
                  << " with " << entries_.size() << " entries" << std::endl;
    } else {
        std::cerr << "Failed to trigger report for " << config_.rptId << std::endl;
    }

    return result;
}

int ReportAdapter::addReportEntries(const std::vector<ReportEntry>& entries) {
    int count = 0;
    for (const auto& entry : entries) {
        if (addReportEntry(entry)) {
            count++;
        }
    }
    return count;
}

bool ReportAdapter::addReportEntry(const ReportEntry& entry) {
    if (!entry.value) {
        return false;
    }

    std::lock_guard<std::mutex> lock(entriesMutex_);

    // 检查缓冲区大小
    if (config_.bufSize > 0 && entries_.size() >= config_.bufSize) {
        std::cerr << "Report buffer overflow, dropping oldest entry" << std::endl;
        // 移除最旧的条目
        entries_.erase(entries_.begin());
    }

    // 释放旧值
    auto it = entries_.begin();
    while (it != entries_.end()) {
        if (it->objectRef == entry.objectRef) {
            if (it->value) {
                MmsValue_delete(it->value);
            }
            entries_.erase(it);
            break;
        }
        ++it;
    }

    // 克隆值（保持所有权）
    MmsValue* clonedValue = MmsValue_clone(entry.value);
    if (!clonedValue) {
        std::cerr << "Failed to clone value for " << entry.objectRef << std::endl;
        return false;
    }

    // 添加新条目
    entries_.push_back(entry);
    entry.value = clonedValue;

    // 调用数据集变化监听
    if (dataChangeListener_) {
        dataChangeListener_(config_.dataSetRef);
    }

    return true;
}

void ReportAdapter::setDataSetChangeListener(std::function<void(const std::string& dataSetRef)> listener) {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    dataChangeListener_ = listener;
}

void ReportAdapter::cleanupEntries() {
    for (auto& entry : entries_) {
        if (entry.value) {
            MmsValue_delete(entry.value);
        }
    }
    entries_.clear();
}

} // namespace IPC

#endif // HAS_LIBIEC61850
