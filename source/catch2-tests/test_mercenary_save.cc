#include "catch_amalgamated.hpp"

#include "AppHdr.h"
#include "errors.h"
#include "mercenary.h"
#include "monster.h"
#include "tags.h"
#include "test_player_fixture.h"

#include <limits>

namespace
{
struct WorldRosterFixture
{
    WorldRosterFixture() { reset_mercenaries_for_new_game(); }
    ~WorldRosterFixture() { reset_mercenaries_for_new_game(); }
};

vector<unsigned char> save_roster(const MercenaryRoster &roster)
{
    vector<unsigned char> bytes;
    writer out(&bytes);
    roster.save(out);
    return bytes;
}

// Independently construct wire records, including values rejected by the
// public API, to exercise validation in the reader rather than its writer.
struct WireRecord
{
    int id = 1;
    string name = "검객";
    int species = SP_HUMAN;
    int background = JOB_FIGHTER;
    int xl = 7;
    int xp = 1234;
    int strength = 12;
    int intelligence = 8;
    int dexterity = 14;
    int state = 0;
    int skill = 17;
};

vector<unsigned char> wire_records(const vector<WireRecord> &rows,
                                   uint64_t next_id = 2,
                                   int skill_count = NUM_SKILLS)
{
    vector<unsigned char> bytes;
    writer out(&bytes);
    marshallInt(out, 0x4d485232);
    marshallUnsigned(out, next_id);
    marshallInt(out, rows.size());
    marshallShort(out, skill_count);
    for (const auto &r : rows)
    {
        marshallInt(out, r.id);
        marshallString(out, r.name);
        marshallInt(out, r.species);
        marshallInt(out, r.background);
        marshallInt(out, r.xl);
        marshallInt(out, r.xp);
        marshallInt(out, r.strength);
        marshallInt(out, r.intelligence);
        marshallInt(out, r.dexterity);
        marshallUByte(out, r.state);
        for (int i = 0; i < skill_count; ++i)
            marshallUByte(out, r.skill);
        marshallUByte(out, 0);
    }
    return bytes;
}
}

TEST_CASE("Mercenary save retains all states and record fields",
          "[muhyeop][save]")
{
    MercenaryRoster source;
    for (int state = 0; state <= 3; ++state)
    {
        auto &r = source.create("검객", SP_HUMAN, JOB_FIGHTER, 12, 8, 14);
        r.xl = 7;
        r.xp = 1234;
        r.state = static_cast<mercenary_roster_state>(state);
        for (int sk = 0; sk < NUM_SKILLS; ++sk)
            r.set_skill(static_cast<skill_type>(sk), sk % 28);
    }
    const auto bytes = save_roster(source);
    MercenaryRoster restored;
    reader in(bytes);
    restored.load(in);
    REQUIRE_FALSE(in.valid());
    REQUIRE(save_roster(restored) == bytes);
    for (int id = 1; id <= 4; ++id)
    {
        const auto *r = restored.find(id);
        REQUIRE(r != nullptr);
        REQUIRE(r->name == "검객");
        REQUIRE(r->species == SP_HUMAN);
        REQUIRE(r->background == JOB_FIGHTER);
        REQUIRE(r->xl == 7);
        REQUIRE(r->xp == 1234);
        REQUIRE(r->base_str == 12);
        REQUIRE(r->base_int == 8);
        REQUIRE(r->base_dex == 14);
        REQUIRE(static_cast<int>(r->state) == id - 1);
        for (int sk = 0; sk < NUM_SKILLS; ++sk)
            REQUIRE(r->skill(static_cast<skill_type>(sk)) == sk % 28);
    }
    REQUIRE(restored.create("Next", SP_HUMAN, JOB_FIGHTER, 10, 10, 10).id == 5);
}

TEST_CASE("Malformed mercenary rows cannot replace an existing roster",
          "[muhyeop][save]")
{
    MercenaryRoster roster;
    auto &original = roster.create("Keep", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    const auto before = save_roster(roster);
    vector<WireRecord> bad;
    WireRecord r;
    r.id = 0; bad.push_back(r);
    r = WireRecord(); r.id = -1; bad.push_back(r);
    r = WireRecord(); r.id = 2; bad.push_back(r);
    r = WireRecord(); r.species = -1; bad.push_back(r);
    r = WireRecord(); r.species = NUM_SPECIES; bad.push_back(r);
    r = WireRecord(); r.background = NUM_JOBS; bad.push_back(r);
    r = WireRecord(); r.xl = 0; bad.push_back(r);
    r = WireRecord(); r.xp = -1; bad.push_back(r);
    r = WireRecord(); r.strength = -1; bad.push_back(r);
    r = WireRecord(); r.intelligence = -1; bad.push_back(r);
    r = WireRecord(); r.dexterity = -1; bad.push_back(r);
    r = WireRecord(); r.state = 4; bad.push_back(r);
    r = WireRecord(); r.skill = 28; bad.push_back(r);
    for (const auto &row : bad)
    {
        const auto bytes = wire_records({row});
        reader in(bytes);
        REQUIRE_THROWS_AS(roster.load(in), corrupted_save);
        REQUIRE(roster.find(1) == &original);
        REQUIRE(save_roster(roster) == before);
    }
    const auto duplicates = wire_records({WireRecord(), WireRecord()});
    reader duplicate_input(duplicates);
    REQUIRE_THROWS_AS(roster.load(duplicate_input), corrupted_save);
    REQUIRE(roster.find(1) == &original);
    REQUIRE(save_roster(roster) == before);
}

TEST_CASE("Truncated mercenary data never partially commits",
          "[muhyeop][save]")
{
    const auto bytes = wire_records({WireRecord()});
    MercenaryRoster roster;
    auto &original = roster.create("Keep", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    const auto before = save_roster(roster);
    for (size_t size = 0; size < bytes.size(); ++size)
    {
        const vector<unsigned char> short_bytes(bytes.begin(), bytes.begin() + size);
        reader in(short_bytes);
        in.set_safe_read(true);
        REQUIRE_THROWS(roster.load(in));
        REQUIRE(roster.find(1) == &original);
        REQUIRE(save_roster(roster) == before);
    }
}

TEST_CASE("Mercenary header and ID allocation are validated",
          "[muhyeop][save]")
{
    MercenaryRoster roster;
    const uint64_t exhausted = uint64_t(std::numeric_limits<merc_id_t>::max()) + 1;
    for (uint64_t next : {uint64_t(0), exhausted + 1})
    {
        const auto bytes = wire_records({}, next);
        reader in(bytes);
        REQUIRE_THROWS_AS(roster.load(in), corrupted_save);
    }
    for (int count : {0, NUM_SKILLS + 1})
    {
        const auto bytes = wire_records({}, 1, count);
        reader in(bytes);
        REQUIRE_THROWS_AS(roster.load(in), corrupted_save);
    }
    auto bytes = wire_records({});
    bytes[0] = 0;
    reader unknown_schema(bytes);
    REQUIRE_THROWS_AS(roster.load(unknown_schema), corrupted_save);

    // Deleted IDs must not be reused: next ID can exceed all surviving IDs.
    bytes = wire_records({WireRecord()}, 101);
    reader with_gap(bytes);
    roster.load(with_gap);
    REQUIRE(roster.create("Next", SP_HUMAN, JOB_FIGHTER, 10, 10, 10).id == 101);

    bytes = wire_records({}, exhausted);
    reader exhausted_input(bytes);
    roster.load(exhausted_input);
    REQUIRE_THROWS_AS(roster.create("Next", SP_HUMAN, JOB_FIGHTER, 10, 10, 10),
                      std::overflow_error);
    REQUIRE(save_roster(roster) == bytes);
}

TEST_CASE("Mercenary writer refuses invalid records before writing",
          "[muhyeop][save]")
{
    MercenaryRoster roster;
    auto &rec = roster.create("Keep", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    rec.base_str = -1;
    vector<unsigned char> bytes;
    writer out(&bytes);
    REQUIRE_THROWS_AS(roster.save(out), corrupted_save);
    REQUIRE(bytes.empty());
    rec.base_str = 10;
    rec.name = string(SHRT_MAX + 1, 'x');
    REQUIRE_THROWS_AS(roster.save(out), corrupted_save);
    REQUIRE(bytes.empty());
}

TEST_CASE("Negative counts and name lengths are rejected in the save reader",
          "[muhyeop][save]")
{
    for (bool negative_count : {false, true})
    {
        vector<unsigned char> bytes;
        writer out(&bytes);
        marshallInt(out, 0x4d485232);
        marshallUnsigned(out, 2);
        marshallInt(out, negative_count ? -1 : 1);
        marshallShort(out, NUM_SKILLS);
        marshallInt(out, 1);
        marshallShort(out, -1);
        reader in(bytes);
        in.set_safe_read(true);
        MercenaryRoster roster;
        REQUIRE_THROWS_AS(roster.load(in), corrupted_save);
        REQUIRE(roster.size() == 0);
    }
}

TEST_CASE_METHOD(WorldRosterFixture, "Old saves clear stale mercenaries without consuming bytes",
                 "[muhyeop][save]")
{
    mercenary_roster().create("Stale", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    const vector<unsigned char> bytes{42};
    reader in(bytes, TAG_MINOR_MUHYEOP_ROSTER - 1);
    read_mercenaries(in);
    REQUIRE(mercenary_roster().size() == 0);
    REQUIRE(unmarshallUByte(in) == 42);
    REQUIRE(mercenary_roster().create("New", SP_HUMAN, JOB_FIGHTER, 10, 10, 10).id == 1);
}

TEST_CASE_METHOD(WorldRosterFixture, "Restored world roster resolves the same shell ID",
                 "[muhyeop][save]")
{
    auto &rec = mercenary_roster().create("Persist", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    rec.set_skill(SK_FIGHTING, 21);
    monster shell;
    shell.props[MUHYEOP_MERC_ID_KEY] = int(rec.id);
    const auto bytes = save_roster(mercenary_roster());
    reset_mercenaries_for_new_game();
    reader in(bytes, TAG_MINOR_MUHYEOP_ROSTER);
    read_mercenaries(in);
    const auto link = lookup_mercenary(shell, mercenary_roster());
    REQUIRE(link.status == mercenary_link_status::LINKED);
    REQUIRE(link.record->name == "Persist");
    REQUIRE(link.record->skill(SK_FIGHTING) == 21);
}

TEST_CASE_METHOD(WorldRosterFixture, "Actual new-game setup starts an empty world roster",
                 "[muhyeop][save]")
{
    mercenary_roster().create("Previous world", SP_HUMAN, JOB_FIGHTER, 10, 10, 10);
    MockPlayerYouTestsFixture new_game;
    REQUIRE(mercenary_roster().size() == 0);
}


TEST_CASE("MHR1 migrates to undeployed and MHR2 rejects invalid deployment flags",
          "[muhyeop][save]")
{
    auto bytes = wire_records({WireRecord()});
    bytes.back() = 2;
    MercenaryRoster roster;
    reader invalid(bytes);
    REQUIRE_THROWS_AS(roster.load(invalid), corrupted_save);
    bytes.pop_back();
    bytes[3] = 0x31;
    reader old(bytes);
    roster.load(old);
    REQUIRE_FALSE(roster.find(1)->deployed);
    REQUIRE(roster.find(1)->xl == 7);
}
