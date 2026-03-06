/**
 * @file tst_ringbuffer.cpp
 * @brief 环形缓冲区测试
 */

#include "../include/ShmRingBuffer.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

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
    ShmRingBuffer<Event> buffer;
    ASSERT_TRUE(!buffer.isInitialized());
    
    char memory[1024 * 64];
    
    Result ret = buffer.initialize(memory, 1000);
    ASSERT_EQ(ret, Result::OK);
    ASSERT_TRUE(buffer.isInitialized());
    ASSERT_EQ(buffer.getCapacity(), 1000u);
    
    ret = buffer.destroy();
    ASSERT_EQ(ret, Result::OK);
    ASSERT_TRUE(!buffer.isInitialized());
}

TEST(single_write_read) {
    ShmRingBuffer<Event> buffer;
    char memory[1024 * 64];
    buffer.initialize(memory, 100);
    
    Event event;
    event.key = makeKey(10000, 1);
    event.addr = 10000;
    event.id = 1;
    event.pointType = PointType::YC;
    event.newValue.floatValue = 23.5f;
    event.timestamp = getCurrentTimestamp();
    
    Result ret = buffer.write(event);
    ASSERT_EQ(ret, Result::OK);
    ASSERT_EQ(buffer.available(0), 1u);
    
    uint32_t readIdx = 0;
    Event readEvent;
    ret = buffer.read(readIdx, readEvent);
    ASSERT_EQ(ret, Result::OK);
    ASSERT_EQ(readEvent.key, event.key);
    ASSERT_EQ(readEvent.newValue.floatValue, event.newValue.floatValue);
    ASSERT_EQ(readIdx, 1u);
    
    ret = buffer.read(readIdx, readEvent);
    ASSERT_EQ(ret, Result::NOT_FOUND);
    
    buffer.destroy();
}

TEST(buffer_full) {
    ShmRingBuffer<Event> buffer;
    char memory[1024 * 64];
    buffer.initialize(memory, 10);
    
    Event event;
    
    // 容量 10，实际可用 9
    for (int i = 0; i < 9; i++) {
        event.key = i;
        Result ret = buffer.write(event);
        ASSERT_EQ(ret, Result::OK);
    }
    
    // 第 10 个应该失败
    Result ret = buffer.write(event);
    ASSERT_EQ(ret, Result::BUFFER_FULL);
    
    buffer.destroy();
}

TEST(circular_wrap) {
    ShmRingBuffer<Event> buffer;
    char memory[1024 * 64];
    buffer.initialize(memory, 10);
    
    Event event;
    Event readEvent;
    uint32_t readIdx = 0;
    
    for (int round = 0; round < 3; round++) {
        // 写入 9 个
        for (int i = 0; i < 9; i++) {
            event.key = round * 100 + i;
            event.newValue.floatValue = static_cast<float>(i);
            Result ret = buffer.write(event);
            if (ret != Result::OK) {
                char msg[128];
                snprintf(msg, sizeof(msg), "write failed at round=%d, i=%d, ret=%d", round, i, (int)ret);
                throw std::runtime_error(msg);
            }
        }
        
        // 读取 9 个
        for (int i = 0; i < 9; i++) {
            Result ret = buffer.read(readIdx, readEvent);
            if (ret != Result::OK) {
                char msg[128];
                snprintf(msg, sizeof(msg), "read failed at round=%d, i=%d, ret=%d, readIdx=%u", 
                         round, i, (int)ret, readIdx);
                throw std::runtime_error(msg);
            }
            if (readEvent.key != static_cast<uint64_t>(round * 100 + i)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "data mismatch at round=%d, i=%d, expected=%llu, got=%llu", 
                         round, i, (unsigned long long)(round * 100 + i), (unsigned long long)readEvent.key);
                throw std::runtime_error(msg);
            }
        }
    }
    
    buffer.destroy();
}

TEST(batch_operations) {
    ShmRingBuffer<Event> buffer;
    char memory[1024 * 64];
    buffer.initialize(memory, 100);
    
    Event events[10];
    for (int i = 0; i < 10; i++) {
        events[i].key = i;
        events[i].newValue.floatValue = static_cast<float>(i * 10);
    }
    
    uint32_t written = 0;
    Result ret = buffer.writeBatch(events, 10, written);
    ASSERT_EQ(ret, Result::OK);
    ASSERT_EQ(written, 10u);
    
    uint32_t readIdx = 0;
    Event readEvent;
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(buffer.read(readIdx, readEvent), Result::OK);
        ASSERT_EQ(readEvent.key, static_cast<uint64_t>(i));
    }
    
    buffer.destroy();
}

TEST(wait_timeout) {
    ShmRingBuffer<Event> buffer;
    char memory[1024 * 64];
    buffer.initialize(memory, 10);
    
    auto start = std::chrono::steady_clock::now();
    Result ret = buffer.wait(100);
    auto end = std::chrono::steady_clock::now();
    
    ASSERT_EQ(ret, Result::TIMEOUT);
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    ASSERT_TRUE(elapsed >= 100 && elapsed < 200);
    
    buffer.destroy();
}

// ========== 并发测试 ==========

TEST(multi_producer) {
    ShmRingBuffer<Event> buffer;
    char memory[1024 * 1024];
    buffer.initialize(memory, 10000);
    
    const int PRODUCER_COUNT = 4;
    const int EVENTS_PER_PRODUCER = 2000;  // 总共 8000，小于 9999
    
    std::atomic<int> writeErrors(0);
    std::atomic<int> totalWritten(0);
    
    std::vector<std::thread> producers;
    for (int p = 0; p < PRODUCER_COUNT; p++) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < EVENTS_PER_PRODUCER; i++) {
                Event event;
                event.key = p * 10000 + i;
                event.newValue.intValue = static_cast<int32_t>(p * 10000 + i);
                
                int retries = 0;
                while (retries < 100) {
                    Result ret = buffer.write(event);
                    if (ret == Result::OK) {
                        totalWritten++;
                        break;
                    } else if (ret == Result::BUFFER_FULL) {
                        std::this_thread::yield();
                        retries++;
                    } else {
                        writeErrors++;
                        break;
                    }
                }
                if (retries >= 100) {
                    writeErrors++;
                }
            }
        });
    }
    
    for (auto& t : producers) {
        t.join();
    }
    
    ASSERT_EQ(writeErrors.load(), 0);
    
    uint32_t readIdx = 0;
    int readCount = 0;
    Event event;
    while (buffer.read(readIdx, event) == Result::OK) {
        readCount++;
    }
    
    ASSERT_EQ(readCount, totalWritten.load());
    ASSERT_EQ(readCount, PRODUCER_COUNT * EVENTS_PER_PRODUCER);
    
    buffer.destroy();
}

TEST(multi_consumer) {
    ShmRingBuffer<Event> buffer;
    char memory[1024 * 1024];
    buffer.initialize(memory, 10000);
    
    const int EVENT_COUNT = 5000;
    const int CONSUMER_COUNT = 4;
    
    std::atomic<int> consumed[CONSUMER_COUNT];
    for (int i = 0; i < CONSUMER_COUNT; i++) {
        consumed[i] = 0;
    }
    
    // 预先写入数据
    for (int i = 0; i < EVENT_COUNT; i++) {
        Event event;
        event.key = i;
        while (buffer.write(event) != Result::OK) {
            std::this_thread::yield();
        }
    }
    
    // 多个消费者各自读取（不更新全局位置）
    std::vector<std::thread> consumers;
    for (int c = 0; c < CONSUMER_COUNT; c++) {
        consumers.emplace_back([&, c]() {
            uint32_t readIdx = 0;
            Event event;
            while (true) {
                Result ret = buffer.read(readIdx, event, false);
                if (ret == Result::OK) {
                    consumed[c]++;
                } else {
                    break;
                }
            }
        });
    }
    
    for (auto& t : consumers) {
        t.join();
    }
    
    for (int c = 0; c < CONSUMER_COUNT; c++) {
        ASSERT_EQ(consumed[c], EVENT_COUNT);
    }
    
    buffer.destroy();
}

TEST(producer_consumer) {
    // 使用更大的缓冲区减少竞争
    ShmRingBuffer<Event> buffer;
    char memory[1024 * 1024];  // 1MB
    buffer.initialize(memory, 10000);
    
    const int TOTAL_EVENTS = 5000;  // 减少事件数量
    std::atomic<bool> done(false);
    std::atomic<int> produced(0);
    std::atomic<int> consumed(0);
    std::atomic<int> errors(0);
    
    // 生产者线程
    std::thread producer([&]() {
        for (int i = 0; i < TOTAL_EVENTS; i++) {
            Event event;
            event.key = i;
            event.newValue.intValue = i * 2;
            
            while (buffer.write(event) != Result::OK) {
                std::this_thread::yield();
            }
            produced++;
        }
        done = true;
    });
    
    // 消费者线程
    std::thread consumer([&]() {
        uint32_t readIdx = 0;
        Event event;
        
        while (!done || buffer.available(readIdx) > 0) {
            Result ret = buffer.read(readIdx, event);
            if (ret == Result::OK) {
                int32_t expected = static_cast<int32_t>(event.key * 2);
                if (event.newValue.intValue != expected) {
                    errors++;
                }
                consumed++;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    ASSERT_EQ(errors.load(), 0);
    ASSERT_EQ(produced.load(), TOTAL_EVENTS);
    ASSERT_EQ(consumed.load(), TOTAL_EVENTS);
    
    buffer.destroy();
}

// ========== 主函数 ==========

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "  ShmRingBuffer Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    RUN_TEST(init_destroy);
    RUN_TEST(single_write_read);
    RUN_TEST(buffer_full);
    RUN_TEST(circular_wrap);
    RUN_TEST(batch_operations);
    RUN_TEST(wait_timeout);
    RUN_TEST(multi_producer);
    RUN_TEST(multi_consumer);
    RUN_TEST(producer_consumer);
    
    std::cout << "====================================" << std::endl;
    std::cout << "  Results: " << s_passed << " passed, " 
              << s_failed << " failed" << std::endl;
    std::cout << "====================================" << std::endl;
    
    return s_failed > 0 ? 1 : 0;
}
