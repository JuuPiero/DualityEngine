#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
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
        // Monotonic id (1, 2, 3...) -- lets the Console track "new since" (Error Pause, selection)
        // even after the bounded buffer drops its oldest entries.
        uint64_t Serial = 0;
    };

    // Minimal in-memory log, mirroring MyGameEngine's Core/Log.h -- spdlog
    // was considered but MyGameEngine itself settled on this simpler
    // approach specifically because the editor's future Console panel needs
    // to enumerate recent entries directly, which a plain logging library
    // sink wouldn't give for free. Revisit only if a real need (file
    // persistence, structured logging) outgrows this.
    //
    // Mutex-guarded: BuildPipeline's 3DS build runs on a background thread and
    // logs progress/errors from it, while ConsolePanel reads GetEntries() every
    // frame on the main thread -- without a lock, that's a concurrent
    // read/write on the same std::deque (real UB, not just a display glitch).
    class Log {
    public:
        static void Trace(const std::string& message);
        static void Info(const std::string& message);
        static void Warn(const std::string& message);
        static void Error(const std::string& message);

        // By value (a snapshot under the lock), not by reference -- a reference
        // to the live deque would let the caller iterate it unguarded after this
        // function returns, exactly the race this class exists to prevent.
        static std::deque<LogEntry> GetEntries() {
            std::lock_guard<std::mutex> lock(s_Mutex);
            return s_Entries;
        }
        static void Clear() {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_Entries.clear();
        }
        // Number of messages ever logged at `level` (not reset by Clear) -- the editor compares
        // successive values to react to new errors (Console "Error Pause").
        static uint64_t GetTotalCount(LogLevel level) {
            std::lock_guard<std::mutex> lock(s_Mutex);
            return s_TotalByLevel[static_cast<int>(level)];
        }

    private:
        static void Push(LogLevel level, const std::string& message);

        static std::mutex s_Mutex;
        static std::deque<LogEntry> s_Entries;
        static uint64_t s_Serial;
        static uint64_t s_TotalByLevel[4];
        static const size_t s_MaxEntries = 500;
    };

}
