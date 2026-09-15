#include "catch_amalgamated.hpp"
#include "AppHdr.h"
#include "mercenary.h"
#include "env.h"
#include "feature.h"
#include "mgen-data.h"
#include "mon-place.h"
#include "god-companions.h"
#include "mon-transit.h"
#include "mon-clone.h"
#include "player.h"
#include "random.h"
#include "tags.h"
#include "test_player_fixture.h"

namespace
{
struct PlacementFixture : MockPlayerYouTestsFixture
{
    decltype(env.grid) old_grid = env.grid;
    decltype(env.mgrid) old_mgrid = env.mgrid;
    const int old_max = env.max_mon_index;
    PlacementFixture()
    {
        init_monsters();
        init_show_table();
        REQUIRE(env.mid_cache.empty());
        REQUIRE(the_lost_ones.empty());
        REQUIRE(companion_list.empty());
        for (int i = 0; i < MAX_MONSTERS; ++i)
            REQUIRE(env.mons[i].type == MONS_NO_MONSTER);
        env.grid.init(DNGN_FLOOR);
        env.mgrid.init(NON_MONSTER);
        you.where_are_you = BRANCH_DUNGEON;
        you.depth = 1;
        you.set_position(coord_def(10, 10));
    }
    ~PlacementFixture()
    {
        for (int i = 0; i < MAX_MONSTERS; ++i)
            env.mons[i].reset();
        env.mid_cache.clear();
        env.max_mon_index = old_max;
        the_lost_ones.clear();
        companion_list.clear();
        env.grid = old_grid;
        env.mgrid = old_mgrid;
        reset_mercenaries_for_new_game();
    }
};
vector<unsigned char> roster_bytes()
{
    vector<unsigned char> bytes;
    writer out(&bytes);
    mercenary_roster().save(out);
    return bytes;
}
}

TEST_CASE_METHOD(PlacementFixture, "Real mercenary creation binds before returning",
                 "[muhyeop][placement]")
{
    auto &rec = mercenary_roster().create("Test ally", SP_HUMAN, JOB_FIGHTER, 12, 8, 10);
    rec.xl = 13;
    const coord_def pos(20, 20);
    monster *shell = create_mercenary_shell(rec.id, pos, 17, 30);
    REQUIRE(shell != nullptr);
    REQUIRE(shell->get_experience_level() == 13);
    REQUIRE(shell->get_hit_dice() == 13);
    REQUIRE(shell->hit_points == 17);
    REQUIRE(shell->max_hit_points == 30);
    REQUIRE(shell->attitude == ATT_FRIENDLY);
    REQUIRE(shell->pos() == pos);
    REQUIRE(env.mgrid(pos) == shell->mindex());
    REQUIRE(env.mid_cache.at(shell->mid) == shell->mindex());
    for (int i = 0; i < NUM_MONSTER_SLOTS; ++i)
        REQUIRE(shell->inv[i] == NON_ITEM);
    REQUIRE(rec.deployed);
    const auto clone_mid = you.last_mid;
    const auto clone_rng = rng::get_states();
    REQUIRE_FALSE(mons_clonable(shell, false));
    REQUIRE_FALSE(actor_is_illusion_cloneable(shell));
    REQUIRE(clone_mons(shell, true) == nullptr);
    REQUIRE(you.last_mid == clone_mid);
    REQUIRE(rng::get_states() == clone_rng);
    // Direct original setters cannot make a mercenary's level drift.
    shell->set_hit_dice(2);
    REQUIRE(shell->get_hit_dice() == 13);
    monster copy(*shell);
    copy.props.erase(MUHYEOP_MERC_ID_KEY);
    REQUIRE(copy.get_experience_level() == 13);
    const auto mid = shell->mid;
    const auto before = roster_bytes();
    REQUIRE(create_mercenary_shell(rec.id, coord_def(21,20), 17, 30) == nullptr);
    REQUIRE(roster_bytes() == before);
    REQUIRE(remove_mercenary_shell(rec.id));
    REQUIRE_FALSE(rec.deployed);
    REQUIRE(rec.xl == 13);
    REQUIRE(env.mid_cache.count(mid) == 0);
    REQUIRE(env.mgrid(pos) == NON_MONSTER);
    REQUIRE(create_mercenary_shell(rec.id, pos, 17, 30) != nullptr);
}

TEST_CASE_METHOD(PlacementFixture, "Failed native placement rolls back RNG and world bookkeeping",
                 "[muhyeop][placement]")
{
    auto &rec = mercenary_roster().create("Pending", SP_HUMAN, JOB_FIGHTER, 10,10,10);
    auto &other = mercenary_roster().create("Existing", SP_HUMAN, JOB_FIGHTER, 10,10,10);
    monster *existing = create_mercenary_shell(other.id, coord_def(20,20), 10,10);
    REQUIRE(existing != nullptr);
    const auto existing_mid = existing->mid;
    const coord_def blocked(40,40);
    // Native placement reserves its final slot, even though our preflight
    // can find an empty slot. Preserve the already deployed actor on failure.
    for (int i = 1; i < MAX_MONSTERS - 1; ++i)
        env.mons[i].type = MONS_HUMAN;
    const auto rng_before = rng::get_states();
    const auto before = roster_bytes();
    const auto cache = env.mid_cache;
    const auto last_mid = you.last_mid;
    const auto max_index = env.max_mon_index;
    REQUIRE(create_mercenary_shell(rec.id, blocked, 10,10) == nullptr);
    REQUIRE(roster_bytes() == before);
    REQUIRE(rng::get_states() == rng_before);
    REQUIRE(env.mid_cache == cache);
    REQUIRE(you.last_mid == last_mid);
    REQUIRE(env.max_mon_index == max_index);
    REQUIRE(env.mgrid(blocked) == NON_MONSTER);
    REQUIRE(existing->mid == existing_mid);
    REQUIRE(existing->get_experience_level() == other.xl);
}

TEST_CASE_METHOD(PlacementFixture, "Invalid requests and off-level copies cannot create duplicates",
                 "[muhyeop][placement]")
{
    auto &rec = mercenary_roster().create("Guarded", SP_HUMAN, JOB_FIGHTER,10,10,10);
    const coord_def pos(20,20);
    const auto before = roster_bytes();
    REQUIRE(create_mercenary_shell(999, pos,10,10) == nullptr);
    REQUIRE(create_mercenary_shell(rec.id, pos,11,10) == nullptr);
    REQUIRE(create_mercenary_shell(rec.id, coord_def(-1,-1),10,10) == nullptr);
    REQUIRE(roster_bytes() == before);
    for (auto state : {mercenary_roster_state::DOWNED, mercenary_roster_state::CARRIED,
                       mercenary_roster_state::DEAD})
    {
        rec.state = state;
        REQUIRE(create_mercenary_shell(rec.id,pos,10,10) == nullptr);
        REQUIRE(rec.state == state);
    }
    rec.state = mercenary_roster_state::ALIVE;
    rec.deployed = true; // Persistent reservation on an unloaded level.
    const auto bytes = roster_bytes();
    reset_mercenaries_for_new_game();
    reader in(bytes);
    mercenary_roster().load(in);
    const auto id = 1;
    REQUIRE(mercenary_roster().find(id)->deployed);
    REQUIRE(create_mercenary_shell(id,pos,10,10) == nullptr);
    mercenary_roster().find(id)->deployed = false;
    const level_id elsewhere(BRANCH_DUNGEON,2);
    auto &transit = the_lost_ones[elsewhere];
    transit.emplace_back();
    transit.back().mons.type = MONS_HUMAN;
    transit.back().mons.props[MUHYEOP_MERC_ID_KEY] = id;
    REQUIRE(create_mercenary_shell(id,pos,10,10) == nullptr);
    the_lost_ones.clear();
    companion_list[123].mons.mons.type = MONS_HUMAN;
    companion_list[123].mons.mons.props[MUHYEOP_MERC_ID_KEY] = id;
    REQUIRE(create_mercenary_shell(id,pos,10,10) == nullptr);
    companion_list.clear();
    REQUIRE(create_mercenary_shell(id,pos,10,10) != nullptr);
}


TEST_CASE_METHOD(PlacementFixture, "Ordinary monster creation and cloning retain native levels",
                 "[muhyeop][placement]")
{
    const coord_def pos(20,20);
    mgen_data mg(MONS_RAT, BEH_HOSTILE, pos, MHITNOT,
                 MG_FORCE_PLACE | MG_FORCE_BEH | MG_FORBID_BANDS | MG_NO_OOD);
    monster *original = create_monster(mg, false);
    REQUIRE(original != nullptr);
    REQUIRE_FALSE(is_mercenary_monster(*original));
    original->set_hit_dice(4);
    REQUIRE(original->get_experience_level() == 4);
    REQUIRE(original->get_hit_dice() == 4);
    monster *copy = clone_mons(original, true, nullptr, ATT_HOSTILE, coord_def(21,20));
    REQUIRE(copy != nullptr);
    REQUIRE(copy != original);
    REQUIRE_FALSE(is_mercenary_monster(*copy));
    REQUIRE(copy->get_experience_level() == 4);
    REQUIRE(copy->get_hit_dice() == 4);
}

TEST_CASE_METHOD(PlacementFixture, "Transit shell can load before its world roster",
                 "[muhyeop][placement][save]")
{
    auto &rec = mercenary_roster().create("Saved", SP_HUMAN, JOB_FIGHTER,10,10,10);
    rec.xl = 11;
    monster *shell = create_mercenary_shell(rec.id,coord_def(20,20),10,10);
    REQUIRE(shell != nullptr);
    vector<unsigned char> wire;
    writer out(&wire);
    marshallMonster(out, *shell);
    const auto records = roster_bytes();
    reset_mercenaries_for_new_game();
    monster loaded;
    reader in(wire, TAG_MINOR_VERSION);
    unmarshallMonster(in, loaded);
    // Native companion/transit readers copy deserialized actors before
    // read_mercenaries. This must not require an already loaded roster.
    monster copy(loaded);
    REQUIRE(is_mercenary_monster(copy));
    reader roster_input(records);
    mercenary_roster().load(roster_input);
    REQUIRE(copy.get_experience_level() == 11);
    REQUIRE(copy.get_hit_dice() == 11);
    REQUIRE(create_mercenary_shell(1,coord_def(21,20),10,10) == nullptr);
}
