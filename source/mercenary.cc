#include "AppHdr.h"

#include "mercenary.h"

#include <limits>
#include <stdexcept>

#include "monster.h"

const char MUHYEOP_MERC_ID_KEY[] = "muhyeop_merc_id";

MercenaryRoster &mercenary_roster()
{
    static MercenaryRoster roster;
    return roster;
}

void reset_mercenaries_for_new_game()
{
    mercenary_roster() = MercenaryRoster();
}

MercenaryRecord::MercenaryRecord(merc_id_t record_id, const std::string &record_name,
                                 species_type record_species, job_type record_background,
                                 int strength, int intelligence, int dexterity)
    : id(record_id), name(record_name), species(record_species),
      background(record_background),
      base_str(strength), base_int(intelligence), base_dex(dexterity)
{
}

bool MercenaryRecord::set_skill(skill_type sk, int level)
{
    if (sk < 0 || sk >= NUM_SKILLS || level < 0 || level > 27)
        return false;
    skills_[sk] = level;
    return true;
}

int MercenaryRecord::skill(skill_type sk, int scale) const
{
    if (sk < 0 || sk >= NUM_SKILLS || scale < 0)
        throw std::out_of_range("invalid mercenary skill query");
    const int level = skills_[sk];
    if (level && scale > std::numeric_limits<int>::max() / level)
        throw std::overflow_error("mercenary skill scale overflow");
    return level * scale;
}

MercenaryRecord &MercenaryRoster::create(const std::string &name,
                                        species_type species,
                                        job_type background, int strength,
                                        int intelligence, int dexterity)
{
    if (next_id_ > uint32_t(std::numeric_limits<merc_id_t>::max()))
        throw std::overflow_error("mercenary IDs exhausted");

    const merc_id_t id = next_id_;
    std::unique_ptr<MercenaryRecord> record(new MercenaryRecord(
        id, name, species, background, strength, intelligence, dexterity));
    MercenaryRecord *result = record.get();
    records_.emplace(id, std::move(record));
    ++next_id_;
    return *result;
}

MercenaryRecord *MercenaryRoster::find(merc_id_t id)
{
    const auto it = records_.find(id);
    return it == records_.end() ? nullptr : it->second.get();
}

const MercenaryRecord *MercenaryRoster::find(merc_id_t id) const
{
    const auto it = records_.find(id);
    return it == records_.end() ? nullptr : it->second.get();
}

bool is_mercenary_monster(const monster &mon)
{
    return mon.props.exists(MUHYEOP_MERC_ID_KEY);
}

MercenaryLink lookup_mercenary(const monster &mon,
                               const MercenaryRoster &roster)
{
    if (!is_mercenary_monster(mon))
        return {mercenary_link_status::NOT_MERCENARY, nullptr};

    const auto &value = mon.props[MUHYEOP_MERC_ID_KEY];
    if (value.get_type() != SV_INT || value.get_int() <= 0)
        return {mercenary_link_status::INVALID_ID, nullptr};

    const auto *record = roster.find(value.get_int());
    return {record ? mercenary_link_status::LINKED
                   : mercenary_link_status::MISSING_RECORD, record};
}


MercenaryShellSearch find_mercenary_shell(const monster *slots,
                                        std::size_t count, merc_id_t id)
{
    if (id <= 0 || (!slots && count))
        throw std::invalid_argument("invalid mercenary shell search");

    const monster *found = nullptr;
    for (std::size_t i = 0; i < count; ++i)
    {
        const monster &mon = slots[i];
        // alive() would exclude a DOWNED shell that still occupies a slot.
        if (mon.type == MONS_NO_MONSTER || !is_mercenary_monster(mon))
            continue;
        const auto &value = mon.props[MUHYEOP_MERC_ID_KEY];
        if (value.get_type() != SV_INT || value.get_int() != id)
            continue;
        if (found)
            return {mercenary_shell_status::DUPLICATE, nullptr};
        found = &mon;
    }
    return {found ? mercenary_shell_status::UNIQUE
                  : mercenary_shell_status::ABSENT, found};
}


mercenary_bind_status bind_mercenary_shell(monster *slots, std::size_t count,
                                          std::size_t target,
                                          merc_id_t id)
{
    if (!slots || target >= count || slots[target].type == MONS_NO_MONSTER)
        return mercenary_bind_status::INVALID_SLOT;
    const auto *record = mercenary_roster().find(id);
    if (!record || record->xl <= 0)
        return mercenary_bind_status::INVALID_RECORD;
    if (record->state != mercenary_roster_state::ALIVE)
        return mercenary_bind_status::NOT_ALIVE;
    monster &shell = slots[target];
    if (is_mercenary_monster(shell))
        return mercenary_bind_status::ALREADY_MARKED;
    if (find_mercenary_shell(slots, count, id).status
        != mercenary_shell_status::ABSENT)
        return mercenary_bind_status::DUPLICATE;
    for (int slot = 0; slot < NUM_MONSTER_SLOTS; ++slot)
        if (shell.inv[slot] != NON_ITEM)
            return mercenary_bind_status::HAS_INVENTORY;

    // All recoverable validation failures leave both shell and record intact.
    // Equipment belongs to the record; never adopt starting monster items.
    shell.props[MUHYEOP_MERC_ID_KEY] = int(id);
    shell.set_hit_dice(record->xl);
    return mercenary_bind_status::LINKED;
}
