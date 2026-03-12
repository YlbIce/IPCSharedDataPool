/**
 * @file GooseReceiverAdapter.cpp
 * @brief GOOSE 订阅器适配器实现
 */

#ifdef HAS_LIBIEC61850

#include "GooseReceiverAdapter.h"
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <chrono>

namespace IPC {

// GOOSE 消息回调包装器
static void gooseCallbackWrapper(GooseSubscriber subscriber, void* parameter) {
    GooseReceiverAdapter* adapter = static_cast<GooseReceiverAdapter*>(parameter);
    if (!adapter || !subscriber) {
        return;
    }

    // 查找对应的订阅
    char* goCBRef = GooseSubscriber_getGoCbRef(subscriber);
    if (!goCBRef) {
        return;
    }

    std::string goRef(goCBRef);

    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(adapter->subscriptionsMutex_));
    for (auto& pair : adapter->subscriptions_) {
        GooseSubscription& sub = pair.second;
        if (sub.goCBRef == goRef && sub.subscriber == subscriber) {
            adapter->handleSubscriptionMessage(sub);
            return;
        }
    }
}

GooseReceiverAdapter::GooseReceiverAdapter(const std::string& interfaceId)
    : receiver_(nullptr)
    , running_(false)
    , nextSubscriptionId_(0)
    , useMacFilter_(false)
    , appIdFilter_(0)
    , useAppIdFilter_(false)
{
    interfaceId_ = interfaceId;
    memset(stats_, 0, sizeof(stats_));
    memset(dstMacFilter_, 0, sizeof(dstMacFilter_));
}

GooseReceiverAdapter::~GooseReceiverAdapter() {
    stop();
    destroy();
}

GooseReceiverAdapter* GooseReceiverAdapter::create(const std::string& interfaceId) {
    GooseReceiverAdapter* adapter = new GooseReceiverAdapter(interfaceId);

    // 创建 GOOSE 接收器
    adapter->receiver_ = GooseReceiver_create();

    if (!adapter->receiver_) {
        std::cerr << "Failed to create GooseReceiver" << std::endl;
        delete adapter;
        return nullptr;
    }

    std::cout << "GooseReceiverAdapter created on " << interfaceId << std::endl;
    return adapter;
}

void GooseReceiverAdapter::destroy() {
    if (receiver_) {
        GooseReceiver_destroy(receiver_);
        receiver_ = nullptr;
    }

    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    for (auto& pair : subscriptions_) {
        if (pair.second.dataSetValue) {
            MmsValue_delete(pair.second.dataSetValue);
        }
        if (pair.second.subscriber) {
            // 注意：不要在这里销毁 subscriber，因为它由 receiver_ 管理
        }
    }
    subscriptions_.clear();
    refToIdMap_.clear();
}

bool GooseReceiverAdapter::start() {
    if (running_.load()) {
        return true;  // 已在运行
    }

    if (!receiver_) {
        return false;
    }

    running_.store(true);
    receiverThread_ = std::thread(&GooseReceiverAdapter::receiverThreadFunc, this);

    std::cout << "GooseReceiverAdapter started" << std::endl;
    return true;
}

void GooseReceiverAdapter::stop() {
    running_.store(false);

    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }

    std::cout << "GooseReceiverAdapter stopped" << std::endl;
}

void GooseReceiverAdapter::receiverThreadFunc() {
    auto sleepInterval = std::chrono::milliseconds(10);  // 10ms 轮询间隔

    while (running_.load()) {
        // 处理所有订阅者
        std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        for (auto& pair : subscriptions_) {
            GooseSubscription& sub = pair.second;
            if (sub.subscriber) {
                handleSubscriptionMessage(sub);
            }
        }

        std::this_thread::sleep_for(sleepInterval);
    }
}

void GooseReceiverAdapter::handleSubscriptionMessage(GooseSubscription& sub) {
    // 检查订阅者是否有效
    if (!GooseSubscriber_isValid(sub.subscriber)) {
        stats_.invalidMessagesReceived++;
        return;
    }

    // 检查过滤条件
    if (useMacFilter_) {
        uint8_t dstMac[6];
        GooseSubscriber_getDstMac(sub.subscriber, dstMac);
        bool match = (memcmp(dstMac, dstMacFilter_, 6) == 0);
        if (!match) {
            return;  // MAC 不匹配，忽略
        }
    }

    if (useAppIdFilter_ && appIdFilter_ != 0) {
        int16_t appId = GooseSubscriber_getAppId(sub.subscriber);
        if (appId != appIdFilter_) {
            return;  // APPID 不匹配，忽略
        }
    }

    // 获取消息信息
    char* goID = GooseSubscriber_getGoId(sub.subscriber);
    char* goCBRef = GooseSubscriber_getGoCbRef(sub.subscriber);
    uint32_t stNum = GooseSubscriber_getStNum(sub.subscriber);
    uint32_t sqNum = GooseSubscriber_getSqNum(sub.subscriber);
    uint64_t timestamp = GooseSubscriber_getTimestamp(sub.subscriber);

    bool isTest = GooseSubscriber_isTest(sub.subscriber);
    if (isTest) {
        return;  // 忽略测试消息
    }

    bool needsCommission = GooseSubscriber_needsCommission(sub.subscriber);
    if (needsCommission) {
        return;  // 忽略需要调试的消息
    }

    // 更新统计信息
    stats_.totalMessagesReceived++;
    if (stNum != sub.lastStNum || sqNum != sub.lastSqNum) {
        stats_.validMessagesReceived++;
    } else {
        // 重复消息
        stats_.totalMessagesReceived++;
    }

    // 更新订阅信息
    sub.lastStNum = stNum;
    sub.lastSqNum = sqNum;
    sub.lastTimestamp = timestamp;
    sub.isValid = true;

    // 释放字符串 (避免内存泄漏)
    if (goID) free(goID);
    if (goCBRef) free(goCBRef);

    // 调用用户回调
    if (sub.callback) {
        MmsValue* values = GooseSubscriber_getDataSetValues(sub.subscriber);
        sub.callback(sub.goCBRef, sub.goID, values, stNum, sqNum, timestamp);
    } else {
        // 如果没有回调，获取数据集值用于验证
        MmsValue* values = GooseSubscriber_getDataSetValues(sub.subscriber);
        // values 会在下一次调用时被覆盖或销毁时释放
    }

    GooseParseError parseError = GooseSubscriber_getParseError(sub.subscriber);
    if (parseError != GOOSE_PARSE_ERROR_NO_ERROR) {
        stats_.parseErrors++;
        std::cerr << "GOOSE parse error: " << parseError
                  << " for " << sub.goCBRef << std::endl;
    }
}

int GooseReceiverAdapter::subscribe(const std::string& goCBRef, GooseMessageCallback callback,
                                  MmsValue* dataSetValue) {
    if (!receiver_) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(subscriptionsMutex_);

    // 检查是否已订阅
    if (refToIdMap_.find(goCBRef) != refToIdMap_.end()) {
        std::cerr << "Already subscribed to: " << goCBRef << std::endl;
        return -1;
    }

    // 创建订阅者
    char* goCBRefCStr = strdup(goCBRef.c_str());
    GooseSubscriber subscriber = GooseSubscriber_create(goCBRefCStr, dataSetValue);
    free(goCBRefCStr);

    if (!subscriber) {
        std::cerr << "Failed to create GooseSubscriber for " << goCBRef << std::endl;
        return -1;
    }

    // 设置回调
    GooseSubscriber_setListener(subscriber, gooseCallbackWrapper, this);

    // 添加到接收器
    GooseReceiver_addSubscriber(receiver_, subscriber);

    // 存储订阅信息
    int subId = generateSubscriptionId();
    GooseSubscription sub;
    sub.goCBRef = goCBRef;
    sub.callback = callback;
    sub.subscriber = subscriber;
    sub.dataSetValue = dataSetValue;
    sub.isValid = false;
    sub.lastStNum = 0;
    sub.lastSqNum = 0;
    sub.lastTimestamp = 0;

    subscriptions_[subId] = sub;
    refToIdMap_[goCBRef] = subId;

    stats_.activeSubscriptions = static_cast<uint32_t>(subscriptions_.size());

    std::cout << "Subscribed to GOOSE: " << goCBRef
              << " with subscription ID: " << subId << std::endl;

    return subId;
}

bool GooseReceiverAdapter::unsubscribe(int subscriptionId) {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);

    auto it = subscriptions_.find(subscriptionId);
    if (it == subscriptions_.end()) {
        return false;
    }

    GooseSubscription& sub = it->second;

    // 从引用映射中移除
    refToIdMap_.erase(sub.goCBRef);

    // 清理数据集值
    if (sub.dataSetValue) {
        MmsValue_delete(sub.dataSetValue);
    }

    // 注意：订阅者由 receiver_ 管理，不要单独销毁
    subscriptions_.erase(it);
    stats_.activeSubscriptions = static_cast<uint32_t>(subscriptions_.size());

    std::cout << "Unsubscribed from GOOSE (ID: " << subscriptionId << ")" << std::endl;
    return true;
}

bool GooseReceiverAdapter::unsubscribeByRef(const std::string& goCBRef) {
    auto it = refToIdMap_.find(goCBRef);
    if (it == refToIdMap_.end()) {
        return false;
    }
    return unsubscribe(it->second);
}

int GooseReceiverAdapter::unsubscribeAll() {
    std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    int count = static_cast<int>(subscriptions_.size());

    // 清理所有数据集值
    for (auto& pair : subscriptions_) {
        if (pair.second.dataSetValue) {
            MmsValue_delete(pair.second.dataSetValue);
        }
    }

    subscriptions_.clear();
    refToIdMap_.clear();
    stats_.activeSubscriptions = 0;

    std::cout << "Unsubscribed all GOOSE subscriptions (" << count << ")" << std::endl;
    return count;
}

int GooseReceiverAdapter::processMessages(int timeoutMs) {
    int processed = 0;

    std::lock_guard<std::mutex> lock(subscriptionsMutex_);

    for (auto& pair : subscriptions_) {
        GooseSubscription& sub = pair.second;
        if (sub.isValid && sub.subscriber) {
            handleSubscriptionMessage(sub);
            processed++;
        }
    }

    return processed;
}

void GooseReceiverAdapter::setDstMacFilter(const uint8_t dstMac[6]) {
    memcpy(dstMacFilter_, dstMac, 6);
    useMacFilter_ = true;
    std::cout << "Set MAC filter: "
              << std::hex << static_cast<int>(dstMac[0]) << ":"
              << static_cast<int>(dstMac[1]) << ":"
              << static_cast<int>(dstMac[2]) << ":"
              << static_cast<int>(dstMac[3]) << ":"
              << static_cast<int>(dstMac[4]) << ":"
              << static_cast<int>(dstMac[5]) << std::dec << std::endl;
}

void GooseReceiverAdapter::setAppIdFilter(uint16_t appId) {
    appIdFilter_ = appId;
    useAppIdFilter_ = true;
    std::cout << "Set APPID filter: " << std::hex << appId << std::dec << std::endl;
}

void GooseReceiverAdapter::setObserverMode(bool enable) {
    // 观察者模式接收所有消息，不设置特定过滤
    if (enable) {
        useMacFilter_ = false;
        useAppIdFilter_ = false;
    }
    std::cout << "Observer mode: " << (enable ? "enabled" : "disabled") << std::endl;
}

void GooseReceiverAdapter::resetStats() {
    memset(&stats_, 0, sizeof(stats_));
    std::cout << "Statistics reset" << std::endl;
}

const GooseSubscription* GooseReceiverAdapter::getSubscription(int subscriptionId) const {
    auto it = subscriptions_.find(subscriptionId);
    if (it != subscriptions_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace IPC

#endif // HAS_LIBIEC61850
