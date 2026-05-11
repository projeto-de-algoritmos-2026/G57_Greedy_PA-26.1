#include "IntervalUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace App {

// ---------------------------------------------------------------------------
// tryPartitionMonitorings — Interval Partitioning
// ---------------------------------------------------------------------------
bool tryPartitionMonitorings(std::span<const Monitoring> mons,
                             std::span<const std::u8string> pClassroomsByPriority,
                             std::u8string_view* pAssignedClassrooms,
                             std::span<const Class> classes) {
    // A implementação no geral é baseada no "interval partitioning" modificado para, entre outros
    // aspectos, trabalhar com seções de tempo linearizadas e dois tipos distintos de intervalo

    if (!pAssignedClassrooms)
        throw std::invalid_argument("pAssignedClassrooms não pode ser nulo");

    static const auto validateTimes = [](std::chrono::minutes start, std::chrono::minutes finish) {
        constexpr std::chrono::minutes lim{24 * 60};
        if (start >= lim || finish >= lim || start >= finish)
            throw std::invalid_argument("horário inválido");
    };

    for (const Monitoring& mon : mons)
        validateTimes(mon.times.start, mon.times.finish);
    for (const Class& klass : classes)
        for (const auto& times : klass.dailyTimes)
            if (times.has_value())
                validateTimes(times->start, times->finish);

    struct MonWithIndex {
        size_t index;
        const Monitoring* mon;
    };
    std::vector<MonWithIndex> orderedMons;
    orderedMons.reserve(mons.size());

    // Tabela de hash
    std::unordered_map<std::u8string_view, size_t> roomIndexByName;
    roomIndexByName.reserve(pClassroomsByPriority.size());
    for (size_t i = 0; i < pClassroomsByPriority.size(); ++i)
        roomIndexByName.emplace(pClassroomsByPriority[i], i);

    // Intervalo de tempo (início, fim). Usando std::pair por simplicidade
    using Interval = std::pair<std::chrono::minutes, std::chrono::minutes>;

    // Para cada dia da semana (0-6) e cada sala, armazena os intervalos de aula já existentes.
    // Usado para evitar sobreposição de monitorias com aulas já agendadas
    std::array<std::vector<std::vector<Interval>>, 7> classIntervalsByDay;
    for (unsigned day = 0; day < classIntervalsByDay.size(); ++day) {
        classIntervalsByDay[day].resize(pClassroomsByPriority.size());
    }

    for (size_t i = 0; i < mons.size(); ++i) {
        if (!mons[i].wd.ok())
            throw std::invalid_argument("dia da semana inválido");
        orderedMons.emplace_back(MonWithIndex{i, &mons[i]});
    }

    for (const Class& klass : classes) {
        /*
        if (klass.classroom.empty()) {
            continue;
        }
         */

        const auto roomIt = roomIndexByName.find(klass.classroom);
        if (roomIt == roomIndexByName.end()) {
            // Essa turma não tem aula na sala klass.classroom
            continue;
        }

        for (unsigned day = 0; day < klass.dailyTimes.size(); ++day) {
            const auto& times = klass.dailyTimes[day];
            if (!times.has_value()) {
                // Não tem aula nesse dia, podemos pular
                continue;
            }

            classIntervalsByDay[day][roomIt->second].emplace_back(
                Interval{times->start, times->finish});
        }
    }

    for (size_t day = 0; day < 7; ++day)
        for (auto& ri : classIntervalsByDay[day])
            std::sort(ri.begin(), ri.end());

    // Ordena monitorias por dia da semana, início e término
    std::sort(orderedMons.begin(), orderedMons.end(),
              [](const MonWithIndex& a, const MonWithIndex& b) {
                  if (a.mon->wd.c_encoding() != b.mon->wd.c_encoding())
                      return a.mon->wd.c_encoding() < b.mon->wd.c_encoding();
                  if (a.mon->times.start != b.mon->times.start)
                      return a.mon->times.start < b.mon->times.start;
                  return a.mon->times.finish < b.mon->times.finish;
              });

    struct RoomRelease {
        std::chrono::minutes freeAt;
        size_t roomIndex;
    };

    static const auto releaseCmp = [](const RoomRelease& a, const RoomRelease& b) {
        if (a.freeAt != b.freeAt)
            return a.freeAt > b.freeAt;
        return a.roomIndex > b.roomIndex;
    };

    // Salas ainda bloqueadas por aulas ou monitorias já atribuídas
    std::priority_queue<RoomRelease, std::vector<RoomRelease>, decltype(releaseCmp)> blockedRooms(
        releaseCmp);
    // Salas disponíveis, sempre priorizando o menor índice primeiro
    std::priority_queue<size_t, std::vector<size_t>, std::greater<>> freeRooms;

    // Reinicia o estado de salas para um novo dia: limpa salas em uso e marca todas como livres
    static const auto resetDayState = [&]() {
        while (!blockedRooms.empty())
            blockedRooms.pop();
        while (!freeRooms.empty())
            freeRooms.pop();
        for (size_t i = 0; i < pClassroomsByPriority.size(); ++i)
            freeRooms.emplace(i);
    };

    static const auto releasesRoomAt = [](const std::chrono::minutes currentTime,
                                          const std::chrono::minutes candidateFreeAt) {
        return candidateFreeAt <= currentTime;
    };

    std::chrono::weekday currentDay{};
    bool hasCurrentDay = false;
    std::vector<std::chrono::minutes> roomFreeAt(pClassroomsByPriority.size(),
                                                 std::chrono::minutes{0});
    std::vector<size_t> nextClassIndex(pClassroomsByPriority.size(), 0);

    // Laço pelas monitorias ordenadas
    for (const MonWithIndex& mi : orderedMons) {
        auto* mon = mi.mon;
        if (!hasCurrentDay || mon->wd != currentDay) {
            currentDay = mon->wd;
            hasCurrentDay = true;
            resetDayState();
            std::fill(roomFreeAt.begin(), roomFreeAt.end(), std::chrono::minutes{0});
            std::fill(nextClassIndex.begin(), nextClassIndex.end(), 0);
        }

        while (!blockedRooms.empty() &&
               releasesRoomAt(mon->times.start, blockedRooms.top().freeAt)) {
            const RoomRelease release = blockedRooms.top();
            blockedRooms.pop();
            if (roomFreeAt[release.roomIndex] == release.freeAt) {
                freeRooms.emplace(release.roomIndex);
            }
        }

        size_t selectedRoomIndex = pClassroomsByPriority.size();
        bool assigned = false;
        while (!freeRooms.empty()) {
            const size_t candidateRoomIndex = freeRooms.top();
            freeRooms.pop();

            if (roomFreeAt[candidateRoomIndex] > mon->times.start) {
                continue;
            }

            const auto& blocked = classIntervalsByDay[mi.mon->wd.c_encoding()][candidateRoomIndex];
            size_t& classIndex = nextClassIndex[candidateRoomIndex];
            while (classIndex < blocked.size() && blocked[classIndex].second <= mon->times.start) {
                ++classIndex;
            }

            if (classIndex < blocked.size() && blocked[classIndex].first < mon->times.finish) {
                roomFreeAt[candidateRoomIndex] = blocked[classIndex].second;
                blockedRooms.push(RoomRelease{roomFreeAt[candidateRoomIndex], candidateRoomIndex});
                continue;
            }

            selectedRoomIndex = candidateRoomIndex;
            assigned = true;
            break;
        }

        if (!assigned)
            return false;

        pAssignedClassrooms[mi.index] = pClassroomsByPriority[selectedRoomIndex];
        roomFreeAt[selectedRoomIndex] = mon->times.finish;
        blockedRooms.push(RoomRelease{roomFreeAt[selectedRoomIndex], selectedRoomIndex});
    }
    // Fim do laço pelas monitorias ordenadas

    // Retornar que conseguimos agendar todas as monitorias
    return true;
}

// ---------------------------------------------------------------------------
// tryScheduleMonitorings — Interval Scheduling Maximization
// ---------------------------------------------------------------------------
std::vector<const Monitoring*> tryScheduleMonitorings(std::span<const Monitoring> mons,
                                                      std::span<const Class> classes) {
    static const auto validateTimes = [](std::chrono::minutes start, std::chrono::minutes finish) {
        constexpr std::chrono::minutes lim{24 * 60};
        if (start >= lim || finish >= lim || start >= finish)
            throw std::invalid_argument("horário inválido");
    };

    for (const Monitoring& mon : mons)
        validateTimes(mon.times.start, mon.times.finish);
    for (const Class& klass : classes)
        for (const auto& times : klass.dailyTimes)
            if (times.has_value())
                validateTimes(times->start, times->finish);

    using Interval = std::pair<std::chrono::minutes, std::chrono::minutes>;

    // Blocos ocupados pelas turmas por dia
    std::array<std::vector<Interval>, 7> classBlocksByDay;
    for (const Class& klass : classes)
        for (unsigned day = 0; day < 7; ++day)
            if (klass.dailyTimes[day].has_value())
                classBlocksByDay[day].emplace_back(klass.dailyTimes[day]->start,
                                                   klass.dailyTimes[day]->finish);
    for (auto& db : classBlocksByDay)
        std::sort(db.begin(), db.end());

    const auto conflictsWithClass = [&](const Monitoring& mon) -> bool {
        for (const auto& block : classBlocksByDay[mon.wd.c_encoding()]) {
            if (block.first >= mon.times.finish)
                break;
            if (mon.times.start < block.second && block.first < mon.times.finish)
                return true;
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
            if (a->times.finish != b->times.finish)
                return a->times.finish < b->times.finish;
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
