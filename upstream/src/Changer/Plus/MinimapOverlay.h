#pragma once
#include <cstdint>
#include <string>
namespace MinimapOverlay { void Update(); void Toggle(std::intptr_t); void CheckAlignment(std::intptr_t); bool Enabled(); std::string Status(); }
