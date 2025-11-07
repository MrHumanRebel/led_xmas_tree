#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// ---------- Color helpers ----------
inline uint32_t ColorW(uint8_t r,uint8_t g,uint8_t b,uint8_t w=0); // fwd

struct C4 { uint8_t r,g,b,w; };

class Col {
public:
  static uint32_t toCol(const C4& c){ return ColorW(c.r,c.g,c.b,c.w); } // csak vShow-ban használunk ColorW-t
  static C4 scale(const C4& c, uint8_t k){
    C4 o;
    o.r = (uint16_t)c.r * k / 255;
    o.g = (uint16_t)c.g * k / 255;
    o.b = (uint16_t)c.b * k / 255;
    o.w = (uint16_t)c.w * k / 255;
    return o;
  }
};

// HEX -> C4 {r,g,b,w}  (#RRGGBB | RRGGBB | #RGB | RGB)
static inline C4 hexToRgbw(const char* hex) {
  if (!hex) return C4{0,0,0,0};

  // --- helper: hex char -> 0..15, vagy -1 ha nem hex ---
  auto hv = [](char c)->int {
    if (c>='0' && c<='9') return c - '0';
    if (c>='a' && c<='f') return 10 + (c - 'a');
    if (c>='A' && c<='F') return 10 + (c - 'A');
    return -1;
  };

  // Ugorjuk át a '#' vagy '0x' / '0X' előtagot
  if (*hex == '#') ++hex;
  else if (hex[0]=='0' && (hex[1]=='x' || hex[1]=='X')) hex += 2;

  // Hossz meghatározása
  size_t n = 0;
  while (hex[n] && n < 8) ++n;

  uint8_t r=0,g=0,b=0;

  if (n == 6) {
    // RRGGBB
    int h1=hv(hex[0]), h2=hv(hex[1]),
        h3=hv(hex[2]), h4=hv(hex[3]),
        h5=hv(hex[4]), h6=hv(hex[5]);
    if (h1<0||h2<0||h3<0||h4<0||h5<0||h6<0) return C4{0,0,0,0};
    r = (uint8_t)((h1<<4)|h2);
    g = (uint8_t)((h3<<4)|h4);
    b = (uint8_t)((h5<<4)|h6);
  } else if (n == 3) {
    // RGB (4 bit/komponens → 8 bitre tükör)
    int h1=hv(hex[0]), h2=hv(hex[1]), h3=hv(hex[2]);
    if (h1<0||h2<0||h3<0) return C4{0,0,0,0};
    r = (uint8_t)((h1<<4)|h1);
    g = (uint8_t)((h2<<4)|h2);
    b = (uint8_t)((h3<<4)|h3);
  } else {
    return C4{0,0,0,0};
  }

  // --- RGB -> RGBW (StackOverflow minta alapján, CC BY-SA 3.0) ---
  auto clamp8 = [](int v)->uint8_t { return (uint8_t)(v<0?0:(v>255?255:v)); };

  float Ri = (float)r, Gi = (float)g, Bi = (float)b;

  float tM = (Ri > Gi ? (Ri > Bi ? Ri : Bi) : (Gi > Bi ? Gi : Bi)); // max(R,G,B)
  if (tM <= 0.0f) return C4{0,0,0,0};

  float multiplier = 255.0f / tM;
  float hR = Ri * multiplier;
  float hG = Gi * multiplier;
  float hB = Bi * multiplier;

  float M = (hR > hG ? (hR > hB ? hR : hB) : (hG > hB ? hG : hB)); // max(hR,hG,hB)
  float m = (hR < hG ? (hR < hB ? hR : hB) : (hG < hB ? hG : hB)); // min(hR,hG,hB)

  // „Whiteness” (nem szigorúan luminance), a minta szerint
  float Luminance = ((M + m) * 0.5f - 127.5f) * (255.0f/127.5f) / multiplier;

  int Wo = (int)lrintf(Luminance);
  int Bo = (int)lrintf(Bi - Luminance);
  int Ro = (int)lrintf(Ri - Luminance);
  int Go = (int)lrintf(Gi - Luminance);

  return C4{ clamp8(Ro), clamp8(Go), clamp8(Bo), clamp8(Wo) };
}


// Színek
const C4 BLUE = hexToRgbw("#00072D");
const C4 CYAN = hexToRgbw("#ADD8E6");
const C4 MINT = hexToRgbw("#2F4F4F");
const C4 RED = hexToRgbw("#3F070F");
const C4 MALLOW = hexToRgbw("#B86B77");
const C4 ROSEGOLD  = hexToRgbw("#B76E79");
const C4 LIGHTPINK = hexToRgbw("#F5428D");
const C4 GOLD      = hexToRgbw("#FFFAED");
const C4 SILVER    = hexToRgbw("#808080");
const C4 GREY    = hexToRgbw("#696969");

const C4 DARKPURPLE = hexToRgbw("#3A0475");
const C4 LIGHTPURPLE = hexToRgbw("#AB65F8");
const C4 PINK = hexToRgbw("#BC4DC1");

// ---------- LED / HW ----------
#define LED_TYPE_RGBW
#define NEOCOLORTYPE NEO_GRBW

const uint8_t PIN_STRIP_A = 4;
const uint8_t PIN_STRIP_B = 12;
const uint8_t PIN_STRIP_C = 1;

const uint16_t NUM_LEDS = 300;
const uint16_t VNUM     = NUM_LEDS * 3;
uint8_t globalMaxBrightness = 64;

#ifdef LED_TYPE_RGBW
Adafruit_NeoPixel stripA(NUM_LEDS, PIN_STRIP_A, NEOCOLORTYPE + NEO_KHZ800);
Adafruit_NeoPixel stripB(NUM_LEDS, PIN_STRIP_B, NEOCOLORTYPE + NEO_KHZ800);
Adafruit_NeoPixel stripC(NUM_LEDS, PIN_STRIP_C, NEOCOLORTYPE + NEO_KHZ800);
#else
Adafruit_NeoPixel stripA(NUM_LEDS, PIN_STRIP_A, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripB(NUM_LEDS, PIN_STRIP_B, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripC(NUM_LEDS, PIN_STRIP_C, NEO_GRB + NEO_KHZ800);
#endif
Adafruit_NeoPixel* S[3] = { &stripA, &stripB, &stripC };

inline uint32_t ColorW(uint8_t r,uint8_t g,uint8_t b,uint8_t w){
#ifdef LED_TYPE_RGBW
  return Adafruit_NeoPixel::Color(r,g,b,w);
#else
  (void)w; return Adafruit_NeoPixel::Color(r,g,b);
#endif
}

// ---------- Virtuális puffer: C4 alapú (nem packed) ----------
static C4 vbuf[VNUM];

inline void vSetC4(uint16_t i, const C4& c){ if (i<VNUM) vbuf[i]=c; }
inline void vFillC4(const C4& c){ for (uint16_t i=0;i<VNUM;i++) vbuf[i]=c; }

inline void vDim(uint8_t k){
  for (uint16_t i=0;i<VNUM;i++){
    C4 c = vbuf[i];
    vbuf[i] = Col::scale(c, k);
  }
}

void vShow(){
  for (uint16_t i=0;i<VNUM;i++){
    uint8_t  sidx = i / NUM_LEDS;
    uint16_t p    = i % NUM_LEDS;
    const C4& c = vbuf[i];
    S[sidx]->setPixelColor(p, ColorW(c.r,c.g,c.b,c.w));
  }
  for (auto* s : S){ s->setBrightness(globalMaxBrightness); s->show(); }
}

// ---------- Effekt lista + léptetési sorrend ----------
enum Effect : uint8_t {
  FX_STATIC_WARM = 0,
  FX_STATIC_COOL,
  FX_STATIC_MINT,
  FX_STAR_COOL,
  FX_STAR_PINK,
  FX_STAR_PURPLE,
  FX_PULSE_MALLOW,
  FX_RUN_SILVER,         
  FX_BLUE_GREY_SMOOTH,
  FX_CYAN_GREY_SMOOTH 
};

static const uint8_t EFFECT_ORDER[] = {
  FX_STATIC_WARM,
  FX_STATIC_COOL,
  FX_STATIC_MINT,
  FX_STAR_COOL,
  FX_STAR_PINK,
  FX_STAR_PURPLE,
  FX_PULSE_MALLOW,
  FX_RUN_SILVER,        
  FX_BLUE_GREY_SMOOTH,
  FX_CYAN_GREY_SMOOTH  
};


static const uint8_t EFFECT_COUNT = sizeof(EFFECT_ORDER)/sizeof(EFFECT_ORDER[0]);

// Effekt paraméterek
struct Params {
  uint8_t effect   = FX_STATIC_WARM;
  uint8_t speed    = 10;   // csak a nem-statikusok használják
  uint8_t density  = 10;
  uint8_t intensity= 40;
} P;

// Sebesség-függő időzítés
inline uint16_t stepIntervalMs(uint8_t speed, uint16_t slow=90, uint16_t fast=10) {
  uint8_t s = speed < 5 ? 5 : speed;
  long r = map(s, 5, 100, slow, fast);
  if (r < fast) r = fast; if (r > slow) r = slow;
  return (uint16_t)r;
}

// ---- Effektek (statikusoknak nincs speed param) ----
void fxStaticWarm(){ vFillC4(GOLD); }
void fxStaticCool(){ vFillC4(SILVER); }
void fxStaticMint(){ vFillC4(MINT); }

void fxStarCool(uint8_t speed){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;

  vDim(230);
  // halvány hideg fátyol
  C4 haze = Col::scale(SILVER, 30);
  for (uint16_t i=0;i<VNUM;i+=32){ vSetC4(i, haze); }

  uint8_t n   = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity,0,100, 160, 255);
  for (uint8_t k=0;k<n;k++){
    uint16_t p = random(VNUM);
    vSetC4(p, C4{0,0,0,amp}); // tiszta W szikra
  }
}

// 4) Csillag – lightpink
void fxStarPink(uint8_t speed){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;

  vDim(230);
  // halvány hideg fátyol
  C4 haze = Col::scale(LIGHTPINK, 30);
  for (uint16_t i=0;i<VNUM;i+=32){ vSetC4(i, haze); }

  uint8_t n   = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity,0,100, 160, 255);
  for (uint8_t k=0;k<n;k++){
    uint16_t p = random(VNUM);
    vSetC4(p, Col::scale(ROSEGOLD, amp));
  }
}


// 5) Nagyon lassú MALLOW pulzus – extra simítás (gamma + EMA)
void fxPulseMallow(uint8_t speed){
  // Sebesség skála
  uint8_t s = speed < 1 ? 1 : (speed > 100 ? 100 : speed);

  // Periódus (maradhat az eddigi karakter): 180s .. 8s
  uint32_t periodMs = map(s, 1, 100, 180000UL, 8000UL);

  // Fázis + „kozmosz” hullám
  uint32_t now = millis();
  float phase = (float)(now % periodMs) / (float)periodMs;       // 0..1
  float wave  = 0.5f * (1.0f - cosf(6.2831853f * phase));        // 0..1, sima cos-ease

  // Perceptuális simítás: gamma (LED szemre simább átmenet)
  // kisebb gamma -> lágyabb középtartomány
  const float gamma = 1.8f;
  float eased = powf(wave, gamma);                                // 0..1

  // Cél fényerő tartomány
  const uint8_t kMin = 26;   // ~10%
  const uint8_t kMax = 89;   // ~35%
  float targetK = (float)kMin + (float)(kMax - kMin) * eased;     // float cél

  // Időbeli simítás: EMA (sebességtől függő „ragacsosság”)
  // lassabb speed -> kisebb alpha -> simább
  static float kf = kMin;                                         // filt. fényerő
  float alpha = 0.04f + (0.18f - 0.04f) * ((float)(s - 1) / 99.0f);
  kf += alpha * (targetK - kf);

  uint8_t k = (uint8_t)(kf + 0.5f);

  C4 c = Col::scale(MALLOW, k);
  vFillC4(c);
}


// 6) Ezüst futó – előre egyesével felgyújt (a végére minden ég),
// majd visszafelé egyesével leolt (a végére minden sötét)
void fxRunSilver(uint8_t speed){
  static uint32_t t0 = 0;
  uint32_t now = millis();
  if (now - t0 < stepIntervalMs(speed, 70, 8)) return;  // azonos sebesség oda-vissza
  t0 = now;

  static int32_t pos = -1;   // -1: indulás előtt
  static int8_t  dir = +1;   // +1 előre, -1 vissza
  static bool    primed = false;

  // első belépés: minden sötét, előre indulunk
  if (!primed) {
    vFillC4(C4{0,0,0,0});
    pos = -1;
    dir = +1;
    primed = true;
  }

  const uint8_t amp = map(P.intensity, 0, 100, 80, 255);
  const C4 on  = Col::scale(SILVER, amp);
  const C4 off = C4{0,0,0,0};

  if (dir > 0) {
    // ELŐRE: minden ticknél egy új LED-et gyújtunk fel
    pos++;
    if (pos >= (int32_t)VNUM) pos = VNUM - 1;   // biztos ami biztos
    vSetC4((uint16_t)pos, on);

    // ha elértük a legvégét, irányt váltunk (mostanra MIND ég)
    if (pos == (int32_t)VNUM - 1) {
      dir = -1;
    }
  } else {
    // VISSZA: minden ticknél egy LED-et oltunk le
    if (pos >= 0 && pos < (int32_t)VNUM) {
      vSetC4((uint16_t)pos, off);
    }
    pos--;

    // ha a legelejéig leoltottunk, mind sötét -> indul újra előre
    if (pos < 0) {
      vFillC4(off);   // biztosan minden 0
      dir = +1;
      pos = -1;       // új gyújtási ciklus indul
    }
  }
}


// 7) BLUE/GREY smooth 60–40% – véletlen célminta, folyamatos lágy átúszással
void fxBlueGreySmooth(uint8_t speed){
  // tick időzítés: lassú -> ritkább keverés, gyors -> sűrűbb
  static uint32_t tTick = 0;
  uint32_t now = millis();
  if (now - tTick < stepIntervalMs(speed, 110, 15)) return;
  tTick = now;

  // célminta (BLUE/GREY) és időnkénti teljes újrasorsolás
  static bool     init = false;
  static C4       target[VNUM];
  static uint32_t tReseed = 0;

  // segédek
  auto lerpC4 = [](const C4& a, const C4& b, uint8_t t)->C4 {
    C4 o;
    o.r = a.r + ((int)b.r - a.r) * t / 255;
    o.g = a.g + ((int)b.g - a.g) * t / 255;
    o.b = a.b + ((int)b.b - a.b) * t / 255;
    o.w = a.w + ((int)b.w - a.w) * t / 255;
    return o;
  };

  // sebességből időzítés és keverési lépés
  uint8_t s = speed < 1 ? 1 : (speed > 100 ? 100 : speed);
  uint32_t reseedMs = map(s, 1, 100, 6000, 1200);

  // arányok: 60% BLUE, 40% GREY
  const uint16_t blueCount = (uint32_t)VNUM * 60 / 100;
  const uint16_t greyCount = VNUM - blueCount; (void)greyCount;

  auto reshuffleTargets = [&](){
    static uint16_t idx[VNUM];
    for (uint16_t i=0;i<VNUM;i++) idx[i]=i;
    for (uint16_t i=VNUM-1;i>0;i--) {
      uint16_t j = random(i+1);
      uint16_t tmp = idx[i]; idx[i]=idx[j]; idx[j]=tmp;
    }
    // első blueCount -> BLUE, maradék -> GREY
    for (uint16_t k=0;k<VNUM;k++){
      target[idx[k]] = (k < blueCount) ? BLUE : GREY;
    }
  };

  if (!init) {
    reshuffleTargets();
    for (uint16_t i=0;i<VNUM;i++) vbuf[i] = target[i]; // sima indulás
    tReseed = now;
    init = true;
  }

  if (now - tReseed >= reseedMs) {
    reshuffleTargets();
    tReseed = now;
  }

  // keverési lépés (alpha): mennyit „közelítünk” a cél felé
  uint8_t alpha = map(s, 1, 100, 10, 32);

  for (uint16_t i=0;i<VNUM;i++){
    vbuf[i] = lerpC4(vbuf[i], target[i], alpha);
  }
}


// 8) CYAN/GREY smooth 50–50% – véletlen célminta, folyamatos lágy átúszással
void fxCyanGreySmooth(uint8_t speed){
  // tick időzítés: lassú -> ritkább keverés, gyors -> sűrűbb
  static uint32_t tTick = 0;
  uint32_t now = millis();
  if (now - tTick < stepIntervalMs(speed, 110, 15)) return;
  tTick = now;

  // célminta (CYAN/GREY) és időnkénti teljes újrasorsolás
  static bool     init = false;
  static C4       target[VNUM];
  static uint32_t tReseed = 0;

  // segédek
  auto lerpC4 = [](const C4& a, const C4& b, uint8_t t)->C4 {
    C4 o;
    o.r = a.r + ((int)b.r - a.r) * t / 255;
    o.g = a.g + ((int)b.g - a.g) * t / 255;
    o.b = a.b + ((int)b.b - a.b) * t / 255;
    o.w = a.w + ((int)b.w - a.w) * t / 255;
    return o;
  };

  // sebességből időzítés és keverési lépés
  uint8_t s = speed < 1 ? 1 : (speed > 100 ? 100 : speed);
  uint32_t reseedMs = map(s, 1, 100, 6000, 1200);

  // pontos 50–50%: half CYAN, half GREY (páratlan esetben az egyik +1-et kap)
  auto reshuffleTargets = [&](){
    static uint16_t idx[VNUM];
    for (uint16_t i=0;i<VNUM;i++) idx[i]=i;
    for (uint16_t i=VNUM-1;i>0;i--) {
      uint16_t j = random(i+1);
      uint16_t tmp = idx[i]; idx[i]=idx[j]; idx[j]=tmp;
    }
    uint16_t half = VNUM / 2;
    // első fele CYAN, második fele GREY
    for (uint16_t k=0;k<VNUM;k++){
      target[idx[k]] = (k < half) ? CYAN : GREY;
    }
    // ha páratlan, a maradék 1 LED GREY lesz (k>=half igaz rá)
  };

  if (!init) {
    reshuffleTargets();
    for (uint16_t i=0;i<VNUM;i++) vbuf[i] = target[i]; // sima indulás
    tReseed = now;
    init = true;
  }

  if (now - tReseed >= reseedMs) {
    reshuffleTargets();
    tReseed = now;
  }

  // keverési lépés (alpha): mennyit „közelítünk” a cél felé
  uint8_t alpha = map(s, 1, 100, 10, 32);

  for (uint16_t i=0;i<VNUM;i++){
    vbuf[i] = lerpC4(vbuf[i], target[i], alpha);
  }
}


// 9) Csillag – DARKPURPLE / LIGHTPURPLE / PINK
void fxStarPurple(uint8_t speed){
  static uint32_t t0=0; 
  uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;

  // enyhe elhalványítás a háttér felé
  vDim(230);

  // halvány lila fátyol (LIGHTPURPLE)
  C4 haze = Col::scale(LIGHTPURPLE, 30);
  for (uint16_t i=0;i<VNUM;i+=32){ vSetC4(i, haze); }

  // szikra szám & amplitúdó az aktuális beállításokból
  uint8_t n   = map(P.density,   0,100, 0, 10);
  uint8_t amp = map(P.intensity, 0,100, 160, 255);

  // háromszínes paletta
  const C4* PAL[3] = { &DARKPURPLE, &LIGHTPURPLE, &PINK };

  for (uint8_t k=0;k<n;k++){
    uint16_t p = random(VNUM);
    const C4& base = *PAL[random(3)];
    vSetC4(p, Col::scale(base, amp));
  }
}

// ---------- Renderer ----------
uint32_t lastFrame   = 0;
const uint16_t MIN_FRAME_MS = 8;

void renderAll() {
  uint32_t now = millis();
  if (now - lastFrame < MIN_FRAME_MS) return;
  lastFrame = now;

  switch (P.effect) {
    case FX_STATIC_WARM:        fxStaticWarm();                 break;
    case FX_STATIC_COOL:        fxStaticCool();                 break;
    case FX_STATIC_MINT:        fxStaticMint();                 break;
    case FX_STAR_COOL:          fxStarCool(P.speed);            break;
    case FX_STAR_PINK:          fxStarPink(P.speed);            break;
    case FX_PULSE_MALLOW:       fxPulseMallow(P.speed);         break;
    case FX_RUN_SILVER:         fxRunSilver(P.speed);           break;   
    case FX_BLUE_GREY_SMOOTH:   fxBlueGreySmooth(P.speed);      break;   
    case FX_CYAN_GREY_SMOOTH:   fxCyanGreySmooth(P.speed);      break;
    case FX_STAR_PURPLE:        fxStarPurple(P.speed);          break;
  }

  vShow();
  yield(); delay(1);
}

// ---------- Rotary (interrupt alapú, azonnali léptetés) ----------
const uint8_t ENC_PIN_A   = 2;   // INT0 (UNO/Nano), ESP-n is interruptos
const uint8_t ENC_PIN_B   = 3;   // INT1
const uint8_t ENC_PIN_BTN = 16;  // gomb

volatile uint8_t encPrevAB = 0;
volatile int8_t  encAccum  = 0;
volatile int8_t  encSteps  = 0;
#define ENC_DETENT 4


const int8_t QDEC_LUT[16] = {
  0, -1, +1,  0,
  +1, 0,  0, -1,
  -1, 0,  0, +1,
   0, +1, -1, 0
};

void IRAM_ATTR isrEnc() {
  uint8_t a = digitalRead(ENC_PIN_A);
  uint8_t b = digitalRead(ENC_PIN_B);
  uint8_t ab = (a?1:0) | (b?2:0);
  uint8_t idx = ((encPrevAB & 0x03) << 2) | (ab & 0x03);
  encPrevAB = ab;
  int8_t d = QDEC_LUT[idx];
  if (!d) return;
  int8_t acc = encAccum + d;
  encAccum = acc;
  if (acc >= ENC_DETENT){ encAccum = 0; encSteps++; }
  else if (acc <= -ENC_DETENT){ encAccum = 0; encSteps--; }
}

// keresd meg az aktuális effekt indexét az EFFECT_ORDER-ben
static int8_t indexOfEffect(uint8_t eff){
  for (uint8_t i=0;i<EFFECT_COUNT;i++) if (EFFECT_ORDER[i]==eff) return (int8_t)i;
  return 0;
}

// az effekthez illő default sebesség (csak a nem-statikusoknál használt)
static uint8_t defaultSpeedFor(uint8_t eff){
  switch (eff){
    case FX_STAR_COOL:          return 3;
    case FX_STAR_PINK:          return 3;
    case FX_PULSE_MALLOW:       return 80;
    case FX_RUN_SILVER:         return 220;  
    case FX_BLUE_GREY_SMOOTH:   return 50; 
    case FX_CYAN_GREY_SMOOTH:   return 50;
    case FX_STAR_PURPLE:        return 5;
    default:                    return 0;
  }
}


void applyEncoderImmediate(){
  // backlog eldobása, pontosan 1 lépés előre
  noInterrupts();
  int8_t hadSteps = encSteps;
  encSteps = 0;
  interrupts();

  if (!hadSteps) return;

  int8_t idx = indexOfEffect(P.effect);
  idx++;
  if (idx >= (int8_t)EFFECT_COUNT) idx = 0;

  // beállítjuk az új effektet + default speedet
  P.effect = EFFECT_ORDER[idx];
  P.speed  = defaultSpeedFor(P.effect);

  // --- BLACKOUT azonnal, mielőtt az új effekt elkezd rajzolni ---
  vFillC4(C4{0,0,0,0});  // virtuális puffer kinullázása
  vShow();               // azonnal áttoljuk a szalagra
  delay(1);              // pici lélegzet (nem kötelező, de szép)
}


// ---------- Button ----------
bool btnLast = false;
uint32_t btnLastMs = 0;
const uint16_t BTN_DEBOUNCE_MS = 30;

void buttonPoll(){
  bool s = digitalRead(ENC_PIN_BTN);
  uint32_t now = millis();
  if (s != btnLast && (now - btnLastMs) > BTN_DEBOUNCE_MS){
    btnLast = s;
    btnLastMs = now;
    if (s == HIGH){
      static const uint8_t stepsPct[] = { 13, 64, 102, 140, 178, 216, 255, 216, 178, 140, 102, 64, 13 };
      static uint8_t idx = 0;
      idx = (idx + 1) % (sizeof(stepsPct)/sizeof(stepsPct[0]));
      globalMaxBrightness = stepsPct[idx];
      for (auto* s : S) s->setBrightness(globalMaxBrightness);
    }
  }
}

void setup(){
  for (auto*s:S){ s->begin(); s->setBrightness(globalMaxBrightness); s->clear(); s->show(); delay(1); }

  pinMode(ENC_PIN_A,   INPUT_PULLUP);
  pinMode(ENC_PIN_B,   INPUT_PULLUP);
  pinMode(ENC_PIN_BTN, INPUT_PULLDOWN_16); // UNO/Nano-n: INPUT_PULLUP + invertált logika

  encPrevAB = (digitalRead(ENC_PIN_A)?1:0) | (digitalRead(ENC_PIN_B)?2:0);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_A), isrEnc, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_B), isrEnc, CHANGE);

  P.effect = FX_STATIC_WARM;
  P.speed  = defaultSpeedFor(P.effect);
  fxStaticWarm();
  vShow();
}

void loop(){
  applyEncoderImmediate();  // azonnali 1-lépéses előre váltás
  buttonPoll();
  renderAll();
}
