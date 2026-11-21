# Open City Seoul — Native Vulkan (`open-city-cpp`) v3.5.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 카메라 상대 고정밀 뎁스 · DPI 독립 반응형 모바일 HUD · 16-bit HDR + 4× MSAA + 2048px Soft PCF 그림자 + 2048×2048 8× 이방성 밉맵 PBR 텍스처/노멀맵 + 실시간 동적 20광원 PBR) + 실제 OpenStreetMap 서울 GIS & SRTM 30m 고도 지형 + 한국 고속도로/일반도로 완전 분리 & 무결점 교차로 차선 인프라 + Rust (`#![no_std]` 네이티브 물리 커널 v2000) + Python 3 (OSM/SRTM GIS · PBR 아틀라스 · SPIR-V 셰이더 빌더)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실제 지도 기반 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **실제 서울 지도 1:1 반영 (`assets/maps/seoul_real.ocmap` · `seoul_map_2d.png`)**: 실제 OpenStreetMap 서울 전역 도로 **25,295개** *(경부고속도로·올림픽대로·강변북로·내부순환로·동부간선도로 878개 고속도로 구간 완전 구분 · 16×16 공간 격자 기반 무결점 교차로 아스팔트 박스 & 이중 황색 중앙선 밀착)* · 실제 건물 폴리곤 **107,600채** *(지하상가/지하철 대합실 도로 침범 자동 필터링)* · 한강/지천/석촌호수 수계 삼각형 **3,581개** · 실제 공원 삼각형 **17,931개** · 지하철역/버스정류장 POI **5,632개**와 AWS Terrarium **SRTM 30m 실측 고도(DEM)** 를 실시간 청크 스트리밍으로 구현했습니다.
- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인(`10`개 사전 컴파일 SPIR-V 1.0 셰이더 + `2048×2048` PBR 알베도/노멀·러프니스 아틀라스 + `2048×1312` 실제 서울 지도 텍스처 + `1024×1024` 3D 한글 간판·고속도로/시내 갠트리 아틀라스)으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명 + 16KB ELF 페이지 정렬된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·폴더블·태블릿에서 DPI 반응형 모바일 UI & 10-포인트 멀티터치로 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임 없이 압축 해제 후 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v3.5.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v3.5.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v3.x / v2.x / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v3.5.0`, `versionCode 350`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거, arm64 16KB 페이지 정렬)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v3.5.0 교차로·중앙선·합류로 차선 전면 재설계 · Android 90° 회전 핫픽스 · 고속도로 vs 시내도로 구분)

0. **무결점 교차로 아스팔트 박스 · 이중 황색 중앙선 밀착 · 실제 교차로 횡단보도 & Z-파이팅 0% (`world.frag` · `Renderer3D.cpp` · `CityMap.cpp`)**
   * 청크 16×16 공간 격자 기반 `4.8m` 서브세그먼트 교차 분석과 셰이더 4비트 도로 플래그(`isCleanInterBox` · `isInterCrosswalk` · `suppressLeftEdge` · `suppressRightEdge`)를 도입해, 강남역 8차선 사거리 등 복합 교차로 내부의 직교 차선 얽힘·우회전/유턴 연결로 대각선 황색 V-선을 100% 제거했습니다.
   * 테헤란로·강남대로 등 왕복 분리 간선대로의 물리적 겹침(유령 황색선)과 미세 회색 틈을 비례 밀착 및 2패스 스무딩으로 완벽 해소해 끊김 없는 정통 한국형 **중앙 이중 황색선**을 구현하고, 실제 교차로 진입부에만 한국 규격 횡단보도를 배치했으며, 도로별 마이크로 고도 오프셋으로 동급 교차로 Z-파이팅 반짝임을 완전 차단했습니다.
   * 실제 OSM 지하철 출입구(`5.5×2.7m`)와 버스 정류장(`4.8×1.7m`)의 4개 모서리가 차도나 교차로 코너를 침범하지 않도록 4코너 풋프린트 인도 후퇴 검사를 적용했습니다.
1. **Android 스마트폰 Vulkan 90° 회전·화면비 찌그러짐 핫픽스 & Android 11+ 전체화면 (`v3.4.2`)**
   * 세로 패널 기반 스마트폰(갤럭시 등)에서 가로 모드 실행 시 `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` + 방향 일치 Extent 가드 + Android `VK_SUBOPTIMAL_KHR` 무중단 프레젠트 및 Android 11+ `WindowInsetsController` 몰입형 전체화면을 적용했습니다.
2. **한국 고속도로 전용 3D 노면·방호벽·방음벽·초록 갠트리 & 셰이더·AI 차선 1:1 통합 (`v3.4.0 ~ v3.4.1`)**
   * 경부고속도로·올림픽대로·강변북로·내부순환로·동부간선도로 878개 구간(`roadClass <= 1`) 저소음 SMA 아스팔트 · `1.15m` 갓길 · 8m/12m 고속도로 점선 · 야광 표지병 · 뉴저지 방호벽/현광방지망/방음벽 · 초록 트러스 갠트리(보행자/신호등/횡단보도 0개) vs 시내 파랑 갠트리 · 에메랄드 그린 2D 지도 & 실시간 도로 등급 배지, 단일 [`computeRoadLaneLayout`](src/world/RealSeoulData.hpp) 차선 중심 동기화.
3. **DPI·해상도 독립 반응형 모바일 UI · 실제 OSM 서울 + SRTM 30m · 32개 자동화 테스트 (`v3.0 ~ v3.3` 기반)**
   * 태블릿·폴더블·스마트폰 DPI 반응형 모바일 UI(겹침 0% · 터치 지도 드래그/핀치 줌) · 교차로 보행자 인도 엄수 & 버스 OBB 양보 AI · 도로 침범 지하상가 0% · **32개 자동화 테스트(Release + ASan/UBSan) 100% 통과**.

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
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust) v3.5.0
├── assets/
│   ├── maps/seoul_real.ocmap            # 실제 OSM 서울 25,295 도로 · 107,600 건물 · 3,581 수계 · 17,931 공원 · SRTM 30m DEM
│   ├── maps/seoul_map_2d.png            # 2048×1312 실제 서울 카토그래피 미니맵 & 전체 지도 텍스처 (고속도로 에메랄드 그린 강조)
│   └── textures/pbr_{albedo,normal_rough}.png # 2048×2048 16타일 밉맵·8×이방성 PBR 알베도 & 탄젠트 노멀/러프니스 아틀라스
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v3.5.0 / 350)
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
│   ├── world/RealSeoulData.*            # OCM3 실제 OSM/SRTM GIS 파서 · Bare-Earth DTM · 도로 침범 지하상가 필터 · O(1) 충돌
│   ├── world/CityMap.*                  # 실제 OSM 서울 & 12대 랜드마크 · 한강 대교 · 고밀도 서울 스트리트 구조물 스트리밍
│   ├── gameplay/                        # 차량 17종(서울 시내버스 포함)·무기 11종·3.25m 미터법 차로·교차로 OBB 양보 AI·보행자 인도 엄수
│   ├── audio/SynthAudio.*               # miniaudio 락프리 실시간 엔진/드리프트/사이렌/총성/빗소리 신시사이저
│   ├── render/
│   │   ├── VulkanContext.*              # Native Vulkan 1.0+ 렌더러 (HDR16·MSAA·2048px 섀도우·8× 이방성 밉맵 PBR·DPI 논리 UI)
│   │   ├── shaders/*.{vert,frag}        # GLSL 450 카메라 상대 고정밀 뎁스·PBR/노멀맵/3.25m 미터법 도로/블룸+ACES/2D UI 셰이더 10종
│   │   ├── VulkanShadersSpv.hpp         # 사전 컴파일된 SPIR-V 1.0 바이트코드
│   │   ├── CarMeshGenerator.*           # 17종 Zero-Jelly CAD 차량 & 10-스포크 휠 생성기
│   │   └── Renderer3D.*                 # 실제 OSM 5×5 청크 스트리밍 · 코너별 DEM 도로 · 옥상 기계실 · 3D 한글 간판 · 16종 도시 구조물
│   ├── ui/                              # Vulkan2D (44px 2048px 한글 & 실제 서울 2D 지도), HudRenderer, DPI 반응형 TouchControls
│   └── main.cpp                         # 데스크톱 main() + Android android_main() 통합 실행 루프
└── tests/
    └── test_simulation.cpp              # 실제 OSM/SRTM 서울 GIS·모바일 DPI UI·교차로 보행자/버스 양보 등 30개 자동 테스트 스위트
```
