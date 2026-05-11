#pragma once

#include <span>

#include "Types.hpp"

namespace App {
/**
 * @brief Tenta fazer o particionamento de um conjunto de monitorias para um conjunto de salas de
 * aula.
 *
 * @param mons As monitorias a serem particionadas.
 * @param pClassroomsByPriority As salas de aula a usar para as monitorias, por ordem de prioridade
 * desejada.
 * @param pAssignedClassrooms Ponteiro para um array com o mesmo tamanho em elementos que mons. Para
 * cada enésima turma em mons, o enésimo membro disto terá o nome da sala de aula necessária
 * para a aula.
 * @param classes As classes que estarão se passando nas salas de aula (em pClassroomsByPriority) em
 * questão na semana.
 * @return true se conseguiu particionar todas as monitorias.
 * @throws std::invalid_argument se classrooms for nulo, ou houver uma monitoria ou turma com
 * horários inválidos (valor maior ou igual a 24 horas)
 */
bool tryPartitionMonitorings(std::span<const Monitoring> mons,
                             std::span<const std::u8string> pClassroomsByPriority,
                             std::u8string_view* pAssignedClassrooms,
                             std::span<const Class> classes);

/**
 * @brief Tenta fazer o agendamento de um conjunto de monitorias para uma única sala de aula ou um
 * único aluno.
 *
 * @param mons As monitorias a serem agendadas.
 * @param classes As classes para a sala de aula ou o aluno na semana.
 * @return std::vector<const Monitoring*>
 * @throws std::invalid_argument se houver uma monitoria ou turma com horários inválidos (valor
 * maior ou igual a 24 horas)
 */
std::vector<const Monitoring*> tryScheduleMonitorings(std::span<const Monitoring> mons,
                                                      std::span<const Class> classes);
}  // namespace App