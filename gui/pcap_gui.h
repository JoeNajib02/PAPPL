#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QString>

class PcapGUI : public QMainWindow {
    Q_OBJECT

public:
    PcapGUI(QWidget *parent = nullptr);
    ~PcapGUI();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onBrowsePcap();
    void onBrowseJson();
    void onProcessFiles();
    void onClear();

private:
    void setupUI();
    void updateStatus(const QString &message);
    void displayResults(const QString &results);
    bool validateFiles();

    // UI Elements
    QLabel *pcapLabel;
    QLabel *jsonLabel;
    QLabel *pcapPathDisplay;
    QLabel *jsonPathDisplay;
    QPushButton *browsePcapBtn;
    QPushButton *browseJsonBtn;
    QPushButton *processBtn;
    QPushButton *clearBtn;
    QTextEdit *resultDisplay;
    QLabel *statusLabel;

    // Data
    QString pcapPath;
    QString jsonPath;
};
