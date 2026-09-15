# Checkpoint14 — world roster 권한 및 실제 용병 shell 생성

기준 부모: f24c47e675004f0fbaa218fac821ef9b5822fd36, PR #1, 원격 브랜치 codex/checkpoint13-roster-save.
이 문서는 이전 누적 명세를 보존하면서 bind/생성 순서에 관한 마지막 중간 상태를 대체한다.

## 복구 및 보존

기존 enzify-bind-fix 워크트리의 미커밋 변경에서 이어서 수정했다. 로컬 브랜치는 codex/roster-authority-fix이며 PR 작업 브랜치를 추적한다. 기존 enzify-resume 및 원본 압축 해제 폴더는 변경하지 않았다. Android JNI 링크의 기존 로컬 삭제 상태는 이번 변경에 포함하지 않는다. reset/재클론/기존 커밋 재작성 없이 새 커밋을 PR 브랜치에 저장한다.

기준 GitHub Actions run 34794282147: f24c47e6의 Linux 전체 빌드 및 Catch2 82 cases / 383593 assertions 성공. 이 기준 검사를 다시 실행하지 않았다.

## 구현 경계

- bind_mercenary_shell의 roster 인자를 제거했다. 게임용 bind와 두 레벨 getter의 단일 원본은 mercenary_roster()다. 별도 roster의 ID가 world ID와 충돌해도 world Record만 사용한다.
- create_mercenary_shell(id, pos, current_hp, max_hp)는 원본 create_monster → mons_place → place_monster → _place_monster_aux → define_monster 흐름을 재사용한다.
- 전용 mgen context는 factory만 설정할 수 있다. ID는 define_monster 이전에 연결한다. 일반 생성은 context=0으로 기존 경로를 유지한다.
- 최초 adapter는 SP_HUMAN/ALIVE, 명시적 HP, 고정 좌표의 friendly shell이다. 원본 시작 장비/밴드/신 효과/초기 AI 평가를 배제한다. 장비 소유권을 Record로 구현한 것은 아직 아니다.
- 검증 실패는 null을 반환한다. native 생성 실패 및 예외에서는 신규 슬롯, MID cache, last_mid, max_mon_index, 현재 RNG를 복구한다. Record.deployed는 생성 후 검증까지 성공해야 true가 된다. 일반 monster/기존 용병을 초기화하지 않는다. 예외는 복구 후 다시 전달한다.
- remove_mercenary_shell은 현재 층의 유일한 shell이며 off-level 복사/인벤토리/구속이 없을 때만 제어된 제거를 허용한다. 죽음/XP/drop/Record 삭제를 호출하지 않는다. 호출자가 HP/status snapshot을 먼저 보관해야 한다.
- clone_mons의 공통 진입점에서 용병을 슬롯/RNG 변경 전에 거부한다. 일반 clone 및 레벨 계산은 기존대로다. illusion/clone 대상 판정도 용병을 제외한다.

## 전역 배치 검증 경계

현재 슬롯 범위 검사만으로는 충분하지 않다. 생성 factory는 다음을 함께 검사한다.

1. world Record의 영속 deployed 예약: 불러오지 않은 다른 층의 shell도 예약을 유지한다.
2. 현재 env.mons의 할당 슬롯: HP 0 shell도 포함한다.
3. the_lost_ones, companion_list, apostles의 shell 복사: 같은 ID 또는 손상된 marker이면 거부한다.

MHR2는 deployed를 저장하며 boolean wire 값도 검증한다. MHR1 데이터는 기존 스키마로 읽고 deployed=false로 이행한다. f24 이전에는 지원되는 실제 생성 경로가 없었다. 임의로 marker를 삽입한 구형/손상된 저장 파일의 모든 층을 자동 검색해 복구하는 기능은 없다. 새 factory 밖에서 직접 bind/marker 삽입으로 배치하는 것은 지원되는 배치 API가 아니다.

deployed는 보수적인 예약이다. 층 이동/귀환/사망에서 자동으로 예약을 해제하거나 transit 복사본을 재조정하지 않는다. 이 후속 상태 전이를 구현할 때에는 Record와 현재 층, 저장된 층, transit/companion의 identity를 함께 검증해야 한다. 기존 예약이 의심된다고 false로 강제 수정해서 재생성하지 않는다.

## raw HD 감사와 복원 순서

monster::hit_dice는 private이다. get_experience_level/get_hit_dice는 용병이면 world Record.XL을 반환하고 일반 monster는 원본 HD/enchantment 계산을 유지한다. setter와 ghost/uglything 초기화에서 용병의 raw 값은 Record XL로 동기화한다. Oklob의 raw HD 기반 주문 빈도는 setter 내부에 있으며 human adapter로 도달하지 않는다.

monster::init_with의 raw 필드 복사는 그대로 유지해야 한다. TAG_YOU는 transit/companion을 roster보다 먼저 역직렬화하고 복사한다. 이 시점의 단순 데이터 복사에서 world Record 조회를 강제하면 정상 save load가 실패한다. 이 필드는 복사용 cache이며 용병 gameplay 레벨 원본이 아니다. marker를 유지한 객체의 레벨 조회는 roster 복원 후 Record를 사용한다. 몬스터 wire HD 저장 역시 getter를 사용한다.

클래스 기본 HD, HP/AC/EV/저항/공격 등 원본 human 통계를 모두 무협 최종 통계로 대체한 것은 아니다. 이후 장비/무공/전투 연결에 이 factory를 사용할 때 전용 계산층을 연결해야 한다.

## 회귀 검증

- marker 유지 bind → XL → HD, foreign roster만 있는 ID 거부 및 world ID 충돌 처리.
- 실제 생성, HP/grid/MID/친화 상태, 시작 장비 없음, 중복 거부, 제거/재생성.
- native 최종 슬롯 예약으로 인한 생성 실패: 기존 용병/Record/grid/cache/MID/RNG 보존.
- 잘못된 ID/HP/좌표/상태, 다른 층 영속 예약 저장 왕복, transit 및 companion 거부.
- 용병 clone/illusion 대상 거부, 일반 rat 생성/clone/HD 유지.
- shell 역직렬화 및 복사를 roster보다 먼저 수행하고, roster 복원 뒤 동일 XL/HD 조회.
- MHR1 이행, MHR2 잘못된 deployed 값 거부 및 기존 save 회귀.

테스트 fixture는 원본 init_show_table을 호출해 지형 정의를 초기화한다. 벽으로 둘러싸인 바닥은 원본에서 유효한 생성 위치이므로 실패 테스트로 사용하지 않는다.

## 남은 작업

실제 플레이 명령/모집 UI, 용병 장비 및 무공 저장, HP/status 원본과 snapshot, 층 이동/귀환/사망의 예약 전이, 구형 손상 배치 복구, 용병 전투 최종 통계와 레벨업 API를 구현해야 한다. 아직 플레이 가능한 전체 무협돌죽 완성본이 아니다. 다음 우선 작업은 층 이동 및 귀환의 예약·snapshot 수명주기를 확정하고 실게임 저장 왕복으로 검증하는 것이다.

## 최종 로컬 검증 결과

- 최종 production 코드의 Linux console 전체 C++11 빌드 성공(exit 0), crawl 링크 완료. 빌드 중 변경된 헤더 의존성까지 후속 make로 반영하고 다시 링크 성공.
- 관련 Catch2: 28 cases / 377935 assertions, 모두 성공.
- 전체 make catch2-tests: 89 cases / 387199 assertions, 모두 성공(exit 0).
- 전체 Catch2 재컴파일 중 빈 pcg.o/l-dgnlvl.o/domino.o 때문에 발생한 링크 오류는 해당 생성 객체만 다시 컴파일해 해결했다. 소스를 우회 수정하지 않았다.
- 신규 역직렬화 테스트에는 실제 저장 파일처럼 TAG_MINOR_VERSION을 명시했다. 지형 초기화 및 버전 누락을 수정한 후 위 전체 검사를 실행했다.
- git diff --check 성공. 새 커밋은 PR #1에 추가하며 main에는 병합하지 않는다. 새 커밋의 원격 Actions는 PR checks에서 별도로 확인한다.

로컬 재현: source에서 make -j2, 이후 TERM=xterm make -j2 catch2-tests. 이 작업 환경은 사용자 경로에 추출한 libncurses-dev 헤더와 설치된 libncursesw.so.6/libtinfo.so.6를 EXTERNAL_FLAGS/NC_LIBS로 지정했다. GitHub workflow는 기존 Ubuntu libncurses-dev 설치 단계를 그대로 사용한다.
