/**
 * @file SharedDataPool.cpp
 * @brief 共享数据池实现
 */

#include "../include/SharedDataPool.h"
#include <cstring>
#include <cmath>
#include <vector>

namespace IPC {

// ========== 创建共享数据池 ==========

SharedDataPool* SharedDataPool::create(const char* name,
                                        uint32_t yxCount,
                                        uint32_t ycCount,
                                        uint32_t dzCount,
                                        uint32_t ykCount) {
    if (name == nullptr || strlen(name) == 0) {
        return nullptr;
    }
    
    // 辅助函数：计算对齐后的区域大小
    auto alignUp = [](size_t size, size_t align) -> size_t {
        return (size + align - 1) & ~(align - 1);
    };
    
    // 计算每种数据区域的大小（带内部对齐）
    auto calcYXDataSize = [&](uint32_t count) -> size_t {
        if (count == 0) return 0;
        size_t valueArea = count * YXData::VALUE_SIZE;
        size_t timestampOffset = alignUp(valueArea, YXData::TIMESTAMP_SIZE);
        size_t timestampArea = count * YXData::TIMESTAMP_SIZE;
        size_t qualityArea = count * YXData::QUALITY_SIZE;
        return timestampOffset + timestampArea + qualityArea;
    };
    
    auto calcYCDataSize = [&](uint32_t count) -> size_t {
        if (count == 0) return 0;
        size_t valueArea = count * YCData::VALUE_SIZE;
        size_t timestampOffset = alignUp(valueArea, YCData::TIMESTAMP_SIZE);
        size_t timestampArea = count * YCData::TIMESTAMP_SIZE;
        size_t qualityArea = count * YCData::QUALITY_SIZE;
        return timestampOffset + timestampArea + qualityArea;
    };
    
    // 计算所需内存大小
    size_t requiredSize = sizeof(ShmHeader);
    
    // YX 数据区
    requiredSize = alignUp(requiredSize, 8);
    size_t yxDataOffset = requiredSize;
    requiredSize += calcYXDataSize(yxCount);
    
    // YC 数据区
    requiredSize = alignUp(requiredSize, 8);
    size_t ycDataOffset = requiredSize;
    requiredSize += calcYCDataSize(ycCount);
    
    // DZ 数据区
    requiredSize = alignUp(requiredSize, 8);
    size_t dzDataOffset = requiredSize;
    requiredSize += calcYCDataSize(dzCount);
    
    // YK 数据区
    requiredSize = alignUp(requiredSize, 8);
    size_t ykDataOffset = requiredSize;
    requiredSize += calcYXDataSize(ykCount);
    
    // 索引区（哈希表 + 索引条目）
    uint32_t totalCount = yxCount + ycCount + dzCount + ykCount;
    uint32_t hashSize = 1;
    while (hashSize < totalCount * 2) {
        hashSize *= 2;
    }
    requiredSize = alignUp(requiredSize, 8);
    size_t indexOffset = requiredSize;
    requiredSize += hashSize * sizeof(uint32_t); // 哈希表
    requiredSize += (totalCount > 0 ? totalCount : 1) * sizeof(IndexEntry); // 索引条目
    
    // 进程信息区
    requiredSize = alignUp(requiredSize, 8);
    size_t processInfoOffset = requiredSize;
    requiredSize += MAX_PROCESS_COUNT * sizeof(ProcessInfo);
    
    // 最终对齐
    requiredSize = alignUp(requiredSize, 8);
    
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
    
    // 设置大小
    if (ftruncate(fd, requiredSize) < 0) {
        ::close(fd);
        shm_unlink(name);
        return nullptr;
    }
    
    // 映射内存
    void* ptr = mmap(nullptr, requiredSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        shm_unlink(name);
        return nullptr;
    }
    
    // 清零
    memset(ptr, 0, requiredSize);
    
    // 初始化头部
    ShmHeader* header = static_cast<ShmHeader*>(ptr);
    header->magic = SHM_MAGIC;
    header->version = SHM_VERSION;
    header->yxCount = yxCount;
    header->ycCount = ycCount;
    header->dzCount = dzCount;
    header->ykCount = ykCount;
    header->indexCount = 0;
    header->processCount = 0;
    header->createTime = getCurrentTimestamp();
    header->lastUpdateTime = header->createTime;
    
    // 保存哈希表大小
    header->hashSize = hashSize;
    header->reserved = 0;
    
    // 设置偏移量
    header->yxDataOffset = yxDataOffset;
    header->ycDataOffset = ycDataOffset;
    header->dzDataOffset = dzDataOffset;
    header->ykDataOffset = ykDataOffset;
    header->indexOffset = indexOffset;
    header->processInfoOffset = processInfoOffset;
    
    // 初始化锁
    header->lock.initialize();
    
    // 创建对象
    SharedDataPool* pool = new SharedDataPool();
    pool->m_name = name;
    pool->m_shmPtr = ptr;
    pool->m_shmSize = requiredSize;
    pool->m_shmFd = fd;
    pool->m_isCreator = true;
    pool->m_header = header;
    
    // 初始化数据指针
    if (!pool->initFromShm()) {
        delete pool;
        munmap(ptr, requiredSize);
        ::close(fd);
        shm_unlink(name);
        return nullptr;
    }
    
    return pool;
}

// ========== 连接到共享数据池 ==========

SharedDataPool* SharedDataPool::connect(const char* name) {
    if (name == nullptr || strlen(name) == 0) {
        return nullptr;
    }
    
    // 打开共享内存
    int fd = shm_open(name, O_RDWR, 0666);
    if (fd < 0) {
        return nullptr;
    }
    
    // 获取大小
    struct stat st;
    if (fstat(fd, &st) < 0) {
        ::close(fd);
        return nullptr;
    }
    size_t size = st.st_size;
    
    // 映射内存
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        return nullptr;
    }
    
    // 验证头部
    ShmHeader* header = static_cast<ShmHeader*>(ptr);
    if (header->magic != SHM_MAGIC) {
        munmap(ptr, size);
        ::close(fd);
        return nullptr;
    }
    
    // 创建对象
    SharedDataPool* pool = new SharedDataPool();
    pool->m_name = name;
    pool->m_shmPtr = ptr;
    pool->m_shmSize = size;
    pool->m_shmFd = fd;
    pool->m_isCreator = false;
    pool->m_header = header;
    
    // 初始化数据指针
    pool->initFromShm();
    
    return pool;
}

// ========== 销毁共享数据池 ==========

void SharedDataPool::destroy() {
    if (!isValid()) return;
    
    if (m_isCreator) {
        // 销毁锁
        m_header->lock.destroy();
        // 取消映射
        munmap(m_shmPtr, m_shmSize);
        // 关闭文件描述符
        ::close(m_shmFd);
        // 删除共享内存
        shm_unlink(m_name.c_str());
    } else {
        disconnect();
    }
    
    m_shmPtr = nullptr;
    m_header = nullptr;
    m_shmFd = -1;
}

// ========== 断开连接 ==========

void SharedDataPool::disconnect() {
    if (!isValid()) return;
    
    munmap(m_shmPtr, m_shmSize);
    ::close(m_shmFd);
    
    m_shmPtr = nullptr;
    m_header = nullptr;
    m_shmFd = -1;
}

// ========== 初始化数据指针 ==========

bool SharedDataPool::initFromShm() {
    // 基础指针检查
    if (m_header == nullptr || m_shmPtr == nullptr) return false;
    
    // 魔数验证
    if (m_header->magic != SHM_MAGIC) return false;
    
    char* base = static_cast<char*>(m_shmPtr);
    char* end = base + m_shmSize;  // 用于边界检查
    
    // 辅助函数：计算对齐后的区域大小
    auto alignUp = [](size_t size, size_t align) -> size_t {
        return (size + align - 1) & ~(align - 1);
    };
    
    // ========== YX 数据指针 ==========
    // 布局：values[1字节] + padding + timestamps[8字节] + qualities[1字节]
    // 紧凑布局，但确保timestamps 8字节对齐
    if (m_header->yxCount > 0) {
        char* yxBase = base + m_header->yxDataOffset;
        
        // 计算带对齐的偏移
        size_t valueAreaSize = m_header->yxCount * YXData::VALUE_SIZE;
        size_t timestampOffset = alignUp(valueAreaSize, YXData::TIMESTAMP_SIZE);  // 对齐到8字节
        size_t timestampAreaSize = m_header->yxCount * YXData::TIMESTAMP_SIZE;
        size_t qualityOffset = timestampOffset + timestampAreaSize;  // quality不需要特殊对齐
        size_t qualityAreaSize = m_header->yxCount * YXData::QUALITY_SIZE;
        size_t totalNeeded = qualityOffset + qualityAreaSize;
        
        // 边界检查
        if (yxBase + totalNeeded > end) return false;
        
        m_yxData.values = reinterpret_cast<uint8_t*>(yxBase);
        m_yxData.timestamps = reinterpret_cast<uint64_t*>(yxBase + timestampOffset);
        m_yxData.qualities = reinterpret_cast<uint8_t*>(yxBase + qualityOffset);
    } else {
        m_yxData.values = nullptr;
        m_yxData.timestamps = nullptr;
        m_yxData.qualities = nullptr;
    }
    
    // ========== YC 数据指针 ==========
    // 布局：values[4字节] + timestamps[8字节] + qualities[1字节]
    // timestamps需要对齐到8字节
    if (m_header->ycCount > 0) {
        char* ycBase = base + m_header->ycDataOffset;
        
        size_t valueAreaSize = m_header->ycCount * YCData::VALUE_SIZE;
        size_t timestampOffset = alignUp(valueAreaSize, YCData::TIMESTAMP_SIZE);  // 对齐到8字节
        size_t timestampAreaSize = m_header->ycCount * YCData::TIMESTAMP_SIZE;
        size_t qualityOffset = timestampOffset + timestampAreaSize;
        size_t qualityAreaSize = m_header->ycCount * YCData::QUALITY_SIZE;
        size_t totalNeeded = qualityOffset + qualityAreaSize;
        
        if (ycBase + totalNeeded > end) return false;
        
        m_ycData.values = reinterpret_cast<float*>(ycBase);
        m_ycData.timestamps = reinterpret_cast<uint64_t*>(ycBase + timestampOffset);
        m_ycData.qualities = reinterpret_cast<uint8_t*>(ycBase + qualityOffset);
    } else {
        m_ycData.values = nullptr;
        m_ycData.timestamps = nullptr;
        m_ycData.qualities = nullptr;
    }
    
    // ========== DZ 数据指针（与 YC 结构相同）==========
    if (m_header->dzCount > 0) {
        char* dzBase = base + m_header->dzDataOffset;
        
        size_t valueAreaSize = m_header->dzCount * YCData::VALUE_SIZE;
        size_t timestampOffset = alignUp(valueAreaSize, YCData::TIMESTAMP_SIZE);
        size_t timestampAreaSize = m_header->dzCount * YCData::TIMESTAMP_SIZE;
        size_t qualityOffset = timestampOffset + timestampAreaSize;
        size_t qualityAreaSize = m_header->dzCount * YCData::QUALITY_SIZE;
        size_t totalNeeded = qualityOffset + qualityAreaSize;
        
        if (dzBase + totalNeeded > end) return false;
        
        m_dzData.values = reinterpret_cast<float*>(dzBase);
        m_dzData.timestamps = reinterpret_cast<uint64_t*>(dzBase + timestampOffset);
        m_dzData.qualities = reinterpret_cast<uint8_t*>(dzBase + qualityOffset);
    } else {
        m_dzData.values = nullptr;
        m_dzData.timestamps = nullptr;
        m_dzData.qualities = nullptr;
    }
    
    // ========== YK 数据指针（与 YX 结构相同）==========
    if (m_header->ykCount > 0) {
        char* ykBase = base + m_header->ykDataOffset;
        
        size_t valueAreaSize = m_header->ykCount * YXData::VALUE_SIZE;
        size_t timestampOffset = alignUp(valueAreaSize, YXData::TIMESTAMP_SIZE);
        size_t timestampAreaSize = m_header->ykCount * YXData::TIMESTAMP_SIZE;
        size_t qualityOffset = timestampOffset + timestampAreaSize;
        size_t qualityAreaSize = m_header->ykCount * YXData::QUALITY_SIZE;
        size_t totalNeeded = qualityOffset + qualityAreaSize;
        
        if (ykBase + totalNeeded > end) return false;
        
        m_ykData.values = reinterpret_cast<uint8_t*>(ykBase);
        m_ykData.timestamps = reinterpret_cast<uint64_t*>(ykBase + timestampOffset);
        m_ykData.qualities = reinterpret_cast<uint8_t*>(ykBase + qualityOffset);
    } else {
        m_ykData.values = nullptr;
        m_ykData.timestamps = nullptr;
        m_ykData.qualities = nullptr;
    }
    
    // ========== 索引表 ==========
    uint32_t totalCount = m_header->yxCount + m_header->ycCount + 
                          m_header->dzCount + m_header->ykCount;
    
    // 使用头部保存的 hashSize，如果为 0 则重新计算（兼容旧版本）
    if (m_header->hashSize > 0) {
        m_hashSize = m_header->hashSize;
    } else {
        m_hashSize = 1;
        while (m_hashSize < totalCount * 2) {
            m_hashSize *= 2;
        }
    }
    
    char* indexBase = base + m_header->indexOffset;
    size_t indexNeeded = m_hashSize * sizeof(uint32_t) + 
                         (totalCount > 0 ? totalCount : 1) * sizeof(IndexEntry);
    
    if (indexBase + indexNeeded > end) return false;
    
    m_hashTable = reinterpret_cast<uint32_t*>(indexBase);
    m_indexTable = reinterpret_cast<IndexEntry*>(indexBase + m_hashSize * sizeof(uint32_t));
    
    // ========== 进程信息 ==========
    char* processBase = base + m_header->processInfoOffset;
    size_t processNeeded = MAX_PROCESS_COUNT * sizeof(ProcessInfo);
    
    if (processBase + processNeeded > end) return false;
    
    m_processInfo = reinterpret_cast<ProcessInfo*>(processBase);
    
    return true;
}

// ========== 哈希函数 ==========

uint32_t SharedDataPool::hashKey(uint64_t key) const {
    // 简单的哈希函数
    uint64_t h = key;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return static_cast<uint32_t>(h & (m_hashSize - 1));
}

// ========== 注册 Key ==========

Result SharedDataPool::registerKey(uint64_t key, PointType type, uint32_t& outIndex) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    
    lockWrite();
    
    uint32_t hash = hashKey(key);
    uint32_t idx = m_hashTable[hash];
    
    uint32_t totalCount = m_header->yxCount + m_header->ycCount + 
                          m_header->dzCount + m_header->ykCount;
    
    // 检查是否已存在（带边界检查）
    int loopGuard = 0;
    while (idx != 0 && idx <= totalCount) {
        if (++loopGuard > 1000000) {
            unlockWrite();
            return Result::ERROR;
        }
        if (m_indexTable[idx].key == key) {
            unlockWrite();
            return Result::ALREADY_EXISTS;
        }
        idx = m_indexTable[idx].next;
    }
    
    // 分配类型内索引
    uint32_t typeIndex = 0;
    switch (type) {
        case PointType::YX:
            if (m_header->yxIndex >= m_header->yxCount) {
                unlockWrite();
                return Result::OUT_OF_MEMORY;
            }
            typeIndex = m_header->yxIndex++;
            break;
        case PointType::YC:
            if (m_header->ycIndex >= m_header->ycCount) {
                unlockWrite();
                return Result::OUT_OF_MEMORY;
            }
            typeIndex = m_header->ycIndex++;
            break;
        case PointType::DZ:
            if (m_header->dzIndex >= m_header->dzCount) {
                unlockWrite();
                return Result::OUT_OF_MEMORY;
            }
            typeIndex = m_header->dzIndex++;
            break;
        case PointType::YK:
            if (m_header->ykIndex >= m_header->ykCount) {
                unlockWrite();
                return Result::OUT_OF_MEMORY;
            }
            typeIndex = m_header->ykIndex++;
            break;
    }
    
    // 分配索引表位置
    uint32_t newIndex = ++m_header->indexCount;
    
    // 插入索引表
    m_indexTable[newIndex].key = key;
    m_indexTable[newIndex].index = typeIndex;
    m_indexTable[newIndex].next = m_hashTable[hash];
    m_indexTable[newIndex].type = type;
    m_hashTable[hash] = newIndex;
    
    outIndex = typeIndex;
    
    unlockWrite();
    return Result::OK;
}

// ========== 查找 Key ==========

Result SharedDataPool::findKey(uint64_t key, PointType& type, uint32_t& index) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    
    lockRead();
    
    uint32_t hash = hashKey(key);
    uint32_t idx = m_hashTable[hash];
    
    // 边界检查：idx 必须在有效范围内
    uint32_t maxIndex = m_header->indexCount + 1; // 索引从 1 开始
    uint32_t totalCount = m_header->yxCount + m_header->ycCount + 
                          m_header->dzCount + m_header->ykCount;
    
    int loopGuard = 0; // 防止无限循环
    while (idx != 0 && idx <= totalCount && idx < maxIndex) {
        if (++loopGuard > 1000000) { // 安全限制
            unlockRead();
            return Result::NOT_FOUND;
        }
        if (m_indexTable[idx].key == key) {
            index = m_indexTable[idx].index;
            type = m_indexTable[idx].type;
            unlockRead();
            return Result::OK;
        }
        idx = m_indexTable[idx].next;
    }
    
    unlockRead();
    return Result::NOT_FOUND;
}

// ========== YX 操作 ==========

Result SharedDataPool::setYX(uint64_t key, uint8_t value, uint64_t timestamp, uint8_t quality) {
    PointType type;
    uint32_t index;
    Result ret = findKey(key, type, index);
    if (ret != Result::OK) return ret;
    if (type != PointType::YX) return Result::INVALID_PARAM;
    return setYXByIndex(index, value, timestamp, quality);
}

Result SharedDataPool::getYX(uint64_t key, uint8_t& value, uint64_t& timestamp, uint8_t& quality) {
    PointType type;
    uint32_t index;
    Result ret = findKey(key, type, index);
    if (ret != Result::OK) return ret;
    if (type != PointType::YX) return Result::INVALID_PARAM;
    return getYXByIndex(index, value, timestamp, quality);
}

Result SharedDataPool::setYXByIndex(uint32_t index, uint8_t value, uint64_t timestamp, uint8_t quality) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (index >= m_header->yxCount) return Result::INVALID_PARAM;
    
    lockWrite();
    m_yxData.values[index] = value;
    m_yxData.timestamps[index] = timestamp;
    m_yxData.qualities[index] = quality;
    m_header->lastUpdateTime = getCurrentTimestamp();
    unlockWrite();
    
    incrementWriteCount(PointType::YX);
    return Result::OK;
}

Result SharedDataPool::getYXByIndex(uint32_t index, uint8_t& value, uint64_t& timestamp, uint8_t& quality) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (index >= m_header->yxCount) return Result::INVALID_PARAM;
    
    lockRead();
    value = m_yxData.values[index];
    timestamp = m_yxData.timestamps[index];
    quality = m_yxData.qualities[index];
    unlockRead();
    
    incrementReadCount();
    return Result::OK;
}

// ========== YC 操作 ==========

Result SharedDataPool::setYC(uint64_t key, float value, uint64_t timestamp, uint8_t quality) {
    PointType type;
    uint32_t index;
    Result ret = findKey(key, type, index);
    if (ret != Result::OK) return ret;
    if (type != PointType::YC) return Result::INVALID_PARAM;
    return setYCByIndex(index, value, timestamp, quality);
}

Result SharedDataPool::getYC(uint64_t key, float& value, uint64_t& timestamp, uint8_t& quality) {
    PointType type;
    uint32_t index;
    Result ret = findKey(key, type, index);
    if (ret != Result::OK) return ret;
    if (type != PointType::YC) return Result::INVALID_PARAM;
    return getYCByIndex(index, value, timestamp, quality);
}

Result SharedDataPool::setYCByIndex(uint32_t index, float value, uint64_t timestamp, uint8_t quality) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (index >= m_header->ycCount) return Result::INVALID_PARAM;
    
    lockWrite();
    m_ycData.values[index] = value;
    m_ycData.timestamps[index] = timestamp;
    m_ycData.qualities[index] = quality;
    m_header->lastUpdateTime = getCurrentTimestamp();
    unlockWrite();
    
    incrementWriteCount(PointType::YC);
    return Result::OK;
}

Result SharedDataPool::getYCByIndex(uint32_t index, float& value, uint64_t& timestamp, uint8_t& quality) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (index >= m_header->ycCount) return Result::INVALID_PARAM;
    
    lockRead();
    value = m_ycData.values[index];
    timestamp = m_ycData.timestamps[index];
    quality = m_ycData.qualities[index];
    unlockRead();
    
    incrementReadCount();
    return Result::OK;
}

// ========== DZ 操作 ==========

Result SharedDataPool::setDZ(uint64_t key, float value, uint64_t timestamp, uint8_t quality) {
    PointType type;
    uint32_t index;
    Result ret = findKey(key, type, index);
    if (ret != Result::OK) return ret;
    if (type != PointType::DZ) return Result::INVALID_PARAM;
    if (index >= m_header->dzCount) return Result::INVALID_PARAM;
    
    lockWrite();
    m_dzData.values[index] = value;
    m_dzData.timestamps[index] = timestamp;
    m_dzData.qualities[index] = quality;
    m_header->lastUpdateTime = getCurrentTimestamp();
    unlockWrite();
    
    incrementWriteCount(PointType::DZ);
    return Result::OK;
}

Result SharedDataPool::getDZ(uint64_t key, float& value, uint64_t& timestamp, uint8_t& quality) {
    PointType type;
    uint32_t index;
    Result ret = findKey(key, type, index);
    if (ret != Result::OK) return ret;
    if (type != PointType::DZ) return Result::INVALID_PARAM;
    if (index >= m_header->dzCount) return Result::INVALID_PARAM;
    
    lockRead();
    value = m_dzData.values[index];
    timestamp = m_dzData.timestamps[index];
    quality = m_dzData.qualities[index];
    unlockRead();
    
    incrementReadCount();
    return Result::OK;
}

// ========== YK 操作 ==========

Result SharedDataPool::setYK(uint64_t key, uint8_t value, uint64_t timestamp, uint8_t quality) {
    PointType type;
    uint32_t index;
    Result ret = findKey(key, type, index);
    if (ret != Result::OK) return ret;
    if (type != PointType::YK) return Result::INVALID_PARAM;
    if (index >= m_header->ykCount) return Result::INVALID_PARAM;
    
    lockWrite();
    m_ykData.values[index] = value;
    m_ykData.timestamps[index] = timestamp;
    m_ykData.qualities[index] = quality;
    m_header->lastUpdateTime = getCurrentTimestamp();
    unlockWrite();
    
    incrementWriteCount(PointType::YK);
    return Result::OK;
}

Result SharedDataPool::getYK(uint64_t key, uint8_t& value, uint64_t& timestamp, uint8_t& quality) {
    PointType type;
    uint32_t index;
    Result ret = findKey(key, type, index);
    if (ret != Result::OK) return ret;
    if (type != PointType::YK) return Result::INVALID_PARAM;
    if (index >= m_header->ykCount) return Result::INVALID_PARAM;
    
    lockRead();
    value = m_ykData.values[index];
    timestamp = m_ykData.timestamps[index];
    quality = m_ykData.qualities[index];
    unlockRead();
    
    incrementReadCount();
    return Result::OK;
}

// ========== 进程管理 ==========

Result SharedDataPool::registerProcess(const char* name, uint32_t& processId) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (name == nullptr) return Result::INVALID_PARAM;
    
    lockWrite();
    
    // 查找空位
    for (uint32_t i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (!m_processInfo[i].active) {
            m_processInfo[i].pid = getpid();
            m_processInfo[i].setName(name);
            m_processInfo[i].lastHeartbeat = getCurrentTimestamp();
            m_processInfo[i].active = true;
            m_processInfo[i].eventReadIndex = 0;
            processId = i;
            m_header->processCount++;
            unlockWrite();
            return Result::OK;
        }
    }
    
    unlockWrite();
    return Result::OUT_OF_MEMORY;
}

Result SharedDataPool::unregisterProcess(uint32_t processId) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (processId >= MAX_PROCESS_COUNT) return Result::INVALID_PARAM;
    
    lockWrite();
    m_processInfo[processId].active = false;
    m_processInfo[processId].pid = 0;
    m_header->processCount--;
    unlockWrite();
    
    return Result::OK;
}

Result SharedDataPool::updateHeartbeat(uint32_t processId) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (processId >= MAX_PROCESS_COUNT) return Result::INVALID_PARAM;
    
    // 心跳更新使用原子写操作，确保跨进程可见性
    // volatile 确保编译器不优化掉写入
    __sync_lock_test_and_set(&m_processInfo[processId].lastHeartbeat, getCurrentTimestamp());
    return Result::OK;
}

Result SharedDataPool::getProcessInfo(uint32_t processId, ProcessInfo& info) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (processId >= MAX_PROCESS_COUNT) return Result::INVALID_PARAM;
    
    lockRead();
    info.pid = m_processInfo[processId].pid;
    info.lastHeartbeat = m_processInfo[processId].lastHeartbeat;
    info.eventReadIndex = m_processInfo[processId].eventReadIndex;
    std::memcpy(info.name, m_processInfo[processId].name, sizeof(info.name));
    info.active = m_processInfo[processId].active;
    unlockRead();
    
    return Result::OK;
}

// ========== 批量操作 ==========

Result SharedDataPool::batchSetYX(const uint64_t* keys, const uint8_t* values,
                                   const uint64_t* timestamps, uint32_t count,
                                   uint32_t* successCount) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    
    uint32_t success = 0;
    lockWrite();
    for (uint32_t i = 0; i < count; i++) {
        PointType type;
        uint32_t index;
        if (findKey(keys[i], type, index) == Result::OK && type == PointType::YX) {
            m_yxData.values[index] = values[i];
            m_yxData.timestamps[index] = timestamps[i];
            success++;
        }
    }
    m_header->lastUpdateTime = getCurrentTimestamp();
    unlockWrite();
    
    // 更新统计
    m_stats.totalWrites += success;
    m_stats.yxWrites += success;
    
    if (successCount) {
        *successCount = success;
    }
    
    return Result::OK;
}

Result SharedDataPool::batchSetYC(const uint64_t* keys, const float* values,
                                   const uint64_t* timestamps, uint32_t count,
                                   uint32_t* successCount) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    
    uint32_t success = 0;
    lockWrite();
    for (uint32_t i = 0; i < count; i++) {
        PointType type;
        uint32_t index;
        if (findKey(keys[i], type, index) == Result::OK && type == PointType::YC) {
            m_ycData.values[index] = values[i];
            m_ycData.timestamps[index] = timestamps[i];
            success++;
        }
    }
    m_header->lastUpdateTime = getCurrentTimestamp();
    unlockWrite();
    
    // 更新统计
    m_stats.totalWrites += success;
    m_stats.ycWrites += success;
    
    if (successCount) {
        *successCount = success;
    }
    
    return Result::OK;
}

// ========== 统计功能 ==========

DataPoolStats SharedDataPool::getStats() const {
    DataPoolStats stats = m_stats;
    stats.activeProcessCount = 0;
    
    // 统计活跃进程数
    for (size_t i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (m_processInfo[i].active) {
            stats.activeProcessCount++;
        }
    }
    
    return stats;
}

void SharedDataPool::resetStats() {
    m_stats = DataPoolStats();
    m_stats.lastResetTime = getCurrentTimestamp();
}

void SharedDataPool::incrementReadCount() {
    m_stats.totalReads++;
}

void SharedDataPool::incrementWriteCount(PointType type) {
    m_stats.totalWrites++;
    switch (type) {
        case PointType::YX: m_stats.yxWrites++; break;
        case PointType::YC: m_stats.ycWrites++; break;
        case PointType::DZ: m_stats.dzWrites++; break;
        case PointType::YK: m_stats.ykWrites++; break;
    }
}

// ========== 健康检查 ==========

ProcessHealth SharedDataPool::checkProcessHealth(uint32_t processId) const {
    if (!isValid() || processId >= MAX_PROCESS_COUNT) {
        return ProcessHealth::UNKNOWN;
    }
    
    if (!m_processInfo[processId].active) {
        return ProcessHealth::UNKNOWN;
    }
    
    uint64_t now = getCurrentTimestamp();
    uint64_t lastHeartbeat = m_processInfo[processId].lastHeartbeat;
    uint64_t elapsed = now > lastHeartbeat ? (now - lastHeartbeat) : 0;
    
    if (elapsed > HEARTBEAT_TIMEOUT_MS) {
        return ProcessHealth::DEAD;
    } else if (elapsed > HEARTBEAT_TIMEOUT_MS / 2) {
        return ProcessHealth::WARNING;
    }
    
    return ProcessHealth::HEALTHY;
}

ProcessHealth SharedDataPool::checkProcessHealthByPid(pid_t pid) const {
    if (!isValid()) {
        return ProcessHealth::UNKNOWN;
    }
    
    for (size_t i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (m_processInfo[i].active && m_processInfo[i].pid == pid) {
            return checkProcessHealth(static_cast<uint32_t>(i));
        }
    }
    
    return ProcessHealth::UNKNOWN;
}

uint32_t SharedDataPool::getActiveProcessList(uint32_t* processIds, uint32_t maxCount) const {
    if (!isValid() || processIds == nullptr) {
        return 0;
    }
    
    uint32_t count = 0;
    for (size_t i = 0; i < MAX_PROCESS_COUNT && count < maxCount; i++) {
        if (m_processInfo[i].active) {
            processIds[count++] = static_cast<uint32_t>(i);
        }
    }
    
    return count;
}

uint32_t SharedDataPool::cleanupDeadProcesses() {
    if (!isValid()) return 0;
    
    uint32_t cleaned = 0;
    lockWrite();
    
    for (size_t i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (m_processInfo[i].active) {
            uint64_t now = getCurrentTimestamp();
            uint64_t lastHeartbeat = m_processInfo[i].lastHeartbeat;
            
            if (now > lastHeartbeat && (now - lastHeartbeat) > HEARTBEAT_TIMEOUT_MS * 2) {
                m_processInfo[i].active = false;
                m_processInfo[i].pid = 0;
                m_header->processCount--;
                cleaned++;
            }
        }
    }
    
    unlockWrite();
    return cleaned;
}

// ========== 快照/持久化 ==========

Result SharedDataPool::saveSnapshot(const char* filename) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (filename == nullptr) return Result::INVALID_PARAM;
    
    lockRead();
    
    // 准备快照头部
    SnapshotHeader header;
    header.magic = 0x49534E50;  // "ISNP"
    header.version = 1;
    header.yxCount = m_header->yxCount;
    header.ycCount = m_header->ycCount;
    header.dzCount = m_header->dzCount;
    header.ykCount = m_header->ykCount;
    header.indexCount = m_header->indexCount;
    header.snapshotTime = getCurrentTimestamp();
    header.shmSize = m_shmSize;
    
    // 计算校验和
    header.checksum = calculateChecksum(m_shmPtr, m_shmSize);
    
    // 使用临时文件实现原子写入
    std::string tempFile = std::string(filename) + ".tmp";
    
    // 打开临时文件
    FILE* fp = fopen(tempFile.c_str(), "wb");
    if (!fp) {
        unlockRead();
        return Result::ERROR;
    }
    
    // 写入头部
    bool writeSuccess = true;
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        writeSuccess = false;
    }
    
    // 写入共享内存数据
    if (writeSuccess && fwrite(m_shmPtr, m_shmSize, 1, fp) != 1) {
        writeSuccess = false;
    }
    
    // 刷新到磁盘
    if (writeSuccess && fflush(fp) != 0) {
        writeSuccess = false;
    }
    
    // 同步到磁盘（确保数据持久化）
    if (writeSuccess && fsync(fileno(fp)) != 0) {
        writeSuccess = false;
    }
    
    fclose(fp);
    unlockRead();
    
    if (!writeSuccess) {
        // 删除临时文件
        unlink(tempFile.c_str());
        return Result::ERROR;
    }
    
    // 原子重命名
    if (rename(tempFile.c_str(), filename) != 0) {
        unlink(tempFile.c_str());
        return Result::ERROR;
    }
    
    return Result::OK;
}

Result SharedDataPool::loadSnapshot(const char* filename) {
    if (!isValid()) return Result::NOT_INITIALIZED;
    if (filename == nullptr) return Result::INVALID_PARAM;
    
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return Result::ERROR;
    }
    
    // 读取头部
    SnapshotHeader header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return Result::ERROR;
    }
    
    // 验证魔数
    if (header.magic != 0x49534E50) {
        fclose(fp);
        return Result::INVALID_PARAM;
    }
    
    // 验证大小
    if (header.shmSize != m_shmSize) {
        fclose(fp);
        return Result::INVALID_PARAM;
    }
    
    // 验证点数配置
    if (header.yxCount != m_header->yxCount ||
        header.ycCount != m_header->ycCount ||
        header.dzCount != m_header->dzCount ||
        header.ykCount != m_header->ykCount) {
        fclose(fp);
        return Result::INVALID_PARAM;
    }
    
    // 读取数据
    std::vector<char> buffer(m_shmSize);
    if (fread(buffer.data(), m_shmSize, 1, fp) != 1) {
        fclose(fp);
        return Result::ERROR;
    }
    fclose(fp);
    
    // 验证校验和
    uint32_t checksum = calculateChecksum(buffer.data(), m_shmSize);
    if (checksum != header.checksum) {
        return Result::ERROR;
    }
    
    // 应用数据
    lockWrite();
    
    // 保留原头部的一些字段
    ShmHeader* snapshotHeader = reinterpret_cast<ShmHeader*>(buffer.data());
    
    // 复制数据区域（跳过头部）
    size_t dataOffset = sizeof(ShmHeader);
    memcpy(static_cast<char*>(m_shmPtr) + dataOffset, 
           buffer.data() + dataOffset, 
           m_shmSize - dataOffset);
    
    // 更新时间戳
    m_header->lastUpdateTime = getCurrentTimestamp();
    
    unlockWrite();
    
    return Result::OK;
}

bool SharedDataPool::validateSnapshot(const char* filename) {
    if (filename == nullptr) return false;
    
    FILE* fp = fopen(filename, "rb");
    if (!fp) return false;
    
    SnapshotHeader header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return false;
    }
    
    // 检查魔数
    if (header.magic != 0x49534E50) {
        fclose(fp);
        return false;
    }
    
    // 读取并验证数据
    std::vector<char> buffer(header.shmSize);
    if (fread(buffer.data(), header.shmSize, 1, fp) != 1) {
        fclose(fp);
        return false;
    }
    fclose(fp);
    
    uint32_t checksum = calculateChecksum(buffer.data(), header.shmSize);
    return checksum == header.checksum;
}

} // namespace IPC
