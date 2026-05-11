#include "IntervalUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace App {
bool tryPartitionClasses(std::span<const Monitoring> mons,
                         std::span<const std::u8string> pClassroomsByPriority,
                         std::u8string_view* pAssignedClassrooms, std::span<const Class> classes) {
    // A implementação no geral é baseada no "interval partitioning" modificado para, entre outros
    // aspectos, trabalhar com seções de tempo linearizadas

    constexpr std::chrono::minutes dayLimit{24 * 60};

    // Validação de pAssignedClassrooms
    if (pAssignedClassrooms == nullptr) {
        throw std::invalid_argument("pAssignedClassrooms não pode ser nulo");
    }

    // Para validar qualquer (tipo de) intervalo, como tanto monitorias quanto aulas das turmas
    static const auto validateTimes = [](std::chrono::minutes start, std::chrono::minutes finish) {
        if (start >= dayLimit || finish >= dayLimit || start >= finish) {
            throw std::invalid_argument("horário inválido");
        }
    };

    // Retorna um {bool} correspondente a se os intervalos de tempo a e b se sobrepôem, o contrário
    // (lógico) de se são «compatíveis» (entre si)
    static const auto overlaps = [](std::chrono::minutes aStart, std::chrono::minutes aFinish,
                                    std::chrono::minutes bStart, std::chrono::minutes bFinish) {
        return aStart < bFinish && bStart < aFinish;
    };

    // Por uma questão de preferência, foi escolhido validar todos os horários de uma vez antes de
    // que a função possa alterar pAssignedClassrooms
    for (const Monitoring& mon : mons) {
        validateTimes(mon.times.start, mon.times.finish);
    }

    // A mesma coisa para as turmas
    for (const Class& klass : classes) {
        for (const std::optional<Class::Times>& times : klass.dailyTimes) {
            if (times.has_value()) {
                validateTimes(times->start, times->finish);
            }
        }
    }

    // Mantém o índice original de cada monitoria e um ponteiro para ela, evitando cópias
    struct MonWithIndex {
        size_t index;
        const Monitoring* mon;
    };

    std::vector<MonWithIndex> orderedMons;
    orderedMons.reserve(mons.size());

    // Tabela de hash
    std::unordered_map<std::u8string_view, size_t> roomIndexByName;
    roomIndexByName.reserve(pClassroomsByPriority.size());
    for (size_t roomIndex = 0; roomIndex < pClassroomsByPriority.size(); ++roomIndex) {
        roomIndexByName.emplace(pClassroomsByPriority[roomIndex], roomIndex);
    }

    // Intervalo de tempo (início, fim). Usando std::pair por simplicidade
    using Interval = std::pair<std::chrono::minutes, std::chrono::minutes>;

    // Para cada dia da semana (0-6) e cada sala, armazena os intervalos de aula já existentes.
    // Usado para evitar sobreposição de monitorias com aulas já agendadas
    std::array<std::vector<std::vector<Interval>>, 7> classIntervalsByDay;
    for (unsigned day = 0; day < classIntervalsByDay.size(); ++day) {
        classIntervalsByDay[day].resize(pClassroomsByPriority.size());
    }

    for (size_t i = 0; i < mons.size(); ++i) {
        if (!mons[i].wd.ok()) {
            throw std::invalid_argument("dia da semana inválido");
        }
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
            const std::optional<Class::Times>& times = klass.dailyTimes[day];
            if (!times.has_value()) {
                // Não tem aula nesse dia, podemos pular
                continue;
            }

            classIntervalsByDay[day][roomIt->second].emplace_back(
                Interval{times->start, times->finish});
        }
    }

    for (std::size_t day = 0; day < classIntervalsByDay.size(); ++day) {
        for (std::vector<Interval>& roomIntervals : classIntervalsByDay[day]) {
            std::sort(roomIntervals.begin(), roomIntervals.end(),
                      [](const Interval& a, const Interval& b) {
                          if (a.first != b.first) {
                              return a.first < b.first;
                          }
                          return a.second < b.second;
                      });
        }
    }

    // Ordenar monitorias por dia da semana, depois por hora de início, depois por hora de término
    std::sort(orderedMons.begin(), orderedMons.end(),
              [](const MonWithIndex& a, const MonWithIndex& b) {
                  if (a.mon->wd.c_encoding() != b.mon->wd.c_encoding()) {
                      return a.mon->wd.c_encoding() < b.mon->wd.c_encoding();
                  }
                  if (a.mon->times.start != b.mon->times.start) {
                      return a.mon->times.start < b.mon->times.start;
                  }
                  return a.mon->times.finish < b.mon->times.finish;
              });

    struct ActiveRoom {
        std::chrono::minutes finish;  // O término do último intervalo nesta sala
        size_t roomIndex;
    };

    // Min-heap: ordena salas em uso pelo menor tempo de término (greedy earliest finish time)
    static const auto activeCmp = [](const ActiveRoom& a, const ActiveRoom& b) {
        if (a.finish != b.finish) {
            return a.finish > b.finish;
        }
        return a.roomIndex > b.roomIndex;
    };

    // Interval Partitioning: mantém salas atualmente em uso, ordenadas por tempo de término
    std::priority_queue<ActiveRoom, std::vector<ActiveRoom>, decltype(activeCmp)> activeRooms(
        activeCmp);
    // Min-heap: salas livres, priorizando índices menores (melhor prioridade)
    std::priority_queue<size_t, std::vector<size_t>, std::greater<>> freeRooms;

    // Reinicia o estado de salas para um novo dia: limpa salas em uso e marca todas como livres
    static const auto resetDayState = [&]() {
        while (!activeRooms.empty()) {
            activeRooms.pop();
        }
        while (!freeRooms.empty()) {
            freeRooms.pop();
        }
        for (size_t roomIndex = 0; roomIndex < pClassroomsByPriority.size(); ++roomIndex) {
            freeRooms.emplace(roomIndex);
        }
    };

    std::chrono::weekday currentDay{};
    bool hasCurrentDay = false;

    // Laço pelas monitorias ordenadas
    for (const MonWithIndex& mi : orderedMons) {
        auto* mon = mi.mon;
        if (!hasCurrentDay || mon->wd != currentDay) {
            currentDay = mon->wd;
            hasCurrentDay = true;
            resetDayState();
        }

        // Libera salas cujas monitorias/aulas terminam antes do início desta monitoria
        while (!activeRooms.empty() && activeRooms.top().finish <= mon->times.start) {
            freeRooms.emplace(activeRooms.top().roomIndex);
            activeRooms.pop();
        }

        // Se não há salas livres (nem monitorias já atribuídas, nem de conflito com aulas),
        // impossível particionar todas as monitorias
        if (freeRooms.empty()) {
            return false;
        }

        // Tenta encontrar a sala de maior prioridade sem conflito com aulas existentes
        size_t selectedRoomIndex = pClassroomsByPriority.size();
        std::vector<size_t> skippedRooms;

        while (!freeRooms.empty()) {
            const size_t candidateRoomIndex = freeRooms.top();
            freeRooms.pop();

            bool blockedByClass = false;
            const std::vector<Interval>& blocked =
                classIntervalsByDay[mi.mon->wd.c_encoding()][candidateRoomIndex];
            for (const Interval& interval : blocked) {
                // Se a aula começa depois que a monitoria termina, sem conflito
                if (interval.first >= mon->times.finish) {
                    break;
                }
                // Verifica se há sobreposição temporal
                if (overlaps(mon->times.start, mon->times.finish, interval.first,
                             interval.second)) {
                    blockedByClass = true;
                    break;
                }
            }

            if (!blockedByClass) {
                selectedRoomIndex = candidateRoomIndex;
                break;
            }

            // Sala candidata bloqueada, preserva para reinserir depois
            skippedRooms.emplace_back(candidateRoomIndex);
        }

        // Reinserir salas que foram puladas (mas bloqueadas por aulas existentes)
        for (size_t skippedRoom : skippedRooms) {
            freeRooms.emplace(skippedRoom);
        }

        if (selectedRoomIndex == pClassroomsByPriority.size()) {
            // Nenhuma sala disponível sem conflito com aulas existentes
            return false;
        }

        // Atribuir a sala selecionada para esta monitoria
        pAssignedClassrooms[mi.index] = pClassroomsByPriority[selectedRoomIndex];
        // Marcar a sala como em uso até o fim da monitoria
        activeRooms.emplace(ActiveRoom{mon->times.finish, selectedRoomIndex});
    }
    // Fim do laço pelas monitorias ordenadas

    // Retornar que conseguimos agendar todas as monitorias
    return true;
}
}  // namespace App