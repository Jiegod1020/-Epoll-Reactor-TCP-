#pragma once
#include <iostream>
#include <string>
#include <ctime>
#include <cstring>

namespace coreactor {

#define LOG_INFO(msg) do { \
    time_t t = time(nullptr); \
    char buf[64]; \
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t)); \
    std::cout << "[INFO][" << buf << "] " << msg << std::endl; \
} while(0)

#define LOG_ERROR(msg) do { \
    time_t t = time(nullptr); \
    char buf[64]; \
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t)); \
    std::cerr << "[ERROR][" << buf << "][" << __FILE__ << ":" << __LINE__ << "] " \
              << msg << " | errno: " << strerror(errno) << std::endl; \
} while(0)

} // namespace coreactor
