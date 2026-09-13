# 무협돌죽 실제구현명세 작업복구 포인터 — Checkpoint12

최종 갱신: 2026-09-13

최신 누적 문서: `무협돌죽_실제구현명세_Checkpoint12.md`
입력 상세 설계: 기존 §2916 인계본. 버리지 않고 소스 패키지 docs/muhyeop/design-2916.md에 함께 보존.
기반 소스: 업로드된 stone_soup-0.34.1.tar.xz
입력 SHA-256: 473b9cdc16be0b537ac11e43c6c77db4b290000e4a17f72a842eba59c6b7be2a

## 현재 상태

- 실제 코드 존재: P0 Record/ID/roster/skill 및 shell lookup API.
- player 무기 숙련/brand 딜레이 계산을 순수 helper로 분리해 연결.
- 집중 테스트 4 cases / 373737 assertions 통과. 95121 delay 조합 원본 확률 가중치 일치.
- shell tests는 컴파일만 성공. 실제 게임에서 용병 spawn 없음.
- 전체 게임 빌드는 원본 libunix.cc의 term.h 누락으로 중단. apt 설치도 환경 권한 오류.
- Town/Tower, 용병 실제 전투/저장/장비, 세대 계승, 무공은 아직 구현되지 않음.

## 복구 순서

1. Checkpoint12 소스 tar.xz를 풀고 docs/muhyeop/README.md와 누적 명세의 마지막 Checkpoint12를 읽는다.
2. source-manifest-checkpoint12.json으로 원본/수정본 해시를 확인한다.
3. source에서 `bash util/test-muhyeop-core.sh`로 집중 테스트 재실행 가능.
4. libncurses 개발 의존성이 있는 환경에서 전체 make / make catch2-tests / 원본 회귀를 먼저 확보한다.
5. create_monster → mons_place → place_monster → _place_monster_aux를 끝까지 추적하여 merc_id 부착점을 확정한다.
6. P1 one-active-shell 및 spawn/despawn/rollback을 구현하되 save 없는 일반 플레이용 spawn은 노출하지 않는다.
7. Record 장비 ownership, TAG_YOU roster 저장, XL/HD 연결 및 P2/P3 전투로 진행한다.

## 금지 및 유지할 원칙

- 문서상 결정 / 소스 검증 / 코드 반영 / 테스트 통과를 혼동하지 않는다.
- current master와 다른 버전의 본문을 0.34.1 증거로 승격하지 않는다.
- marker가 깨졌을 때 ordinary monster fallback 금지. 현재 구현은 구분 API이며 실행 guard는 후속 과제.
- you swap 금지. Record가 실제 XL/skill/equipment authority. shell HD는 후속 구현에서 XL mirror.
- 세대 전환으로 로스터와 DEAD Record를 재생성/삭제하지 않는다.
- 검증과 변경을 작은 단위로 저장하고, 누적 명세를 축약본으로 덮어쓰지 않는다.
