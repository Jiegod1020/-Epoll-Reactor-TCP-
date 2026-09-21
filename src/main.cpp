#include "net/TcpServer.h"
#include "util/Logger.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    // 命令行参数：端口号和从 Reactor 线程数。
    uint16_t port = 8080;
    int subLoopNum = 4;

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::strtoul(argv[1], nullptr, 10));
    }
    if (argc >= 3) {
        subLoopNum = std::atoi(argv[2]);
    }
    if (port == 0 || subLoopNum <= 0) {
        std::cerr << "usage: " << argv[0] << " [port] [sub_reactor_threads]\n";
        return EXIT_FAILURE;
    }

    try {
        coreactor::TcpServer server(port, subLoopNum);

        sigset_t signalSet;
        sigemptyset(&signalSet);
        sigaddset(&signalSet, SIGINT);
        sigaddset(&signalSet, SIGTERM);
        // 阻塞信号，使它们不会在任意工作线程中异步打断执行；
        // 下面专门的 shutdownThread 使用 sigwait 同步接收信号。
        pthread_sigmask(SIG_BLOCK, &signalSet, nullptr);

        // 示例业务：把收到的字节原样写回客户端，构成一个 TCP echo server。
        server.setMessageCallback(
            [](const coreactor::TcpConnection::ptr& connection,
               coreactor::Buffer* buffer) {
                const std::string message = buffer->retrieveAllAsString();
                connection->coWrite(message);
            });

        // 新连接建立时的生命周期通知，可用于认证、统计和连接初始化。
        server.setConnectionCallback(
            [](const coreactor::TcpConnection::ptr& connection) {
                LOG_INFO("new connection, fd=" << connection->fd());
            });

        // start() 阻塞在主 Reactor；信号线程收到退出信号后调用 stop()，
        // stop() 会通过 eventfd 唤醒主/从 EventLoop 并等待它们结束。
        std::thread shutdownThread([&server, signalSet]() {
            int signal = 0;
            sigwait(&signalSet, &signal);
            server.stop();
        });

        server.start();
        shutdownThread.join();
    } catch (const std::exception& ex) {
        LOG_ERROR("server stopped: " << ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
