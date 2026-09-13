# 무협돌죽 Checkpoint12 — 실제 소스 개발 시작본

이 패키지는 완성 게임/Windows 실행 파일이 아니다. DCSS 0.34.1 소스 전체(의존성 포함)에 첫 코드 패치를 적용한 개발 소스다.

현재 구현: 용병 Record/ID/roster/skill 기반, 잘못된 shell 연결 구분 API, player 무기 딜레이의 공통 계산층.
아직 구현하지 않은 것: 플레이용 용병 출현, 장비/저장/전투 연결, 마을/100층 탑, 무공, 세대 계승, UI.

## 사용

- 전체 소스 tar.xz는 원본과 별도 폴더에 압축 해제한다. Linux에서는 `tar --no-same-owner -xf <패키지.tar.xz>`를 사용하면 소유권 보존 오류를 피할 수 있다.
- 원본 소스를 이미 갖고 있다면 작은 패치 ZIP의 `muhyeop-checkpoint12.patch`를 원본 stone_soup-0.34.1 디렉터리에서 `patch -p1 < <패치경로>`로 적용할 수 있다.
- 패치와 전체 소스 패키지는 같은 변경분이다. 이미 수정된 전체 소스에 패치를 중복 적용하지 않는다.
- 집중 검사: source 디렉터리에서 `bash util/test-muhyeop-core.sh`.
- 전체 빌드 안내는 원본 `INSTALL.md`를 따른다. Linux console은 libncurses 개발 헤더/라이브러리가 필요하다.
- 이번 환경에서는 원본 전체 빌드가 term.h 누락으로 중단됐다. 실행 파일을 만들었다고 표시하지 않는다.

## 검증

집중 검사 4개 / 373737 assertions 통과, delay 95121 조합 확률 가중치 일치.
수정 production translation units 컴파일 성공. shell 테스트는 컴파일만 성공.
전체 엔진 Catch2, 실제 전투, save/load, Windows/tiles 실행은 미검증.

## 이어가기

누적 명세 `spec-checkpoint12.md` 마지막 Checkpoint12와 `recovery-checkpoint12.md`를 먼저 읽는다.
`design-2916.md`는 기존 상세 설계 원문이고, 전체 검토·구현 완료를 의미하지 않는다.
해시·변경 목록은 `source-manifest-checkpoint12.json`, 실제 결과는 validation 폴더에 있다.
