# Open City Seoul — Native Vulkan (`open-city-cpp`) v3.0.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 16-bit HDR + 4× MSAA + 2048px Soft PCF 그림자 + 2048×2048 8× 이방성 밉맵 PBR 텍스처/노멀맵 + 실시간 동적 20광원 PBR) + 실제 OpenStreetMap 서울 GIS & SRTM 30m 고도 지형 + Rust (`#![no_std]` 네이티브 물리 커널 v2000) + Python 3 (OSM/SRTM GIS · PBR 아틀라스 · SPIR-V 셰이더 빌더)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실제 지도 기반 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **실제 서울 지도 1:1 반영 (`assets/maps/seoul_real.ocmap` · `seoul_map_2d.png`)**: 실제 OpenStreetMap 서울 전역 도로 **25,295개** · 실제 건물 폴리곤 **107,600채** · 한강/지천/석촌호수 수계 삼각형 **3,581개** · 실제 공원 삼각형 **17,931개** · 지하철역/버스정류장 POI **5,632개**와 AWS Terrarium **SRTM 30m 실측 고도(DEM)** 를 실시간 청크 스트리밍으로 구현했습니다.
- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인(`10`개 사전 컴파일 SPIR-V 1.0 셰이더 + `2048×2048` PBR 알베도/노멀·러프니스 아틀라스 + `2048×1312` 실제 서울 지도 텍스처 + `1024×1024` 3D 한글 간판 아틀라스)으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명 + 16KB ELF 페이지 정렬된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·태블릿에서 가로 전체화면 & 10-포인트 멀티터치로 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임 없이 압축 해제 후 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v3.0.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v3.0.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v2.x / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v3.0.0`, `versionCode 300`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거, arm64 16KB 페이지 정렬)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v3.0.0 실제 OpenStreetMap 서울 지도 · SRTM 30m 실측 고도 · 2048×2048 PBR 텍스처 & 대규모 실사 개편)

1. **실제 OpenStreetMap 서울 GIS & AWS Terrarium SRTM 30m 실측 고도 파이프라인 (`RealSeoulData` · `CityMap` · `tools/build_real_seoul_map.py`)**
   * 가상의 격자 도로를 완전히 대체하여 **실제 서울의 곡선 도로·교차로·한강 대교 25,295개**, **실제 건물 외곽 폴리곤(ㄱ/ㄷ/ㅁ자 이어클립 지붕) 107,600채**, **한강·여의도·밤섬·노들섬·석촌호수·청계천/중랑천/탄천/안양천 수계 3,581개**, **여의도공원·서울숲·남산·용산·올림픽공원 등 17,931개**, **지하철 출입구 & 버스정류장 5,632개**를 `8.5MB` 초고속 바이너리(`seoul_real.ocmap`)로 스트리밍합니다.
   * **SRTM 30m 실측 고도(DEM) + 도심 고층빌딩 레이더 스파이크 보정(Bare-Earth DTM)** 으로 남산(258m)·북한산·관악산·한강 지형은 실측 그대로 살리면서 테헤란로·강남·여의도·광화문 도심 도로가 매끄러운 실제 구배로 이어집니다.
   * 모든 도로 세그먼트를 `<= 10m` 간격으로 세분화하고 청크별 `16m` 고정밀 도심 지반(`groundMesh`)을 생성해 지형 겹침·공중 도로·인도 교차로 관통 현상을 0%로 제거했으며, 실제 2D 건물 폴리곤 충돌·3D 레이캐스트·곡선 도로 AI 교통 및 보행자 내비게이션을 완비했습니다.
2. **2048×2048 밉맵 · 8× 이방성 필터링 · 탄젠트 공간 노멀맵 PBR 머티리얼 (`VulkanContext` · `world.frag` · `tools/gen_pbr_textures.py`)**
   * `2048×2048` 16타일 PBR 알베도(`pbr_albedo.png`) + 탄젠트 공간 노멀/러프니스/AO(`pbr_normal_rough.png`) 아틀라스를 GPU 풀 밉맵 체인 + `8×` 이방성 샘플러로 바인딩합니다.
   * 서울 아스팔트·보도블록·노란 점자블록·한국형 아파트 외벽·유리 커튼월 초고층·화강암 오피스·벽돌/스타코 상가·1층 상가 유리창·공원 잔디·산림·화강암 암벽·한옥 기와·나무 껍질·다층 유기적 나뭇잎까지 100% 실제 노멀맵 기반 Cook-Torrance GGX PBR로 렌더링합니다.
3. **2048×1312 실제 서울 지도 2D 미니맵 & 전체 지도 (`seoul_map_2d.png` · `Vulkan2D` · `HudRenderer`)**
   * 미니맵과 `[M]` 전체 지도 모두 실제 OpenStreetMap 서울 카토그래피 이미지(`2048×1312`) 위에서 플레이어 위치·방향·실시간 주변 차량·12대 랜드마크 핀·GPS 경로·클릭 빠른 이동(도로 자동 스냅)을 1:1 GPS 좌표로 표시합니다.
4. **실기 스크린샷 기반 그래픽 결함 전면 해결 (v3.0.0 폴리시)**
   * **차량 17종 전면 점검**: 하이퍼카/트랙카/스포츠카 지붕·윈드실드 틈새 제거, 펜더 돌출 제거, 앞바퀴 본넷 관통 완전 수정, 야간 헤드라이트 하얀 원형 아티팩트 제거 및 실제 듀얼 스포트라이트 빔 적용.
   * **캐릭터 & 식생 개편**: 3인칭 캐릭터 골반 돌출 제거 및 자연스러운 하체/스니커즈 비율·보행 모션, 다분기 3D 나무줄기와 28클러스터 유기적 나뭇잎 수관 적용.
   * **3D 한글 간판·이정표 & 서울 시내버스 & 동적 20광원**: `1024×1024` 3D 한글 간판·도로 이정표·지하철역명판·BIS·한국 번호판, 서울 간선(472)/지선(5511) 버스, 전 차량 3D 운전자, 27개 자동화 테스트(Release + ASan/UBSan) 100% 통과.

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
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust) v3.0.0
├── assets/
│   ├── maps/seoul_real.ocmap            # 실제 OSM 서울 25,295 도로 · 107,600 건물 · 3,581 수계 · 17,931 공원 · SRTM 30m DEM
│   ├── maps/seoul_map_2d.png            # 2048×1312 실제 서울 카토그래피 미니맵 & 전체 지도 텍스처
│   └── textures/pbr_{albedo,normal_rough}.png # 2048×2048 16타일 밉맵·8×이방성 PBR 알베도 & 탄젠트 노멀/러프니스 아틀라스
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v3.0.0 / 300)
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
│   ├── world/CityMap.*                  # 실제 OSM 서울 & 12대 랜드마크 · 한강 대교 · 청크 스트리밍 통합 맵
│   ├── gameplay/                        # 차량 17종(서울 시내버스 포함)·무기 11종·곡선 도로 AI 교통/보행자·빠른 이동/GPS
│   ├── audio/SynthAudio.*               # miniaudio 락프리 실시간 엔진/드리프트/사이렌/총성/빗소리 신시사이저
│   ├── render/
│   │   ├── VulkanContext.*              # Native Vulkan 1.0+ 렌더러 (HDR16·MSAA·2048px 섀도우·8× 이방성 밉맵 PBR)
│   │   ├── shaders/*.{vert,frag}        # GLSL 450 PBR/노멀맵/스카이/섀도우/블룸+ACES/2D UI 셰이더 10종
│   │   ├── VulkanShadersSpv.hpp         # 사전 컴파일된 SPIR-V 1.0 바이트코드
│   │   ├── CarMeshGenerator.*           # 17종 Zero-Jelly CAD 차량 & 10-스포크 휠 생성기
│   │   └── Renderer3D.*                 # 실제 OSM 5×5 청크 스트리밍 · 12대 랜드마크 · 3D 한글 간판 · 캐릭터/차량 렌더러
│   ├── ui/                              # Vulkan2D (2048px 한글 & 실제 서울 2D 지도 텍스처), HudRenderer, TouchControls
│   └── main.cpp                         # 데스크톱 main() + Android android_main() 통합 실행 루프
└── tests/
    └── test_simulation.cpp              # 실제 OSM/SRTM 서울 GIS·PBR·한강 대교·물리 v2000 등 27개 자동 테스트 스위트
```
