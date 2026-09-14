#include "catch_amalgamated.hpp"

#include "AppHdr.h"
#include "mercenary.h"
#include "monster.h"

#include <limits>

TEST_CASE("Broken mercenary identity is distinct from ordinary monsters",
          "[muhyeop][mercenary]")
{
    MercenaryRoster roster;
    auto &rec = roster.create("First", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    monster shell;
    REQUIRE(lookup_mercenary(shell, roster).status
            == mercenary_link_status::NOT_MERCENARY);
    REQUIRE_FALSE(shell.props.exists(MUHYEOP_MERC_ID_KEY));

    shell.props[MUHYEOP_MERC_ID_KEY] = "1";
    REQUIRE(is_mercenary_monster(shell));
    REQUIRE(lookup_mercenary(shell, roster).status
            == mercenary_link_status::INVALID_ID);
    shell.props.erase(MUHYEOP_MERC_ID_KEY);
    for (int invalid : {0, -1})
    {
        shell.props[MUHYEOP_MERC_ID_KEY] = invalid;
        REQUIRE(is_mercenary_monster(shell));
        REQUIRE(lookup_mercenary(shell, roster).status
                == mercenary_link_status::INVALID_ID);
        REQUIRE(lookup_mercenary(shell, roster).record == nullptr);
    }
    shell.props[MUHYEOP_MERC_ID_KEY] = 9999;
    REQUIRE(lookup_mercenary(shell, roster).status
            == mercenary_link_status::MISSING_RECORD);
    REQUIRE(lookup_mercenary(shell, roster).record == nullptr);
    REQUIRE(roster.size() == 1);

    shell.props[MUHYEOP_MERC_ID_KEY] = int(rec.id);
    REQUIRE(lookup_mercenary(shell, roster).status
            == mercenary_link_status::LINKED);
    REQUIRE(lookup_mercenary(shell, roster).record == &rec);
}

TEST_CASE("Shell reset preserves the original record", "[muhyeop][mercenary]")
{
    MercenaryRoster roster;
    auto &rec = roster.create("First", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    rec.set_skill(SK_FIGHTING, 17);
    monster shell;
    shell.props[MUHYEOP_MERC_ID_KEY] = int(rec.id);
    REQUIRE(lookup_mercenary(shell, roster).record == &rec);
    shell.reset();
    REQUIRE_FALSE(is_mercenary_monster(shell));
    REQUIRE(roster.find(rec.id) == &rec);
    REQUIRE(rec.skill(SK_FIGHTING) == 17);
}


TEST_CASE("Mercenary shell search preserves duplicates and downed slots",
          "[muhyeop][mercenary]")
{
    monster slots[3];
    slots[0].type = MONS_HUMAN;
    slots[0].hit_points = 0; // A downed shell must still block a second spawn.
    slots[0].props[MUHYEOP_MERC_ID_KEY] = 7;
    auto result = find_mercenary_shell(slots, 3, 7);
    REQUIRE(result.status == mercenary_shell_status::UNIQUE);
    REQUIRE(result.shell == &slots[0]);

    slots[2].type = MONS_HUMAN;
    slots[2].props[MUHYEOP_MERC_ID_KEY] = 7;
    result = find_mercenary_shell(slots, 3, 7);
    REQUIRE(result.status == mercenary_shell_status::DUPLICATE);
    REQUIRE(result.shell == nullptr);
    REQUIRE(slots[0].props[MUHYEOP_MERC_ID_KEY].get_int() == 7);
    REQUIRE(slots[2].props[MUHYEOP_MERC_ID_KEY].get_int() == 7);
    REQUIRE(slots[0].type == MONS_HUMAN);
    REQUIRE(slots[2].type == MONS_HUMAN);

    slots[2].reset();
    REQUIRE(find_mercenary_shell(slots, 3, 7).shell == &slots[0]);
    slots[0].reset();
    REQUIRE(find_mercenary_shell(slots, 3, 7).status
            == mercenary_shell_status::ABSENT);
}

TEST_CASE("Mercenary shell search rejects bad input and ignores unrelated slots",
          "[muhyeop][mercenary]")
{
    monster slots[3];
    slots[0].props[MUHYEOP_MERC_ID_KEY] = 7; // Empty slot with stale props.
    slots[1].type = MONS_HUMAN;
    slots[1].props[MUHYEOP_MERC_ID_KEY] = "7"; // Not a valid integer identity.
    slots[2].type = MONS_HUMAN;
    slots[2].props[MUHYEOP_MERC_ID_KEY] = 8;
    const auto result = find_mercenary_shell(slots, 3, 7);
    REQUIRE(result.status == mercenary_shell_status::ABSENT);
    REQUIRE(result.shell == nullptr);
    REQUIRE(lookup_mercenary(slots[1], MercenaryRoster()).status
            == mercenary_link_status::INVALID_ID);
    REQUIRE(find_mercenary_shell(nullptr, 0, 7).status
            == mercenary_shell_status::ABSENT);
    REQUIRE_THROWS_AS(find_mercenary_shell(nullptr, 1, 7), std::invalid_argument);
    REQUIRE_THROWS_AS(find_mercenary_shell(slots, 3, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(find_mercenary_shell(slots, 3, -1), std::invalid_argument);
}
