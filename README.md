# Open City Seoul — Native Vulkan (`open-city-cpp`) v6.0.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 카메라 상대 고정밀 뎁스 · DPI 독립 반응형 모바일 HUD · 16-bit HDR + 4× MSAA + 2048px Soft PCF 그림자 + 듀얼 4096×4096 8× 이방성 밉맵 건축/도로 PBR 아틀라스 + 4096×4096 64슬롯 4K 한글 간판·이정표 + 3D 인테리어 패럴랙스 매핑 + 실시간 동적 20광원 PBR) + DLR Eclipse SUMO 단일 토폴로지 사전 베이킹 3D 도로·교차로·보도·차선 폴리곤 메쉬 엔진 (`OCRM v1`, `assets/maps/seoul_roads_hd.ocrm`, 국토지리정보원 정밀도로지도 NGII HD Map 및 행정안전부 실폭도로 `TL_SPRD_RW` 공식 SHP 임포터 내장!) + Overture Maps · OpenStreetMap · TUM GlobalBuildingAtlas · AWS Terrarium 15m 베어-어스 DTM 융합 서울 3D 월드 (`OCM6 v4`) + Rust (`#![no_std]` 임계감쇠 서스펜션 물리 커널 v2000)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실제 지도·단일 토폴로지 정밀도로·실제 건축 기반 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **단일 토폴로지 정밀 3D 도로·교차로·보도·차선 메쉬 (`assets/maps/seoul_roads_hd.ocrm` OCRM v1 · `26.6 MB`)**: 런타임 겹침 도로 리본 방식을 전면 대체하여 **DLR Eclipse SUMO `netconvert` + Shapely 2D 불리언 유니온 + `mapbox_earcut` + SoA+Delta16 LZ4**로 서울 전역 **3,161개 청크 (`934,324` 도로/교차로 삼각형 · `817,784` 인도/화강암 경계석 삼각형 · `1,302,398` 한국형 차선·버스전용차로·횡단보도·정지선 · `496,576` 고속도로 뉴저지 방호벽·방음벽·고가교)** 를 사전 베이킹했습니다. 강남역 사거리 같은 10×10차로 복선 교차로도 **단 하나의 곡선 통합 폴리곤**으로 합쳐지며 보도는 수학적 차집합 `Buffer(Road) \ RoadUnion \ Buildings`로 계산해 **교차로 보도 침범 0.00% · 도로 겹침 Z-fighting 0.00%**를 보장합니다.
- **대한민국 공식 정부 정밀도로지도 / 실폭도로 SHP 원클릭 임포터 (`tools/import_korea_hdmap_ocrm.py`)**: 국토지리정보원 정밀도로지도(**NGII HD Map**: `A3_DRIVEWAYSECTION.shp`, `B2_SURFACELINEMARK.shp`, `B3_SURFACEMARK.shp`, `A2_LINK.shp`), 행정안전부 도로명주소 **실폭도로(`TL_SPRD_RW.shp`)**, 1:1000 연속수치지형도(`A001`/`A003`/`A004`)를 좌표계 자동 인식(`EPSG:5179`/`5186`/`5174`/`32652`/`4326`)으로 기존 `seoul_roads_hd.ocrm` 위에 청크 단위 패치할 수 있습니다.
- **실제 서울 초정밀 3D 지형·건축 (`assets/maps/seoul_real.ocmap` OCM6 v4 · `seoul_map_2d.png` 4096×2624)**: **AWS Terrarium 15m Bare-Earth DTM (`2048×1638`, 4cm 수직 정밀도) + Overture Maps + OpenStreetMap + TUM GlobalBuildingAtlas** 융합으로 **184,254채 실제 서울 3D 건물**, **59,558개 실측 POI**, 한강/지천/석촌호수 **3,581개**, 공원 **17,931개**를 프레임 예산 스트리밍으로 구현했습니다.
- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명 + 16KB ELF 페이지 정렬된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·폴더블·태블릿에서 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임 없이 압축 해제 후 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v6.0.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v6.0.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v5.x / v4.x / v3.x / v2.x / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v6.0.0`, `versionCode 600`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거, arm64 16KB 페이지 정렬)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v6.0.0 Eclipse SUMO 단일 토폴로지 3D 도로·교차로 엔진 `OCRM v1` & 한국 공식 정밀도로지도/실폭도로 SHP 임포터)

0. **런타임 도로 리본 전면 폐기 -> Eclipse SUMO + Shapely 단일 토폴로지 3D 도로·교차로 메쉬 (`OCRM v1` · `OcrmRoadMesh.cpp` · `build_seoul_sumo_ocrm.py`)**
   * **교차로 보도·중앙분리대 침범 0.00%**: 강남역 사거리 등 대형 복선 교차로 한가운데를 회색 보도/중앙분리대가 가로지르던 구조적 한계를 근본적으로 해결! SUMO `netconvert`가 모든 다차로 교차로를 곡선 코너가 살아있는 **단일 통합 교차로 폴리곤**으로 합치고, 보도는 수학적 차집합(`Buffer \ RoadUnion \ Buildings`)으로 생성해 교차로 침범과 도로 Z-fighting을 **수학적으로 0%**로 만들었습니다.
   * **초경량 무손실 `SoA+Delta16` + 제로 의존성 C++20 LZ4 디코더**: 서울 전역 3,161청크·355만+ 폴리곤을 단 **`26.6 MB`** 로 압축(Android/Windows/Linux 외부 라이브러리 의존성 0개!)하고, 전용 Vulkan 셰이더 `MatId::RoadPoly`(포천 쇄석 아스팔트 / 고속도로 SMA / 교차로 / 우천 회전 보로노이 물웅덩이) + `MatId::RoadPaint`(황색 중앙선 · 파란 버스전용차로 · 백색 점선 · 횡단보도 · 정지선) + 월드 공간 화강암 연석·점자블록으로 렌더링합니다.
1. **한국 정부 공식 정밀도로지도(NGII HD Map) · 행정안전부 실폭도로(`TL_SPRD_RW`) 원클릭 패치 (`tools/import_korea_hdmap_ocrm.py`)**
   * 국토지리정보원([map.ngii.go.kr](https://map.ngii.go.kr))의 정밀도로지도(`A3_DRIVEWAYSECTION` / `B2_SURFACELINEMARK` / `B3_SURFACEMARK`) 또는 행정안전부([business.juso.go.kr](https://business.juso.go.kr))의 실폭도로(`TL_SPRD_RW.shp`) 압축파일/폴더를 내려받아 아래 명령 한 줄로 기존 `seoul_roads_hd.ocrm` 위에 즉시 패치할 수 있습니다:
   ```bash
   uv run --with pyshp --with shapely --with mapbox-earcut --with pyproj --with scipy --with numpy --with lz4 \
     python3 tools/import_korea_hdmap_ocrm.py --input ~/Downloads/NGII_HDMap_Seoul.zip \
     --patch assets/maps/seoul_roads_hd.ocrm --out assets/maps/seoul_roads_hd.ocrm
   ```
2. **전통 유산 랜드마크 전용 포천 화강암 PBR · 실제 서울 신호 준수 교통 AI · 38개 자동화 검증 스위트**
   * 경복궁·광화문·숭례문 전용 포천 화강암 PBR(`MatId::LandmarkStone`), 2중 회전 보로노이 야간 빗방울 파문, 실제 서울 교차로 신호 준수 교통 AI, 그리고 강남역 사거리 교차로 중앙 보도 정점 0개 수학 검증을 포함한 **38개 자동화 테스트(Release + ASan/UBSan) 100% 통과**!

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
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust) v6.0.0
├── assets/
│   ├── maps/seoul_real.ocmap            # OCM6 v4 실제 서울 15m DTM(2048×1638) · 50,253 곡선 3D 도로 · 184,254 3D 건물 · 59.5K 실측 POI
│   ├── maps/seoul_roads_hd.ocrm         # OCRM v1 Eclipse SUMO 단일 토폴로지 3D 도로·교차로·인도·차선 메쉬 (3,161청크 · SoA+Delta16 LZ4 26.6 MB)
│   ├── maps/seoul_map_2d.png            # 4096×2624 실제 서울 카토그래피 미니맵 & 전체 지도 텍스처 (고속도로·순환도로 에메랄드 그린)
│   └── textures/
│       ├── pbr_{albedo,normal_rough}.png    # 4096×4096 16타일 밉맵·8×이방성 4K 건축 PBR 알베도 & 노멀/러프니스 아틀라스
│       ├── street_{albedo,normal_rough}.png # 4096×4096 16타일 전용 서울 도로·거리 PBR (포천 쇄석 아스팔트·SMA·화강암경계석·점자블록·보도블록)
│       └── seoul_signs_4k.png               # 4096×4096 64슬롯 4K 한글 간판·이정표 (고속도로/시내 갠트리 16 · 한국 상가 32 · 지하철 8 · 번호판)
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v6.0.0 / 600)
│   └── src/com/stickmongus/opencity/
│       └── NativeLoader.java            # 몰입형 전체화면 + 디스플레이 컷아웃 + libopen_city.so 로더
├── tools/
│   ├── build_android_apk.sh             # NDK CMake + aapt2 + d8 + zipalign + apksigner v1/v2/v3 원스톱 빌더
│   ├── build_seoul_sumo_ocrm.py         # Eclipse SUMO netconvert + Shapely 단일 토폴로지 3D 서울 도로·교차로·보도·차선 컴파일러 (v6.0.0)
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
│   ├── core/                            # Math.hpp (2D/3D 수학, SAT OBB, 레이캐스트), Mat4.hpp (Vulkan 행렬)
│   ├── platform/PlatformApp.*           # 크로스플랫폼 윈도우/입력/Vulkan Surface/Android Asset 로더
│   ├── physics/                         # Rust (#![no_std]) 임계감쇠 서스펜션 물리 커널 v2000 & C++20 완전 패리티 브리지
│   ├── world/OcrmRoadMesh.*             # 제로 의존성 C++20 OCRM v1 로더 & SoA+Delta16 LZ4 블록 디코더
│   ├── world/RealSeoulData.*            # OCM6 v4 15m 바이큐빅 DTM · 리본 투영 C¹ 노면 쿼리 · 다층 surfaceHeightAt · O(1) 충돌
│   ├── world/CityMap.*                  # 실제 OSM/Overture 서울 12대 랜드마크 · 한강 대교 · 실측 교차로/상권 구조물 스트리밍
│   ├── gameplay/                        # 차량 17종(서울 시내버스 포함)·무기 11종·3.25m 미터법 차로·교차로 신호 준수 AI·보행자 인도 엄수
│   ├── audio/SynthAudio.*               # miniaudio 락프리 실시간 엔진/드리프트/사이렌/총성/빗소리 신시사이저
│   ├── render/
│   │   ├── VulkanContext.*              # Native Vulkan 1.0+ 렌더러 (HDR16·MSAA·2048px 섀도우·듀얼 4K PBR·8× 이방성 밉맵·DPI UI)
│   │   ├── shaders/*.{vert,frag}        # GLSL 450 카메라 상대 고정밀 뎁스·OCRM RoadPoly/RoadPaint·듀얼 4K PBR·블룸+ACES 셰이더 10종
│   │   ├── VulkanShadersSpv.hpp         # 사전 컴파일된 SPIR-V 1.0 바이트코드
│   │   ├── CarMeshGenerator.*           # 17종 Zero-Jelly CAD 차량 & 10-스포크 휠 생성기
│   │   └── Renderer3D.*                 # OCRM v1 단일 토폴로지 5×5 청크 스트리밍 · 32×32 지반 함몰 · 64슬롯 4K 한글 간판
│   ├── ui/                              # Vulkan2D (44px 2048px 한글 & 4K 서울 2D 지도), HudRenderer, DPI 반응형 TouchControls
│   └── main.cpp                         # 데스크톱 main() + Android android_main() 통합 실행 루프 & 수직 카메라 스무딩
└── tests/
    └── test_simulation.cpp              # OCRM v1 단일 토폴로지·강남역 교차로 침범 0% · OCM6 노면 연속성 · 38개 자동 테스트 스위트
```
