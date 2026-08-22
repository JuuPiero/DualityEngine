#include "DualityEngine/Core/Log.h"

#include <iostream>

namespace Duality {

    std::deque<LogEntry> Log::s_Entries;

    static const char* LevelPrefix(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "[TRACE] ";
            case LogLevel::Info:  return "[INFO]  ";
            case LogLevel::Warn:  return "[WARN]  ";
            case LogLevel::Error: return "[ERROR] ";
        }
        return "";
    }

    void Log::Push(LogLevel level, const std::string& message) {
        s_Entries.push_back({ level, message });
        while (s_Entries.size() > s_MaxEntries)
            s_Entries.pop_front();

        std::ostream& stream = (level == LogLevel::Error) ? std::cerr : std::cout;
        stream << LevelPrefix(level) << message << std::endl;
    }

    void Log::Trace(const std::string& message) { Push(LogLevel::Trace, message); }
    void Log::Info(const std::string& message) { Push(LogLevel::Info, message); }
    void Log::Warn(const std::string& message) { Push(LogLevel::Warn, message); }
    void Log::Error(const std::string& message) { Push(LogLevel::Error, message); }

}
