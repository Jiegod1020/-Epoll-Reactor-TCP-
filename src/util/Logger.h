#pragma once
#include <iostream>
#include <string>
#include <ctime>
#include <cstring>
#include <cerrno>

namespace coreactor {

// 这些宏支持流式表达式，例如 LOG_INFO("fd=" << fd)。
// 宏中的 do/while(0) 保证它在 if/else 中仍表现为一条语句。
#define LOG_INFO(msg) do { \
    /* 生成本地时间戳并输出普通运行信息。 */ \
    time_t t = time(nullptr); \
    char buf[64]; \
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t)); \
    std::cout << "[INFO][" << buf << "] " << msg << std::endl; \
} while(0)

#define LOG_ERROR(msg) do { \
    /* 错误日志额外记录源文件、行号和当前线程的 errno 文本。 */ \
    time_t t = time(nullptr); \
    char buf[64]; \
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t)); \
    std::cerr << "[ERROR][" << buf << "][" << __FILE__ << ":" << __LINE__ << "] " \
              << msg << " | errno: " << strerror(errno) << std::endl; \
} while(0)

} // namespace coreactor
