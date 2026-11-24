# Open City Seoul — Native Vulkan (`open-city-cpp`) v6.3.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 카메라 상대 고정밀 뎁스 · DPI 독립 반응형 모바일 HUD · 16-bit HDR + 4× MSAA + 2048px 가변 0~16-Tap Soft PCF 그림자 + 듀얼 4096×4096 8× 이방성 밉맵 건축/도로 PBR 아틀라스 + 4096×4096 64슬롯 4K 한글 간판·이정표 + 3D 인테리어 패럴랙스 매핑 + 실시간 0~20광원 PBR + 세밀한 인터랙티브 한글 그래픽 상세 설정 메뉴 & 250~1600m 렌더 거리 슬라이더) + DLR Eclipse SUMO 중심정렬 단일 토폴로지 3D 도로·교차로·보도·건물전면광장 메쉬 엔진 (`OCRM v1`, 바리센트릭 3D 삼각형 정밀 접지 물리 & 3×3 인접 청크 심리스 지면 · 국토지리정보원 NGII HD Map / 행안부 실폭도로 SHP 임포터 내장!) + 전 서울 포렌식 정비 Geofabrik OSM · Overture Maps · TUM GlobalBuildingAtlas 185,917채 실제 서울 3D 건물 & AWS Terrarium 15m 베어-어스 DTM 융합 월드 (`OCM6 v4`) + 12종 실물 조각 한국 거리 구조물(따릉이·서울 LED 가로등·4구 후드 신호등·유리 버스정류장·아치형 지하철 출입구·24지주 보행자 가드레일·목재 벤치·소화전·분리수거함·한전 배전함) + Rust (`#![no_std]` 임계감쇠 서스펜션 물리 커널 v2000)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실제 지도·단일 토폴로지 정밀도로·실제 건축 기반 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **전 서울 건물 정밀정렬 & 지형 뚫림 0.00% 완전 정비 (`v6.3.0`)**: 서울 전역 750km² **185,917채 건물·48,380개 도로·99,907개 공원 삼각형·120m 광역 산지/지형·4m 정밀 청크 지면**을 전수 포렌식 감사하여 ① 간선·고속도로·보도·한강 침범 건물 **0채**(이설·재클리핑 `35,829채` · 간선 아스팔트/보도/수면 침범 **0건**), ② 1층 상가 매몰(`-62.5m` AABB 코너 버그)을 **폴리곤 정점+도로높이 정밀 접지**로 해소하고 모든 비탈길 건물에 **한국산 혼드 화강암 기초 기단(Plinth Skirt)** 을 동적 연장해 공중 부양 **0채**, ③ **광화문·세종대왕상·이순신장군상 11~12.5m 공중 부양**을 광장 실표고로 밀착 접지, ④ 120m 광역 지형(`9,707건`)·OSM 공원(`405건`)·4m 로컬 지면의 도로/보도 관통 및 경부고속도로 지면 구멍(`22건`)·해발 88m 절벽 찢어짐을 **전부 0건**으로 완전 소거했습니다.
- **실물 조각 한국 거리 구조물 12종 & 바리센트릭 OCRM 3D 정밀 접지 (`v6.2.0`)**: 따릉이(스포크 휠 자전거 4대+거치대+QR 키오스크)·서울형 곡선 LED 가로등·4구 후드 신호등+도로명판+보행자 신호기·캔틸레버 유리 버스정류장+BIT 단말기·아치형 유리 지하철 출입구+계단+스테인리스 난간·24지주 가드레일 등 **12종 한국 거리 구조물을 전용 GPU 메쉬로 사전 베이킹**하고 OCRM 3D 삼각형 바리센트릭 접지를 적용했습니다.
- **세밀한 실시간 한글 `그래픽 상세 설정` 메뉴 (`[U]` / 모바일 `그래픽` 버튼)**: 4대 프리셋(`울트라` / `높음` / `보통` / `성능 우선` + `사용자 지정`)과 **렌더 거리(`250m ~ 1600m`)** · 건물/스카이라인 디테일 · 거리 구조물/가로수 밀도 · 그림자 품질(`끄기 ~ 16-Tap Soft PCF`) · 동적 광원/HDR 블룸(`0~20광원`) · 반사 & 3D 실내 패럴랙스 · 대기 안개 · 실시간 교통량/인파 밀도 · 실시간 FPS 표시를 게임 도중 즉시 조절할 수 있습니다.
- **대한민국 공식 정부 정밀도로지도 / 실폭도로 SHP 원클릭 임포터 (`tools/import_korea_hdmap_ocrm.py`)**: 국토지리정보원 정밀도로지도(**NGII HD Map**: `A3_DRIVEWAYSECTION.shp`, `B2_SURFACELINEMARK.shp`, `B3_SURFACEMARK.shp`, `A2_LINK.shp`), 행정안전부 도로명주소 **실폭도로(`TL_SPRD_RW.shp`)**, 1:1000 연속수치지형도를 기존 `seoul_roads_hd.ocrm` 위에 청크 단위 패치할 수 있습니다.
- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명 + 16KB ELF 페이지 정렬된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·폴더블·태블릿에서 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임 없이 압축 해제 후 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v6.3.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v6.3.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v6.x / v5.x / v4.x / v3.x / v2.x / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v6.3.0`, `versionCode 630`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거, arm64 16KB 페이지 정렬)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v6.3.0 전 서울 건물 정밀정렬 · 지형/공원/산지 관통 0.00% · 경사지 화강암 기단)

0. **전 서울 185,917채 건물 도로·보도·수면 클리핑 & 폴리곤 실표고 접지 (`polish_seoul_buildings_v63.py` · `Renderer3D.cpp`)**
   * 고속도로(`+2.35m`), 간선도로(`+2.00m`, 화강암 경계석·노란 점자블록 보행로 완전 보존), 주택가 이면도로(`+0.50m`), 한강·지천 수면(`+1.20m`)을 통합 STRtree로 클리핑해 **간선 도로·보도·한강 침범 건물 0채**를 달성했습니다.
   * 기존 AABB 모서리 최소고도 방식이 유발하던 1층 상가 매몰(`35,833채`, 최대 `-62.46m`)을 폴리곤 정점+도로높이 접지로 해소하고, 비탈길 건물 하단에 **화강암 기초 기단(`piersV`)** 을 최저 코너 `-3.5m`까지 동적 연장해 공중 부양을 **0채**로 차단했습니다.
   * **광화문·세종대왕상·이순신장군상**이 경복궁 근정전 고도(`46.13m`)를 공유해 **11~12.5m 공중에 떠 있던 버그**를 국지 광장 표고(`33.6~35.1m`)로 밀착 교정하고 모든 랜드마크에 `-4.0m` 지하 기초를 보강했습니다.
1. **광역 120m 산지·지형(`9,707 -> 0`), OSM 공원(`405 -> 0`), 4m 청크 지면 구멍(`22 -> 0`) & 88m 절벽 찢어짐 완전 소거 (`Renderer3D.cpp`)**
   * `buildSeoulMacroMeshes()`에서 모든 지상 도로 세그먼트의 최저 고도를 120m 격자 정점(`gridMinRoadY`)에 선계산해 `y <= minRdY - 4.2m`로 하강시켜, 서울 전역 120m 광역 산지·지형의 도로/보도 관통 **9,707건(최대 +23.25m)을 0건**으로 소거했습니다.
   * `realParksMesh_`를 도로 경계에서 10m까지 세분화하고 7점 OCRM 검사로 관통 삼각형을 배제해 공원 녹지 침범 **405건 -> 0건**, `buildRealSeoulChunkMesh()`에서 도로 하부 쿼드 삭제(`경부고속도로 구멍 21건`)와 `avgH >= 88m` 컷오프를 제거해 **지면 구멍 0건 · 88m 절벽 찢어짐 0건 · 3.6m 보도 캡으로 급경사지 보도 관통 0건**을 달성했습니다 (**41개 자동화 테스트 Release + ASan/UBSan 100% 통과**!).
2. **실물 조각 한국 거리 구조물 12종 & 실시간 한글 `그래픽 상세 설정` (`v6.2.0` 계승)**
   * 따릉이·서울 곡선 LED 가로등·4구 후드 신호등·유리 버스정류장·아치형 지하철 출입구·24지주 가드레일 등 12종 구조물 GPU 메쉬와 `[U]` 키(모바일 `그래픽` 버튼) **250~1600m 렌더 거리 · 0~16-Tap Soft PCF 그림자 · 0~20광원 · 교통량/인파 실시간 제어**를 그대로 제공합니다.

---

## 모바일 멀티터치 조작 안내 (Android / `--mobile-ui`)

| 영역 / 버튼 | 도보 모드 | 차량 운전 모드 |
| :--- | :--- | :--- |
| 좌측 상단 미니맵 탭 | **실제 서울 전체 지도 열기** (`OSM + SRTM 30m`, GPS · 빠른 이동) | 동일 |
| 좌측 하단 | 가상 아날로그 조이스틱 *(끝까지 밀면 자동 전력질주)* | `◀` / `▶` 부드러운 조향 패드 |
| 화면 빈 영역 드래그 | 1:1 정밀 시점 회전 | 360° 자유 시점 회전 |
| 우측 하단 주요 버튼 | `사격` · `조준` · `점프` · `장전` · `탑승/상점` | `가속` · `브레이크` · `니트로` · `드리프트` · `하차` · `경적` · `후방` |
| 상단 중앙 툴바 | `지도` · `1인칭/3인칭` · `폰` · `커스텀` · `날씨` · `시간` · **`그래픽`** | 동일 |

---

## PC 키보드 & 마우스 조작키 안내

| 입력 | 동작 |
| :--- | :--- |
| `W` `A` `S` `D` / 방향키 | 도보 이동 / 17종 차량 가속·후진·속도 연동 정밀 조향·실제 한강 대교 주행 |
| **`M` 또는 미니맵 클릭** | **실제 서울특별시 전체 지도 (`OSM + SRTM 30m`) 열기/닫기 (클릭 GPS · 빠른 이동)** |
| **`V`** | **1인칭 시점 (도보 FPS · 3D 차량 운전석) ↔ 3인칭 시점 전환** |
| **`Y`** | **맑음 ↔ 비 오는 서울 (젖은 아스팔트 노면 반사 & 시민 우산) 전환** |
| **`T`** | **시간 +2시간 이동 (낮 · 노을 · 서울 야경 즉시 전환)** |
| **`U`** | **세밀한 Vulkan `그래픽 상세 설정` 메뉴 열기/닫기 (렌더 거리 250~1600m · 그림자 · 광원 · 반사 · 안개 · 교통량 · FPS)** |
| **`P` / `K` / `E`** | **서울 스마트폰** / **커스텀 스튜디오** / **근접 상점·차고 열기** |
| `Space` / `Left Shift` | 점프 · 핸드브레이크 드리프트 / 전력질주 · 니트로 부스트 |
| `F` / `G` / `Q` / `R` | 차량 탑승·하차 / 경적·경찰 사이렌 / 후방 카메라 / 재장전 |
| `1` ~ `9`, `0` | 무기 슬롯 즉시 선택 (모달 열림 시 옵션/프리셋 선택) |
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
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust) v6.3.0
├── assets/
│   ├── maps/seoul_real.ocmap            # OCM6 v4 실제 서울 15m DTM · 48,380 3D 도로 · 185,917 전수 정비 3D 건물 · 60.7K POI (32.4 MB)
│   ├── maps/seoul_roads_hd.ocrm         # OCRM v1 Eclipse SUMO 단일 토폴로지 3D 도로·교차로·보도·건물광장 메쉬 (3,161청크 · LZ4 26.6 MB)
│   ├── maps/seoul_map_2d.png            # 4096×2624 실제 서울 카토그래피 미니맵 & 전체 지도 텍스처 (고속도로·순환도로 에메랄드 그린)
│   └── textures/
│       ├── pbr_{albedo,normal_rough}.png    # 4096×4096 16타일 밉맵·8×이방성 4K 건축 PBR 알베도 & 노멀/러프니스 아틀라스
│       ├── street_{albedo,normal_rough}.png # 4096×4096 16타일 전용 서울 도로·거리 PBR (포천 쇄석 아스팔트·SMA·화강암경계석·점자블록·보도블록)
│       └── seoul_signs_4k.png               # 4096×4096 64슬롯 4K 한글 간판·이정표 (고속도로/시내 갠트리 16 · 한국 상가 32 · 지하철 8 · 번호판)
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v6.3.0 / 630)
│   └── src/com/stickmongus/opencity/
│       └── NativeLoader.java            # 몰입형 전체화면 + 디스플레이 컷아웃 + libopen_city.so 로더
├── tools/
│   ├── build_android_apk.sh             # NDK CMake + aapt2 + d8 + zipalign + apksigner v1/v2/v3 원스톱 빌더
│   ├── polish_seoul_buildings_v63.py    # 전 서울 185,917채 건물 도로·보도·수면 STRtree 클리핑 & 폴리곤 실표고 접지 컴파일러
│   ├── sync_city_and_buildings_to_sumo.py # Geofabrik OSM 실측 건물 융합 · SUMO 도로 클리핑 · OCM6 1:1 동기화 컴파일러
│   ├── build_seoul_sumo_ocrm.py         # Eclipse SUMO netconvert + Shapely 단일 토폴로지 3D 서울 도로·교차로·보도·광장 컴파일러
│   ├── import_korea_hdmap_ocrm.py       # 국토지리정보원 정밀도로지도(NGII HD Map A3/B2/B3) · 행정안전부 실폭도로(TL_SPRD_RW) 공식 SHP 패처
│   ├── ocrm_writer.py                   # OCRM v1 공통 폴리곤 삼각분할(earcut) · SoA+Delta16 LZ4 압축 엔진
│   ├── polish_ocm6_twin_boulevards.py   # 양방향 대칭 대로/고속도로 평탄화 · 전 고도 립시츠 경사 제어 · 지형 카빙 폴리셔
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
│   ├── core/                            # Math.hpp (2D/3D 수학, SAT OBB, 레이캐스트), Mat4.hpp, GraphicsSettings.hpp (세밀한 그래픽 프리셋·슬라이더)
│   ├── platform/PlatformApp.*           # 크로스플랫폼 윈도우/입력/Vulkan Surface/Android Asset 로더
│   ├── physics/                         # Rust (#![no_std]) 임계감쇠 서스펜션 물리 커널 v2000 & C++20 완전 패리티 브리지
│   ├── world/OcrmRoadMesh.*             # 제로 의존성 C++20 OCRM v1 로더 · SoA+Delta16 LZ4 디코더 · 16청크 LRU 3D 바리센트릭 표면 샘플러
│   ├── world/RealSeoulData.*            # OCM6 v4 15m 바이큐빅 DTM · 리본 투영 C¹ 노면 쿼리 · 다층 surfaceHeightAt · O(1) 충돌
│   ├── world/CityMap.*                  # 실제 OSM/Overture 서울 12대 랜드마크 · 한강 대교 · OCRM 3D 접지 스트리밍
│   ├── gameplay/                        # 차량 17종(서울 시내버스 포함)·무기 11종·실시간 교통량/인파 밀도·교차로 신호 준수 AI·보행자 인도 엄수
│   ├── audio/SynthAudio.*               # miniaudio 락프리 실시간 엔진/드리프트/사이렌/총성/빗소리 신시사이저
│   ├── render/
│   │   ├── VulkanContext.*              # Native Vulkan 1.0+ 렌더러 (HDR16·MSAA·2048px 섀도우·듀얼 4K PBR·8× 이방성 밉맵·DPI UI)
│   │   ├── shaders/*.{vert,frag}        # GLSL 450 카메라 상대 고정밀 뎁스·가변 0~16-Tap Soft PCF·반사/안개/블룸 동적 제어 셰이더 10종
│   │   ├── VulkanShadersSpv.hpp         # 사전 컴파일된 SPIR-V 1.0 바이트코드
│   │   ├── CarMeshGenerator.*           # 17종 Zero-Jelly CAD 차량 & 10-스포크 휠 생성기
│   │   └── Renderer3D.*                 # OCRM v1 3×3 심리스 지면 · 녹지 언덕 · 12종 실물 조각 한국 거리 구조물 · 가변 렌더 거리(250~1600m)
│   ├── ui/                              # Vulkan2D (44px 2048px 한글 & 4K 서울 지도), HudRenderer (그래픽 상세 설정 모달 + FPS), TouchControls
│   └── main.cpp                         # 데스크톱 main() + Android android_main() 통합 실행 루프 & 수직 카메라 스무딩
└── tests/
    └── test_simulation.cpp              # OCRM 3D 바리센트릭 접지 · 그래픽 설정/렌더 거리 · 40개 자동 테스트 스위트
```
