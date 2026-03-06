#ifndef IPC_EVENT_CENTER_H
#define IPC_EVENT_CENTER_H

#include "Common.h"
#include "ShmRingBuffer.h"
#include "ProcessRWLock.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <functional>

namespace IPC {

// ========== 事件中心头部结构 ==========

struct EventCenterHeader {
    uint32_t magic;           // 魔数
    uint32_t version;         // 版本号
    uint32_t eventCapacity;   // 事件缓冲区容量
    uint32_t subscriberCount; // 订阅者数量
    uint64_t totalEvents;     // 总事件数
    uint64_t lastEventTime;   // 最后事件时间
    
    ProcessRWLock lock;       // 锁
};

// ========== 订阅回调类型 ==========

using EventCallback = std::function<void(const Event&)>;

// ========== 事件中心主类 ==========

/**
 * @brief 跨进程事件中心
 * 
 * 特点：
 * - 基于环形缓冲区的事件队列
 * - 支持多发布者多订阅者
 * - 每个订阅者维护独立的读取位置
 * - 支持事件过滤（按类型、地址等）
 */
class IPCEventCenter {
public:
    /**
     * @brief 创建事件中心
     * @param name 共享内存名称
     * @param eventCapacity 事件缓冲区容量
     */
    static IPCEventCenter* create(const char* name, uint32_t eventCapacity = 10000);
    
    /**
     * @brief 连接到已存在的事件中心
     */
    static IPCEventCenter* connect(const char* name);
    
    /**
     * @brief 销毁事件中心
     */
    void destroy();
    
    /**
     * @brief 断开连接
     */
    void disconnect();
    
    // ========== 事件发布 ==========
    
    /**
     * @brief 发布事件
     */
    Result publish(const Event& event);
    
    /**
     * @brief 批量发布事件
     */
    Result publishBatch(const Event* events, uint32_t count, uint32_t& published);
    
    /**
     * @brief 发布数据变更事件（便捷方法）
     */
    Result publishDataChange(uint64_t key, PointType type, 
                             uint32_t oldValue, uint32_t newValue,
                             const char* source = nullptr);
    
    /**
     * @brief 发布数据变更事件（浮点版本）
     */
    Result publishDataChange(uint64_t key, PointType type,
                             float oldValue, float newValue,
                             const char* source = nullptr);
    
    // ========== 事件订阅 ==========
    
    /**
     * @brief 订阅事件（非阻塞）
     * @param callback 回调函数
     * @param subscriberId 输出订阅者ID
     */
    Result subscribe(EventCallback callback, uint32_t& subscriberId);
    
    /**
     * @brief 取消订阅
     */
    Result unsubscribe(uint32_t subscriberId);
    
    /**
     * @brief 获取订阅者数量
     */
    uint32_t getSubscriberCount() const;
    
    // ========== 事件消费 ==========
    
    /**
     * @brief 拉取事件（非阻塞）
     * @param subscriberId 订阅者ID
     * @param event 输出事件
     */
    Result poll(uint32_t subscriberId, Event& event);
    
    /**
     * @brief 等待事件（阻塞）
     * @param subscriberId 订阅者ID
     * @param event 输出事件
     * @param timeoutMs 超时时间（毫秒），-1 表示无限等待
     */
    Result wait(uint32_t subscriberId, Event& event, int timeoutMs = -1);
    
    /**
     * @brief 处理所有待处理事件
     * @param subscriberId 订阅者ID
     * @param maxEvents 最大处理事件数（0表示无限制）
     * @return 处理的事件数量
     */
    uint32_t process(uint32_t subscriberId, uint32_t maxEvents = 0);
    
    // ========== 状态查询 ==========
    
    bool isValid() const { return m_shmPtr != nullptr && m_header != nullptr; }
    uint32_t getEventCapacity() const { return m_header ? m_header->eventCapacity : 0; }
    uint64_t getTotalEvents() const { return m_header ? m_header->totalEvents : 0; }
    
    /**
     * @brief 获取待处理事件数量
     */
    uint32_t getPendingEvents(uint32_t subscriberId) const;
    
    /**
     * @brief 获取订阅者读取位置
     */
    uint32_t getSubscriberReadIndex(uint32_t subscriberId) const;
    
    const char* getName() const { return m_name.c_str(); }

private:
    IPCEventCenter() = default;
    
    // 禁止拷贝
    IPCEventCenter(const IPCEventCenter&) = delete;
    IPCEventCenter& operator=(const IPCEventCenter&) = delete;
    
    bool initFromShm();
    
private:
    std::string m_name;
    void* m_shmPtr;
    size_t m_shmSize;
    int m_shmFd;
    bool m_isCreator;
    
    EventCenterHeader* m_header;
    ShmRingBuffer<Event>* m_ringBuffer;
    void* m_ringBufferMemory;  // 环形缓冲区数据区
    
    // 本地订阅者信息
    struct LocalSubscriber {
        uint32_t readIndex;
        EventCallback callback;
        bool active;
    };
    LocalSubscriber m_subscribers[MAX_PROCESS_COUNT];
    uint32_t m_nextSubscriberId;
    
public:
    ~IPCEventCenter() = default;
};

} // namespace IPC

#endif // IPC_EVENT_CENTER_H
