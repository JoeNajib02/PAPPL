#include "pcap_gui.h"
#include "../src/pcap_data_manipulator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QGroupBox>
#include <QMessageBox>
#include <QScrollArea>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <sstream>
#include <iomanip>

using namespace ouster;
using namespace ouster::sensor_utils;

PcapGUI::PcapGUI(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("Ouster PCAP Manipulator");
    setGeometry(100, 100, 900, 700);
    setAcceptDrops(true);
    setupUI();
}

PcapGUI::~PcapGUI() {}

void PcapGUI::setupUI() {
    // Central widget
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Title
    QLabel *titleLabel = new QLabel("Ouster PCAP Data Processor");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);

    // File selection group
    QGroupBox *fileGroup = new QGroupBox("Input Files");
    QVBoxLayout *fileLayout = new QVBoxLayout();

    // PCAP file section
    pcapLabel = new QLabel("PCAP File:");
    pcapPathDisplay = new QLabel("(Drag & drop or browse)");
    pcapPathDisplay->setStyleSheet("color: gray; padding: 8px; background: #f0f0f0; border-radius: 4px;");
    browsePcapBtn = new QPushButton("Browse PCAP");
    connect(browsePcapBtn, &QPushButton::clicked, this, &PcapGUI::onBrowsePcap);

    QHBoxLayout *pcapLayout = new QHBoxLayout();
    pcapLayout->addWidget(pcapPathDisplay, 1);
    pcapLayout->addWidget(browsePcapBtn);

    fileLayout->addWidget(pcapLabel);
    fileLayout->addLayout(pcapLayout);

    // JSON file section
    jsonLabel = new QLabel("JSON Metadata File:");
    jsonPathDisplay = new QLabel("(Drag & drop or browse)");
    jsonPathDisplay->setStyleSheet("color: gray; padding: 8px; background: #f0f0f0; border-radius: 4px;");
    browseJsonBtn = new QPushButton("Browse JSON");
    connect(browseJsonBtn, &QPushButton::clicked, this, &PcapGUI::onBrowseJson);

    QHBoxLayout *jsonLayout = new QHBoxLayout();
    jsonLayout->addWidget(jsonPathDisplay, 1);
    jsonLayout->addWidget(browseJsonBtn);

    fileLayout->addWidget(jsonLabel);
    fileLayout->addLayout(jsonLayout);

    fileGroup->setLayout(fileLayout);
    mainLayout->addWidget(fileGroup);

    // Status label
    statusLabel = new QLabel("Ready to process files");
    statusLabel->setStyleSheet("color: blue; padding: 8px;");
    mainLayout->addWidget(statusLabel);

    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    processBtn = new QPushButton("Process Files");
    processBtn->setStyleSheet("background-color: #4CAF50; color: white; padding: 10px; font-weight: bold;");
    connect(processBtn, &QPushButton::clicked, this, &PcapGUI::onProcessFiles);

    clearBtn = new QPushButton("Clear");
    clearBtn->setStyleSheet("background-color: #f44336; color: white; padding: 10px;");
    connect(clearBtn, &QPushButton::clicked, this, &PcapGUI::onClear);

    buttonLayout->addStretch();
    buttonLayout->addWidget(processBtn);
    buttonLayout->addWidget(clearBtn);
    mainLayout->addLayout(buttonLayout);

    // Results display
    QLabel *resultsLabel = new QLabel("Processing Results:");
    mainLayout->addWidget(resultsLabel);

    resultDisplay = new QTextEdit();
    resultDisplay->setReadOnly(true);
    resultDisplay->setStyleSheet("font-family: monospace; background: #f9f9f9; border: 1px solid #ddd;");
    mainLayout->addWidget(resultDisplay);

    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);
}

void PcapGUI::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void PcapGUI::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();

    if (mimeData->hasUrls()) {
        QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            QString filePath = url.toLocalFile();
            if (filePath.endsWith(".pcap")) {
                pcapPath = filePath;
                pcapPathDisplay->setText(filePath);
                pcapPathDisplay->setStyleSheet("color: green; padding: 8px; background: #f0f0f0; border-radius: 4px;");
            } else if (filePath.endsWith(".json")) {
                jsonPath = filePath;
                jsonPathDisplay->setText(filePath);
                jsonPathDisplay->setStyleSheet("color: green; padding: 8px; background: #f0f0f0; border-radius: 4px;");
            }
        }
        event->acceptProposedAction();
    }
}

void PcapGUI::onBrowsePcap() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Select PCAP File", "",
        "PCAP Files (*.pcap);;All Files (*)");
    if (!fileName.isEmpty()) {
        pcapPath = fileName;
        pcapPathDisplay->setText(fileName);
        pcapPathDisplay->setStyleSheet("color: green; padding: 8px; background: #f0f0f0; border-radius: 4px;");
    }
}

void PcapGUI::onBrowseJson() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Select JSON Metadata File", "",
        "JSON Files (*.json);;All Files (*)");
    if (!fileName.isEmpty()) {
        jsonPath = fileName;
        jsonPathDisplay->setText(fileName);
        jsonPathDisplay->setStyleSheet("color: green; padding: 8px; background: #f0f0f0; border-radius: 4px;");
    }
}

bool PcapGUI::validateFiles() {
    if (pcapPath.isEmpty()) {
        updateStatus("Error: PCAP file not selected");
        return false;
    }
    if (jsonPath.isEmpty()) {
        updateStatus("Error: JSON file not selected");
        return false;
    }
    return true;
}

void PcapGUI::onProcessFiles() {
    if (!validateFiles()) {
        return;
    }

    updateStatus("Processing...");
    processBtn->setEnabled(false);

    try {
        PcapDataManipulator manipulator;

        // Load metadata
        if (!manipulator.load_metadata(jsonPath.toStdString())) {
            updateStatus("Error: Failed to load metadata");
            processBtn->setEnabled(true);
            return;
        }

        // Load PCAP
        int numScans = manipulator.load_pcap(pcapPath.toStdString());
        if (numScans < 0) {
            updateStatus("Error: Failed to load PCAP file");
            processBtn->setEnabled(true);
            return;
        }

        // Build results
        std::stringstream ss;
        ss << "✓ Successfully loaded " << numScans << " scans\n\n";

        const auto& info = manipulator.get_sensor_info();
        ss << "=== Sensor Information ===\n";
        ss << "Serial: " << info.sn << "\n";
        ss << "Firmware: " << info.image_rev << "\n";
        ss << "Model: " << info.prod_line << "\n";
        ss << "Resolution: " << info.format.columns_per_frame << " x "
           << info.format.pixels_per_column << "\n\n";

        const auto& stats = manipulator.get_statistics();
        ss << "=== Data Statistics ===\n";
        ss << "Total Scans: " << stats.total_scans << "\n";
        ss << "Total Packets: " << stats.total_packets << "\n";
        ss << "Start Timestamp: " << stats.start_timestamp << "\n";
        ss << "End Timestamp: " << stats.end_timestamp << "\n\n";

        if (numScans > 0) {
            const auto& scan = manipulator.get_scan(0);
            ss << "=== First Scan Info ===\n";
            ss << "Frame ID: " << scan.frame_id << "\n";
            ss << "Dimensions: " << scan.w << " x " << scan.h << "\n\n";

            // Range filtering
            size_t filtered = manipulator.filter_by_range(5000);
            ss << "=== Filtering Results ===\n";
            ss << "Measurements filtered by range (< 5m): " << filtered << "\n\n";

            // Point cloud analysis
            try {
                auto point_cloud = manipulator.get_point_cloud(0);
                auto valid_points = manipulator.get_valid_points(0);
                ss << "=== Point Cloud Analysis ===\n";
                ss << "Valid points (range > 0): " << valid_points.size() << "\n";

                if (!valid_points.empty()) {
                    double min_x = valid_points[0].x(), max_x = min_x;
                    double min_y = valid_points[0].y(), max_y = min_y;
                    double min_z = valid_points[0].z(), max_z = min_z;

                    for (const auto& pt : valid_points) {
                        min_x = std::min(min_x, pt.x());
                        max_x = std::max(max_x, pt.x());
                        min_y = std::min(min_y, pt.y());
                        max_y = std::max(max_y, pt.y());
                        min_z = std::min(min_z, pt.z());
                        max_z = std::max(max_z, pt.z());
                    }

                    ss << std::fixed << std::setprecision(2);
                    ss << "X range: [" << min_x << ", " << max_x << "] meters\n";
                    ss << "Y range: [" << min_y << ", " << max_y << "] meters\n";
                    ss << "Z range: [" << min_z << ", " << max_z << "] meters\n";
                }
            } catch (const std::exception& e) {
                ss << "Point cloud analysis: " << e.what() << "\n";
            }
        }

        displayResults(QString::fromStdString(ss.str()));
        updateStatus("Processing complete!");

    } catch (const std::exception& e) {
        updateStatus(QString("Error: %1").arg(e.what()));
    }

    processBtn->setEnabled(true);
}

void PcapGUI::onClear() {
    pcapPath.clear();
    jsonPath.clear();
    pcapPathDisplay->setText("(Drag & drop or browse)");
    pcapPathDisplay->setStyleSheet("color: gray; padding: 8px; background: #f0f0f0; border-radius: 4px;");
    jsonPathDisplay->setText("(Drag & drop or browse)");
    jsonPathDisplay->setStyleSheet("color: gray; padding: 8px; background: #f0f0f0; border-radius: 4px;");
    resultDisplay->clear();
    updateStatus("Ready to process files");
}

void PcapGUI::updateStatus(const QString &message) {
    statusLabel->setText(message);
    QApplication::processEvents();
}

void PcapGUI::displayResults(const QString &results) {
    resultDisplay->setText(results);
}
