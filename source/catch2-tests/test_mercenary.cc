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
