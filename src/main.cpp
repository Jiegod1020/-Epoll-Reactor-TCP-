#include "net/TcpServer.h"
#include "util/Logger.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    // main 是程序入口。默认监听 8080 端口，并创建 4 个从 Reactor 工作线程。
    uint16_t port = 8080;
    int subLoopNum = 4;

    // argv[0] 是程序名，argv[1]（如果存在）是端口号。
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::strtoul(argv[1], nullptr, 10));
    }
    // argv[2]（如果存在）是从 Reactor 的数量；每个从 Reactor 对应一个线程。
    if (argc >= 3) {
        subLoopNum = std::atoi(argv[2]);
    }
    // 端口 0 和非正线程数都不是这里支持的配置，尽早提示正确用法。
    if (port == 0 || subLoopNum <= 0) {
        std::cerr << "usage: " << argv[0] << " [port] [sub_reactor_threads]\n";
        return EXIT_FAILURE;
    }

    try {
        // TcpServer 构造时建立主 EventLoop、Acceptor 和从 EventLoop 对象，
        // 但此时还没有进入事件循环。
        coreactor::TcpServer server(port, subLoopNum);

        // 将 SIGINT（Ctrl+C）和 SIGTERM 加入信号集合。
        sigset_t signalSet;
        sigemptyset(&signalSet);
        sigaddset(&signalSet, SIGINT);
        sigaddset(&signalSet, SIGTERM);
        // 在线程创建前屏蔽这些信号，使新线程也继承屏蔽状态。
        // 后面的 shutdownThread 会用 sigwait 在普通线程上下文中同步接收它们。
        pthread_sigmask(SIG_BLOCK, &signalSet, nullptr);

        // 注册业务消息回调。buffer 中是一条已经解码好的 DATA 帧载荷，
        // 本例读取它并调用 sendData 回显；sendData 会重新添加协议帧头。
        server.setMessageCallback(
            [](const coreactor::TcpConnection::ptr& connection,
               coreactor::Buffer* buffer) {
                const std::string message = buffer->retrieveAllAsString();
                connection->sendData(message);
            });

        // 注册新连接回调。它由 TcpServer 在 accept 后调用，可扩展认证、统计等逻辑。
        server.setConnectionCallback(
            [](const coreactor::TcpConnection::ptr& connection) {
                LOG_INFO("new connection, fd=" << connection->fd());
            });

        // 单独的信号线程阻塞等待退出信号。收到信号后调用 stop()，
        // stop() 会让各 EventLoop 退出并 join 从 Reactor 线程。
        std::thread shutdownThread([&server, signalSet]() {
            int signal = 0;
            sigwait(&signalSet, &signal);
            server.stop();
        });

        // start() 在当前线程运行主 Reactor，通常会一直运行到收到退出信号。
        server.start();
        // start 返回说明主 Reactor 已结束；等待信号线程也完成清理。
        shutdownThread.join();
    } catch (const std::exception& ex) {
        // 例如监听端口绑定失败时，构造或启动过程可能抛出异常。
        LOG_ERROR("server stopped: " << ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
