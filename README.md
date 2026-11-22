# Open City Seoul — Native Vulkan (`open-city-cpp`) v4.0.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 카메라 상대 고정밀 뎁스 · DPI 독립 반응형 모바일 HUD · 16-bit HDR + 4× MSAA + 2048px Soft PCF 그림자 + 4096×4096 8× 이방성 밉맵 PBR 텍스처/노멀맵 + 3D 인테리어 패럴랙스 매핑 + 실시간 동적 20광원 PBR) + 실제 Overture Maps · OpenStreetMap · TUM GlobalBuildingAtlas 위성 고도 · AWS SRTM 30m DEM 융합 서울 3D 건축 월드 + Rust (`#![no_std]` 네이티브 물리 커널 v2000) + Python 3 (OCM4 3D GIS · 4K PBR 아틀라스 · SPIR-V 셰이더 빌더)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실제 지도·실제 건축 기반 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **실제 서울 3D 건축 & 지도 1:1 반영 (`assets/maps/seoul_real.ocmap` OCM4 v2 · `seoul_map_2d.png`)**: **Overture Maps (2026-11-18) + OpenStreetMap + TUM GlobalBuildingAtlas 3m 위성 고도(131K 실측)** 융합 **184,308채 실제 서울 3D 건물** *(92.7% 실측/위성 보정 높이 · 최대 48정점 정밀 폴리곤 921,906정점 · 236개 다층 스텝 3D 빌딩 파트 · 10종 한국 건축 스타일 · 건물별 24-bit 실제 외장 RGB · 7종 3D 지붕[박공/모임/피라미드/돔/외쪽/스텝 크라운/평지붕 파라펫·헬기장·항공장애등] · 도로 침범 건물 0%)*, 실제 서울 전역 도로 **25,295개**, 한강/지천/석촌호수 **3,581개**, 공원 **17,931개**, 지하철·버스 POI **5,632개**, AWS **SRTM 30m DEM** 을 프레임 예산 스트리밍으로 구현했습니다.
- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인(`10`개 사전 컴파일 SPIR-V 1.0 셰이더 + **`4096×4096`** 4K PBR 알베도/노멀·러프니스 아틀라스 + **`3072×1968`** 실제 서울 지도 텍스처 + `1024×1024` 3D 한글 간판·고속도로/시내 갠트리 아틀라스)으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명 + 16KB ELF 페이지 정렬된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·폴더블·태블릿에서 DPI 반응형 모바일 UI & 10-포인트 멀티터치로 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임 없이 압축 해제 후 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v4.0.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v4.0.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v3.x / v2.x / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v4.0.0`, `versionCode 400`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거, arm64 16KB 페이지 정렬)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v4.0.0 실제 서울 3D 건축 18.4만 채 대개편 · 4K PBR + 3D 인테리어 패럴랙스 · 도로 침범 건물 0%)

0. **실제 서울 3D 건축 184,308채 (`OCM4 v2`) · 92.7% 위성 실측 높이 · 7종 3D 지붕 & 다층 스텝 타워 (`tools/build_seoul_ocm4.py` · `RealSeoulData.*` · `Renderer3D.cpp`)**
   * **Overture Maps (2026-11-18) + OpenStreetMap + TUM GlobalBuildingAtlas(3m 위성 고도)** 를 융합해 기존 10.7만 채에서 **184,308채**(정밀 48정점 풋프린트, 921,906정점, 236개 `building:part` 다층 스텝 고층 타워)로 대폭 확장하고, 92.7%의 건물에 실제/위성 보정 높이를 적용했습니다.
   * **10종 한국 건축 스타일**(아파트·블루/틸 글래스·다크 차콜/브론즈 메탈릭 타워·화강석 오피스·성수/홍대 벽돌·모던 스톤/스터코 상가·전통 한옥 기와·학교/병원/관공서·산업/스타디움·한국형 빌라/주택), **건물별 실제 24-bit 외장 RGB**, **7종 3D 지붕**(박공·모임·피라미드·돔·외쪽·스텝 크라운·평지붕 3D 파라펫/헬기장/항공장애등) 및 1층/2층 **3D 포디움 코니스 돌출 몰딩**을 생성합니다.
   * **도로 침범 건물 0% 완전 제거**: 선릉역 테헤란로 버스전용차로를 가로막던 지하철 출구 지붕(`bldg #40410`)을 포함한 1,432개 지붕/캐노피/지하구조물/도로침범 객체를 완전 제거하고 2,113개 모서리 건물을 차도 경계선 밖으로 정밀 클리핑했으며, 런타임 풀-폭 도로 방어막과 `minHeight > 3m` 스카이브릿지 하부 주행 통과 물리까지 완성했습니다.
1. **4096×4096 4K PBR 아틀라스 · 3D 인테리어 패럴랙스 매핑 · 주간 노란 창문 버그 제거 (`gen_pbr_textures.py` · `world.frag`)**
   * 16타일 PBR 텍스처를 `4096×4096`(타일당 `1024px`)로 4배 고해상도화하고, 한국 아파트 실외기 루버/난간 · 커튼월 블라인드 · 화강석 줄눈 · 56단 벽돌/인방 · 1층 통유리/스테인리스 손잡이 디테일을 정밀 베이킹했습니다.
   * 낮 시간대(16:51 등) 만화처럼 노랗게 빛나던 가짜 창문 발광을 제거하고, 시점에 따라 깊이가 움직이는 **실시간 3D 인테리어 패럴랙스 매핑(천장 LED 바·내벽·블라인드)** + 건물별 Low-E 코팅 반사(사파이어 블루·에메랄드 틸·스모크 차콜·브론즈·실버) + 야간 층별 클러스터 오피스/주거 조명을 구현했습니다.
2. **모바일 프레임 예산 청크 스트리밍 & 차량/드라이버 버그 수정 (`Renderer3D.cpp` · `CarMeshGenerator.cpp`)**
   * 18.4만 채 고밀도 환경에서도 모바일 끊김이 없도록 가까운 청크 우선 **프레임당 최대 2청크 점진 빌드 + 49청크 LRU 캐시**를 적용했습니다.
   * **코케트 오픈 로드스터(컨버터블)** 탑승 시 운전자가 차체 뒤로 숨던 시트 높이 클램프 버그를 수정하고, **119 소방차**(후면 알루미늄 롤러 셔터·사다리·형광 노란 반사띠·크롬 발판 범퍼) 및 **서울 시내버스** 후면 디테일을 대폭 보강했습니다.
3. **무결점 교차로·이중 황색 중앙선 · 한국 고속도로/시내도로 분리 · Android 90° 회전 핫픽스 · 33개 자동화 테스트 (`v3.0 ~ v3.5` 기반)**
   * 16×16 공간 격자 교차로 아스팔트 박스, 878개 한국 고속도로 전용 3D 인프라, DPI 독립 모바일 HUD, **33개 자동화 테스트(Release + ASan/UBSan) 100% 통과**.

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
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust) v4.0.0
├── assets/
│   ├── maps/seoul_real.ocmap            # OCM4 v2 실제 서울 184,308 건물(92.7% 위성고도·7종 3D지붕) · 25,295 도로 · SRTM 30m DEM
│   ├── maps/seoul_map_2d.png            # 3072×1968 실제 서울 카토그래피 미니맵 & 전체 지도 텍스처 (고속도로 에메랄드 그린 강조)
│   └── textures/pbr_{albedo,normal_rough}.png # 4096×4096 16타일 밉맵·8×이방성 4K PBR 알베도 & 탄젠트 노멀/러프니스 아틀라스
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v4.0.0 / 400)
│   └── src/com/stickmongus/opencity/
│       └── NativeLoader.java            # 몰입형 전체화면 + 디스플레이 컷아웃 + libopen_city.so 로더
├── tools/
│   ├── build_android_apk.sh             # NDK CMake + aapt2 + d8 + zipalign + apksigner v1/v2/v3 원스톱 빌더
│   ├── build_seoul_ocm4.py              # Overture Maps + OSM + TUM GlobalBuildingAtlas + SRTM -> OCM4 v2 & 3072px 2D 지도
│   ├── gen_pbr_textures.py              # 4096×4096 16타일 4K PBR 알베도/노멀/러프니스/AO 아틀라스 생성기
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
