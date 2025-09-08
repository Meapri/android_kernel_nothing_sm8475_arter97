### arter 커널용 UDFPS/FOD 화면 꺼짐 ROM 측 수정 가이드

#### TL;DR
- 문제 원인: 프레임워크는 AIDL UDFPS 콜백(onPointerDown/Up)을 기대하지만, 기기는 HIDL 2.3 Goodix HAL만 제공 → HBM 미토글 → 화면 어둡거나 꺼짐.
- 커널로 모두 우회하는 대신, ROM(벤더/Framework)에서 HBM/FOD 토글을 직접 처리하면 즉시 해결 가능.
- 권장안: 벤더 Fingerprint HAL 또는 경량 벤더 데몬이 FOD DOWN/UP 시 아래 sysfs를 토글
  - `/sys/panel_feature/hbm_mode` (HBM on/off)
  - `/sys/devices/platform/goodix_ts.0/gesture/fod_en` (FOD 제스처 enable)

---

### 증상 정리
- UDFPS 터치 시 화면이 어두워지거나 꺼짐(Doze/LP 상태 유지)
- 로그에 AIDL UDFPS 콜백 누락 메시지, Goodix HAL은 HIDL v2.3만 존재
- 수동으로 다음을 쓰면 정상 동작 확인됨
  - `echo 1 > /sys/panel_feature/hbm_mode`
  - `echo 1 > /sys/devices/platform/goodix_ts.0/gesture/fod_en`

### 핵심 원인
- 프레임워크(SystemUI/UdfpsController)는 AIDL 경로로만 HBM/레이어 토글을 기대
- 기기 HAL(HIDL 2.3)은 UDFPS 전용 onPointerDown/Up 이벤트를 프레임워크로 전달하지 않음
- 따라서 ROM이 직접 HBM/FOD를 켜줘야 함(커널 sysfs 토글)

### ROM 측 해결 옵션

#### 옵션 A) 벤더 Fingerprint HAL에서 직접 토글 (권장)
1) Goodix HAL 소스 내 FOD DOWN/UP 이벤트 지점에 sysfs 토글 추가
   - 예: `onFingerDown()` / `onFingerUp()` 또는 Acquired/Authenticated 분기
   - 의사코드(C++):
```
static void writeInt(const char* path, int v) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) { char s[8]; int n = snprintf(s, sizeof(s), "%d", v); write(fd, s, n); close(fd); }
}

void GoodixHal::onFingerDown() {
    writeInt("/sys/devices/platform/goodix_ts.0/gesture/fod_en", 1);
    writeInt("/sys/panel_feature/hbm_mode", 1);
}

void GoodixHal::onFingerUp() {
    writeInt("/sys/panel_feature/hbm_mode", 0);
}
```
2) 부팅 시 FOD 제스처 enable(한 번만)
   - HAL init 경로 혹은 init rc에서: `echo 1 > /sys/devices/platform/goodix_ts.0/gesture/fod_en`
3) SELinux(벤더) 허용
   - 컨텍스트 확인(기기에서):
     - `/sys/panel_feature/hbm_mode` → `u:object_r:vendor_sysfs_graphics:s0`
     - `/sys/devices/platform/goodix_ts.0/gesture/fod_en` → `u:object_r:sysfs:s0`
   - sepolicy(예시):
```
allow hal_fingerprint_default vendor_sysfs_graphics:file rw_file_perms;
allow hal_fingerprint_default sysfs:file rw_file_perms;
```
   - 필요 시 파일 컨텍스트 매핑 추가(벤더 file_contexts)

장점: 데몬 없이 HAL만으로 완결, 지연 최소화. 프레임워크 변경 불필요.

#### 옵션 B) 경량 벤더 데몬(udfpsd) 사용
1) 동작
   - Goodix 커널 드라이버 netlink/uevent 또는 input/uevent를 수신해 FOD DOWN/UP 이벤트 획득
   - 이벤트에 따라 위 sysfs 두 노드 토글(다운=HBM1/FOD1, 업=HBM0)
2) 통합
   - 바이너리: `vendor/bin/udfpsd`
   - init 서비스(예):
```
service udfpsd /vendor/bin/udfpsd
    class late_start
    user system
    group system
    oneshot
    seclabel u:object_r:hal_fingerprint_default:s0
```
   - sepolicy(예시):
```
allow hal_fingerprint_default vendor_sysfs_graphics:file rw_file_perms;
allow hal_fingerprint_default sysfs:file rw_file_perms;
```
3) 부팅 시 FOD enable:
   - `on post-fs-data` 또는 서비스 시작 시 `echo 1 > /sys/.../fod_en`

장점: HAL 수정이 어려울 때 적용 가능. 프레임워크 무관.

#### 옵션 C) Framework/SystemUI 오버레이/브릿지(보완)
1) 오버레이로 UDFPS 지원 신호 켜기(ROM 버전에 맞춰 값명 상이)
   - 예(디바이스 overlay):
```
<bool name="config_supportsUDFPS">true</bool>
```
   - 기기별 X/Y/W/H 등 추가 overlay 필요 시 설정
2) HIDL→AIDL 브릿지 추가(대공사)
   - packages/services/Biometrics 경로의 HIDL 경로에 Udfps onPointerDown/Up 유사 이벤트를 SystemUI로 전달하는 브릿지 구현
   - 유지보수 부담 큼 → 옵션 A/B 권장

### 권장 적용 순서(최소 변경)
1) 벤더 sepolicy에 sysfs 쓰기 허용 추가(위 allow 2줄)
2) 부팅 시 FOD enable(Init rc 1줄)
3) HAL에 onFingerDown/Up 훅으로 HBM/FOD sysfs 토글 추가
4) 테스트
   - 루트 셸 불필요: 일반 부팅 상태에서 지문 눌렀을 때 화면 꺼짐 없이 HBM 상승 확인
   - `dmesg | grep -i hbm`(HAL 로그), `cat /sys/panel_feature/hbm_mode`

### 테스트 체크리스트
- [ ] 부팅 후 `/sys/devices/platform/goodix_ts.0/gesture/fod_en` 값이 1
- [ ] 지문 터치 다운 시 `/sys/panel_feature/hbm_mode`가 1로 전환
- [ ] 손을 떼면(`/sys/panel_feature/hbm_mode`가 0) 화면 정상 복귀
- [ ] SELinux Enforcing에서도 동작(denial 없음)

### 트러블슈팅
- Permission denied → sepolicy, 서비스 도메인 확인(로그캣 `avc: denied`)
- 값 반영 지연/깜빡임 → HBM set 직후 1 VBLANK 대기 또는 짧은 wakelock 고려(프레임 드랍 방지)
- Doze/LP1/LP2에서 깨어나지 않음 → HAL에서 FOD DOWN 시 `setScreenState(ON)` 또는 짧은 wakelock 추가

### 부록: 참조 노드/컨텍스트
- `/sys/panel_feature/hbm_mode` → `u:object_r:vendor_sysfs_graphics:s0`
- `/sys/devices/platform/goodix_ts.0/gesture/fod_en` → `u:object_r:sysfs:s0`



