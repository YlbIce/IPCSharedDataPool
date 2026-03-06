/**
 * @file business_process/main.cpp
 * @brief 业务进程示例 - 数据处理与监控
 * 
 * 功能：
 * - 连接共享数据池
 * - 订阅数据变更事件
 * - 执行业务逻辑处理
 */

#include <QCoreApplication>
#include <QTimer>
#include <QCommandLineParser>
#include <iostream>
#include <iomanip>
#include "../../include/DataPoolClient.h"

using namespace IPC;

class BusinessProcess : public QObject {
    Q_OBJECT

public:
    BusinessProcess(QObject* parent = nullptr) 
        : QObject(parent), m_client(nullptr), m_timer(nullptr), 
          m_eventCount(0), m_subId(INVALID_INDEX) {}
    
    ~BusinessProcess() {
        if (m_client) {
            m_client->shutdown();
            delete m_client;
        }
    }
    
    bool init() {
        DataPoolClient::Config config;
        config.poolName = "/ipc_data_pool";
        config.eventName = "/ipc_events";
        config.processName = "business_process";
        config.create = false;  // 连接已存在的数据池
        
        m_client = DataPoolClient::init(config);
        if (!m_client) {
            std::cerr << "Failed to connect to data pool" << std::endl;
            return false;
        }
        
        // 订阅事件
        m_subId = m_client->subscribe([this](const Event& event) {
            onEvent(event);
        });
        
        if (m_subId == INVALID_INDEX) {
            std::cerr << "Failed to subscribe events" << std::endl;
            return false;
        }
        
        std::cout << "Business process connected" << std::endl;
        std::cout << "  Data pool: " << m_client->getDataPool()->getName() << std::endl;
        std::cout << "  Subscriber ID: " << m_subId << std::endl;
        
        return true;
    }
    
    void start() {
        // 定时处理事件
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &BusinessProcess::processEvents);
        m_timer->start(10);  // 10ms 处理一次
        
        // 定时输出统计
        QTimer* statsTimer = new QTimer(this);
        connect(statsTimer, &QTimer::timeout, this, &BusinessProcess::printStats);
        statsTimer->start(5000);  // 5秒输出一次统计
        
        std::cout << "Business process started" << std::endl;
    }
    
private:
    void onEvent(const Event& event) {
        m_eventCount++;
        
        // 业务逻辑：记录关键事件
        if (event.isCritical) {
            std::cout << "[CRITICAL] Key: " << event.key 
                      << ", Type: " << static_cast<int>(event.pointType)
                      << ", Time: " << event.timestamp << std::endl;
        }
        
        // 业务逻辑：遥信变位
        if (event.pointType == PointType::YX) {
            std::cout << "[YX_CHANGE] Addr: " << getKeyAddr(event.key)
                      << ", ID: " << getKeyId(event.key)
                      << ", Value: " << event.newValue.intValue << std::endl;
        }
    }
    
private slots:
    void processEvents() {
        if (m_client && m_subId != INVALID_INDEX) {
            m_client->processEvents(m_subId, 100);  // 每次最多处理100个事件
        }
    }
    
    void printStats() {
        std::cout << "\n=== Statistics ===" << std::endl;
        std::cout << "Events processed: " << m_eventCount << std::endl;
        std::cout << "Pending events: " << m_client->getEventCenter()->getPendingEvents(m_subId) << std::endl;
        
        // 读取一些随机数据展示
        uint32_t yxCount = m_client->getDataPool()->getYXCount();
        uint32_t ycCount = m_client->getDataPool()->getYCCount();
        
        if (yxCount > 0) {
            uint8_t value, quality;
            m_client->getYXByIndex(0, value, quality);
            std::cout << "YX[0]: " << static_cast<int>(value) << " (quality: " << static_cast<int>(quality) << ")" << std::endl;
        }
        
        if (ycCount > 0) {
            float value;
            uint8_t quality;
            m_client->getYCByIndex(0, value, quality);
            std::cout << "YC[0]: " << std::fixed << std::setprecision(2) << value << " (quality: " << static_cast<int>(quality) << ")" << std::endl;
        }
    }

private:
    DataPoolClient* m_client;
    QTimer* m_timer;
    std::atomic<uint64_t> m_eventCount;
    uint32_t m_subId;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("business_process");
    QCoreApplication::setApplicationVersion("1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Business Process - Data Monitor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);
    
    BusinessProcess business;
    if (!business.init()) {
        return 1;
    }
    
    business.start();
    
    return app.exec();
}

#include "main.moc"
