#include <QApplication>
#include "pcap_gui.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    PcapGUI window;
    window.show();
    
    return app.exec();
}
