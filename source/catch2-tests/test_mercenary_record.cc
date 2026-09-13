#include "catch_amalgamated.hpp"

#include "AppHdr.h"
#include "mercenary.h"
#include <limits>

TEST_CASE("Mercenary records survive roster growth and death state",
          "[muhyeop][mercenary]")
{
    MercenaryRoster roster;
    auto &first = roster.create("First", SP_HUMAN, JOB_FIGHTER, 12, 8, 10);
    const merc_id_t id = first.id;
    first.set_skill(SK_FIGHTING, 17);
    for (int i = 0; i < 1024; ++i)
        roster.create("Recruit", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    REQUIRE(roster.find(id) == &first);
    REQUIRE(roster.size() == 1025);
    REQUIRE(roster.find(1025)->id == 1025);
    REQUIRE(roster.find(0) == nullptr);
    REQUIRE(roster.find(-1) == nullptr);
    REQUIRE(roster.find(9999) == nullptr);
    REQUIRE(roster.size() == 1025);

    first.state = mercenary_roster_state::DEAD;
    REQUIRE(roster.find(id) == &first);
    REQUIRE(first.skill(SK_FIGHTING) == 17);
}

TEST_CASE("Mercenary skills use record values with checked scale",
          "[muhyeop][mercenary]")
{
    MercenaryRoster roster;
    auto &rec = roster.create("First", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    REQUIRE(rec.skill(SK_FIGHTING) == 0);
    REQUIRE(rec.set_skill(SK_FIGHTING, 27));
    REQUIRE(rec.skill(SK_FIGHTING, 100) == 2700);
    REQUIRE_FALSE(rec.set_skill(SK_FIGHTING, 28));
    REQUIRE_FALSE(rec.set_skill(SK_FIGHTING, -1));
    REQUIRE_FALSE(rec.set_skill(NUM_SKILLS, 1));
    REQUIRE(rec.skill(SK_FIGHTING) == 27);
    REQUIRE_THROWS(rec.skill(NUM_SKILLS));
    REQUIRE_THROWS(rec.skill(SK_FIGHTING, -1));
    REQUIRE_THROWS(rec.skill(SK_FIGHTING, std::numeric_limits<int>::max()));
    rec.xl = 20;
    REQUIRE(rec.skill(SK_FIGHTING, 10) == 270);
}
