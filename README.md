# Open City Seoul — Native Vulkan (`open-city-cpp`) v3.1.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 16-bit HDR + 4× MSAA + 2048px Soft PCF 그림자 + 2048×2048 8× 이방성 밉맵 PBR 텍스처/노멀맵 + 실시간 동적 20광원 PBR) + 실제 OpenStreetMap 서울 GIS & SRTM 30m 고도 지형 + Rust (`#![no_std]` 네이티브 물리 커널 v2000) + Python 3 (OSM/SRTM GIS · PBR 아틀라스 · SPIR-V 셰이더 빌더)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실제 지도 기반 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **실제 서울 지도 1:1 반영 (`assets/maps/seoul_real.ocmap` · `seoul_map_2d.png`)**: 실제 OpenStreetMap 서울 전역 도로 **25,295개** · 실제 건물 폴리곤 **107,600채** · 한강/지천/석촌호수 수계 삼각형 **3,581개** · 실제 공원 삼각형 **17,931개** · 지하철역/버스정류장 POI **5,632개**와 AWS Terrarium **SRTM 30m 실측 고도(DEM)** 를 실시간 청크 스트리밍으로 구현했습니다.
- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인(`10`개 사전 컴파일 SPIR-V 1.0 셰이더 + `2048×2048` PBR 알베도/노멀·러프니스 아틀라스 + `2048×1312` 실제 서울 지도 텍스처 + `1024×1024` 3D 한글 간판 아틀라스)으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명 + 16KB ELF 페이지 정렬된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·태블릿에서 가로 전체화면 & 10-포인트 멀티터치로 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임 없이 압축 해제 후 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v3.1.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v3.1.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v3.0 / v2.x / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v3.1.0`, `versionCode 310`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거, arm64 16KB 페이지 정렬)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v3.1.0 차량 가감속 피치 관성 정밀 수정 · 한국 표준 3.25m 미터법 차선 & 서울 고밀도 도시 구조물)

1. **차량 종방향(가속·감속) 피치 관성 및 4륜 서스펜션 정밀 교정 (`physics_kernel.rs` · `PhysicsBridge.hpp` · `GameWorld.cpp`)**
   * 급가속 시 차체 후륜이 가라앉고 앞범퍼가 들리는 **리어 스쿼트(+bodyPitch)**, 급제동·정면 충돌 시 앞범퍼가 내려앉는 **노즈 다이브(-bodyPitch)** 및 4륜 스트럿 압축 방향을 Rust 네이티브 물리 커널과 C++20 폴백 엔진에 1:1 동일하게 교정했습니다 *(코너링 롤은 기존 외측 기울기 유지)*.
2. **한국 도로 표준 미터법 차선 (`3.15~3.45m` 차로 폭 · `0.15m` 실선/점선) & 서울 버스전용차로 (`world.frag` · `Renderer3D.cpp` · `GameWorld.cpp`)**
   * 기존 정규화(`[-1..+1]`) 도로 UV로 차선 하나가 ~9.5m로 늘어나던 문제를 해결해, 실제 미터 단위(`[-hw..+hw]`)로 도로 폭과 차로 수를 인코딩했습니다.
   * 왕복 복선 황색 중앙선, 실제 `3.25m` 간격 다차로 백색 점선(`3m` 도색 / `5m` 간격), **서울 대로 파란색 버스전용차로**, `0.45m` 횡단보도 지브라·`0.40m` 정지선, 그리고 차로별 타이어 마모 자국을 정밀 렌더링하며 AI 교통 차량과 서울 시내버스(전용차로 우선 주행)도 정확히 3.25m 차로 중앙에 맞춰 주행합니다.
3. **고밀도 정밀 서울 도시 구조물 & 옥상 건축 디테일 (`RealSeoulData` · `CityMap` · `Renderer3D`)**
   * 인접 골목 때문에 넓은 대로 경계가 가려지던 `isOnRoad` 판정을 전 도로 에지 검사로 교정해 테헤란로 아스팔트 위 가로수·보행자 스폰을 0%로 제거했습니다.
   * 실제 상가 건물 외벽에 밀착된 **3D 발광 한글 간판(`ShopSign`)**, 보도변 **따릉이 거치대(`DdareungiRack`) · 가판대/버스카드 키오스크(`StreetKiosk`) · 한전 지상변압기(`UtilityBox`) · 야외 카페 파라솔(`CafeTable`) · 대로 중앙분리대 방현 펜스(`MedianBarrier`) · 7.5m 모듈형 보행자 안전펜스(`PedGuardrail`) · 도로 폭 맞춤 갠트리 표지판(`RoadGantry`) · 교차로 보행신호등+볼라드 · 한강 대교 난간 가로등**, 그리고 실제 OSM 건물 옥상 **엘리베이터 기계실·냉각탑·통신 마스트**를 고밀도로 배치했습니다.
4. **실제 OpenStreetMap 서울 GIS + SRTM 30m 실측 고도 & 2048×2048 PBR 파이프라인 (`v3.0.0` 기반)**
   * 실제 서울 도로 **25,295개**, 건물 폴리곤 **107,600채**, 수계 **3,581개**, 공원 **17,931개**, 지하철/버스 POI **5,632개**, Bare-Earth DTM 보정 SRTM 30m 고도, `2048×2048` 8× 이방성 밉맵 PBR 텍스처/노멀맵, `2048×1312` 실제 서울 카토그래피 미니맵/전체 지도, 17종 Zero-Jelly 차량, **28개 자동화 테스트(Release + ASan/UBSan) 100% 통과**.

---

## 모바일 멀티터치 조작 안내 (Android / `--mobile-ui`)

| 영역 / 버튼 | 도보 모드 | 차량 운전 모드 |
| :--- | :--- | :--- |
| 좌측 상단 미니맵 탭 | **실제 서울 전체 지도 열기** (`OSM + SRTM 30m`, GPS · 빠른 이동) | 동일 |
| 좌측 하단 | 가상 아날로그 조이스틱 *(끝까지 밀면 자동 전력질주)* | `◀` / `▶` 부드러운 조향 패드 |
| 화면 빈 영역 드래그 | 1:1 정밀 시점 회전 | 360° 자유 시점 회전 |
| 우측 하단 주요 버튼 | `사격` · `조준` · `점프` · `장전` · `탑승/상점` | `가속` · `브레이크` · `니트로` · `드리프트` · `하차` · `경적` · `후방` |
| 상단 중앙 툴바 | `지도` · `1인칭/3인칭` · `폰` · `커스텀` · `날씨` · `시간` · `품질` | 동일 |

---

## PC 키보드 & 마우스 조작키 안내

| 입력 | 동작 |
| :--- | :--- |
| `W` `A` `S` `D` / 방향키 | 도보 이동 / 17종 차량 가속·후진·속도 연동 정밀 조향·실제 한강 대교 주행 |
| **`M` 또는 미니맵 클릭** | **실제 서울특별시 전체 지도 (`OSM + SRTM 30m`) 열기/닫기 (클릭 GPS · 빠른 이동)** |
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
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust) v3.1.0
├── assets/
│   ├── maps/seoul_real.ocmap            # 실제 OSM 서울 25,295 도로 · 107,600 건물 · 3,581 수계 · 17,931 공원 · SRTM 30m DEM
│   ├── maps/seoul_map_2d.png            # 2048×1312 실제 서울 카토그래피 미니맵 & 전체 지도 텍스처
│   └── textures/pbr_{albedo,normal_rough}.png # 2048×2048 16타일 밉맵·8×이방성 PBR 알베도 & 탄젠트 노멀/러프니스 아틀라스
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v3.1.0 / 310)
│   └── src/com/stickmongus/opencity/
│       └── NativeLoader.java            # 몰입형 전체화면 + 디스플레이 컷아웃 + libopen_city.so 로더
├── tools/
│   ├── build_android_apk.sh             # NDK CMake + aapt2 + d8 + zipalign + apksigner v1/v2/v3 원스톱 빌더
│   ├── build_real_seoul_map.py          # OpenStreetMap + AWS Terrarium SRTM 30m DEM -> seoul_real.ocmap & 2D 지도 생성기
│   ├── gen_pbr_textures.py              # 2048×2048 16타일 PBR 알베도/노멀/러프니스/AO 아틀라스 생성기
│   ├── compile_vulkan_shaders.py        # GLSL 450 -> SPIR-V 1.0 C++ 헤더 컴파일러
│   ├── gen_korean_glyphs.py             # 한글 UI 글리프 코드포인트 자동 추출기
│   └── gen_android_icons.py             # Android 런처 아이콘 생성기
├── src/
│   ├── core/                            # Math.hpp (2D/3D 수학, SAT OBB, 레이캐스트), Mat4.hpp (Vulkan 행렬)
│   ├── platform/PlatformApp.*           # 크로스플랫폼 윈도우/입력/Vulkan Surface/Android Asset 로더
│   ├── physics/                         # Rust (#![no_std]) 물리 커널 v2000 & C++20 완전 패리티 브리지
│   ├── world/RealSeoulData.*            # OCM3 실제 OSM/SRTM GIS 파서 · Bare-Earth DTM 보정 · O(1) 청크/폴리곤 충돌
│   ├── world/CityMap.*                  # 실제 OSM 서울 & 12대 랜드마크 · 한강 대교 · 고밀도 서울 스트리트 구조물 스트리밍
│   ├── gameplay/                        # 차량 17종(서울 시내버스 포함)·무기 11종·3.25m 미터법 차로 AI 교통/보행자·빠른 이동/GPS
│   ├── audio/SynthAudio.*               # miniaudio 락프리 실시간 엔진/드리프트/사이렌/총성/빗소리 신시사이저
│   ├── render/
│   │   ├── VulkanContext.*              # Native Vulkan 1.0+ 렌더러 (HDR16·MSAA·2048px 섀도우·8× 이방성 밉맵 PBR)
│   │   ├── shaders/*.{vert,frag}        # GLSL 450 PBR/노멀맵/3.25m 미터법 도로/스카이/섀도우/블룸+ACES/2D UI 셰이더 10종
│   │   ├── VulkanShadersSpv.hpp         # 사전 컴파일된 SPIR-V 1.0 바이트코드
│   │   ├── CarMeshGenerator.*           # 17종 Zero-Jelly CAD 차량 & 10-스포크 휠 생성기
│   │   └── Renderer3D.*                 # 실제 OSM 5×5 청크 스트리밍 · 옥상 기계실/마스트 · 3D 한글 간판 · 16종 도시 구조물
│   ├── ui/                              # Vulkan2D (2048px 한글 & 실제 서울 2D 지도 텍스처), HudRenderer, TouchControls
│   └── main.cpp                         # 데스크톱 main() + Android android_main() 통합 실행 루프
└── tests/
    └── test_simulation.cpp              # 실제 OSM/SRTM 서울 GIS·피치 관성·3.25m 차로·고밀도 구조물 등 28개 자동 테스트 스위트
```
