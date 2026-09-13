#include "catch_amalgamated.hpp"

#include "AppHdr.h"
#include "attack-delay.h"

// Frozen reference: the skill/brand block from the uploaded, unmodified
// DCSS 0.34.1 source/player-act.cc, player::attack_delay_with().
static random_var original_delay(int base, int skill, int minimum_skill,
                                 brand_type brand)
{
    const int DELAY_SCALE = 20;
    const int wpn_sklev = min(skill, minimum_skill);
    random_var attk_delay(base);
    attk_delay -= div_rand_round(random_var(wpn_sklev), DELAY_SCALE);
    if (brand == SPWPN_SPEED)
        attk_delay = div_rand_round(attk_delay * 2, 3);
    else if (brand == SPWPN_HEAVY)
        attk_delay = div_rand_round(attk_delay * 3, 2);
    return attk_delay;
}

TEST_CASE("Shared weapon delay preserves the original probability weights",
          "[muhyeop][delay]")
{
    for (int base : {10, 11, 12, 13, 14, 15, 16, 18, 19, 20, 23, 27, 30})
        for (int minimum_skill : {0, 40, 100, 120, 140, 180, 200, 270, 280})
            for (int skill = 0; skill <= 270; ++skill)
                for (brand_type brand : {SPWPN_NORMAL, SPWPN_SPEED, SPWPN_HEAVY})
                {
                    const auto old = original_delay(base, skill, minimum_skill, brand);
                    const auto now = weapon_skill_delay(base, skill, minimum_skill, brand);
                    REQUIRE(now.min() == old.min());
                    REQUIRE(now.max() == old.max());
                    for (int delay = old.min(); delay <= old.max(); ++delay)
                        REQUIRE(now.weight(delay) == old.weight(delay));
                }
}

TEST_CASE("Weapon delay keeps fractional rounding before brand scaling",
          "[muhyeop][delay]")
{
    const auto normal = weapon_skill_delay(15, 101, 160, SPWPN_NORMAL);
    REQUIRE(normal.min() == 9);
    REQUIRE(normal.max() == 10);
    REQUIRE(normal.weight(9) == 1);
    REQUIRE(normal.weight(10) == 19);

    const auto capped = weapon_skill_delay(15, 270, 160, SPWPN_SPEED);
    REQUIRE(capped.min() == 4);
    REQUIRE(capped.max() == 5);
    REQUIRE(capped.weight(4) == 1);
    REQUIRE(capped.weight(5) == 2);
}
