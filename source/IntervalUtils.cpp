#include "IntervalUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace App {
bool tryPartitionClasses(std::span<const Monitoring> mons,
                         std::span<std::u8string> pClassroomsByPriority,
                         std::u8string_view* pAssignedClassrooms, std::span<const Class> classes) {
    constexpr std::chrono::minutes dayLimit{24 * 60};

    if (pAssignedClassrooms == nullptr) {
        throw std::invalid_argument("pAssignedClassrooms nao pode ser nulo");
    }

    auto validateTimes = [](std::chrono::minutes start, std::chrono::minutes finish) {
        if (start >= dayLimit || finish >= dayLimit || start >= finish) {
            throw std::invalid_argument("horario invalido");
        }
    };

    auto overlaps = [](std::chrono::minutes aStart, std::chrono::minutes aFinish,
                       std::chrono::minutes bStart, std::chrono::minutes bFinish) {
        return aStart < bFinish && bStart < aFinish;
    };

    for (const Monitoring& mon : mons) {
        validateTimes(mon.times.start, mon.times.finish);
    }

    for (const Class& klass : classes) {
        for (const std::optional<Class::Times>& times : klass.dailyTimes) {
            if (times.has_value()) {
                validateTimes(times->start, times->finish);
            }
        }
    }

    struct MonWithIndex {
        size_t index;
        std::chrono::weekday wd;
        unsigned dayIndex;
        std::chrono::minutes start;
        std::chrono::minutes finish;
    };

    std::vector<MonWithIndex> orderedMons;
    orderedMons.reserve(mons.size());

    std::unordered_map<std::u8string_view, size_t> roomIndexByName;
    roomIndexByName.reserve(pClassroomsByPriority.size());
    for (size_t roomIndex = 0; roomIndex < pClassroomsByPriority.size(); ++roomIndex) {
        roomIndexByName.emplace(pClassroomsByPriority[roomIndex], roomIndex);
    }

    struct Interval {
        std::chrono::minutes start;
        std::chrono::minutes finish;
    };

    std::array<std::vector<std::vector<Interval>>, 7> classIntervalsByDay;
    for (unsigned day = 0; day < classIntervalsByDay.size(); ++day) {
        classIntervalsByDay[day].resize(pClassroomsByPriority.size());
    }

    for (size_t i = 0; i < mons.size(); ++i) {
        const unsigned dayIndex = mons[i].wd.c_encoding();
        if (dayIndex >= 7) {
            throw std::invalid_argument("dia da semana invalido");
        }
        orderedMons.push_back(MonWithIndex{i, mons[i].wd, dayIndex, mons[i].times.start, mons[i].times.finish});
    }

    for (const Class& klass : classes) {
        if (klass.classroom.empty()) {
            continue;
        }

        const auto roomIt = roomIndexByName.find(klass.classroom);
        if (roomIt == roomIndexByName.end()) {
            continue;
        }

        for (unsigned day = 0; day < klass.dailyTimes.size(); ++day) {
            const std::optional<Class::Times>& times = klass.dailyTimes[day];
            if (!times.has_value()) {
                continue;
            }

            classIntervalsByDay[day][roomIt->second].push_back(Interval{times->start, times->finish});
        }
    }

    for (unsigned day = 0; day < classIntervalsByDay.size(); ++day) {
        for (std::vector<Interval>& roomIntervals : classIntervalsByDay[day]) {
            std::sort(roomIntervals.begin(), roomIntervals.end(), [](const Interval& a, const Interval& b) {
                if (a.start != b.start) {
                    return a.start < b.start;
                }
                return a.finish < b.finish;
            });
        }
    }

    std::sort(orderedMons.begin(), orderedMons.end(), [](const MonWithIndex& a, const MonWithIndex& b) {
        if (a.wd.c_encoding() != b.wd.c_encoding()) {
            return a.wd.c_encoding() < b.wd.c_encoding();
        }
        if (a.start != b.start) {
            return a.start < b.start;
        }
        return a.finish < b.finish;
    });

    struct ActiveRoom {
        std::chrono::minutes finish;
        size_t roomIndex;
    };

    auto activeCmp = [](const ActiveRoom& a, const ActiveRoom& b) {
        if (a.finish != b.finish) {
            return a.finish > b.finish;
        }
        return a.roomIndex > b.roomIndex;
    };

    std::priority_queue<ActiveRoom, std::vector<ActiveRoom>, decltype(activeCmp)> activeRooms(activeCmp);
    std::priority_queue<size_t, std::vector<size_t>, std::greater<>> freeRooms;

    auto resetDayState = [&]() {
        while (!activeRooms.empty()) {
            activeRooms.pop();
        }
        while (!freeRooms.empty()) {
            freeRooms.pop();
        }
        for (size_t roomIndex = 0; roomIndex < pClassroomsByPriority.size(); ++roomIndex) {
            freeRooms.push(roomIndex);
        }
    };

    std::chrono::weekday currentDay{};
    bool hasCurrentDay = false;

    for (const MonWithIndex& mon : orderedMons) {
        if (!hasCurrentDay || mon.wd != currentDay) {
            currentDay = mon.wd;
            hasCurrentDay = true;
            resetDayState();
        }

        while (!activeRooms.empty() && activeRooms.top().finish <= mon.start) {
            freeRooms.push(activeRooms.top().roomIndex);
            activeRooms.pop();
        }

        if (freeRooms.empty()) {
            return false;
        }

        size_t selectedRoomIndex = pClassroomsByPriority.size();
        std::vector<size_t> skippedRooms;

        while (!freeRooms.empty()) {
            const size_t candidateRoomIndex = freeRooms.top();
            freeRooms.pop();

            bool blockedByClass = false;
            const std::vector<Interval>& blocked = classIntervalsByDay[mon.dayIndex][candidateRoomIndex];
            for (const Interval& interval : blocked) {
                if (interval.start >= mon.finish) {
                    break;
                }
                if (overlaps(mon.start, mon.finish, interval.start, interval.finish)) {
                    blockedByClass = true;
                    break;
                }
            }

            if (!blockedByClass) {
                selectedRoomIndex = candidateRoomIndex;
                break;
            }

            skippedRooms.push_back(candidateRoomIndex);
        }

        for (size_t skippedRoom : skippedRooms) {
            freeRooms.push(skippedRoom);
        }

        if (selectedRoomIndex == pClassroomsByPriority.size()) {
            return false;
        }

        pAssignedClassrooms[mon.index] = pClassroomsByPriority[selectedRoomIndex];
        activeRooms.push(ActiveRoom{mon.finish, selectedRoomIndex});
    }

    return true;
}
}  // namespace App