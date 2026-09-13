/**
 * @file
 * @brief Muhyeop mercenary record foundation (no live spawning yet).
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "job-type.h"
#include "skill-type.h"
#include "species-type.h"

class monster;
class reader;
class writer;
using merc_id_t = int32_t;

enum class mercenary_roster_state : uint8_t
{
    ALIVE = 0,
    DOWNED = 1,
    CARRIED = 2,
    DEAD = 3,
};

// Record schema 1: equipment and martial arts are subsequent patches.
// Do not spawn a playable mercenary until those paths are connected.
struct MercenaryRecord
{
    MercenaryRecord(const MercenaryRecord &) = delete;
    MercenaryRecord &operator=(const MercenaryRecord &) = delete;

    const merc_id_t id;
    std::string name;
    species_type species;
    job_type background;
    int xl = 1;
    int xp = 0;
    int base_str;
    int base_int;
    int base_dex;
    mercenary_roster_state state = mercenary_roster_state::ALIVE;

    bool set_skill(skill_type sk, int level);
    int skill(skill_type sk, int scale = 1) const;

private:
    friend class MercenaryRoster;
    MercenaryRecord(merc_id_t id, const std::string &name,
                    species_type species, job_type background,
                    int strength, int intelligence, int dexterity);
    std::array<uint8_t, NUM_SKILLS> skills_{};
};

// Own records separately from the monster shell. Map order is deterministic,
// record addresses survive recruitment, and IDs are never vector indices.
// No dismissal API yet: that needs equipment ownership handling first.
class MercenaryRoster
{
public:
    MercenaryRecord &create(const std::string &name, species_type species,
                            job_type background, int strength,
                            int intelligence, int dexterity);
    MercenaryRecord *find(merc_id_t id);
    const MercenaryRecord *find(merc_id_t id) const;
    std::size_t size() const { return records_.size(); }
    void save(writer &out) const;
    // Parse and validate before replacing this roster. Existing pointers are
    // invalidated only on a successful load (never on a malformed record).
    void load(reader &in);

private:
    std::map<merc_id_t, std::unique_ptr<MercenaryRecord>> records_;
    uint32_t next_id_ = 1;
};

// World-owned data, independent of player copies and disposable shells.
MercenaryRoster &mercenary_roster();
// Only for a genuinely NEW world, not a generation transition.
void reset_mercenaries_for_new_game();
// Old saves have no roster bytes. Clear stale data without reading any bytes.
void read_mercenaries(reader &in);

// Presence and validity are deliberately different. A corrupt marker must
// never make a mercenary silently use the ordinary monster combat path.
enum class mercenary_link_status
{
    NOT_MERCENARY,
    INVALID_ID,
    MISSING_RECORD,
    LINKED,
};

struct MercenaryLink
{
    mercenary_link_status status;
    const MercenaryRecord *record;
};

extern const char MUHYEOP_MERC_ID_KEY[];
bool is_mercenary_monster(const monster &mon);
MercenaryLink lookup_mercenary(const monster &mon,
                               const MercenaryRoster &roster);
