#ifndef SHM_RING_BUFFER_H
#define SHM_RING_BUFFER_H

#include "Common.h"
#include <semaphore.h>
#include <cstring>
#include <errno.h>
#include <thread>
#include <cstddef>

namespace IPC {

/**
 * @brief 共享内存环形缓冲区
 * 
 * 设计说明：
 * - 单生产者-单消费者模型（单个写入者，多个读取者各自维护读取位置）
 * - 写入端：使用 CAS 保证多生产者安全
 * - 读取端：每个消费者维护自己的读取位置
 * - 容量：capacity - 1 可用（一个槽位用于区分满/空）
 * 
 * 注意：
 * - 这是模板类，所有实现必须在头文件中
 * - 数据区使用相对偏移量存储，确保跨进程地址独立性
 */
template<typename T>
class ShmRingBuffer {
public:
    ShmRingBuffer() {
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_readIndex.store(0, std::memory_order_relaxed);
        m_capacity.store(0, std::memory_order_relaxed);
        m_dataOffset.store(0, std::memory_order_relaxed);
        m_shmBase.store(nullptr, std::memory_order_relaxed);
        m_initialized.store(false, std::memory_order_relaxed);
    }
    
    ~ShmRingBuffer() {
        if (m_initialized.load(std::memory_order_acquire)) {
            destroy();
        }
    }
    
    ShmRingBuffer(const ShmRingBuffer&) = delete;
    ShmRingBuffer& operator=(const ShmRingBuffer&) = delete;
    
    /**
     * @brief 初始化（创建者调用）
     * @param dataPtr 数据区指针
     * @param capacity 容量（实际可用 capacity - 1）
     * @param shmBase 共享内存基地址（用于计算相对偏移）
     */
    Result initialize(void* dataPtr, uint32_t capacity, void* shmBase = nullptr) {
        bool expected = false;
        if (!m_initialized.compare_exchange_strong(expected, true,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return Result::ALREADY_EXISTS;
        }
        
        if (dataPtr == nullptr || capacity < 2) {
            m_initialized.store(false, std::memory_order_release);
            return Result::INVALID_PARAM;
        }
        
        m_capacity.store(capacity, std::memory_order_release);
        
        // 计算并存储相对偏移量
        if (shmBase != nullptr) {
            m_dataOffset.store(static_cast<uint64_t>(
                reinterpret_cast<char*>(dataPtr) - reinterpret_cast<char*>(shmBase)),
                std::memory_order_release);
            m_shmBase.store(shmBase, std::memory_order_release);
        } else {
            // 如果没有提供基地址，存储绝对地址（仅单进程使用）
            m_dataOffset.store(reinterpret_cast<uint64_t>(dataPtr), std::memory_order_release);
            m_shmBase.store(nullptr, std::memory_order_release);
        }
        
        m_writeIndex.store(0, std::memory_order_relaxed);
        m_readIndex.store(0, std::memory_order_relaxed);
        
        if (sem_init(&m_sem, 1, 0) != 0) {
            m_initialized.store(false, std::memory_order_release);
            return Result::ERROR;
        }
        
        // 初始化数据区
        T* dataPtrLocal = getDataPtr();
        for (uint32_t i = 0; i < capacity; i++) {
            new (&dataPtrLocal[i]) T();
        }
        
        // 内存屏障确保所有初始化在 m_initialized = true 之前完成
        std::atomic_thread_fence(std::memory_order_release);
        
        return Result::OK;
    }
    
    /**
     * @brief 附加到已初始化的缓冲区（客户端调用）
     * @param shmBase 当前进程的共享内存基地址
     */
    Result attach(void* shmBase) {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return Result::NOT_INITIALIZED;
        }
        
        if (shmBase == nullptr) {
            return Result::INVALID_PARAM;
        }
        
        m_shmBase.store(shmBase, std::memory_order_release);
        return Result::OK;
    }
    
    Result destroy() {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return Result::NOT_INITIALIZED;
        }
        
        sem_destroy(&m_sem);
        m_initialized.store(false, std::memory_order_release);
        m_capacity.store(0, std::memory_order_release);
        m_dataOffset.store(0, std::memory_order_release);
        m_shmBase.store(nullptr, std::memory_order_release);
        return Result::OK;
    }
    
    /**
     * @brief 获取数据指针（根据当前进程的共享内存基地址计算）
     */
    T* getDataPtr() const {
        uint64_t offset = m_dataOffset.load(std::memory_order_acquire);
        void* base = m_shmBase.load(std::memory_order_acquire);
        
        if (base != nullptr) {
            // 使用相对偏移量计算绝对地址
            return reinterpret_cast<T*>(reinterpret_cast<char*>(base) + offset);
        } else {
            // 没有基地址时，offset 就是绝对地址（仅单进程使用）
            return reinterpret_cast<T*>(offset);
        }
    }
    
    /**
     * @brief 写入事件（多生产者安全）
     */
    Result write(const T& event) {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return Result::NOT_INITIALIZED;
        }
        
        uint32_t capacityLocal = m_capacity.load(std::memory_order_acquire);
        T* dataLocal = getDataPtr();
        
        if (dataLocal == nullptr) {
            return Result::NOT_INITIALIZED;
        }
        
        // 使用 CAS 原子获取写入位置
        uint32_t writeIdx, nextWriteIdx;
        
        do {
            writeIdx = m_writeIndex.load(std::memory_order_relaxed);
            nextWriteIdx = (writeIdx + 1) % capacityLocal;
            
            // 检查是否满
            uint32_t readIdx = m_readIndex.load(std::memory_order_acquire);
            if (nextWriteIdx == readIdx) {
                return Result::BUFFER_FULL;
            }
            
            // CAS 尝试获取写入位置
        } while (!m_writeIndex.compare_exchange_weak(
            writeIdx, nextWriteIdx,
            std::memory_order_acq_rel,
            std::memory_order_relaxed));
        
        // 写入数据
        dataLocal[writeIdx] = event;
        
        // 通知消费者
        sem_post(&m_sem);
        
        return Result::OK;
    }
    
    Result writeBatch(const T* events, uint32_t count, uint32_t& written) {
        written = 0;
        for (uint32_t i = 0; i < count; i++) {
            Result ret = write(events[i]);
            if (ret != Result::OK) {
                return ret;
            }
            written++;
        }
        return Result::OK;
    }
    
    /**
     * @brief 读取事件（消费者）
     * @param readIdx 读取位置（每个消费者维护自己的位置）
     * @param event 输出事件
     * @param updateGlobal 是否更新全局读取位置
     */
    Result read(uint32_t& readIdx, T& event, bool updateGlobal = true) {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return Result::NOT_INITIALIZED;
        }
        
        uint32_t capacityLocal = m_capacity.load(std::memory_order_acquire);
        T* dataLocal = getDataPtr();
        
        if (capacityLocal == 0 || dataLocal == nullptr) {
            return Result::NOT_INITIALIZED;
        }
        
        uint32_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
        
        // 检查是否有新数据
        if (readIdx == writeIdx) {
            return Result::NOT_FOUND;
        }
        
        // 读取数据
        event = dataLocal[readIdx];
        
        // 更新读取位置
        readIdx = (readIdx + 1) % capacityLocal;
        
        // 更新全局读取位置（用于满判断）
        if (updateGlobal) {
            // 使用 CAS 确保只前进
            uint32_t oldReadIdx;
            do {
                oldReadIdx = m_readIndex.load(std::memory_order_relaxed);
                // 计算距离，确保新位置比旧位置靠后（环形考虑）
                uint32_t oldDist = (writeIdx + capacityLocal - oldReadIdx) % capacityLocal;
                uint32_t newDist = (writeIdx + capacityLocal - readIdx) % capacityLocal;
                if (newDist >= oldDist) break;  // 新位置不比旧位置新
            } while (!m_readIndex.compare_exchange_weak(
                oldReadIdx, readIdx,
                std::memory_order_release,
                std::memory_order_relaxed));
        }
        
        return Result::OK;
    }
    
    /**
     * @brief 等待新事件
     */
    Result wait(int timeoutMs = -1) {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return Result::NOT_INITIALIZED;
        }
        
        if (timeoutMs < 0) {
            while (sem_wait(&m_sem) != 0) {
                if (errno == EINTR) continue;
                return Result::ERROR;
            }
            return Result::OK;
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeoutMs / 1000;
            ts.tv_nsec += (timeoutMs % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            
            while (sem_timedwait(&m_sem, &ts) != 0) {
                if (errno == EINTR) continue;
                if (errno == ETIMEDOUT) return Result::TIMEOUT;
                return Result::ERROR;
            }
            return Result::OK;
        }
    }
    
    Result tryWait() {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return Result::NOT_INITIALIZED;
        }
        
        if (sem_trywait(&m_sem) == 0) {
            return Result::OK;
        }
        if (errno == EAGAIN) {
            return Result::NOT_FOUND;
        }
        return Result::ERROR;
    }
    
    /**
     * @brief 获取可读数量
     */
    uint32_t available(uint32_t readIdx) const {
        uint32_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
        uint32_t capacityLocal = m_capacity.load(std::memory_order_acquire);
        if (writeIdx >= readIdx) {
            return writeIdx - readIdx;
        } else {
            return capacityLocal - readIdx + writeIdx;
        }
    }
    
    uint32_t getWriteIndex() const {
        return m_writeIndex.load(std::memory_order_acquire);
    }
    
    uint32_t getReadIndex() const {
        return m_readIndex.load(std::memory_order_acquire);
    }
    
    void setReadIndex(uint32_t idx) {
        m_readIndex.store(idx, std::memory_order_release);
    }
    
    uint32_t getCapacity() const { 
        return m_capacity.load(std::memory_order_acquire); 
    }
    
    bool isInitialized() const { 
        return m_initialized.load(std::memory_order_acquire); 
    }
    
    bool isEmpty(uint32_t readIdx) const {
        return readIdx == m_writeIndex.load(std::memory_order_acquire);
    }
    
    bool isFull() const {
        uint32_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
        uint32_t readIdx = m_readIndex.load(std::memory_order_acquire);
        uint32_t capacityLocal = m_capacity.load(std::memory_order_acquire);
        uint32_t nextWriteIdx = (writeIdx + 1) % capacityLocal;
        return nextWriteIdx == readIdx;
    }

private:
    alignas(64) std::atomic<uint32_t> m_writeIndex{0};
    alignas(64) std::atomic<uint32_t> m_readIndex{0};
    alignas(64) std::atomic<uint32_t> m_capacity{0};
    std::atomic<uint64_t> m_dataOffset{0};       // 数据区相对于共享内存基地址的偏移量
    std::atomic<void*> m_shmBase{nullptr};       // 当前进程的共享内存基地址（进程本地）
    sem_t m_sem;
    std::atomic<bool> m_initialized{false};
};

} // namespace IPC

#endif // SHM_RING_BUFFER_H
