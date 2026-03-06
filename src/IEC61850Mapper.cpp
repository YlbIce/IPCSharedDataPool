#include "IEC61850Mapper.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <pugixml.hpp>

namespace IPC {

// 魔数和版本
constexpr uint32_t IEC61850_MAGIC = 0x49454336;  // "IEC6"
constexpr uint32_t IEC61850_VERSION = 1;

// 计算共享内存大小
static size_t calculateShmSize(const IEC61850Mapper::Config& config) {
    size_t headerSize = sizeof(IEC61850Mapper::ShmHeader);
    size_t mappingSize = config.maxMappings * sizeof(DAMapping);
    size_t lnSize = config.maxLNs * sizeof(LNMapping);
    size_t dsSize = config.maxDataSets * sizeof(DataSetDef);
    size_t memberSize = config.maxMappings * sizeof(DataSetMember); // 每个映射对应一个成员
    size_t rptSize = config.maxReportCtrls * sizeof(ReportControl);
    
    return headerSize + mappingSize + lnSize + dsSize + memberSize + rptSize;
}

IEC61850Mapper* IEC61850Mapper::create(const Config& config) {
    // 计算共享内存大小
    size_t shmSize = calculateShmSize(config);
    
    int fd = -1;
    void* shm = nullptr;
    bool isCreator = config.create;
    
    if (config.create) {
        // 创建新的共享内存
        fd = shm_open(config.shmName.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) {
            return nullptr;
        }
        
        if (ftruncate(fd, shmSize) < 0) {
            ::close(fd);
            shm_unlink(config.shmName.c_str());
            return nullptr;
        }
    } else {
        // 连接现有共享内存
        fd = shm_open(config.shmName.c_str(), O_RDWR, 0666);
        if (fd < 0) {
            return nullptr;
        }
    }
    
    // 映射共享内存
    shm = mmap(nullptr, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    
    if (shm == MAP_FAILED) {
        if (config.create) {
            shm_unlink(config.shmName.c_str());
        }
        return nullptr;
    }
    
    // 创建IEC61850Mapper对象
    IEC61850Mapper* mapper = new IEC61850Mapper();
    mapper->m_shm = shm;
    mapper->m_shmSize = shmSize;
    mapper->m_isCreator = isCreator;
    
    // 初始化头部和数据区指针
    ShmHeader* header = static_cast<ShmHeader*>(shm);
    uint8_t* dataPtr = static_cast<uint8_t*>(shm) + sizeof(ShmHeader);
    
    if (config.create) {
        // 初始化头部
        std::memset(shm, 0, shmSize);
        header->magic = IEC61850_MAGIC;
        header->version = IEC61850_VERSION;
        header->maxMappings = config.maxMappings;
        header->maxLNs = config.maxLNs;
        header->maxDataSets = config.maxDataSets;
        header->maxReportCtrls = config.maxReportCtrls;
        header->mappingCount = 0;
        header->lnCount = 0;
        header->dataSetCount = 0;
        header->reportCtrlCount = 0;
        header->createTime = getCurrentTimestamp();
    } else {
        // 验证现有共享内存
        if (header->magic != IEC61850_MAGIC || header->version != IEC61850_VERSION) {
            munmap(shm, shmSize);
            delete mapper;
            return nullptr;
        }
    }
    
    // 设置数据区指针
    mapper->m_mappings = reinterpret_cast<DAMapping*>(dataPtr);
    dataPtr += config.maxMappings * sizeof(DAMapping);
    
    mapper->m_lns = reinterpret_cast<LNMapping*>(dataPtr);
    dataPtr += config.maxLNs * sizeof(LNMapping);
    
    mapper->m_dataSets = reinterpret_cast<DataSetDef*>(dataPtr);
    dataPtr += config.maxDataSets * sizeof(DataSetDef);
    
    mapper->m_dsMembers = reinterpret_cast<DataSetMember*>(dataPtr);
    dataPtr += config.maxMappings * sizeof(DataSetMember);
    
    mapper->m_reportCtrls = reinterpret_cast<ReportControl*>(dataPtr);
    
    mapper->m_dsMemberCount = 0;
    
    return mapper;
}

void IEC61850Mapper::destroy() {
    if (m_shm) {
        // 取消映射
        munmap(m_shm, m_shmSize);
        
        // 如果是创建者，删除共享内存
        if (m_isCreator) {
            // 注意：这里假设使用默认名称
        }
        
        m_shm = nullptr;
    }
    delete this;
}

uint32_t IEC61850Mapper::addDAMapping(const DAMapping& mapping) {
    if (!m_shm) return INVALID_INDEX;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (header->mappingCount >= header->maxMappings) {
        return INVALID_INDEX;
    }
    
    uint32_t index = header->mappingCount++;
    m_mappings[index] = mapping;
    m_mappings[index].dataIndex = INVALID_INDEX;
    
    return index;
}

uint32_t IEC61850Mapper::addDAMappings(const DAMapping* mappings, uint32_t count) {
    if (!m_shm || !mappings) return 0;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    uint32_t added = 0;
    for (uint32_t i = 0; i < count && header->mappingCount < header->maxMappings; i++) {
        m_mappings[header->mappingCount] = mappings[i];
        m_mappings[header->mappingCount].dataIndex = INVALID_INDEX;
        header->mappingCount++;
        added++;
    }
    
    return added;
}

bool IEC61850Mapper::removeDAMapping(uint32_t index) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (index >= header->mappingCount) {
        return false;
    }
    
    // 移动最后一个到当前位置
    if (index < header->mappingCount - 1) {
        m_mappings[index] = m_mappings[header->mappingCount - 1];
    }
    header->mappingCount--;
    
    return true;
}

bool IEC61850Mapper::findMappingByKey(uint64_t dataKey, DAMapping& mapping) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->mappingCount; i++) {
        if (m_mappings[i].dataKey == dataKey) {
            mapping = m_mappings[i];
            return true;
        }
    }
    
    return false;
}

bool IEC61850Mapper::findMappingByRef(const char* lnRef, const char* doName, 
                                       const char* daName, DAMapping& mapping) {
    if (!m_shm || !lnRef || !doName || !daName) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->mappingCount; i++) {
        // 构建完整的lnRef进行比较
        char fullRef[32];
        snprintf(fullRef, sizeof(fullRef), "%s%s%d", 
                 m_mappings[i].lnPrefix, 
                 getLNClassName(static_cast<LNClass>(m_mappings[i].lnClass)),
                 m_mappings[i].lnInst);
        
        if (strcmp(fullRef, lnRef) == 0 &&
            strcmp(m_mappings[i].doName, doName) == 0 &&
            strcmp(m_mappings[i].daName, daName) == 0) {
            mapping = m_mappings[i];
            return true;
        }
    }
    
    return false;
}

uint32_t IEC61850Mapper::getMappingCount() {
    if (!m_shm) return 0;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    return header->mappingCount;
}

uint32_t IEC61850Mapper::getMappings(DAMapping* mappings, uint32_t maxCount, 
                                      uint32_t startIndex) {
    if (!m_shm || !mappings) return 0;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    uint32_t count = 0;
    for (uint32_t i = startIndex; i < header->mappingCount && count < maxCount; i++) {
        mappings[count++] = m_mappings[i];
    }
    
    return count;
}

uint32_t IEC61850Mapper::addLogicalNode(const LNMapping& ln) {
    if (!m_shm) return INVALID_INDEX;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (header->lnCount >= header->maxLNs) {
        return INVALID_INDEX;
    }
    
    uint32_t index = header->lnCount++;
    m_lns[index] = ln;
    
    return index;
}

bool IEC61850Mapper::getLogicalNode(uint32_t index, LNMapping& ln) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (index >= header->lnCount) {
        return false;
    }
    
    ln = m_lns[index];
    return true;
}

bool IEC61850Mapper::findLogicalNodeByRef(const char* lnRef, LNMapping& ln) {
    if (!m_shm || !lnRef) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->lnCount; i++) {
        if (strcmp(m_lns[i].lnRef, lnRef) == 0) {
            ln = m_lns[i];
            return true;
        }
    }
    
    return false;
}

uint32_t IEC61850Mapper::getLNCount() {
    if (!m_shm) return 0;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    return header->lnCount;
}

uint32_t IEC61850Mapper::createDataSet(const DataSetDef& def, const DataSetMember* members) {
    if (!m_shm || !members) return INVALID_INDEX;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (header->dataSetCount >= header->maxDataSets) {
        return INVALID_INDEX;
    }
    
    uint32_t index = header->dataSetCount++;
    m_dataSets[index] = def;
    m_dataSets[index].memberStartIndex = m_dsMemberCount;
    
    // 复制成员
    for (uint16_t i = 0; i < def.memberCount; i++) {
        m_dsMembers[m_dsMemberCount + i] = members[i];
    }
    m_dsMemberCount += def.memberCount;
    
    return index;
}

bool IEC61850Mapper::removeDataSet(uint32_t index) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (index >= header->dataSetCount) {
        return false;
    }
    
    // 移动成员
    uint32_t startIdx = m_dataSets[index].memberStartIndex;
    uint16_t count = m_dataSets[index].memberCount;
    uint32_t endIdx = startIdx + count;
    
    // 移动后续成员
    for (uint32_t i = endIdx; i < m_dsMemberCount; i++) {
        m_dsMembers[i - count] = m_dsMembers[i];
    }
    m_dsMemberCount -= count;
    
    // 更新后续数据集的成员起始索引
    for (uint32_t i = index + 1; i < header->dataSetCount; i++) {
        m_dataSets[i].memberStartIndex -= count;
    }
    
    // 移动数据集
    if (index < header->dataSetCount - 1) {
        m_dataSets[index] = m_dataSets[header->dataSetCount - 1];
    }
    header->dataSetCount--;
    
    return true;
}

bool IEC61850Mapper::getDataSet(uint32_t index, DataSetDef& def, 
                                 std::vector<DataSetMember>& members) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (index >= header->dataSetCount) {
        return false;
    }
    
    def = m_dataSets[index];
    
    members.clear();
    for (uint16_t i = 0; i < def.memberCount; i++) {
        members.push_back(m_dsMembers[def.memberStartIndex + i]);
    }
    
    return true;
}

bool IEC61850Mapper::findDataSetByName(const char* name, DataSetDef& def) {
    if (!m_shm || !name) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    for (uint32_t i = 0; i < header->dataSetCount; i++) {
        if (strcmp(m_dataSets[i].name, name) == 0) {
            def = m_dataSets[i];
            return true;
        }
    }
    
    return false;
}

uint32_t IEC61850Mapper::createReportControl(const ReportControl& ctrl) {
    if (!m_shm) return INVALID_INDEX;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (header->reportCtrlCount >= header->maxReportCtrls) {
        return INVALID_INDEX;
    }
    
    uint32_t index = header->reportCtrlCount++;
    m_reportCtrls[index] = ctrl;
    
    return index;
}

bool IEC61850Mapper::getReportControl(uint32_t index, ReportControl& ctrl) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (index >= header->reportCtrlCount) {
        return false;
    }
    
    ctrl = m_reportCtrls[index];
    return true;
}

bool IEC61850Mapper::enableReportControl(uint32_t index, bool enable) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (index >= header->reportCtrlCount) {
        return false;
    }
    
    // 使用reserved字段的第一个字节作为使能标志
    m_reportCtrls[index].reserved[0] = enable ? 1 : 0;
    return true;
}

bool IEC61850Mapper::readYX(uint32_t mappingIndex, uint8_t& value, uint16_t& quality) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (mappingIndex >= header->mappingCount) {
        return false;
    }
    
    // 这里需要与DataPool集成
    // 暂时返回模拟数据
    value = 0;
    quality = static_cast<uint16_t>(QualityFlag::GOOD);
    
    m_stats.readCount++;
    m_stats.lastUpdateTime = getCurrentTimestamp();
    
    return true;
}

bool IEC61850Mapper::readYC(uint32_t mappingIndex, float& value, uint16_t& quality) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (mappingIndex >= header->mappingCount) {
        return false;
    }
    
    // 这里需要与DataPool集成
    value = 0.0f;
    quality = static_cast<uint16_t>(QualityFlag::GOOD);
    
    m_stats.readCount++;
    m_stats.lastUpdateTime = getCurrentTimestamp();
    
    return true;
}

bool IEC61850Mapper::writeYX(uint32_t mappingIndex, uint8_t value, uint16_t quality) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (mappingIndex >= header->mappingCount) {
        return false;
    }
    
    // 这里需要与DataPool集成
    
    m_stats.writeCount++;
    m_stats.lastUpdateTime = getCurrentTimestamp();
    
    // 触发回调
    if (m_callbacks.onYXChange) {
        m_callbacks.onYXChange(&m_mappings[mappingIndex], 0, value);
    }
    
    return true;
}

bool IEC61850Mapper::writeYC(uint32_t mappingIndex, float value, uint16_t quality) {
    if (!m_shm) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    if (mappingIndex >= header->mappingCount) {
        return false;
    }
    
    // 这里需要与DataPool集成
    
    m_stats.writeCount++;
    m_stats.lastUpdateTime = getCurrentTimestamp();
    
    // 触发回调
    if (m_callbacks.onYCChange) {
        m_callbacks.onYCChange(&m_mappings[mappingIndex], 0.0f, value);
    }
    
    return true;
}

uint16_t IEC61850Mapper::toIEC61850Quality(uint8_t internalQuality) {
    uint16_t quality = static_cast<uint16_t>(QualityFlag::GOOD);
    
    if (internalQuality == 0xFF) {
        // 无效
        quality = static_cast<uint16_t>(QualityFlag::INVALID);
    } else if (internalQuality > 0) {
        // 有质量问题
        quality = static_cast<uint16_t>(QualityFlag::QUESTIONABLE);
        
        // 根据内部质量码设置具体标志
        if (internalQuality & 0x01) {
            quality |= static_cast<uint16_t>(QualityFlag::OUT_OF_RANGE);
        }
        if (internalQuality & 0x02) {
            quality |= static_cast<uint16_t>(QualityFlag::OLD_DATA);
        }
        if (internalQuality & 0x04) {
            quality |= static_cast<uint16_t>(QualityFlag::FAILURE);
        }
        if (internalQuality & 0x08) {
            quality |= static_cast<uint16_t>(QualityFlag::TEST);
        }
    }
    
    return quality;
}

uint8_t IEC61850Mapper::fromIEC61850Quality(uint16_t iecQuality) {
    if (iecQuality == static_cast<uint16_t>(QualityFlag::GOOD)) {
        return 0;
    }
    
    if (iecQuality & static_cast<uint16_t>(QualityFlag::INVALID)) {
        return 0xFF;
    }
    
    uint8_t quality = 0;
    
    if (iecQuality & static_cast<uint16_t>(QualityFlag::OUT_OF_RANGE)) {
        quality |= 0x01;
    }
    if (iecQuality & static_cast<uint16_t>(QualityFlag::OLD_DATA)) {
        quality |= 0x02;
    }
    if (iecQuality & static_cast<uint16_t>(QualityFlag::FAILURE)) {
        quality |= 0x04;
    }
    if (iecQuality & static_cast<uint16_t>(QualityFlag::TEST)) {
        quality |= 0x08;
    }
    
    return quality;
}

bool IEC61850Mapper::isQualityGood(uint16_t iecQuality) {
    return iecQuality == static_cast<uint16_t>(QualityFlag::GOOD);
}

void IEC61850Mapper::setCallbacks(const IEC61850Callbacks& callbacks) {
    m_callbacks = callbacks;
}

bool IEC61850Mapper::exportToSCL(const char* filename) {
    if (!m_shm || !filename) return false;
    
    FILE* fp = fopen(filename, "w");
    if (!fp) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    // 写入SCL头
    fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(fp, "<SCL version=\"2007\" revision=\"B\">\n");
    
    // 写入逻辑节点
    fprintf(fp, "  <DataTypeTemplates>\n");
    
    // 逻辑节点类型
    for (uint32_t i = 0; i < header->lnCount; i++) {
        const LNMapping& ln = m_lns[i];
        fprintf(fp, "    <LNodeType id=\"%s\" lnClass=\"%s\">\n",
                ln.lnRef, getLNClassName(static_cast<LNClass>(ln.lnClass)));
        
        // 数据对象
        for (uint32_t j = 0; j < header->mappingCount; j++) {
            const DAMapping& da = m_mappings[j];
            char fullRef[32];
            snprintf(fullRef, sizeof(fullRef), "%s%s%d",
                     da.lnPrefix, getLNClassName(static_cast<LNClass>(da.lnClass)), da.lnInst);
            
            if (strcmp(fullRef, ln.lnRef) == 0) {
                fprintf(fp, "      <DO name=\"%s\" type=\"%s\"/>\n",
                        da.doName, da.daName);
            }
        }
        
        fprintf(fp, "    </LNodeType>\n");
    }
    
    fprintf(fp, "  </DataTypeTemplates>\n");
    fprintf(fp, "</SCL>\n");
    
    fclose(fp);
    return true;
}

bool IEC61850Mapper::exportDataSetsToCSV(const char* filename) {
    if (!m_shm || !filename) return false;
    
    FILE* fp = fopen(filename, "w");
    if (!fp) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    // 写入CSV头
    fprintf(fp, "DataSetName,LDInst,LNRef,MemberCount\n");
    
    // 写入数据集
    for (uint32_t i = 0; i < header->dataSetCount; i++) {
        const DataSetDef& ds = m_dataSets[i];
        fprintf(fp, "%s,%s,%s,%d\n", ds.name, ds.ldInst, ds.lnRef, ds.memberCount);
    }
    
    fclose(fp);
    return true;
}

bool IEC61850Mapper::exportMappingsToCSV(const char* filename) {
    if (!m_shm || !filename) return false;
    
    FILE* fp = fopen(filename, "w");
    if (!fp) return false;
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    // 写入CSV头
    fprintf(fp, "Index,DataKey,LNRef,DOName,DAName,DAType,FC,DataIndex\n");
    
    // 写入映射
    for (uint32_t i = 0; i < header->mappingCount; i++) {
        const DAMapping& da = m_mappings[i];
        char fullRef[32];
        snprintf(fullRef, sizeof(fullRef), "%s%s%d",
                 da.lnPrefix, getLNClassName(static_cast<LNClass>(da.lnClass)), da.lnInst);
        
        fprintf(fp, "%u,0x%016llX,%s,%s,%s,%d,%d,%u\n",
                i, (unsigned long long)da.dataKey, fullRef,
                da.doName, da.daName, da.daType, da.fc, da.dataIndex);
    }
    
    fclose(fp);
    return true;
}

MappingStats IEC61850Mapper::getStats() {
    if (!m_shm) return MappingStats();
    
    ShmHeader* header = static_cast<ShmHeader*>(m_shm);
    
    m_stats.totalMappings = header->mappingCount;
    m_stats.totalLNs = header->lnCount;
    m_stats.totalDataSets = header->dataSetCount;
    
    return m_stats;
}

void IEC61850Mapper::resetStats() {
    std::memset(&m_stats, 0, sizeof(MappingStats));
}

/**
 * @brief 从SCL文件导入配置（使用pugixml）
 * 
 * SCL (Substation Configuration Language) 是 IEC 61850 的配置文件格式
 * 使用 pugixml 解析 XML，无Qt依赖
 */
uint32_t IEC61850Mapper::importFromSCL(const char* sclFile) {
    if (!m_shm || !sclFile) return 0;
    
    // 使用 pugixml 解析 SCL 文件
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(sclFile);
    
    if (!result) {
        return 0;
    }
    
    uint32_t importCount = 0;
    
    // 遍历 SCL 结构
    // SCL -> IED -> AccessPoint -> Server -> LDevice -> LN/LN0 -> DOI -> DAI
    pugi::xml_node scl = doc.child("SCL");
    if (!scl) {
        // 尝试直接从根节点开始
        scl = doc.root();
    }
    
    // 递归遍历所有节点
    for (pugi::xml_node ied = scl.child("IED"); ied; ied = ied.next_sibling("IED")) {
        for (pugi::xml_node ap = ied.child("AccessPoint"); ap; ap = ap.next_sibling("AccessPoint")) {
            for (pugi::xml_node server = ap.child("Server"); server; server = server.next_sibling("Server")) {
                // 遍历 LDevice
                for (pugi::xml_node ld = server.child("LDevice"); ld; ld = ld.next_sibling("LDevice")) {
                    const char* ldInst = ld.attribute("inst").as_string("");
                    
                    // 遍历 LN 和 LN0
                    for (pugi::xml_node ln = ld.first_child(); ln; ln = ln.next_sibling()) {
                        const char* lnName = ln.name();
                        
                        // 只处理 LN 和 LN0 节点
                        if (strcmp(lnName, "LN") != 0 && strcmp(lnName, "LN0") != 0) {
                            continue;
                        }
                        
                        const char* lnPrefix = ln.attribute("prefix").as_string("");
                        const char* lnClass = ln.attribute("lnClass").as_string("");
                        int lnInst = ln.attribute("inst").as_int(0);
                        
                        // 添加逻辑节点
                        LNMapping lnMapping;
                        std::memset(&lnMapping, 0, sizeof(LNMapping));
                        
                        // 构建 lnRef: prefix + lnClass + inst
                        char lnRef[32];
                        snprintf(lnRef, sizeof(lnRef), "%s%s%d", lnPrefix, lnClass, lnInst);
                        std::strncpy(lnMapping.lnRef, lnRef, sizeof(lnMapping.lnRef) - 1);
                        std::strncpy(lnMapping.lnPrefix, lnPrefix, sizeof(lnMapping.lnPrefix) - 1);
                        lnMapping.lnClass = static_cast<uint8_t>(parseLNClassName(lnClass));
                        lnMapping.lnInst = static_cast<uint8_t>(lnInst);
                        lnMapping.daCount = 0;
                        lnMapping.daStartIndex = m_stats.totalMappings;
                        
                        addLogicalNode(lnMapping);
                        
                        // 遍历 DOI
                        for (pugi::xml_node doi = ln.child("DOI"); doi; doi = doi.next_sibling("DOI")) {
                            const char* doName = doi.attribute("name").as_string("");
                            
                            // 遍历 DAI
                            for (pugi::xml_node dai = doi.child("DAI"); dai; dai = dai.next_sibling("DAI")) {
                                const char* daName = dai.attribute("name").as_string("");
                                const char* fc = dai.attribute("fc").as_string("");
                                const char* bType = dai.attribute("bType").as_string("");
                                uint64_t dataKey = dai.attribute("dataKey").as_ullong(0);
                                
                                // 创建映射
                                if (strlen(doName) > 0 && strlen(daName) > 0) {
                                    DAMapping mapping;
                                    std::memset(&mapping, 0, sizeof(DAMapping));
                                    
                                    mapping.dataKey = dataKey;
                                    std::strncpy(mapping.lnPrefix, lnPrefix, sizeof(mapping.lnPrefix) - 1);
                                    mapping.lnClass = static_cast<uint8_t>(parseLNClassName(lnClass));
                                    mapping.lnInst = static_cast<uint8_t>(lnInst);
                                    std::strncpy(mapping.doName, doName, sizeof(mapping.doName) - 1);
                                    std::strncpy(mapping.daName, daName, sizeof(mapping.daName) - 1);
                                    
                                    // 解析数据属性类型
                                    if (strcmp(bType, "BOOLEAN") == 0 || strcmp(bType, "Dbpos") == 0) {
                                        mapping.daType = static_cast<uint8_t>(DAType::SPS);
                                    } else if (strcmp(bType, "FLOAT32") == 0) {
                                        mapping.daType = static_cast<uint8_t>(DAType::MV);
                                    } else if (strcmp(bType, "INT32") == 0) {
                                        mapping.daType = static_cast<uint8_t>(DAType::INS);
                                    }
                                    
                                    // 解析功能约束
                                    if (strcmp(fc, "ST") == 0) {
                                        mapping.fc = 0;  // Status
                                    } else if (strcmp(fc, "MX") == 0) {
                                        mapping.fc = 1;  // Measurand
                                    } else if (strcmp(fc, "CF") == 0) {
                                        mapping.fc = 2;  // Configuration
                                    } else if (strcmp(fc, "SP") == 0) {
                                        mapping.fc = 3;  // Setting
                                    } else if (strcmp(fc, "SE") == 0) {
                                        mapping.fc = 4;  // Setting extension
                                    } else if (strcmp(fc, "SG") == 0) {
                                        mapping.fc = 5;  // Setting group
                                    }
                                    
                                    if (addDAMapping(mapping) != INVALID_INDEX) {
                                        importCount++;
                                    }
                                }
                            }
                        }
                        
                        // 也处理 DA 节点（DataTypeTemplate 中的定义）
                        for (pugi::xml_node doi = ln.child("DOI"); doi; doi = doi.next_sibling("DOI")) {
                            const char* doName = doi.attribute("name").as_string("");
                            
                            for (pugi::xml_node da = doi.child("DA"); da; da = da.next_sibling("DA")) {
                                const char* daName = da.attribute("name").as_string("");
                                const char* fc = da.attribute("fc").as_string("");
                                const char* bType = da.attribute("bType").as_string("");
                                
                                if (strlen(doName) > 0 && strlen(daName) > 0) {
                                    DAMapping mapping;
                                    std::memset(&mapping, 0, sizeof(DAMapping));
                                    
                                    std::strncpy(mapping.lnPrefix, lnPrefix, sizeof(mapping.lnPrefix) - 1);
                                    mapping.lnClass = static_cast<uint8_t>(parseLNClassName(lnClass));
                                    mapping.lnInst = static_cast<uint8_t>(lnInst);
                                    std::strncpy(mapping.doName, doName, sizeof(mapping.doName) - 1);
                                    std::strncpy(mapping.daName, daName, sizeof(mapping.daName) - 1);
                                    
                                    if (strcmp(bType, "BOOLEAN") == 0 || strcmp(bType, "Dbpos") == 0) {
                                        mapping.daType = static_cast<uint8_t>(DAType::SPS);
                                    } else if (strcmp(bType, "FLOAT32") == 0) {
                                        mapping.daType = static_cast<uint8_t>(DAType::MV);
                                    } else if (strcmp(bType, "INT32") == 0) {
                                        mapping.daType = static_cast<uint8_t>(DAType::INS);
                                    }
                                    
                                    if (strcmp(fc, "ST") == 0) mapping.fc = 0;
                                    else if (strcmp(fc, "MX") == 0) mapping.fc = 1;
                                    else if (strcmp(fc, "CF") == 0) mapping.fc = 2;
                                    else if (strcmp(fc, "SP") == 0) mapping.fc = 3;
                                    else mapping.fc = 4;
                                    
                                    if (addDAMapping(mapping) != INVALID_INDEX) {
                                        importCount++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    return importCount;
}

} // namespace IPC
