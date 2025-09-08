### UDFPS Callback Null – AIDL Fingerprint HAL Migration (KernelSU Module)

## TL;DR
- 문제: 기기는 HIDL 2.3 Fingerprint HAL만 제공하고, 프레임워크는 AIDL(UDFPS `onPointerDown/Up`)을 기대해 콜백이 항상 null.
- 해결: AIDL `IFingerprint/default` HAL을 KernelSU 모듈로 배포하고, 기존 HIDL 2.3 서비스를 비활성화.
- 범위: 유저스페이스만 수정. 커널 변경 불필요.

## 환경
- Device/ROM: Nothing Pong (SM8475), LineageOS
- Device tree: android_device_nothing_Pong ([link](https://github.com/LineageOS/android_device_nothing_Pong))
- Kernel(참고용): android_kernel_nothing_sm8475 ([link](https://github.com/LineageOS/android_kernel_nothing_sm8475))

## 증상
- UDFPS에서 손가락 올리면 화면 OFF, 떼면 ON.
- `UdfpsControllerOverlay` 생성/파괴 및 Goodix HAL `goodixExtCmd`(Finger Down/Up)는 발생.
- Framework 로그: `FingerprintCallback: sendUdfpsPointerDown/Up, callback null`, Doze/Dream 진입으로 화면 OFF.
- `dumpsys fingerprint`: `provider: FingerprintProvider/defaultHIDL`.
- 실행 중 서비스: `android.hardware.biometrics.fingerprint@2.3-service.nt.pong` (HIDL 2.3).

## 근본 원인
- UDFPS는 AIDL Fingerprint HAL에서 `onPointerDown/Up`을 제공해야 함.
- HIDL 2.3 표준엔 포인터 API가 없어 SystemUI 콜백 바인딩 불가 → null.

## 목표
- ROM 재빌드 없이 AIDL `IFingerprint/default` 제공(시스템리스).
- 기존 HIDL 2.3 서비스와 충돌 없이 비활성화.

## 제약
- 커널 변경 없음.
- AVB/dm-verity를 깨지 않도록 적용.
- SELinux 허용 필요.

## 접근(권장: KernelSU 모듈)
- AIDL HAL 바이너리 및 설정을 모듈로 배포:
  - `/vendor/bin/hw/android.hardware.biometrics.fingerprint-service.nt.pong` (AIDL 서비스 실행파일)
  - `/vendor/etc/vintf/manifest/manifest_fingerprint_aidl.xml` (AIDL 선언)
  - `/vendor/etc/init/android.hardware.biometrics.fingerprint-service.nt.pong.rc` (서비스 기동)
  - `/vendor/etc/init/android.hardware.biometrics.fingerprint@2.3-service.nt.pong.rc` (disabled 오버레이)
  - `sepolicy.rule` (binder/hwservice/벤더 파일 접근 허용)
  - `post-fs-data.sh` / `service.sh` (권한/라벨 적용, 부팅 후 start)

## AIDL HAL 요구사항
- `IFingerprint/default` 구현:
  - 표준 흐름: enroll/authenticate/remove/…
  - UDFPS: `onPointerDown(x, y, minor, major, deviceId)` → Goodix extCmd(Finger Down), `onPointerUp()` → Goodix extCmd(Finger Up)
  - UDFPS 센서 좌표/반경 정확히 반환(sensor props)

## 입력 필요(제작 시)
- AIDL HAL 바이너리/소스(AOSP/CAF 베이스 등) 또는 빌드 위탁
- 서비스 접미사: `nt.pong`
- UDFPS 센터/반경(없으면 SystemUI 오버레이/로그로 추정 가능)
- 벤더 라이브러리 존재(로그 기반):
  - `/vendor/lib64/hw/android.hardware.biometrics.fingerprint@2.3.so`
  - `/vendor/lib64/libvendor.goodix.hardware.biometrics.fingerprint@2.1.so`

## 모듈 산출물 목록
- module.prop
- post-fs-data.sh (chmod/chcon, 구 HIDL 서비스 stop)
- service.sh (AIDL 서비스 start)
- sepolicy.rule (최소 허용 규칙)
- system/vendor/bin/hw/android.hardware.biometrics.fingerprint-service.nt.pong
- system/vendor/etc/init/android.hardware.biometrics.fingerprint-service.nt.pong.rc
- system/vendor/etc/vintf/manifest/manifest_fingerprint_aidl.xml
- system/vendor/etc/init/android.hardware.biometrics.fingerprint@2.3-service.nt.pong.rc (disabled)

## 샘플 스니펫

manifest_fingerprint_aidl.xml
```xml
<manifest version="1.0" type="device">
  <hal format="aidl">
    <name>android.hardware.biometrics.fingerprint</name>
    <version>1</version>
    <interface>
      <name>IFingerprint</name>
      <instance>default</instance>
    </interface>
  </hal>
  </manifest>
```

android.hardware.biometrics.fingerprint-service.nt.pong.rc
```
service android.hardware.biometrics.fingerprint-service.nt.pong /vendor/bin/hw/android.hardware.biometrics.fingerprint-service.nt.pong
    class late_start
    user system
    group system input drmrpc
    capabilities BLOCK_SUSPEND
    writepid /dev/cpuset/system-background/tasks
    oneshot

on property:sys.boot_completed=1
    start android.hardware.biometrics.fingerprint-service.nt.pong
```

HIDL 2.3 disable overlay
```
service android.hardware.biometrics.fingerprint@2.3-service.nt.pong /bin/true
    class hal
    disabled
    oneshot
```

post-fs-data.sh
```sh
#!/system/bin/sh
chmod 0755 /vendor/bin/hw/android.hardware.biometrics.fingerprint-service.nt.pong
chown root:root /vendor/bin/hw/android.hardware.biometrics.fingerprint-service.nt.pong
chcon u:object_r:hal_fingerprint_default_exec:s0 /vendor/bin/hw/android.hardware.biometrics.fingerprint-service.nt.pong
stop android.hardware.biometrics.fingerprint@2.3-service.nt.pong 2>/dev/null
```

service.sh
```sh
#!/system/bin/sh
setprop ctl.start android.hardware.biometrics.fingerprint-service.nt.pong
```

sepolicy.rule (예시)
```
allow hal_fingerprint_default hal_fingerprint_default_exec file { read open execute getattr map };
allow hal_fingerprint_default vendor_file file { read open getattr };
allow hal_fingerprint_default vendor_file dir { search getattr open };
allow hal_fingerprint_default servicemanager service_manager { add find list };
allow hal_fingerprint_default hal_fingerprint_hwservice hwservice_manager find;
allow hal_fingerprint_default hal_client_domain binder call;
allow hal_fingerprint_default hwservicemanager binder { call transfer };
```

## 구현 체크리스트
- AIDL HAL 빌드(arm64), 벤더 Goodix 라이브러리 링크/동적 로딩 검증
- 센서 props 값 채움(좌표/반경)
- 파일 배치/라벨 `u:object_r:hal_fingerprint_default_exec:s0`
- HIDL 2.3 서비스 비활성화 오버레이 적용
- 부팅 후 서비스/SELinux 정상 기동 확인

## 검증
- `service list | grep -i android.hardware.biometrics.fingerprint.IFingerprint/default`
- `dumpsys fingerprint | head -n 80` 에 AIDL provider와 UDFPS props
- `logcat`에 더 이상 `FingerprintCallback ... callback null` 없음
- UDFPS 등록/인증 정상, 화면 OFF 전환 없음

## 리스크 & 대응
- sepolicy deny → `logcat | grep -i avc` 확인 후 규칙 보강
- 벤더 심볼 불일치 → dlopen/dlsym 또는 BSP 태그 매칭
- 센서 좌표 오차 → props 보정

## 참고 링크
- Device tree: android_device_nothing_Pong ([link](https://github.com/LineageOS/android_device_nothing_Pong))
- Kernel (reference): android_kernel_nothing_sm8475 ([link](https://github.com/LineageOS/android_kernel_nothing_sm8475))


