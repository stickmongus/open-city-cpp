# Open City Seoul — Native Vulkan (`open-city-cpp`) v2.3.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 16-bit HDR + MSAA + 2048px Soft PCF 그림자 + 실시간 동적 20광원 PBR) + Rust (`#![no_std]` 네이티브 물리 커널 v2000) + Python 3 (SPIR-V 셰이더 & 글리프 빌더)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실측 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인(`10`개 사전 컴파일 SPIR-V 1.0 셰이더 + 3D 한글 텍스처 아틀라스)으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·태블릿에서 가로 전체화면 & 10-포인트 멀티터치로 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임이나 외부 에셋 폴더 없이 단일 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v2.3.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v2.3.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v2.2 / v2.1 / v2.0 / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v2.3.0`, `versionCode 230`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v2.3.0 진짜 3D 한글 간판·이정표 · 서울 시내버스 & 3D 운전자 · 동적 다중 광원 · 탄피/탄창 물리 · NPC 소품)

1. **진짜 읽히는 3D 한글 텍스처 아틀라스 (`VulkanContext` · `world.frag` · `Renderer3D`)**
   * 앱 시작 시 `stb_truetype`으로 `1024×1024` RGBA8 32종 서울 한글 표지판 아틀라스를 생성해 Vulkan `set=0, binding=2`(`MatId::SignTextured = 17`)로 바인딩
   * **도로 이정표 갠트리**(`↑ 반포대교 · 남산`, `↑ 강남역 · 테헤란로 →`, `↑ 광화문 · 시청`, `↑ 여의도 · 올림픽대로`, `↑ 서울 외곽순환 IC` 등) · **1~2층 한국 상가 간판 12종**(`편의점 24시`, `서울 커피 · 베이커리`, `온누리 약국`, `김밥 · 떡볶이 분식`, `치킨 · 호프`, `한국은행 · ATM`, `공인중개사 · 부동산`, `서울 내과 · 치과`, `영어 · 수학 학원`, `한우 · 삼겹살`, `크린 세탁소`) · **서울 지하철 역명판** · **BIS 버스 도착 안내판** · **아파트 `101동/105동` 측벽 패널** · **버스 LED 노선판** · **전 차량 한국 번호판**(`123가 4568`, `서울 34바 8247`, `경찰 112`) 완비
2. **서울 시내버스(17번째 CAD 차량) & 전 차량 3D 탑승 운전자 (`CarMeshGenerator` · `GameWorld` · `Renderer3D`)**
   * **파랑 간선버스(472)** · **초록 지선버스(5511)** 파노라마 측면 창·우측 양문형 폴딩 도어·지붕 공조기·내부 승객 좌석·전면 LED 행선판 탑재 및 도심 교통 실시간 운행
   * 모든 주행/탑승 차량 내부에서 **핸들을 잡고 조향하는 3D 운전자** 렌더링 + 탑승 시 서스펜션 바운스 추가
3. **실시간 동적 20광원 PBR 라이팅 & 3인칭 스프링암 카메라 벽 충돌 방지 (`SceneUBO` · `world.frag` · `main.cpp`)**
   * 야간·우천 시 **차량 듀얼 헤드라이트 스포트라이트**, **경찰/119 적·청 점멸 사이렌**, **총구 화염 광원**, **따뜻한 서울 가로등**이 젖은 아스팔트·물웅덩이·건물 외벽을 GGX 스페큘러로 실시간 조명
   * 도보/운전 3인칭 모두 `raycastBuildings3D` 스프링암 충돌 방지로 좁은 골목 벽 관통 0%
4. **총기 탄피·샷건 셸·재장전 빈 탄창 투하 · 3인칭 레이저 빔 & 살아있는 서울 시민 3D 소품**
   * 회전하는 금속 **황동 탄피**, **빨간 12게이지 샷건 셸**, 재장전 시 투하되는 **빈 탄창(`DroppedMag`)**, 총구 건연기, 조준 시 **3인칭 레이저 사이트 빔**
   * 서울 시민 NPC들이 **스마트폰**, **아이스 아메리카노 커피컵**, **서류가방**, **쇼핑백**을 2-Bone IK 팔로 실제로 들고 보행 + 외곽 산악 미세 틈새 0% 제거 & 외곽순환 IC 갠트리·소나무 열식 마감 (26/26 테스트 통과)

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
