#pragma once

#include <array>
#include <chrono>
#include <cinttypes>
#include <optional>
#include <string>

namespace App {
struct Subject {
    std::string id;
    std::u8string name;
};

enum DayOfTheWeekFlag {
    Sunday = 0x1,
    Monday = 0x2,
    Tuesday = 0x4,
    Wednesday = 0x8,
    Thursday = 0x10,
    Friday = 0x20
};

typedef uint8_t DayOfTheWeekFlagSet;

constexpr DayOfTheWeekFlagSet getFromStdChronoWeekday(std::chrono::weekday wd) {
    return (DayOfTheWeekFlag)(1 << wd.c_encoding());
}

// Turma
struct Class {
    struct Times {
        std::chrono::minutes start;
        std::chrono::minutes finish;
    };

    Subject subject;
    std::u8string teacher;
    // Indexado de Domingo a Sábado.
    // Se um opcional for nulo, então não tem a aula da turma nesse dia
    std::array<std::optional<Times>, 7> dailyTimes;
    std::u8string classroom;
};

// Monitoria
struct Monitoring {
    struct Times {
        std::chrono::minutes start;
        std::chrono::minutes finish;
    };

    Subject subject;
    std::u8string monitor;
    std::chrono::weekday wd;
    Times times;
};
}  // namespace App