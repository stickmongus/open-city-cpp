# Open City Seoul — Native Vulkan (`open-city-cpp`) v2.2.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 16-bit HDR + MSAA + 2048px Soft PCF 그림자) + Rust (`#![no_std]` 네이티브 물리 커널 v2000) + Python 3 (SPIR-V 셰이더 & 글리프 빌더)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실측 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인(`10`개 사전 컴파일 SPIR-V 1.0 셰이더)으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·태블릿에서 가로 전체화면 & 10-포인트 멀티터치로 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임이나 외부 에셋 폴더 없이 단일 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v2.2.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v2.2.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v2.1 / v2.0 / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v2.2.0`, `versionCode 220`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v2.2.0 무기 파지·조작 풀 애니메이션 · 맵 가장자리 마감 · 진짜 서울 거리 풍경)

1. **전 무기 11종 3인칭 + 1인칭 파지 · 조준 · 반동 · 재장전 · 교체 · 격투 풀 애니메이션 (`Renderer3D.cpp` · `GameWorld.cpp`)**
   * **10종 정밀 3D 화기 모델링**: 9mm 권총 · .44 매그넘 · 전술 SMG · 펌프 샷건 · 드럼 샷건 · K2/M4 카빈 · K14 저격소총 · K3 중기관총 · 6열 회전 미니건 · RPG 어깨 발사기 (투톤 건메탈/스틸/폴리머/우드 마감, 옵틱·소음기·레이저 사이트)
   * **2-Bone 해석적 팔 IK (Inverse Kinematics)**: 장착 중인 화기를 항상 양손/한손에 파지하며, 전술 로우레디 → 어깨 견착/위버 조준 전환, 사격 반동 킥백, 왼손 탄창 교체 재장전, 무기 교체 홀스터 드로우, 맨주먹 좌/우 잽·크로스 복싱 펀치 완비
   * **1인칭 FPS 뷰모델 전면 개편**: 1인칭 팔·소매·손과 정밀 3D 화기 연동 + 걷기 흔들림·ADS 조준·재장전·미니건 회전·복싱 주먹 지원
2. **3D 월드 & 서울 전체 지도 가장자리 완벽 마감 (`CityMap.cpp` · `Renderer3D.cpp` · `HudRenderer.cpp`)**
   * **서울 외곽 14개 순환 산악 링**(도봉산·수락산·불암산·용마산·검단산·남한산·대모산·삼성산·수리산·계양산·개화산 등) + 물리 고도와 1:1 일치하는 **3D 외곽 그린벨트 구릉 지형 스커트** + **외곽 순환도로 방음벽·옹벽 경계**로 낭떠러지/공허 0% 마감
   * **40km × 34km 연속 서울 분지 바닥** + **한강 호안 석축** + **원거리 서울 스카이라인 LOD (`520m~5.8km`)** + **서울 산악 전수 상향(+Y) 법선 교정**
   * **서울 전체 지도 (`M`)**: 실제 25개 자치구 서울특별시 경계 실루엣 · 외곽 14개 산악 등고선 · 수도권 제1순환고속도로 링 추가
3. **진짜 서울 거리 풍경 전면 고도화 (`world.frag` · `CityMap.cpp` · `Renderer3D.cpp`)**
   * **한국형 녹색 도로 이정표 갠트리(문형식 표지판)** · **보행자 방호울타리** · **서울 지하철 노선 원형 폴사인** · **BIS 디지털 버스 도착 안내기**
   * **서울 인터로킹 보도블럭 + 화강암 경계석 + 노란 점자블록(유도선·횡단보도 점형 패드)** + **백색 직진 화살표 & 황색 주정차 금지선**
   * 아파트 외곽 잔디 띠 제거(단지 중앙 조경 정원화) + 대로변 다중 한국 상가 간판·돌출간판 + 빌라 **옥상 초록 우레탄 방수 페인트·스테인리스 물탱크** + 24클러스터 자연 수목 + 콤팩트 연석 POI 마커
4. **실측 서울 12대 랜드마크 · 16종 CAD 차량 · Native Vulkan 1.0+ 엔진 · 도주 가능한 서울 경찰 AI**
   * 3-Pass Native Vulkan 파이프라인(**2048px 16-Tap Soft PCF 그림자** → **16-bit Linear HDR + MSAA** → **Kawase Bloom + ACES + 2D 한글 UI**)
   * 젤리 현상 0% `24°` 하드 크리즈 CAD 차량 16종 + Rust/C++20 물리 커널 패리티 + 시야 차폐 기반 도주 가능한 서울 경찰 추격 AI (25/25 단위·통합 테스트 통과)

---

## 모바일 멀티터치 조작 안내 (Android / `--mobile-ui`)

| 영역 / 버튼 | 도보 모드 | 차량 운전 모드 |
| :--- | :--- | :--- |
| 좌측 상단 미니맵 탭 | **서울 전체 지도 열기** (`30km × 25km`, GPS · 빠른 이동) | 동일 |
| 좌측 하단 | 가상 아날로그 조이스틱 *(끝까지 밀면 자동 전력질주)* | `◀` / `▶` 부드러운 조향 패드 |
| 화면 빈 영역 드래그 | 1:1 정밀 시점 회전 | 360° 자유 시점 회전 |
| 우측 하단 주요 버튼 | `사격` · `조준` · `점프` · `장전` · `탑승/상점` | `가속` · `브레이크` · `니트로` · `드리프트` · `하차` · `경적` · `후방` |
| 상단 중앙 툴바 | `지도` · `1인칭/3인칭` · `폰` · `커스텀` · `날씨` · `시간` · `품질` | 동일 |

---

## PC 키보드 & 마우스 조작키 안내

| 입력 | 동작 |
| :--- | :--- |
| `W` `A` `S` `D` / 방향키 | 도보 이동 / 16종 차량 가속·후진·속도 연동 정밀 조향·한강 대교 주행 |
| **`M` 또는 미니맵 클릭** | **서울특별시 전체 지도 (`30km × 25km`) 열기/닫기 (클릭 GPS · 빠른 이동)** |
| **`V`** | **1인칭 시점 (도보 FPS · 3D 차량 운전석) ↔ 3인칭 시점 전환** |
| **`Y`** | **맑음 ↔ 비 오는 서울 (젖은 아스팔트 노면 반사 & 시민 우산) 전환** |
| **`T`** | **시간 +2시간 이동 (낮 · 노을 · 서울 야경 즉시 전환)** |
| **`U`** | **Vulkan 그래픽 품질 프리셋 전환 (`최고 HDR+그림자` / `높음` / `성능 우선`)** |
| **`P` / `K` / `E`** | **서울 스마트폰** / **커스텀 스튜디오** / **근접 상점·차고 열기** |
| `Space` / `Left Shift` | 점프 · 핸드브레이크 드리프트 / 전력질주 · 니트로 부스트 |
| `F` / `G` / `Q` / `R` | 차량 탑승·하차 / 경적·경찰 사이렌 / 후방 카메라 / 재장전 |
| `1` ~ `9`, `0` | 무기 슬롯 즉시 선택 (모달 열림 시 옵션 선택) |
| `H` / `Tab` / `ESC` | 조작 안내 숨기기 / 마우스 잠금 해제 / 모달 닫기·종료 |

---

## 소스 코드에서 직접 빌드하기

### Android `.apk` 직접 빌드 (Linux + Android SDK/NDK + JDK)
```bash
export ANDROID_KEYSTORE_PATH=/path/to/open-city-release.jks
export ANDROID_KEY_ALIAS=opencity
export ANDROID_STORE_PASSWORD="..."
export ANDROID_KEY_PASSWORD="..."

# 유니버설 Native Vulkan APK (arm64-v8a + armeabi-v7a) 빌드·정렬·서명·검증
./tools/build_android_apk.sh
```

### Windows (Visual Studio 2022 + Rust + CMake)
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
.\build\Release\open_city.exe
```

### Linux (Debian / Ubuntu)
```bash
sudo apt-get install build-essential cmake ninja-build rustc python3 \
  libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev libvulkan1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/open_city              # PC Native Vulkan 모드
./build/open_city --mobile-ui  # PC에서 모바일 터치 HUD 미리보기
```

---

## 프로젝트 구조

```text
open-city-cpp/
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust)
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v2.0.0 / 200)
│   └── src/com/stickmongus/opencity/
│       └── NativeLoader.java            # 몰입형 전체화면 + 디스플레이 컷아웃 + libopen_city.so 로더
├── tools/
│   ├── build_android_apk.sh             # NDK CMake + aapt2 + d8 + zipalign + apksigner v1/v2/v3 원스톱 빌더
│   ├── compile_vulkan_shaders.py        # GLSL 450 -> SPIR-V 1.0 C++ 헤더 컴파일러
│   ├── gen_korean_glyphs.py             # 한글 UI 글리프 코드포인트 자동 추출기
│   └── gen_android_icons.py             # Android 런처 아이콘 생성기
├── src/
│   ├── core/                            # Math.hpp (2D/3D 수학, SAT OBB, 레이캐스트), Mat4.hpp (Vulkan 행렬)
│   ├── platform/PlatformApp.*           # 크로스플랫폼 윈도우/입력/Vulkan Surface (GLFW 데스크톱 + Android Glue)
│   ├── physics/                         # Rust (#![no_std]) 물리 커널 v2000 & C++20 완전 패리티 브리지
│   ├── world/CityMap.*                  # 30km×25km 실측 서울 (한강·15개 대교·7대 산·12대 랜드마크·청크 스트리밍)
│   ├── gameplay/                        # 차량 16종·무기 11종·현실적 시야 기반 경찰 추격 AI·빠른 이동/GPS
│   ├── audio/SynthAudio.*               # miniaudio 락프리 실시간 엔진/드리프트/사이렌/총성/빗소리 신시사이저
│   ├── render/
│   │   ├── VulkanContext.*              # Native Vulkan 1.0+ 렌더러 (HDR16·MSAA·2048px 섀도우·PNG 캡처)
│   │   ├── shaders/*.{vert,frag}        # GLSL 450 PBR/스카이/섀도우/블룸+ACES/2D UI 셰이더 10종
│   │   ├── VulkanShadersSpv.hpp         # 사전 컴파일된 SPIR-V 1.0 바이트코드
│   │   ├── CarMeshGenerator.*           # 16종 Zero-Jelly CAD 차량 & 10-스포크 휠 생성기
│   │   └── Renderer3D.*                 # 실측 서울 3D 월드·랜드마크·한강·차량·캐릭터 Vulkan 렌더러
│   ├── ui/                              # Vulkan2D (stb_truetype 2048px 한글 아틀라스), HudRenderer (전체지도), TouchControls
│   └── main.cpp                         # 데스크톱 main() + Android android_main() 통합 실행 루프
└── tests/
    └── test_simulation.cpp              # 실측 서울·한강 대교·물리 v2000·도주 가능 경찰 AI 등 23개 자동 테스트 스위트
```
