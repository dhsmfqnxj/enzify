#include "AppHdr.h"

#include "mercenary.h"

#include <limits>

#include "errors.h"
#include "tags.h"

namespace
{
// MHR1. A later equipment/martial schema must explicitly handle older data.
const int32_t ROSTER_SCHEMA = 0x4d485231;
const uint64_t EXHAUSTED_ID = uint64_t(std::numeric_limits<merc_id_t>::max()) + 1;

void check(bool valid, const char *message)
{
    if (!valid)
        throw corrupted_save(message);
}

void validate_record(const MercenaryRecord &rec)
{
    check(rec.id > 0, "Invalid mercenary ID");
    check(rec.name.size() <= SHRT_MAX, "Mercenary name exceeds save format");
    check(rec.species >= 0 && rec.species < NUM_SPECIES, "Invalid mercenary species");
    check(rec.background >= 0 && rec.background < NUM_JOBS, "Invalid mercenary background");
    // Do not invent gameplay caps here. These fields use signed 32-bit storage.
    check(rec.xl > 0 && rec.xp >= 0, "Invalid mercenary progression");
    check(rec.base_str >= 0 && rec.base_int >= 0 && rec.base_dex >= 0,
          "Invalid mercenary base stats");
    check(static_cast<unsigned>(rec.state)
              <= static_cast<unsigned>(mercenary_roster_state::DEAD),
          "Invalid mercenary state");
}

// Same short-length wire format as marshallString, but reject a negative
// length in release builds too, before it can become a huge allocation/read.
string read_name(reader &in)
{
    const int length = unmarshallShort(in);
    check(length >= 0, "Invalid mercenary name length");
    string result(length, '\0');
    if (length)
        in.read(&result[0], length);
    return result;
}
}

void MercenaryRoster::save(writer &out) const
{
    check(next_id_ >= 1 && next_id_ <= EXHAUSTED_ID, "Invalid mercenary next ID");
    check(records_.size() <= size_t(INT_MAX), "Mercenary roster exceeds save format");
    // Validate the entire roster before emitting any bytes.
    for (const auto &entry : records_)
    {
        check(entry.first == entry.second->id && entry.first < int64_t(next_id_),
              "Inconsistent mercenary identity");
        validate_record(*entry.second);
    }

    marshallInt(out, ROSTER_SCHEMA);
    marshallUnsigned(out, next_id_);
    marshallInt(out, records_.size());
    marshallShort(out, NUM_SKILLS);
    for (const auto &entry : records_)
    {
        const MercenaryRecord &rec = *entry.second;
        marshallInt(out, rec.id);
        marshallString(out, rec.name);
        marshallInt(out, rec.species);
        marshallInt(out, rec.background);
        marshallInt(out, rec.xl);
        marshallInt(out, rec.xp);
        marshallInt(out, rec.base_str);
        marshallInt(out, rec.base_int);
        marshallInt(out, rec.base_dex);
        marshallUByte(out, static_cast<uint8_t>(rec.state));
        for (int sk = 0; sk < NUM_SKILLS; ++sk)
            marshallUByte(out, rec.skill(static_cast<skill_type>(sk)));
    }
}

void MercenaryRoster::load(reader &in)
{
    check(unmarshallInt(in) == ROSTER_SCHEMA, "Unsupported mercenary roster schema");
    const uint64_t next_id = unmarshallUnsigned(in);
    check(next_id >= 1 && next_id <= EXHAUSTED_ID, "Invalid mercenary next ID");
    const int count = unmarshallInt(in);
    const int skill_count = unmarshallShort(in);
    check(count >= 0, "Invalid mercenary roster count");
    check(skill_count > 0 && skill_count <= NUM_SKILLS, "Invalid mercenary skill count");

    MercenaryRoster pending;
    merc_id_t previous_id = 0;
    for (int i = 0; i < count; ++i)
    {
        const merc_id_t id = unmarshallInt(in);
        check(id > previous_id && uint64_t(id) < next_id,
              "Duplicate, unordered or invalid mercenary ID");
        previous_id = id;
        const string name = read_name(in);
        const int species = unmarshallInt(in);
        const int background = unmarshallInt(in);
        // Check before enum conversion so invalid enum values never escape.
        check(species >= 0 && species < NUM_SPECIES, "Invalid mercenary species");
        check(background >= 0 && background < NUM_JOBS, "Invalid mercenary background");
        std::unique_ptr<MercenaryRecord> rec(new MercenaryRecord(
            id, name, static_cast<species_type>(species),
            static_cast<job_type>(background), 0, 0, 0));
        rec->xl = unmarshallInt(in);
        rec->xp = unmarshallInt(in);
        rec->base_str = unmarshallInt(in);
        rec->base_int = unmarshallInt(in);
        rec->base_dex = unmarshallInt(in);
        const auto state = unmarshallUByte(in);
        check(state <= static_cast<uint8_t>(mercenary_roster_state::DEAD),
              "Invalid mercenary state");
        rec->state = static_cast<mercenary_roster_state>(state);
        for (int sk = 0; sk < skill_count; ++sk)
            check(rec->set_skill(static_cast<skill_type>(sk), unmarshallUByte(in)),
                  "Invalid mercenary skill level");
        validate_record(*rec);
        pending.records_.emplace(id, std::move(rec));
    }
    pending.next_id_ = next_id;
    records_.swap(pending.records_);
    std::swap(next_id_, pending.next_id_);
}

void read_mercenaries(reader &in)
{
#if TAG_MAJOR_VERSION == 34
    if (in.getMinorVersion() < TAG_MINOR_MUHYEOP_ROSTER)
    {
        reset_mercenaries_for_new_game();
        return;
    }
#endif
    mercenary_roster().load(in);
}
