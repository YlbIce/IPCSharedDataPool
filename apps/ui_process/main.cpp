/**
 * @file ui_process/main.cpp
 * @brief UI进程 - 可视化数据监控与操作界面
 * 
 * 功能：
 * - 实时数据展示
 * - 遥控操作
 * - SOE事件查看
 * - 三取二表决结果监控
 * - 点位趋势图（使用QCustomPlot）
 */

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QProgressBar>
#include <QTabWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QSplitter>
#include <QStatusBar>
#include <QCommandLineParser>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QVector>
#include <deque>
#include <iostream>
#include <random>
#include "../../include/DataPoolClient.h"
#include "../../include/ProcessMonitor.h"
#include "../../examples/qcustomplot/qcustomplot.h"

using namespace IPC;

/**
 * @brief 遥控操作对话框
 */
class YKDialog : public QDialog {
    Q_OBJECT
public:
    YKDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("遥控操作");
        setModal(true);
        
        QFormLayout* layout = new QFormLayout(this);
        
        m_keyEdit = new QLineEdit(this);
        m_keyEdit->setPlaceholderText("设备地址:点号 (如 4:0)");
        layout->addRow("点位Key:", m_keyEdit);
        
        m_valueCombo = new QComboBox(this);
        m_valueCombo->addItem("分 (0)", 0);
        m_valueCombo->addItem("合 (1)", 1);
        layout->addRow("控制值:", m_valueCombo);
        
        m_qualitySpin = new QSpinBox(this);
        m_qualitySpin->setRange(0, 255);
        m_qualitySpin->setValue(0);
        layout->addRow("质量码:", m_qualitySpin);
        
        QDialogButtonBox* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addRow(buttons);
    }
    
    uint64_t getKey() const {
        QString text = m_keyEdit->text();
        auto parts = text.split(':');
        if (parts.size() == 2) {
            return makeKey(parts[0].toInt(), parts[1].toInt());
        }
        return 0;
    }
    
    uint8_t getValue() const { return static_cast<uint8_t>(m_valueCombo->currentData().toInt()); }
    uint8_t getQuality() const { return static_cast<uint8_t>(m_qualitySpin->value()); }

private:
    QLineEdit* m_keyEdit;
    QComboBox* m_valueCombo;
    QSpinBox* m_qualitySpin;
};

/**
 * @brief 点位趋势图对话框（使用QCustomPlot）
 */
class TrendDialog : public QDialog {
    Q_OBJECT

public:
    TrendDialog(DataPoolClient* client, uint64_t key, QWidget* parent = nullptr)
        : QDialog(parent), m_client(client), m_key(key), m_running(false) {
        setWindowTitle(QString("YC趋势图 - Key: %1").arg(key));
        resize(900, 600);
        
        setupUI();
        startTrend();
    }
    
    ~TrendDialog() {
        stopTrend();
    }

private:
    void setupUI() {
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        
        // 控制面板
        QHBoxLayout* ctrlLayout = new QHBoxLayout();
        
        ctrlLayout->addWidget(new QLabel("采样间隔(ms):"));
        m_intervalSpin = new QSpinBox(this);
        m_intervalSpin->setRange(10, 5000);
        m_intervalSpin->setValue(100);
        ctrlLayout->addWidget(m_intervalSpin);
        
        ctrlLayout->addWidget(new QLabel("显示点数:"));
        m_pointsSpin = new QSpinBox(this);
        m_pointsSpin->setRange(50, 2000);
        m_pointsSpin->setValue(500);
        ctrlLayout->addWidget(m_pointsSpin);
        
        m_pauseBtn = new QPushButton("暂停", this);
        connect(m_pauseBtn, &QPushButton::clicked, this, &TrendDialog::togglePause);
        ctrlLayout->addWidget(m_pauseBtn);
        
        QPushButton* clearBtn = new QPushButton("清除", this);
        connect(clearBtn, &QPushButton::clicked, this, &TrendDialog::clearData);
        ctrlLayout->addWidget(clearBtn);
        
        QPushButton* exportBtn = new QPushButton("导出PNG", this);
        connect(exportBtn, &QPushButton::clicked, this, &TrendDialog::exportPNG);
        ctrlLayout->addWidget(exportBtn);
        
        ctrlLayout->addStretch();
        
        // 统计信息
        m_statsLabel = new QLabel("当前值: -- | 最小: -- | 最大: -- | 平均: --", this);
        m_statsLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
        ctrlLayout->addWidget(m_statsLabel);
        
        mainLayout->addLayout(ctrlLayout);
        
        // QCustomPlot图表
        m_plot = new QCustomPlot(this);
        setupPlot();
        mainLayout->addWidget(m_plot);
        
        // 定时器
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &TrendDialog::updateData);
    }
    
    void setupPlot() {
        // 添加曲线
        m_plot->addGraph();
        m_plot->graph(0)->setPen(QPen(QColor(33, 150, 243), 2));  // 蓝色线条
        m_plot->graph(0)->setBrush(QBrush(QColor(33, 150, 243, 30)));  // 半透明填充
        
        // 设置坐标轴
        m_plot->xAxis->setLabel("时间 (秒)");
        m_plot->yAxis->setLabel("值");
        m_plot->xAxis->setRange(0, 60);
        m_plot->yAxis->setRange(-10, 110);
        
        // 网格
        m_plot->xAxis->grid()->setVisible(true);
        m_plot->yAxis->grid()->setVisible(true);
        m_plot->xAxis->grid()->setPen(QPen(QColor(200, 200, 200), 1, Qt::DotLine));
        m_plot->yAxis->grid()->setPen(QPen(QColor(200, 200, 200), 1, Qt::DotLine));
        
        // 交互：拖拽、缩放
        m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
        
        // 图例
        m_plot->legend->setVisible(true);
        m_plot->graph(0)->setName(QString("YC %1").arg(m_key));
        m_plot->legend->setBrush(QColor(255, 255, 255, 150));
        
        // 时间轴格式化
        QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
        timeTicker->setTimeFormat("%m:%s");
        m_plot->xAxis->setTicker(timeTicker);
        
        m_plot->axisRect()->setupFullAxesBox();
        
        // 同步上下和左右坐标轴
        connect(m_plot->xAxis, SIGNAL(rangeChanged(QCPRange)), m_plot->xAxis2, SLOT(setRange(QCPRange)));
        connect(m_plot->yAxis, SIGNAL(rangeChanged(QCPRange)), m_plot->yAxis2, SLOT(setRange(QCPRange)));
    }
    
    void startTrend() {
        m_startTime = QDateTime::currentDateTime();
        m_running = true;
        m_timer->start(m_intervalSpin->value());
    }
    
    void stopTrend() {
        m_running = false;
        m_timer->stop();
    }
    
private slots:
    void updateData() {
        if (!m_client || !m_running) return;
        
        float value;
        uint8_t quality;
        if (!m_client->getYC(m_key, value, quality)) return;
        
        // 计算时间（秒）
        double time = m_startTime.msecsTo(QDateTime::currentDateTime()) / 1000.0;
        
        // 添加数据点
        m_plot->graph(0)->addData(time, value);
        
        // 限制数据点数量
        int maxPoints = m_pointsSpin->value();
        while (m_plot->graph(0)->data()->size() > maxPoints) {
            m_plot->graph(0)->data()->remove(m_plot->graph(0)->data()->begin()->key);
        }
        
        // 自动滚动X轴
        m_plot->xAxis->setRange(time, 60, Qt::AlignRight);
        
        // 自动调整Y轴范围
        m_plot->graph(0)->rescaleValueAxis(false, true);
        
        // 重绘
        m_plot->replot();
        
        // 更新统计
        updateStats();
    }
    
    void updateStats() {
        if (m_plot->graph(0)->data()->isEmpty()) {
            m_statsLabel->setText("当前值: -- | 最小: -- | 最大: -- | 平均: --");
            return;
        }
        
        // 获取最后一个值 - 使用end()-1获取最后一个元素
        auto it = m_plot->graph(0)->data()->constEnd();
        --it;  // 移动到最后一个元素
        double current = it->value;
        
        // 计算统计
        double minVal = 1e30, maxVal = -1e30, sum = 0;
        int count = 0;
        for (auto it = m_plot->graph(0)->data()->constBegin(); it != m_plot->graph(0)->data()->constEnd(); ++it) {
            double v = it->value;
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
            sum += v;
            count++;
        }
        double avg = count > 0 ? sum / count : 0;
        
        m_statsLabel->setText(QString("当前值: %1 | 最小: %2 | 最大: %3 | 平均: %4")
            .arg(current, 0, 'f', 2)
            .arg(minVal, 0, 'f', 2)
            .arg(maxVal, 0, 'f', 2)
            .arg(avg, 0, 'f', 2));
    }
    
    void togglePause() {
        m_running = !m_running;
        if (m_running) {
            m_timer->start(m_intervalSpin->value());
            m_pauseBtn->setText("暂停");
        } else {
            m_timer->stop();
            m_pauseBtn->setText("继续");
        }
    }
    
    void clearData() {
        m_plot->graph(0)->data()->clear();
        m_startTime = QDateTime::currentDateTime();
        m_plot->xAxis->setRange(0, 60);
        m_plot->replot();
    }
    
    void exportPNG() {
        QString filename = QString("trend_%1_%2.png")
            .arg(m_key)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        if (m_plot->savePng(filename, 1200, 800)) {
            QMessageBox::information(this, "导出成功", QString("趋势图已保存到: %1").arg(filename));
        }
    }

private:
    DataPoolClient* m_client;
    uint64_t m_key;
    bool m_running;
    QDateTime m_startTime;
    
    QCustomPlot* m_plot;
    QTimer* m_timer;
    
    QSpinBox* m_intervalSpin;
    QSpinBox* m_pointsSpin;
    QPushButton* m_pauseBtn;
    QLabel* m_statsLabel;
};

/**
 * @brief UI进程主窗口
 */
class UIProcessWindow : public QMainWindow {
    Q_OBJECT

public:
    UIProcessWindow(QWidget* parent = nullptr) 
        : QMainWindow(parent), m_client(nullptr), m_processMonitor(nullptr) {
        m_processMonitor = new ProcessMonitor();
        setupUI();
        setupMenu();
    }
    
    ~UIProcessWindow() {
        if (m_client) {
            m_client->shutdown();
            delete m_client;
        }
        if (m_processMonitor) {
            delete m_processMonitor;
        }
    }

private:
    void setupUI() {
        setWindowTitle("UI进程 - 数据监控与操作");
        resize(1400, 900);
        
        QWidget* central = new QWidget(this);
        setCentralWidget(central);
        QVBoxLayout* mainLayout = new QVBoxLayout(central);
        
        // 顶部连接区
        QGroupBox* connGroup = new QGroupBox("连接配置", this);
        QHBoxLayout* connLayout = new QHBoxLayout(connGroup);
        
        connLayout->addWidget(new QLabel("数据池名称:"));
        m_poolNameEdit = new QLineEdit("/ipc_data_pool", this);
        connLayout->addWidget(m_poolNameEdit);
        
        m_connectBtn = new QPushButton("连接数据池", this);
        m_connectBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
        connLayout->addWidget(m_connectBtn);
        
        m_disconnectBtn = new QPushButton("断开连接", this);
        m_disconnectBtn->setEnabled(false);
        m_disconnectBtn->setStyleSheet("background-color: #f44336; color: white; font-weight: bold;");
        connLayout->addWidget(m_disconnectBtn);
        
        connLayout->addStretch();
        
        m_statusLabel = new QLabel("未连接", this);
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        connLayout->addWidget(m_statusLabel);
        
        mainLayout->addWidget(connGroup);
        
        // Tab页面
        QTabWidget* tabWidget = new QTabWidget(this);
        
        // ====== 数据监控页 ======
        QWidget* dataTab = new QWidget();
        QVBoxLayout* dataLayout = new QVBoxLayout(dataTab);
        
        // 统计信息
        QGridLayout* statsGrid = new QGridLayout();
        statsGrid->addWidget(new QLabel("YX点数:"), 0, 0);
        m_yxCountLabel = new QLabel("0");
        m_yxCountLabel->setStyleSheet("font-weight: bold; color: #F44336;");
        statsGrid->addWidget(m_yxCountLabel, 0, 1);
        
        statsGrid->addWidget(new QLabel("YC点数:"), 0, 2);
        m_ycCountLabel = new QLabel("0");
        m_ycCountLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
        statsGrid->addWidget(m_ycCountLabel, 0, 3);
        
        statsGrid->addWidget(new QLabel("DZ点数:"), 0, 4);
        m_dzCountLabel = new QLabel("0");
        m_dzCountLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
        statsGrid->addWidget(m_dzCountLabel, 0, 5);
        
        statsGrid->addWidget(new QLabel("YK点数:"), 0, 6);
        m_ykCountLabel = new QLabel("0");
        m_ykCountLabel->setStyleSheet("font-weight: bold; color: #9C27B0;");
        statsGrid->addWidget(m_ykCountLabel, 0, 7);
        
        statsGrid->addWidget(new QLabel("活跃进程:"), 1, 0);
        m_procCountLabel = new QLabel("0");
        m_procCountLabel->setStyleSheet("font-weight: bold; color: #009688;");
        statsGrid->addWidget(m_procCountLabel, 1, 1);
        
        statsGrid->addWidget(new QLabel("读取次数:"), 1, 2);
        m_readCountLabel = new QLabel("0");
        statsGrid->addWidget(m_readCountLabel, 1, 3);
        
        statsGrid->addWidget(new QLabel("写入次数:"), 1, 4);
        m_writeCountLabel = new QLabel("0");
        statsGrid->addWidget(m_writeCountLabel, 1, 5);
        
        // 系统监控信息
        statsGrid->addWidget(new QLabel("进程CPU:"), 2, 0);
        m_procCpuLabel = new QLabel("0.0%");
        m_procCpuLabel->setStyleSheet("font-weight: bold; color: #E91E63;");
        statsGrid->addWidget(m_procCpuLabel, 2, 1);
        
        statsGrid->addWidget(new QLabel("进程内存:"), 2, 2);
        m_procMemLabel = new QLabel("0 MB");
        m_procMemLabel->setStyleSheet("font-weight: bold; color: #673AB7;");
        statsGrid->addWidget(m_procMemLabel, 2, 3);
        
        statsGrid->addWidget(new QLabel("系统CPU:"), 2, 4);
        m_sysCpuLabel = new QLabel("0.0%");
        m_sysCpuLabel->setStyleSheet("font-weight: bold; color: #00BCD4;");
        statsGrid->addWidget(m_sysCpuLabel, 2, 5);
        
        statsGrid->addWidget(new QLabel("系统内存:"), 2, 6);
        m_sysMemLabel = new QLabel("0.0%");
        m_sysMemLabel->setStyleSheet("font-weight: bold; color: #8BC34A;");
        statsGrid->addWidget(m_sysMemLabel, 2, 7);
        
        statsGrid->addWidget(new QLabel("负载:"), 2, 8);
        m_loadAvgLabel = new QLabel("0.00");
        m_loadAvgLabel->setStyleSheet("font-weight: bold; color: #FF5722;");
        statsGrid->addWidget(m_loadAvgLabel, 2, 9);
        
        statsGrid->setColumnStretch(10, 1);
        dataLayout->addLayout(statsGrid);
        
        // 数据表格
        QSplitter* dataSplitter = new QSplitter(Qt::Horizontal, this);
        
        // YX数据表
        QGroupBox* yxGroup = new QGroupBox("YX (遥信) - 双击查看详情", this);
        QVBoxLayout* yxLayout = new QVBoxLayout(yxGroup);
        m_yxTable = new QTableWidget(this);
        m_yxTable->setColumnCount(6);
        m_yxTable->setHorizontalHeaderLabels({"索引", "Key", "值", "质量", "更新时间", "状态"});
        m_yxTable->horizontalHeader()->setStretchLastSection(true);
        m_yxTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        yxLayout->addWidget(m_yxTable);
        dataSplitter->addWidget(yxGroup);
        
        // YC数据表
        QGroupBox* ycGroup = new QGroupBox("YC (遥测) - 双击查看趋势图", this);
        QVBoxLayout* ycLayout = new QVBoxLayout(ycGroup);
        m_ycTable = new QTableWidget(this);
        m_ycTable->setColumnCount(6);
        m_ycTable->setHorizontalHeaderLabels({"索引", "Key", "值", "质量", "更新时间", "单位"});
        m_ycTable->horizontalHeader()->setStretchLastSection(true);
        m_ycTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ycLayout->addWidget(m_ycTable);
        dataSplitter->addWidget(ycGroup);
        
        dataSplitter->setSizes({500, 500});
        dataLayout->addWidget(dataSplitter);
        
        // 刷新控制
        QHBoxLayout* refreshLayout = new QHBoxLayout();
        refreshLayout->addWidget(new QLabel("刷新间隔:"));
        m_refreshIntervalSpin = new QSpinBox(this);
        m_refreshIntervalSpin->setRange(100, 10000);
        m_refreshIntervalSpin->setValue(500);
        refreshLayout->addWidget(m_refreshIntervalSpin);
        refreshLayout->addWidget(new QLabel("ms"));
        
        QPushButton* refreshBtn = new QPushButton("立即刷新", this);
        connect(refreshBtn, &QPushButton::clicked, this, &UIProcessWindow::refreshData);
        refreshLayout->addWidget(refreshBtn);
        refreshLayout->addStretch();
        dataLayout->addLayout(refreshLayout);
        
        tabWidget->addTab(dataTab, "数据监控");
        
        // ====== 遥控操作页 ======
        QWidget* ykTab = new QWidget();
        QVBoxLayout* ykLayout = new QVBoxLayout(ykTab);
        
        QHBoxLayout* ykCtrlLayout = new QHBoxLayout();
        QPushButton* addYKBtn = new QPushButton("新建遥控命令", this);
        addYKBtn->setStyleSheet("background-color: #2196F3; color: white; font-weight: bold;");
        connect(addYKBtn, &QPushButton::clicked, this, &UIProcessWindow::showYKDialog);
        ykCtrlLayout->addWidget(addYKBtn);
        ykCtrlLayout->addStretch();
        ykLayout->addLayout(ykCtrlLayout);
        
        m_ykTable = new QTableWidget(this);
        m_ykTable->setColumnCount(5);
        m_ykTable->setHorizontalHeaderLabels({"Key", "当前值", "控制值", "质量", "状态"});
        m_ykTable->horizontalHeader()->setStretchLastSection(true);
        ykLayout->addWidget(m_ykTable);
        
        tabWidget->addTab(ykTab, "遥控操作");
        
        // ====== SOE事件页 ======
        QWidget* soeTab = new QWidget();
        QVBoxLayout* soeLayout = new QVBoxLayout(soeTab);
        
        QHBoxLayout* soeCtrlLayout = new QHBoxLayout();
        soeCtrlLayout->addWidget(new QLabel("显示数量:"));
        m_soeCountSpin = new QSpinBox(this);
        m_soeCountSpin->setRange(10, 1000);
        m_soeCountSpin->setValue(100);
        soeCtrlLayout->addWidget(m_soeCountSpin);
        
        QPushButton* refreshSoeBtn = new QPushButton("刷新SOE", this);
        connect(refreshSoeBtn, &QPushButton::clicked, this, &UIProcessWindow::refreshSOE);
        soeCtrlLayout->addWidget(refreshSoeBtn);
        
        QPushButton* exportSoeBtn = new QPushButton("导出CSV", this);
        connect(exportSoeBtn, &QPushButton::clicked, this, [this]() {
            if (m_client) {
                QString filename = QString("soe_%1.csv")
                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
                if (m_client->exportSOEToCSV(filename.toUtf8().constData())) {
                    QMessageBox::information(this, "导出成功", 
                        QString("SOE已导出到: %1").arg(filename));
                }
            }
        });
        soeCtrlLayout->addWidget(exportSoeBtn);
        soeCtrlLayout->addStretch();
        soeLayout->addLayout(soeCtrlLayout);
        
        m_soeTable = new QTableWidget(this);
        m_soeTable->setColumnCount(8);
        m_soeTable->setHorizontalHeaderLabels({"时间", "点位", "类型", "旧值", "新值", "优先级", "质量", "来源"});
        m_soeTable->horizontalHeader()->setStretchLastSection(true);
        soeLayout->addWidget(m_soeTable);
        
        // SOE统计
        QGridLayout* soeStatsGrid = new QGridLayout();
        soeStatsGrid->addWidget(new QLabel("总记录数:"), 0, 0);
        m_soeTotalLabel = new QLabel("0");
        soeStatsGrid->addWidget(m_soeTotalLabel, 0, 1);
        
        soeStatsGrid->addWidget(new QLabel("丢弃数:"), 0, 2);
        m_soeDroppedLabel = new QLabel("0");
        soeStatsGrid->addWidget(m_soeDroppedLabel, 0, 3);
        
        soeStatsGrid->addWidget(new QLabel("缓冲区使用:"), 0, 4);
        m_soeLoadLabel = new QLabel("0%");
        soeStatsGrid->addWidget(m_soeLoadLabel, 0, 5);
        soeLayout->addLayout(soeStatsGrid);
        
        tabWidget->addTab(soeTab, "SOE事件");
        
        // ====== 表决监控页 ======
        QWidget* votingTab = new QWidget();
        QVBoxLayout* votingLayout = new QVBoxLayout(votingTab);
        
        QHBoxLayout* votingCtrlLayout = new QHBoxLayout();
        QPushButton* addVotingBtn = new QPushButton("添加表决组", this);
        connect(addVotingBtn, &QPushButton::clicked, this, &UIProcessWindow::addVotingGroup);
        votingCtrlLayout->addWidget(addVotingBtn);
        
        QPushButton* execVotingBtn = new QPushButton("执行表决", this);
        connect(execVotingBtn, &QPushButton::clicked, this, &UIProcessWindow::executeVoting);
        votingCtrlLayout->addWidget(execVotingBtn);
        votingCtrlLayout->addStretch();
        votingLayout->addLayout(votingCtrlLayout);
        
        m_votingTable = new QTableWidget(this);
        m_votingTable->setColumnCount(8);
        m_votingTable->setHorizontalHeaderLabels({"组ID", "名称", "源A", "源B", "源C", "结果", "值", "告警"});
        m_votingTable->horizontalHeader()->setStretchLastSection(true);
        votingLayout->addWidget(m_votingTable);
        
        tabWidget->addTab(votingTab, "三取二表决");
        
        // ====== 进程状态页 ======
        QWidget* procTab = new QWidget();
        QVBoxLayout* procLayout = new QVBoxLayout(procTab);
        
        m_procTable = new QTableWidget(this);
        m_procTable->setColumnCount(6);
        m_procTable->setHorizontalHeaderLabels({"进程ID", "PID", "名称", "最后心跳", "健康状态", "操作"});
        m_procTable->horizontalHeader()->setStretchLastSection(true);
        procLayout->addWidget(m_procTable);
        
        tabWidget->addTab(procTab, "进程状态");
        
        mainLayout->addWidget(tabWidget);
        
        // 底部日志
        QGroupBox* logGroup = new QGroupBox("操作日志", this);
        QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
        m_logEdit = new QTextEdit(this);
        m_logEdit->setReadOnly(true);
        m_logEdit->setMaximumHeight(120);
        m_logEdit->setStyleSheet("font-family: Consolas; font-size: 10px;");
        logLayout->addWidget(m_logEdit);
        mainLayout->addWidget(logGroup);
        
        // 连接信号
        connect(m_connectBtn, &QPushButton::clicked, this, &UIProcessWindow::connectToPool);
        connect(m_disconnectBtn, &QPushButton::clicked, this, &UIProcessWindow::disconnectFromPool);
        
        // 双击YC表显示趋势图
        connect(m_ycTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
            Q_UNUSED(col);
            if (!m_client) return;
            
            QTableWidgetItem* item = m_ycTable->item(row, 1);
            if (!item) return;
            
            QString keyText = item->text();
            auto parts = keyText.split(':');
            if (parts.size() != 2) return;
            
            uint64_t key = makeKey(parts[0].toInt(), parts[1].toInt());
            
            // 创建趋势图对话框
            TrendDialog* dialog = new TrendDialog(m_client, key, this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            
            appendLog(QString("打开趋势图: %1").arg(keyText));
        });
        
        // 定时刷新
        m_refreshTimer = new QTimer(this);
        connect(m_refreshTimer, &QTimer::timeout, this, &UIProcessWindow::refreshData);
    }
    
    void setupMenu() {
        QMenu* fileMenu = menuBar()->addMenu("文件(&F)");
        
        QAction* saveSnapshotAction = new QAction("保存快照(&S)", this);
        saveSnapshotAction->setShortcut(QKeySequence::Save);
        connect(saveSnapshotAction, &QAction::triggered, this, [this]() {
            if (m_client) {
                QString filename = QString("snapshot_%1.bin")
                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
                if (m_client->saveSnapshot(filename.toUtf8().constData())) {
                    QMessageBox::information(this, "成功", QString("快照已保存: %1").arg(filename));
                }
            }
        });
        fileMenu->addAction(saveSnapshotAction);
        
        QAction* loadSnapshotAction = new QAction("加载快照(&L)", this);
        connect(loadSnapshotAction, &QAction::triggered, this, [this]() {
            // 简化实现
            QMessageBox::information(this, "提示", "请选择快照文件加载");
        });
        fileMenu->addAction(loadSnapshotAction);
        
        fileMenu->addSeparator();
        QAction* exitAction = new QAction("退出(&X)", this);
        exitAction->setShortcut(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
        fileMenu->addAction(exitAction);
        
        QMenu* helpMenu = menuBar()->addMenu("帮助(&H)");
        QAction* aboutAction = new QAction("关于(&A)", this);
        connect(aboutAction, &QAction::triggered, this, [this]() {
            QMessageBox::about(this, "关于", 
                "UI进程 - 数据监控与操作\n\n"
                "实时数据监控、遥控操作、SOE查看\n"
                "趋势图使用QCustomPlot\n"
                "版本: 1.1.0");
        });
        helpMenu->addAction(aboutAction);
    }

private slots:
    void connectToPool() {
        if (m_client) {
            appendLog("已经连接");
            return;
        }
        
        DataPoolClient::Config config;
        config.poolName = m_poolNameEdit->text().toStdString();
        config.eventName = "/ipc_events";
        config.soeName = "/ipc_soe";
        config.processName = "ui_process";
        config.create = false;
        config.enableSOE = true;
        config.enableVoting = true;
        
        m_client = DataPoolClient::init(config);
        if (!m_client) {
            appendLog("错误: 连接数据池失败!");
            QMessageBox::critical(this, "错误", "连接数据池失败!\n请确保通信进程已启动。");
            return;
        }
        
        m_client->startHeartbeat(1000);
        
        // 订阅YK反馈事件
        m_ykSubId = m_client->subscribe([this](const Event& event) {
            if (event.pointType == PointType::YK) {
                QMetaObject::invokeMethod(this, [this, event]() {
                    handleYKFeedback(event);
                });
            }
        });
        
        if (m_ykSubId != INVALID_INDEX) {
            appendLog(QString("YK事件订阅成功, ID: %1").arg(m_ykSubId));
        }
        
        m_connectBtn->setEnabled(false);
        m_disconnectBtn->setEnabled(true);
        m_poolNameEdit->setEnabled(false);
        
        m_statusLabel->setText("已连接");
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        
        m_refreshTimer->start(m_refreshIntervalSpin->value());
        
        appendLog("已连接到数据池: " + m_poolNameEdit->text());
        refreshData();
        refreshSOE();
    }
    
    void disconnectFromPool() {
        m_refreshTimer->stop();
        
        if (m_client) {
            m_client->shutdown();
            delete m_client;
            m_client = nullptr;
        }
        
        m_connectBtn->setEnabled(true);
        m_disconnectBtn->setEnabled(false);
        m_poolNameEdit->setEnabled(true);
        
        m_statusLabel->setText("未连接");
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        
        appendLog("已断开连接");
    }
    
    void refreshData() {
        if (!m_client) return;
        
        // 处理YK事件
        if (m_ykSubId != INVALID_INDEX) {
            m_client->processEvents(m_ykSubId, 10);
        }
        
        SharedDataPool* pool = m_client->getDataPool();
        const ShmHeader* header = pool->getHeader();
        
        // 更新统计
        m_yxCountLabel->setText(QString::number(header->yxCount));
        m_ycCountLabel->setText(QString::number(header->ycCount));
        m_dzCountLabel->setText(QString::number(header->dzCount));
        m_ykCountLabel->setText(QString::number(header->ykCount));
        
        DataPoolStats stats = m_client->getStats();
        m_readCountLabel->setText(QString::number(stats.totalReads));
        m_writeCountLabel->setText(QString::number(stats.totalWrites));
        m_procCountLabel->setText(QString::number(stats.activeProcessCount));
        
        // 更新系统监控信息
        updateSystemMonitor();
        
        // 更新YX表格
        uint32_t yxCount = std::min(50u, header->yxCount);
        m_yxTable->setRowCount(yxCount);
        for (uint32_t i = 0; i < yxCount; i++) {
            uint8_t value, quality;
            m_client->getYXByIndex(i, value, quality);
            
            m_yxTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            m_yxTable->setItem(i, 1, new QTableWidgetItem(QString("1:%1").arg(i)));
            
            QTableWidgetItem* valueItem = new QTableWidgetItem(QString::number(value));
            valueItem->setBackground(value ? QBrush(QColor("#C8E6C9")) : QBrush(QColor("#FFCDD2")));
            valueItem->setTextAlignment(Qt::AlignCenter);
            m_yxTable->setItem(i, 2, valueItem);
            
            m_yxTable->setItem(i, 3, new QTableWidgetItem(QString::number(quality)));
            m_yxTable->setItem(i, 4, new QTableWidgetItem("-"));
            
            QString status = quality == 0 ? "正常" : "异常";
            m_yxTable->setItem(i, 5, new QTableWidgetItem(status));
        }
        
        // 更新YC表格
        uint32_t ycCount = std::min(50u, header->ycCount);
        m_ycTable->setRowCount(ycCount);
        for (uint32_t i = 0; i < ycCount; i++) {
            float value;
            uint8_t quality;
            m_client->getYCByIndex(i, value, quality);
            
            m_ycTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            m_ycTable->setItem(i, 1, new QTableWidgetItem(QString("2:%1").arg(i)));
            
            QTableWidgetItem* valueItem = new QTableWidgetItem(QString::number(value, 'f', 2));
            m_ycTable->setItem(i, 2, valueItem);
            
            m_ycTable->setItem(i, 3, new QTableWidgetItem(QString::number(quality)));
            m_ycTable->setItem(i, 4, new QTableWidgetItem("-"));
            m_ycTable->setItem(i, 5, new QTableWidgetItem("-"));
        }
        
        // 更新YK表格
        uint32_t ykCount = std::min(30u, header->ykCount);
        m_ykTable->setRowCount(ykCount);
        for (uint32_t i = 0; i < ykCount; i++) {
            uint8_t value, quality;
            m_client->getYK(makeKey(4, i), value, quality);
            
            m_ykTable->setItem(i, 0, new QTableWidgetItem(QString("4:%1").arg(i)));
            m_ykTable->setItem(i, 1, new QTableWidgetItem(QString::number(value)));
            
            // 根据值显示控制状态
            QString controlValue = value ? "合" : "分";
            m_ykTable->setItem(i, 2, new QTableWidgetItem(controlValue));
            
            // 质量显示
            QString qualityStr = (quality == 0) ? "良好" : QString("异常(%1)").arg(quality);
            m_ykTable->setItem(i, 3, new QTableWidgetItem(qualityStr));
            
            // 状态显示
            QString status = (quality == 0) ? "已反馈" : "待反馈";
            m_ykTable->setItem(i, 4, new QTableWidgetItem(status));
        }
        
        // 更新进程表格
        int row = 0;
        m_procTable->setRowCount(MAX_PROCESS_COUNT);
        for (uint32_t i = 0; i < MAX_PROCESS_COUNT; i++) {
            ProcessInfo info;
            if (pool->getProcessInfo(i, info) == Result::OK && info.active) {
                m_procTable->setItem(row, 0, new QTableWidgetItem(QString::number(i)));
                m_procTable->setItem(row, 1, new QTableWidgetItem(QString::number(info.pid)));
                m_procTable->setItem(row, 2, new QTableWidgetItem(info.name));
                m_procTable->setItem(row, 3, new QTableWidgetItem(QString::number(info.lastHeartbeat)));
                
                ProcessHealth health = m_client->checkProcessHealth(i);
                QString healthStr;
                QColor healthColor;
                switch (health) {
                    case ProcessHealth::HEALTHY:
                        healthStr = "健康";
                        healthColor = QColor("#4CAF50");
                        break;
                    case ProcessHealth::WARNING:
                        healthStr = "警告";
                        healthColor = QColor("#FF9800");
                        break;
                    case ProcessHealth::DEAD:
                        healthStr = "死亡";
                        healthColor = QColor("#F44336");
                        break;
                    default:
                        healthStr = "未知";
                        healthColor = QColor("#9E9E9E");
                }
                QTableWidgetItem* healthItem = new QTableWidgetItem(healthStr);
                healthItem->setBackground(QBrush(healthColor));
                healthItem->setForeground(QBrush(Qt::white));
                m_procTable->setItem(row, 4, healthItem);
                m_procTable->setItem(row, 5, new QTableWidgetItem("-"));
                row++;
            }
        }
        m_procTable->setRowCount(row);
    }
    
    void updateSystemMonitor() {
        if (!m_processMonitor) return;
        
        // 获取进程资源信息
        ProcessResourceInfo procInfo;
        if (m_processMonitor->getProcessInfo(procInfo)) {
            m_procCpuLabel->setText(QString("%1%").arg(procInfo.cpuPercent, 0, 'f', 1));
            m_procMemLabel->setText(QString::fromStdString(
                ProcessMonitor::formatMemory(procInfo.vmRSS)));
            
            // CPU使用率高时改变颜色
            if (procInfo.cpuPercent > 80) {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #F44336;");
            } else if (procInfo.cpuPercent > 50) {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
            } else {
                m_procCpuLabel->setStyleSheet("font-weight: bold; color: #E91E63;");
            }
        }
        
        // 获取系统资源信息
        SystemResourceInfo sysInfo;
        if (m_processMonitor->getSystemInfo(sysInfo)) {
            m_sysCpuLabel->setText(QString("%1%").arg(sysInfo.cpuUsage, 0, 'f', 1));
            m_sysMemLabel->setText(QString("%1%").arg(sysInfo.memoryUsage, 0, 'f', 1));
            m_loadAvgLabel->setText(QString("%1 / %2 / %3")
                .arg(sysInfo.loadAvg1, 0, 'f', 2)
                .arg(sysInfo.loadAvg5, 0, 'f', 2)
                .arg(sysInfo.loadAvg15, 0, 'f', 2));
            
            // 系统负载高时改变颜色
            if (sysInfo.loadAvg1 > sysInfo.cpuCores) {
                m_loadAvgLabel->setStyleSheet("font-weight: bold; color: #F44336;");
            } else if (sysInfo.loadAvg1 > sysInfo.cpuCores * 0.7) {
                m_loadAvgLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
            } else {
                m_loadAvgLabel->setStyleSheet("font-weight: bold; color: #FF5722;");
            }
        }
    }
    
    void refreshSOE() {
        if (!m_client) return;
        
        uint32_t count = m_soeCountSpin->value();
        std::vector<SOERecord> records(count);
        uint32_t actualCount = 0;
        
        if (m_client->getLatestSOE(count, records.data(), actualCount)) {
            m_soeTable->setRowCount(actualCount);
            for (uint32_t i = 0; i < actualCount; i++) {
                const SOERecord& r = records[i];
                
                QString timeStr = QDateTime::fromMSecsSinceEpoch(r.absoluteTime / 1000000)
                    .toString("hh:mm:ss.zzz");
                m_soeTable->setItem(i, 0, new QTableWidgetItem(timeStr));
                m_soeTable->setItem(i, 1, new QTableWidgetItem(QString::number(r.pointKey)));
                
                QString typeStr;
                switch (static_cast<SOEEventType>(r.eventType)) {
                    case SOEEventType::YX_CHANGE: typeStr = "YX变位"; break;
                    case SOEEventType::YK_EXECUTE: typeStr = "YK执行"; break;
                    case SOEEventType::YK_FEEDBACK: typeStr = "YK返校"; break;
                    case SOEEventType::ALARM_TRIGGER: typeStr = "告警触发"; break;
                    case SOEEventType::ALARM_CLEAR: typeStr = "告警复归"; break;
                    case SOEEventType::PROTECTION_ACT: typeStr = "保护动作"; break;
                    default: typeStr = "其他";
                }
                m_soeTable->setItem(i, 2, new QTableWidgetItem(typeStr));
                m_soeTable->setItem(i, 3, new QTableWidgetItem(QString::number(r.oldValue)));
                m_soeTable->setItem(i, 4, new QTableWidgetItem(QString::number(r.newValue)));
                m_soeTable->setItem(i, 5, new QTableWidgetItem(QString::number(r.priority)));
                m_soeTable->setItem(i, 6, new QTableWidgetItem(QString::number(r.quality)));
                m_soeTable->setItem(i, 7, new QTableWidgetItem(QString::number(r.sourcePid)));
            }
        }
        
        SOEStats stats = m_client->getSOEStats();
        m_soeTotalLabel->setText(QString::number(stats.totalRecords));
        m_soeDroppedLabel->setText(QString::number(stats.droppedRecords));
        m_soeLoadLabel->setText(QString::number(stats.loadPercent, 'f', 1) + "%");
    }
    
    void showYKDialog() {
        YKDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            uint64_t key = dialog.getKey();
            uint8_t value = dialog.getValue();
            uint8_t quality = dialog.getQuality();
            
            if (key == 0) {
                QMessageBox::warning(this, "错误", "无效的点位Key");
                return;
            }
            
            if (m_client->setYK(key, value, quality)) {
                m_ykCmdSent++;
                appendLog(QString("遥控命令已发送: Key=%1, Value=%2, Quality=%3")
                    .arg(key).arg(value).arg(quality));
                appendLog(QString("等待comm_process反馈..."));
                QMessageBox::information(this, "成功", 
                    QString("遥控命令已发送\nKey: %1\n值: %2\n\n请查看YK表格状态或日志等待反馈")
                    .arg(key).arg(value ? "合" : "分"));
                
                // 发布YK事件，让comm_process接收处理
                m_client->publishEvent(key, PointType::YK, static_cast<uint32_t>(0), static_cast<uint32_t>(value));
                
                // 记录SOE
                m_client->recordSOEYKExecute(static_cast<uint32_t>(key), value);
            } else {
                QMessageBox::warning(this, "错误", "遥控命令发送失败");
            }
        }
    }
    
    void handleYKFeedback(const Event& event) {
        m_ykCmdFeedback++;
        appendLog(QString("收到YK反馈: Key=%1, 新值=%2")
            .arg(event.key)
            .arg(event.newValue.intValue));
        
        // 刷新YK表格
        refreshData();
    }
    
    void addVotingGroup() {
        if (!m_client) return;
        
        // 简化：添加一个默认表决组
        VotingConfig config;
        config.groupId = m_votingTable->rowCount();
        snprintf(config.name, sizeof(config.name), "表决组%d", config.groupId);
        config.sourceKeyA = makeKey(1, 0);
        config.sourceKeyB = makeKey(1, 1);
        config.sourceKeyC = makeKey(1, 2);
        config.sourceType = 0;  // YX
        config.votingStrategy = 0;  // 严格三取二
        config.enableDeviation = 1;
        config.deviationLimit = 0.0f;
        
        uint32_t groupId = m_client->addVotingGroup(config);
        if (groupId != INVALID_INDEX) {
            appendLog(QString("添加表决组: %1").arg(groupId));
            refreshVoting();
        }
    }
    
    void executeVoting() {
        if (!m_client) return;
        
        VotingEngine* engine = m_client->getVotingEngine();
        if (!engine) {
            QMessageBox::warning(this, "错误", "表决引擎未启用");
            return;
        }
        
        int row = m_votingTable->currentRow();
        if (row < 0) {
            QMessageBox::warning(this, "提示", "请先选择一个表决组");
            return;
        }
        
        uint32_t groupId = m_votingTable->item(row, 0)->text().toUInt();
        VotingOutput output;
        
        if (m_client->performVotingYX(groupId, output)) {
            QString resultStr;
            switch (static_cast<VotingResult>(output.result)) {
                case VotingResult::UNANIMOUS: resultStr = "一致"; break;
                case VotingResult::MAJORITY: resultStr = "多数"; break;
                case VotingResult::DISAGREE: resultStr = "不一致"; break;
                default: resultStr = "失败";
            }
            
            m_votingTable->setItem(row, 5, new QTableWidgetItem(resultStr));
            m_votingTable->setItem(row, 6, new QTableWidgetItem(QString::number(output.yxValue)));
            m_votingTable->setItem(row, 7, new QTableWidgetItem(QString::number(output.alarmFlags)));
            
            appendLog(QString("表决结果: 组%1 = %2 (值:%3)")
                .arg(groupId).arg(resultStr).arg(output.yxValue));
        }
    }
    
    void refreshVoting() {
        if (!m_client) return;
        
        VotingEngine* engine = m_client->getVotingEngine();
        if (!engine) return;
        
        uint32_t count = engine->getVotingGroupCount();
        m_votingTable->setRowCount(count);
        
        for (uint32_t i = 0; i < count; i++) {
            VotingConfig config;
            if (engine->getVotingGroupConfig(i, config)) {
                m_votingTable->setItem(i, 0, new QTableWidgetItem(QString::number(config.groupId)));
                m_votingTable->setItem(i, 1, new QTableWidgetItem(config.name));
                m_votingTable->setItem(i, 2, new QTableWidgetItem(QString::number(config.sourceKeyA)));
                m_votingTable->setItem(i, 3, new QTableWidgetItem(QString::number(config.sourceKeyB)));
                m_votingTable->setItem(i, 4, new QTableWidgetItem(QString::number(config.sourceKeyC)));
                m_votingTable->setItem(i, 5, new QTableWidgetItem("-"));
                m_votingTable->setItem(i, 6, new QTableWidgetItem("-"));
                m_votingTable->setItem(i, 7, new QTableWidgetItem("-"));
            }
        }
    }
    
    void appendLog(const QString& msg) {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        m_logEdit->append(QString("[%1] %2").arg(timestamp, msg));
    }

private:
    DataPoolClient* m_client;
    uint32_t m_ykSubId = INVALID_INDEX;
    uint32_t m_ykCmdSent = 0;
    uint32_t m_ykCmdFeedback = 0;
    
    // 系统监控
    ProcessMonitor* m_processMonitor;
    
    QLineEdit* m_poolNameEdit;
    QPushButton* m_connectBtn;
    QPushButton* m_disconnectBtn;
    QLabel* m_statusLabel;
    
    QLabel* m_yxCountLabel;
    QLabel* m_ycCountLabel;
    QLabel* m_dzCountLabel;
    QLabel* m_ykCountLabel;
    QLabel* m_procCountLabel;
    QLabel* m_readCountLabel;
    QLabel* m_writeCountLabel;
    
    // 系统监控标签
    QLabel* m_procCpuLabel;
    QLabel* m_procMemLabel;
    QLabel* m_sysCpuLabel;
    QLabel* m_sysMemLabel;
    QLabel* m_loadAvgLabel;
    
    QLabel* m_soeTotalLabel;
    QLabel* m_soeDroppedLabel;
    QLabel* m_soeLoadLabel;
    
    QTableWidget* m_yxTable;
    QTableWidget* m_ycTable;
    QTableWidget* m_ykTable;
    QTableWidget* m_soeTable;
    QTableWidget* m_votingTable;
    QTableWidget* m_procTable;
    
    QTextEdit* m_logEdit;
    
    QSpinBox* m_refreshIntervalSpin;
    QSpinBox* m_soeCountSpin;
    QTimer* m_refreshTimer;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("ui_process");
    QApplication::setApplicationVersion("1.1");
    QApplication::setStyle("Fusion");
    
    UIProcessWindow window;
    window.show();
    
    return app.exec();
}

#include "main.moc"
