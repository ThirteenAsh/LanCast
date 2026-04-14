#include "Common/Logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

namespace lancast {
namespace {
std::mutex g_logger_mutex;
std::string g_log_file = "lancast.log";

std::ofstream openStreamLocked() {
    std::filesystem::path path(g_log_file);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    return std::ofstream(g_log_file, std::ios::out | std::ios::app);
}

std::string nowString() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto time = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}
}

void Logger::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_log_file = path.empty() ? std::string("lancast.log") : path;
    auto out = openStreamLocked();
    out << "===== log started " << nowString() << " =====" << std::endl;
}

std::string Logger::logFile() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    return g_log_file;
}

void Logger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    auto out = openStreamLocked();
    out << '[' << nowString() << "] [tid " << std::this_thread::get_id() << "] "
        << message << std::endl;
}

}  // namespace lancast
