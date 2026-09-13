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
