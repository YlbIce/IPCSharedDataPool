#ifndef PROCESS_RWLOCK_H
#define PROCESS_RWLOCK_H

#include "Common.h"
#include <pthread.h>
#include <cstdint>
#include <system_error>

namespace IPC {

/**
 * @brief 跨进程读写锁
 * 
 * 封装 pthread_rwlock，支持 PTHREAD_PROCESS_SHARED
 */
class ProcessRWLock {
public:
    /**
     * @brief 默认构造函数
     */
    ProcessRWLock() : m_initialized(false) {}
    
    /**
     * @brief 析构函数
     */
    ~ProcessRWLock() {
        if (m_initialized) {
            destroy();
        }
    }
    
    // 禁止拷贝
    ProcessRWLock(const ProcessRWLock&) = delete;
    ProcessRWLock& operator=(const ProcessRWLock&) = delete;
    
    /**
     * @brief 初始化锁
     * @return Result::OK 成功
     */
    Result initialize() {
        if (m_initialized) {
            return Result::ALREADY_EXISTS;
        }
        
        pthread_rwlockattr_t attr;
        int ret = pthread_rwlockattr_init(&attr);
        if (ret != 0) {
            return Result::ERROR;
        }
        
        // 设置为进程间共享
        ret = pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        if (ret != 0) {
            pthread_rwlockattr_destroy(&attr);
            return Result::ERROR;
        }
        
        ret = pthread_rwlock_init(&m_lock, &attr);
        pthread_rwlockattr_destroy(&attr);
        
        if (ret != 0) {
            return Result::ERROR;
        }
        
        m_initialized = true;
        return Result::OK;
    }
    
    /**
     * @brief 销毁锁
     * @return Result::OK 成功
     */
    Result destroy() {
        if (!m_initialized) {
            return Result::NOT_INITIALIZED;
        }
        
        int ret = pthread_rwlock_destroy(&m_lock);
        if (ret != 0) {
            return Result::ERROR;
        }
        
        m_initialized = false;
        return Result::OK;
    }
    
    /**
     * @brief 获取读锁（共享锁）
     * @return Result::OK 成功
     */
    Result readLock() {
        if (!m_initialized) {
            return Result::NOT_INITIALIZED;
        }
        
        int ret = pthread_rwlock_rdlock(&m_lock);
        return (ret == 0) ? Result::OK : Result::ERROR;
    }
    
    /**
     * @brief 尝试获取读锁
     * @return Result::OK 成功, Result::TIMEOUT 锁被占用
     */
    Result tryReadLock() {
        if (!m_initialized) {
            return Result::NOT_INITIALIZED;
        }
        
        int ret = pthread_rwlock_tryrdlock(&m_lock);
        if (ret == 0) {
            return Result::OK;
        } else if (ret == EBUSY) {
            return Result::TIMEOUT;
        }
        return Result::ERROR;
    }
    
    /**
     * @brief 获取写锁（独占锁）
     * @return Result::OK 成功
     */
    Result writeLock() {
        if (!m_initialized) {
            return Result::NOT_INITIALIZED;
        }
        
        int ret = pthread_rwlock_wrlock(&m_lock);
        return (ret == 0) ? Result::OK : Result::ERROR;
    }
    
    /**
     * @brief 尝试获取写锁
     * @return Result::OK 成功, Result::TIMEOUT 锁被占用
     */
    Result tryWriteLock() {
        if (!m_initialized) {
            return Result::NOT_INITIALIZED;
        }
        
        int ret = pthread_rwlock_trywrlock(&m_lock);
        if (ret == 0) {
            return Result::OK;
        } else if (ret == EBUSY) {
            return Result::TIMEOUT;
        }
        return Result::ERROR;
    }
    
    /**
     * @brief 带超时的获取读锁
     * @param timeoutMs 超时时间（毫秒）
     * @return Result::OK 成功, Result::TIMEOUT 超时
     */
    Result readLockTimeout(uint32_t timeoutMs) {
        if (!m_initialized) {
            return Result::NOT_INITIALIZED;
        }
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeoutMs / 1000;
        ts.tv_nsec += (timeoutMs % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        
        int ret = pthread_rwlock_timedrdlock(&m_lock, &ts);
        if (ret == 0) {
            return Result::OK;
        } else if (ret == ETIMEDOUT) {
            return Result::TIMEOUT;
        }
        return Result::ERROR;
    }
    
    /**
     * @brief 带超时的获取写锁
     * @param timeoutMs 超时时间（毫秒）
     * @return Result::OK 成功, Result::TIMEOUT 超时
     */
    Result writeLockTimeout(uint32_t timeoutMs) {
        if (!m_initialized) {
            return Result::NOT_INITIALIZED;
        }
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeoutMs / 1000;
        ts.tv_nsec += (timeoutMs % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        
        int ret = pthread_rwlock_timedwrlock(&m_lock, &ts);
        if (ret == 0) {
            return Result::OK;
        } else if (ret == ETIMEDOUT) {
            return Result::TIMEOUT;
        }
        return Result::ERROR;
    }
    
    /**
     * @brief 带超时的写锁守卫
     */
    class WriteGuardTimeout {
    public:
        explicit WriteGuardTimeout(ProcessRWLock& lock, uint32_t timeoutMs) 
            : m_lock(lock), m_result(lock.writeLockTimeout(timeoutMs)) {}
        
        ~WriteGuardTimeout() {
            if (m_result == Result::OK) {
                m_lock.unlock();
            }
        }
        
        Result result() const { return m_result; }
        
    private:
        ProcessRWLock& m_lock;
        Result m_result;
    };
    
    /**
     * @brief 释放锁
     * @return Result::OK 成功
     */
    Result unlock() {
        if (!m_initialized) {
            return Result::NOT_INITIALIZED;
        }
        
        int ret = pthread_rwlock_unlock(&m_lock);
        return (ret == 0) ? Result::OK : Result::ERROR;
    }
    
    /**
     * @brief 获取底层 pthread_rwlock 指针
     */
    pthread_rwlock_t* nativeHandle() {
        return &m_lock;
    }
    
    /**
     * @brief 获取初始化状态
     */
    bool isInitialized() const {
        return m_initialized;
    }
    
    // ========== RAII 包装类 ==========
    
    /**
     * @brief 读锁守卫
     */
    class ReadGuard {
    public:
        explicit ReadGuard(ProcessRWLock& lock) : m_lock(lock) {
            m_result = m_lock.readLock();
        }
        
        ~ReadGuard() {
            if (m_result == Result::OK) {
                m_lock.unlock();
            }
        }
        
        Result result() const { return m_result; }
        
    private:
        ProcessRWLock& m_lock;
        Result m_result;
    };
    
    /**
     * @brief 写锁守卫
     */
    class WriteGuard {
    public:
        explicit WriteGuard(ProcessRWLock& lock) : m_lock(lock) {
            m_result = m_lock.writeLock();
        }
        
        ~WriteGuard() {
            if (m_result == Result::OK) {
                m_lock.unlock();
            }
        }
        
        Result result() const { return m_result; }
        
    private:
        ProcessRWLock& m_lock;
        Result m_result;
    };

private:
    pthread_rwlock_t m_lock;
    bool m_initialized;
};

} // namespace IPC

#endif // PROCESS_RWLOCK_H
