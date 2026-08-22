#pragma once

#include <deque>
#include <string>

namespace Duality {

    enum class LogLevel {
        Trace = 0,
        Info,
        Warn,
        Error
    };

    struct LogEntry {
        LogLevel Level;
        std::string Message;
    };

    // Minimal in-memory log, mirroring MyGameEngine's Core/Log.h -- spdlog
    // was considered but MyGameEngine itself settled on this simpler
    // approach specifically because the editor's future Console panel needs
    // to enumerate recent entries directly, which a plain logging library
    // sink wouldn't give for free. Revisit only if a real need (file
    // persistence, structured logging) outgrows this.
    class Log {
    public:
        static void Trace(const std::string& message);
        static void Info(const std::string& message);
        static void Warn(const std::string& message);
        static void Error(const std::string& message);

        static const std::deque<LogEntry>& GetEntries() { return s_Entries; }
        static void Clear() { s_Entries.clear(); }

    private:
        static void Push(LogLevel level, const std::string& message);

        static std::deque<LogEntry> s_Entries;
        static const size_t s_MaxEntries = 500;
    };

}
