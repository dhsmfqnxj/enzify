#include "AppHdr.h"
#include "mercenary.h"

#include "env.h"
#include "god-companions.h"
#include "mgen-data.h"
#include "mon-place.h"
#include "mon-transit.h"
#include "player.h"
#include "random.h"
#include "state.h"
#include "terrain.h"

namespace
{
bool blocks_identity(const monster &mon, merc_id_t id)
{
    if (!is_mercenary_monster(mon))
        return false;
    const auto &value = mon.props[MUHYEOP_MERC_ID_KEY];
    // Fail closed on corrupt off-level identity; do not guess its owner.
    return value.get_type() != SV_INT || value.get_int() <= 0
           || value.get_int() == id;
}

bool empty_inventory(const monster &mon)
{
    for (int i = 0; i < NUM_MONSTER_SLOTS; ++i)
        if (mon.inv[i] != NON_ITEM)
            return false;
    return true;
}
}

bool mercenary_has_offlevel_copy(merc_id_t id)
{
    for (const auto &level : the_lost_ones)
        for (const auto &entry : level.second)
            if (blocks_identity(entry.mons, id))
                return true;
    for (const auto &entry : companion_list)
        if (blocks_identity(entry.second.mons.mons, id))
            return true;
    for (const auto &entry : apostles)
        if (blocks_identity(entry.apostle.mons, id))
            return true;
    return false;
}

monster *create_mercenary_shell(merc_id_t id, const coord_def &pos,
                               int current_hp, int max_hp)
{
    auto *rec = mercenary_roster().find(id);
    // This first engine adapter is intentionally limited to human records.
    // Other species need their actor-safe anatomy/resistance paths first.
    if (!rec || rec->xl <= 0 || rec->species != SP_HUMAN || rec->deployed
        || rec->state != mercenary_roster_state::ALIVE
        || current_hp <= 0 || current_hp > max_hp || max_hp > MAX_MONSTER_HP
        || !in_bounds(pos) || you.pos() == pos || monster_at(pos)
        || !monster_habitable_grid(MONS_HUMAN, pos)
        || crawl_state.game_is_arena() || crawl_state.generating_level
        || !level_id::current().is_valid())
    {
        return nullptr;
    }
    for (int i = 0; i < MAX_MONSTERS; ++i)
        if (env.mons[i].type != MONS_NO_MONSTER
            && blocks_identity(env.mons[i], id))
            return nullptr;
    if (mercenary_has_offlevel_copy(id))
        return nullptr;

    int slot = 0;
    while (slot < MAX_MONSTERS && env.mons[slot].type != MONS_NO_MONSTER)
        ++slot;
    if (slot == MAX_MONSTERS)
        return nullptr;

    // Only this operation's empty slot can be rolled back. No band, unique,
    // item creation, announcements, god effects or initial AI evaluation.
    const mid_t old_mid = you.last_mid;
    const int old_max_index = env.max_mon_index;
    const auto old_rng = rng::current_generator();
    auto rollback = [&]()
    {
        monster &pending = env.mons[slot];
        if (pending.mid > old_mid)
            env.mid_cache.erase(pending.mid);
        pending.reset();
        you.last_mid = old_mid;
        env.max_mon_index = old_max_index;
        rng::current_generator() = old_rng;
    };

    try
    {
        mgen_data mg(MONS_HUMAN, BEH_FRIENDLY, pos, MHITNOT,
                     MG_FORCE_PLACE | MG_FORCE_BEH | MG_FORBID_BANDS | MG_NO_OOD);
        mg._mercenary_id = id;
        mg._mercenary_hp = current_hp;
        mg.hp = max_hp;
        mg.mname = rec->name;
        monster *shell = create_monster(mg, false);
        if (!shell || shell != &env.mons[slot] || shell->pos() != pos
            || !empty_inventory(*shell)
            || lookup_mercenary(*shell, mercenary_roster()).record != rec
            || find_mercenary_shell(&env.mons[0], MAX_MONSTERS, id).status
                != mercenary_shell_status::UNIQUE)
        {
            rollback();
            return nullptr;
        }
        rec->deployed = true;
        return shell;
    }
    catch (...)
    {
        rollback();
        throw;
    }
}

bool remove_mercenary_shell(merc_id_t id)
{
    auto *rec = mercenary_roster().find(id);
    if (!rec || !rec->deployed || mercenary_has_offlevel_copy(id))
        return false;
    const auto found = find_mercenary_shell(&env.mons[0], MAX_MONSTERS, id);
    if (found.status != mercenary_shell_status::UNIQUE)
        return false;
    monster &shell = env.mons[found.shell->mindex()];
    if (!empty_inventory(shell) || shell.is_constricted() || shell.is_constricting())
        return false;
    // No death event, XP, drops or roster deletion for controlled removal.
    env.mid_cache.erase(shell.mid);
    shell.reset();
    rec->deployed = false;
    return true;
}
