#pragma once

#include <span>

#include "Types.hpp"

namespace App {
/**
 * @brief Tenta fazer o particionamento de um conjunto de monitorias para um conjunto de salas de aula.
 *
 * @param mons As turmas a serem particionadas.
 * @param classrooms Referência para um array com o mesmo tamanho em elementos que mons. Para
 * cada enésima turma em mons, o enésimo membro disto terá o número da sala de aula necessária
 * para a aula.
 */
void tryPartitionClasses(std::span<const Monitoring> mons, uint32_t& classrooms);
}  // namespace App