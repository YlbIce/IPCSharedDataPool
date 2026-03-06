/**
 * @file IPCEventCenter.cpp
 * @brief 跨进程事件中心实现
 */

#include "../include/IPCEventCenter.h"
#include <cstring>

namespace IPC {

// ========== 创建事件中心 ==========

IPCEventCenter* IPCEventCenter::create(const char* name, uint32_t eventCapacity) {
    if (name == nullptr || strlen(name) == 0) {
        return nullptr;
    }
    
    // 计算所需内存大小
    size_t headerSize = sizeof(EventCenterHeader);
    headerSize = (headerSize + 63) & ~63; // 64字节对齐
    
    // 环形缓冲区大小（包括 ShmRingBuffer 对象和数据区）
    size_t ringBufferObjSize = sizeof(ShmRingBuffer<Event>);
    ringBufferObjSize = (ringBufferObjSize + 63) & ~63;
    
    size_t ringBufferDataSize = eventCapacity * sizeof(Event);
    ringBufferDataSize = (ringBufferDataSize + 63) & ~63;
    
    size_t requiredSize = headerSize + ringBufferObjSize + ringBufferDataSize;
    
    // 创建共享内存 - 如果已存在则先清理
    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (fd < 0) {
        // 共享内存可能已存在（上次异常退出），先删除再重试
        shm_unlink(name);
        fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
        if (fd < 0) {
            return nullptr;
        }
    }
    
    if (ftruncate(fd, requiredSize) < 0) {
        ::close(fd);
        shm_unlink(name);
        return nullptr;
    }
    
    void* ptr = mmap(nullptr, requiredSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        shm_unlink(name);
        return nullptr;
    }
    
    memset(ptr, 0, requiredSize);
    
    // 初始化头部
    EventCenterHeader* header = static_cast<EventCenterHeader*>(ptr);
    header->magic = SHM_MAGIC;
    header->version = SHM_VERSION;
    header->eventCapacity = eventCapacity;
    header->subscriberCount = 0;
    header->totalEvents = 0;
    header->lastEventTime = 0;
    
    // 初始化锁
    header->lock.initialize();
    
    // 初始化环形缓冲区
    char* base = static_cast<char*>(ptr);
    ShmRingBuffer<Event>* ringBuffer = reinterpret_cast<ShmRingBuffer<Event>*>(base + headerSize);
    void* ringBufferData = base + headerSize + ringBufferObjSize;
    
    // 在共享内存中构造 ShmRingBuffer
    new (ringBuffer) ShmRingBuffer<Event>();
    // 传入共享内存基地址，以便存储相对偏移量
    Result ret = ringBuffer->initialize(ringBufferData, eventCapacity, ptr);
    if (ret != Result::OK) {
        header->lock.destroy();
        munmap(ptr, requiredSize);
        ::close(fd);
        shm_unlink(name);
        return nullptr;
    }
    
    // 创建对象
    IPCEventCenter* center = new IPCEventCenter();
    center->m_name = name;
    center->m_shmPtr = ptr;
    center->m_shmSize = requiredSize;
    center->m_shmFd = fd;
    center->m_isCreator = true;
    center->m_header = header;
    center->m_ringBuffer = ringBuffer;
    center->m_ringBufferMemory = ringBufferData;
    center->m_nextSubscriberId = 0;
    
    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        center->m_subscribers[i].active = false;
        center->m_subscribers[i].readIndex = 0;
    }
    
    return center;
}

// ========== 连接到事件中心 ==========

IPCEventCenter* IPCEventCenter::connect(const char* name) {
    if (name == nullptr || strlen(name) == 0) {
        return nullptr;
    }
    
    int fd = shm_open(name, O_RDWR, 0666);
    if (fd < 0) {
        return nullptr;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        ::close(fd);
        return nullptr;
    }
    size_t size = st.st_size;
    
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        return nullptr;
    }
    
    EventCenterHeader* header = static_cast<EventCenterHeader*>(ptr);
    if (header->magic != SHM_MAGIC) {
        munmap(ptr, size);
        ::close(fd);
        return nullptr;
    }
    
    // 计算环形缓冲区位置
    size_t headerSize = sizeof(EventCenterHeader);
    headerSize = (headerSize + 63) & ~63;
    size_t ringBufferObjSize = sizeof(ShmRingBuffer<Event>);
    ringBufferObjSize = (ringBufferObjSize + 63) & ~63;
    
    char* base = static_cast<char*>(ptr);
    ShmRingBuffer<Event>* ringBuffer = reinterpret_cast<ShmRingBuffer<Event>*>(base + headerSize);
    
    IPCEventCenter* center = new IPCEventCenter();
    center->m_name = name;
    center->m_shmPtr = ptr;
    center->m_shmSize = size;
    center->m_shmFd = fd;
    center->m_isCreator = false;
    center->m_header = header;
    center->m_ringBuffer = ringBuffer;
    center->m_ringBufferMemory = base + headerSize + ringBufferObjSize;
    center->m_nextSubscriberId = 0;
    
    // 验证环形缓冲区已初始化
    if (!ringBuffer->isInitialized()) {
        munmap(ptr, size);
        ::close(fd);
        delete center;
        return nullptr;
    }
    
    // 附加环形缓冲区，设置当前进程的共享内存基地址
    Result attachRet = ringBuffer->attach(ptr);
    if (attachRet != Result::OK) {
        munmap(ptr, size);
        ::close(fd);
        delete center;
        return nullptr;
    }
    
    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        center->m_subscribers[i].active = false;
        center->m_subscribers[i].readIndex = 0;
    }
    
    return center;
}

// ========== 销毁事件中心 ==========

void IPCEventCenter::destroy() {
    if (!isValid()) return;
    
    if (m_isCreator) {
        m_ringBuffer->destroy();
        m_header->lock.destroy();
        munmap(m_shmPtr, m_shmSize);
        ::close(m_shmFd);
        shm_unlink(m_name.c_str());
    } else {
        disconnect();
    }
    
    m_shmPtr = nullptr;
    m_header = nullptr;
    m_ringBuffer = nullptr;
}

// ========== 断开连接 ==========

void IPCEventCenter::disconnect() {
    if (!isValid()) return;
    
    munmap(m_shmPtr, m_shmSize);
    ::close(m_shmFd);
    
    m_shmPtr = nullptr;
    m_header = nullptr;
    m_ringBuffer = nullptr;
}

// ========== 发布事件 ==========

Result IPCEventCenter::publish(const Event& event) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    
    Result ret = m_ringBuffer->write(event);
    if (ret == Result::OK) {
        m_header->lock.writeLock();
        m_header->totalEvents++;
        m_header->lastEventTime = getCurrentTimestamp();
        m_header->lock.unlock();
    }
    
    return ret;
}

Result IPCEventCenter::publishBatch(const Event* events, uint32_t count, uint32_t& published) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    
    published = 0;
    for (uint32_t i = 0; i < count; i++) {
        Result ret = publish(events[i]);
        if (ret != Result::OK) {
            return ret;
        }
        published++;
    }
    
    return Result::OK;
}

Result IPCEventCenter::publishDataChange(uint64_t key, PointType type,
                                          uint32_t oldValue, uint32_t newValue,
                                          const char* source) {
    Event event;
    event.key = key;
    // 从key解析addr和id
    event.addr = (key >> 32) & 0xFFFF;
    event.id = key & 0xFFFFFFFF;
    event.pointType = type;
    event.oldValue.intValue = oldValue;
    event.newValue.intValue = newValue;
    event.timestamp = getCurrentTimestamp();
    event.quality = 0;
    event.isCritical = 0;
    event.sourcePid = getpid();
    
    if (source) {
        strncpy(event.source, source, sizeof(event.source) - 1);
        event.source[sizeof(event.source) - 1] = '\0';
    }
    
    return publish(event);
}

Result IPCEventCenter::publishDataChange(uint64_t key, PointType type,
                                          float oldValue, float newValue,
                                          const char* source) {
    Event event;
    event.key = key;
    // 从key解析addr和id
    event.addr = (key >> 32) & 0xFFFF;
    event.id = key & 0xFFFFFFFF;
    event.pointType = type;
    event.oldValue.floatValue = oldValue;
    event.newValue.floatValue = newValue;
    event.timestamp = getCurrentTimestamp();
    event.quality = 0;
    event.isCritical = 0;
    event.sourcePid = getpid();
    
    if (source) {
        strncpy(event.source, source, sizeof(event.source) - 1);
        event.source[sizeof(event.source) - 1] = '\0';
    }
    
    return publish(event);
}

// ========== 订阅事件 ==========

Result IPCEventCenter::subscribe(EventCallback callback, uint32_t& subscriberId) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (!callback) return Result::INVALID_PARAM;
    if (!m_ringBuffer || !m_ringBuffer->isInitialized()) return Result::NOT_INITIALIZED;
    
    // 查找空位
    for (uint32_t i = 0; i < MAX_PROCESS_COUNT; i++) {
        uint32_t id = (m_nextSubscriberId + i) % MAX_PROCESS_COUNT;
        if (!m_subscribers[id].active) {
            m_subscribers[id].active = true;
            m_subscribers[id].callback = callback;
            m_subscribers[id].readIndex = m_ringBuffer->getWriteIndex(); // 从当前位置开始
            
            m_header->lock.writeLock();
            m_header->subscriberCount++;
            m_header->lock.unlock();
            
            subscriberId = id;
            m_nextSubscriberId = (id + 1) % MAX_PROCESS_COUNT;
            return Result::OK;
        }
    }
    
    return Result::OUT_OF_MEMORY;
}

Result IPCEventCenter::unsubscribe(uint32_t subscriberId) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (subscriberId >= MAX_PROCESS_COUNT) return Result::INVALID_PARAM;
    
    if (m_subscribers[subscriberId].active) {
        m_subscribers[subscriberId].active = false;
        m_subscribers[subscriberId].callback = nullptr;
        
        m_header->lock.writeLock();
        m_header->subscriberCount--;
        m_header->lock.unlock();
        
        return Result::OK;
    }
    
    return Result::NOT_FOUND;
}

uint32_t IPCEventCenter::getSubscriberCount() const {
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (m_subscribers[i].active) {
            count++;
        }
    }
    return count;
}

// ========== 事件消费 ==========

Result IPCEventCenter::poll(uint32_t subscriberId, Event& event) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (subscriberId >= MAX_PROCESS_COUNT) return Result::INVALID_PARAM;
    if (!m_subscribers[subscriberId].active) return Result::NOT_FOUND;
    if (!m_ringBuffer) return Result::NOT_INITIALIZED;
    if (!m_ringBuffer->isInitialized()) return Result::NOT_INITIALIZED;
    
    // 验证 readIndex 在有效范围内
    uint32_t capacity = m_ringBuffer->getCapacity();
    uint32_t& readIdx = m_subscribers[subscriberId].readIndex;
    if (capacity == 0 || readIdx >= capacity) {
        // readIndex 无效，重置到当前位置
        readIdx = m_ringBuffer->getWriteIndex();
    }
    
    return m_ringBuffer->read(readIdx, event, false);
}

Result IPCEventCenter::wait(uint32_t subscriberId, Event& event, int timeoutMs) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (subscriberId >= MAX_PROCESS_COUNT) return Result::INVALID_PARAM;
    if (!m_subscribers[subscriberId].active) return Result::NOT_FOUND;
    
    // 先尝试直接读取
    Result ret = poll(subscriberId, event);
    if (ret == Result::OK) {
        return Result::OK;
    }
    
    // 等待新事件
    ret = m_ringBuffer->wait(timeoutMs);
    if (ret == Result::OK) {
        return poll(subscriberId, event);
    }
    
    return ret;
}

uint32_t IPCEventCenter::process(uint32_t subscriberId, uint32_t maxEvents) {
    if (!isValid()) return 0;
    if (subscriberId >= MAX_PROCESS_COUNT) return 0;
    if (!m_subscribers[subscriberId].active) return 0;
    
    uint32_t processed = 0;
    Event event;
    
    while (poll(subscriberId, event) == Result::OK) {
        if (m_subscribers[subscriberId].callback) {
            m_subscribers[subscriberId].callback(event);
        }
        processed++;
        
        if (maxEvents > 0 && processed >= maxEvents) {
            break;
        }
    }
    
    return processed;
}

// ========== 状态查询 ==========

uint32_t IPCEventCenter::getPendingEvents(uint32_t subscriberId) const {
    if (!isValid() || subscriberId >= MAX_PROCESS_COUNT) return 0;
    if (!m_subscribers[subscriberId].active) return 0;
    
    return m_ringBuffer->available(m_subscribers[subscriberId].readIndex);
}

uint32_t IPCEventCenter::getSubscriberReadIndex(uint32_t subscriberId) const {
    if (!isValid() || subscriberId >= MAX_PROCESS_COUNT) return 0;
    return m_subscribers[subscriberId].readIndex;
}

} // namespace IPC
