# Open City Seoul — Native Vulkan (`open-city-cpp`) v6.7.0

실제 AAA 오픈월드처럼 **모던 C++20 + Native Vulkan 1.0+ 렌더링 엔진 (volk · SPIR-V 1.0 · 카메라 상대 고정밀 뎁스 · DPI 독립 반응형 모바일 HUD · 16-bit HDR + 4× MSAA + 2048px 가변 0~16-Tap Soft PCF 그림자 + 듀얼 4096×4096 8× 이방성 밉맵 건축/도로 PBR 아틀라스 + 반투명 강화유리 `MatId::StreetGlass` 알파 파이프라인 + 4096×4096 64슬롯 4K 한글 간판·이정표 + 1층 상가·오피스 3D 인테리어 패럴랙스 & 전통 한옥 세살창호 파사드 + 실시간 0~20광원 PBR + 세밀한 인터랙티브 한글 그래픽 상세 설정 메뉴 & 250~1600m 렌더 거리 슬라이더) + DLR Eclipse SUMO 중심정렬 단일 토폴로지 3D 도로·교차로·보도·건물전면광장 메쉬 엔진 (`OCRM v1`, 전 서울 고가교 형고 ≥ 4.8m · C¹ 접속 램프 · 콘크리트 교대/옹벽 · 2,732기 한국형 T-교각+코핑 · 바리센트릭 3D 삼각형 정밀 접지 물리 & 3×3 인접 청크 심리스 지면 · 국토지리정보원 NGII HD Map / 행안부 실폭도로 SHP 임포터 내장!) + 전 서울 포렌식 정비 Geofabrik OSM · Overture Maps · TUM GlobalBuildingAtlas 185,911채 실제 서울 3D 건물 & AWS Terrarium 15m 베어-어스 DTM 융합 월드 (`OCM6 v4`) + 한강 4대 대표 교량 실물 상부구조 + 17종 실물 비례·재질 서울 거리 구조물 + Rust (`#![no_std]` 임계감쇠 서스펜션 물리 커널 v2000)** 아키텍처로 구축된 **크로스플랫폼(Android · Windows · Linux) 실제 지도·단일 토폴로지 정밀도로·실제 건축 기반 서울 3D 오픈월드 대도시 샌드박스 게임**입니다.

- **전 서울 경사지 DTM·건물/고가교 교대·한강 4대 교량·광화문광장·1F 상가·캐릭터/우천 전면 폴리싱 (`v6.7.0`)**: ① 전 서울 급경사지 도로 위 DTM 지형 관통(`3,994 -> 0`, 최대 `0.75m`)을 전수 절토하고 경사지 부유 건물 **704채 접지**·OCRM 도로 침범 건물 **6채 -> 0채**·고가교 말단 콘크리트 교대 옹벽 **927기 주입**·종로 부유 가로수(`3 -> 0`)·강남 구조물 겹침(`1 -> 0`)을 완결했으며, ② 실측 GPS 좌표에 **반포대교(잠수교+달빛무지개분수)·동작대교(블루 스틸 쌍둥이 아치)·성수대교(민트-틸 게르버 트러스)·올림픽대교(88m 사장교 주탑+황금 성화)** 상부구조를 구축했고, ③ 1층 상가 4K PBR 아틀라스(`||` 반복 손잡이 제거·불투명 멀리언), 광화문광장 정오 화강암 톤·역사물길·화단·볼라드, 이면도로/골목 주철 맨홀·타르 균열, 캐릭터/NPC 재킷 디테일·통근 백팩/토트백·다양한 헤어스타일 및 우천 물보라·야간 LED 헤드라이트 투사를 전면 강화했습니다(**45개 자동화 테스트 Release + ASan/UBSan 100% 통과**!).
- **전 서울 25개 권역 2차 포렌식 감사·1층 상가/한옥 리마스터·경찰/교통/NPC 정밀 보정 (`v6.6.0`)**: 1층 상가 3D 패럴랙스 매장 내부 & 북촌·익선동 전통 한옥 외벽 리마스터, 경찰/교통 100% OCRM 아스팔트 스폰 및 수중 NPC `0건`, 25개 권역 부유 구조물(`29 -> 0`)·동대문 DDP 곡면 브러시드 알루미늄 쉘·지하철 출입구 간격(`≥ 45m`) 및 서울 전 권역 실제 노선 색상 정합을 완료했습니다.
- **전 서울 고가교·입체교차로·교각·옹벽 전면 정비 & 구조물/차량 AI 전수 보정 (`v6.5.0`)**: 서울 전역 48,380개 도로·3,161개 OCRM 청크를 전수 감사·재정비하여 저형고 고가교 `362개소` 상향(`≥ 4.80m`), 수직 절벽 단차 `790개소` C¹ 코사인 접속 램프·콘크리트 옹벽 + **2,732기 한국형 해머헤드 T-교각** 구축, 교차로 침범 방음벽·구조물/가로수 아스팔트 침범(`0건`) 및 차량 AI 보도 이탈을 전면 해소했습니다.
- **실제 서울 보도블럭 & 17종 서울 거리 구조물 전면 리마스터 (`v6.4.0`)**: 서울 표준 **30×30cm 쇼트블라스트 화강암 콘크리트 보도블럭** · **1m 포천 화강암 경계석** · 도로 축 100% 평행 **30cm 4줄 KS F 4561 노란 선형 점자블록** · **20cm 경계띠석** · **60×30cm 버너구이 화강암 광장 판석**을 OCRM 보도↔건물전면광장 심리스 연속성으로 재구축하고, 전용 반투명 강화유리(`MatId::StreetGlass`)를 적용한 **17종 서울 거리 구조물**을 리마스터했습니다.
- **전 서울 건물 정밀정렬 & 지형 뚫림 0.00% 완전 정비 (`v6.3.0`)**: 서울 전역 750km² **185,911채 건물·48,380개 도로·99,907개 공원 삼각형·120m 광역 산지/지형·4m 정밀 청크 지면**을 전수 정비하여 간선·고속도로·보도·한강 침범 건물 **0채**, 경사지 **화강암 기초 기단(Plinth Skirt)** 동적 연장, 광역 지형·공원·지면 구멍 관통을 전부 **0건**으로 소거했습니다.
- **세밀한 실시간 한글 `그래픽 상세 설정` 메뉴 (`[U]` / 모바일 `그래픽` 버튼)**: 4대 프리셋(`울트라` / `높음` / `보통` / `성능 우선` + `사용자 지정`)과 **렌더 거리(`250m ~ 1600m`)** · 건물/스카이라인 디테일 · 거리 구조물/가로수 밀도 · 그림자 품질(`끄기 ~ 16-Tap Soft PCF`) · 동적 광원/HDR 블룸(`0~20광원`) · 반사 & 3D 실내 패럴랙스 · 대기 안개 · 실시간 교통량/인파 밀도 · 실시간 FPS 표시를 게임 도중 즉시 조절할 수 있습니다.
- **대한민국 공식 정부 정밀도로지도 / 실폭도로 SHP 원클릭 임포터 (`tools/import_korea_hdmap_ocrm.py`)**: 국토지리정보원 정밀도로지도(**NGII HD Map**), 행정안전부 도로명주소 **실폭도로(`TL_SPRD_RW.shp`)**, 1:1000 연속수치지형도를 기존 `seoul_roads_hd.ocrm` 위에 청크 단위 패치할 수 있습니다.
- **Native Vulkan 1.0+ (Zero OpenGL / Zero raylib)**: Android(`arm64-v8a` + `armeabi-v7a`), Windows x64, Linux x64 모두 100% 네이티브 Vulkan 파이프라인으로 구동됩니다.
- **Android (`.apk`)**: 정식 RSA-4096 v1/v2/v3 서명 + 16KB ELF 페이지 정렬된 유니버설 Vulkan APK(`arm64-v8a` + `armeabi-v7a`) 하나로 Android 7.0+ Vulkan 지원 스마트폰·폴더블·태블릿에서 즉시 실행됩니다.
- **Windows (`.exe`)**: 별도 런타임 없이 압축 해제 후 `open_city.exe` 실행 파일 하나로 Windows 10/11(Vulkan 지원 GPU)에서 즉시 실행됩니다.

---

## 바로 플레이하기 (Android `.apk` / Windows `.exe` 다운로드)

컴파일러 설치 없이 바로 플레이하려면 GitHub **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 페이지에서 최신 버전(**`v6.7.0`**)을 내려받으세요:

### Android 스마트폰 / 태블릿 (Android 7.0+ / Native Vulkan 1.0+)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Android-v6.7.0.apk`** 를 다운로드합니다.
2. 다운로드한 `.apk` 파일을 탭해 설치합니다 *(최초 1회 "출처를 알 수 없는 앱 설치" 허용 필요 — 기존 v6.x / v5.x / v4.x / v3.x / v2.x / v1.x 버전 위에도 동일 서명으로 그대로 덮어쓰기 업데이트 가능!)*.
3. **Open City** 앱 아이콘을 눌러 가로 모드 전체화면으로 바로 플레이하세요!
   * **패키지명**: `com.stickmongus.opencity` (`v6.7.0`, `versionCode 670`)
   * **그래픽 API**: Native Vulkan 1.0+ (`libopen_city.so`, OpenGL/GLES 완전 제거, arm64 16KB 페이지 정렬)
   * **서명 인증서 SHA-256 지문**: `6C:9B:7E:D9:1E:60:AA:E2:E9:B3:2F:70:D6:D9:F1:A1:EB:A4:86:05:6D:35:8E:00:C9:75:EF:28:3C:41:A9:5E`

### Windows 10 / 11 PC (Vulkan 1.0+ GPU)
1. **[Releases](https://github.com/stickmongus/open-city-cpp/releases)** 에서 **`OpenCity-Windows-x64.zip`** 을 다운로드하고 압축을 풉니다.
2. **`open_city.exe`** 를 더블클릭하면 바로 게임이 시작됩니다!

---

## 주요 혁신 (v6.7.0 전 서울 경사지 DTM·교대/건물·한강 4대 교량·광화문광장·1F 상가·캐릭터/우천 폴리싱)

0. **전 서울 경사지 DTM 절토(`3,994 -> 0`) · 고가교 말단 교대 옹벽 `927기` · 부유/도로 침범 건물·가로수 0건 (`polish_seoul_v67.py` · `CityMap.cpp`)**
   * 급경사지 도로 위 지형 관통(`>1.2m`) **3,994건 -> 0건**(최대 `0.75m`)으로 전수 절토하고, 경사지 청크 지면 블렌딩(`hillBlend` / `flatShelfM`)을 정교화해 자연스러운 사면 전이를 구현했습니다.
   * 경사지 부유 건물 **704채 접지**, OCRM 아스팔트 중심 침범 건물 **6채 -> 0채**, 고가교 말단부 하부 **콘크리트 교대 옹벽 `927기`** 주입, 종로 부유 가로수(`3 -> 0`) 및 강남 구조물 겹침(`1 -> 0`)을 완결했습니다.
1. **서울 한강 대표 4대 교량 실물 상부구조 3D 구현 (`Renderer3D.cpp`)**
   * GPS 실측 좌표 기준으로 **반포대교(하부 잠수교 + 양측 36노즐 달빛무지개분수)** · **동작대교(블루 스틸 쌍둥이 포물선 아치)** · **성수대교(민트-틸 게르버 캔틸레버 트러스)** · **올림픽대교(88m 4각 경사 사장교 주탑 + 황금 성화 + 24본 사장 케이블)** 상부구조를 구축했습니다.
2. **1층 상가 4K PBR 아틀라스(`||` 반복 제거) · 광화문광장 역사물길 · 골목 맨홀/균열 · 캐릭터/우천 강화 (`gen_pbr_textures.py` · `world.frag` · `GameWorld.cpp`)**
   * 1층 상가 4K PBR 텍스처(Tile 7)를 출입문 1베이 + 파노라마 쇼윈도 3베이 및 불투명 멀리언(`rough=0.34`)으로 리마스터해 반복 `||` 손잡이 잔상을 제거하고, 광화문광장 정오 화강암 톤·**역사물길·화강석 화단·광장 볼라드**와 골목 **주철 맨홀·타르 균열**을 추가했습니다.
   * 캐릭터/NPC 재킷 스탠드칼라·지퍼·어깨 요크·서울 통근 백팩/토트백·다양한 헤어스타일/니트 비니와 젖은 아스팔트 빗물 반사·후륜 물보라 미스트·야간 LED 헤드라이트 바닥 투사를 강화했습니다(**45개 자동화 테스트 Release + ASan/UBSan 100% 통과**!).

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
├── CMakeLists.txt                       # Native Vulkan (Vulkan-Headers + volk + GLFW 3.4 + miniaudio + stb + Rust) v6.7.0
├── assets/
│   ├── maps/seoul_real.ocmap            # OCM6 v4 실제 서울 15m DTM · 48,380 3D 도로 · 185,911 전수 정비 3D 건물 · 60.7K POI (30.9 MB)
│   ├── maps/seoul_roads_hd.ocrm         # OCRM v1 Eclipse SUMO 단일 토폴로지 3D 도로·교차로·보도·T-교각·교대/옹벽 메쉬 (3,161청크 · LZ4 26.4 MB)
│   ├── maps/seoul_map_2d.png            # 4096×2624 실제 서울 카토그래피 미니맵 & 전체 지도 텍스처 (고속도로·순환도로 에메랄드 그린)
│   └── textures/
│       ├── pbr_{albedo,normal_rough}.png    # 4096×4096 16타일 밉맵·8×이방성 4K 건축 PBR 알베도 & 노멀/러프니스 아틀라스
│       ├── street_{albedo,normal_rough}.png # 4096×4096 16타일 전용 서울 도로·거리 PBR (포천 쇄석 아스팔트·SMA·화강암경계석·30cm 점자블록·30×30cm 보도블록)
│       └── seoul_signs_4k.png               # 4096×4096 64슬롯 4K 한글 간판·이정표 (고속도로/시내 갠트리 16 · 한국 상가 32 · 지하철 8 · 번호판)
├── android/
│   ├── AndroidManifest.xml              # Vulkan 1.0+ 하드웨어 기능 · 가로 전체화면 NativeActivity (v6.7.0 / 670)
│   └── src/com/stickmongus/opencity/
│       └── NativeLoader.java            # 몰입형 전체화면 + 디스플레이 컷아웃 + libopen_city.so 로더
├── tools/
│   ├── build_android_apk.sh             # NDK CMake + aapt2 + d8 + zipalign + apksigner v1/v2/v3 원스톱 빌더
│   ├── polish_seoul_v67.py              # 전 서울 급경사지 DTM 절토 · 부유/도로 침범 건물 정비 · 고가교 말단 교대 옹벽(927기) 컴파일러
│   ├── polish_seoul_bridges_v65.py      # 전 서울 48,380 도로·3,161 OCRM 청크 고가교 형고·C¹ 접속 램프·콘크리트 옹벽·2,732 T-교각 컴파일러
│   ├── polish_seoul_buildings_v63.py    # 전 서울 185,911채 건물 도로·보도·수면 STRtree 클리핑 & 폴리곤 실표고 접지 컴파일러
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
