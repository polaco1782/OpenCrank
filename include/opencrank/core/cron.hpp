// Minimal CRON parser for OpenCrank (supports minute, hour, day, month, weekday)
// Only supports '*' and single values for each field (no ranges, steps, or lists)
#pragma once
#include <string>
#include <ctime>
#include <vector>
#include <sstream>

namespace opencrank {

struct CronSchedule {
    int minute = -1;   // 0-59 or -1 for *
    int hour = -1;     // 0-23 or -1 for *
    int day = -1;      // 1-31 or -1 for *
    int month = -1;    // 1-12 or -1 for *
    int weekday = -1;  // 0-6 (Sun=0) or -1 for *

    static CronSchedule parse(const std::string& expr) {
        CronSchedule sched;
        std::istringstream iss(expr);
        std::string field;
        std::vector<std::string> fields;
        while (iss >> field) fields.push_back(field);
        if (fields.size() != 5) return sched;
        sched.minute = (fields[0] == "*") ? -1 : std::stoi(fields[0]);
        sched.hour = (fields[1] == "*") ? -1 : std::stoi(fields[1]);
        sched.day = (fields[2] == "*") ? -1 : std::stoi(fields[2]);
        sched.month = (fields[3] == "*") ? -1 : std::stoi(fields[3]);
        sched.weekday = (fields[4] == "*") ? -1 : std::stoi(fields[4]);
        return sched;
    }

    bool matches(const std::tm& tm) const {
        if (minute != -1 && minute != tm.tm_min) return false;
        if (hour != -1 && hour != tm.tm_hour) return false;
        if (day != -1 && day != tm.tm_mday) return false;
        if (month != -1 && month != (tm.tm_mon + 1)) return false;
        if (weekday != -1 && weekday != tm.tm_wday) return false;
        return true;
    }
};

} // namespace opencrank
