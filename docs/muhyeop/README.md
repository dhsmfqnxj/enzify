# 무협돌죽 개발 복구 안내

최신 변경: Checkpoint14 world roster 권한 및 실제 shell 생성 adapter.
먼저 [checkpoint14-placement.md](checkpoint14-placement.md)를 읽는다. 아래 내용은 이전 단계 기록이다.
먼저 spec-checkpoint13.md의 마지막 Checkpoint13과 recovery-checkpoint13.md를 읽는다.
설계 원문은 design-2916.md, 이전 체크포인트 파일도 그대로 보존한다.

GitHub main의 Checkpoint12 전체 compile/Catch2는 성공했다 (Actions run 34756925309).
Checkpoint13의 원격 검증 결과는 해당 PR의 Actions check를 따른다.

아직 플레이 가능한 무협돌죽 완성본이 아니다.
현재는 Record/ID/skill 기반, 공통 무기 딜레이, 핵심 roster 저장·복원까지다.
마을/탑/무공, 실제 용병 spawn/전투/장비/세대 전환은 후속 작업이다.

전체 빌드: source에서 make -j2
전체 테스트: source에서 make -j2 catch2-tests
집중 P0/딜레이 테스트: source에서 bash util/test-muhyeop-core.sh
