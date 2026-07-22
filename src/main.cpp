#include "someip_daemon.h"
#include <iostream>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::cout << "TBOX-SOMEIP Service v1.0.0" << std::endl;
    
    tbox::someip::SomeipDaemon daemon;
    
    // 初始化守护进程
    if (!daemon.initialize()) {
        std::cerr << "Failed to initialize daemon" << std::endl;
        return 1;
    }
    
    // 启动守护进程
    if (!daemon.start()) {
        std::cerr << "Failed to start daemon" << std::endl;
        return 1;
    }
    
    std::cout << "Daemon running. Press Ctrl+C to stop." << std::endl;
    
    // 主循环
    while (daemon.isRunning()) {
        sleep(1);
    }
    
    return 0;
}
