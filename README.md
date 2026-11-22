# Open City Seoul — Native Vulkan (`open-city-cpp`) v5.1.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 카메라 상대 고정밀 뎁스 · DPI 독립 반응형 모바일 HUD · 16-bit HDR + 4× MSAA + 2048px Soft PCF 그림자 + 듀얼 4096×4096 8× 이방성 밉맵 건축/도로 PBR 아틀라스 + 4096×4096 64슬롯 4K 한글 간판·이정표 + 3D 인테리어 패럴랙스 매핑 + 실시간 동적 20광원 PBR) + 실제 Overture Maps · OpenStreetMap · TUM GlobalBuildingAtlas 위성 고도 · AWS Terrarium 15m 베어-어스 DTM 융합 서울 초정밀 3D 도로·거리·건축 월드 (`OCM6 v4`) + Rust (`#![no_std]` 임계감쇠 서스펜션 물리 커널 v2000) + Python 3 (OCM6 15m DTM·양방향 대칭 대로 평탄화·곡선 도로·상권 GIS · 4K PBR · SPIR-V 셰이더 빌더)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실제 지도·초정밀 도로·실제 건축 기반 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **실제 서울 초정밀 3D 도로·거리 & 3D 건축 1:1 반영 (`assets/maps/seoul_real.ocmap` OCM6 v4 · `seoul_map_2d.png` 4096×2624)**: **AWS Terrarium 15m Bare-Earth DTM (`2048×1638`, 4cm 수직 정밀도) + Overture Maps (2026-11-18) + OpenStreetMap + TUM GlobalBuildingAtlas** 를 융합해 **50,253개 캣멀-롬 곡선 3D 서울 도로(`780,179` 3D 정점 · 전역 C² 종단선형 스무딩 · 양방향 28m 대칭 대로 평탄화 · 도로 하부 8m 정밀 지반 함몰로 보도 침범 0% · 리본 투영 C¹ 노면 보간 · 덜컹거림 0%)**, **184,254채 실제 서울 3D 건물**, **59,558개 실측 POI** *(지하철·버스 5,632 + 실측 신호 교차로 5,251 + 실제 Overture 서울 상권 48,675)*, 한강/지천/석촌호수 **3,581개**, 공원 **17,931개**를 프레임 예산 스트리밍으로 구현했습니다.
- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인(`10`개 사전 컴파일 SPIR-V 1.0 셰이더 + **`4096×4096`** 4K 건축 PBR + **`4096×4096`** 전용 서울 도로·거리 PBR + **`4096×4096`** 64슬롯 4K 한글 간판·이정표 아틀라스 + **`4096×2624`** 실제 서울 2D 지도 텍스처)으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명 + 16KB ELF 페이지 정렬된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·폴더블·태블릿에서 DPI 반응형 모바일 UI & 10-포인트 멀티터치로 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임 없이 압축 해제 후 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v5.1.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v5.1.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v5.0 / v4.x / v3.x / v2.x / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v5.1.0`, `versionCode 510`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거, arm64 16KB 페이지 정렬)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v5.1.0 서울 대로 지반 침범 0% · 양방향 평탄화 · 주간 아스팔트·갠트리·교통 완전 정비 · `OCM6 v4`)

0. **도로 위 지반 돌출 0% · 양방향 대로 단차 완전 평탄화 (`tools/polish_ocm6_twin_boulevards.py` · `Renderer3D.cpp`)**
   * **8m 도로 인식 지반 메시 & 원경 지형 정밀 함몰**: 청크 지반을 8.0m(`32×32`)로 고해상도화하고 모든 지상 도로 하부(`ohw+5m..8.5m`)를 수학적으로 노면 아래 `-0.52m`로 함몰시켜 테헤란로·강남대로·역삼 언덕 등에서 회색 보도블록이나 거친 원경 지형 삼각형이 아스팔트를 침범하는 현상을 **0.00%**로 완전 제거했습니다.
   * **양방향 대로 평탄화 & 정밀 중앙분리대**: `query_pairs(r<=28m)` 양방향 대로 평탄화 스프링으로 테헤란로 동·서행 고도 단차를 최대 `4.30m -> 0.39m` *(중앙값 4cm)* 로 평탄화하고, 중앙분리대 폭을 실제 간격에 1:1 밀착·접지(`-0.28m`)했습니다.
1. **주간 4K 풍화 아스팔트 · 깔끔한 횡단보도 · 비가림 4K 갠트리 · 현실 교통 배치 (`world.frag` · `CityMap.cpp` · `GameWorld.cpp`)**
   * **현실적인 서울 주간 아스팔트 & 횡단보도**: 과도하게 새까맣던 아스팔트 알베도를 실제 햇빛 아래 풍화된 서울 화강암 골재 차콜 톤으로 보정해 4K 질감과 고속도로 SMA 대비를 선명히 드러내고, 횡단보도 구간의 노란선 겹침을 깔끔히 분리했습니다.
   * **4K 갠트리 시야 확보 & 대로 전용 대형차 스폰**: 가로등·배너·가로수가 4K 이정표(`강남대로·테헤란로`)를 가리지 않도록 18m 시야 확보, 중앙분리대 보행자 가드레일 제거, 서울 시내버스·소방차의 간선대로 전용 스폰 및 보행자 7m 안전 이격, 자연스러운 서울 가로수 수관 색조 다양화를 적용했습니다.
2. **실제 서울 초정밀 3D 도로·거리 4K PBR & 184,254채 3D 건축 · 36개 자동화 테스트 100% 통과**
   * AWS Terrarium 15m DTM + 캣멀-롬 곡선 3D 도로 + 임계감쇠 4륜 서스펜션 + 듀얼 4K PBR + 64슬롯 4K 한글 간판 + 184,254채 실제 서울 3D 건물 + **36개 자동화 테스트(Release + ASan/UBSan) 100% 통과** · Android 16KB ELF · Native Vulkan 1.0+!

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
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust) v5.1.0
├── assets/
│   ├── maps/seoul_real.ocmap            # OCM6 v4 실제 서울 15m DTM(2048×1638) · 50,253 곡선 3D 도로(780K 정점 · 양방향 평탄화) · 184,254 3D 건물 · 59.5K 실측 POI
│   ├── maps/seoul_map_2d.png            # 4096×2624 실제 서울 카토그래피 미니맵 & 전체 지도 텍스처 (고속도로·순환도로 에메랄드 그린)
│   └── textures/
│       ├── pbr_{albedo,normal_rough}.png    # 4096×4096 16타일 밉맵·8×이방성 4K 건축 PBR 알베도 & 노멀/러프니스 아틀라스
│       ├── street_{albedo,normal_rough}.png # 4096×4096 16타일 전용 서울 도로·거리 PBR (SMA·신축이음·맨홀·화강암경계석·점자블록·보도블록)
│       └── seoul_signs_4k.png               # 4096×4096 64슬롯 4K 한글 간판·이정표 (고속도로/시내 갠트리 16 · 한국 상가 32 · 지하철 8 · 번호판)
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v5.1.0 / 510)
│   └── src/com/stickmongus/opencity/
│       └── NativeLoader.java            # 몰입형 전체화면 + 디스플레이 컷아웃 + libopen_city.so 로더
├── tools/
│   ├── build_android_apk.sh             # NDK CMake + aapt2 + d8 + zipalign + apksigner v1/v2/v3 원스톱 빌더
│   ├── polish_ocm6_twin_boulevards.py   # 양방향 대칭 대로(28m) 평탄화 & 전폭 도로-지형 카빙 포스트폴리셔 (v5.1.0)
│   ├── build_seoul_ocm6.py              # AWS Terrarium 15m DTM + Overture 상권 83K + 캣멀-롬 곡선 3D 도로 -> OCM6 v4 & 4096px 지도
│   ├── build_seoul_ocm5_roads.py        # Overture Maps 2026-11-18 3D 교통 + OCM4 184K 건물 베이스 빌더
│   ├── build_seoul_ocm4.py              # Overture Maps + OSM + TUM GlobalBuildingAtlas + SRTM -> OCM4 v2 3D 건축 베이스
│   ├── gen_street_pbr_textures.py       # 4096×4096 16타일 전용 서울 도로·거리 PBR 아틀라스 생성기
│   ├── gen_seoul_signs_4k.py            # 4096×4096 64슬롯 4K 한글 간판·고속도로/시내 갠트리·지하철 캐노피 생성기
│   ├── gen_pbr_textures.py              # 4096×4096 16타일 4K 건축 PBR 아틀라스 생성기
│   ├── compile_vulkan_shaders.py        # GLSL 450 -> SPIR-V 1.0 C++ 헤더 컴파일러
│   ├── gen_korean_glyphs.py             # 한글 UI 글리프 코드포인트 자동 추출기
│   └── gen_android_icons.py             # Android 런처 아이콘 생성기
├── src/
│   ├── core/                            # Math.hpp (2D/3D 수학, SAT OBB, 레이캐스트), Mat4.hpp (Vulkan 행렬)
│   ├── platform/PlatformApp.*           # 크로스플랫폼 윈도우/입력/Vulkan Surface/Android Asset 로더
│   ├── physics/                         # Rust (#![no_std]) 임계감쇠 서스펜션 물리 커널 v2000 & C++20 완전 패리티 브리지
│   ├── world/RealSeoulData.*            # OCM6 v4 15m 바이큐빅 DTM · 리본 투영 C¹ 노면 쿼리 · 다층 surfaceHeightAt · O(1) 충돌
│   ├── world/CityMap.*                  # 실제 OSM/Overture 서울 12대 랜드마크 · 한강 대교 · 실측 교차로/상권 구조물 스트리밍
│   ├── gameplay/                        # 차량 17종(서울 시내버스 포함)·무기 11종·3.25m 미터법 차로·교차로 OBB 양보 AI·보행자 인도 엄수
│   ├── audio/SynthAudio.*               # miniaudio 락프리 실시간 엔진/드리프트/사이렌/총성/빗소리 신시사이저
│   ├── render/
│   │   ├── VulkanContext.*              # Native Vulkan 1.0+ 렌더러 (HDR16·MSAA·2048px 섀도우·듀얼 4K PBR·8× 이방성 밉맵·DPI UI)
│   │   ├── shaders/*.{vert,frag}        # GLSL 450 카메라 상대 고정밀 뎁스·듀얼 4K PBR·노면화살표/신축이음/맨홀·블룸+ACES 셰이더 10종
│   │   ├── VulkanShadersSpv.hpp         # 사전 컴파일된 SPIR-V 1.0 바이트코드
│   │   ├── CarMeshGenerator.*           # 17종 Zero-Jelly CAD 차량 & 10-스포크 휠 생성기
│   │   └── Renderer3D.*                 # 실제 OCM6 5×5 청크 스트리밍 · 24×24 서브베이스 · 턱낮춤 경계석 · 64슬롯 4K 한글 간판
│   ├── ui/                              # Vulkan2D (44px 2048px 한글 & 4K 서울 2D 지도), HudRenderer, DPI 반응형 TouchControls
│   └── main.cpp                         # 데스크톱 main() + Android android_main() 통합 실행 루프 & 수직 카메라 스무딩
└── tests/
    └── test_simulation.cpp              # OCM6 초정밀 노면 연속성 · 240프레임 주행 서스펜션 · 실제 서울 GIS 등 35개 자동 테스트 스위트
```
