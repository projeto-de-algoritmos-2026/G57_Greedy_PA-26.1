#include "IntervalUtils.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace App {

// ---------------------------------------------------------------------------
// tryPartitionMonitorings — Interval Partitioning (João Felipe)
// ---------------------------------------------------------------------------
bool tryPartitionMonitorings(std::span<const Monitoring> mons,
                             std::span<const std::u8string> pClassroomsByPriority,
                             std::u8string_view* pAssignedClassrooms,
                             std::span<const Class> classes) {
    constexpr std::chrono::minutes dayLimit{24 * 60};
    if (!pAssignedClassrooms)
        throw std::invalid_argument("pAssignedClassrooms não pode ser nulo");

    static const auto validateTimes = [](std::chrono::minutes start,
                                         std::chrono::minutes finish) {
        constexpr std::chrono::minutes lim{24 * 60};
        if (start >= lim || finish >= lim || start >= finish)
            throw std::invalid_argument("horário inválido");
    };
    static const auto overlaps = [](std::chrono::minutes aStart, std::chrono::minutes aFinish,
                                    std::chrono::minutes bStart, std::chrono::minutes bFinish) {
        return aStart < bFinish && bStart < aFinish;
    };

    for (const Monitoring& mon : mons) validateTimes(mon.times.start, mon.times.finish);
    for (const Class& klass : classes)
        for (const auto& times : klass.dailyTimes)
            if (times.has_value()) validateTimes(times->start, times->finish);

    struct MonWithIndex { size_t index; const Monitoring* mon; };
    std::vector<MonWithIndex> orderedMons;
    orderedMons.reserve(mons.size());

    std::unordered_map<std::u8string_view, size_t> roomIndexByName;
    roomIndexByName.reserve(pClassroomsByPriority.size());
    for (size_t i = 0; i < pClassroomsByPriority.size(); ++i)
        roomIndexByName.emplace(pClassroomsByPriority[i], i);

    using Interval = std::pair<std::chrono::minutes, std::chrono::minutes>;
    std::array<std::vector<std::vector<Interval>>, 7> classIntervalsByDay;
    for (unsigned day = 0; day < 7; ++day)
        classIntervalsByDay[day].resize(pClassroomsByPriority.size());

    for (size_t i = 0; i < mons.size(); ++i) {
        if (!mons[i].wd.ok()) throw std::invalid_argument("dia da semana inválido");
        orderedMons.emplace_back(MonWithIndex{i, &mons[i]});
    }

    for (const Class& klass : classes) {
        const auto roomIt = roomIndexByName.find(klass.classroom);
        if (roomIt == roomIndexByName.end()) continue;
        for (unsigned day = 0; day < klass.dailyTimes.size(); ++day) {
            const auto& times = klass.dailyTimes[day];
            if (!times.has_value()) continue;
            classIntervalsByDay[day][roomIt->second].emplace_back(times->start, times->finish);
        }
    }

    for (size_t day = 0; day < 7; ++day)
        for (auto& ri : classIntervalsByDay[day])
            std::sort(ri.begin(), ri.end());

    std::sort(orderedMons.begin(), orderedMons.end(),
              [](const MonWithIndex& a, const MonWithIndex& b) {
                  if (a.mon->wd.c_encoding() != b.mon->wd.c_encoding())
                      return a.mon->wd.c_encoding() < b.mon->wd.c_encoding();
                  if (a.mon->times.start != b.mon->times.start)
                      return a.mon->times.start < b.mon->times.start;
                  return a.mon->times.finish < b.mon->times.finish;
              });

    struct ActiveRoom { std::chrono::minutes finish; size_t roomIndex; };
    static const auto activeCmp = [](const ActiveRoom& a, const ActiveRoom& b) {
        if (a.finish != b.finish) return a.finish > b.finish;
        return a.roomIndex > b.roomIndex;
    };

    std::priority_queue<ActiveRoom, std::vector<ActiveRoom>, decltype(activeCmp)> activeRooms(activeCmp);
    std::priority_queue<size_t, std::vector<size_t>, std::greater<>> freeRooms;

    const auto resetDayState = [&]() {
        while (!activeRooms.empty()) activeRooms.pop();
        while (!freeRooms.empty()) freeRooms.pop();
        for (size_t i = 0; i < pClassroomsByPriority.size(); ++i) freeRooms.emplace(i);
    };

    std::chrono::weekday currentDay{};
    bool hasCurrentDay = false;

    for (const MonWithIndex& mi : orderedMons) {
        auto* mon = mi.mon;
        if (!hasCurrentDay || mon->wd != currentDay) {
            currentDay = mon->wd; hasCurrentDay = true; resetDayState();
        }
        while (!activeRooms.empty() && activeRooms.top().finish <= mon->times.start) {
            freeRooms.emplace(activeRooms.top().roomIndex); activeRooms.pop();
        }
        if (freeRooms.empty()) return false;

        size_t selectedRoomIndex = pClassroomsByPriority.size();
        std::vector<size_t> skippedRooms;

        while (!freeRooms.empty()) {
            const size_t candidateRoomIndex = freeRooms.top();
            freeRooms.pop();
            bool blockedByClass = false;
            const auto& blocked = classIntervalsByDay[mi.mon->wd.c_encoding()][candidateRoomIndex];
            for (const auto& interval : blocked) {
                if (interval.first >= mon->times.finish) break;
                if (overlaps(mon->times.start, mon->times.finish, interval.first, interval.second)) {
                    blockedByClass = true; break;
                }
            }
            if (!blockedByClass) { selectedRoomIndex = candidateRoomIndex; break; }
            skippedRooms.emplace_back(candidateRoomIndex);
        }
        for (size_t sr : skippedRooms) freeRooms.emplace(sr);
        if (selectedRoomIndex == pClassroomsByPriority.size()) return false;

        pAssignedClassrooms[mi.index] = pClassroomsByPriority[selectedRoomIndex];
        activeRooms.emplace(ActiveRoom{mon->times.finish, selectedRoomIndex});
    }
    return true;
}

// ---------------------------------------------------------------------------
// tryScheduleMonitorings — Interval Scheduling Maximization
// ---------------------------------------------------------------------------
std::vector<const Monitoring*> tryScheduleMonitorings(std::span<const Monitoring> mons,
                                                      std::span<const Class> classes) {
    constexpr std::chrono::minutes dayLimit{24 * 60};

    static const auto validateTimes = [](std::chrono::minutes start,
                                         std::chrono::minutes finish) {
        constexpr std::chrono::minutes lim{24 * 60};
        if (start >= lim || finish >= lim || start >= finish)
            throw std::invalid_argument("horário inválido");
    };

    for (const Monitoring& mon : mons) validateTimes(mon.times.start, mon.times.finish);
    for (const Class& klass : classes)
        for (const auto& times : klass.dailyTimes)
            if (times.has_value()) validateTimes(times->start, times->finish);

    using Interval = std::pair<std::chrono::minutes, std::chrono::minutes>;

    // Blocos ocupados pelas turmas por dia
    std::array<std::vector<Interval>, 7> classBlocksByDay;
    for (const Class& klass : classes)
        for (unsigned day = 0; day < 7; ++day)
            if (klass.dailyTimes[day].has_value())
                classBlocksByDay[day].emplace_back(klass.dailyTimes[day]->start,
                                                   klass.dailyTimes[day]->finish);
    for (auto& db : classBlocksByDay) std::sort(db.begin(), db.end());

    const auto conflictsWithClass = [&](const Monitoring& mon) -> bool {
        for (const auto& block : classBlocksByDay[mon.wd.c_encoding()]) {
            if (block.first >= mon.times.finish) break;
            if (mon.times.start < block.second && block.first < mon.times.finish) return true;
        }
        return false;
    };

    // Agrupa por dia, filtrando conflitos com turmas
    std::array<std::vector<const Monitoring*>, 7> monsByDay;
    for (const Monitoring& mon : mons)
        if (!conflictsWithClass(mon))
            monsByDay[mon.wd.c_encoding()].push_back(&mon);

    std::vector<const Monitoring*> result;

    // Greedy: earliest finish time por dia
    for (auto& dayMons : monsByDay) {
        std::sort(dayMons.begin(), dayMons.end(), [](const Monitoring* a, const Monitoring* b) {
            if (a->times.finish != b->times.finish) return a->times.finish < b->times.finish;
            return a->times.start < b->times.start;
        });
        std::chrono::minutes lastFinish{0};
        for (const Monitoring* mon : dayMons) {
            if (mon->times.start >= lastFinish) {
                result.push_back(mon);
                lastFinish = mon->times.finish;
            }
        }
    }
    return result;
}

}  // namespace App
