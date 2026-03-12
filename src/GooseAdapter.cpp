/**
 * @file GooseAdapter.cpp
 * @brief GOOSE 发布器适配器实现
 */

#ifdef HAS_LIBIEC61850

#include "GooseAdapter.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace IPC {

GooseAdapter::GooseAdapter(const GooseCommConfig& config)
    : config_(config)
    , publisher_(nullptr)
    , dataSet_(nullptr)
    , stNum_(1)
    , sqNum_(0)
    , enabled_(true)
{
}

GooseAdapter::~GooseAdapter() {
    destroy();
}

GooseAdapter* GooseAdapter::create(const GooseCommConfig& config) {
    GooseAdapter* adapter = new GooseAdapter(config);
    if (adapter->initialize()) {
        return adapter;
    }
    delete adapter;
    return nullptr;
}

void GooseAdapter::destroy() {
    if (publisher_) {
        GoosePublisher_destroy(publisher_);
        publisher_ = nullptr;
    }
    cleanupValues();

    if (dataSet_) {
        LinkedList_destroy(dataSet_);
        dataSet_ = nullptr;
    }
}

bool GooseAdapter::initialize() {
    // 创建通信参数
    CommParameters params;
    params.vlanPriority = config_.vlanPriority;
    params.vlanId = config_.vlanId;
    params.appId = config_.appId;
    memcpy(params.dstAddress, config_.dstAddress, 6);

    // 创建 GOOSE 发布器
    if (config_.useVlanTag) {
        publisher_ = GoosePublisher_createEx(&params, config_.interfaceName.c_str(), true);
    } else {
        publisher_ = GoosePublisher_create(&params, config_.interfaceName.c_str());
    }

    if (!publisher_) {
        std::cerr << "Failed to create GoosePublisher" << std::endl;
        return false;
    }

    // 设置 GOOSE 控制块参数
    if (!config_.goID.empty()) {
        GoosePublisher_setGoID(publisher_, const_cast<char*>(config_.goID.c_str()));
    }

    if (!config_.goCBRef.empty()) {
        GoosePublisher_setGoCbRef(publisher_, const_cast<char*>(config_.goCBRef.c_str()));
    }

    if (!config_.dataSetRef.empty()) {
        GoosePublisher_setDataSetRef(publisher_, const_cast<char*>(config_.dataSetRef.c_str()));
    }

    GoosePublisher_setConfRev(publisher_, config_.confRev);
    GoosePublisher_setTimeAllowedToLive(publisher_, config_.timeAllowedToLive);
    GoosePublisher_setNeedsCommission(publisher_, false);  // 不需要调试模式

    std::cout << "GooseAdapter initialized on " << config_.interfaceName
              << " with GoID=" << config_.goID
              << ", GoCBRef=" << config_.goCBRef << std::endl;

    return true;
}

int GooseAdapter::addDataSetMember(const GooseDataSetMember& member) {
    if (!publisher_) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(valuesMutex_);

    // 创建对应类型的 MmsValue
    MmsValue* value = nullptr;
    switch (member.type) {
        case GooseDataType::BOOLEAN:
            value = MmsValue_newBoolean(false);
            break;
        case GooseDataType::INTEGER:
            value = MmsValue_newInteger(0);
            break;
        case GooseDataType::FLOAT:
            value = MmsValue_newFloat(0.0f);
            break;
        case GooseDataType::BITSTRING: {
            value = MmsValue_newBitString(2, 0);
            break;
        }
        default:
            std::cerr << "Unknown GooseDataType: " << static_cast<int>(member.type) << std::endl;
            return -1;
    }

    if (!value) {
        return -1;
    }

    int index = static_cast<int>(members_.size());
    member.index = index;
    member.value = value;
    members_.push_back(member);
    values_.push_back(value);

    // 重建数据集链表
    if (dataSet_) {
        LinkedList_destroy(dataSet_);
    }

    dataSet_ = LinkedList_create();
    for (const auto& m : members_) {
        LinkedList_add(dataSet_, static_cast<void*>(m.value));
    }

    std::cout << "Added GOOSE member: " << member.name
              << " at index " << index << std::endl;

    return index;
}

int GooseAdapter::addDataSetMembers(const std::vector<GooseDataSetMember>& members) {
    int count = 0;
    for (const auto& member : members) {
        if (addDataSetMember(member) >= 0) {
            count++;
        }
    }
    return count;
}

void GooseAdapter::updateMemberValue(int index, MmsValue* value) {
    if (index < 0 || index >= static_cast<int>(members_.size())) {
        std::cerr << "Invalid member index: " << index << std::endl;
        return;
    }

    if (!value) {
        std::cerr << "Null value provided for member " << index << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(valuesMutex_);

    // 更新链表中的值
    LinkedList dataSet = LinkedList_getNext(dataSet_);
    for (int i = 0; i <= index; ++i) {
        dataSet = LinkedList_getNext(dataSet);
    }

    // 释放旧值，设置新值
    if (values_[index]) {
        MmsValue_delete(values_[index]);
    }
    values_[index] = value;
    members_[index].value = value;

    // 更新链表
    if (dataSet) {
        LinkedList_insertAfter(dataSet_, static_cast<void*>(value));
    }
}

bool GooseAdapter::publish(bool increaseStNum) {
    if (!publisher_ || !enabled_) {
        return false;
    }

    // 调用数据变化回调
    if (dataChangeCallback_) {
        dataChangeCallback_(config_.goCBRef);
    }

    // 增加序列号
    if (increaseStNum) {
        GoosePublisher_increaseStNum(publisher_);
        stNum_ = GoosePublisher_getStNum(publisher_);
        sqNum_ = 0;
    } else {
        sqNum_++;
    }

    // 发布 GOOSE 报文
    int result = GoosePublisher_publish(publisher_, dataSet_);

    if (result == 0) {
        std::cout << "GOOSE published successfully, stNum=" << stNum_
                  << ", sqNum=" << sqNum_ << std::endl;
        return true;
    }

    std::cerr << "Failed to publish GOOSE, error code: " << result << std::endl;
    return false;
}

uint64_t GooseAdapter::increaseStNum() {
    if (!publisher_) {
        return 0;
    }
    return GoosePublisher_increaseStNum(publisher_);
}

void GooseAdapter::setStNum(uint32_t stNum) {
    if (publisher_) {
        GoosePublisher_setStNum(publisher_, stNum);
        stNum_ = stNum;
    }
}

void GooseAdapter::setSqNum(uint32_t sqNum) {
    if (publisher_) {
        GoosePublisher_setSqNum(publisher_, sqNum);
        sqNum_ = sqNum;
    }
}

void GooseAdapter::reset() {
    if (publisher_) {
        GoosePublisher_reset(publisher_);
        stNum_ = 1;
        sqNum_ = 0;
    }
}

const GooseDataSetMember* GooseAdapter::getMember(int index) const {
    if (index >= 0 && index < static_cast<int>(members_.size())) {
        return &members_[index];
    }
    return nullptr;
}

void GooseAdapter::cleanupValues() {
    for (auto value : values_) {
        if (value) {
            MmsValue_delete(value);
        }
    }
    values_.clear();
    members_.clear();
}

} // namespace IPC

#endif // HAS_LIBIEC61850
