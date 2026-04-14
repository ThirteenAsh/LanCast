#pragma once

#include <string>

namespace lancast {

class Logger {
public:
    static void setLogFile(const std::string& path);
    static std::string logFile();
    static void log(const std::string& message);
};

}  // namespace lancast
