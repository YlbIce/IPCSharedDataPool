/**
 * @file tst_rwlock.cpp
 * @brief 跨进程读写锁测试
 */

#include "../include/ProcessRWLock.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <atomic>

using namespace IPC;

static int s_passed = 0;
static int s_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    try { \
        test_##name(); \
        std::cout << "PASSED" << std::endl; \
        s_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << std::endl; \
        s_failed++; \
    } catch (...) { \
        std::cout << "FAILED: unknown error" << std::endl; \
        s_failed++; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

// ========== 测试用例 ==========

TEST(init_destroy) {
    ProcessRWLock lock;
    ASSERT_TRUE(!lock.isInitialized());
    
    Result ret = lock.initialize();
    ASSERT_EQ(ret, Result::OK);
    ASSERT_TRUE(lock.isInitialized());
    
    ret = lock.destroy();
    ASSERT_EQ(ret, Result::OK);
    ASSERT_TRUE(!lock.isInitialized());
}

TEST(double_init) {
    ProcessRWLock lock;
    ASSERT_EQ(lock.initialize(), Result::OK);
    ASSERT_EQ(lock.initialize(), Result::ALREADY_EXISTS);
    lock.destroy();
}

TEST(read_lock) {
    ProcessRWLock lock;
    lock.initialize();
    
    Result ret = lock.readLock();
    ASSERT_EQ(ret, Result::OK);
    
    ret = lock.unlock();
    ASSERT_EQ(ret, Result::OK);
    
    lock.destroy();
}

TEST(write_lock) {
    ProcessRWLock lock;
    lock.initialize();
    
    Result ret = lock.writeLock();
    ASSERT_EQ(ret, Result::OK);
    
    ret = lock.unlock();
    ASSERT_EQ(ret, Result::OK);
    
    lock.destroy();
}

TEST(try_lock) {
    ProcessRWLock lock;
    lock.initialize();
    
    // 尝试获取读锁
    Result ret = lock.tryReadLock();
    ASSERT_EQ(ret, Result::OK);
    
    // 在持有读锁时尝试获取写锁应该失败
    ret = lock.tryWriteLock();
    ASSERT_EQ(ret, Result::TIMEOUT);
    
    ret = lock.unlock();
    ASSERT_EQ(ret, Result::OK);
    
    // 尝试获取写锁
    ret = lock.tryWriteLock();
    ASSERT_EQ(ret, Result::OK);
    
    // 在持有写锁时尝试获取读锁应该失败
    ret = lock.tryReadLock();
    ASSERT_EQ(ret, Result::TIMEOUT);
    
    lock.unlock();
    lock.destroy();
}

TEST(read_guard) {
    ProcessRWLock lock;
    lock.initialize();
    
    {
        ProcessRWLock::ReadGuard guard(lock);
        ASSERT_EQ(guard.result(), Result::OK);
        // 锁会在作用域结束时自动释放
    }
    
    // 应该能再次获取锁
    ASSERT_EQ(lock.tryWriteLock(), Result::OK);
    lock.unlock();
    lock.destroy();
}

TEST(write_guard) {
    ProcessRWLock lock;
    lock.initialize();
    
    {
        ProcessRWLock::WriteGuard guard(lock);
        ASSERT_EQ(guard.result(), Result::OK);
        // 锁会在作用域结束时自动释放
    }
    
    // 应该能再次获取锁
    ASSERT_EQ(lock.tryWriteLock(), Result::OK);
    lock.unlock();
    lock.destroy();
}

// ========== 并发测试 ==========

TEST(concurrent_read) {
    ProcessRWLock lock;
    lock.initialize();
    
    const int THREAD_COUNT = 8;
    const int OPERATIONS = 10000;
    std::atomic<int> counter(0);
    std::atomic<int> errors(0);
    
    std::vector<std::thread> threads;
    for (int t = 0; t < THREAD_COUNT; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < OPERATIONS; i++) {
                ProcessRWLock::ReadGuard guard(lock);
                if (guard.result() != Result::OK) {
                    errors++;
                }
                counter++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_EQ(errors.load(), 0);
    ASSERT_EQ(counter.load(), THREAD_COUNT * OPERATIONS);
    lock.destroy();
}

TEST(concurrent_write) {
    ProcessRWLock lock;
    lock.initialize();
    
    const int THREAD_COUNT = 4;
    const int OPERATIONS = 1000;
    std::atomic<int> counter(0);
    std::atomic<int> errors(0);
    
    std::vector<std::thread> threads;
    for (int t = 0; t < THREAD_COUNT; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < OPERATIONS; i++) {
                ProcessRWLock::WriteGuard guard(lock);
                if (guard.result() != Result::OK) {
                    errors++;
                }
                counter++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_EQ(errors.load(), 0);
    ASSERT_EQ(counter.load(), THREAD_COUNT * OPERATIONS);
    lock.destroy();
}

TEST(read_write_mixed) {
    ProcessRWLock lock;
    lock.initialize();
    
    const int READER_COUNT = 4;
    const int WRITER_COUNT = 2;
    const int OPERATIONS = 1000;
    
    std::atomic<int> readers(0);
    std::atomic<int> writers(0);
    std::atomic<int> maxConcurrentReaders(0);
    std::atomic<int> errors(0);
    
    std::vector<std::thread> threads;
    
    // 读线程
    for (int t = 0; t < READER_COUNT; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < OPERATIONS; i++) {
                ProcessRWLock::ReadGuard guard(lock);
                if (guard.result() != Result::OK) {
                    errors++;
                    continue;
                }
                readers++;
                int current = readers.load();
                int maxVal = maxConcurrentReaders.load();
                while (current > maxVal && !maxConcurrentReaders.compare_exchange_weak(maxVal, current));
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                readers--;
            }
        });
    }
    
    // 写线程
    for (int t = 0; t < WRITER_COUNT; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < OPERATIONS; i++) {
                ProcessRWLock::WriteGuard guard(lock);
                if (guard.result() != Result::OK) {
                    errors++;
                    continue;
                }
                writers++;
                // 写入时应该没有其他读者
                if (readers.load() > 0) {
                    errors++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                writers--;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_EQ(errors.load(), 0);
    // 应该有多个并发读者
    ASSERT_TRUE(maxConcurrentReaders.load() > 1);
    
    lock.destroy();
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  ProcessRWLock Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RUN_TEST(init_destroy);
    RUN_TEST(double_init);
    RUN_TEST(read_lock);
    RUN_TEST(write_lock);
    RUN_TEST(try_lock);
    RUN_TEST(read_guard);
    RUN_TEST(write_guard);
    RUN_TEST(concurrent_read);
    RUN_TEST(concurrent_write);
    RUN_TEST(read_write_mixed);
    
    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, " 
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;
    
    return s_failed > 0 ? 1 : 0;
}
