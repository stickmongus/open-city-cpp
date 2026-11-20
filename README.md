# Open City Seoul — Native Vulkan (`open-city-cpp`) v2.0.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 16-bit HDR + MSAA + 2048px Soft PCF 그림자) + Rust (`#![no_std]` 네이티브 물리 커널 v2000) + Python 3 (SPIR-V 셰이더 & 글리프 빌더)** 아키텍처로 전면 재설계된 **크로스플랫폼(Android · Windows · Linux) 실측 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인(`10`개 사전 컴파일 SPIR-V 1.0 셰이더)으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·태블릿에서 가로 전체화면 & 10-포인트 멀티터치로 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임이나 외부 에셋 폴더 없이 단일 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v2.0.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v2.0.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v2.0.0`, `versionCode 200`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v2.0.0 6대 핵심 전면 개편)

1. **Native Vulkan 1.0+ 3D 엔진 완전 교체 (`src/render/VulkanContext.*` · `VulkanShadersSpv.hpp`)**
   * 기존 `raylib` / OpenGL / OpenGL ES 3.0 스택을 **100% 제거**하고 `Vulkan-Headers` + `volk` + `GLFW 3.4`(데스크톱) / `android_native_app_glue`(안드로이드) + `stb_truetype` + `miniaudio` 기반 자체 엔진으로 재구축
   * 3-Pass Vulkan 렌더 파이프라인: **2048×2048 16-Tap 회전 포아송 Soft PCF 그림자 패스** → **16-bit Linear HDR (`R16G16B16A16_SFLOAT`) + 4x/2x MSAA 3D 월드 패스** → **Kawase HDR 블룸 + ACES 필르믹 톤매핑 + 2D 한글 UI 패스**
2. **젤리 현상 완전 제거 — 16종 차량 CAD 정밀 디자인 전면 개편 (`src/render/CarMeshGenerator.*`)**
   * `24°` 하드 크리즈 법선 분리(`MeshBuilder3D::buildCreasedVertices`) 기반 **프레스 가공 강판 차체**, 곡면형 전·후면 범퍼 및 랩어라운드 3D LED 헤드램프·DRL·풀와이드 테일라이트
   * 차체 플랭크 곡면(`flankZAt`)에 1mm 단위로 밀착하는 **원형 3D 휠아치 & 10-스포크 알로이 림 + 브렘보 캘리퍼**
   * A/B/C/D 필러와 **동일 곡면 수식(`greenhouseSurfacePt`)으로 결합된 매끈한 스모크드 곡면 측면/전면 유리** + 유리 너머로 보이는 **3D 대시보드·스티어링 휠·버킷 시트 실내**
   * 젤리처럼 번들거리는 과노출을 제거한 **에너지 보존형 메탈릭 베이스코트 + 클리어코트 PBR 셰이더** & 서울 경찰차·해치 택시·배송 밴 무-Z-파이팅 리버리
3. **현실적인 자동차 물리 엔진 & 조향 조작감 (`physics_kernel.rs` v2000 · C++20 100% 패리티)**
   * 속도 연동 가변 조향 랙(저속 경쾌·고속 안정), 언덕·한강 대교 경사 중력(`slope_z`), Pacejka 코너링 스크럽 & 트레일 브레이킹 하중 이동, 오버스티어 셀프-얼라이닝 카운터스티어, 키보드·모바일 터치 점진 조향/스로틀 필터 적용
4. **실제 서울 크기(`30 km × 25 km`) 초대형 서울특별시 오픈월드 (`src/world/CityMap.*`)**
   * **실제 S자 곡선 한강** · **여의도 섬** · **석촌호수** · **청계천** · **주행 가능한 한강 대교 15개**(반포대교 달빛무지개분수·잠수교, 올림픽대교 사장교 주탑, 서강/한강대교 아치 등)
   * **서울 실제 7대 명산**(남산·북한산·인왕산·북악산·관악산·아차산·청계산) & **12대 매크로 랜드마크**(555m 롯데월드타워, 남산 N서울타워, 경복궁·광화문 곡선 기와지붕, 여의도 63빌딩·파크원·국회의사당, 삼성동 코엑스, 서울시청, 숭례문, 동대문 DDP, 잠실종합운동장, 강남역)
   * 한국식 노란 중앙선·파란 버스전용차로·횡단보도, 2짝 알루미늄 창틀·베란다 난간이 반사되는 **한국형 아파트(아파트)**, 은행나무·벚꽃나무·소나무, O(1) `252m` 실시간 청크 스트리밍
5. **클릭 가능한 전체 화면 서울 지도 (`M` 또는 미니맵 클릭/탭)**
   * `30km × 25km` 서울 전역을 한눈에 보고 줌(`1x~12x`)·패닝·12대 랜드마크 원클릭 선택 · **GPS 목적지 설정** · **원탭 즉시 빠른 이동(Fast-Travel)** 및 실시간 경찰 수색 구역 확인
6. **현실적이고 도주 가능한 서울 경찰 추격 AI (`src/gameplay/GameWorld.cpp`)**
   * 건물 차폐 시야(LOS) + 전방 시야각(Vision Cone) + 야간/우천 시야 감쇠, 마지막 목격 위치(`policeLastKnownPos`) 수색 순찰 모드 전환
   * 숨어 있을 때 앞길 매복 순간이동 스폰 완전 제거, AI 비현실적 니트로 제거, 골목/시야 이탈 시 **`1.55×` 은신 회피 가속** 및 HUD 실시간 도주 진행률 게이지 표시

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
