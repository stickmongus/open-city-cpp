# Open City C++ + Rust (`open-city-cpp`) — v0.9.0

실제 GTA 시리즈처럼 **모던 C++20 + Rust (`#![no_std]` 네이티브 물리 커널) + Python 3 (3D CAD 에셋 생성기) + OpenGL 3.3 (`raylib 5.5` + 커스텀 GLSL 330 Cook-Torrance GGX PBR 셰이더)** 폴리글랏 아키텍처로 제작된 **3D 1인칭·3인칭 오픈월드 시티 액션 & 풀 커스터마이징 샌드박스 게임**입니다.

별도의 외부 에셋 폴더나 런타임(VC++ / Rust 런타임) 설치 없이 **단일 `open_city.exe` 실행 파일 하나만으로** Windows 10/11에서 즉시 실행되며, Windows 기본 한글 폰트(`맑은 고딕`)를 통해 현대 한글 11,172자 전체를 깨짐 없이 렌더링합니다.

---

## 바로 플레이하기 (Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub 자동 빌드 결과물을 내려받으세요:

1. 리포지토리 우측 **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지로 이동합니다.
2. 최신 버전(**`v0.9.0`**)의 **`OpenCity-Windows-x64.zip`** 파일을 다운로드하고 압축을 풉니다.
3. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 기능 (v0.9.0 폴리글랏 리얼리즘 대업데이트)

* **폴리글랏 4대 언어 하이브리드 아키텍처 (`C++20` + `Rust` + `Python 3` + `GLSL 330`)**
  * **Rust 네이티브 물리 엔진 (`src/physics/physics_kernel.rs`)**: `#![no_std]` FFI 정적 링크 방식으로 **6단 자동변속기(`D1~D6`/`R`)·실시간 RPM 타코미터**, **4륜 독립 스프링-댐퍼 서스펜션**, **Pacejka 타이어 슬립 커브**, **3D 스턴트 점프대 비행 물리**, **회전 충돌 임펄스(`r × J` PIT 매뉴버 스핀아웃)**, **4분면 차체 크럼플 파손(`앞·뒤·좌·우`)**, **3D 회전 거리 소품 파괴 및 소화전 물기둥 분출**을 시뮬레이션합니다.
  * **GLSL 330 Cook-Torrance GGX 마이크로패싯 PBR & 3D 인테리어 매핑 (`src/render/Renderer3D.cpp`)**: 빌딩 유리창 내부에 실제 **3D 방·천장 조명·가구·블라인드가 깊이감 있게 비치는 3D Raycasted Interior Mapping**, **360° 3D 스카이라인·네온 환경 반사맵**, **16-Tap 푸아송 소프트 그림자**, 그리고 **`[U]` 우천·젖은 아스팔트 노면 거울 반사(Wet Road Mirror Puddles)** 를 지원합니다.
  * **Python 3 절차적 3D CAD 에셋 파이프라인 (`tools/generate_realistic_assets.py`)**: 16종 차량 정밀 32-스테이션 CAD 프로파일, **금문교 스타일 현수교 현수선(`Catenary Curve`)**, **선셋 피어 대관람차 3D 링**, **도시 전역 6대 스턴트 점프대** 데이터를 자동 생성합니다.
* **초사실적 3D 도시 랜드마크 구조물 & 디테일 업그레이드된 16종 차량**
  * **5대 3D 랜드마크 구조물**: 북부 해안 **골든 베이 현수교(42m H형 주탑 + 메인 현수 케이블)**, 선셋 피어 **회전 네온 대관람차(12개 곤돌라)**, 항구 **컨테이너 갠트리 크레인**, 대로변 **고속도로 오버헤드 이정표 갠트리**, **도시 전역 6개 3D 스턴트 점프 램프(체공 현금 보너스)**
  * **초정밀 3D 차량 디테일**: **전면 허니콤 라디에이터 그릴·크롬 서라운드·코너 브레이크 에어 인테이크**, **3D 프로젝터 헤드라이트 베젤·LED DRL 아이브로우**, **플러시 도어 핸들·B필러 트림·윈드실드 와이퍼**, **실시간 4륜 서스펜션 상하 스트로크**, **충돌 시 보닛·범퍼가 실제로 들리고 찌그러지는 4분면 크럼플 변형**
* **`[K]` 통합 4탭 커스텀 스튜디오 (`캐릭터 · 총기 건스미스 · 차량 풀 커스텀 · 조준선/HUD`)**
* **총기·중화기 11종 풀 아스날 & 3.2배율 광학 저격 스코프 (`1인칭·3인칭 3D 모델 + 전용 사운드`)**
* **어지러움 제로(`Zero-Dizziness`) 1:1 정밀 에임, `[V]` 1·3인칭 시점, 도보 점프 & 경찰 `★1~★5` 추격전**

---

## 조작키 안내

| 입력 | 동작 |
| :--- | :--- |
| `W` `A` `S` `D` / 방향키 | 도보 이동 / 16종 차량 6단 자동변속 가속·후진·조향·스턴트 점프 |
| **`U`** | **우천 날씨 & 젖은 아스팔트 노면 거울 반사(Wet Road Puddles) ON/OFF 토글** |
| **`K`** | **통합 커스텀 스튜디오 열기/닫기** (`Q`/`E` 탭 전환, `1`~`8` 캐릭터·총기·차량·조준선 실시간 개조) |
| **`V` 또는 마우스 휠클릭 (`MMB`)** | **1인칭 시점 (도보 FPS · 3D 차량 운전석) ↔ 3인칭 시점 즉시 전환** |
| 마우스 이동 | 흔들림 없는 1:1 정밀 시점 조준 (`-64°` 하단 ~ `+68°` 상단, 스튜디오 내 360° 쇼룸 회전) |
| 마우스 우클릭 (`RMB`) / 좌클릭 (`LMB`) | **정밀 광학 조준 · 저격 스코프 줌 (자동 감도 감속)** / 무기 발사 |
| **`1` ~ `9`, `0`, `-` / `Z`·`X` / 마우스 휠** | **무기 11종 슬롯 즉시 선택 및 이전(`Z`)·다음(`X`) 순환 교체** |
| **`[` / `]`** | **실시간 마우스 감도 낮추기 / 높이기 (`30% ~ 250%`)** |
| `Space` | **도보 3D 점프 (차량 지붕 등반)** / 차량 핸드브레이크 **드리프트** |
| `F` 또는 `E` | 차량 탑승·주행 차량 탈취·하차 / 미션 마커에서 미션 시작 |
| `P` | **스마트폰 열기/닫기** (`Q`/`E` 탭 전환, `1`~`9`,`0` 차량 10종 호출·부동산·중화기 보급·**수배 해제**) |
| `B` | 상점·전시장·풀튜닝 차고·주유소·부티크 메뉴 열기 (`1`~`9`, `0`, `-`, `=` 선택) |
| `Left Shift` | 도보 전력질주 / 차량 **니트로 부스트** |
| `R` | 탄창 재장전 |
| `G` | 차량 경적 울리기 / **경찰차·구급차·소방차 사이렌 & 경광등 ON/OFF** |
| `C` | 후방 카메라 보기 |
| `I` | 마우스 상하(Y축) 시점 반전 토글 |
| `F3` | 그래픽 품질 프리셋 전환 (`울트라` / `높음` / `고성능`) |
| `T` | 시간 빠르게 감기 (+3시간, 낮/노을/밤 즉시 전환) |
| `L` | 한국어 / 영어 UI 전환 |
| `H` | 좌측 상단 조작 안내 표시/숨기기 |
| `Tab` / `ESC` | 메뉴 닫기 / 마우스 커서 잠금·해제 |
| `Backspace` | 진행 중인 미션 중단 |
| `F11` | 전체화면 전환 |

---

## 소스 코드에서 직접 빌드하기 (CMake + C++20 + Rust + Python 3)

`raylib 5.5`는 CMake `FetchContent`를 통해 자동으로 다운로드·정적 링크되며, `rustc`와 `python3`가 설치되어 있으면 CMake가 자동으로 Rust 물리 커널 오브젝트와 Python 3D 에셋 헤더를 빌드합니다.

### Windows (Visual Studio 2022 + Rust + Python 3 + CMake)
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
.\build\Release\open_city.exe
```

### Linux (Debian / Ubuntu)
```bash
sudo apt-get install build-essential cmake ninja-build rustc python3 \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/open_city
```

---

## 프로젝트 구조

```text
open-city-cpp/
├── CMakeLists.txt
├── tools/
│   └── generate_realistic_assets.py     # Python 3 절차적 3D CAD 에셋·현수교·대관람차·스턴트 램프 생성기
├── src/
│   ├── physics/
│   │   ├── physics_kernel.rs            # Rust (#![no_std]) 4륜 서스펜션·6단 변속·스턴트 점프·회전 충돌 물리 커널
│   │   └── PhysicsBridge.hpp            # C++20 <-> Rust FFI 브리지 헤더
│   ├── core/Math.hpp                    # 2D/3D 벡터, SAT OBB 충돌, 레이캐스트, 결정론적 RNG
│   ├── world/CityMap.*                  # 8x8 블록 도시, 현수교·대관람차·크레인·스턴트 램프 6기·거리 소품 생성
│   ├── gameplay/Types.hpp               # 차량 16종·무기 11종·4륜 서스펜션/파손/기어 및 커스텀 상태 정의
│   ├── gameplay/GameWorld.*             # Rust 물리 연동, 스턴트 점프 보너스, 소화전 분수, 4탭 커스텀 스튜디오
│   ├── audio/SynthAudio.*               # 절차적 16-bit PCM 사운드 합성 (소음기 총성, 매그넘·저격총·미니건, 사이렌)
│   ├── render/
│   │   ├── GeneratedRealisticAssets.hpp # Python 생성 3D 현수선·푸아송 커널·스턴트 램프 상수 헤더
│   │   └── Renderer3D.*                 # Cook-Torrance GGX PBR, 3D 빌딩 내부 매핑, 젖은 노면 반사, 랜드마크 렌더러
│   ├── ui/HudRenderer.*                 # 11,172자 한글 폰트, 6단 기어·RPM 게이지, [K] 4탭 커스텀 스튜디오 HUD
│   └── main.cpp                         # 어지러움 제로 1:1 카메라, [U] 우천 토글, 스턴트 체공 카메라 루프
└── tests/
    └── test_simulation.cpp              # Rust 물리 엔진·스턴트 점프·회전 충돌·커스텀 등 16개 자동 테스트 스위트
```

