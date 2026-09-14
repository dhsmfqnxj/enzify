> 최신 상태: 파일 마지막 Checkpoint 12를 먼저 읽는다. P0 코드와 집중 테스트만 완료; 완성 게임 아님.

# 무협돌죽 / 탑 공략형 DCSS 0.34.1
# 실제 구현 명세서 + 패치 지도 — Checkpoint 01

작성일: 2026-09-13  
기준 소스: Dungeon Crawl Stone Soup 0.34.1  
설계 입력: `무협돌죽_탑공략형_DCSS_최신인계본_2026-09-13_§2916까지_업데이트.md`

> 이 문서는 기존 상세 설계 보존본을 **실제 DCSS 0.34.1 코드에 옮기기 위한 구현용 문서**로 전환하는 첫 체크포인트다.
> 여기서는 검증된 원본 파일/함수만 확정명으로 적는다.
> 아직 원본 소스에서 정확한 삽입 위치를 검증하지 못한 항목은 반드시 `[SOURCE CHECK REQUIRED]`로 표시한다.
> 이 문서는 “이미 패치가 완료되었다”는 뜻이 아니다. **실제 코딩을 바로 시작할 수 있도록 패치 위치와 순서를 고정하는 문서**다.

---

## 0. 구현 최우선 원칙

1. 원본 DCSS 0.34.1 player/monster 경로를 가능한 한 그대로 보존한다.
2. `you`를 mercenary로 임시 swap/overwrite하지 않는다.
3. mercenary는 새 `actor` 파생클래스를 만들지 않는다.
4. 영구 데이터 authority는 `MercenaryRecord`, 필드 representation은 기존 `monster` shell이다.
5. ordinary monster가 쓰는 기존 HD/attack-spec combat path는 변경하지 않는다.
6. player path는 결과가 바뀌지 않도록 유지한다.
7. mercenary에 필요한 player-like 계산만 작은 actor-safe helper로 추출하거나 mercenary 전용 얇은 branch에서 호출한다.
8. 실제 장비 `item_def`는 한 개만 존재하며 `MercenaryRecord` equipment slot이 owner다. `monster.inv`에 복사하지 않는다.
9. 기존 enum/save ID를 재정렬하지 않는다.
10. 패치는 컴파일 가능한 작은 단계로 쪼개고 각 단계마다 원본 player/monster 회귀테스트를 한다.

---

# 1. 확인된 원본 전투 진입점

## 1.1 `crawl-ref/source/fight.cc`

### 실제 함수
```cpp
bool fight_melee(actor *attacker, actor *defender, bool is_rampage,
                 bool *did_hit, bool simu)
```

### 원본 구조 확인
- player attacker:
  - `melee_attack attk(&you, defender);`
  - `attk.launch_attack_set();`
  - 성공/취소 판정 후 `you.time_taken = you.melee_attack_delay().roll();`
- non-player attacker:
  - 이후 monster 처리 구간으로 내려감.
- 따라서 mercenary shell은 아무 패치도 안 하면 **monster path**로 들어간다.

### 구현 결론
`fight_melee()` 자체를 대규모 복사하지 않는다.

mercenary도 기존 `melee_attack` 객체를 사용하되:
1. `melee_attack::launch_attack_set()`에서 mercenary shell을 일반 monster와 분리
2. `monster::attack_delay()` / `melee_attack_delay()`에서 mercenary만 player-skill 기반 delay 반환
3. `attack` 내부 hit/damage 계산에서 mercenary를 player-like 계산 branch로 연결

### 권장 분기 형태
```cpp
bool melee_attack::launch_attack_set(bool skip_player_post_attack)
{
    if (is_mercenary_actor(attacker))
        return run_mercenary_attack_set();

    if (!attacker->is_player())
        return run_monster_attack_set();

    ...
}
```

### 금지
```cpp
// 금지
attacker->is_player() == true 로 속이기
you = mercenary_data;
monster attack spec에 merc stat을 억지로 주입
```

---

# 2. `melee_attack` 실제 구조와 mercenary 진입 방식

## 2.1 `crawl-ref/source/melee-attack.cc`

### 생성자
```cpp
melee_attack::melee_attack(actor *attk, actor *defn,
                           int attack_num, int effective_attack_num)
    : attack(attk, defn), ...
{
    attack_position = attacker->pos();
    set_weapon(attacker->weapon(attack_num));
}
```

### 중요한 의미
생성자가 이미 `actor*`를 받고 `attacker->weapon()`을 호출한다.

즉 `monster::weapon()`을 mercenary-aware하게 만들면:
- melee_attack 생성자
- `primary_weapon()`
- 일부 공통 brand/weapon 처리

가 실제 `MercenaryRecord` 장비를 자연스럽게 볼 수 있다.

---

## 2.2 `melee_attack::set_weapon(item_def *wpn)`

원본은 다음처럼 attacker가 monster인지로 갈린다.

```cpp
weapon = mutable_wpn = wpn;

if (const monster* mons = attacker->as_monster())
{
    damage_brand = mons->damage_brand(attack_number);
    damage_type = mons->damage_type(attack_number);
}
else
{
    damage_brand = you.damage_brand(wpn);
    damage_type = you.damage_type(wpn);
}
```

### 문제
mercenary shell도 `as_monster()`가 참이므로 현재 코드는:
- monster `damage_brand(attack_number)`
- monster `damage_type(attack_number)`

를 사용한다.

이는 “실제 Record weapon `item_def`가 brand authority”라는 설계와 충돌한다.

### 패치
`set_weapon()`에 mercenary branch를 먼저 넣는다.

권장 개념:
```cpp
if (is_mercenary_actor(attacker))
{
    damage_brand = actor_weapon_brand(*attacker, wpn);
    damage_type  = actor_weapon_damage_type(*attacker, wpn);
}
else if (const monster* mons = attacker->as_monster())
{
    ...
}
else
{
    // 기존 player 코드 변경 금지
    ...
}
```

### 새 helper 후보
```cpp
brand_type actor_weapon_brand(const actor &attacker,
                              const item_def *weapon);

vorpal_damage_type actor_weapon_damage_type(const actor &attacker,
                                            const item_def *weapon);
```

단:
- player는 기존 player 함수와 결과가 **완전히 동일**해야 함.
- ordinary monster는 기존 `monster::damage_brand()` / `damage_type()` 유지.
- mercenary만 실제 `item_def` brand/weapon type을 읽는다.

`actor_weapon_brand()`를 모든 actor에 강제로 적용할 필요는 없다.
안정성 우선이면 초기 패치는 mercenary-specific helper로 시작해도 된다.

---

## 2.3 `melee_attack::launch_attack_set()`

원본:
```cpp
if (!attacker->is_player())
    return run_monster_attack_set();

bool success = run_player_attack_set();
...
player_attempted_attack(...);
```

### 문제
mercenary는 현재 `run_monster_attack_set()`으로 들어가며,
이 함수는 `MAX_NUM_ATTACKS`, `mons_attack_spec`, monster weapon wield 흐름을 사용한다.

### 패치
새 private method:
```cpp
bool melee_attack::run_mercenary_attack_set();
```

### 초기 구현 권장안
용병 기본 평타는 1회 공격:
```cpp
bool melee_attack::run_mercenary_attack_set()
{
    ASSERT(is_mercenary_actor(attacker));
    ASSERT(!attack_number);

    // constructor/set_weapon에서 실제 Record weapon이 이미 연결돼야 함.
    return attack();
}
```

### 이유
- monster multiattack spec 배제
- player `run_player_attack_set()`는 `you.weapon()`, `you.offhand_weapon()` 등 직접 global player를 읽으므로 그대로 호출하면 안 됨
- 공통 `attack()` phase pipeline은 actor* 기반이며 많은 후처리를 재사용 가능

### 이후 확장
dual wield 등을 mercenary에 허용해야 한다면 별도 actor-safe dual-wield helper로 확장.
초기 구현에서는 **단일 기본 공격**으로 시작하는 것이 가장 안전하다.

---

# 3. 명중 계산: 실제 수정 포인트

## 3.1 `crawl-ref/source/attack.cc`

### 실제 함수
```cpp
int attack::calc_pre_roll_to_hit(bool random)
```

원본은:
```cpp
if (stat_source().is_player())
{
    mhit = 15 + (you.dex() / 2);
    mhit += maybe_random2_div(you.skill(SK_FIGHTING, 100), ...);
    ...
}
else
{
    mhit = calc_mon_to_hit_base();
    ...
}
```

### 핵심 문제
mercenary shell은 `monster`이므로:
```cpp
stat_source().is_monster() == true
```
이고 monster HD 기반 명중식으로 들어간다.

`monster::skill()`만 고쳐서는 해결되지 않는다.

---

## 3.2 패치 전략

### 원본 player branch를 직접 망가뜨리지 않는다

다음 helper를 추출하는 방향을 권장:

```cpp
struct melee_hit_inputs
{
    int dex;
    int fighting_scaled_100;
    int weapon_skill_scaled_100;
    int xl;
    int slaying;
    int vertigo_penalty;
    int weapon_plus;
    int weapon_hit;
    bool using_weapon;
    bool form_uses_xl;
    bool unarmed;
};
```

또는 더 작은 actor-safe query helper 집합:

```cpp
int actor_melee_dex(const actor &a);
int actor_melee_skill(const actor &a, skill_type sk, int scale);
int actor_melee_slaying(const actor &a, bool throwing, bool random);
```

그러나 첫 패치에서 구조체를 과도하게 만들 필요는 없다.

### 권장 최소 패치
```cpp
if (stat_source().is_player())
{
    // 기존 player branch 그대로
}
else if (is_mercenary_actor(&stat_source()))
{
    mhit = calc_mercenary_playerlike_to_hit(...);
}
else
{
    // 기존 monster branch 그대로
}
```

### 새 함수 후보
```cpp
int calc_mercenary_playerlike_pre_roll_to_hit(
    const monster &merc,
    const item_def *weapon,
    skill_type wpn_skill,
    bool random);
```

이 함수 내부는 player branch의 actor-safe 부분만 동일한 integer/random semantics로 구현한다.

### mercenary에서 제외할 player-only 요소
초기 구현에서 다음은 자동 이식하지 않는다.
- Xom 자극
- player mutation 직접 조회
- player form 전용 적중 보정
- `you` duration 직접 조회
- player religion 관련 처리

용병에게 실제 공통 상태/장비로 존재하는 modifier만 별도 actor-safe query로 추가한다.

---

## 3.3 `attack::calc_to_hit(bool)`

원본은 roll 방식도 player/monster로 분기한다.

```cpp
const actor &src = stat_source();
if (src.is_player())
    mhit = maybe_random2(mhit, random);

mhit += post_roll_to_hit_modifiers(...);

if (!src.is_player())
    mhit = maybe_random2(mhit + 1, random);
```

### 의미
mercenary가 player-like pre-roll만 얻고 여기서는 monster roll을 타면
player와 완전히 같은 명중 semantics가 아니다.

### 패치
공통 predicate 제안:
```cpp
bool uses_playerlike_melee_math(const actor &a);
```

의미:
```cpp
return a.is_player() || is_mercenary_actor(&a);
```

단 **이 predicate는 melee math 안에서만 사용**한다.
`actor::is_player()` 자체 의미를 변경하면 안 된다.

예:
```cpp
const bool playerlike = uses_playerlike_melee_math(stat_source());

if (playerlike)
    mhit = maybe_random2(mhit, random);

mhit += post_roll_to_hit_modifiers(...);

if (!playerlike)
    mhit = maybe_random2(mhit + 1, random);
```

### 주의
`post_roll_to_hit_modifiers()` 내부 player-only 상태가 있는지 전체 검토 필요.

상태:
`[PARTIALLY VERIFIED — function body 추가 검토 필요]`

---

# 4. 피해 계산: 실제 수정 포인트

## 4.1 `attack::calc_damage()`

원본:
```cpp
if (stat_source().is_monster())
{
    // monster weapon + attk_damage / monster attack spec
}
else
{
    // player weapon/unarmed + stats + skill + slaying + AC
}
```

### 문제
mercenary는 지금 monster branch다.
이 branch는:
- `attk_damage`
- monster attack spec
- monster식 slaying/damage modifier

를 사용한다.

설계상 mercenary에서 금지한 경로다.

---

## 4.2 패치 원칙

```cpp
if (stat_source().is_monster()
    && !is_mercenary_actor(&stat_source()))
{
    // 기존 monster branch 그대로
}
else
{
    // player/merc player-like
}
```

하지만 원본 player branch 안에는 직접 `you`를 읽는 함수들이 있다:
- `player_apply_slaying_bonuses`
- `player_stab`
- 일부 mutation/form/player state

따라서 branch만 합치면 안 된다.

### 권장 실제 구조
```cpp
if (is_mercenary_actor(&stat_source()))
    return calc_mercenary_playerlike_damage();

if (stat_source().is_monster())
    return existing_monster_damage_path();

return existing_player_damage_path();
```

### 새 method 후보
`attack` 또는 `melee_attack` private:
```cpp
int attack::calc_mercenary_playerlike_damage();
```

혹은 공통 helper:
```cpp
int calc_actor_playerlike_melee_damage(
    const actor &attacker,
    actor &defender,
    const item_def *weapon,
    skill_type weapon_skill,
    ...);
```

초기 안정성은 **mercenary helper를 먼저 만든 뒤 player path와 동일성 테스트를 통해 점진적으로 공통화**하는 쪽을 추천한다.
처음부터 player 코드를 대규모 템플릿화하지 않는다.

---

## 4.3 반드시 재사용할 원본 연산

원본 player damage 흐름에서 actor-safe하게 재사용해야 할 계산:
- `adjusted_weapon_damage()`
- `stat_modify_damage(...)`
- weapon skill scaling
- Fighting scaling
- defender AC 처리
- 원본 random/integer rounding semantics

### `[SOURCE CHECK REQUIRED]`
아래 함수들의 선언/내부 `you` 의존도를 정확히 더 조사해야 한다.
- `attack::apply_weapon_skill`
- `attack::apply_fighting_skill`
- `stat_modify_damage`
- player slaying helper
- unarmed base damage helper

Checkpoint 02에서 실제 함수별로 “그대로 재사용 / helper 추출 / merc branch 별도 구현”을 확정한다.

---

# 5. Unarmed: 현재 원본에서 그대로는 불가능

## 실제 함수
```cpp
int attack::calc_base_unarmed_damage() const
```

원본에는:
```cpp
if (!attacker->is_player())
    return 0;
```

따라서 mercenary가 무기를 안 들었을 때 이 함수를 그대로 호출하면 base UC damage가 0이 된다.

### 패치
mercenary용 actor-safe UC helper가 필요하다.

제안:
```cpp
int calc_actor_unarmed_base_damage(const actor &a);
```

동작:
- player: 기존 `unarmed_base_damage(true) + unarmed_base_damage_bonus(true)`와 결과 동일
- mercenary: 실제 species/trait/Unarmed skill/허용된 공통 상태 기준
- ordinary monster: 기존 monster attack spec 경로 유지

### 중요
player 전용 form/mutation 전부를 mercenary에 자동 복제하지 않는다.
용병에게 실제 존재하는 trait/species/장비 규칙만 지원한다.

상태:
`[SOURCE CHECK REQUIRED — unarmed_base_damage 정의/의존성 추가 추적 필요]`

---

# 6. Weapon brand / damage type

## 원본 확인
`melee_attack::set_weapon()`에서 monster attacker는:
```cpp
mons->damage_brand(attack_number);
mons->damage_type(attack_number);
```

`monster::damage_brand()` 자체는 `monster::weapon(which_attack)`을 읽어서
weapon brand를 반환할 수 있지만,
`monster::weapon()`이 현재 `monster.inv[]`와 `mons_attack_spec()`을 사용한다.

따라서 mercenary가 Record 장비만 갖는 설계에서는 **monster::weapon() 패치가 필수**다.

---

# 7. `monster` shell 인터페이스: 실제 수정 포인트

## 7.1 `crawl-ref/source/monster.cc`

### 실제 함수
```cpp
int monster::skill(skill_type sk, int scale, bool real, bool temp) const
```

원본은 실질적으로 HD 기반이다.

### 패치
함수 맨 앞:
```cpp
if (is_mercenary())
    return mercenary_skill(*this, sk, scale, real, temp);
```

그 뒤 기존 원본 코드는 그대로.

제안 helper:
```cpp
const MercenaryRecord *get_mercenary_record(const monster &mon);
MercenaryRecord *get_mercenary_record(monster &mon);

bool monster::is_mercenary() const;
```

`is_mercenary()`의 실제 판정 저장 위치는 아래 Record/shell linkage 설계에서 확정.

---

## 7.2 `monster::weapon(int which_attack)`

원본은:
- `mons_attack_spec(*this, which_attack)`
- `inv[MSLOT_WEAPON]`
- `inv[MSLOT_ALT_WEAPON]`

을 사용한다.

### mercenary 패치
함수 최상단:
```cpp
if (is_mercenary())
    return mercenary_equipped_weapon(*this, which_attack);
```

초기 규칙:
- `which_attack == 0` → Record main weapon
- 기본 평타에서 offhand/multiattack은 사용하지 않음
- actual `item_def`는 Record slot에 존재
- shell `inv[]`에는 넣지 않음

### 반환형 주의
원본 signature가 `item_def *`이므로 Record 안 actual item 객체 주소가 안정적으로 유지되어야 한다.

따라서 `MercenaryRecord` equipment storage는:
- 공격 중 vector reallocation으로 주소가 바뀌는 구조 금지
- fixed slot array / stable owning storage 선호

`[DATA STRUCTURE DESIGN REQUIRED — Checkpoint 02]`

---

## 7.3 `monster::attack_delay(const item_def *projectile)`

원본 monster delay:
```cpp
const item_def* weap = weapon();
if (!weap || ...)
    return random_var(10);

random_var delay(weapon_adjust_delay(*weap, 10));
```

이는 실제 mercenary weapon skill을 사용하지 않는다.

### mercenary branch
```cpp
if (is_mercenary())
    return mercenary_attack_delay(*this, projectile);
```

### 원본에서 확인된 재사용 함수
`fight.cc`:
```cpp
int weapon_min_delay_skill(const item_def &weapon);
int weapon_min_delay(const item_def &weapon, bool check_speed);
```

또 `weapon_adjust_delay(...)`가 존재한다.

### `[SOURCE CHECK REQUIRED]`
player의 정확한 `player::attack_delay()` / `player::melee_attack_delay()` 계산을 더 추적해
어느 부분이 skill 기반이고 어느 부분이 player-only 상태인지 분리해야 한다.

그 전에는 merc delay 공식을 새로 작성하지 않는다.

---

# 8. `fight_melee()` energy 처리와 mercenary

원본 non-player 구간은 monster scheduler/energy semantics를 사용한다.

이는 프로젝트 방향과 맞는다:
- mercenary field presence는 monster shell
- scheduler/energy도 monster shell이 담당

따라서 **mercenary를 player turn system으로 넣으면 안 된다.**

구조:
- 공격 계산: player-like
- 행동 스케줄링/energy: monster shell

즉:
```text
monster scheduler
    ↓
fight_melee(merc_shell, target)
    ↓
melee_attack
    ↓
mercenary player-like to-hit/damage
    ↓
monster shell energy 차감
```

이 분리가 핵심이다.

---

# 9. `MercenaryRecord` / shell linkage — 패치 초안

아직 원본 roster 저장 위치 전체를 조사하지 않았으므로
새 파일 배치는 프로젝트 추가 파일로 제안한다.

## 새 파일 제안
```text
crawl-ref/source/mercenary.h
crawl-ref/source/mercenary.cc
```

## 최소 API
```cpp
using merc_id_t = int32_t;

enum class mercenary_roster_state : uint8_t
{
    ALIVE,
    DOWNED,
    CARRIED,
    DEAD,
};

struct MercenaryRecord
{
    merc_id_t id;

    // identity/progression
    string name;
    species_type species;
    job_type background;
    int xl;
    int xp;

    // stats / skills
    int base_str;
    int base_int;
    int base_dex;
    array<int, NUM_SKILLS> skills;

    // actual owned equipment
    // exact storage layout: Checkpoint 02에서 확정

    // martial/spell/traits/etc
    // 기존 설계본에서 단계적으로 옮김

    mercenary_roster_state state;
};
```

### 주의
실제 skill 저장 단위가 player skill과 동일한 fixed-point scale인지
현재 설계본과 원본 player skill storage를 대조한 뒤 타입을 확정해야 한다.

따라서 위 `array<int, NUM_SKILLS>`는 **개념 초안**이다.

---

## shell linkage 최소 API

```cpp
bool is_mercenary_monster(const monster &mon);

merc_id_t mercenary_id(const monster &mon);

MercenaryRecord *find_mercenary(merc_id_t id);
const MercenaryRecord *find_mercenary(merc_id_t id);

MercenaryRecord *record_for(const monster &mon);
```

### `monster`에 merc_id를 저장하는 방법
두 후보:

A. explicit field 추가
```cpp
merc_id_t merc_id;
```

B. `monster::props`에 저장

### 현재 추천
**explicit field** 쪽이 핵심 identity에는 더 안전하다.

이유:
- 전투 hot path에서 빈번하게 조회
- 타입 안전
- `props` string-key lookup 피함
- mercenary 여부가 actor identity의 핵심

하지만 save compatibility와 monster marshalling 위치를 더 검증해야 하므로:

`[SOURCE CHECK REQUIRED — monster save/marshalling 확인 후 최종 확정]`

---

# 10. Save system: 실제 확인된 파일/함수

## 10.1 `crawl-ref/source/tags.cc`

실제 `TAG_YOU` write 순서:
```cpp
_tag_construct_you(th);
CANARY;
_tag_construct_you_items(th);
CANARY;
_tag_construct_you_dungeon(th);
CANARY;
_tag_construct_lost_monsters(th);
CANARY;
_tag_construct_companions(th);
```

read도 대칭 구조다.

실제 함수명은:
```cpp
_tag_construct_you(...)
_tag_read_you(...)
_tag_construct_you_items(...)
_tag_construct_you_dungeon(...)
```

즉 예전 설계 문서에서 추상적으로 말한 `tag_construct_you`가 아니라
0.34.1 실제 함수는 앞에 `_`가 붙는다.

---

## 10.2 item serialization 재사용

원본 save 계층에:
```cpp
marshallItem(...)
unmarshallItem(...)
```
계열이 존재하고 player inventory/shop 등 실제 item 저장에 사용된다.

### 구현 원칙
MercenaryRecord equipment의 actual `item_def`도
새 임의 binary item serializer를 만들지 말고 원본 item marshalling helper를 재사용한다.

---

## 10.3 save minor version

`crawl-ref/source/tag-version.h`

enum 끝:
```cpp
TAG_MINOR_MONINFO_CLEANUP,
...
NUM_TAG_MINORS,
TAG_MINOR_VERSION = NUM_TAG_MINORS - 1
```

### 패치
새 minor는 **맨 뒤에 append**:
```cpp
TAG_MINOR_MUHYEOP_MERCENARIES,
NUM_TAG_MINORS,
```

정확한 이름은 프로젝트 스타일에 맞춰 확정 가능.

기존 enum 순서 변경 금지.

---

## 10.4 MercenaryRecord 저장 위치

추천 방향:
- `TAG_YOU`의 append-only compatible 영역에 mercenary roster block 추가
- 기존 item marshalling helper 사용
- 새 tag minor 이상에서만 읽기
- 구버전 save에서는 empty roster로 초기화

그러나 `_tag_construct_you`, `_tag_construct_you_dungeon`,
`_tag_read_you`, `_tag_read_you_dungeon`의 전체 끝부분을 더 조사해야
**정확히 어디에 append하는 것이 가장 안전한지** 확정 가능하다.

상태:
`[SOURCE CHECK REQUIRED — Checkpoint 02]`

### 금지
- 별도 독자 save 파일
- JSON sidecar
- 이름 문자열을 stable save key로 사용
- monster shell과 Record 양쪽에 장비 복제 저장

---

# 11. 현재까지 드러난 가장 중요한 구현 사실

## 설계본만 보고는 알기 어려웠던 실제 소스상의 문제

### 문제 A
`melee_attack` 자체가 actor-generic처럼 보여도
실제로 `launch_attack_set()`에서 player/monster를 강하게 갈라놓는다.

### 문제 B
`attack::calc_pre_roll_to_hit()`는 player global `you`를 직접 읽는다.

### 문제 C
`attack::calc_damage()`도 `stat_source().is_monster()` 기준으로
monster attack spec 경로와 player 경로를 갈라놓는다.

### 문제 D
`calc_base_unarmed_damage()`는 non-player에게 바로 0을 반환한다.

### 문제 E
`monster::weapon()`은 `monster.inv[]`와 `mons_attack_spec()` 기반이다.

따라서 “MercenaryRecord skills만 연결하면 player형 전투가 된다”는 수준으로는 구현이 불가능하다.

---

# 12. 실제 패치 순서

## P0 — 기반 타입만 추가, gameplay 변경 없음
목표: 컴파일 성공, 원본 게임 결과 변화 0.

작업:
1. `mercenary.h/.cc` 추가
2. `merc_id_t`
3. 최소 `MercenaryRecord`
4. roster container
5. `find_mercenary()`
6. `is_mercenary_monster()` skeleton
7. 아직 실제 mercenary spawn 없음

테스트:
- 원본 player melee 동일
- ordinary monster melee 동일
- save/load 기존 게임 동일

---

## P1 — shell ↔ Record identity
목표: friendly monster shell 하나를 Record에 연결 가능.

작업:
1. merc_id 저장 위치 확정
2. monster save marshalling 확인
3. shell lookup
4. one-active-shell invariant
5. shell spawn/despawn helper
6. Record 삭제 없이 shell 제거 가능

아직:
- custom melee 없음
- custom equipment 없음

테스트:
- spawn → save → load → merc_id 연결
- despawn → respawn 동일 Record
- duplicate shell 방지

---

## P2 — equipment/skill query 연결
목표: merc shell이 실제 Record 장비/skill을 조회.

패치:
- `monster::skill()`
- `monster::weapon()`
- shield/body armour/wearing/artefact scan 등
- 실제 Record `item_def` 저장/소유권

아직 combat formula는 일반 monster일 수 있음.
이 단계는 조회/ownership 검증용.

테스트:
- 장비 중복 item 없음
- monster.inv 비어 있어도 merc `weapon()`이 Record weapon 반환
- ordinary monster inventory path 동일

---

## P3 — mercenary basic melee
목표: HD/attack-spec이 아닌 player-like 실제 stat/skill 평타.

패치:
- `melee_attack::launch_attack_set`
- `run_mercenary_attack_set`
- `melee_attack::set_weapon`
- `attack::calc_pre_roll_to_hit`
- `attack::calc_to_hit`
- `attack::calc_damage`
- UC helper
- `monster::attack_delay`
- 필요한 actor-safe stat/slaying helper

테스트:
1. 동일 weapon/stat/skill seed에서 player와 merc 핵심 to-hit/damage 비교
2. merc HD를 바꿔도 Record XL/skill이 같으면 평타 공식이 HD 때문에 변하지 않는지
3. ordinary monster 결과 동일
4. player 결과 동일
5. miss도 정상 energy 소모
6. precheck invalid target은 0 action
7. brand once
8. death/reward once

---

## P4 — save roster/equipment
P1/P2 데이터가 안정화된 뒤 적용.

작업:
- `tag-version.h` minor append
- TAG_YOU에 MercenaryRecord roster append
- actual item marshalling
- 구버전 load fallback
- corruption validation

---

## P5 이후
- final stats/UI
- mercenary spells
- martial system
- tactical commands
- generation/world persistence
- floor regeneration
- boss/unique permanent-death registry

각 단계는 소스 매핑 후 별도 checkpoint에서 확정한다.

---

# 13. Checkpoint 02에서 반드시 조사할 원본 소스

1. `player::attack_delay()` / `player::melee_attack_delay()` 실제 정의
2. `attack::apply_weapon_skill`
3. `attack::apply_fighting_skill`
4. `stat_modify_damage`
5. unarmed damage 계산 함수 전체
6. `monster::shield/body_armour/wearing/scan_artefacts` 실제 구현
7. monster serialization (`_marshall_monster`, `_unmarshall_monster` 계열 실제 함수명)
8. `_tag_construct_you_dungeon` / corresponding read의 정확한 끝부분
9. player skill 저장 format
10. monster creation / `mgen_data` / `create_monster` 실제 shell spawn 경로

---

# 14. 현재 상태 표시

| 영역 | 상태 |
|---|---|
| `fight_melee` 진입 | VERIFIED |
| `melee_attack` constructor | VERIFIED |
| player/monster attack-set fork | VERIFIED |
| `set_weapon` brand fork | VERIFIED |
| to-hit player/monster fork | VERIFIED |
| damage player/monster fork | VERIFIED |
| non-player UC=0 문제 | VERIFIED |
| `monster::skill` HD 기반 | VERIFIED |
| `monster::weapon` inv/attack-spec 기반 | VERIFIED |
| `monster::attack_delay` generic monster 방식 | VERIFIED |
| TAG_YOU write structure | VERIFIED |
| tag minor append location | VERIFIED |
| exact merc roster save insertion point | SOURCE CHECK REQUIRED |
| exact monster merc_id serialization point | SOURCE CHECK REQUIRED |
| player delay skill formula extraction | SOURCE CHECK REQUIRED |
| player unarmed actor-safe extraction | SOURCE CHECK REQUIRED |
| actual source code patch | NOT YET APPLIED |

---

# 15. 검증한 0.34.1 원본 파일

- `crawl-ref/source/fight.cc`
- `crawl-ref/source/melee-attack.cc`
- `crawl-ref/source/attack.cc`
- `crawl-ref/source/monster.cc`
- `crawl-ref/source/monster.h`
- `crawl-ref/source/tags.cc`
- `crawl-ref/source/tag-version.h`

Source baseline:
`https://github.com/crawl/crawl/tree/0.34.1`

---

## Checkpoint 01 결론

현재 단계에서 제작자가 가장 먼저 알아야 할 핵심은 다음이다.

**용병은 monster shell을 그대로 사용하지만, 기존 monster melee path를 그대로 쓰면 안 된다.**

실제 DCSS 0.34.1의 전투 코드는:
- `fight_melee`
- `melee_attack::launch_attack_set`
- `attack::calc_pre_roll_to_hit`
- `attack::calc_damage`
- `monster::weapon`
- `monster::attack_delay`
- `monster::skill`

에서 player/monster가 서로 다른 전투 semantics를 갖는다.

따라서 안정적인 구현은:
1. shell/scheduler는 monster로 유지
2. `actor::is_player()` 의미는 건드리지 않음
3. mercenary 판별을 별도로 둠
4. melee math만 player-like branch/helper로 재사용
5. ordinary monster/player 원본 path는 그대로 유지

하는 방식이다.


---

# Checkpoint 02 — 실제 원본 계산 helper / 장비 accessor / 저장호환 경계 추가 매핑

> 상태 표기
> - `VERIFIED`: DCSS 0.34.1 소스/동일 0.34.1 Debian source에서 실제 선언·구조 확인
> - `PATCH DECISION`: 현재 프로젝트 안정성 원칙으로 구현 방향 확정
> - `[SOURCE CHECK REQUIRED]`: 구현 전 정확한 원본 정의 위치/호출부 추가 확인 필요
>
> 이번 체크포인트의 목적은 **P2(Record 조회/장비)와 P3(평타 계산)에서 무엇을 실제로 추출할지**를 구체화하는 것이다.

---

## 16. `fight.cc` — player 피해 계산 helper를 actor-safe overload로 바꾸는 실제 전략

### VERIFIED — 원본 함수

`crawl-ref/source/fight.cc`에는 player 기본 근접 피해 계산에서 쓰이는 다음 함수들이 존재한다.

```cpp
bool weapon_uses_strength(skill_type wpn_skill);

int stat_modify_damage(int damage, skill_type wpn_skill);
int apply_weapon_skill(int damage, skill_type wpn_skill, bool random);
int apply_fighting_skill(int damage, bool aux, bool random);
```

현재 구현의 핵심 의존성:

```cpp
stat_modify_damage(...)
    -> weapon_uses_strength(wpn_skill)
    -> you.strength() 또는 you.dex()

apply_weapon_skill(...)
    -> you.skill(wpn_skill, 100)

apply_fighting_skill(...)
    -> you.skill(SK_FIGHTING, 100)
```

즉 함수 이름만 보면 순수 계산 함수처럼 보이지만 실제로는 global `you`에 묶여 있다.

---

## 16.1 PATCH DECISION — 기존 player wrapper 유지 + actor overload 추가

원본 player behavior를 건드리지 않기 위해 **기존 signature는 그대로 남긴다.**

권장 형태:

```cpp
int stat_modify_damage(const actor &attacker,
                       int damage,
                       skill_type wpn_skill);

int apply_weapon_skill(const actor &attacker,
                       int damage,
                       skill_type wpn_skill,
                       bool random);

int apply_fighting_skill(const actor &attacker,
                         int damage,
                         bool aux,
                         bool random);
```

기존 함수는 wrapper:

```cpp
int stat_modify_damage(int damage, skill_type wpn_skill)
{
    return stat_modify_damage(you, damage, wpn_skill);
}

int apply_weapon_skill(int damage, skill_type wpn_skill, bool random)
{
    return apply_weapon_skill(you, damage, wpn_skill, random);
}

int apply_fighting_skill(int damage, bool aux, bool random)
{
    return apply_fighting_skill(you, damage, aux, random);
}
```

### 중요한 조건

`actor &` overload가 모든 monster를 지원해야 한다는 뜻이 아니다.

초기 구현 contract:

```cpp
ASSERT(attacker.is_player() || is_mercenary_actor(attacker));
```

- player → 기존 수치와 완전히 동일
- mercenary → `MercenaryRecord` 실제 stat/skill
- ordinary monster → 이 helper를 호출하지 않음

### 이유

이 방식이면:
- 원본 player call-site를 한꺼번에 수정할 필요 없음
- player regression risk가 낮음
- mercenary가 같은 정수/RNG 계산식을 공유 가능
- monster HD 기반 기존 경로 유지

---

# 17. player-like STR / DEX / skill query의 실제 경계

## PATCH DECISION

`fight.cc` 계산 helper가 `actor`를 받더라도 직접 다음처럼 쓰지 않는다.

```cpp
attacker.as_player()->strength()
```

mercenary는 player가 아니기 때문이다.

대신 좁은 melee input helper:

```cpp
int playerlike_melee_str(const actor &attacker);
int playerlike_melee_dex(const actor &attacker);

int playerlike_melee_skill(const actor &attacker,
                           skill_type skill,
                           int scale);
```

개념 구현:

```cpp
int playerlike_melee_skill(const actor &a, skill_type sk, int scale)
{
    if (a.is_player())
        return you.skill(sk, scale);

    if (const monster *mon = a.as_monster();
        mon && is_mercenary_monster(*mon))
    {
        const MercenaryRecord &rec = require_mercenary_record(*mon);
        return scale_mercenary_skill(rec, sk, scale);
    }

    die("playerlike_melee_skill called for non-playerlike actor");
}
```

### 금지
- ordinary monster를 여기로 보내기
- mercenary skill을 shell HD에서 계산
- `you.skills[]`를 잠시 덮어쓰기
- UI용 cached skill을 combat에 사용

---

# 18. MercenaryRecord skill 저장 단위

## VERIFIED — 원본 player 저장형태

`player`는 최소한:
```cpp
FixedVector<uint8_t, NUM_SKILLS> skills;
```
를 장기 상태로 가진다.

하지만 player의 실제 `player::skill()`은 단순 `skills[sk]`만 읽는 것이 아니라
훈련/skill point/cross-training/temporary modifier 등의 player 전용 의미를 포함할 수 있다.

## PATCH DECISION

용병은 **이미 설계에서 확정한 실제 0~27 스킬값**을 authority로 사용한다.

첫 구현에서는 player training internals를 복제하지 않는다.

추천 저장:

```cpp
using merc_skill_t = uint8_t;

FixedVector<merc_skill_t, NUM_SKILLS> skills;
```

또는 프로젝트 build 환경에서 `FixedVector` 사용이 불편하면:

```cpp
array<uint8_t, NUM_SKILLS> skills;
```

### 조회
```cpp
int mercenary_skill(const MercenaryRecord &rec,
                    skill_type sk,
                    int scale)
{
    ASSERT_RANGE(sk, 0, NUM_SKILLS);
    return rec.skills[sk] * scale;
}
```

### 주의
이것은 `player::skill()` 내부를 그대로 복사한다는 뜻이 아니다.
용병의 skill authority 자체가 이미 0~27 최종 실제값이기 때문에
player 훈련용 skill-point 계층을 추가하면 이중 원본이 된다.

---

# 19. Unarmed — player helper 전체 generic화 금지

## VERIFIED

원본 player UC 계산은 `fight.cc`에서 대략 다음 계층을 사용한다.

```cpp
int unarmed_base_damage(bool random);
int unarmed_base_damage_bonus(bool random);
```

그리고 내부에서:
- `get_form()`
- `you.has_usable_claws()`
- `you.has_claws()`
- `you.form_uses_xl()`
- `you.skill(SK_UNARMED_COMBAT)`

등 global player/form/mutation 상태를 직접 읽는다.

또 기존 attack/melee 경로에는 non-player UC를 player UC처럼 처리하지 않는 분기가 있다.

## PATCH DECISION

초기 P3에서는 player UC 함수 전체를 actor generic으로 뜯지 않는다.

새 helper:

```cpp
int mercenary_unarmed_base_damage(const monster &merc, bool random);
int mercenary_unarmed_skill_bonus(const monster &merc, bool random);
```

입력:
- 실제 Record STR/DEX
- `SK_UNARMED_COMBAT`
- 실제 species/trait 중 **용병에게 구현된 것만**
- 실제 equipment restrictions

### 금지
- player transformation global `get_form()`을 mercenary에 호출
- `you` mutation을 mercenary 특성처럼 사용
- monster attack dice를 UC base damage로 fallback

### 향후
species/trait aux를 추가할 때 각 trait를 actor-safe source로 하나씩 옮긴다.
처음부터 player form subsystem 전체를 generic화하지 않는다.

---

# 20. `monster` 장비 accessor — 실제 원본 구조

## VERIFIED — `monster.h`

0.34.1 `monster`는 actor interface에 다음을 제공한다.

```cpp
int wearing(object_class_type obj_type, int sub_type,
            bool count_plus = 0, bool check_attuned = false) const override;

int wearing_ego(object_class_type obj_type, int ego) const override;

int scan_artefacts(artefact_prop_type which_property,
                   vector<const item_def *> *matches = nullptr) const override;

bool unrand_equipped(int unrand_index,
                     bool include_melded = false) const override;

item_def *mslot_item(mon_inv_type sl) const;

item_def *weapon(int which_attack = -1) const override;
item_def *launcher() const;
item_def *melee_weapon() const;
item_def *missiles() const;

item_def *shield() const override;
item_def *offhand_item() const override;
item_def *body_armour() const override;
```

## VERIFIED — `monster.cc`

`mslot_item()`은 monster inventory index를 읽어 `env.item[]`을 반환한다.

즉 일반 monster 장비 ownership은 대략:
```text
monster.inv[MSLOT_*] → env.item[index]
```

프로젝트 mercenary 설계:
```text
MercenaryRecord equipment slot → actual item_def
```

이므로 그대로 사용할 수 없다.

---

# 21. PATCH DECISION — mercenary는 accessor 최상단에서 Record로 분기

예:

```cpp
item_def *monster::weapon(int which_attack) const
{
    if (is_mercenary())
        return mercenary_weapon(*this, which_attack);

    // 기존 원본 코드 그대로
}
```

같은 방식으로 우선 패치:

```cpp
monster::weapon()
monster::shield()
monster::offhand_item()
monster::body_armour()
monster::wearing()
monster::wearing_ego()
monster::scan_artefacts()
monster::unrand_equipped()
```

### 중요한 원칙

`mslot_item()` 자체를 무조건 merc-aware로 만들어 모든 장비 의미를 감추는 것보다,
**공개 actor equipment 의미를 가진 accessor에서 merc branch를 명시**하는 쪽이 초기 디버깅에 안전하다.

ordinary monster:
```text
monster.inv / env.item
```

mercenary:
```text
MercenaryRecord actual item slots
```

두 ownership 구조를 억지로 합치지 않는다.

---

# 22. 중요한 발견 — accessor만 바꾸면 AC/저항이 전부 해결되지 않음

## VERIFIED

0.34.1 `monster`에는 다음 actor defense interface가 있다.

```cpp
int armour_class() const override;
int evasion(bool ignore_temporary = false,
            const actor *attacker = nullptr) const override;

bool shielded() const override;
int shield_class() const;
int shield_bonus() const override;
int shield_bypass_ability(int tohit) const override;

int res_fire() const override;
int res_cold() const override;
int res_elec() const override;
int res_poison(bool temp = true) const override;
int res_negative_energy(bool intrinsic_only = false) const override;
int res_corr() const override;
int willpower(...) const override;
...
```

그리고 monster resist/defense 구현 일부는 단순히 `body_armour()` 같은 virtual/accessor를 통하지 않고
`monster.inv[MSLOT_*]` 및 monster innate data를 직접 보는 부분이 있다.

### 결론

`weapon()/shield()/body_armour()`만 Record로 연결하고 끝내면:
- 표시 장비는 맞는데
- AC/rF/rC/rPois/Will이 monster innate/inv 기반으로 남는

**전투/UI 불일치**가 생길 수 있다.

---

# 23. PATCH DECISION — mercenary final defense/resistance branch

P2 다음에 별도 **P2.5** 단계 추가.

패치 대상:

```cpp
monster::armour_class()
monster::evasion(...)
monster::shielded()
monster::shield_class()
monster::shield_bonus()
monster::shield_bypass_ability(...)
monster::res_fire()
monster::res_cold()
monster::res_elec()
monster::res_poison(...)
monster::res_negative_energy(...)
monster::res_corr()
monster::willpower(...)
```

정확한 전체 res 함수 목록은 `actor` interface 전수검색 후 확정.

각 함수 최상단:

```cpp
if (is_mercenary())
    return mercenary_final_xxx(*this);
```

ordinary monster body는 그대로 둔다.

---

# 24. 공통 final stat 계층의 실제 코드 배치

새 파일 후보:

```text
mercenary.h/.cc
actor-stats.h/.cc         [새 파일 후보]
```

안정성 우선 첫 구현에서는 새 거대한 `actor-stats` subsystem을 만들지 않는다.

추천:

```text
mercenary.h/.cc
    ├─ record lookup
    ├─ raw stat/skill/equipment query
    └─ mercenary final AC/EV/SH/resistance helper
```

공통화가 실제로 반복될 때만:
```text
actor-stats.h/.cc
```
로 player/merc shared pure math를 옮긴다.

즉 **먼저 merc branch를 정확히 구현 → player 동일성 테스트 → 안전한 부분만 공통화** 순서.

---

# 25. MercenaryRecord equipment 실제 저장 구조

## 설계 제약

이미 확정된 요구:
- 실제 `item_def`는 Record slot에만 존재
- shell `monster.inv[]` 미러링 금지
- one item = one owner
- field shell 삭제/재생성으로 item이 소멸하면 안 됨
- 전투 중 `weapon()`이 반환한 주소가 갑자기 invalidate되면 안 됨

## PATCH DECISION

장비 슬롯을 `vector<item_def>`로 저장하지 않는다.

이유:
- resize/reallocation 시 `item_def*` 주소 변경 가능
- `melee_attack`은 attack 중 weapon pointer를 들고 있음

추천 고정 저장:

```cpp
enum class merc_equip_slot : uint8_t
{
    WEAPON,
    BODY_ARMOUR,
    SHIELD,
    // 프로젝트에서 실제 허용하는 slot을 이후 확정
    NUM_SLOTS
};

struct MercenaryEquipment
{
    array<item_def, static_cast<size_t>(merc_equip_slot::NUM_SLOTS)> items;
    bitset<static_cast<size_t>(merc_equip_slot::NUM_SLOTS)> occupied;
};
```

또는 Crawl 스타일 container가 적합하면 `FixedVector<item_def, N>`.

### 반환
```cpp
item_def *merc_equipped_item(MercenaryRecord &rec, merc_equip_slot slot)
{
    if (!rec.equipment.occupied[...])
        return nullptr;
    return &rec.equipment.items[...];
}
```

### 장점
- Record lifetime 동안 slot 주소 안정
- shell recreation과 무관
- item copy mirror 불필요
- save 시 slot 순서가 deterministic

### `[SOURCE CHECK REQUIRED]`
프로젝트가 최종적으로 허용하는 merc equipment slot 목록을
기존 장비 UI 설계와 원본 DCSS wearable restrictions에 맞춰 확정해야 한다.

---

# 26. item transfer는 “C++ 객체 주소 영구 동일”이 목표가 아님

프로젝트의 “한 item = 한 실제 object = 한 owner” 원칙은
**동시에 두 owner에 복제 상태가 없어야 한다**는 의미다.

원본 Crawl item 이동은 container 간 값 이동/복사가 내부적으로 필요할 수 있다.

따라서 구현 invariant:

```text
transaction 시작 전:
    source owner 1개

commit 후:
    source invalid
    destination owner 1개

어떤 정상 observable 시점에도:
    source + destination 동시 authority 없음
```

### 전투 중 금지
melee execution 중 equipped weapon을 이동시키는 코드가 발생해도
현재 attack은 시작 snapshot/현재 weapon pointer lifetime 정책을 따라야 한다.

초기 구현에서는 combat action 안에서 직접 equipment transfer를 일으키는 기능을 넣지 않는 것이 안전.

---

# 27. player damage helper를 mercenary에 재사용하는 구체 패치 예시

원본 개념:

```cpp
damage = stat_modify_damage(damage, wpn_skill);
damage = apply_weapon_skill(damage, wpn_skill, random);
damage = apply_fighting_skill(damage, false, random);
```

mercenary:

```cpp
damage = stat_modify_damage(*attacker, damage, wpn_skill);
damage = apply_weapon_skill(*attacker, damage, wpn_skill, random);
damage = apply_fighting_skill(*attacker, damage, false, random);
```

### 이때 구현해야 할 actor input

```cpp
int playerlike_strength(const actor &);
int playerlike_dexterity(const actor &);
int playerlike_skill(const actor &, skill_type, int scale);
```

### player wrapper regression

테스트 seed N개에 대해:

```cpp
old_player_function(args) == new_actor_overload(you, args)
```

를 randomized regression test로 비교한다.

이 테스트가 통과하기 전에는 기존 player call-site를 actor overload로 대량 교체하지 않는다.

---

# 28. attack delay — 현재 확인 수준과 안전한 구현 원칙

## VERIFIED

0.34.1 `player.h`에는:

```cpp
random_var attack_delay(const item_def *projectile = nullptr,
                        bool rescale = true) const override;
```

가 존재한다.

`fight.cc`에는:

```cpp
int weapon_min_delay_skill(const item_def &weapon);
int weapon_min_delay(const item_def &weapon, bool check_speed = true);
int weapon_adjust_delay(const item_def &weapon, int base_delay, bool random = true);
```

가 존재한다.

`monster::attack_delay()`는 monster weapon을 읽어
player skill 기반 min-delay progression을 그대로 계산하는 구조가 아니다.

## 아직 확인 안 된 것

`player::attack_delay()`의 **0.34.1 정확한 정의 파일/전체 body**.

따라서 지금 단계에서:
```cpp
merc_delay = ...
```
공식을 임의 작성하지 않는다.

## SOURCE CHECK REQUIRED

Checkpoint03에서 반드시:
1. `player::attack_delay()` 정의 위치
2. melee weapon branch
3. skill 적용
4. min-delay clamp
5. random_var construction
6. haste/slow/rescale가 어느 계층에서 적용되는지

를 원본 body 기준으로 추출한다.

---

# 29. save compatibility — 원본 공식 개발문서 기준 구현 규칙

## VERIFIED — Crawl 공식 개발문서

`crawl-ref/docs/develop/save_compatibility.txt`는 다음을 명시한다.

- actor/player/monster에 드문/임시 속성을 추가할 때 `props`를 고려할 수 있음
- `tags.cc` 변경은 save compatibility를 신경 써야 함
- enum 기존 numeric value 변경 금지
- 새 save field를 추가할 때 `tag_minor_version` 끝에 새 값을 추가
- 구버전 load는 minor version conditional로 처리
- 기존 enum option은 끝에 append

### 프로젝트 적용

#### `MercenaryRecord roster`
이건 핵심 persistent game subsystem이라 `props` blob에 전부 밀어 넣지 않는다.
명시적 typed save block을 사용한다.

#### shell `merc_id`
두 후보가 있었지만 안정성 판단을 다음처럼 수정한다.

**추천 우선순위:**
1. `monster.props`에 stable integer `merc_id`를 저장해 prototype 단계에서 save wiring 최소화
2. core identity access는 반드시 typed helper로 감싼다
3. 성능/타입 안정성 때문에 explicit monster field가 필요하다고 확인된 뒤에만 minor-version serialization 추가

공식 Crawl 문서도 “일부 monster에만 적용되는 property는 props가 save compatibility에 유리”하다고 안내한다.

### 따라서 Checkpoint01의
> explicit `monster` field가 더 안전할 수 있다

는 **최종 확정이 아니며**, 구현 초기 P1에서는 props 방식이 더 낮은 침범도로 보인다.

---

# 30. PATCH DECISION — P1 shell link는 `props` 기반 prototype을 우선

키:

```cpp
#define MERCENARY_ID_KEY "muhyeop_merc_id"
```

helper:

```cpp
bool is_mercenary_monster(const monster &mon)
{
    return mon.props.exists(MERCENARY_ID_KEY);
}

merc_id_t mercenary_id(const monster &mon)
{
    ASSERT(is_mercenary_monster(mon));
    return mon.props[MERCENARY_ID_KEY].get_int();
}
```

실제 property API의 정확한 getter/setter 타입은 코드 작성 전 확인.

### 장점
- `monster` binary layout/save marshalling 직접 수정 최소화
- ordinary monster에 새 field 의미 없음
- shell recreation 시 `merc_id`만 붙이면 됨
- 기존 Crawl save compatibility 권장과 맞음

### 단점
- string-key property lookup
- compile-time type safety 낮음

### 결론
초기 안전성 기준으로 props 우선.
프로파일링으로 문제가 확인되기 전 explicit field로 옮기지 않는다.

---

# 31. Record 자체는 props에 넣지 않는다

`MercenaryRecord` 전체:
- skills
- equipment actual item_def
- mastery
- learned martial/spell
- state
- progression

를 monster props에 저장하면 안 된다.

이유:
- shell은 재생성 가능
- DEAD/CARRIED merc는 shell이 없음
- roster는 world/player-side persistent data
- equipment ownership이 shell 수명에 종속됨

따라서:

```text
monster.props[merc_id]
        ↓
MercenaryRoster
        ↓
MercenaryRecord
```

만 허용.

---

# 32. Mercenary roster container

새 타입 제안:

```cpp
class MercenaryRoster
{
public:
    MercenaryRecord *find(merc_id_t id);
    const MercenaryRecord *find(merc_id_t id) const;

    bool contains(merc_id_t id) const;

    MercenaryRecord &create(...);

private:
    vector<MercenaryRecord> records;
    merc_id_t next_id;
};
```

### 중요한 pointer 안정성

전투 중 `get_mercenary_record()`가 `MercenaryRecord*`를 반환한 뒤
`records.push_back()`으로 vector reallocation이 일어나면 pointer가 무효화될 수 있다.

따라서 두 방식 중 하나:

A. stable owning container:
```cpp
vector<unique_ptr<MercenaryRecord>>
```

B. `std::deque<MercenaryRecord>`

### 추천
```cpp
vector<unique_ptr<MercenaryRecord>>
```

이유:
- Record address stable
- roster ordering과 identity 분리
- DEAD Record 유지 쉬움
- serialization은 ID 순서로 별도 deterministic iteration 가능

`merc_id`가 authority이므로 vector index는 identity가 아니다.

---

# 33. shell → Record lookup

```cpp
MercenaryRecord *get_mercenary_record(monster &mon)
{
    if (!is_mercenary_monster(mon))
        return nullptr;

    return mercenary_roster().find(mercenary_id(mon));
}
```

필수 invariant:

```cpp
is_mercenary_monster(mon) => get_mercenary_record(mon) != nullptr
```

깨졌을 때:
- 개발 빌드 ASSERT + 상세 diagnostic
- 릴리스에서 ordinary monster fallback 금지
- 해당 merc action fail-closed

즉 broken merc link가 monster HD 전투로 조용히 내려가면 안 된다.

---

# 34. monster equipment/resistance 분기에서 broken Record 처리

금지:

```cpp
if (!rec)
    return monster::original_weapon();
```

이렇게 하면 save/link corruption이
“용병이 갑자기 일반 monster 공식으로 싸움”으로 숨는다.

권장:
```cpp
if (is_mercenary())
{
    const MercenaryRecord *rec = get_mercenary_record(*this);
    ASSERT(rec);
    if (!rec)
        return nullptr; // 또는 safe error path
    ...
}
```

전투 executor에서는 missing Record를 precheck failure로 잡는다.

---

# 35. monster shell HP와 Record의 save 관계

기존 확정 설계:
- 현장 ALIVE shell 존재 → current HP authority = `monster::hit_points`
- Record → persistent identity/stats/progression/equipment
- DOWNED/DEAD/비배치 lifecycle은 별도 상태

### 구현 영향

ALIVE active merc를 save할 때:
- shell HP는 원본 monster level save 경로에서 저장 가능
- Record에 같은 current HP를 매 턴 mirror하면 안 됨

비배치/town ALIVE:
- 별도 current HP가 필요 없도록 full heal semantics 사용

DOWNED:
- 기존 설계의 shell HP sentinel은 field runtime 상태
- logical state는 Record roster state

### `[SOURCE CHECK REQUIRED]`
level unload/transit에서 friendly monster shell이 어떤 chunk로 저장되는지
P1/P4 작업 전에 정확히 추적한다.

---

# 36. scheduler / energy — 설계상 유지할 경계

## VERIFIED

`monster`에는:
```cpp
int speed;
int speed_increment;

int action_energy(energy_use_type et) const;
bool has_action_energy() const;
void drain_action_energy();
```
가 있다.

따라서 merc shell은 기존 monster scheduler representation을 그대로 사용할 수 있다.

## PATCH DECISION

절대 추가하지 않음:
```cpp
MercenaryRecord.energy
MercenaryRecord.next_turn
MercenaryRecord.scheduler_time
```

전투 계산은 player-like여도 turn scheduling은 monster shell.

```text
MercenaryRecord:
    stat / skill / equipment / progression

monster shell:
    speed / speed_increment / scheduler / position / temporary enchants
```

---

# 37. melee energy 차감 위치 — 구현 시 지켜야 할 조건

현재 프로젝트 목표:

```text
precheck 실패 → 0 energy
실제 attack started → miss/block/0 damage도 정상 delay 소비
```

`melee_attack`/monster scheduler의 실제 0.34.1 차감 위치를
**정확한 source body로 한 번 더 검증하기 전에는 새 차감 코드를 넣지 않는다.**

### 금지
- `mercenary_melee_attack()` wrapper에서 speed_increment 직접 차감
- `monster::attack_delay()`에서도 차감
- `melee_attack`에서도 차감

즉 **delay 계산과 energy commit은 각각 정확히 한 원본 경로만 authority**.

`[SOURCE CHECK REQUIRED — exact 0.34.1 energy commit site]`

---

# 38. P2 / P2.5 / P3 패치 순서 수정

## P2A — Record query
- `monster::skill`
- `monster::weapon`
- `shield/body_armour/offhand`
- wearing/ego/artifact scan
- Record equipment fixed-address storage
- broken-link fail-closed

목표:
**전투 공식은 아직 바꾸지 않고 조회 authority부터 맞춘다.**

## P2B — merc defense/final stat
- AC
- EV
- SH
- resistances
- Will
- 장비 penalty
- UI용 동일 query

목표:
**실제 전투 방어와 UI가 같은 Record/equipment를 보게 한다.**

## P3A — player-like pure melee math
- stat modifier actor overload
- weapon skill actor overload
- Fighting actor overload
- player behavior equality test

## P3B — merc to-hit/damage branch
- `attack::calc_pre_roll_to_hit`
- `attack::calc_to_hit`
- `attack::calc_damage`

## P3C — attack delay
- 원본 `player::attack_delay()` body 검증 후 actor-safe extraction

## P3D — Unarmed / brand / aux
- UC merc helper
- actual item brand
- 허용된 aux만 actor-safe하게

---

# 39. Checkpoint 02 구현 리스크 목록

### R1 — player global helper 오염
가장 위험:
player 계산을 generic화하면서 `you` 전용 god/form/mutation logic을 merc에 잘못 적용.

대응:
- pure math부터 작은 overload
- player wrapper 유지
- player equality test

### R2 — equipment pointer lifetime
`melee_attack`이 weapon pointer를 보관하는 동안 Record equipment container가 realloc되면 crash 가능.

대응:
- fixed slot storage
- action 중 equipment mutation 제한
- Record stable address

### R3 — 장비 accessor만 패치하고 resistance 놓침
UI weapon은 맞는데 실제 AC/rF/Will이 monster 값인 bug.

대응:
- P2B를 별도 milestone로 둠
- `actor` defense virtual 전수검색

### R4 — broken merc link ordinary monster fallback
save/load 후 merc_id가 깨지면 HD 전투로 조용히 돌아가는 bug.

대응:
- fail-closed
- invariant/assert
- load validation

### R5 — energy 이중 차감
wrapper와 원본 monster melee 양쪽에서 delay를 지불.

대응:
- exact 0.34.1 commit 위치 확인 전 새 차감 코드 금지

---

# 40. Checkpoint 03에서 바로 조사할 항목

1. `player::attack_delay()` 0.34.1 정의 파일/body
2. 0.34.1 `melee_attack`의 monster energy commit 정확한 위치
3. `monster` level serialization 및 `props` round-trip 실제 경로
4. `mgen_data` / `create_monster()` exact declaration + spawn lifecycle
5. `get_free_monster()` / `set_new_monster_id()` / `mid` 배정 시점
6. friendly monster placement + follower level transition 처리
7. `monster::armour_class/evasion/shield_bonus/res_*` 실제 source body
8. item transfer helper 중 Record custom container가 재사용 가능한 부분
9. `TAG_YOU` 안 roster block 최종 삽입 위치
10. build system에 `mercenary.cc` 추가 방식

---

# 41. 실제 소스 기준 현재 확정도

| 하위시스템 | 상태 |
|---|---|
| fight melee 진입 | VERIFIED |
| attack-set player/monster 분기 | VERIFIED |
| player damage helper global-you 의존 | VERIFIED |
| actor overload 전략 | PATCH DECISION |
| merc skill Record 조회 | PATCH DECISION |
| unarmed player-global 의존 | VERIFIED |
| merc UC 별도 actor-safe helper | PATCH DECISION |
| monster equipment accessor 목록 | VERIFIED |
| monster inv 기반 장비 구조 | VERIFIED |
| merc accessor early-branch | PATCH DECISION |
| defense/resistance 추가 branch 필요 | VERIFIED + PATCH DECISION |
| Record equipment fixed-address storage | PATCH DECISION |
| monster shell scheduler 필드 | VERIFIED |
| scheduler를 Record에 복제하지 않음 | PATCH DECISION |
| shell merc_id props prototype | PATCH DECISION |
| Record 전체를 props에 저장 금지 | PATCH DECISION |
| roster stable owning container | PATCH DECISION |
| exact player attack delay body | SOURCE CHECK REQUIRED |
| exact 0.34.1 melee energy commit body | SOURCE CHECK REQUIRED |
| exact monster serialization | SOURCE CHECK REQUIRED |
| exact create_monster lifecycle | SOURCE CHECK REQUIRED |

---

# Checkpoint 02 결론

Checkpoint01에서 “어느 전투 함수에 mercenary branch가 필요한지”를 잡았다면,
Checkpoint02에서는 **그 branch에 실제 어떤 데이터를 어떻게 공급할지**를 고정했다.

현재 구현 핵심은 다음 구조다.

```text
monster shell
   │
   ├─ position / HP / enchant / speed_increment / scheduler
   │
   └─ props[muhyeop_merc_id]
             │
             ▼
      MercenaryRoster
             │
             ▼
      MercenaryRecord
       ├─ XL / stat / skill
       ├─ fixed-address actual equipment item_def
       ├─ martial/spell/progression
       └─ roster state
```

전투:
```text
fight_melee
  → melee_attack
  → merc branch
  → actor-safe player-like pure math
  → original hit/damage/brand/death pipeline
  → monster-shell scheduler/energy
```

원본 player와 ordinary monster의 기존 경로는 그대로 유지한다.



---

# Checkpoint 03 — Checkpoint 01~02 공식 0.34.1 소스 재검증 / 1차 교정

작성일: 2026-09-13

## 이 체크포인트의 우선순위

**Checkpoint 03부터는 Checkpoint 01~02의 초안보다 우선한다.**

Checkpoint 01~02와 이 문서가 충돌하면:
- Checkpoint 03의 `VERIFIED` / `CORRECTED` 내용을 따른다.
- 아직 확인하지 못한 내용은 `[SOURCE CHECK REQUIRED]` 상태로 유지한다.
- 과거 초안의 추정 함수명이나 패치 구조를 그대로 구현하지 않는다.

## 소스 기준

공식 DCSS GitHub release:
- release: `0.34.1`
- release commit 표시: `1eebc1a`
- release page: `https://github.com/crawl/crawl/releases`
- raw source 기준: `https://raw.githubusercontent.com/crawl/crawl/0.34.1/...`

보조 대조:
- Debian Sources `crawl 2:0.34.1-2`

### 환경 관련 정정
로컬에 만들어져 있던 `/mnt/data/crawl_0.34.1.orig.tar.xz`는 실제로 **0 byte**였고,
현재 실행환경에서는 GitHub release tarball을 직접 container로 다운로드하지 못했다.

따라서 “전체 tarball을 로컬에 풀어서 분석 완료”라는 식으로 간주하면 안 된다.

현재 검증 authority는:
1. 공식 GitHub `0.34.1` raw source
2. Debian Sources의 `0.34.1` source mirror

이다.

---

# 42. VERIFIED — `fight_melee()`의 실제 monster energy commit 위치

파일:
```text
crawl-ref/source/fight.cc
```

공식 0.34.1 raw 기준:
```cpp
bool fight_melee(actor *attacker, actor *defender, bool is_rampage,
                 bool *did_hit, bool simu)
```

non-player attacker 처리 마지막에 실제로:

```cpp
melee_attack attk(attacker, defender);
attk.simu = simu;
attk.launch_attack_set();

...

if (!attacker->alive())
    return true;

// Lose energy for the attack.
int energy = attacker->as_monster()->action_energy(EUT_ATTACK);
int delay = attacker->attack_delay().roll();
...
attacker->as_monster()->speed_increment
    -= div_rand_round(energy * delay, 10);
```

가 실행된다.

공식 raw source line 기준 대략:
```text
fight.cc:508~523
```

## CORRECTED

Checkpoint 02의:

> exact 0.34.1 melee energy commit body = SOURCE CHECK REQUIRED

는 이제 폐기.

상태:
```text
VERIFIED
```

## 구현 결론

용병 shell도 `fight_melee()`의 non-player scheduler 경로를 유지하면
기존 monster energy system에서 **공격 action당 정확히 한 번** energy를 지불할 수 있다.

따라서 초기 구현에서는 다음을 추가하면 안 된다.

```cpp
// 금지
mercenary_melee_attack() {
    merc.speed_increment -= ...;
}
```

또는:

```cpp
// 금지
run_mercenary_attack_set() {
    merc.lose_energy(EUT_ATTACK);
}
```

energy authority:
```text
fight_melee() non-player tail
```

---

# 43. VERIFIED — dead/reviving target special energy path

같은 `fight_melee()` 시작부에서:

```cpp
if (!defender->alive())
{
    if (defender->alive_or_reviving())
    {
        if (monster* mon = attacker->as_monster())
            mon->lose_energy(EUT_ATTACK);
        return true;
    }
    ...
}
```

가 존재한다.

즉 “precheck 무효면 무조건 0 energy”라는 설계 문장을
원본 엔진 전체에 절대 규칙처럼 덮어쓰면 안 된다.

## CORRECTED 의미

프로젝트 규칙은 다음처럼 좁혀야 한다.

```text
일반적인 invalid target / 취소:
    원본 취소 semantics 유지

alive_or_reviving 특수상태:
    원본 fight_melee()가 infinite-loop 방지 목적으로 energy 소비
```

용병도 이 engine safety exception을 그대로 따른다.

---

# 44. VERIFIED — player melee delay와 monster/merc scheduler는 서로 다른 위치

player branch:

```cpp
const bool success = attk.launch_attack_set();

if (attk.cancel_attack)
    you.turn_is_over = false;
else
    you.time_taken = you.melee_attack_delay().roll();
```

공식 raw 기준:
```text
fight.cc:479~486
```

monster branch:

```cpp
attk.launch_attack_set();
...
energy = action_energy(EUT_ATTACK);
delay = attacker->attack_delay().roll();
speed_increment -= ...
```

## 구현 의미

용병은:
- player `you.time_taken` 경로로 들어가면 안 됨.
- monster shell scheduler 경로를 유지.
- 단 `attacker->attack_delay()`가 mercenary이면 Record skill 기반 player-like delay를 반환하도록 패치.

구조:

```text
merc combat formula = player-like
merc turn economy    = monster shell scheduler
```

이 기존 설계는 실제 소스와 일치.

---

# 45. VERIFIED — `melee_attack::launch_attack_set()` 실제 분기

파일:
```text
crawl-ref/source/melee-attack.cc
```

실제 0.34.1:

```cpp
bool melee_attack::launch_attack_set(bool skip_player_post_attack)
{
    if (!attacker->is_player())
        return run_monster_attack_set();

    bool success = run_player_attack_set();

    if (!skip_player_post_attack)
    {
        player_attempted_attack(...);
    }

    return success;
}
```

공식 raw 기준:
```text
melee-attack.cc:1433~1449
```

Checkpoint 01의 이 분석은 맞았음.

---

# 46. CORRECTED — mercenary runner를 단순 `return attack();`로 끝내면 부족

Checkpoint 01에서는 개념 초안으로:

```cpp
bool melee_attack::run_mercenary_attack_set()
{
    return attack();
}
```

를 제안했다.

실제 `run_monster_attack_set()` 전체를 확인한 결과,
이건 **최종 구현으로는 너무 단순하다.**

원본 monster runner에는 다음 lifecycle이 있다.

- `mon->wield_melee_weapon()`
- defender death / banishment 검사
- hostility 변화 검사
- hydra multiattack/retarget
- 각 swing마다 새 `melee_attack`
- `copy_params_to(...)`
- `attack()`
- aggregate:
  - `success`
  - `did_hit`
  - `is_sunder`
- `fire_final_effects()`
- instant-cleave energy refund
- sunder charge

공식 raw 기준:
```text
melee-attack.cc:1450~1550
```

## mercenary에 필요한 것 / 필요없는 것

### 필요
- target live/revalidation
- hostility revalidation
- child `melee_attack` 또는 equivalent per-swing lifecycle
- `copy_params_to`
- aggregate `did_hit`
- `fire_final_effects()`가 current attack lifecycle에서 필요하다면 정확히 1회
- attacker alive check

### 기본적으로 불필요
- hydra multiattack
- `MAX_NUM_ATTACKS`
- monster attack spec iteration
- `mon->wield_melee_weapon()`  
  (Record actual equipped item이 authority라 shell inventory auto-wield 금지)
- hydra retarget
- monster-only instant-cleave/sunder special handling  
  (해당 effect를 용병에게 실제 허용할 때만 별도 actor-safe 검토)

## 새 runner 권장 형태

정확한 body는 추가 검증 후 확정하지만 방향은:

```cpp
bool melee_attack::run_mercenary_attack_set()
{
    ASSERT(is_mercenary_actor(attacker));

    // 1. defender live/banish/hostility recheck
    // 2. Record actual weapon을 사용한 1회 attack lifecycle
    // 3. did_hit / cancel / result aggregation
    // 4. 필요한 final-effects exactly once
    // 5. energy 차감은 여기서 하지 않음
}
```

---

# 47. VERIFIED — `melee_attack::set_weapon()`은 mercenary를 monster로 본다

실제 0.34.1:

```cpp
void melee_attack::set_weapon(item_def *wpn)
{
    weapon = mutable_wpn = wpn;
    if (const monster* mons = attacker->as_monster())
    {
        damage_brand = mons->damage_brand(attack_number);
        damage_type = mons->damage_type(attack_number);
    }
    else
    {
        damage_brand = you.damage_brand(wpn);
        damage_type = you.damage_type(wpn);
    }

    init_attack(attack_number);
    if (weapon && !using_weapon())
        wpn_skill = SK_FIGHTING;
}
```

공식 raw 기준:
```text
melee-attack.cc:1380~1397
```

Checkpoint 01~02의 문제 지적은 맞았음.

## 구현 결론

mercenary branch는 `as_monster()`보다 먼저 판정해야 한다.

```cpp
if (is_mercenary_actor(attacker))
{
    ...
}
else if (const monster* mons = attacker->as_monster())
{
    ...
}
else
{
    // original player
}
```

---

# 48. VERIFIED — `attack::calc_pre_roll_to_hit()` player/monster hard split

파일:
```text
crawl-ref/source/attack.cc
```

실제 0.34.1 player branch는 직접:

```cpp
you.dex()
you.skill(SK_FIGHTING, 100)
you.skill(wpn_skill, 100)
you.form_uses_xl()
you.experience_level
weapon->plus
property(*weapon, PWPN_HIT)
you.slaying(...)
you.duration[DUR_VERTIGO]
you.get_mutation_level(MUT_EYEBALLS)
```

를 읽는다.

monster branch는:

```cpp
calc_mon_to_hit_base()
weapon->plus
property(*weapon, PWPN_HIT)
attacker->slaying()
```

를 쓴다.

공식 raw 기준:
```text
attack.cc:151~221
```

Checkpoint 01의 핵심 분석은 맞았음.

---

# 49. VERIFIED — `attack::calc_to_hit()` roll semantics도 player/monster 분리

실제:

```cpp
const actor &src = stat_source();

if (src.is_player())
    mhit = maybe_random2(mhit, random);

mhit += post_roll_to_hit_modifiers(mhit, random);

if (!src.is_player())
    mhit = maybe_random2(mhit + 1, random);
```

공식 raw 기준:
```text
attack.cc:269~288
```

## 구현 결론

mercenary에게 player-like hit semantics를 쓰려면
`calc_pre_roll_to_hit()`만 바꾸면 안 된다.

melee 전용 predicate 예:

```cpp
bool uses_playerlike_melee_math(const actor &a);
```

를 `calc_to_hit()`의 roll 위치에도 적용해야 한다.

다만 이 predicate로 `actor::is_player()` 자체를 대체하면 안 된다.

---

# 50. NEW VERIFIED RISK — `post_roll_to_hit_modifiers()`도 actor identity에 따라 의미가 다름

실제 0.34.1:

```cpp
if (!defender->visible_to(attacker))
{
    if (attacker->is_player())
        modifiers -= 6;
    else
        modifiers -= mhit * 35 / 100;
}
```

또 defender가 player일 때:
```cpp
you.get_mutation_level(MUT_TRANSLUCENT_SKIN)
```

를 본다.

## 구현 영향

용병 player-like 명중을 완전히 맞추려면
다음 선택을 명시해야 한다.

### 추천
mercenary attacker에 대해:
- invisible target penalty를 player-like `-6`으로 사용할지
- monster-like 35% penalty를 사용할지

기존 설계 목표가 **player melee 핵심식 공유**이므로
초기 추천은 player-like penalty.

단, player-only mutation/`you` defender logic은 그대로 분리 유지.

따라서 `post_roll_to_hit_modifiers()`도
“순수 player-like attack math”와 “실제 player-only state”를 분리해야 한다.

---

# 51. VERIFIED — `attack::calc_damage()` hard split

실제 0.34.1:

```cpp
if (stat_source().is_monster())
{
    ...
    damage_max += attk_damage;
    damage += 1 + random2(attk_damage);
    ...
}
else
{
    potential_damage = using_weapon()
        ? adjusted_weapon_damage()
        : calc_base_unarmed_damage();

    potential_damage = stat_modify_damage(...);
    damage = ...
    damage = apply_weapon_skill(...);
    damage = apply_fighting_skill(...);
    damage = player_apply_misc_modifiers(...);
    damage = player_apply_slaying_bonuses(...);
    damage = player_stab(...);
    damage = player_apply_final_multipliers(...);
    damage = apply_defender_ac(...);
    damage = player_apply_postac_multipliers(...);
}
```

공식 raw 기준:
```text
attack.cc:857~914
```

Checkpoint 01~02의 “monster branch를 그대로 쓰면 안 된다”는 결론은 맞음.

---

# 52. VERIFIED — non-player Unarmed base damage = 0

실제:

```cpp
int attack::calc_base_unarmed_damage() const
{
    if (weapon)
        return 0;

    if (!attacker->is_player())
        return 0;

    const int dam = unarmed_base_damage(true)
                  + unarmed_base_damage_bonus(true);

    return dam > 0 ? dam : 0;
}
```

공식 raw 기준:
```text
attack.cc:840~851
```

Checkpoint 01~02의 UC 문제 분석은 정확.

---

# 53. NEW VERIFIED RISK — hit resolution에서도 `is_player()`가 직접 쓰임

`melee_attack::attack()` 실제 hit resolution:

```cpp
const int ev = defender->evasion(false, attacker);
ev_margin = test_hit(to_hit, ev, !attacker->is_player());
```

공식 raw 기준:
```text
melee-attack.cc:1751~1755
```

`attack::test_hit()`에서 `randomise_ev`가 true면:

```cpp
ev = random2avg(2*ev, 2);
```

를 실행한다.

즉 mercenary는 아무 조치가 없으면:
- to-hit 수치는 player-like로 계산해도
- defender EV randomization은 monster 방식

이 된다.

## CORRECTED 구현 범위

Checkpoint 01~02에서 잡았던 패치 대상에 추가:

```text
melee_attack::attack() hit-resolution call
```

권장:

```cpp
const bool playerlike = uses_playerlike_melee_math(*attacker);
ev_margin = test_hit(to_hit, ev, !playerlike);
```

### 주의
player blind 상태 등 `test_hit()` 내부의 `attacker->is_player()` 분기는
실제 player 전용 effect이므로 모두 무조건 playerlike로 바꾸면 안 된다.

즉:
- EV roll semantic
- player-only status semantic

을 분리해야 한다.

---

# 54. VERIFIED — `attack::test_hit()` 내부 player-only effect

실제:
```cpp
if (defender->is_player() && you.duration[DUR_AUTODODGE])
    return -1000;

if (randomise_ev)
    ev = random2avg(2*ev, 2);

...

if (attacker->is_player() && you.duration[DUR_BLIND])
{
    ...
}
```

## 구현 결론

mercenary용 player-like hit math 적용 시:

### 공유
- `randomise_ev = false` 쪽 player-like semantics

### 공유 금지
- `you.duration[DUR_BLIND]`
- player AutoDodge 등 player-specific state

용병에 Blind가 필요하면 shell enchant/공통 actor state로 별도 연결해야 함.

---

# 55. NEW VERIFIED RISK — `could_harm()` 취소 semantics

`melee_attack::attack()`:

```cpp
if (!could_harm(attacker, defender,
                attacker->is_player(),
                attacker->is_player()))
{
    cancel_attack = attacker->is_player()
        && !(you.confused() || !you.can_see(*defender));
    return false;
}
```

공식 raw 기준:
```text
melee-attack.cc:1715~1721
```

즉 mercenary는 현재:
- player처럼 사전에 “이 공격은 무효”를 취소하는 path가 아니라
- monster semantics로 처리될 가능성이 있음.

## 아직 확정하지 않음

용병에 이 player cancel semantics를 얼마나 공유할지는
`could_harm()` 실제 signature/body와 monster AI 호출부까지 추가 확인 후 결정.

상태:
```text
SOURCE CHECK REQUIRED
```

---

# 56. NEW VERIFIED RISK — player post-attack / conduct / stab는 mercenary에 그대로 쓰면 안 됨

실제 `melee_attack::attack()`에는:

```cpp
if (attacker->is_player() && ...)
{
    set_attack_conducts(...);
    player_stab_check();
    ...
}
```

가 있고,

`launch_attack_set()` player path 끝에는:

```cpp
player_attempted_attack(...)
```

가 있다.

mercenary에게:
- god conduct
- Dith shadow
- player duration maintenance
- player form trigger
- player stab global state

를 그대로 실행하면 안 된다.

## 결론

`uses_playerlike_melee_math()`는:
- 숫자 계산/roll semantic용

이지:

```text
"is player for all effects"
```

predicate가 아니다.

### 반드시 분리
```text
real player identity
player-like merc melee math
ordinary monster identity
```

---

# 57. CORRECTED — mercenary single-swing runner의 최소 책임

현재 가장 안전한 형태는:

```text
run_mercenary_attack_set()
    1. Record link 유효성 확인
    2. defender live/banish/hostility 확인
    3. shell inventory auto-wield 금지
    4. 실제 Record weapon을 set_weapon
    5. 1회 melee_attack.attack()
    6. did_hit / cancel / total result 복사
    7. required final-effects 처리
    8. energy 차감하지 않음
```

실제 정확한 code body는:
- `fire_final_effects()`
- nested `melee_attack` 필요성
- copy/aggregate field

를 추가 검증한 뒤 확정.

---

# 58. 재검증 상태표 — 1차

| Checkpoint01~02 항목 | 재검증 결과 |
|---|---|
| 공식 base = DCSS 0.34.1 | VERIFIED |
| `fight_melee` actor* wrapper | VERIFIED |
| player branch `you.melee_attack_delay()` | VERIFIED |
| monster branch energy commit | VERIFIED, 위치 확정 |
| energy commit이 runner 내부일 가능성 | CORRECTED — `fight_melee()` tail |
| `launch_attack_set()` player/monster split | VERIFIED |
| `run_monster_attack_set()` multiattack | VERIFIED |
| merc runner = `return attack()` 충분 | CORRECTED — 불충분 |
| `set_weapon()` merc가 monster branch로 감 | VERIFIED |
| `calc_pre_roll_to_hit()` hard split | VERIFIED |
| `calc_to_hit()` roll hard split | VERIFIED |
| `post_roll_to_hit_modifiers()` 추가 분리 필요 | NEW VERIFIED |
| `calc_damage()` hard split | VERIFIED |
| non-player UC base 0 | VERIFIED |
| hit EV roll도 monster/player semantics 분리 | NEW VERIFIED |
| player conduct/stab/postattack 자동 공유 금지 | VERIFIED |
| exact player `attack_delay()` body | 아직 SOURCE CHECK REQUIRED |
| shell `props` save roundtrip | 아직 SOURCE CHECK REQUIRED |
| monster AC/EV/SH/res body | 아직 SOURCE CHECK REQUIRED |
| exact spawn/mgen lifecycle | 아직 SOURCE CHECK REQUIRED |

---

# 59. 패치 지도 P3 범위 수정

Checkpoint 02의 P3를 다음처럼 세분화한다.

## P3A — numerical player-like helpers
- STR/DEX
- Fighting
- weapon skill
- slaying
- stat scaling
- damage scaling

## P3B — to-hit pre-roll / roll
- `calc_pre_roll_to_hit`
- `calc_to_hit`
- invisible target penalty

## P3C — actual hit resolution
- `melee_attack::attack()`의
  ```cpp
  test_hit(..., !attacker->is_player())
  ```
  를 merc player-like semantics와 분리

## P3D — damage
- player-like base weapon/UC
- stat/skill/Fighting
- allowed misc/slaying
- AC exactly once

## P3E — attack set lifecycle
- `run_mercenary_attack_set`
- final effects
- target invalidation
- no monster multiattack
- no shell auto-wield

## P3F — delay
- `player::attack_delay()` 실제 body 검증 후 추출
- energy commit은 기존 `fight_melee()` tail 사용

---

# 60. 다음 즉시 재검증 묶음

다음 저장 전에 실제 0.34.1에서 확인할 항목:

1. `player::attack_delay()` / `melee_attack_delay()` 실제 정의
2. `monster::attack_delay()` / `melee_attack_delay()` 실제 body
3. `monster::skill()` 실제 body
4. `monster::weapon()/shield()/body_armour()` 실제 body
5. `monster::armour_class()/evasion()/shield_bonus()` 실제 body
6. resist / willpower 실제 body
7. monster `props` serialization roundtrip
8. `mgen_data`, `create_monster`, MID 부여 lifecycle

---

# Checkpoint 03 1차 결론

Checkpoint 01~02의 **큰 방향은 상당 부분 맞았지만**, 실제 0.34.1 코드에서는
단순히 to-hit/damage 함수만 merc branch로 바꾸면 충분하지 않다.

추가로 실제 확인된 핵심은:

```text
fight_melee
    → launch_attack_set
    → run_monster_attack_set / merc runner
    → melee_attack::attack
        → calc_to_hit
        → test_hit EV roll semantics
        → shield / spines / hit / brand / death
    → fight_melee tail에서 monster energy commit
```

따라서 용병 구현은:
- player-like **수학**
- monster-shell **scheduler**
- 실제 player-only **god/form/global effects**

세 층을 분리해야 한다.

이 분리가 앞으로 패치 지도의 핵심 기준이다.


---

# Checkpoint 04 — monster accessor / skill / delay / defense / resistance 실제 body 재검증

> Checkpoint 04는 Checkpoint 03까지 전부 포함한 누적본이다.
> 이 문서 내용이 Checkpoint 01~03의 오래된 추정과 충돌하면 Checkpoint 04를 우선한다.

---

# 61. VERIFIED — `monster::skill()`은 실제로 HD 기반

파일:
```text
crawl-ref/source/monster.cc
```

실제 0.34.1:

```cpp
int monster::skill(skill_type sk, int scale, bool /*real*/, bool /*temp*/) const
{
    if (mons_intel(*this) < I_HUMAN && !mons_is_avatar(type))
        return 0;

    const int hd = scale * get_hit_dice();

    switch (sk)
    {
    case SK_INVOCATIONS:
    case SK_EVOCATIONS:
        return hd;

    case SK_NECROMANCY:
        return (has_spell_of_type(spschool::necromancy)) ? hd * 2 : hd/2;

    ...

    case SK_SHORT_BLADES:
    case SK_LONG_BLADES:
    case SK_AXES:
    case SK_MACES_FLAILS:
    case SK_POLEARMS:
    case SK_STAVES:
        ...
        return ret;

    default:
        return 0;
    }
}
```

공식 raw 기준:
```text
monster.cc:3821~3866
```

## 구현 결론

이 함수 최상단 merc branch는 확정적으로 필요:

```cpp
if (is_mercenary())
    return mercenary_skill(require_mercenary_record(*this), sk, scale);
```

그 뒤 원본 monster code는 그대로 둔다.

---

# 62. VERIFIED — `monster::attack_delay()`는 weapon skill을 전혀 사용하지 않음

실제 0.34.1:

```cpp
random_var monster::attack_delay(const item_def *projectile) const
{
    const item_def* weap = weapon();
    if (!weap || (projectile && is_throwable(this, *projectile)))
        return random_var(10);

    random_var delay(weapon_adjust_delay(*weap, 10));
    return delay;
}

random_var monster::melee_attack_delay() const
{
    return attack_delay();
}
```

공식 raw 기준:
```text
monster.cc:415~428
```

## 의미

Checkpoint 01~02의 판단:
> mercenary는 `monster::attack_delay()` 그대로 쓰면 player weapon skill delay가 안 나옴

은 정확.

## 구현 결론

```cpp
random_var monster::attack_delay(const item_def *projectile) const
{
    if (is_mercenary())
        return mercenary_attack_delay(*this, projectile);

    // 원본 그대로
}
```

`melee_attack_delay()`는 `attack_delay()`를 그대로 호출하므로,
초기 구현에서는 별도 merc branch가 **중복일 수 있다.**

즉 Checkpoint01의:
> `attack_delay()` / `melee_attack_delay()` 둘 다 merc branch 필요

는 다음처럼 교정.

### CORRECTED
우선:
```text
monster::attack_delay()만 branch
```

`melee_attack_delay()`는 현재 원본 delegate를 유지.

단 향후 player와 동일한 별도 melee semantics가 필요하다고 확인되면 재검토.

---

# 63. VERIFIED — `monster::weapon()`은 attack spec + `inv[]` 기반

실제:

```cpp
item_def *monster::weapon(int which_attack) const
{
    const mon_attack_def attk = mons_attack_spec(*this, which_attack);

    if (attk.type != AT_HIT && attk.type != AT_WEAP_ONLY)
        return nullptr;

    ...

    int weap = inv[MSLOT_WEAPON];

    if (which_attack && mons_wields_two_weapons(*this))
    {
        const int offhand = _mons_offhand_weapon_index(this);
        ...
        weap = offhand;
    }

    return weap == NON_ITEM ? nullptr : &env.item[weap];
}
```

공식 raw 기준:
```text
monster.cc:466~494
```

## 구현 결론

mercenary는 함수 제일 앞에서:
```cpp
if (is_mercenary())
    return mercenary_weapon(*this, which_attack);
```

로 분리.

### 중요
`mons_attack_spec()`을 먼저 호출하면 이미 merc가 monster attack definition에 의존하므로
반드시 그보다 앞에서 branch.

---

# 64. VERIFIED — `monster::damage_brand()`은 `weapon()`을 사용

실제:

```cpp
brand_type monster::damage_brand(int which_attack) const
{
    const item_def *mweap = weapon(which_attack);

    if (!mweap)
        return ghost_brand();

    return !is_range_weapon(*mweap)
        ? static_cast<brand_type>(get_weapon_brand(*mweap))
        : SPWPN_NORMAL;
}
```

## 의미

`monster::weapon()`을 merc-aware하게 하면
일부 brand 계산은 자동으로 실제 Record weapon을 볼 수 있다.

하지만 `melee_attack::set_weapon()`는 monster attacker에 대해
`monster::damage_type()`도 호출한다.

`damage_type()`은 weapon이 없으면:
```cpp
mons_attack_spec(...)
```
으로 fallback한다.

## 구현 결론

mercenary에서 weapon이 없는 경우:
- 일반 monster attack type으로 fallback시키면 안 됨.
- UC damage type은 mercenary player-like UC 정책으로 별도 처리.

---

# 65. VERIFIED — `monster::armour_class()`는 accessor만으로 해결되지 않음

실제 0.34.1:

```cpp
int monster::armour_class() const
{
    int ac = base_armour_class();

    ac += 5 * wearing_ego(OBJ_WEAPONS, SPWPN_PROTECTION);

    const item_def *armour = mslot_item(MSLOT_ARMOUR);
    if (armour)
        ac += armour_bonus(*armour);

    const item_def *ring = mslot_item(MSLOT_JEWELLERY);
    ...

    ac += scan_artefacts(ARTP_AC);

    if (has_ench(ENCH_IDEALISED))
        ac += 4 + get_hit_dice() / 3;

    ...
}
```

공식 raw:
```text
monster.cc:3000~3040
```

### 문제
직접 사용:
- `base_armour_class()` = monster species/data AC
- `mslot_item()`
- `get_hit_dice()`
- monster enchant semantics

## CORRECTED

Checkpoint02의:
> equipment accessor를 merc-aware하게 만들면 방어 계산 상당부분이 따라올 수 있다

는 **불충분**.

실제 구현은:

```cpp
int monster::armour_class() const
{
    if (is_mercenary())
        return mercenary_armour_class(*this);

    // original monster
}
```

처럼 함수 최상단에서 완전히 분리하는 것이 더 안전.

---

# 66. VERIFIED — `monster::evasion()`도 `mslot_item()` + monster base EV 직접 사용

실제:

```cpp
int monster::evasion(bool ignore_temporary, const actor*) const
{
    int ev = base_evasion();

    for (int slot = MSLOT_ARMOUR; slot <= MSLOT_SHIELD; slot++)
    {
        const item_def* armour = mslot_item(...);
        if (armour)
            ev += property(*armour, PARM_EVASION) / 60;
    }

    ev += 8 * wearing_ego(...);

    const item_def *ring = mslot_item(MSLOT_JEWELLERY);
    ...

    ev += scan_artefacts(ARTP_EVASION);

    if (ignore_temporary)
        return max(ev, 0);

    if (paralysed() || petrified() || ...)
        return 0;

    if (caught())
        ev /= 5;
    else if (confused())
        ev /= 2;

    if (has_ench(ENCH_AGILE))
        ev += AGILITY_BONUS;

    if (is_constricted())
        ev -= 10;

    return max(ev, 0);
}
```

공식 raw:
```text
monster.cc:3102~3145
```

## 구현 결론

mercenary EV도 함수 최상단 branch:

```cpp
if (is_mercenary())
    return mercenary_evasion(*this, ignore_temporary);
```

### helper 내부
- base DEX/stat
- equipment penalties
- Record/equipment authority
- shell live temporary statuses
- original common temporary state helper 가능한 부분 재사용

---

# 67. VERIFIED — monster SH는 HD를 shield skill proxy로 사용

실제:

```cpp
int monster::shield_class() const
{
    int sh = 0;
    const item_def *shld = shield();

    if (shld)
    {
        const int base = property(*shld, PARM_AC) + shld->plus;
        sh += base * 2;

        // monster HD as proxy for shield skill
        sh += get_hit_dice() * 4 / 3;
    }

    ...
    return sh;
}
```

공식 raw:
```text
monster.cc:2856~2879
```

그리고:

```cpp
int monster::shield_bypass_ability(int) const
{
    return mon_shield_bypass(get_hit_dice());
}
```

## 구현 결론

mercenary에서 그대로 쓰면:
- SH가 XL/HD 기반
- 방패 숙련 무시

가 되어 설계와 충돌.

필수 merc branch:

```text
monster::shield_class()
monster::shield_bonus()       // class branch 결과 이용 가능하지만 확인
monster::shield_bypass_ability()
```

특히 공격자 merc의 shield bypass도 player-like 계산이 필요.

---

# 68. VERIFIED — rF / rC / rElec / rPois / rN은 `monster.inv[]`를 직접 읽음

## `monster::res_fire()`

직접:
```cpp
inv[MSLOT_ARMOUR]
inv[MSLOT_SHIELD]
inv[MSLOT_JEWELLERY]
env.item[...]
get_mons_resist(...)
primary_weapon()
```

## `monster::res_cold()`
동일한 구조.

## `monster::res_elec()`
동일한 구조.

## `monster::res_poison()`
동일한 구조.

## `monster::res_negative_energy()`
동일한 구조.

따라서:
```text
weapon()/shield()/body_armour()만 merc-aware
```
로는 해결 안 됨.

## 구현 결론

각 public actor resistance function 최상단에서:

```cpp
if (is_mercenary())
    return mercenary_res_fire(*this);
```

같이 분리하는 것이 안전.

---

# 69. VERIFIED — Willpower도 HD + `inv[]` 직접 사용

실제:

```cpp
int monster::willpower() const
{
    if (mons_invuln_will(*this))
        return WILL_INVULN;

    ...

    const int type_wl = get_monster_data(type)->willpower;

    int u = type_wl < 0
        ? get_hit_dice() * -type_wl * 4 / 3
        : mons_class_willpower(type, base_monster);

    ...

    const int HD = get_hit_dice();
    ...

    u += WL_PIP * scan_artefacts(ARTP_WILLPOWER);

    const int armour    = inv[MSLOT_ARMOUR];
    const int shld      = inv[MSLOT_SHIELD];
    const int jewellery = inv[MSLOT_JEWELLERY];

    ...
}
```

공식 raw:
```text
monster.cc:3695~3741+
```

## 구현 결론

mercenary Will:
- monster type base Will 사용 금지
- HD proxy 사용 금지
- Record/player-like stat/XL/equipment policy 사용

따라서:

```cpp
if (is_mercenary())
    return mercenary_willpower(*this);
```

최상단 branch 확정.

---

# 70. CORRECTED — P2B는 “accessor patch”가 아니라 “actor defense override branch”

이전 문서에서 P2B는 방어 helper 추가 정도로 서술했지만,
실제 소스 기준 구현 단위는 더 명확하다.

## P2B 실제 patch list

최소:

```text
monster::armour_class()
monster::evasion()
monster::shield_class()
monster::shield_bonus()
monster::shield_bypass_ability()

monster::res_fire()
monster::res_steam()         // rF를 호출하므로 검증
monster::res_cold()
monster::res_elec()
monster::res_poison()
monster::res_negative_energy()
monster::res_corr()
monster::willpower()
```

추가 actor resistance interface 전수검색 필요:
```text
SOURCE CHECK REQUIRED
```

---

# 71. NEW implementation architecture — shell temporary state와 Record final stat 합성

실제 monster methods를 완전히 버리면
shell enchant:
- corrosion
- agile
- paralysis
- confusion
- caught
- constriction
- resistance enchant

등을 놓칠 수 있다.

따라서 merc helper는:

```text
Record persistent base/stat/equipment
          +
shell live temporary enchants/state
          ↓
mercenary final actor stat
```

구조로 만든다.

예:

```cpp
int mercenary_evasion(const monster &shell, bool ignore_temporary)
{
    const MercenaryRecord &rec = require_record(shell);

    int ev = mercenary_base_evasion(rec);
    ev += mercenary_equipment_ev(rec);

    if (ignore_temporary)
        return max(ev, 0);

    // shell live status 적용
    ...
}
```

### 금지
- shell monster base EV를 시작값으로 사용
- HD를 skill proxy로 사용
- Record에 temporary enchant 결과를 permanent copy 저장

---

# 72. NEW implementation architecture — resistance helper의 데이터 출처

mercenary resist:

```text
species/constitution/trait intrinsic
+ actual Record equipment
+ artefact props
+ shell temporary enchants
= final resistance
```

원본 monster:
```text
monster type innate resistance
+ monster inventory
+ enchant
```

와 출처가 다르다.

따라서 monster function 내부 몇 줄만 조건문으로 고치는 것보다
mercenary helper로 분리하는 쪽이 유지보수 안전성이 높다.

---

# 73. `mslot_item()` 자체를 merc-aware하게 만드는 안은 기각에 가까움

실제 소스를 보면:
- AC
- EV
- resist
- Will
- weapon
- jewellery

가 `inv[]`, `mslot_item()`, monster type data를 혼용한다.

`mslot_item()` 하나만 가로채면
일부 계산은 Record를 보고 일부는 monster `inv[]`를 봐서
오히려 이중 authority가 더 숨겨진다.

## PATCH DECISION

초기 구현:
- `mslot_item()`은 ordinary monster용으로 유지
- merc public combat accessor/method를 최상단 branch
- merc helper가 Record equipment를 직접 읽음

---

# 74. player-like defense 전체 복사도 아직 금지

현재 단계에서:
```cpp
mercenary_armour_class()
mercenary_evasion()
...
```
를 만들되,
player AC/EV 함수 body 전체를 복사해 두 번째 player 시스템을 만들지는 않는다.

다음 단계:
1. player defense 함수 실제 body 추적
2. pure formula 부분
3. species/equipment restriction 부분
4. `you` global part

을 분해한 뒤 가능한 부분만 공통 helper로 추출.

---

# 75. Checkpoint 04 재검증 상태

| 항목 | 상태 |
|---|---|
| monster skill HD 기반 | VERIFIED |
| merc `monster::skill` early branch | PATCH DECISION |
| monster attack delay weapon skill 무시 | VERIFIED |
| merc `monster::attack_delay` branch | PATCH DECISION |
| `melee_attack_delay()` 별도 branch 필수 | CORRECTED — 현재는 delegate라 우선 불필요 |
| monster weapon attack-spec + inv 기반 | VERIFIED |
| merc weapon branch는 `mons_attack_spec`보다 먼저 | PATCH DECISION |
| monster AC direct inv/HD/monster base | VERIFIED |
| monster EV direct inv/monster base | VERIFIED |
| monster SH HD proxy | VERIFIED |
| monster resistance direct inv/monster innate | VERIFIED |
| monster Will direct inv/HD/type | VERIFIED |
| `mslot_item`만 patch하면 충분 | REJECTED |
| merc public defense/res method early branch | PATCH DECISION |
| exact player defense formula reuse 범위 | SOURCE CHECK REQUIRED |
| all actor resistance method complete list | SOURCE CHECK REQUIRED |

---

# 76. 다음 저장 전 조사

1. player `attack_delay()` 실제 정의 위치/body
2. player AC/EV/SH 함수 실제 body
3. player resistance/Will helper 실제 body
4. monster props save/load serialization
5. `mgen_data`/`create_monster`
6. MID 생성/actor identity
7. follower/transit shell lifecycle



---

# Checkpoint 05 — player AC / EV / SH / Will / resistance 실제 body 재검증

> Checkpoint 05는 Checkpoint 04까지 포함한 누적본이다.
> 앞 체크포인트와 충돌 시 Checkpoint 05의 VERIFIED/CORRECTED 내용을 우선한다.

---

# 77. VERIFIED — player AC는 equipment + player 전용 효과가 섞여 있음

파일:
```text
crawl-ref/source/player.cc
```

0.34.1 실제 `player::base_ac(int scale)`는 대략 다음 요소를 읽는다.

- `equipment.items`
- melded/overflow 검사
- 실제 armour item
- `base_ac_from(...)`
- item plus
- protection ego
- protection ring
- artefact AC
- form AC
- racial/species AC
- mutation AC

`player::armour_class()`는:

```cpp
return div_rand_round(armour_class_scaled(100), 100);
```

형태로 최종 scale을 내린다.

`player::armour_class_scaled()`에는 추가로:
- icy armour
- mutation
- fiery/Qazlal
- protection duration
- gizmo
- passwall armour
- phalanx barrier
- corrosion
- sanguine armour

등 **실제 player 전용 global 상태**가 섞여 있다.

## 구현 결론

mercenary에서:

```cpp
you.armour_class()
```

또는 player body 전체 복사 사용 금지.

대신:

```text
Record species/base stat
+ actual Record equipment
+ actor-safe pure armour math
+ shell live temporary status
```

로 merc AC를 구성한다.

---

# 78. PATCH DECISION — AC 공통화는 pure input helper부터

초기 helper 후보:

```cpp
int calc_armour_item_ac(const item_def &armour, ...);
int calc_actor_armour_skill_bonus(...);
int calc_actor_armour_encumbrance(...);
```

단, 기존 player helper가 이미 pure하게 존재하면 새 helper를 중복 작성하지 않고
원본 helper를 그대로 재사용한다.

### 절대 공통화하지 않는 것
- player god state
- player-only durations
- player global form state
- `you` mutation 직접 조회

mercenary에 실제로 존재하는 trait/status만 별도 actor-safe layer에서 추가.

---

# 79. VERIFIED — player EV도 player 전용 helper에 강하게 묶여 있음

0.34.1 `player::evasion()`은:

```cpp
int base_evasion =
    div_rand_round(_player_evasion(100, ignore_temporary), 100);
```

형태로 `_player_evasion(...)` 결과를 사용한다.

`_player_evasion(...)`은:
- DEX
- Dodging
- body armour penalty
- shield/size/form 관련 상태
- player temporary states

등을 조합하는 player-specific helper다.

## 구현 결론

mercenary EV는:
```text
player::evasion() 직접 호출 X
_player_evasion()에 merc data 억지 주입 X
```

초기 구현은 pure constituent formula를 actor-safe하게 분리하고,
shell temporary state를 별도 합성한다.

---

# 80. VERIFIED — player body armour penalty의 핵심 수식은 actor-safe 추출 후보

실제 player penalty 계층에는:

```cpp
player::unadjusted_body_armour_penalty()
player::adjusted_body_armour_penalty(...)
```

가 있다.

`adjusted_body_armour_penalty()` 핵심은 다음 형태다.

```text
2 * base_ev_penalty^2
* (450 - ArmourSkill*10-scale 계열)
* scale
/ (5 * (STR + 3))
/ 450
```

정확한 정수 scale/rounding은 원본을 그대로 유지해야 한다.

## PATCH DECISION

새 pure helper:

```cpp
int adjusted_body_armour_penalty_from_inputs(
    int base_ev_penalty,
    int armour_skill_scaled,
    int strength,
    int scale);
```

기존 player 함수는 wrapper로 유지.

mercenary는:
- Record STR
- Record Armour skill
- actual body armour

을 input으로 넣는다.

---

# 81. VERIFIED — player shield penalty도 pure input 추출 후보

실제 `player::adjusted_shield_penalty()`는 대략:

```text
2 * base_shield_penalty^2
* (270 - ShieldsSkill*10-scale 계열)
* scale
/ (25 + 5*STR)
/ 270
```

구조다.

## PATCH DECISION

pure helper 후보:

```cpp
int adjusted_shield_penalty_from_inputs(
    int base_shield_penalty,
    int shields_skill_scaled,
    int strength,
    int scale);
```

player wrapper 결과가 기존과 bit-for-bit 동일해야 한다.

---

# 82. VERIFIED — player shield base에는 Shields skill + DEX가 직접 들어감

0.34.1 player shield 계산 계층:

```cpp
player::shielded()
player::shield_bonus()
player_shield_class(...)
```

내부 `_sh_from_shield()` 계열은:
- shield base property
- `SK_SHIELDS`
- shield plus
- DEX

를 사용한다.

`player::shield_bonus()`는 최종 shield class에 randomization을 적용한다.

실제:
```cpp
const int shield_class = player_shield_class();

if (shield_class <= 0)
    return -100;

return random2avg(shield_class * 2, 2) / 3 - 1;
```

또:

```cpp
int player::shield_bypass_ability(int tohit) const
{
    return 15 + tohit / 2;
}
```

## 구현 결론

mercenary SH는 monster HD proxy를 버리고:

```text
actual shield item
+ Record Shields skill
+ Record DEX
+ shell temporary effects
```

를 사용해야 한다.

---

# 83. PATCH DECISION — shield pure helper 추출

후보:

```cpp
int shield_class_from_item_skill_dex(
    const item_def &shield,
    int shields_skill_scaled,
    int dex,
    int scale);
```

player 기존 helper가 이미 충분히 pure하면 이름을 새로 만들지 않고
그 helper를 actor input 형태로 overload한다.

### mercenary
```cpp
monster::shield_class()
{
    if (is_mercenary())
        return mercenary_shield_class(*this);

    // original monster
}
```

### `shield_bonus()`
player와 같은 randomization을 공유할 수 있는지
`monster::shield_bonus()` call contract까지 확인 후 최종 확정.

---

# 84. VERIFIED — player Will 기본은 XL × species modifier 계열

0.34.1 player Will 계산은:
- XL
- species Will modifier
- artefact Will
- armour ego Will/Guile
- ring Will
- mutation
- form
- duration/environment
- clamp

를 합성한다.

## 구현 결론

mercenary Will:
- Record.XL
- 실제 species/constitution/trait
- actual Record equipment
- shell temporary status

를 authority로 사용.

### 금지
- shell HD 기반 monster Will
- monster type base Will
- global `you` form/god/environment state 자동 상속

---

# 85. PATCH DECISION — Will 공통화 경계

pure core 후보:

```cpp
int base_will_from_xl_species(
    int xl,
    species_type species);
```

장비 contribution:
```cpp
int equipment_will_bonus(...);
```

temporary shell effect:
```cpp
apply_mercenary_temporary_will_modifiers(shell, will);
```

정확한 helper명은 기존 player utility를 확인한 뒤 최소 변경으로 정한다.

---

# 86. VERIFIED — player resist도 실제 equipment + player global effect가 혼합됨

0.34.1 player resistance 함수는:
- actual equipped items
- rings
- artefact properties
- mutations
- forms
- god effects
- durations
- environmental effects
- clamp

를 섞는다.

## 구현 결론

mercenary resistance에 player 함수 자체를 호출하면 안 된다.

구조:

```text
Record intrinsic
+ actual Record equipment
+ artefact properties
+ shell temporary status
= merc final resistance
```

player religion/form/global duration은 자동 적용하지 않는다.

---

# 87. CORRECTED — “player defense를 그대로 actor generic으로 만들기”는 과도함

Checkpoint 02~04에서 공통화 가능성을 넓게 열어뒀지만,
실제 player body를 확인한 결과 초기 패치에서:

```text
player AC/EV/SH/Will/res 전체를 actor generic화
```

하는 것은 침범 범위가 너무 크다.

## 안전한 순서

1. mercenary helper를 별도 구현
2. 원본 pure formula/helper가 있으면 그대로 재사용
3. 중복되는 순수 산술이 명확할 때만 작은 input-based helper 추출
4. player wrapper 결과 동일성 테스트
5. player global state는 건드리지 않음

---

# 88. UPDATED P2B — 실제 방어 구현 단계

## P2B-1 equipment/intrinsic query
- actual Record equipment
- Record species/trait
- Record stat/skill

## P2B-2 AC
```cpp
monster::armour_class()
    -> mercenary_armour_class()
```

## P2B-3 EV
```cpp
monster::evasion()
    -> mercenary_evasion()
```

## P2B-4 SH
```cpp
monster::shield_class()
monster::shield_bonus()
monster::shield_bypass_ability()
```

## P2B-5 resistance / Will
```cpp
monster::res_fire()
monster::res_cold()
monster::res_elec()
monster::res_poison()
monster::res_negative_energy()
monster::res_corr()
monster::willpower()
```

각 merc helper는:
```text
Record persistent data
+
shell live temporary state
```
를 합성한다.

---

# 89. Regression tests — player pure helper extraction

pure input helper를 추출할 때마다:

```text
기존 player wrapper 결과
==
새 pure helper에 현재 you 입력을 전달한 결과
```

를 deterministic seed 범위에서 비교한다.

필수 후보:
- adjusted body armour penalty
- adjusted shield penalty
- shield class core
- stat/skill melee damage scaling
- attack delay core (추후 확인)

### 실패 시
generic helper 확장을 중단하고 merc-specific implementation으로 되돌린다.

---

# 90. Regression tests — mercenary HD 비의존성

Record가 동일할 때 shell HD만 강제로 변화시키는 dev test:

```text
merc AC
merc EV
merc SH
merc Will
merc melee to-hit
merc melee damage
merc attack delay
```

가 HD 때문에 변하면 실패.

예외:
engine compatibility에서 명시적으로 HD를 쓰기로 한 기능만 별도.

현재 기본 전투/방어 authority에는 HD 사용 금지.

---

# 91. Regression tests — ordinary monster 비침범

mercenary branch 추가 후 ordinary monster:

```text
monster::skill()
monster::attack_delay()
monster::weapon()
monster::armour_class()
monster::evasion()
monster::shield_class()
monster::res_*
monster::willpower()
```

의 기존 결과가 동일 seed/input에서 바뀌지 않아야 한다.

---

# 92. 현재 미검증 상태 유지 — player attack delay

Checkpoint 05 시점에서도
**`player::attack_delay()`의 0.34.1 정확한 전체 body를 아직 VERIFIED로 올리지 않는다.**

따라서 다음은 금지:

```cpp
mercenary_attack_delay(...) {
    // 추측 공식
}
```

다음 Checkpoint에서 실제 정의 위치/body를 잡은 후:
- weapon skill
- min delay
- armour/shield penalty
- form/unarmed
- speed brand
- random_var
- rescale/status

경계를 확정한다.

상태:
```text
SOURCE CHECK REQUIRED
```

---

# 93. Checkpoint 05 상태표

| 영역 | 상태 |
|---|---|
| player base AC equipment layer | VERIFIED |
| player final AC에 global effect 혼합 | VERIFIED |
| player EV `_player_evasion` 사용 | VERIFIED |
| body armour penalty pure extraction 가능성 | VERIFIED + PATCH DECISION |
| shield penalty pure extraction 가능성 | VERIFIED + PATCH DECISION |
| shield class core skill/DEX 사용 | VERIFIED |
| player shield bonus randomization | VERIFIED |
| player Will XL/species/equipment 구조 | VERIFIED |
| player resist equipment+global 혼합 | VERIFIED |
| merc defense = player 전체 함수 호출 | REJECTED |
| merc defense = Record + shell temp layer | PATCH DECISION |
| player attack delay exact body | SOURCE CHECK REQUIRED |
| monster props serialization | SOURCE CHECK REQUIRED |
| mgen/create_monster lifecycle | SOURCE CHECK REQUIRED |

---

# Checkpoint 05 결론

실제 0.34.1 player defense 코드를 확인한 결과,
용병을 player-like하게 만들기 위해 **player 객체 함수를 통째로 재사용하는 방식은 안전하지 않다.**

안정적인 구현 방향은:

```text
원본 player/monster public path 유지
        +
작은 pure 수식만 input-based helper로 추출
        +
mercenary는 Record persistent data
        +
monster shell temporary state
```

이다.

즉:

```text
“player처럼 계산”
≠
“mercenary를 player로 가장”
```

이며 이 구분을 모든 전투/방어 패치에 적용한다.


---

# Checkpoint 06 — monster props 저장 / player EV 세부 / shell link 재검증

> Checkpoint 06은 Checkpoint 05까지 포함한 누적본이다.
> 이 문서가 앞 checkpoint의 미확정 추정과 충돌하면 Checkpoint 06을 우선한다.

---

# 94. VERIFIED — monster `props`는 level monster save에 자동 포함됨

파일:
```text
crawl-ref/source/tags.cc
```

실제 함수:
```cpp
void marshallMonster(writer &th, const monster& m)
```

monster 본체의 주요 state를 기록한 뒤 마지막 부분에서 실제로:

```cpp
m.props.write(th);
```

를 호출한다.

반대편:
```cpp
void unmarshallMonster(reader &th, monster& m)
```

에서는:

```cpp
m.props.clear();
m.props.read(th);
```

를 호출한다.

## 구현 의미

shell에:

```cpp
mon.props[MERCENARY_ID_KEY] = merc_id;
```

형태로 stable integer link를 두면,
**active level monster save/load에서는 별도 monster field marshalling 변경 없이 자동 round-trip**된다.

이는 Checkpoint02의 props 기반 prototype 제안을 실제 0.34.1 소스가 뒷받침한다.

상태:
```text
VERIFIED + PATCH DECISION CONFIRMED
```

---

# 95. VERIFIED — `marshallMonster()`는 monster inventory index 자체도 별도 저장

실제 `marshallMonster()`는 `MP_ITEMS`를 계산하고:

```cpp
if (parts & MP_ITEMS)
    for (int j = 0; j < NUM_MONSTER_SLOTS; j++)
        marshallShort(th, m.inv[j]);
```

를 사용한다.

## 구현 의미

mercenary 실제 장비를 `monster.inv[]`에도 넣으면:
- shell save가 그 inventory link를 별도 authority로 저장
- Record equipment save도 따로 저장
- ownership 이중화

가 발생할 수 있다.

따라서 기존 확정:
```text
merc actual equipment = MercenaryRecord only
monster.inv mirror = 금지
```

가 실제 save 구조상 더 중요해졌다.

---

# 96. VERIFIED — follower/transit도 `marshallMonster()`를 재사용

`tags.cc` 실제:

```cpp
static void marshall_follower(writer &th, const follower &f)
{
    ASSERT(!invalid_monster_type(f.mons.type));
    ASSERT(f.mons.alive());

    marshallMonster(th, f.mons);
    marshallInt(th, f.transit_start_time);

    for (int i = 0; i < NUM_MONSTER_SLOTS; ++i)
        marshallItem(th, f.items[i]);
}
```

unmarshal도:
```cpp
unmarshallMonster(th, f.mons);
```

를 사용한다.

## 중요한 의미

monster `props` 기반 `merc_id`는:
- active level monster
- follower/transit monster representation

양쪽에서 **monster serialization을 통과하면 보존될 가능성이 높다.**

다만 mercenary를 원본 follower transit 시스템에 실제로 넣을지 여부는 별도 설계다.

프로젝트 기본:
```text
MercenaryRecord = 영구 authority
shell = field representation
```

이므로 원본 follower object를 merc 영구 authority로 승격하지 않는다.

---

# 97. PATCH DECISION CONFIRMED — P1 shell identity는 props 우선

초기 구현:

```cpp
static constexpr const char *MERCENARY_ID_KEY = "muhyeop_merc_id";
```

또는 Crawl 기존 key naming style에 맞는 macro/constant.

typed API:

```cpp
bool is_mercenary_monster(const monster &mon);

optional<merc_id_t> try_mercenary_id(const monster &mon);

merc_id_t require_mercenary_id(const monster &mon);

MercenaryRecord *get_mercenary_record(monster &mon);
const MercenaryRecord *get_mercenary_record(const monster &mon);
```

### 직접 string key 접근 제한
combat/UI/save 코드 곳곳에서:

```cpp
mon.props["muhyeop_merc_id"]
```

를 직접 쓰지 않는다.

오직 mercenary adapter/helper 계층에서만 접근.

---

# 98. broken props type도 fail-closed

`props`는 동적 variant property storage이므로
해당 key가:
- 없음
- 정수가 아님
- range 밖
- roster에 없는 merc_id

인 경우가 있을 수 있다.

따라서:

```cpp
try_mercenary_id()
```

는 validation 포함.

금지:
```cpp
invalid merc_id -> 0
invalid merc_id -> ordinary monster로 처리
invalid merc_id -> 첫 mercenary 선택
```

### release behavior
broken link이면:
- merc-specific action 거부
- diagnostic
- save/load validation에서 가능한 빨리 발견

---

# 99. `props` 사용으로 tag minor가 필요 없는 범위와 필요한 범위를 분리

## shell merc_id
기존 monster props serializer를 쓰므로
**이 link 하나만 추가하는 것 자체에는 별도 monster field용 tag minor가 필요하지 않을 가능성이 높다.**

## MercenaryRecord roster
반대로 Record 전체는 새 persistent data이므로:
- 명시 roster serialization
- tag minor append
- 구버전 save fallback

이 여전히 필요하다.

즉:

```text
shell -> merc_id
    existing props serialization

roster -> full Record
    new typed save block + save version handling
```

으로 분리.

---

# 100. VERIFIED — player EV permanent core도 `you` global에 직접 묶임

0.34.1 `player.cc`:

```cpp
static int _player_evasion(int final_scale, bool ignore_temporary)
```

permanent 영역은 대략:

```cpp
const int size_factor = _player_evasion_size_factor();
const int size_base_ev = (10 + size_factor) * scale;

int natural_evasion =
      size_base_ev
    + _player_armour_adjusted_dodge_bonus(scale)
    - you.adjusted_body_armour_penalty(scale)
    - you.adjusted_shield_penalty(scale)
    - _player_aux_evasion_penalty(scale)
    + get_form()->ev_bonus()
    + _player_base_evasion_modifiers() * scale;
```

이후 temporary multiplier/modifier가 추가된다.

## 구현 의미

player EV를 한 함수 통째로 actor overload 하는 것은 초기 구현에 부적합.

mercenary EV는:
- size
- DEX
- Dodging
- armour penalty
- shield penalty

같은 **실제 공유 대상**만 pure helper로 추출하고,
player form/mutation/global state는 포함하지 않는다.

---

# 101. VERIFIED — player dodge bonus 핵심도 DEX × Dodging + armour/STR

실제 helper:

```cpp
static int _player_armour_adjusted_dodge_bonus(int scale)
```

핵심 입력:
- `you.skill(SK_DODGING, 10)`
- `you.dex()`
- size factor
- body armour encumbrance
- `you.strength()`

이다.

## PATCH DECISION

pure input helper 후보:

```cpp
int armour_adjusted_dodge_bonus_from_inputs(
    int scale,
    int dodging_skill_scaled_10,
    int dex,
    int size_factor,
    int armour_penalty,
    int strength);
```

player wrapper는 기존 결과 동일.
mercenary는 Record/stat/equipment input 사용.

---

# 102. UPDATED P2B EV 구성

초기 merc EV:

```text
size base EV
+ pure dodge bonus(Dodging, DEX, size, armour, STR)
- pure adjusted armour penalty
- pure adjusted shield penalty
+ supported merc intrinsic modifiers
+ supported shell temporary modifiers
```

### 제외
- global player form
- player-only mutation helper
- player god effect
- `you.duration` 직접 조회

### ordinary monster
기존 `monster::evasion()` 그대로.

---

# 103. UPDATED save architecture

현재까지 실제 소스 기준:

```text
TAG_YOU / roster block
    └─ MercenaryRecord[]
       ├─ merc_id
       ├─ progression/stat/skills
       ├─ actual equipment item_def
       └─ martial/spell/traits/state

active monster chunk
    └─ monster shell
       ├─ HP/position/speed/enchants/AI
       ├─ props["muhyeop_merc_id"]
       └─ monster.inv[]에는 merc actual equipment 없음
```

로드 후:
```text
1. Record roster 복원
2. active level monster 복원
3. props merc_id validation
4. merc shell ↔ Record link 확인
5. derived/UI/cache 재계산
```

정확한 TAG_YOU roster insertion point는 계속 별도 확인.

---

# 104. 신규 회귀 테스트 — props round-trip

## active level
1. merc Record 생성
2. shell 생성
3. shell props에 merc_id
4. save
5. load
6. `is_mercenary_monster(shell) == true`
7. `mercenary_id(shell) == original id`
8. roster lookup이 같은 Record identity를 resolve

## broken link
- props ID를 존재하지 않는 값으로 강제
- ordinary monster combat fallback이 일어나면 실패

## no-equipment-mirror
- shell `inv[]`가 비어 있어도
- `monster::weapon()` merc branch가 Record actual weapon 반환
- save/load 후에도 동일

---

# 105. Checkpoint 06 상태표

| 영역 | 상태 |
|---|---|
| monster props write in marshallMonster | VERIFIED |
| monster props read in unmarshallMonster | VERIFIED |
| active level props save roundtrip | VERIFIED by serializer path |
| follower serializer reuses marshallMonster | VERIFIED |
| shell merc_id props prototype | CONFIRMED PATCH DECISION |
| full Record in props | REJECTED |
| merc actual gear in monster.inv | REJECTED, save duplication risk verified |
| player EV permanent formula source | VERIFIED |
| player dodge core inputs | VERIFIED |
| merc EV pure-input extraction | PATCH DECISION |
| exact player attack delay definition | SOURCE CHECK REQUIRED |
| create_monster/mgen lifecycle | SOURCE CHECK REQUIRED |

---

# Checkpoint 06 결론

Checkpoint02에서 제안했던:

```text
monster.props에 merc_id만 저장
```

방식은 실제 0.34.1 `marshallMonster()`/`unmarshallMonster()`가
`m.props.write()`/`read()`를 이미 수행하므로
**낮은 침범도로 shell identity를 저장하는 현실적인 1차 구현안**으로 확인됐다.

반면 `monster.inv[]`는 별도 저장 대상이므로
actual merc equipment를 shell에도 복제하면 실제 save authority가 이중화된다.

따라서:

```text
props = link only
Record = permanent authority
shell = runtime field state
monster.inv = ordinary monster only
```

원칙을 유지한다.


---

# Checkpoint 07 — monster MID / shell lifecycle / spawn 경계 재검증

> Checkpoint 07은 Checkpoint 06까지 포함한 누적본이다.
> 확인하지 못한 `create_monster()` 내부 순서는 추측으로 확정하지 않는다.

---

# 106. VERIFIED — `monster::reset()`은 MID와 props를 모두 제거

파일:
```text
crawl-ref/source/monster.cc
```

0.34.1 실제 `monster::reset()`:

```cpp
mid = 0;
...
type = MONS_NO_MONSTER;
...
speed_increment = 0;
...
mons_remove_from_grid(*this);
...
props.clear();
```

## 구현 의미

mercenary shell이 reset/despawn되어 monster slot이 재사용되면:
- old MID는 유지되지 않음
- `muhyeop_merc_id` props도 유지되지 않음

따라서 Record↔shell linkage를 shell 재생성 때마다 명시적으로 다시 붙여야 한다.

이는 올바른 동작이다:
```text
MercenaryRecord = persistent
monster shell    = disposable representation
```

---

# 107. VERIFIED — `monster::init_with()`는 MID와 props를 복사함

실제:

```cpp
void monster::init_with(const monster& mon)
{
    reset();
    mid = mon.mid;
    ...
    props = mon.props;
    ...
}
```

## 위험

일반 monster copy/clone 경로에서 merc shell 전체를 그대로 복제하면:
- 동일 merc_id props
- 동일/복사된 MID semantics

등이 생길 수 있다.

## PATCH DECISION

mercenary shell을 일반 monster clone/copy 기능의 대상으로 사용하지 않는다.

필요한 복제/분신 gameplay가 있더라도:
- 원본 merc_id를 새 shell에 복제하지 않음
- 별도 runtime actor identity
- MercenaryRecord authority 없음

으로 처리.

---

# 108. VERIFIED — `monster::set_new_monster_id()` 실제 body

0.34.1:

```cpp
void monster::set_new_monster_id()
{
    mid = ++you.last_mid;

    ASSERT(mid < MID_FIRST_NON_MONSTER);

    env.mid_cache[mid] = mindex();
}
```

공식 raw source 기준:
```text
monster.cc:4211~4219
```

## 의미

원본 monster의 실제 runtime stable identity는:
```text
monster::mid
```

이며 새 monster ID source는:
```text
you.last_mid
```

이다.

`mindex()`는 monster slot/index이고,
MID와 동일한 identity가 아니다.

---

# 109. PATCH DECISION — merc_id와 MID를 합치지 않음

두 ID의 의미를 명확히 분리한다.

```text
merc_id
    = MercenaryRecord의 world-persistent identity
    = 세대/층/원정/shell 교체에도 유지

monster::mid
    = 현재 monster shell runtime/engine identity
    = shell 재생성 시 새 값 가능
```

금지:
```cpp
merc_id = shell.mid;
shell.mid = merc_id;
```

### 이유
- MID는 `you.last_mid` / `env.mid_cache` engine lifecycle에 묶임
- MercenaryRecord는 shell이 없어도 존재
- DEAD merc도 Record identity 유지
- shell 재생성 가능

---

# 110. VERIFIED — MID cache는 `set_new_monster_id()`에서 즉시 연결

실제:
```cpp
env.mid_cache[mid] = mindex();
```

따라서 merc shell이 정상 original spawn lifecycle을 거쳐 MID를 받는다면
actor lookup은 원본 MID 체계를 그대로 사용할 수 있다.

## 구현 원칙

새로운 merc 전용 actor lookup global map을 만들지 않는다.

런타임 shell lookup:
```text
MID / original engine actor lookup
```

persistent merc lookup:
```text
merc_id / MercenaryRoster
```

두 층을 구분한다.

---

# 111. VERIFIED — `monster::find_home_near_place()`는 original movement helper를 사용

실제 0.34.1:

```cpp
bool monster::find_home_near_place(const coord_def &c)
```

는 유효 위치를 탐색한 뒤:

```cpp
bool moved_to_pos = move_to(place, MV_INTERNAL);
ASSERT(moved_to_pos);
```

를 사용한다.

주석상:
- monster가 아직 level에 완전히 올라오지 않았을 수 있어
- location effect finalisation은 그 시점에 하지 않음

## 구현 의미

merc shell 배치를 raw:
```cpp
shell.position = p;
mgrd(p) = ...
```

로 직접 조립하는 것보다
original placement/movement lifecycle을 재사용해야 한다는 기존 방향이 맞다.

---

# 112. VERIFIED — `find_place_to_live()`는 near-player / anywhere helper 조합

실제:

```cpp
bool monster::find_place_to_live(bool near_player, bool force_near)
{
    return near_player && find_home_near_player()
           || (!force_near && find_home_anywhere());
}
```

## 구현 의미

용병 입장 배치에서:
- 주인공 주변 안전 배치
- placement 실패 반환

이라는 얇은 wrapper를 만들 수 있다.

다만 이 함수가 **새 monster 생성 전체 lifecycle**을 담당하는 것은 아니다.
이미 만들어진 monster의 home position 탐색 helper다.

---

# 113. VERIFIED — `create_monster(mgen_data(...))`가 원본의 일반 생성 API

0.34.1 코드 전반에서 다음 패턴이 사용된다.

```cpp
mgen_data mg(...);
monster *mon = create_monster(mg);
```

또는:
```cpp
monster *mon = create_monster(
    mgen_data(...));
```

반환값:
```text
monster* 또는 생성 실패 시 nullptr
```

이라는 호출 contract가 실제 사용부에서 확인된다.

## 현재 검증 한계

`mon-place.cc`의 0.34.1 `create_monster()` **전체 구현 body와 내부 순서**는
현재 소스 접근 경로에서 아직 완전히 확보하지 못했다.

따라서 다음은 아직 확정 금지:
- free slot 확보 정확한 시점
- `set_new_monster_id()` 정확한 호출 지점
- grid 등록 시점
- gear 생성 시점
- final placement effect 시점
- post-create hooks 순서

상태:
```text
SOURCE CHECK REQUIRED
```

---

# 114. CORRECTED — merc_id props 부착 시점은 아직 최종 확정 아님

이전 개념:
```text
create_monster() 성공
→ 반환된 shell에 merc_id props 붙임
```

은 단순하고 안전해 보이지만,
`create_monster()` 내부에서:
- stats query
- equipment creation
- AI/finalisation
- placement event

가 merc-specific branch를 호출할 가능성을 아직 배제하지 못했다.

따라서 현재는 두 후보를 유지한다.

## 후보 A — create 성공 직후 attach
```cpp
monster *shell = create_monster(mg);
if (!shell)
    return failure;

attach_merc_id(*shell, rec.id);
```

장점:
- 원본 생성 API 최소 침범

위험:
- create 내부에서 이미 merc-aware override가 필요하면 늦음

## 후보 B — `mgen_data`/creation hook로 identity를 미리 전달
만약 `mgen_data`가 props/custom init data 전달을 안전하게 지원하거나,
creation 과정에 작은 hook를 넣을 수 있다면:
```text
create lifecycle 시작부터 mercenary identity available
```
하게 할 수 있음.

### 최종 선택
`create_monster()`와 `mgen_data` 실제 body 전부 확인 후 확정.

---

# 115. PATCH DECISION — one-active-shell validation은 spawn 전후 둘 다

Persistent authority:
```text
merc_id
```

spawn 전에:
```cpp
find_active_shell_by_merc_id(rec.id)
```
가 이미 성공하면 새 shell 생성 금지.

spawn 성공 후:
- shell의 merc_id
- Record 상태
- level
- active shell uniqueness

재검증.

### 금지
duplicate 발견 시:
```text
둘 중 하나 임의 삭제
```

개발:
- assert / diagnostic

릴리스:
- 새 spawn 실패
- 기존 active shell 유지

---

# 116. PATCH DECISION — spawn 실패는 Record를 변경하지 않음

정상 transaction:

```text
1. Record 존재/ALIVE/배치가능 검증
2. active shell 없음 검증
3. original monster spawn 시도
4. 실패 → Record unchanged
5. 성공 → merc linkage commit
6. shell live-state 초기화
7. final validation
```

### 절대 금지
spawn 시작 전에:
```text
Record.state = DEPLOYED
```
로 먼저 바꾸고 실패 후 복구하는 구조.

field representation 생성 성공이 확인된 뒤
배치-derived 상태만 갱신.

---

# 117. PATCH DECISION — shell MID는 original engine에 맡김

merc wrapper에서:
```cpp
shell->mid = ...
```
직접 배정하지 않는다.

`set_new_monster_id()` 또는 original creation path가 MID를 관리하게 한다.

merc persistent link는 별도:
```cpp
shell.props[MERCENARY_ID_KEY] = rec.id;
```

---

# 118. PATCH DECISION — shell despawn/reset 전에 persistent item을 건드리지 않음

`monster::reset()`은:
```text
inv clear
props clear
grid removal
MID clear
```
등 shell state를 비운다.

merc actual item은 Record에 있으므로:
```text
shell reset/despawn
→ Record equipment untouched
```

가 되어야 한다.

### death drop exception
실제 merc DEAD 처리로 장비를 world에 떨어뜨리는 경우만
명시적인 Record→world item ownership transaction을 먼저 수행.

그 후 shell 제거.

---

# 119. MID 관련 테스트

## shell respawn
1. Record merc_id = A
2. shell1 spawn → MID X
3. despawn
4. shell2 spawn → MID Y
5. `A`는 동일
6. `X != Y` 허용/정상

## lookup
- `monster_by_mid(Y)` → current shell
- roster.find(A) → persistent Record

## duplicate
- A에 shell이 살아 있는데 두 번째 spawn 요청
- 실패해야 함

## stale MID
- despawn된 X를 다음 merc shell에 persistent target ID로 재사용 금지

---

# 120. Checkpoint 07 상태표

| 영역 | 상태 |
|---|---|
| monster reset clears MID | VERIFIED |
| monster reset clears props | VERIFIED |
| monster copy copies MID/props | VERIFIED |
| merc shell generic clone 금지 | PATCH DECISION |
| set_new_monster_id body | VERIFIED |
| MID source = ++you.last_mid | VERIFIED |
| MID cache registration | VERIFIED |
| merc_id와 MID 분리 | PATCH DECISION |
| find_home uses original move_to | VERIFIED |
| create_monster returns pointer/null contract | VERIFIED from call sites |
| create_monster exact internal sequence | SOURCE CHECK REQUIRED |
| exact MID assignment point in create_monster | SOURCE CHECK REQUIRED |
| merc_id attach exact point | SOURCE CHECK REQUIRED |
| one-active-shell pre/post validation | PATCH DECISION |
| spawn failure Record unchanged | PATCH DECISION |

---

# Checkpoint 07 결론

실제 0.34.1에는 이미 두 가지 다른 identity 역할이 있다.

```text
monster::mid
    = current engine actor identity
    = you.last_mid에서 생성
    = env.mid_cache로 runtime lookup

merc_id
    = 무협돌죽 MercenaryRecord persistent identity
```

둘을 합치지 않는다.

mercenary shell은 원본 monster MID lifecycle을 그대로 사용하고,
`props`에는 persistent `merc_id` link만 둔다.

다만 `create_monster()` 내부 순서를 완전히 검증하기 전에는
**merc_id를 creation 전/후 어느 시점에 붙일지 확정하지 않는다.**
이 부분은 다음 spawn-source 검증에서 최종 결정한다.


---

# Checkpoint 08 — attack-delay 정의 추적 / 0.34.1 소스 파일 존재 재검증

> Checkpoint 08은 Checkpoint 07까지 포함한 누적본이다.
> 이번 체크포인트는 **확인하지 못한 함수 body를 추측으로 채우지 않는 것** 자체를 구현 안전성 규칙으로 명시한다.

---

# 121. VERIFIED — DCSS 0.34.1 실제 소스 패키지에 관련 파일 존재

Mageia/Fedora 0.34.1 debugsource/source file list에서 실제로 다음 파일들이 존재함을 재확인:

```text
source/player-act.cc
source/player-equip.cc
source/player-stats.cc
source/player.cc
source/player.h
source/mgen-data.h
source/mon-place.cc   (패키지 전체 source tree의 monster placement 계층)
source/fight.cc
source/attack.cc
source/melee-attack.cc
source/monster.cc
source/tags.cc
```

따라서 이후 매핑은 “파일이 있을 것”이라고 상상하지 않고
0.34.1 실제 package/source tree 기준으로 계속 추적한다.

---

# 122. VERIFIED — `fight.h`의 delay 관련 공용 helper 선언

0.34.1 Debian source `fight.h`에서 실제 선언 확인:

```cpp
int weapon_min_delay_skill(const item_def &weapon);
int weapon_min_delay(const item_def &weapon, bool check_speed = true);
int weapon_adjust_delay(const item_def &weapon,
                        int base_delay,
                        bool random = true);
```

이 helper들은 실제 0.34.1 melee/weapon delay 계층의 재사용 후보다.

## 구현 의미

mercenary delay를 별도 임의 공식으로 만들지 않는다.

최종 merc delay helper는 반드시:
- 원본 weapon base delay
- `weapon_min_delay_skill`
- `weapon_min_delay`
- `weapon_adjust_delay`
- player weapon skill 반영 방식

을 실제 player `attack_delay()` body와 대조한 뒤 확정한다.

---

# 123. SOURCE CHECK REQUIRED — `player::attack_delay()` 실제 정의 body

현재까지 확정 가능한 것:

- 0.34.1 player actor interface에 `attack_delay(...)`가 존재
- `fight_melee()` player path는 player melee delay를 사용
- monster shell path는 virtual `attacker->attack_delay().roll()`을 사용
- `monster::attack_delay()`는 weapon skill을 사용하지 않는 별도 monster 구현

그러나 **0.34.1 `player::attack_delay()` 전체 정의 body와 정확한 정의 파일**은
현재 접근 가능한 공식 raw/Debian indexed source에서 아직 직접 확보하지 못했다.

따라서 다음을 절대 추측 확정하지 않는다.

```cpp
random_var mercenary_attack_delay(...)
{
    // 추정한 player 공식
}
```

상태:
```text
SOURCE CHECK REQUIRED
```

---

# 124. PATCH DECISION — delay patch는 “body 검증 후”로 의존성 고정

P3F 실행 조건:

```text
player::attack_delay() actual 0.34.1 body VERIFIED
```

전에는 구현하지 않는다.

즉 패치 순서:

```text
P3A~E
    ↓
delay source verification
    ↓
P3F actor-safe delay extraction
```

로 고정.

---

# 125. VERIFIED — merc delay가 적용될 엔진 호출 지점은 이미 확보됨

`fight_melee()` non-player tail:

```text
attacker->attack_delay().roll()
```

을 호출하므로,
`monster::attack_delay()`에서 mercenary early branch가 들어가면
추가 scheduler 패치 없이 merc delay가 기존 monster energy 계산으로 들어간다.

구조:

```text
monster shell scheduler
    ↓
fight_melee()
    ↓
attacker->attack_delay()
    ↓
monster::attack_delay()
    ├─ merc -> mercenary_attack_delay()
    └─ ordinary monster -> original body
```

따라서 최종 merc delay 공식만 검증되면
energy commit 구조는 별도 재설계할 필요가 없다.

---

# 126. CORRECTED — delay 관련 구현 문서 표기

이전 Checkpoint 일부에서 “player min-delay 규칙 공유”를
이미 구현 가능 수준으로 읽을 수 있게 써둔 부분은 다음으로 정정.

현재 상태:

```text
설계 목표:
    player-like weapon-skill delay 공유

엔진 연결 위치:
    VERIFIED

공용 weapon delay helper:
    VERIFIED

player exact formula/body:
    SOURCE CHECK REQUIRED

merc exact implementation:
    NOT YET SPECIFIED
```

---

# 127. Source verification rule 강화

앞으로 실제 구현 명세서에 함수 단위로 다음 중 하나를 붙인다.

```text
VERIFIED_BODY
VERIFIED_DECLARATION_ONLY
VERIFIED_CALLSITE_ONLY
PATCH_DECISION
SOURCE_CHECK_REQUIRED
```

### 이유
“함수 존재 확인”과 “body 확인”을 같은 VERIFIED로 쓰면
제작 단계에서 잘못된 추론이 섞일 수 있다.

Checkpoint 08 이후부터는 가능하면 이 세분화 표기를 사용한다.

---

# 128. 현재 attack-delay 추적 상태

| 항목 | 상태 |
|---|---|
| `fight.h` weapon delay helper 선언 | VERIFIED_DECLARATION_ONLY |
| `fight_melee()` delay 호출 | VERIFIED_BODY |
| `monster::attack_delay()` body | VERIFIED_BODY |
| `monster::melee_attack_delay()` delegate | VERIFIED_BODY |
| player delay actor API 존재 | VERIFIED_DECLARATION_ONLY |
| `player::attack_delay()` 정확한 정의 파일 | SOURCE_CHECK_REQUIRED |
| `player::attack_delay()` exact body | SOURCE_CHECK_REQUIRED |
| merc delay exact formula | NOT YET SPECIFIED |
| monster scheduler energy integration | VERIFIED_BODY |

---

# 129. 다음 조사 우선순위

attack-delay body가 즉시 열리지 않는다고 전체 작업을 멈추지 않는다.

병렬로 실제 0.34.1에서 다음을 계속 매핑한다.

1. `mgen_data` actual fields / constructor
2. `create_monster()` exact lifecycle
3. MercenaryRecord roster의 `TAG_YOU` 삽입 위치
4. item marshalling API
5. shell HP / level transit / follower 처리
6. player equipment restriction에서 pure reuse 가능한 helper
7. build system source-file registration

attack delay 정의를 찾는 즉시 이 목록보다 우선해 교정/확정한다.

---

# Checkpoint 08 결론

이번 단계에서는 **함수 선언만 보고 body를 상상해서 구현하는 실수를 막는 것**을 우선했다.

용병 attack delay가 들어갈 엔진 지점과
원본 weapon-delay helper는 확인됐지만,
player의 정확한 0.34.1 delay body가 직접 검증되기 전까지
mercenary delay 수식은 확정하지 않는다.

이후 구현 명세는:
```text
“확실히 아는 것만 코드 수준으로 확정”
```
원칙으로 계속 진행한다.



---

# Checkpoint 09 — `TAG_YOU` MercenaryRecord roster 저장 삽입점 / tag minor 조건 재검증

작성일: 2026-09-13

> Checkpoint 09는 Checkpoint 08까지의 전체 누적본을 포함한다.
> 이번 체크포인트는 작업 복구 포인터 1~2순위인
> `TAG_YOU / MercenaryRecord roster 정확한 save 삽입점`과
> `tag-version.h minor append 위치 / 구버전 load 조건`을
> 공식 DCSS 0.34.1 소스 기준으로 다시 고정한다.

## 소스 authority

공식 DCSS 0.34.1:
- `crawl-ref/source/tags.cc`
- `crawl-ref/source/tag-version.h`
- release tag `0.34.1` / release commit 표시 `1eebc1a`

이 체크포인트에서도 함수 body를 직접 확인한 것과 패치 결정을 구분한다.

---

# 130. VERIFIED_BODY — `TAG_YOU`의 실제 최상위 write 순서

0.34.1 `tags.cc`의 `tag_write()`에서 `TAG_YOU`는 실제로 다음 순서다.

```cpp
_tag_construct_you(th);
CANARY;
_tag_construct_you_items(th);
CANARY;
_tag_construct_you_dungeon(th);
CANARY;
_tag_construct_lost_monsters(th);
CANARY;
_tag_construct_companions(th);
```

즉 0.34.1 원본에서 `companions`가 현재 `TAG_YOU` chunk의 마지막 sub-block이다.

상태:

```text
VERIFIED_BODY
```

---

# 131. VERIFIED_BODY — `TAG_YOU`의 실제 read 순서와 companions 조건

0.34.1 `tag_read()`의 `TAG_YOU` case는 다음 순서로 읽는다.

```cpp
_tag_read_you(th);
EAT_CANARY;
_tag_read_you_items(th);
EAT_CANARY;
_tag_read_you_dungeon(th);
EAT_CANARY;
_tag_read_lost_monsters(th);
EAT_CANARY;

#if TAG_MAJOR_VERSION == 34
if (th.getMinorVersion() < TAG_MINOR_NO_ITEM_TRANSIT)
{
    _tag_read_lost_items(th);
    EAT_CANARY;
}

if (th.getMinorVersion() >= TAG_MINOR_COMPANION_LIST)
#endif
    _tag_read_companions(th);
```

그 뒤에는 `init_can_currently_train()` 및 과거 save fixup 같은 후처리로 넘어간다.

즉 현재 원본에는 companions 뒤에 다른 persistent `TAG_YOU` sub-block이 없다.

상태:

```text
VERIFIED_BODY
```

---

# 132. CORRECTED + PATCH_DECISION — Mercenary roster는 `_tag_construct_you()` 내부가 아니라 `TAG_YOU` 끝의 새 sub-block으로 append

과거 체크포인트에서는 다음 정도만 확정되어 있었다.

```text
TAG_YOU의 append-only compatible 영역에 mercenary roster block 추가
```

이 표현은 실제 삽입 위치가 아직 불명확했다.

공식 0.34.1 `tag_write()` / `tag_read()` 구조를 다시 확인한 뒤
초기 구현의 정확한 패치 위치를 다음으로 고정한다.

## write

새 helper 선언:

```cpp
static void _tag_construct_mercenaries(writer &th);
static void _tag_read_mercenaries(reader &th);
```

`tag_write(TAG_YOU)`의 **현재 마지막인 companions 뒤에** 새 block을 append한다.

권장 구조:

```cpp
_tag_construct_lost_monsters(th);
CANARY;
_tag_construct_companions(th);

// 무협돌죽 추가
CANARY;
_tag_construct_mercenaries(th);
```

## read

기존 companions read semantics는 변경하지 않는다.

그 뒤에 새 minor 이상에서만:

```cpp
#if TAG_MAJOR_VERSION == 34
if (th.getMinorVersion() >= TAG_MINOR_MUHYEOP_MERCENARIES)
{
    EAT_CANARY;
    _tag_read_mercenaries(th);
}
#endif
```

를 추가한다.

### 왜 이 위치인가

1. 기존 `_tag_construct_you()` body의 중간 layout을 바꾸지 않는다.
2. player inventory / dungeon / lost monster / companion 기존 sub-block을 건드리지 않는다.
3. 새 기능이 하나의 독립 save block으로 격리된다.
4. 구버전 save는 새 minor 조건을 통과하지 않으므로 새 block을 읽지 않는다.
5. current 0.34.1 save layout의 마지막에 append되므로 기존 필드 desync 위험이 가장 낮다.

### 중요한 금지

다음은 초기 구현에서 하지 않는다.

```text
- _tag_construct_you() 내부 중간에 roster 필드 삽입
- _tag_construct_you_items()에 merc equipment를 섞기
- _tag_construct_companions() 안에 MercenaryRecord를 숨겨 저장
- monster shell save만으로 roster 영속성을 대신하기
```

상태:

```text
PATCH_DECISION
```

---

# 133. VERIFIED_BODY — 0.34.1 `tag-version.h` 실제 enum 끝

공식 `tag-version.h` 0.34.1의 끝부분은 실제로:

```cpp
TAG_MINOR_CONSTRICTED_TYPE,
TAG_MINOR_LUA_5_4,
TAG_MINOR_PIETY_LOGGING,
TAG_MINOR_MONINFO_CLEANUP,
#endif
NUM_TAG_MINORS,
TAG_MINOR_VERSION = NUM_TAG_MINORS - 1
```

이다.

즉 Checkpoint 01의

```text
새 minor는 맨 뒤 append
```

방향 자체는 맞았지만,
정확한 0.34.1 삽입 위치는 더 구체적으로 다음과 같다.

상태:

```text
VERIFIED_BODY
```

---

# 134. CORRECTED + PATCH_DECISION — custom minor의 정확한 append 위치

0.34.1 기반 커스텀 브랜치에서는:

```cpp
TAG_MINOR_PIETY_LOGGING,
TAG_MINOR_MONINFO_CLEANUP,
TAG_MINOR_MUHYEOP_MERCENARIES,
#endif
NUM_TAG_MINORS,
TAG_MINOR_VERSION = NUM_TAG_MINORS - 1
```

처럼 **`TAG_MINOR_MONINFO_CLEANUP` 직후, `#endif` 직전**에 append한다.

기존 numeric value는 하나도 재정렬하지 않는다.

`TAG_MINOR_VERSION`은 기존 식:

```cpp
TAG_MINOR_VERSION = NUM_TAG_MINORS - 1
```

을 그대로 유지한다.

새 enum 값이 append되므로 current minor는 자동으로 새 마지막 값을 가리킨다.

상태:

```text
PATCH_DECISION
```

---

# 135. PATCH_DECISION — 구버전 load fallback

`TAG_MINOR_MUHYEOP_MERCENARIES`보다 낮은 save에서는:

```text
MercenaryRoster = empty
next_id = 초기 안전값
active-party merc links = 없음
```

으로 초기화한다.

즉 read 구조:

```cpp
if (th.getMinorVersion() >= TAG_MINOR_MUHYEOP_MERCENARIES)
{
    EAT_CANARY;
    _tag_read_mercenaries(th);
}
else
{
    clear_mercenary_roster_for_legacy_load();
}
```

### 주의

`clear_mercenary_roster_for_legacy_load()`는 개념명이다.
실제 helper 이름/roster global ownership 위치는
`MercenaryRoster` 실제 코드 배치가 확정될 때 결정한다.

따라서:

```text
old-save default semantics = PATCH_DECISION
exact helper name = NOT YET SPECIFIED
```

---

# 136. PATCH_DECISION — 새 sub-block의 CANARY 위치

새 block을 독립적으로 분리하기 위해:

```cpp
_tag_construct_companions(th);
CANARY;
_tag_construct_mercenaries(th);
```

와

```cpp
_tag_read_companions(th);

if (th.getMinorVersion() >= TAG_MINOR_MUHYEOP_MERCENARIES)
{
    EAT_CANARY;
    _tag_read_mercenaries(th);
}
```

를 짝으로 둔다.

구버전 save에는 companions 뒤 CANARY가 없으므로
`EAT_CANARY`도 반드시 새 minor 조건 안에서만 실행한다.

### 금지

```cpp
_tag_read_companions(th);
EAT_CANARY; // 무조건 실행 금지
if (minor >= NEW_MINOR)
    _tag_read_mercenaries(th);
```

이렇게 하면 구버전 save에서 companions 뒤 데이터를 잘못 읽게 된다.

상태:

```text
PATCH_DECISION
```

---

# 137. 현재 roster save insertion 상태표

| 항목 | 상태 |
|---|---|
| `TAG_YOU` write sub-block 순서 | VERIFIED_BODY |
| `TAG_YOU` read sub-block 순서 | VERIFIED_BODY |
| companions가 현재 마지막 write block | VERIFIED_BODY |
| companions read minor 조건 | VERIFIED_BODY |
| merc roster를 독립 sub-block으로 저장 | PATCH_DECISION |
| merc roster 정확한 위치 = companions 뒤 append | PATCH_DECISION |
| custom CANARY를 새 minor 조건과 함께 읽기 | PATCH_DECISION |
| `tag-version.h` 마지막 원본 minor = `TAG_MINOR_MONINFO_CLEANUP` | VERIFIED_BODY |
| custom minor 위치 = `TAG_MINOR_MONINFO_CLEANUP` 뒤 / `#endif` 앞 | PATCH_DECISION |
| 기존 enum 재정렬 금지 | PATCH_DECISION |
| old save roster fallback = empty | PATCH_DECISION |
| `_tag_construct_mercenaries()` exact record field body | SOURCE_CHECK_REQUIRED |
| `_tag_read_mercenaries()` exact validation body | SOURCE_CHECK_REQUIRED |
| actual equipment `item_def` marshalling sequence | SOURCE_CHECK_REQUIRED |

---

# 138. 다음 즉시 조사 위치

복구 포인터 1~2순위의 **삽입점 / minor 조건**은 이번 체크포인트에서 고정했다.

다음 우선순위:

1. `mgen_data` actual fields / constructor
2. `create_monster()` exact lifecycle 및 MID assignment
3. merc shell에 `merc_id`를 붙일 안전한 정확 시점
4. `player::attack_delay()` 정확한 0.34.1 body
5. shell HP / follower / transit / level lifecycle
6. item marshalling 및 `MercenaryRecord` actual equipment save body
7. build system source-file registration

단, roster record body를 실제 작성하기 전
`marshallItem()` / `unmarshallItem()` signature와 item empty-slot 표현은 반드시 다시 확인한다.

---

# Checkpoint 09 결론

이번 재검증으로 MercenaryRecord roster의 저장 위치는 더 이상
막연한 "`TAG_YOU` 어딘가"가 아니다.

초기 안정 구현의 save 경계는 다음으로 고정한다.

```text
TAG_YOU
  ├─ you
  ├─ you_items
  ├─ you_dungeon
  ├─ lost_monsters
  ├─ companions
  ├─ CANARY                  [new minor 이상]
  └─ mercenaries             [new minor 이상]
```

그리고 save minor는:

```text
TAG_MINOR_MONINFO_CLEANUP
    ↓ append
TAG_MINOR_MUHYEOP_MERCENARIES
    ↓
NUM_TAG_MINORS
```

순서를 사용한다.

이렇게 하면:
- player 원본 save body를 중간 수정하지 않고
- ordinary monster 저장 구조를 건드리지 않으며
- 구버전 save에서는 roster를 empty로 안전 초기화하고
- 새 mercenary persistent subsystem만 독립적으로 추가할 수 있다.

Checkpoint 09 이후에도 실제 record field marshalling과 item ownership save body는
원본 0.34.1 helper를 직접 확인한 뒤에만 확정한다.



---

# Checkpoint 10 — `mgen_data` / `create_monster()` 호출 경계 재검증

작성일: 2026-09-13

> Checkpoint 10은 Checkpoint 09까지의 전체 누적본을 포함한다.
> 이번 단위는 복구포인터 다음 우선순위인
> `mgen_data` / `create_monster()`의 **실제 호출 경계**를 다시 확인한다.
>
> 중요:
> 이번 검증에서는 `mgen-data.h`와 `mon-place.cc`의 전체 0.34.1 body를
> 직접 확보하지 못한 항목을 절대 `VERIFIED_BODY`로 올리지 않는다.

---

# 139. VERIFIED_CALLSITE_ONLY — `mgen_data`는 create 전 mutable generation descriptor

실제 Crawl 소스 호출부에서 다음 패턴들이 반복된다.

```cpp
mgen_data mg(...);
mg.set_summoned(...);
mg.set_prox(...);
monster *mon = create_monster(mg);
```

또 다른 실제 호출부에서는:

```cpp
mgen_data mg(...);
mg.hd = ...;
const monster *mon = create_monster(mg);
```

또:

```cpp
mgen_data fauna_data(...);
fauna_data.extra_flags |= MF_WAS_IN_VIEW;
fauna_data.copy_from_parent(&mons);

if (create_monster(fauna_data))
{
    ...
}
```

즉 호출부 수준에서 확실한 경계는:

```text
mgen_data
    = monster 생성 전 입력 descriptor

create_monster(mg)
    = 실제 monster shell 생성 시도
```

이다.

현재 확인된 mutable input 예:
- `hd`
- `extra_flags`
- `set_summoned(...)`
- `set_prox(...)`
- `copy_from_parent(...)`

정확한 전체 field 목록 / constructor parameter default는 아직 body/선언 직접 확보 전이므로
이번 체크포인트에서 전부 확정하지 않는다.

상태:

```text
VERIFIED_CALLSITE_ONLY
```

---

# 140. VERIFIED_CALLSITE_ONLY — `create_monster()` 반환값은 생성된 shell pointer / failure null 의미로 사용됨

여러 실제 call-site가 다음처럼 사용한다.

```cpp
monster *mon = create_monster(mg);

if (!mon)
{
    // creation failed
    ...
}
```

또는:

```cpp
if (create_monster(mg))
    created = true;
```

그리고 성공 후:

```cpp
mon->props[...]
mon->flags |= ...
mon->foe = ...
mon->hit_points = ...
mon->behaviour = ...
```

처럼 반환된 실제 shell을 후처리한다.

따라서 Checkpoint07의:

```text
create_monster returns pointer/null contract
```

은 계속 유효하다.

상태:

```text
VERIFIED_CALLSITE_ONLY
```

---

# 141. VERIFIED_CALLSITE_ONLY — 생성 후 shell `props` 부착은 기존 코드에서도 실제 사용되는 패턴

실제 호출부에는:

```cpp
monster *bat = create_monster(...);

if (bat)
{
    bat->props[BLORKULA_REVIVAL_TIMER_KEY] = revive_timer;
    ...
}
```

같은 패턴이 존재한다.

즉:

```text
create_monster() 성공
    ↓
returned monster*
    ↓
shell.props 추가 설정
```

자체는 Crawl에서 이미 사용되는 정상적인 후처리 패턴이다.

## 구현 의미

mercenary shell도 기술적으로는:

```cpp
monster *shell = create_monster(mg);
if (shell)
    shell->props[MERCENARY_ID_KEY] = rec.id;
```

형태가 엔진 사용 패턴과 어긋나지 않는다.

하지만 이 사실만으로
**merc_id attach의 최종 정확 시점**을 아직 확정하면 안 된다.

이유:
- `create_monster()` 내부에서 MID/cache/grid/gear/behaviour 초기화가 어떤 순서인지 아직 body 직접 확인 필요
- creation 과정 안에서 post-create callback이나 visibility/AI side effect가 발생하는지 확인 필요
- one-active-shell invariant를 attach 전에 검사할지, 성공 직후 검사할지 결정 필요

따라서 상태:

```text
post-create props mutation pattern = VERIFIED_CALLSITE_ONLY
merc_id exact attach point          = SOURCE_CHECK_REQUIRED
```

---

# 142. CORRECTED — `mgen_data`에 merc persistent identity를 미리 넣는 안은 현재 확정하지 않음

`mgen_data`가 생성 전 descriptor라는 것은 확인했지만,
현재 직접 확인된 call-site만으로는 다음 필드가 존재한다고 볼 수 없다.

```cpp
mg.props[...]
```

또는:

```cpp
mg.merc_id = ...
```

따라서 merc persistent identity를
`mgen_data` 안에 새 field로 먼저 넣어 creation 내부에서 전달하는 구조는
현재 구현 명세에서 **확정하지 않는다**.

초기 저침범 후보는 여전히:

```text
1. Record 중복 active-shell 사전검사
2. ordinary create_monster(mg)
3. 실패하면 Record 변화 없음
4. 성공한 returned shell에 props[MERCENARY_ID_KEY] attach
5. postcondition validation
```

이다.

단 이 순서는 `create_monster()` exact body 확인 전까지:

```text
PATCH CANDIDATE — NOT FINAL
```

로 둔다.

---

# 143. PATCH_DECISION — spawn failure에서는 persistent Record를 mutate하지 않음

실제 call-site contract가 null failure를 사용하므로,
mercenary spawn wrapper도 다음 transaction 경계를 사용한다.

```text
before create:
    Record authority 유지
    state/gear/progression 변경 금지

create_monster(mg) == nullptr:
    Record unchanged
    active shell 없음

create_monster(mg) != nullptr:
    shell link attach
    validate link
    이후에만 active placement 성공으로 취급
```

특히 실패 전에:

```cpp
rec.state = ALIVE_ON_FIELD;
rec.active_mid = ...;
```

같은 persistent state를 먼저 commit하지 않는다.

현재 설계에서는 persistent active MID 자체를 두지 않는 방향이므로
더욱 단순하게:

```text
Record merc_id = persistent identity
shell MID      = engine runtime identity
```

경계를 유지한다.

상태:

```text
PATCH_DECISION
```

---

# 144. 현재 `mgen_data` / creation 검증 상태표

| 항목 | 상태 |
|---|---|
| `mgen_data`가 create 전 descriptor로 사용 | VERIFIED_CALLSITE_ONLY |
| `mgen_data.h` exact field list | SOURCE_CHECK_REQUIRED |
| constructor exact signature/defaults | SOURCE_CHECK_REQUIRED |
| `hd` create 전 override 사용 | VERIFIED_CALLSITE_ONLY |
| `extra_flags` create 전 override 사용 | VERIFIED_CALLSITE_ONLY |
| `set_summoned()` create 전 사용 | VERIFIED_CALLSITE_ONLY |
| `set_prox()` create 전 사용 | VERIFIED_CALLSITE_ONLY |
| `copy_from_parent()` create 전 사용 | VERIFIED_CALLSITE_ONLY |
| `create_monster()` pointer/null result contract | VERIFIED_CALLSITE_ONLY |
| returned shell post-create props mutation | VERIFIED_CALLSITE_ONLY |
| `create_monster()` exact internal body | SOURCE_CHECK_REQUIRED |
| free monster slot acquisition exact step | SOURCE_CHECK_REQUIRED |
| MID assignment exact step | SOURCE_CHECK_REQUIRED |
| grid registration exact step | SOURCE_CHECK_REQUIRED |
| equipment generation exact step | SOURCE_CHECK_REQUIRED |
| behaviour/foe finalisation exact step | SOURCE_CHECK_REQUIRED |
| merc_id exact attach point | SOURCE_CHECK_REQUIRED |

---

# 145. 다음 조사 위치

이번 체크포인트에서는 호출 경계만 올렸고
`create_monster()` 내부 순서는 아직 추측하지 않았다.

다음 조사 우선순위:

1. `mon-place.h`의 `create_monster()` exact declaration
2. `mgen-data.h` exact constructor / field list
3. `mon-place.cc` `create_monster()` full body
4. `get_free_monster()` 호출 위치
5. `set_new_monster_id()` 호출 위치
6. grid placement / MID cache registration 순서
7. 위가 끝나면 merc_id attach exact point 확정
8. 이후 `player::attack_delay()` body
9. shell transit/follower lifecycle
10. item marshalling / Record equipment save
11. build registration

---

# Checkpoint 10 결론

현재 확실히 확인된 엔진 경계는:

```text
mgen_data
    = 생성 전 mutable descriptor

create_monster(mg)
    = shell 생성 시도
    = success: monster*
    = failure: nullptr

returned monster*
    = 생성 성공 뒤 props/flags 등 후처리 가능
```

이다.

따라서 mercenary shell 생성도
별도 monster 생성 엔진을 만들 필요는 없고
원본 `create_monster()`를 이용하는 방향을 유지한다.

하지만:

```text
create_monster 내부에서
free slot → init → MID → grid → gear → behaviour → visibility
```

같은 순서를 현재 문서가 임의로 상상해서 확정해서는 안 된다.

특히 `merc_id`를 정확히 언제 붙일지는
0.34.1 `mon-place.cc` full body를 직접 확인한 뒤에만 최종 결정한다.



---

# Checkpoint 11 — DCSS 0.34.1 source authority / provenance 고정

작성일: 2026-09-13

> Checkpoint 11은 Checkpoint 10까지의 전체 누적본을 포함한다.
>
> 이번 체크포인트는 `mon-place.cc`, `mgen-data.h`, `player-act.cc`의 body를
> 아직 직접 확보하지 못한 상태에서 잘못된 버전의 소스를 섞는 위험을 제거하기 위해
> **어떤 소스를 0.34.1 VERIFIED의 근거로 인정할지**를 먼저 고정한다.
>
> 구현 함수 body를 새로 추측해서 채우는 체크포인트가 아니다.

---

# 146. VERIFIED_SOURCE_PROVENANCE — 공식 DCSS release 기준은 `0.34.1`

공식 Crawl GitHub release에서:

```text
release tag: 0.34.1
release short commit: 1eebc1a
release date: 2026-03-15
```

를 확인했다.

이 프로젝트의 실제 구현 검증은 계속:

```text
Dungeon Crawl Stone Soup 0.34.1 stable release
```

만 기준으로 한다.

`master`, trunk, 이후 commit의 함수 body를
0.34.1 body로 자동 승격하지 않는다.

상태:

```text
VERIFIED_SOURCE_PROVENANCE
```

---

# 147. VERIFIED_SOURCE_PROVENANCE — 공식 GitHub source archive와 Debian orig tar의 SHA-256 동일

공식 GitHub 0.34.1 release asset:

```text
stone_soup-0.34.1-nodeps.tar.xz
SHA-256:
360b5ac25913d20cd3df6c98a4a41280f3ba07e8742281a7b22341017802f3a2
```

Debian의 0.34.1 source package가 사용하는 upstream orig tar:

```text
crawl_0.34.1.orig.tar.xz
SHA-256:
360b5ac25913d20cd3df6c98a4a41280f3ba07e8742281a7b22341017802f3a2
```

두 SHA-256이 완전히 동일하다.

## 의미

Debian이 사용하는 0.34.1 upstream orig source archive는
공식 GitHub release의 nodeps 0.34.1 source archive와
동일한 archive identity를 가진다.

따라서 버전이 정확히 고정된 Debian Sources의 0.34.1 upstream source body는
공식 release 대조용 mirror로 사용할 수 있다.

상태:

```text
VERIFIED_SOURCE_PROVENANCE
```

---

# 148. VERIFIED_SOURCE_PROVENANCE — Debian `crawl 2:0.34.1-2`는 별도 source patch 없음

Debian Sources patch index에서:

```text
crawl 2:0.34.1-2
This package has no patches yet.
```

로 표시된다.

따라서 프로젝트에서 다음 경로의 source body를 직접 확보했을 경우:

```text
sources.debian.org/src/crawl/2%3A0.34.1-2/source/...
```

그 body를:

```text
"Debian이 수정한 별도 gameplay C++ body"
```

로 의심해 배제할 필요가 없다.

단, `debian/` packaging metadata 자체와
upstream `source/` tree는 구분한다.

상태:

```text
VERIFIED_SOURCE_PROVENANCE
```

---

# 149. PATCH/VERIFICATION RULE — Checkpoint11 이후 source evidence 등급

Checkpoint08의 source verification rule을 더 엄격하게 갱신한다.

## A. `VERIFIED_BODY` 허용

다음 중 하나의 **정확한 0.34.1 파일 body**를 직접 읽었을 때만 허용:

```text
1. official crawl/crawl tag 0.34.1 file
2. official 0.34.1 source archive의 실제 file
3. version-pinned Debian Sources:
   crawl 2:0.34.1-2 / upstream source tree
```

그리고 필요한 함수의 body 범위 자체가 실제로 보여야 한다.

## B. `VERIFIED_DECLARATION_ONLY`

0.34.1 exact file에서 declaration만 확인한 경우.

## C. `VERIFIED_CALLSITE_ONLY`

0.34.1 exact file의 call-site는 확인했지만
callee body는 못 본 경우.

## D. `SUPPORTING_CURRENT_MASTER`

현재 `master`에서 동일/유사 패턴을 발견했지만
0.34.1 exact file body를 확인하지 못한 경우.

이 상태는:

```text
VERIFIED_*
```

로 승격할 수 없다.

## E. `EXCLUDED_VERSION_MISMATCH`

0.26.1 / 0.23 등 과거 Debian source나
0.34.1이 아닌 버전의 함수 body.

구조 참고는 가능하지만 구현 명세 authority로 사용 금지.

---

# 150. CORRECTED — current `master` call-site를 0.34.1 증거처럼 읽지 않음

Checkpoint10 작업 중 검색 결과에는
current GitHub `master`의 일부 call-site가 함께 잡혔다.

예를 들어:

```text
create_monster() 성공 뒤 returned shell의 props 수정
```

패턴 자체는 현재 Crawl 코드에서 존재하는 유용한 supporting evidence다.

그러나 Checkpoint11 이후에는
0.34.1 exact call-site가 직접 확보되기 전까지:

```text
SUPPORTING_CURRENT_MASTER
```

로만 취급한다.

따라서 Checkpoint10의
“post-create returned shell props mutation은 원본에서 사용되는 패턴”
문장은 다음처럼 좁혀 읽는다.

```text
현재 Crawl 계열에서 존재하는 패턴:
    SUPPORTING_CURRENT_MASTER

0.34.1 exact call-site에서 동일한지:
    SOURCE_CHECK_REQUIRED
```

### 변하지 않는 결론

이 교정은 다음을 뒤집지 않는다.

```text
merc_id exact attach point = SOURCE_CHECK_REQUIRED
```

즉 여전히 `create_monster()` exact 0.34.1 body를 확인하기 전에는
attach 위치를 확정하지 않는다.

---

# 151. VERIFIED_DECLARATION_ONLY — 0.34.1 Debian Sources의 `monster.h` 재확인

버전 고정 Debian Sources:

```text
crawl 2:0.34.1-2/source/monster.h
```

에서 실제 0.34.1 `monster` declaration에 다음이 존재함을 다시 확인했다.

```cpp
void set_new_monster_id();

random_var attack_delay(const item_def *projectile = nullptr) const override;
random_var melee_attack_delay() const override;

int skill(skill_type skill, int scale = 1,
          bool real = false, bool temp = true) const override;
```

또 shell runtime state에:

```text
hit_points
max_hit_points
speed
speed_increment
```

등이 존재하는 기존 구조도 이 exact-version header와 일치한다.

상태:

```text
VERIFIED_DECLARATION_ONLY
```

이 항목은 기존 Checkpoint의 관련 선언 확인을
새 source-provenance 규칙으로 다시 지지한다.

---

# 152. SOURCE_CHECK_REQUIRED — 아직 승격하지 않은 핵심 target body

source provenance가 고정됐다고 해서
다음 함수/파일 body가 자동으로 검증된 것은 아니다.

현재 그대로 남긴다.

```text
mgen-data.h
    exact struct field list
    exact constructor signatures/defaults
    exact mutator bodies where relevant

mon-place.h
    create_monster exact declaration

mon-place.cc
    create_monster full body
    get_free_monster call position
    set_new_monster_id call position
    grid placement
    MID cache registration
    equipment creation
    behaviour / foe setup
    visibility / event side effects

player-act.cc or actual defining file
    player::attack_delay() full 0.34.1 body
```

상태:

```text
SOURCE_CHECK_REQUIRED
```

이번 체크포인트는 이 빈칸을 추론으로 채우지 않는다.

---

# 153. 소스 획득 실패를 구현 사실로 오인하지 않음

현재 실행환경에서
공식 release archive를 로컬 컨테이너에 직접 내려받아 풀어
전체 tree grep을 완료했다고 주장하지 않는다.

확인된 것은:

```text
official release identity
official source archive hash
Debian orig tar hash identity
Debian 0.34.1-2 no-patch status
version-pinned Debian Sources 개별 file 접근 가능성
```

이다.

따라서 이후 body 확보는:

```text
version-pinned exact file을 실제로 열어서 확인
```

하는 방식으로 계속 진행한다.

---

# 154. Checkpoint11 상태표

| 항목 | 상태 |
|---|---|
| official release tag `0.34.1` | VERIFIED_SOURCE_PROVENANCE |
| official release short commit `1eebc1a` | VERIFIED_SOURCE_PROVENANCE |
| GitHub 0.34.1 nodeps tar SHA-256 | VERIFIED_SOURCE_PROVENANCE |
| Debian 0.34.1 orig tar SHA-256 동일 | VERIFIED_SOURCE_PROVENANCE |
| Debian `2:0.34.1-2` no source patch | VERIFIED_SOURCE_PROVENANCE |
| version-pinned Debian Sources를 0.34.1 body authority로 사용 | VERIFICATION_RULE |
| current master body를 0.34.1 VERIFIED로 사용 | FORBIDDEN |
| old 0.26/0.23 body를 0.34.1 VERIFIED로 사용 | FORBIDDEN |
| exact 0.34.1 `monster.h` core declarations | VERIFIED_DECLARATION_ONLY |
| `mgen-data.h` exact body | SOURCE_CHECK_REQUIRED |
| `mon-place.h` create declaration | SOURCE_CHECK_REQUIRED |
| `mon-place.cc` create full body | SOURCE_CHECK_REQUIRED |
| merc_id exact attach point | SOURCE_CHECK_REQUIRED |
| `player::attack_delay()` exact body | SOURCE_CHECK_REQUIRED |

---

# 155. 다음 조사 위치

source authority가 고정됐으므로 다음에는
검색 결과의 버전 혼입을 허용하지 않고 다음 순서로 간다.

1. `crawl 2:0.34.1-2/source/mon-place.h`
   - `create_monster()` exact declaration
2. `crawl 2:0.34.1-2/source/mgen-data.h`
   - exact fields / constructors
3. `crawl 2:0.34.1-2/source/mon-place.cc`
   - `create_monster()` full body
4. 같은 exact body에서:
   - `get_free_monster()`
   - `set_new_monster_id()`
   - grid placement
   - MID/cache
   - equipment
   - behaviour/event 순서
5. 위 확인 후:
   - mercenary `merc_id` exact attach point 최종 결정
6. exact 0.34.1 `player::attack_delay()` definition
7. shell HP / follower / transit / level lifecycle
8. item marshalling / Record actual equipment save
9. build system source-file registration

---

# Checkpoint 11 결론

이번 체크포인트의 핵심 진전은
새 gameplay 함수를 추측해서 추가한 것이 아니라
**이후 VERIFIED를 판정할 source authority 자체를 고정한 것**이다.

앞으로:

```text
official 0.34.1
        또는
version-pinned Debian 2:0.34.1-2 upstream source
            ↓
실제 target body 직접 확인
            ↓
VERIFIED_BODY
```

순서만 허용한다.

`master`나 과거 version에서 비슷한 함수가 검색됐다는 이유로
0.34.1 implementation body를 채우지 않는다.

따라서 현재 `create_monster()` 내부 lifecycle,
merc_id attach exact point,
player attack delay 공식은
여전히 미확정이며,
다음 checkpoint에서 정확한 0.34.1 target file을 직접 확보한 뒤 계속한다.


---

# Checkpoint 12 — 로컬 DCSS 0.34.1 기반 첫 실제 코드 패치

작성일: 2026-09-13

## 156. 현재 결과와 범위

이 체크포인트부터 실제 C++ 수정 파일과 재적용 가능한 패치가 존재한다.
아직 플레이 가능한 무협돌죽 완성본은 아니다. P0 로스터 기반과 무기 딜레이의 순수 계산층만 구현했다.
기존 §2916 설계 전체의 검토·구현 완료를 뜻하지 않는다.
아래 최신 상태가 앞선 체크포인트의 상태 설명보다 우선한다. 기존 설계 및 누적 검토 내용은 삭제하지 않았다.

| 구분 | 상태 |
|---|---|
| MercenaryRecord / stable ID / roster / skill query | 코드 반영, 집중 테스트 통과 |
| shell marker / invalid ID / missing record 구분 API | 코드 반영, 테스트 소스 컴파일; 실행 미검증 |
| weapon skill + brand delay 공통 함수 | player 호출 연결, 원본 확률 분포 비교 통과 |
| live mercenary spawn / one-active-shell | 미구현 |
| roster 전역/월드 소유자와 세대 수명 연결 | 미구현 |
| roster/equipment save/load | 미구현 |
| 용병 평타 명중/피해, 방어, 장비 ownership | 미구현 |
| Town / 100F Tower / 재생형 던전 / 세대 계승 / 무공 / UI | 미구현 |
| 원본 전체 빌드 및 플레이 회귀 | 환경 의존성 문제로 완료 못 함 |

## 157. 실제 로컬 소스 authority

입력은 사용자가 업로드한 `stone_soup-0.34.1.tar.xz`이다.
SHA-256: `473b9cdc16be0b537ac11e43c6c77db4b290000e4a17f72a842eba59c6b7be2a`
압축 내부 `source/util/release_ver`는 `0.34.1`이다.
이번 턴에서 원격 공식 release hash를 다시 조회한 것은 아니다. 업로드 바이트와 내부 버전을 고정했다.
이 파일은 의존성이 포함된 tar이며, Checkpoint11의 nodeps tar hash와 다르다.

실제 로컬 경로는 `stone_soup-0.34.1/source/...`이다. 기존 문서의 `crawl-ref/source/...`는 저장소 배치에 해당한다.
빌드 후 기존 엔진 .cc/.h/Makefile.obj 798개를 업로드 원본과 바이트 비교했다.
기존 파일 중 변경된 것은 `source/player-act.cc`, `source/Makefile.obj` 두 개다.
일반 monster/attack/melee-attack/tags/enum 원본 파일은 수정하지 않았다. 이는 실행 회귀 테스트 통과를 의미하지 않는다.

## 158. VERIFIED_BODY / DECLARATION — 로컬에서 추가 확인

| 파일 / 함수 | 확인 범위와 의미 |
|---|---|
| `source/mon-place.h:36` / `create_monster` | declaration: mgen_data 값 전달, fail_msg 기본 true |
| `source/mgen-data.h` / mgen_data | fields와 기본 constructor, props 보유 확인 |
| `source/mon-place.cc:3163` / create_monster | wrapper 전체 확인. 위치 선정 후 mons_place 호출, 실패 시 null 반환 |
| `source/mon-place.cc:2909` / mons_place | wrapper 전체 확인. place_monster 호출 |
| `source/monster.cc:4505` / set_new_monster_id | you.last_mid 증가 및 env.mid_cache 연결 확인 |
| `source/player-act.cc` / attack_delay, melee_attack_delay, _player_attack_delay, attack_delay_with | 해당 함수 본문 확인. 실제 무기 숙련/brand 수식은 attack_delay_with에 있음 |
| `source/tags.cc:1309,1510` / TAG_YOU cases | companions 뒤 append 방향과 기존 read 후처리를 재확인. serializer 패치 없음 |
| `source/Makefile.obj` | OBJECTS 및 TEST_OBJECTS에 실제 파일 등록 |

`mgen_data.props`가 있다고 해서 임의의 모든 property가 shell에 자동 복사되는 것은 아니다.
확인한 생성 코드에서는 특정 key별 복사가 존재하며, 용병 key 전달은 아직 구현되지 않았다.
`_place_monster_aux` 전체 부수효과·장비 생성·behaviour/event를 끝까지 검증한 것은 아니다.
따라서 **merc_id 부착 시점은 여전히 SOURCE_CHECK_REQUIRED**다. 생성 직후 부착을 확정하지 않는다.

## 159. CODE_APPLIED — P0 로스터 기반

새 파일 `source/mercenary.h/.cc`:

- MercenaryRecord: immutable positive int32 ID, name/species/background, xl/xp, base STR/INT/DEX, state, 실제 0~27 skill.
- Record 복사/대입 금지. 장비/무공/특성/체질 필드는 아직 없음.
- 명세의 안정적 소유 컨테이너 원칙을 `map<merc_id_t, unique_ptr<MercenaryRecord>>`로 구현.
  vector index를 ID로 쓰지 않으며, 고용 증가 시 Record 주소가 유지되고 키 순서는 결정적이다.
- 1부터 증가하는 ID; 0/음수는 유효 ID 아님. ID 범위 소진 시 예외. 신규 할당 실패 전에 counter를 증가시키지 않음.
- 로스터 고정 인원 cap 없음. 해고/clear API는 장비 ownership·월드 lifecycle 구현 전까지 제공하지 않음.
- skill은 Record 자체의 실제 값이다. XL/HD에서 계산하지 않는다. 잘못된 값 설정은 거부하며 query scale overflow도 검사한다.
- shell props key 존재 여부와 연결 유효성은 구분한다.
  NOT_MERCENARY / INVALID_ID / MISSING_RECORD / LINKED 네 상태를 반환한다.
- lookup에 roster를 명시적으로 전달한다. 월드 소유 roster는 아직 연결하지 않았다.
- 게임에서 marker를 부착하거나 용병을 생성하는 경로는 없다. 따라서 이 API 추가만으로 실제 전투의 fail-closed guard가 완성된 것은 아니다.
- ALIVE/DOWNED/CARRIED/DEAD는 기반 enum이다. 다운/구조/부활/사망 전이 동작은 미구현이다.

## 160. CODE_APPLIED — 무기 숙련/brand 딜레이의 순수 계산층

새 파일 `source/attack-delay.h/.cc`:

`weapon_skill_delay(base_delay, skill_tenths, min_delay_skill_tenths, brand)`

원본 순서를 보존한다:

1. 무기 최소 딜레이 도달 숙련도로 skill 기여를 제한.
2. skill / 20의 random_var 확률 반올림을 base delay에서 차감.
3. SPEED는 2/3, HEAVY는 3/2를 확률 반올림으로 적용.

이 함수는 `you`, monster HD, 전역 장비를 읽지 않는다. 확률 변수를 반환하며 RNG를 roll하지 않는다.
`player::attack_delay_with()`의 기존 무기 분기를 이 함수에 연결했다.
Woodcutter 고정 딜레이 early return, 투척, 맨손, 최소값 clamp, shield/armour penalty,
Finesse/Haste/player_speed, offhand 조합은 기존 player 경로에 남았다.
이 helper 하나를 용병의 완성 attack_delay로 부르면 안 된다. 용병 장비 조회, 실제 숙련,
고정 딜레이 특수 무기, 방어구/방패 penalty, 상태 및 monster energy 처리가 추가로 필요하다.

## 161. 검증 결과와 한계

- 원본 빌드: `make -C stone_soup-0.34.1/source -j4` 실행.
  `libunix.cc:30:10: fatal error: term.h: No such file or directory`에서 중단.
- libncurses-dev 설치 시도: 패키지 목록 없음. apt update는 환경의 setgroups/setegid/seteuid 권한 오류로 실패.
  권한 우회나 가짜 터미널 헤더/함수 대체는 하지 않았다.
- 변경 translation units: native Makefile의 ASSERTS 포함 컴파일 규칙으로 mercenary.o, attack-delay.o, player-act.o 성공.
- 추가 shell 테스트 소스: C++14 / ASSERTS로 컴파일 성공. 링크 및 실행 미검증.
- 집중 C++ 테스트: 4 cases / 373,737 assertions 통과.
  95,121개의 base speed / skill / minimum-skill / brand 조합에서 원본 수식의 확률 가중치와 비교.
  소수 숙련 반올림 및 SPEED cap의 명시적 기대값도 확인.
  로스터 1,025명으로 증가 후 주소/ID 보존, 없는 ID 조회의 비삽입,
  DEAD 상태 변경 후 Record/skill 보존, skill 범위 및 overflow 거부를 확인.
- 집중 runner는 실제 production .cc를 컴파일하며, 사용하지 않는 shell adapter/roll 함수는 linker GC로 제외한다.
  엔진 ASSERTS는 집중 runner에 켜지지 않으며 Catch 검사는 활성화된다.
  따라서 전체 게임, RNG sequence, shell reset 동작, save/load, UI 검증을 대체하지 않는다.
- 전체 make catch2-tests, 실제 player/ordinary monster 전투, save/load, Windows/tiles 실행은 미검증.

재실행:

```sh
cd stone_soup-0.34.1/source
bash util/test-muhyeop-core.sh
# 터미널 개발 의존성이 준비된 환경에서 별도로:
make -j4
make catch2-tests
```

## 162. 다음 복구 지점

1. libncurses 개발 헤더/라이브러리가 있는 환경에서 원본 및 수정본 전체 빌드와 기존 Catch2 테스트를 확보.
2. create_monster → mons_place → place_monster → _place_monster_aux의 전체 lifecycle을 추적.
   MID/grid/starting gear/behaviour/event 순서를 확인하고 merc_id attach 위치를 결정.
3. P1에서 one-active-shell, spawn 실패 rollback, despawn/respawn, world-owned roster 수명을 함께 구현.
   roster 저장 없이 일반 플레이용 spawn을 노출하지 않는다.
4. Record actual item_def ownership 및 TAG_YOU append serializer + tag minor + old-save fallback을 검증/구현.
5. Record XL/skill/equipment accessor, 방어/저항 분기, 평타 명중/피해/energy를 각 작은 패치로 연결.
6. final stats/UI, 무공, Town/Tower, 세대 계승 및 재생형 던전은 기존 상세 설계를 다시 대조하며 진행.

# Checkpoint 12 결론

문서만 있는 상태에서 벗어나 실제 코드 패치와 실행된 집중 테스트가 생겼다.
다만 P0의 전체 게임 회귀 gate는 미통과이며, P1 이후 및 완성 게임은 아직 구현되지 않았다.


---

# Checkpoint 13 — 용병 핵심 Record 저장/복원 연결

작성일: 2026-09-13. 기준 GitHub main: da1dfdf15aa048141647ca8c1f30fd62256289a9.
이전 Checkpoint12 내용 전체를 보존하며, 최신 상태는 이 절을 우선한다.

## 이전 빌드 기록 정정

기존 로컬 term.h 문제는 로컬 환경에 남아 있지만 GitHub의 Ubuntu runner에서는 해결됐다.
https://github.com/dhsmfqnxj/enzify/actions/runs/34756925309
해당 실행의 linux-console job 103722778556에서 dependency 설치, 전체 게임 build,
전체 Catch2 suite가 모두 success임을 GitHub API로 직접 확인했다.
이제 플러그인 계정 및 dhsmfqnxj/enzify read/write 접근도 확인했다.

## 실제 반영한 코드

- source/mercenary-save.cc: roster schema MHR1 (0x4d485231), 원본 writer/reader와 marshalling 사용.
- source/mercenary.h/.cc: world-owned function-static roster accessor, 새 월드용 명시적 reset,
  save/load API, ALIVE/DOWNED/CARRIED/DEAD 저장 번호 0/1/2/3 고정.
- source/ng-setup.cc: setup_game 진입 시 새 월드 roster 초기화.
  startup.cc의 restore 성공 경로는 setup_game을 호출하지 않음을 원본에서 확인.
  미래의 세대 전환은 setup_game/reset_mercenaries_for_new_game을 재사용하면 안 된다.
- source/tag-version.h: 기존 minor 뒤, NUM_TAG_MINORS 앞에 TAG_MINOR_MUHYEOP_ROSTER append.
- source/tags.cc: TAG_YOU companions 뒤 CANARY + roster 저장.
  read에서는 새 minor 이상만 canary를 소비하고 roster를 읽음.
  이전 minor에서는 roster를 비우되 새로운 바이트를 읽지 않음.
- source/Makefile.obj: mercenary-save.o 및 test_mercenary_save.o 등록.

## 저장되는 항목

id, name, species, background, xl, xp, base STR/INT/DEX, 실제 skill 값, roster state,
next_id, 저장 당시 skill 개수. Record id 순서로 결정적으로 기록.
장비 item_def, 무공, 특성/체질, HP, active deployment 정보는 아직 Record 필드와 기능이 없어 저장하지 않는다.
따라서 이 변경을 용병 전체 세이브 구현 완료로 표현하면 안 된다.

## 데이터 검증

- schema, next_id 범위, count, skill count, 양수/오름차순/중복 ID, 다음 ID와의 순서를 검사.
- 원본 이름 문자열의 signed-short 길이 형식은 유지하되 음수 길이는 release에서도 예외 처리.
- species/job enum 범위, 양수 XL, 비음수 XP와 기본 스탯, 0~3 state, 0~27 skill 검사.
- 임의의 작은 roster 인원 제한이나 임의 XL/기본 스탯 상한은 추가하지 않음.
  저장 형식의 정수 표현 한계만 검사. 실제 gameplay cap/HD 변환 규칙은 후속 단계.
- next_id가 가장 큰 현재 ID보다 커야 하므로 향후 삭제/해고 후에도 사용했던 ID를 재사용하지 않음.
- INT32_MAX+1은 ID 소진 sentinel. 저장/복원되며 신규 고용은 overflow 예외.
- read는 별도 pending roster에서 전부 읽고 검증한 뒤 swap.
  손상/잘림 실패 시 기존 Record 주소와 내용 유지. 성공적인 load는 객체를 교체하므로 이전 pointer를 보존하면 안 됨.
- save는 전체 roster를 검증한 뒤 첫 바이트를 기록.
- 정상 세대 전환에서의 객체 유지와 디스크 load에서의 동일 ID 복원은 서로 다른 수명 사건이다.

## 테스트

추가 Catch2 테스트: 필드/4상태 round-trip, next ID 유지, 유효하지 않은 행/중복 ID,
모든 잘린 prefix에서 기존 roster 보존, header/schema/ID소진 검증, 잘못된 writer 입력,
음수 count/name length, old minor의 비소비 fallback, 복원 후 shell-ID lookup,
실제 setup_game을 쓰는 기존 fixture를 통한 새 월드 reset.

로컬: 변경 translation units의 native ASSERTS 컴파일 성공, 신규 테스트 소스 컴파일 성공.
원격: 이 변경의 전체 빌드/Catch2 최종 결과는 해당 PR의 GitHub Actions check로 확인한다.
아직 전체 캐릭터 TAG_YOU save/load 실플레이 왕복, 용병 spawn, 이동/transit, 사망/drop은 검증하지 않았다.

## 범위와 다음 구현

이번 변경은 P0 핵심 Record를 잃지 않게 저장 기반을 먼저 연결하는 단계다.
P1 spawn은 아직 활성화하지 않는다. create_monster→mons_place→place_monster→_place_monster_aux의
전체 gear/behaviour/event 순서를 확인하고 merc_id 부착점 및 one-active-shell invariant를 확정해야 한다.
그다음 Record 실제 장비 ownership/item marshalling, XL mirror/실제 skill/accessor,
player-like 평타/방어/저항 분기를 차례로 구현한다.
Town/Tower/무공/세대 계승의 실제 플레이 기능은 아직 미구현이다.


## Checkpoint13 재개 검증 및 P1 생성 경로 검토 — 2026-09-14

검증 대상 코드: `87158f6d4303fec2daff71a448d91f4b03f85413`.
Actions run 34758331299 / job 103726535268: Linux console 전체 빌드와
전체 Catch2 성공. 실제 로그: `All tests passed (383522 assertions in 76 test cases)`.
https://github.com/dhsmfqnxj/enzify/actions/runs/34758331299
이는 위의 원격 검사 대기 기록을 대체한다. 실플레이 save/load 검증을 대신하지 않는다.

기존 압축 해제 작업 폴더를 수정하지 않고 별도 Git checkout을 복구했다.
비교 가능한 tracked 파일의 실질 내용 차이는 0개, CRLF/LF 차이는 483개다.
원격에만 있는 README.md와 workflow 2개, 별도 취급한 framework 경로 26개는
`recovery-comparison-2026-09-14.json`에 기록했다. 전체 파일 완전 일치라고 해석하지 않는다.

### 원본 함수별 연결 위치

다음 행 번호는 위 코드 SHA 기준이며, 아래는 조사/패치 설계이지 구현 완료가 아니다.

| 파일 / 함수 | 확인된 원본 동작 | P1 구현 조건 |
|---|---|---|
| mon-place.cc:3163 create_monster | 위치를 재탐색할 수 있고 mons_place를 호출 | 호출 전 Record 존재·ALIVE·배치 자격·중복 검사. 실제 반환 위치를 검증 |
| mon-place.cc:2909 mons_place | 랜덤 타입 및 BEH_COPY 처리 후 place_monster | 용병은 구체적인 shell 타입과 명시적 태도로 진입 |
| mon-place.cc:628 place_monster | 몬스터 수/점유 검사, 타입 결정, 본체 생성 후 band 처리 | random/band/unique 특수 생성 경로를 용병 일반 생성에 허용하지 않음 |
| mon-place.cc:832 get_free_monster | 빈 슬롯을 reset하고 반환 | 기존 shell/Record를 삭제해서 슬롯을 확보하지 않음 |
| mon-place.cc:899 _place_monster_aux | 위치 및 arena 검사 뒤 966에서 MID 등록, 1018에서 define_monster | Record 검증은 MID 등록보다 앞. ID 부착은 reset 이후이며 accessor 진입보다 앞이어야 함 |
| mon-place.cc:1204–1248 장비 분기 | give_item/give_weapon/wield_melee_weapon 실행 | 용병의 Record 장비 소유권 경로가 준비되기 전에는 플레이용 spawn을 노출하지 않음 |
| mon-place.cc:1307 move_to | 태도 설정 후 grid 배치. 실패 시 MID 제거 및 reset | 용병 rollback은 장비 소유권도 명시적으로 정리해야 함 |
| mon-place.cc:1551 behaviour_event | create_monster 반환 전에 ME_EVAL, 이후 autofoe/announcement | 성공 반환 후에만 ID를 붙이는 설계는 너무 늦음 |
| monster.cc:129 monster::reset | inv 인덱스 초기화, grid 제거, props 초기화 | MID cache 삭제와 실제 item 소유권 처리를 reset만으로 대체할 수 없음 |
| monster.cc:4755 destroy_inventory | 소유 아이템을 destroy_item으로 파괴 | Record 소유의 실제 장비를 공유한 채 호출하면 안 됨 |

### 연결 방식 결정과 아직 남은 검증

- create_monster 반환 후 ID 부착은 채택하지 않는다. 일반 생성 내부에 용병 전용 준비
  단계를 두고, 첫 용병 accessor/장비 처리 이전에 유효한 연결이 보이도록 해야 한다.
- `mgen_data.props`는 전체가 자동 복사되지 않는다. `MUHYEOP_MERC_ID_KEY`를
  전달하더라도 명시적으로 검증하고 부착하는 코드가 필요하다.
- 최초 후보는 `_place_monster_aux`의 위치 검증 이후부터 define_monster 전후 구간이다.
  define_monster 및 그 하위 함수의 accessor/초기화 부작용 검토가 남아 있어
  최종 삽입 행은 아직 확정하지 않는다. 이를 확정한 것처럼 live spawn을 추가하지 않는다.
- spawn 전후 중복 검사는 모두 필요하다. 현재 층 검색만으로 다른 층 저장 shell,
  transit, companions까지 포함한 전역 유일성을 보장할 수 없다.
- 실패 복구는 이번 시도에서 만든 shell만 대상으로 한다. 기존 shell/Record는 보존한다.
  아이템 소유권, MID cache, grid, props를 순서대로 정리하고 Record 상태는 바꾸지 않는다.
  monster_die를 취소 처리로 재사용하지 않는다.
- 실제 소환 노출 전 필수 검사: 중복 요청, 점유/부적합 위치, 슬롯 소진,
  생성 중 실패, 초기 행동 시 유효 ID, 저장 후 재배치, 타 층/transit 중복 방지.

다음 착수점: define_monster의 하위 호출을 추적해 ID 삽입점을 확정하고,
장비/HP/배치 수명과 함께 P1을 구현한다. 이번 재개에서는 runtime 소스를 변경하지 않았다.


## P1 준비 코드 — 용병 shell 중복 탐지

- source/mercenary.h/.cc: find_mercenary_shell(slots, count, id) 추가.
- 지정된 슬롯 범위에서 ABSENT/UNIQUE/DUPLICATE를 구분. 중복이면 포인터를 반환하지 않음.
- HP가 0이어도 할당된 shell은 계산한다. 빈 슬롯, 다른 ID, 문자열 ID는 일치로 계산하지 않는다.
- 읽기 전용이며 shell/Record를 변경하지 않는다. 잘못된 검색 인자는 invalid_argument.
- 현재 층/지정 범위 검사 기반 함수이며 transit 및 다른 층을 포함한 전역 중복 보장은 아직 아니다.
- 실제 spawn 호출부에는 아직 연결하지 않았다. 다음 단계는 전역 배치 수명 및 생성 전후 검사 연결.
- Catch2 회귀 테스트 2개 추가: DOWNED/중복 보존/reset 이후 검색, 잘못된 입력/무관 슬롯.
- 변경 production/test translation unit C++14+ASSERTS 컴파일 성공.
  전체 실행 검사는 이 코드 커밋의 Actions 결과를 확인할 것. 기존 76개 통과는 이전 코드 결과다.


## P1/P2 준비 — 실제 monster XL 조회 연결

- source/mon-act.cc의 monster::get_experience_level()에 용병 분기를 추가했다.
- marker가 없는 일반 monster는 원본 hit_dice 반환을 유지한다.
- 용병은 world roster에서 ID를 조회하고 Record.xl을 반환한다.
- 잘못된 marker/없는 Record/0 이하 XL은 logic_error이며 일반 monster fallback을 하지 않는다.
- get_hit_dice의 기존 임시 enchantment 보정은 이번 패치에서 변경하지 않았다.
  get_experience_level을 부르므로 그 기본값은 이제 Record XL이다.
- raw hit_dice 미러 동기화, 용병 전용 drain 정책, 실제 skill/장비/전투 공식 연결은 미완료.
- 신규 테스트 2개: 일반 monster 유지, Record XL 실시간 반영, shell HD 독립성,
  DOWNED 조회, marker 제거 시 원본 경로, 잘못된 ID/Record/XL 오류.
- mon-act.cc와 test_mercenary.cc의 C++14 ASSERTS 컴파일 성공.
- 전체 엔진 테스트 실행은 이 최신 커밋의 Actions에서 확인해야 한다.
- 실제 spawn이 노출되기 전, 오류가 gameplay 도중 발생하지 않도록 생성/불러오기
  경계에서 identity를 검증하고 실패 처리를 연결해야 한다. 현재 완성 플레이 기능이 아니다.


## 설계 원문 400절 대조 — 용병 get_hit_dice 보완

- design-2916.md 400절은 get_experience_level과 get_hit_dice 모두 Record XL 그대로 반환하도록 확정했다.
- mon-act.cc의 get_hit_dice에 용병 전용 조기 반환을 추가했다.
- 지난 패치의 “용병에도 기존 임시 enchantment HD 보정 적용”은 미완료 중간 상태였으며 이 절로 대체한다.
- 일반 monster는 기존 drained/wretched/tempered 보정 유지.
- 회귀 테스트: 각 보정의 일반 monster 예상값, 용병 XL 직접 조회, XL 변경,
  잘못된 ID 거부, marker 제거 후 원본 계산 복귀. 생산 소스 및 테스트 C++14 ASSERTS 컴파일 성공.
- 최신 전체 Actions 결과는 아직 미확인. 이전 실행 대기와 최신 결과를 혼동하지 않는다.
- raw HD mirror 동기화, 실제 배치/해제, 장비/HP 수명은 여전히 후속 작업이다.
