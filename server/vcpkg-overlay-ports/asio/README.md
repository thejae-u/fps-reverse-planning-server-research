# Windows 고해상도 Asio 타이머

vcpkg의 Asio 1.32.0 포트를 기반으로 Windows IOCP 타이머 생성 부분만 패치합니다.
Windows CMake preset의 `VCPKG_OVERLAY_PORTS`가 이 포트를 선택합니다.
Linux/macOS preset은 기존 vcpkg 포트를 사용합니다.

`CreateWaitableTimerExW`에 `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`의 문서상 값
`0x00000002`를 전달합니다. 구형 SDK에서도 컴파일할 수 있도록 숫자 상수를 사용합니다.
Windows 10 1803 이전에서 플래그가 `ERROR_INVALID_PARAMETER`로 거부되면 일반
타이머로 돌아갑니다. 다른 생성 오류는 기존 Asio 예외 처리로 전달합니다.

`steady_clock`, `steady_timer`, IOCP 이벤트 전달, strand, 예약 취소와 핸들 정리는
기존 Asio 구현을 사용합니다. 해당 IOCP context에 속한 모든 타이머에 적용되므로
tick뿐 아니라 heartbeat와 handshake 타이머에도 적용됩니다.
고해상도 타이머는 OS 스케줄링이나 handler 대기로 인한 지연까지 보장하지 않습니다.

의존성을 설치할 때 패치가 적용되므로 `vcpkg_installed`의 생성 파일을 직접 수정하지
않습니다. Asio를 업데이트할 때는 포트의 버전·소스 해시와 패치를 함께 검토해야 합니다.
패치를 처음 적용하거나 변경한 뒤에는 각 구성에서
`cmake --build build/x64-debug --clean-first --parallel 4`처럼 전체 재컴파일하여
기존 precompiled header에도 변경이 반영되도록 합니다.

원본 포트: `microsoft/vcpkg`, Asio 1.32.0.
Asio 라이선스: BSL-1.0 (설치 시 원본 라이선스를 포함합니다).
API 문서: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw
