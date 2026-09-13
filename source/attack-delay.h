#pragma once

#include "item-prop-enum.h"
#include "random-var.h"

// The skill/brand part of player::attack_delay_with in DCSS 0.34.1.
// Skills use tenths. No actor, global player state, or RNG draw is needed.
// Callers handle fixed-delay artefacts before this function, then apply their
// own shield/armour penalties, status modifiers and action scheduling.
random_var weapon_skill_delay(int base_delay, int skill_tenths,
                               int min_delay_skill_tenths, brand_type brand);
