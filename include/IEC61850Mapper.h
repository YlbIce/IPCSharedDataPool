#ifndef IEC61850_MAPPER_H
#define IEC61850_MAPPER_H

#include "Common.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

namespace IPC {

// ========== IEC 61850 常量定义 ==========

// 逻辑节点类型
enum class LNClass : uint8_t {
    // 系统逻辑节点
    LPHD = 0,   // 物理设备信息
    LLN0 = 1,   // 逻辑节点零
    
    // 开关设备
    XCBR = 10,  // 断路器
    XSWI = 11,  // 隔离开关
    CILO = 12,  // 闭锁
    CSWI = 13,  // 开关控制
    
    // 测量
    MMXU = 20,  // 测量（不分组）
    MMTR = 21,  // 计量
    MHAN = 22,  // 谐波
    MHAI = 23,  // 增量谐波
    
    // 保护功能
    PTOC = 30,  // 过流保护
    PDIS = 31,  // 距离保护
    PIOC = 32,  // 瞬时过流
    PDEF = 33,  // 接地故障
    PVOC = 34,  // 电压控制过流
    PTEV = 35,  // 变压器保护
    
    // 保护相关
    RREC = 40,  // 重合闸
    RBRF = 41,  // 断路器失败
    
    // 变压器
    YLTC = 50,  // 有载调压
    TCTR = 51,  // 电流互感器
    TVTR = 52,  // 电压互感器
    
    // 控制
    GAPC = 60,  // 通用自动过程控制
    
    // 用户扩展
    USER = 255
};

// 数据属性类型
enum class DAType : uint8_t {
    // 状态
    SPS = 0,    // 单点状态
    DPS = 1,    // 双点状态
    INS = 2,    // 整数状态
    
    // 测量
    MV = 10,    // 测量值
    CMV = 11,   // 复数测量值
    SAV = 12,   // 采样值
    
    // 控制
    SPC = 20,   // 单点控制
    DPC = 21,   // 双点控制
    INC = 22,   // 整数控制
    
    // 定值
    ASG = 30,   // 模拟定值
    ISG = 31,   // 整数定值
    ENG = 32,   // 枚举定值
    
    // 其他
    ACT = 40,   // 激活信息
    WYE = 41,   // 三相测量
    DEL = 42,   // 三角测量
    
    CUSTOM = 255
};

// 质量码标志（IEC 61850 Quality）
enum class QualityFlag : uint16_t {
    GOOD = 0x0000,
    INVALID = 0x0001,
    QUESTIONABLE = 0x0002,
    OLD_DATA = 0x0004,
    INCONSISTENT = 0x0008,
    INACCURATE = 0x0010,
    OUT_OF_RANGE = 0x0020,
    FAILURE = 0x0040,
    OSCILLATORY = 0x0080,
    DERIVED = 0x0100,
    OPERATOR_BLOCKED = 0x0200,
    TEST = 0x0400,
    SUBSTITUTED = 0x0800,
    SOURCE = 0x1000,       // 来源：substituted or process
    OVERFLOW = 0x2000,
    INVALID_REASON_1 = 0x4000,
    INVALID_REASON_2 = 0x8000
};

// ========== 映射结构定义 ==========

/**
 * @brief IEC 61850 数据属性映射条目
 */
#pragma pack(push, 1)
struct DAMapping {
    uint64_t dataKey;           // 数据池中的key
    char lnPrefix[4];           // 逻辑节点前缀（如"CB"）
    uint8_t lnClass;            // LNClass
    uint8_t lnInst;             // 逻辑节点实例号
    char doName[16];            // 数据对象名（如"Pos", "StVal"）
    char daName[16];            // 数据属性名（如"stVal", "q", "t"）
    uint8_t daType;             // DAType
    uint8_t fc;                 // 功能约束（ST/MX/CF/SP/SE/MX等）
    uint8_t triggerOpts;        // 触发选项（dchg/qchg/dupd）
    uint8_t sAddr;              // 简短地址
    uint32_t dataIndex;         // 数据池索引（缓存）
    uint8_t reserved[18];
};
#pragma pack(pop)

static_assert(sizeof(DAMapping) == 72, "DAMapping size mismatch");

/**
 * @brief 逻辑节点映射
 */
#pragma pack(push, 1)
struct LNMapping {
    char lnRef[32];             // 逻辑节点引用（如"CB1XCBR1"）
    uint8_t lnClass;            // LNClass
    uint8_t lnInst;             // 实例号
    char lnPrefix[4];           // 前缀
    char lnSuffix[4];           // 后缀
    uint16_t daCount;           // 数据属性数量
    uint32_t daStartIndex;      // 数据属性起始索引
    uint32_t parentIndex;       // 父节点索引（LDevice）
    uint8_t reserved[12];
};
#pragma pack(pop)

static_assert(sizeof(LNMapping) == 64, "LNMapping size mismatch");

/**
 * @brief 数据集定义
 */
#pragma pack(push, 1)
struct DataSetDef {
    char name[16];              // 数据集名称
    char ldInst[4];             // 逻辑设备实例
    char lnRef[16];             // 关联逻辑节点
    uint16_t memberCount;       // 成员数量
    uint32_t memberStartIndex;  // 成员起始索引
    uint8_t isDynamic;          // 是否动态创建
    uint8_t reserved[21];
};
#pragma pack(pop)

static_assert(sizeof(DataSetDef) == 64, "DataSetDef size mismatch");

/**
 * @brief 数据集成员
 */
#pragma pack(push, 1)
struct DataSetMember {
    uint32_t daMappingIndex;    // 关联的DAMapping索引
    char fcDa[32];              // 功能约束数据属性引用
    uint8_t reserved[28];
};
#pragma pack(pop)

static_assert(sizeof(DataSetMember) == 64, "DataSetMember size mismatch");

/**
 * @brief 报告控制块定义
 */
#pragma pack(push, 1)
struct ReportControl {
    char name[16];              // 报告控制块名称
    char rptId[16];             // 报告ID
    char dataSetRef[32];        // 数据集引用
    uint8_t confRev[4];         // 配置版本
    uint8_t optFlds;            // 可选域
    uint8_t trgOps;             // 触发选项
    uint16_t intgPd;            // 集成周期（毫秒）
    uint16_t bufTime;           // 缓冲时间
    uint8_t buffered;           // 是否缓冲
    uint8_t indexed;            // 是否索引
    uint8_t reserved[84];
};
#pragma pack(pop)

static_assert(sizeof(ReportControl) == 160, "ReportControl size mismatch");

/**
 * @brief 映射统计
 */
struct MappingStats {
    uint32_t totalMappings;     // 总映射数
    uint32_t totalLNs;          // 总逻辑节点数
    uint32_t totalDataSets;     // 总数据集数
    uint32_t readCount;         // 读取次数
    uint32_t writeCount;        // 写入次数
    uint32_t eventCount;        // 事件次数
    uint64_t lastUpdateTime;    // 最后更新时间
};

/**
 * @brief IEC 61850 映射回调
 */
struct IEC61850Callbacks {
    // 数据变更回调
    std::function<void(const DAMapping* mapping, uint32_t oldValue, uint32_t newValue)> onYXChange;
    std::function<void(const DAMapping* mapping, float oldValue, float newValue)> onYCChange;
    
    // 品质变更回调
    std::function<void(const DAMapping* mapping, uint16_t oldQuality, uint16_t newQuality)> onQualityChange;
    
    // 报告触发回调
    std::function<void(const ReportControl* rptCtrl, const char* reason)> onReportTrigger;
};

/**
 * @brief IEC 61850 数据模型映射器
 * 
 * 提供数据池到 IEC 61850 数据模型的映射功能：
 * - 逻辑节点（LN）映射
 * - 数据属性（DA）映射
 * - 数据集定义与导出
 * - 报告控制块支持
 * - IEC 61850 质量码转换
 */
class IEC61850Mapper {
public:
    /**
     * @brief 配置参数
     */
    struct Config {
        std::string shmName;        // 共享内存名称
        uint32_t maxMappings;       // 最大映射条目数
        uint32_t maxLNs;            // 最大逻辑节点数
        uint32_t maxDataSets;       // 最大数据集数
        uint32_t maxReportCtrls;    // 最大报告控制块数
        bool create;                // 是否创建
        
        Config()
            : shmName("/ipc_iec61850"),
              maxMappings(10000),
              maxLNs(1000),
              maxDataSets(100),
              maxReportCtrls(50),
              create(false) {}
    };
    
    /**
     * @brief 创建或连接映射器
     */
    static IEC61850Mapper* create(const Config& config);
    
    /**
     * @brief 销毁映射器
     */
    void destroy();
    
    // ========== 数据属性映射 ==========
    
    /**
     * @brief 添加数据属性映射
     */
    uint32_t addDAMapping(const DAMapping& mapping);
    
    /**
     * @brief 批量添加映射
     */
    uint32_t addDAMappings(const DAMapping* mappings, uint32_t count);
    
    /**
     * @brief 删除数据属性映射
     */
    bool removeDAMapping(uint32_t index);
    
    /**
     * @brief 通过数据池key查找映射
     */
    bool findMappingByKey(uint64_t dataKey, DAMapping& mapping);
    
    /**
     * @brief 通过IEC引用查找映射
     */
    bool findMappingByRef(const char* lnRef, const char* doName, 
                          const char* daName, DAMapping& mapping);
    
    /**
     * @brief 获取映射数量
     */
    uint32_t getMappingCount();
    
    /**
     * @brief 遍历映射（用于导出）
     */
    uint32_t getMappings(DAMapping* mappings, uint32_t maxCount, 
                          uint32_t startIndex = 0);
    
    // ========== 逻辑节点映射 ==========
    
    /**
     * @brief 添加逻辑节点
     */
    uint32_t addLogicalNode(const LNMapping& ln);
    
    /**
     * @brief 获取逻辑节点
     */
    bool getLogicalNode(uint32_t index, LNMapping& ln);
    
    /**
     * @brief 通过引用查找逻辑节点
     */
    bool findLogicalNodeByRef(const char* lnRef, LNMapping& ln);
    
    /**
     * @brief 获取逻辑节点数量
     */
    uint32_t getLNCount();
    
    // ========== 数据集管理 ==========
    
    /**
     * @brief 创建数据集
     */
    uint32_t createDataSet(const DataSetDef& def, const DataSetMember* members);
    
    /**
     * @brief 删除数据集
     */
    bool removeDataSet(uint32_t index);
    
    /**
     * @brief 获取数据集定义
     */
    bool getDataSet(uint32_t index, DataSetDef& def, std::vector<DataSetMember>& members);
    
    /**
     * @brief 通过名称查找数据集
     */
    bool findDataSetByName(const char* name, DataSetDef& def);
    
    // ========== 报告控制块 ==========
    
    /**
     * @brief 创建报告控制块
     */
    uint32_t createReportControl(const ReportControl& ctrl);
    
    /**
     * @brief 获取报告控制块
     */
    bool getReportControl(uint32_t index, ReportControl& ctrl);
    
    /**
     * @brief 使能报告控制块
     */
    bool enableReportControl(uint32_t index, bool enable);
    
    // ========== 数据读写 ==========
    
    /**
     * @brief 读取映射数据（YX）
     */
    bool readYX(uint32_t mappingIndex, uint8_t& value, uint16_t& quality);
    
    /**
     * @brief 读取映射数据（YC）
     */
    bool readYC(uint32_t mappingIndex, float& value, uint16_t& quality);
    
    /**
     * @brief 写入映射数据（YX）
     */
    bool writeYX(uint32_t mappingIndex, uint8_t value, uint16_t quality = 0);
    
    /**
     * @brief 写入映射数据（YC）
     */
    bool writeYC(uint32_t mappingIndex, float value, uint16_t quality = 0);
    
    // ========== 质量码转换 ==========
    
    /**
     * @brief 内部质量码转IEC 61850质量码
     */
    static uint16_t toIEC61850Quality(uint8_t internalQuality);
    
    /**
     * @brief IEC 61850质量码转内部质量码
     */
    static uint8_t fromIEC61850Quality(uint16_t iecQuality);
    
    /**
     * @brief 检查质量是否良好
     */
    static bool isQualityGood(uint16_t iecQuality);
    
    // ========== 回调设置 ==========
    
    /**
     * @brief 设置回调函数
     */
    void setCallbacks(const IEC61850Callbacks& callbacks);
    
    // ========== 导出功能 ==========
    
    /**
     * @brief 导出为SCL格式（简化XML）
     */
    bool exportToSCL(const char* filename);
    
    /**
     * @brief 导出数据集到CSV
     */
    bool exportDataSetsToCSV(const char* filename);
    
    /**
     * @brief 导出映射表到CSV
     */
    bool exportMappingsToCSV(const char* filename);
    
    /**
     * @brief 从SCL文件导入配置
     * @param sclFile SCL文件路径
     * @return 导入的映射数量，失败返回0
     */
    uint32_t importFromSCL(const char* sclFile);
    
    // ========== 统计信息 ==========
    
    /**
     * @brief 获取统计信息
     */
    MappingStats getStats();
    
    /**
     * @brief 重置统计
     */
    void resetStats();
    
private:
    IEC61850Mapper() = default;
    
public:
    // 共享内存头
    struct ShmHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t maxMappings;
        uint32_t maxLNs;
        uint32_t maxDataSets;
        uint32_t maxReportCtrls;
        uint32_t mappingCount;
        uint32_t lnCount;
        uint32_t dataSetCount;
        uint32_t reportCtrlCount;
        uint64_t createTime;
        uint8_t reserved[32];
    };
    
private:
    void* m_shm;
    void* m_shmData;
    size_t m_shmSize;
    bool m_isCreator;
    
    // 数据区域指针
    DAMapping* m_mappings;
    LNMapping* m_lns;
    DataSetDef* m_dataSets;
    DataSetMember* m_dsMembers;
    ReportControl* m_reportCtrls;
    uint32_t m_dsMemberCount;
    
    IEC61850Callbacks m_callbacks;
    MappingStats m_stats;
};

// ========== 辅助函数 ==========

/**
 * @brief 获取逻辑节点类名称字符串
 */
inline const char* getLNClassName(LNClass lnClass) {
    switch (lnClass) {
        case LNClass::LPHD: return "LPHD";
        case LNClass::LLN0: return "LLN0";
        case LNClass::XCBR: return "XCBR";
        case LNClass::XSWI: return "XSWI";
        case LNClass::CILO: return "CILO";
        case LNClass::CSWI: return "CSWI";
        case LNClass::MMXU: return "MMXU";
        case LNClass::MMTR: return "MMTR";
        case LNClass::MHAN: return "MHAN";
        case LNClass::MHAI: return "MHAI";
        case LNClass::PTOC: return "PTOC";
        case LNClass::PDIS: return "PDIS";
        case LNClass::PIOC: return "PIOC";
        case LNClass::PDEF: return "PDEF";
        case LNClass::PVOC: return "PVOC";
        case LNClass::PTEV: return "PTEV";
        case LNClass::RREC: return "RREC";
        case LNClass::RBRF: return "RBRF";
        case LNClass::YLTC: return "YLTC";
        case LNClass::TCTR: return "TCTR";
        case LNClass::TVTR: return "TVTR";
        case LNClass::GAPC: return "GAPC";
        default: return "USER";
    }
}

/**
 * @brief 解析逻辑节点类名称
 */
inline LNClass parseLNClassName(const char* name) {
    if (strcmp(name, "LPHD") == 0) return LNClass::LPHD;
    if (strcmp(name, "LLN0") == 0) return LNClass::LLN0;
    if (strcmp(name, "XCBR") == 0) return LNClass::XCBR;
    if (strcmp(name, "XSWI") == 0) return LNClass::XSWI;
    if (strcmp(name, "CILO") == 0) return LNClass::CILO;
    if (strcmp(name, "CSWI") == 0) return LNClass::CSWI;
    if (strcmp(name, "MMXU") == 0) return LNClass::MMXU;
    if (strcmp(name, "MMTR") == 0) return LNClass::MMTR;
    if (strcmp(name, "MHAN") == 0) return LNClass::MHAN;
    if (strcmp(name, "MHAI") == 0) return LNClass::MHAI;
    if (strcmp(name, "PTOC") == 0) return LNClass::PTOC;
    if (strcmp(name, "PDIS") == 0) return LNClass::PDIS;
    if (strcmp(name, "PIOC") == 0) return LNClass::PIOC;
    if (strcmp(name, "PDEF") == 0) return LNClass::PDEF;
    if (strcmp(name, "PVOC") == 0) return LNClass::PVOC;
    if (strcmp(name, "PTEV") == 0) return LNClass::PTEV;
    if (strcmp(name, "RREC") == 0) return LNClass::RREC;
    if (strcmp(name, "RBRF") == 0) return LNClass::RBRF;
    if (strcmp(name, "YLTC") == 0) return LNClass::YLTC;
    if (strcmp(name, "TCTR") == 0) return LNClass::TCTR;
    if (strcmp(name, "TVTR") == 0) return LNClass::TVTR;
    if (strcmp(name, "GAPC") == 0) return LNClass::GAPC;
    return LNClass::USER;
}

} // namespace IPC

#endif // IEC61850_MAPPER_H
