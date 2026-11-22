#!/usr/bin/env python3
"""
Generate an ultra-crisp 4096x4096 (4 cols x 16 rows = 64 wide 1024x256 slots)
Real Korean Seoul Highway Gantry, Boulevard Sign, Metro Canopy, Bus BIS & 1F Commercial Shopfront Atlas:
  - assets/textures/seoul_signs_4k.png

Slot layout (col = slot % 4, row = slot // 4, 1024x256 each):
   0.. 7: 8 Real Korean GREEN Expressway & Urban Motorway Overhead Gantries
   8..15: 8 Real Korean ROYAL-BLUE City Boulevard Overhead Gantries
  16..47: 32 Photorealistic Korean 1F Commercial Shopfront Fascia Signboards
  48..55: 8 Authentic Seoul Metro Subway Station Entrance Canopies (Lines 1-9)
  56..63: 8 Seoul Transit BIS, Luxury Apt Emblem, Bus Front LEDs, License Plates & Road Safety Signs
"""

import os
import time
from PIL import Image, ImageDraw, ImageFont

ATLAS_W = 4096
ATLAS_H = 4096
COLS = 4
ROWS = 16
CW = ATLAS_W // COLS   # 1024
CH = ATLAS_H // ROWS   # 256

FONT_CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
]


def load_font(size: int, idx: int = 0):
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            try:
                # For NotoSansCJK-Bold.ttc, index 1 is Korean (KR) in standard Debian CJK TTC!
                # Verify Korean glyph presence cleanly; fall back to index 0 if needed.
                return ImageFont.truetype(p, size=size, index=1 if p.endswith(".ttc") else 0)
            except Exception:
                try:
                    return ImageFont.truetype(p, size=size, index=0)
                except Exception:
                    pass
    return ImageFont.load_default()


def draw_centered(draw: ImageDraw.ImageDraw, cx: int, cy: int, text: str, font, fill, shadow=None):
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = cx - tw // 2 - bbox[0]
    ty = cy - th // 2 - bbox[1]
    if shadow is not None:
        draw.text((tx + 3, ty + 3), text, font=font, fill=shadow)
    draw.text((tx, ty), text, font=font, fill=fill)


def draw_gantry(draw, x0, y0, is_exp: bool, route_no: str, title_kr: str, sub_kr: str, eng: str, arrows="↑   ↑"):
    bg = (14, 108, 66, 255) if is_exp else (18, 72, 162, 255)
    dark_trim = (8, 68, 42, 255) if is_exp else (12, 48, 114, 255)
    white = (248, 250, 252, 255)
    yellow = (252, 212, 48, 255)

    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=dark_trim)
    draw.rounded_rectangle([x0 + 10, y0 + 10, x0 + CW - 11, y0 + CH - 11], radius=18, fill=bg, outline=white, width=8)

    # Route shield badge on left
    if route_no:
        bx0, by0, bx1, by1 = x0 + 38, y0 + 38, x0 + 188, y0 + 158
        if is_exp:
            # Iconic Korean Expressway Red-Top / Blue-Bottom Shield!
            draw.rounded_rectangle([bx0, by0, bx1, by1], radius=24, fill=(22, 86, 186, 255), outline=white, width=6)
            draw.rounded_rectangle([bx0 + 3, by0 + 3, bx1 - 3, by0 + 44], radius=18, fill=(214, 42, 42, 255))
            draw.rectangle([bx0 + 3, by0 + 26, bx1 - 3, by0 + 46], fill=(214, 42, 42, 255))
            draw_centered(draw, (bx0 + bx1) // 2, by0 + 24, "고속", load_font(22), white)
            draw_centered(draw, (bx0 + bx1) // 2, by0 + 84, route_no, load_font(56), white)
        else:
            # Korean Metropolitan Route Oval / Rounded Shield
            draw.rounded_rectangle([bx0, by0 + 12, bx1, by1 - 12], radius=44, fill=(24, 128, 78, 255), outline=white, width=6)
            draw_centered(draw, (bx0 + bx1) // 2, (by0 + by1) // 2, route_no, load_font(44), white)

    cx_text = x0 + (595 if route_no else 512)
    draw_centered(draw, cx_text, y0 + 66, title_kr, load_font(64), white, shadow=(0, 0, 0, 120))
    draw_centered(draw, cx_text, y0 + 138, sub_kr, load_font(38), (226, 238, 248, 255))
    draw_centered(draw, cx_text, y0 + 184, eng, load_font(25), (198, 218, 236, 255))

    # Crisp bottom lane arrows bar
    draw.line([x0 + 36, y0 + 206, x0 + CW - 36, y0 + 206], fill=(220, 234, 246, 180), width=3)
    draw_centered(draw, x0 + CW // 2, y0 + 226, arrows, load_font(36), yellow if is_exp else white)


def draw_shop_sign(draw, x0, y0, bg, border, accent, main_txt, sub_txt, badge_txt="", dark_text=False, split_bar=False):
    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(28, 30, 36, 255))
    draw.rounded_rectangle([x0 + 10, y0 + 10, x0 + CW - 11, y0 + CH - 11], radius=14, fill=bg, outline=border, width=7)

    if split_bar:
        # Convenience store / tricolor accent stripe along top & bottom!
        draw.rectangle([x0 + 16, y0 + 16, x0 + CW - 17, y0 + 44], fill=accent)
        draw.rectangle([x0 + 16, y0 + CH - 44, x0 + CW - 17, y0 + CH - 17], fill=border)

    fg = (24, 28, 36, 255) if dark_text else (250, 250, 252, 255)
    sub_fg = (58, 66, 78, 255) if dark_text else (228, 234, 242, 255)
    sh = None if dark_text else (0, 0, 0, 110)

    cx = x0 + CW // 2
    if badge_txt:
        # Left circular/rounded emblem badge
        bx0, by0, bx1, by1 = x0 + 42, y0 + 46, x0 + 206, y0 + 210
        draw.ellipse([bx0, by0, bx1, by1], fill=accent, outline=(255, 255, 255, 230), width=6)
        b_fg = (24, 28, 36, 255) if sum(accent[:3]) > 460 else (252, 252, 255, 255)
        draw_centered(draw, (bx0 + bx1) // 2, (by0 + by1) // 2, badge_txt, load_font(48 if len(badge_txt) <= 3 else 34), b_fg)
        cx = x0 + 610

    f_main = load_font(76 if len(main_txt) <= 9 else (64 if len(main_txt) <= 13 else 54))
    draw_centered(draw, cx, y0 + 104, main_txt, f_main, fg, shadow=sh)
    if sub_txt:
        draw_centered(draw, cx, y0 + 188, sub_txt, load_font(34), sub_fg)


def draw_metro_canopy(draw, x0, y0, line_col, line_num, kr_name, en_name, exit_no):
    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(32, 36, 44, 255))
    draw.rectangle([x0 + 8, y0 + 8, x0 + CW - 9, y0 + CH - 9], fill=(42, 46, 56, 255))
    # Signature top & bottom Metro line color stripe
    draw.rectangle([x0 + 8, y0 + 8, x0 + CW - 9, y0 + 42], fill=line_col)
    draw.rectangle([x0 + 8, y0 + CH - 34, x0 + CW - 9, y0 + CH - 9], fill=line_col)
    # Circular Line badge
    draw.ellipse([x0 + 46, y0 + 56, x0 + 196, y0 + 206], fill=line_col, outline=(255, 255, 255, 255), width=6)
    draw_centered(draw, x0 + 121, y0 + 131, line_num, load_font(76), (255, 255, 255, 255))
    # Station Name (KR + EN)
    draw_centered(draw, x0 + 540, y0 + 108, kr_name, load_font(74), (252, 252, 255, 255))
    draw_centered(draw, x0 + 540, y0 + 184, en_name, load_font(34), (212, 220, 232, 255))
    # Yellow Korean Exit Number Box (출구!)
    draw.rounded_rectangle([x0 + 838, y0 + 62, x0 + 986, y0 + 198], radius=18, fill=(246, 200, 32, 255))
    draw_centered(draw, x0 + 912, y0 + 114, exit_no, load_font(68), (24, 26, 30, 255))
    draw_centered(draw, x0 + 912, y0 + 174, "출구", load_font(28), (24, 26, 30, 255))


def main():
    t0 = time.time()
    os.makedirs("assets/textures", exist_ok=True)
    img = Image.new("RGBA", (ATLAS_W, ATLAS_H), (30, 34, 42, 255))
    draw = ImageDraw.Draw(img)

    def slot_xy(slot: int):
        return (slot % COLS) * CW, (slot // COLS) * CH

    # 0..7: 8 Real Korean GREEN Expressway Overhead Gantries
    exp_gantries = [
        ("88", "올림픽대로", "잠실 · 여의도 · 김포공항", "Olympic-daero · Jamsil · Gimpo Airport", "↑      ↑      ↑"),
        ("1",  "경부고속도로", "서초 · 양재 · 판교 · 부산", "Gyeongbu Expwy · Seocho · Yangjae · Busan", "↑      ↑      ↑"),
        ("70", "강변북로", "마포 · 성수 · 구리 · 일산", "Gangbyeonbuk-ro · Mapo · Seongsu · Guri", "↑      ↑      ↑"),
        ("30", "내부순환로", "성산 · 길음 · 성동 · 홍은", "Naebu Expwy · Seongsan · Gireum · Seongdong", "↑      ↑"),
        ("61", "분당수서로", "수서 · 분당 · 성남 · 청담", "Bundang-Suseo Expwy · Suseo · Bundang", "↑      ↑"),
        ("61", "동부간선로", "의정부 · 성수 · 수서 · 장지", "Dongbu Expwy · Uijeongbu · Seongsu", "↑      ↑"),
        ("88", "올림픽대로", "반포 · 동작 · 여의도 · 공항", "Olympic-daero · Banpo · Yeouido", "↑      ↑      ↗"),
        ("1",  "반포IC · 서초IC", "한남대교 · 경부간선도로", "Banpo IC · Seocho IC · Hannam Bridge", "↖    ↑    ↑    ↗"),
    ]
    for i, g in enumerate(exp_gantries):
        x0, y0 = slot_xy(i)
        draw_gantry(draw, x0, y0, True, *g)

    # 8..15: 8 Real Korean ROYAL-BLUE City Boulevard Overhead Gantries
    city_gantries = [
        ("강남", "테헤란로", "강남역 · 선릉역 · 삼성역 · 코엑스", "Teheran-ro · Gangnam · Samseong · COEX", "↑    ↑    ↑   [버스]"),
        ("종로", "세종대로", "광화문 · 시청 · 숭례문 · 서울역", "Sejong-daero · Gwanghwamun · City Hall", "↑      ↑      ↑"),
        ("서초", "반포대로", "반포대교 · 고속터미널 · 예술의전당", "Banpo-daero · Banpo Bridge · Express Terminal", "↑      ↑      ↑"),
        ("송파", "올림픽로", "잠실역 · 롯데월드타워 · 종합운동장", "Olympic-ro · Jamsil · Lotte World Tower", "↑    ↑    ↑   [버스]"),
        ("영등포", "여의대로", "여의도 · 국회의사당 · 63빌딩 · 마포", "Yeoui-daero · Yeouido · National Assembly", "↑      ↑      ↑"),
        ("강남", "강남대로", "신사역 · 논현역 · 강남역 · 양재역", "Gangnam-daero · Sinsa · Gangnam · Yangjae", "↑    ↑    ↑   [버스]"),
        ("종로", "종로 · 을지로", "종각 · 동대문 DDP · 청계천", "Jongno · Euljiro · Dongdaemun DDP", "↑      ↑      ↑"),
        ("마포", "양화로 · 성수", "홍대입구 · 합정 · 성수 · 서울숲", "Yanghwa-ro · Hongdae · Seongsu · Seoul Forest", "↑      ↑      ↗"),
    ]
    for i, g in enumerate(city_gantries):
        x0, y0 = slot_xy(8 + i)
        draw_gantry(draw, x0, y0, False, *g)

    # 16..47: 32 Photorealistic Korean 1F Commercial Shopfront Fascia Signboards
    shops = [
        # (bg, border, accent, main, sub, badge, dark_text, split_bar)
        ((16, 78, 52, 255),   (236, 240, 236, 255), (10, 112, 70, 255),   "STARBUCKS",       "스타벅스 커피 · RESERVE", "★", False, False),
        ((38, 44, 48, 255),   (154, 212, 46, 255),  (154, 212, 46, 255),  "OLIVE YOUNG",     "올리브영 · HEALTH & BEAUTY", "OY", False, False),
        ((244, 248, 252, 255),(22, 134, 224, 255),  (242, 118, 34, 255),  "GS25 편의점",     "24시 FRESH · CAFE25", "GS", True, True),
        ((106, 46, 152, 255), (144, 216, 46, 255),  (144, 216, 46, 255),  "CU 24시 편의점",  "NICE TO CU · GET COFFEE", "CU", False, True),
        ((248, 248, 246, 255),(218, 44, 38, 255),   (22, 134, 74, 255),   "7-ELEVEN",        "세븐일레븐 24시 편의점", "7", True, True),
        ((28, 28, 32, 255),   (186, 32, 52, 255),   (186, 32, 52, 255),   "A TWOSOME PLACE", "투썸플레이스 · COFFEE & DESSERT", "TW", False, False),
        ((18, 46, 108, 255),  (228, 194, 112, 255), (228, 194, 112, 255), "PARIS BAGUETTE",  "파리바게뜨 · BOULANGERIE", "PB", False, False),
        ((250, 212, 22, 255), (32, 32, 36, 255),    (32, 32, 36, 255),    "MEGA COFFEE",     "메가MGC커피 · BIG SIZE", "M", True, False),
        ((22, 58, 138, 255),  (236, 242, 252, 255), (42, 108, 212, 255),  "EDIYA COFFEE",    "이디야커피 · COFFEE LAB", "E", False, False),
        ((62, 52, 42, 255),   (248, 194, 24, 255),  (248, 194, 24, 255),  "KB 국민은행",     "KB Kookmin Bank · ATM 365", "KB", False, False),
        ((20, 74, 168, 255),  (224, 236, 252, 255), (46, 132, 234, 255),  "신한은행",        "Shinhan Bank · 글로벌금융", "S", False, False),
        ((16, 128, 116, 255), (234, 248, 246, 255), (228, 66, 78, 255),   "하나은행",        "Hana Bank · 365코너", "H", False, False),
        ((246, 250, 248, 255),(22, 152, 82, 255),   (22, 152, 82, 255),   "온누리 약국",     "처방전 조제 · PHARMACY", "+", True, False),
        ((242, 248, 254, 255),(36, 126, 206, 255),  (36, 126, 206, 255),  "서울연세치과",    "임플란트 · 교정 · DENTAL", "치", True, False),
        ((28, 36, 56, 255),   (214, 184, 124, 255), (214, 184, 124, 255), "강남피부과·성형", "DERMATOLOGY & AESTHETICS", "뷰", False, False),
        ((232, 92, 34, 255),  (252, 232, 168, 255), (252, 210, 44, 255),  "김밥천국 24시",   "김밥 · 라면 · 돈까스 · 찌개", "김", False, False),
        ((46, 32, 26, 255),   (232, 162, 48, 255),  (212, 124, 34, 255),  "교촌치킨 1991",   "KYOCHON CHICKEN & PUB", "닭", False, False),
        ((34, 26, 24, 255),   (208, 66, 44, 255),   (208, 66, 44, 255),   "한우 숯불갈비",   "숙성 삼겹살 · 전통 화로구이", "牛", False, False),
        ((242, 236, 224, 255),(42, 38, 36, 255),    (188, 44, 38, 255),   "을지로 평양냉면", "전통 육수 · 만두 · 수육", "麵", True, False),
        ((216, 30, 46, 255),  (252, 252, 252, 255), (252, 252, 252, 255), "DAISO 다이소",    "국민가게 · 생활용품 백화점", "d", False, False),
        ((24, 26, 30, 255),   (236, 238, 242, 255), (240, 242, 246, 255), "MUSINSA STANDARD","무신사 스탠다드 · FLAGSHIP", "M", False, False),
        ((24, 52, 96, 255),   (232, 198, 92, 255),  (232, 198, 92, 255),  "서울 공인중개사", "아파트 · 오피스텔 · 상가임대", "家", False, False),
        ((26, 62, 54, 255),   (218, 192, 134, 255), (218, 192, 134, 255), "현대백화점",      "THE HYUNDAI SEOUL", "H", False, False),
        ((20, 82, 58, 255),   (228, 236, 244, 255), (24, 68, 148, 255),   "교보문고",        "KYOBO BOOK CENTRE · 핫트랙스", "교", False, False),
        ((198, 26, 38, 255),  (252, 246, 240, 255), (28, 28, 34, 255),    "CGV IMAX",        "멀티플렉스 영화관 · 4DX", "CGV", False, False),
        ((164, 78, 52, 255),  (246, 236, 222, 255), (242, 212, 168, 255), "성수 베이커리",   "ARTISAN BAKERY & ROASTERS", "빵", False, False),
        ((44, 30, 26, 255),   (224, 62, 42, 255),   (224, 62, 42, 255),   "홍대 라멘·이자카야","진한 돈코츠 · 생맥주", "라", False, False),
        ((24, 34, 58, 255),   (218, 178, 92, 255),  (218, 178, 92, 255),  "이태원 브런치",   "ROOFTOP CAFE & WINE BAR", "W", False, False),
        ((134, 34, 44, 255),  (244, 234, 216, 255), (222, 182, 118, 255), "본죽 & 비빔밥",   "정성을 담은 건강 한식", "本", False, False),
        ((20, 62, 164, 255),  (244, 248, 255, 255), (42, 108, 224, 255),  "SAMSUNG STORE",   "삼성스토어 · GALAXY EXPERIENCE", "S", False, False),
        ((250, 218, 28, 255), (42, 34, 28, 255),    (62, 44, 32, 255),    "KAKAO FRIENDS",   "카카오프렌즈 플래그십 스토어", "K", True, False),
        ((32, 74, 86, 255),   (222, 238, 242, 255), (246, 188, 56, 255),  "24시 스터디카페", "프리미엄 라운지 · COFFEE", "24", False, False),
    ]
    for i, s in enumerate(shops):
        x0, y0 = slot_xy(16 + i)
        draw_shop_sign(draw, x0, y0, *s)

    # 48..55: 8 Authentic Seoul Metro Subway Station Canopies
    metros = [
        ((42, 168, 78, 255),  "2", "강남역",     "Gangnam Station · Line 2", "10"),
        ((42, 168, 78, 255),  "2", "삼성역",     "Samseong · COEX Mall",     "5"),
        ((42, 168, 78, 255),  "2", "잠실역",     "Jamsil · Lotte World",     "1"),
        ((192, 162, 66, 255), "9", "고속터미널", "Express Bus Terminal",     "8"),
        ((236, 118, 28, 255), "3", "경복궁역",   "Gyeongbokgung Palace",     "5"),
        ((36, 98, 198, 255),  "1", "시청역",     "City Hall · Seoul Plaza",  "4"),
        ((146, 72, 178, 255), "5", "여의도역",   "Yeouido · IFC Mall",       "3"),
        ((42, 168, 78, 255),  "2", "홍대입구역", "Hongik Univ. Station",     "9"),
    ]
    for i, m in enumerate(metros):
        x0, y0 = slot_xy(48 + i)
        draw_metro_canopy(draw, x0, y0, *m)

    # 56: Real-time Seoul Bus BIS Display
    x0, y0 = slot_xy(56)
    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(16, 22, 34, 255))
    draw.rectangle([x0 + 12, y0 + 12, x0 + CW - 13, y0 + 68], fill=(24, 98, 212, 255))
    draw_centered(draw, x0 + CW // 2, y0 + 40, "서울시 버스 도착안내  SEOUL BUS BIS", load_font(36), (252, 252, 255, 255))
    rows_bis = [
        ((64, 156, 255, 255), "472", "강남역 · 신사 · 명동", "곧 도착", (255, 212, 64, 255)),
        ((64, 156, 255, 255), "146", "삼성역 · 잠실 · 상계", "3분 후",  (92, 242, 148, 255)),
        ((68, 196, 96, 255),  "3412", "압구정 · 코엑스 · 송파", "6분 후", (92, 242, 148, 255)),
    ]
    for ri, (c, num, route, eta, ec) in enumerate(rows_bis):
        ry = y0 + 96 + ri * 52
        draw.rounded_rectangle([x0 + 28, ry - 18, x0 + 178, ry + 20], radius=8, fill=c)
        draw_centered(draw, x0 + 103, ry + 1, num, load_font(32), (255, 255, 255, 255))
        draw.text((x0 + 206, ry - 18), route, font=load_font(32), fill=(236, 242, 248, 255))
        draw_centered(draw, x0 + 890, ry + 1, eta, load_font(32), ec)

    # 57: Luxury Korean Apartment Complex Wall Emblem
    x0, y0 = slot_xy(57)
    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(236, 232, 224, 255))
    draw.rectangle([x0 + 14, y0 + 14, x0 + CW - 15, y0 + CH - 15], outline=(146, 126, 92, 255), width=8)
    draw_centered(draw, x0 + CW // 2, y0 + 94, "RAEMIAN  래미안", load_font(74), (36, 54, 72, 255))
    draw_centered(draw, x0 + CW // 2, y0 + 184, "101동 · SEOUL SIGNATURE RESIDENCE", load_font(34), (112, 96, 72, 255))

    # 58..59: Seoul Blue Bus LED Front Destination Boards
    for slot_i, (bnum, bdest) in [(58, ("472", "강남역 · 신사 · 시청")), (59, ("146", "삼성역 · 잠실 · 테헤란로"))]:
        x0, y0 = slot_xy(slot_i)
        draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(14, 16, 20, 255))
        draw.rectangle([x0 + 14, y0 + 14, x0 + CW - 15, y0 + CH - 15], outline=(48, 54, 64, 255), width=6)
        draw_centered(draw, x0 + 180, y0 + 128, bnum, load_font(112), (255, 198, 42, 255))
        draw_centered(draw, x0 + 640, y0 + 128, bdest, load_font(62), (255, 222, 92, 255))

    # 60: Authentic Korean White Vehicle License Plate
    x0, y0 = slot_xy(60)
    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(246, 248, 252, 255))
    draw.rounded_rectangle([x0 + 16, y0 + 16, x0 + CW - 17, y0 + CH - 17], radius=18, outline=(28, 32, 40, 255), width=10)
    draw.rectangle([x0 + 22, y0 + 22, x0 + 122, y0 + CH - 23], fill=(24, 76, 184, 255))
    draw_centered(draw, x0 + 72, y0 + 190, "KOR", load_font(34), (255, 255, 255, 255))
    draw_centered(draw, x0 + 566, y0 + 128, "123가 4567", load_font(136), (22, 24, 28, 255))

    # 61: Korean National Police Panel
    x0, y0 = slot_xy(61)
    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(246, 248, 252, 255))
    draw.rectangle([x0, y0 + 166, x0 + CW - 1, y0 + CH - 1], fill=(20, 62, 152, 255))
    draw.rectangle([x0, y0 + 148, x0 + CW - 1, y0 + 168], fill=(246, 196, 32, 255))
    draw_centered(draw, x0 + CW // 2, y0 + 78, "경찰   POLICE", load_font(84), (18, 48, 124, 255))
    draw_centered(draw, x0 + CW // 2, y0 + 210, "서울특별시경찰청 · 112", load_font(44), (252, 252, 255, 255))

    # 62: Korean School Zone / Speed 30 Safety Sign
    x0, y0 = slot_xy(62)
    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(248, 206, 26, 255))
    draw.rounded_rectangle([x0 + 14, y0 + 14, x0 + CW - 15, y0 + CH - 15], radius=22, outline=(28, 28, 32, 255), width=10)
    draw.ellipse([x0 + 46, y0 + 38, x0 + 226, y0 + 218], fill=(252, 252, 252, 255), outline=(218, 32, 36, 255), width=22)
    draw_centered(draw, x0 + 136, y0 + 128, "30", load_font(78), (24, 24, 28, 255))
    draw_centered(draw, x0 + 610, y0 + 96, "어린이 보호구역", load_font(72), (24, 24, 28, 255))
    draw_centered(draw, x0 + 610, y0 + 184, "SCHOOL ZONE · 천천히", load_font(40), (192, 32, 34, 255))

    # 63: Korean Bus-Only Lane & Speed 60 Sign
    x0, y0 = slot_xy(63)
    draw.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=(24, 92, 198, 255))
    draw.rounded_rectangle([x0 + 14, y0 + 14, x0 + CW - 15, y0 + CH - 15], radius=22, outline=(252, 252, 252, 255), width=10)
    draw.ellipse([x0 + 46, y0 + 38, x0 + 226, y0 + 218], fill=(252, 252, 252, 255), outline=(218, 32, 36, 255), width=22)
    draw_centered(draw, x0 + 136, y0 + 128, "60", load_font(78), (24, 24, 28, 255))
    draw_centered(draw, x0 + 610, y0 + 96, "버스전용차로", load_font(76), (252, 252, 255, 255))
    draw_centered(draw, x0 + 610, y0 + 184, "BUS ONLY · 07:00~21:00", load_font(40), (226, 238, 252, 255))

    out_path = "assets/textures/seoul_signs_4k.png"
    img.save(out_path, compress_level=6)
    print(f"Saved {out_path} ({os.path.getsize(out_path)/1e6:.2f} MB) in {time.time()-t0:.1f}s")


if __name__ == "__main__":
    main()
