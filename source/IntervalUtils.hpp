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
 * @return true se conseguiu particionar todas as monitorias.
 * @throws std::invalid_argument se classrooms for nulo
 * @throws std::invalid_argument se houver uma monitoria ou turma com horários inválidos (valor
 * maior ou igual a 24 horas)
 */
bool tryPartitionClasses(std::span<const Monitoring> mons,
                         std::span<const std::u8string> pClassroomsByPriority,
                         std::u8string_view* pAssignedClassrooms, std::span<const Class> classes);
}  // namespace App