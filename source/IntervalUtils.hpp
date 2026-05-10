#pragma once

#include <span>

#include "Types.hpp"

namespace App {
/**
 * @brief 
 * 
 * @param classes 
 * @return true 
 * @return false 
 */
bool tryPartitionClasses(std::span<const Class> classes, uint32_t*, uint32_t);
}