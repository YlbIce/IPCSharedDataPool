/**
 * @file ui_process/main.cpp
 * @brief UI进程示例 - 数据展示与查询 (带GUI界面)
 * 
 * 功能：
 * - 连接共享数据池
 * - 查询并展示数据
 * - 提供图形界面显示运行信息
 */

#include <QApplication>
#include <QMainWindow>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QCommandLineParser>
#include <QDateTime>
#include <QGroupBox>
#include <QSplitter>
#include <QTextEdit>
#include <QScrollBar>
#include <iostream>
#include <iomanip>
#include "../../include/DataPoolClient.h"

using namespace IPC;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent), m_client(nullptr) {
        setupUI();
    }
    
    ~MainWindow() {
        if (m_client) {
            m_client->shutdown();
            delete m_client;
        }
    }
    
    bool init() {
        DataPoolClient::Config config;
        config.poolName = "/ipc_data_pool";
        config.eventName = "/ipc_events";
        config.processName = "ui_process";
        config.create = false;
        
        m_client = DataPoolClient::init(config);
        if (!m_client) {
            appendLog("错误: 无法连接到数据池");
            return false;
        }
        
        appendLog("UI进程已连接到数据池");
        return true;
    }
    
    void startRefresh(int intervalMs) {
        QTimer* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::refresh);
        timer->start(intervalMs);
        
        // 初始刷新
        refresh();
    }

private:
    void setupUI() {
        setWindowTitle("IPC数据池监控 - UI进程");
        resize(1000, 700);
        
        // 创建中央widget
        QWidget* centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        
        // 主布局
        QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
        
        // 状态栏
        QHBoxLayout* statusLayout = new QHBoxLayout();
        QLabel* statusLabel = new QLabel("状态: ", this);
        m_statusValueLabel = new QLabel("未连接", this);
        m_statusValueLabel->setStyleSheet("color: red; font-weight: bold;");
        statusLayout->addWidget(statusLabel);
        statusLayout->addWidget(m_statusValueLabel);
        statusLayout->addStretch();
        
        QLabel* timeLabel = new QLabel("更新时间: ", this);
        m_timeValueLabel = new QLabel("--", this);
        statusLayout->addWidget(timeLabel);
        statusLayout->addWidget(m_timeValueLabel);
        
        mainLayout->addLayout(statusLayout);
        
        // 分割器
        QSplitter* splitter = new QSplitter(Qt::Vertical, this);
        
        // 数据显示区域
        QGroupBox* dataGroup = new QGroupBox("数据池状态", this);
        QVBoxLayout* dataLayout = new QVBoxLayout(dataGroup);
        m_dataBrowser = new QTextBrowser(this);
        m_dataBrowser->setFont(QFont("Consolas", 10));
        m_dataBrowser->setOpenExternalLinks(false);
        dataLayout->addWidget(m_dataBrowser);
        splitter->addWidget(dataGroup);
        
        // 日志显示区域
        QGroupBox* logGroup = new QGroupBox("运行日志", this);
        QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
        m_logBrowser = new QTextBrowser(this);
        m_logBrowser->setFont(QFont("Consolas", 9));
        m_logBrowser->setMaximumHeight(200);
        logLayout->addWidget(m_logBrowser);
        splitter->addWidget(logGroup);
        
        // 设置分割比例
        splitter->setSizes({500, 200});
        
        mainLayout->addWidget(splitter);
        
        // 底部按钮区
        QHBoxLayout* buttonLayout = new QHBoxLayout();
        
        QPushButton* refreshBtn = new QPushButton("刷新数据", this);
        connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refresh);
        buttonLayout->addWidget(refreshBtn);
        
        QPushButton* clearLogBtn = new QPushButton("清除日志", this);
        connect(clearLogBtn, &QPushButton::clicked, this, &MainWindow::clearLog);
        buttonLayout->addWidget(clearLogBtn);
        
        buttonLayout->addStretch();
        
        mainLayout->addLayout(buttonLayout);
    }
    
    void appendLog(const QString& msg) {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        m_logBrowser->append(QString("[%1] %2").arg(timestamp, msg));
        m_logBrowser->verticalScrollBar()->setValue(m_logBrowser->verticalScrollBar()->maximum());
    }
    
private slots:
    void refresh() {
        if (!m_client) {
            m_statusValueLabel->setText("未连接");
            m_statusValueLabel->setStyleSheet("color: red; font-weight: bold;");
            return;
        }
        
        m_statusValueLabel->setText("已连接");
        m_statusValueLabel->setStyleSheet("color: green; font-weight: bold;");
        m_timeValueLabel->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));
        
        updateDataDisplay();
    }
    
    void clearLog() {
        m_logBrowser->clear();
        appendLog("日志已清除");
    }
    
    void updateDataDisplay() {
        SharedDataPool* pool = m_client->getDataPool();
        const ShmHeader* header = pool->getHeader();
        
        QString html;
        html += "<html><body style='font-family: Consolas;'>";
        
        // 标题
        html += "<h3 style='color: #2196F3;'>IPC共享数据池概览</h3>";
        
        // 基本信息
        html += "<table border='0' cellpadding='4' style='background-color: #f5f5f5;'>";
        html += QString("<tr><td><b>数据池名称:</b></td><td>%1</td></tr>").arg(pool->getName());
        html += QString("<tr><td><b>创建时间:</b></td><td>%1</td></tr>").arg(header->createTime);
        html += QString("<tr><td><b>最后更新:</b></td><td>%1</td></tr>").arg(header->lastUpdateTime);
        html += "</table><br>";
        
        // 点计数统计
        html += "<h4 style='color: #4CAF50;'>点计数统计</h4>";
        html += "<table border='1' cellpadding='6' cellspacing='0' style='border-collapse: collapse;'>";
        html += "<tr style='background-color: #e3f2fd;'><th>类型</th><th>描述</th><th>数量</th></tr>";
        html += QString("<tr><td style='color: #F44336; font-weight: bold;'>YX</td><td>遥信</td><td>%1</td></tr>").arg(header->yxCount);
        html += QString("<tr><td style='color: #2196F3; font-weight: bold;'>YC</td><td>遥测</td><td>%1</td></tr>").arg(header->ycCount);
        html += QString("<tr><td style='color: #FF9800; font-weight: bold;'>DZ</td><td>定值</td><td>%1</td></tr>").arg(header->dzCount);
        html += QString("<tr><td style='color: #9C27B0; font-weight: bold;'>YK</td><td>遥控</td><td>%1</td></tr>").arg(header->ykCount);
        html += "</table><br>";
        
        // YX数据表格
        if (header->yxCount > 0) {
            html += "<h4 style='color: #F44336;'>YX数据 (前10条)</h4>";
            html += "<table border='1' cellpadding='4' cellspacing='0' style='border-collapse: collapse;'>";
            html += "<tr style='background-color: #ffebee;'><th>序号</th><th>值</th><th>时间戳</th><th>质量码</th></tr>";
            
            for (uint32_t i = 0; i < std::min(header->yxCount, 10u); i++) {
                uint8_t value, quality;
                uint64_t ts;
                pool->getYXByIndex(i, value, ts, quality);
                
                QString valueColor = value ? "#4CAF50" : "#9E9E9E";
                html += QString("<tr><td>%1</td><td style='color:%2; font-weight:bold;'>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(i)
                    .arg(valueColor)
                    .arg(static_cast<int>(value))
                    .arg(ts)
                    .arg(static_cast<int>(quality));
            }
            html += "</table><br>";
        }
        
        // YC数据表格
        if (header->ycCount > 0) {
            html += "<h4 style='color: #2196F3;'>YC数据 (前10条)</h4>";
            html += "<table border='1' cellpadding='4' cellspacing='0' style='border-collapse: collapse;'>";
            html += "<tr style='background-color: #e3f2fd;'><th>序号</th><th>值</th><th>时间戳</th><th>质量码</th></tr>";
            
            for (uint32_t i = 0; i < std::min(header->ycCount, 10u); i++) {
                float value;
                uint8_t quality;
                uint64_t ts;
                pool->getYCByIndex(i, value, ts, quality);
                
                html += QString("<tr><td>%1</td><td style='font-weight:bold;'>%2</td><td>%3</td><td>%4</td></tr>")
                    .arg(i)
                    .arg(value, 0, 'f', 2)
                    .arg(ts)
                    .arg(static_cast<int>(quality));
            }
            html += "</table><br>";
        }
        
        // 注册进程信息
        html += "<h4 style='color: #607D8B;'>注册进程</h4>";
        html += "<table border='1' cellpadding='4' cellspacing='0' style='border-collapse: collapse;'>";
        html += "<tr style='background-color: #eceff1;'><th>进程ID</th><th>PID</th><th>名称</th><th>最后心跳</th></tr>";
        
        bool hasProcess = false;
        for (uint32_t i = 0; i < MAX_PROCESS_COUNT; i++) {
            ProcessInfo info;
            if (pool->getProcessInfo(i, info) == Result::OK && info.active.load()) {
                hasProcess = true;
                html += QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                    .arg(i)
                    .arg(info.pid)
                    .arg(info.name)
                    .arg(info.lastHeartbeat);
            }
        }
        if (!hasProcess) {
            html += "<tr><td colspan='4' style='text-align: center; color: #999;'>暂无注册进程</td></tr>";
        }
        html += "</table>";
        
        html += "</body></html>";
        
        m_dataBrowser->setHtml(html);
    }

private:
    DataPoolClient* m_client;
    QTextBrowser* m_dataBrowser;
    QTextBrowser* m_logBrowser;
    QLabel* m_statusValueLabel;
    QLabel* m_timeValueLabel;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("ui_process");
    QApplication::setApplicationVersion("1.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("UI Process - Data Display (GUI)");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption intervalOption("interval", "刷新间隔 (ms)", "ms", "1000");
    parser.addOption(intervalOption);
    
    parser.process(app);
    
    int interval = parser.value(intervalOption).toInt();
    
    MainWindow window;
    if (!window.init()) {
        return 1;
    }
    
    window.show();
    window.startRefresh(interval);
    
    return app.exec();
}

#include "main.moc"
