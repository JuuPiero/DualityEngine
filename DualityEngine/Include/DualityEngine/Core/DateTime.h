#pragma once

#include <ctime>

namespace Duality {

    // Real-world date/time, identical on both platforms with zero
    // platform-specific code -- plain <ctime> (time()/gmtime()) already
    // works on real 3DS hardware via libctru's own C runtime integration,
    // confirmed against References/devkitpro-3ds-templates/time/rtc.
    // Header-only (no engine-owned mutable state to keep in sync across a
    // DLL boundary, unlike Input/AudioEngine), so it's directly callable
    // from Behaviour scripts with no EngineServices entry needed.
    struct DateTime {
        int Year, Month, Day, Hour, Minute, Second, DayOfWeek;

        static DateTime Now() {
            std::time_t now = std::time(nullptr);
            std::tm* utc = std::gmtime(&now);
            return DateTime{
                utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
                utc->tm_hour, utc->tm_min, utc->tm_sec, utc->tm_wday
            };
        }
    };

}
