# 무협돌죽 작업복구 포인터 — Checkpoint13

기준: dhsmfqnxj/enzify, 기존 main da1dfdf15aa048141647ca8c1f30fd62256289a9.
개발 브랜치: codex/checkpoint13-roster-save.
최신 누적 명세: docs/muhyeop/spec-checkpoint13.md 마지막 Checkpoint13.
설계 원문: docs/muhyeop/design-2916.md. 이전 명세를 삭제/축약하지 않는다.

현재 코드:
- Checkpoint12 P0 및 공통 무기 딜레이 유지.
- core roster 필드와 next ID를 TAG_YOU 끝에 저장. 새로운 minor 추가.
- old minor: roster empty, 새 바이트 소비 없음.
- 잘못된 save의 read 실패 시 기존 roster 유지. writer는 선검증.
- 진짜 새 게임 setup_game에서만 초기화. 세대 전환에 이 reset을 호출하지 말 것.
- 장비/무공/HP와 live shell 배치 정보는 아직 미구현/미저장.

검증:
- 기존 main 전체 compile/Catch2 성공을 API로 확인: Actions run 34756925309.
- 이번 소스의 local ASSERTS compile 및 신규 tests compile 성공.
- 이번 PR의 Actions 최종 check를 확인하고 source SHA와 함께 기록한다.
- 전체 캐릭터 save/load 실플레이 왕복은 별도 후속 검증 대상.

다음:
1. 최신 PR의 성공/실패 로그 확인. 실패하면 먼저 수정.
2. create_monster 전체 lifecycle 조사 및 merc_id hook 확정.
3. P1 one-active-shell, spawn rollback, despawn/respawn 및 saved deployment 처리.
4. 실제 장비 ownership+serializer, XL/skill accessor, P2/P3 전투 연결.
원본 player/ordinary monster 경로를 섞지 않는다. you swap 금지.


## 2026-09-14 복구 완료 기록 (위 대기 상태를 대체)

- 원격 PR #1은 open/draft, main 미병합. 코드 SHA: 87158f6d4303fec2daff71a448d91f4b03f85413.
- Actions 34758331299 성공. 전체 빌드 및 Catch2 76 cases / 383522 assertions 성공.
- 복구 Git checkout: enzify-resume, branch codex/checkpoint13-roster-save.
- 복구 직후 git status clean. 기존 stone_soup-0.34.1 폴더는 그대로 보존.
- 비교 내역: recovery-comparison-2026-09-14.json. 일반 파일 내용은 줄바꿈 제외 일치.
- 마지막 완료: Checkpoint13 원격 검사 확정 + P1 생성 순서/rollback 원본 함수 조사.
- 최신 조사: spec-checkpoint13.md 마지막 “Checkpoint13 재개 검증 및 P1 생성 경로 검토”.
- 다음: define_monster 하위 호출의 accessor/초기화 확인 → ID 삽입 위치 확정.
  그 뒤 실제 장비/HP/배치 수명을 포함한 P1 구현. 아직 live spawn 구현 완료가 아니다.
- 기존 수정본에 패치를 중복 적용하거나 압축 해제본으로 checkout을 덮어쓰지 않는다.


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
