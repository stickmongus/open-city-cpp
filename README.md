# Open City C++ (`open-city-cpp`)

실제 GTA 시리즈처럼 **모던 C++20**과 **OpenGL 3.3 (`raylib 5.5` + 커스텀 GLSL 셰이더)** 기반으로 제작된 **3D 3인칭 오픈월드 시티 액션 샌드박스 게임**입니다.

별도의 외부 에셋 폴더나 런타임(VC++ 재배포 패키지) 설치 없이 **단일 `open_city.exe` 실행 파일 하나만으로** Windows 10/11에서 즉시 실행되며, Windows 기본 한글 폰트(`맑은 고딕`)를 자동 감지해 한국어 UI를 지원합니다.

---

## 바로 플레이하기 (Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub 자동 빌드 결과물을 내려받으세요:

1. 리포지토리 우측 **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지(또는 상단 **Actions** 탭의 최신 빌드 아티팩트)로 이동합니다.
2. **`OpenCity-Windows-x64.zip`** 파일을 다운로드하고 압축을 풉니다.
3. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 기능

* **3D 오픈월드 메트로폴리스 (`Sunset Bay`)**
  * 다운타운 고층 빌딩 숲, 미드타운 상업지구, 센트럴 파크 광장, 선셋 해변도로, 항구 공업단지로 구성된 8x8 블록 도시
  * 실시간 **낮/밤 시간 변화** (노을·야간 고층 빌딩 창문 조명·가로등·차량 헤드라이트 및 거리 안개 셰이더)
* **도보 이동 & 차량 탈취(Carjacking) 및 드리프트 물리**
  * 주차된 슈퍼카·머슬카·SUV·택시·밴 탑승은 물론, 도로를 달리는 AI 차량을 세워 운전자를 끌어내고 탈취 가능 (`F` / `E`)
  * 핸드브레이크(`Space`) 드리프트 슬립 물리, 타이어 스키드마크·연기 파티클, 드리프트 콤보 현금 보너스 및 니트로 부스트(`Shift`)
* **정밀 3D 에임 & 살아있는 도시 AI (v0.2.0 업그레이드)**
  * 화면 정중앙 크로스헤어 3D 광학 레이캐스트와 총구 탄도를 동기화하여 근거리·원거리·고저차 어디서든 정확한 사격 지원 (헤드샷 크리티컬 & 히트마커 `X` 피드백)
  * 맨주먹, 9mm 권총, 전술 기관단총(SMG), 펌프 샷건(산탄), 돌격 카빈소총 지원 및 탄도 트레이서·재장전·차량 폭발·소화전 물기둥 물리
  * 교차로 **실시간 신호등**과 횡단보도 보행자를 인식해 정지하는 **도시 차량 트래픽 AI**, 인도와 횡단보도 신호를 따라 자연스럽게 걷고 총성에 인도를 따라 대피하는 **시민 보행자 AI**, 구역을 순찰·교전하는 **라이벌 갱단 AI**
* **미니맵 레이더 HUD, 상점·튜닝 차고 및 3종 미션**
  * 카메라 방향에 맞춰 회전하는 **GTA 스타일 원형 미니맵 레이더** (GPS 경로선, 상점·미션 마커, 방탄복/체력 게이지)
  * **AMMU-TECH 무기 상점**: 기관단총, 샷건, 돌격소총, 탄약, 방탄복, 회복 키트 구매
  * **PAY 'N' TUNE 차고**: 차량 수리, 터보 엔진 스테이지 업그레이드(최고속도·가속 증가), 커스텀 메탈릭 도색, 니트로 충전
  * **반복 플레이 가능한 3종 미션**:
    1. **슈퍼카 탈취 & 긴급 배달 (VIP Exotic Delivery)**
    2. **도심 스트리트 체크포인트 레이스 (Midnight Street Sprint)**
    3. **항구 라이벌 갱단 소탕 & 현금 가방 회수 (Harbor Syndicate Raid)**

---

## 조작키 안내

| 입력 | 동작 |
| :--- | :--- |
| `W` `A` `S` `D` / 방향키 | 도보 이동 / 차량 가속·후진·조향 |
| 마우스 이동 | 상하좌우 자유 3인칭 시점 회전 (`-64°` 하단 ~ `+68°` 고층빌딩·하늘 상단) |
| 마우스 우클릭 (`RMB`) / 좌클릭 (`LMB`) | 정밀 줌 조준 / 무기 발사 |
| `F` 또는 `E` | 차량 탑승·주행 차량 탈취·하차 / 미션 마커에서 미션 시작 |
| `Space` | 차량 핸드브레이크 **드리프트** |
| `Left Shift` | 도보 전력질주 / 차량 **니트로 부스트** |
| `1` ~ `5` / 마우스 휠 | 무기 슬롯 교체 (상점 메뉴 열림 시 `1`~`6` 아이템 구매) |
| `R` | 탄창 재장전 |
| `B` | 무기 상점 / 튜닝 차고 구역 안에서 구매 메뉴 열기·닫기 |
| `G` | 차량 경적 울리기 |
| `C` | 후방 카메라 보기 |
| `I` | 마우스 상하(Y축) 시점 반전 토글 |
| `T` | 시간 빠르게 감기 (+3시간, 낮/노을/밤 즉시 전환) |
| `L` | 한국어 / 영어 UI 전환 |
| `H` | 좌측 상단 조작 안내 표시/숨기기 |
| `Tab` / `ESC` | 마우스 커서 잠금/해제 |
| `Backspace` | 진행 중인 미션 중단 |
| `F11` | 전체화면 전환 |

---

## 소스 코드에서 직접 빌드하기 (CMake + C++20)

`raylib 5.5`는 CMake `FetchContent`를 통해 자동으로 다운로드·정적 링크됩니다.

### Windows (Visual Studio 2022 + CMake)
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
.\build\Release\open_city.exe
```

### Linux (Debian / Ubuntu)
```bash
sudo apt-get install build-essential cmake ninja-build \
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
├── src/
│   ├── core/Math.hpp            # 2D/3D 벡터, SAT OBB 충돌, 레이캐스트, 결정론적 RNG
│   ├── world/CityMap.*          # 8x8 블록 도시, 구역별 고층빌딩·공원·도로망·상점/미션 POI 생성
│   ├── gameplay/Types.hpp       # 차량 6종, 무기 5종, NPC, 픽업, 미션, 오디오 이벤트 정의
│   ├── gameplay/GameWorld.*     # 물리 주행·드리프트, 차량 탈취, 트래픽/갱단 AI, 전투, 미션 엔진
│   ├── audio/SynthAudio.*       # 절차적 16-bit PCM 사운드 합성 (총성, 폭발, 엔진음, 경적 등)
│   ├── render/Renderer3D.*      # GLSL 330 조명·안개·야간 창문 셰이더 및 3D 멀티파트 모델 렌더러
│   ├── ui/HudRenderer.*         # 회전형 원형 미니맵 레이더, 속도계, 상점 모달, 한/영 폰트 로더
│   └── main.cpp                 # 3인칭 오빗/추격 카메라, 입력 루프, 프레임 실행 진입점
└── tests/
    └── test_simulation.cpp      # 도시 충돌, 차량 탈취, 드리프트 보너스, 상점, 3종 미션 자동 검증 테스트
```
