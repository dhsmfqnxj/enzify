#include "AppHdr.h"

#include "attack-delay.h"

random_var weapon_skill_delay(int base_delay, int skill_tenths,
                               int min_delay_skill_tenths, brand_type brand)
{
    // Preserve the cap before random rounding and speed-brand multiplication.
    const int skill = min(skill_tenths, min_delay_skill_tenths);
    random_var delay(base_delay);
    delay -= div_rand_round(random_var(skill), 20);
    if (brand == SPWPN_SPEED)
        delay = div_rand_round(delay * 2, 3);
    else if (brand == SPWPN_HEAVY)
        delay = div_rand_round(delay * 3, 2);
    return delay;
}
