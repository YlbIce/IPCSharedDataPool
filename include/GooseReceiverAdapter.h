/**
 * @file GooseReceiverAdapter.h
 * @brief GOOSE 订阅器 C++ 适配器
 *
 * 基于 libiec61850 的 GooseReceiver 实现的 C++ 封装，
 * 提供面向对象的订阅和管理接口。
 */

#ifndef GOOSE_RECEIVER_ADAPTER_H
#define GOOSE_RECEIVER_ADAPTER_H

#ifdef HAS_LIBIEC61850

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <goose_subscriber.h>
#include <goose_receiver.h>
#include <mms_value.h>

namespace IPC {

/**
 * @brief GOOSE 消息回调函数类型
 */
using GooseMessageCallback = std::function<void(
    const std::string& goCBRef,
    const std::string& goID,
    MmsValue* values,
    uint32_t stNum,
    uint32_t sqNum,
    uint64_t timestamp
)>;

/**
 * @brief GOOSE 订阅信息
 */
struct GooseSubscription {
    std::string goCBRef;            // GOOSE 控制块引用
    GooseMessageCallback callback;  // 消息回调
    GooseSubscriber subscriber;          // libiec61850 订阅器
    MmsValue* dataSetValues;          // 数据集值 (由 Adapter 管理)
    bool isValid;                   // 订阅是否有效
    uint32_t lastStNum;              // 最后收到状态号
    uint32_t lastSqNum;              // 最后收到序列号
    uint64_t lastTimestamp;           // 最后接收时间戳
};

/**
 * @brief GOOSE 订阅器统计信息
 */
struct GooseReceiverStats {
    uint64_t totalMessagesReceived;   // 总接收消息数
    uint64_t validMessagesReceived;  // 有效消息数
    uint64_t invalidMessagesReceived; // 无效消息数
    uint64_t parseErrors;            // 解析错误数
    uint32_t activeSubscriptions;      // 活跃订阅数
};

/**
 * @brief GOOSE 接收器适配器类
 *
 * 提供对 libiec61850 GooseReceiver 的 C++ 封装，
 * 支持多订阅管理和消息处理。
 */
class GooseReceiverAdapter {
public:
    /**
     * @brief 创建 GOOSE 接收器适配器
     * @param interfaceId 网络接口名 (eth0)
     * @return 成功返回指针，失败返回 nullptr
     */
    static GooseReceiverAdapter* create(const std::string& interfaceId);

    /**
     * @brief 销毁 GOOSE 接收器
     */
    void destroy();

    /**
     * @brief 启动接收器线程
     * @return 成功返回 true
     */
    bool start();

    /**
     * @brief 停止接收器线程
     */
    void stop();

    /**
     * @brief 检查接收器是否正在运行
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief 订阅 GOOSE 控制块
     * @param goCBRef GOOSE 控制块引用 (如 "simpleIO/LLN0$GO$gcbEvents")
     * @param callback 消息回调函数
     * @param dataSetValue 可选的预分配数据集值
     * @return 成功返回订阅 ID，失败返回 -1
     */
    int subscribe(const std::string& goCBRef, GooseMessageCallback callback,
               MmsValue* dataSetValue = nullptr);

    /**
     * @brief 取消订阅
     * @param subscriptionId 订阅 ID
     * @return 成功返回 true
     */
    bool unsubscribe(int subscriptionId);

    /**
     * @brief 取消指定 GOCBRef 的订阅
     * @param goCBRef GOOSE 控制块引用
     * @return 成功返回 true
     */
    bool unsubscribeByRef(const std::string& goCBRef);

    /**
     * @brief 取消所有订阅
     * @return 取消的订阅数量
     */
    int unsubscribeAll();

    /**
     * @brief 处理接收到的 GOOSE 消息
     * @param timeoutMs 处理超时 (毫秒)，0 表示处理所有消息
     * @return 处理的消息数量
     */
    int processMessages(int timeoutMs = 0);

    /**
     * @brief 设置目标 MAC 地址过滤
     * @param dstMac 目标 MAC 地址 (6 字节)
     */
    void setDstMacFilter(const uint8_t dstMac[6]);

    /**
     * @brief 设置 APPID 过滤
     * @param appId 应用标识
     */
    void setAppIdFilter(uint16_t appId);

    /**
     * @brief 设置为观察者模式 (接收所有 GOOSE 消息)
     */
    void setObserverMode(bool enable);

    /**
     * @brief 获取订阅数量
     */
    size_t getSubscriptionCount() const { return subscriptions_.size(); }

    /**
     * @brief 获取统计信息
     */
    GooseReceiverStats getStats() const { return stats_; }

    /**
     * @brief 重置统计信息
     */
    void resetStats();

    /**
     * @brief 获取指定订阅 ID 的订阅信息
     * @param subscriptionId 订阅 ID
     * @return 订阅信息指针，无效 ID 返回 nullptr
     */
    const GooseSubscription* getSubscription(int subscriptionId) const;

private:
    GooseReceiverAdapter(const std::string& interfaceId);
    ~GooseReceiverAdapter();

    // 禁止拷贝
    GooseReceiverAdapter(const GooseReceiverAdapter&) = delete;
    GooseReceiverAdapter& operator=(const GooseReceiverAdapter&) = delete;

    /**
     * @brief 接收器线程主循环
     */
    void receiverThreadFunc();

    /**
     * @brief 处理单个 GOOSE 订阅器的消息
     * @param sub 订阅信息
     */
    void handleSubscriptionMessage(GooseSubscription& sub);

    /**
     * @brief 生成下一个订阅 ID
     */
    int generateSubscriptionId() const { return nextSubscriptionId_++; }

private:
    std::string interfaceId_;                          // 网络接口名
    GooseReceiver receiver_;                           // libiec61850 接收器
    std::unordered_map<int, GooseSubscription> subscriptions_;  // 订阅映射
    std::unordered_map<std::string, int> refToIdMap_;     // 引用到 ID 映射

    std::atomic<bool> running_;                       // 运行标志
    std::thread receiverThread_;                        // 接收线程

    GooseReceiverStats stats_;                          // 统计信息

    std::mutex subscriptionsMutex_;                     // 订阅锁
    std::atomic<int> nextSubscriptionId_;               // 下一个订阅 ID

    uint8_t dstMacFilter_[6];                        // MAC 地址过滤
    uint16_t appIdFilter_;                             // APPID 过滤 (0 表示不过滤)
    bool useMacFilter_;                               // 是否使用 MAC 过滤
    bool useAppIdFilter_;                             // 是否使用 APPID 过滤
};

} // namespace IPC

#endif // HAS_LIBIEC61850
#endif // GOOSE_RECEIVER_ADAPTER_H
