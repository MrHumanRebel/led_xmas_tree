// ==============================
// Xmas Tree RGBW (ESP8266 only) + WiFi AP + Web UI
// HBP (iamh2o) RGB->RGBW HEX konverter + hue-preserving dim
// ==============================

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <stdint.h>

// -------- WiFi / Web --------
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// ---------- LED TYPE FIRST (ColorW ezt használja) ----------
#define LED_TYPE_RGBW
#define NEOCOLORTYPE NEO_GRBW

// ---------- PACK helper ----------
static inline uint32_t ColorW(uint8_t r,uint8_t g,uint8_t b,uint8_t w=0){
#ifdef LED_TYPE_RGBW
  return Adafruit_NeoPixel::Color(r,g,b,w);
#else
  (void)w; return Adafruit_NeoPixel::Color(r,g,b);
#endif
}

// ---------- Alaptípusok ----------
struct C4   { uint8_t r,g,b,w; };
struct RGB8 { uint8_t r,g,b;   };
struct RGBW8{ uint8_t r,g,b,w; };

// ---------- (vissza) Col helper a skálázáshoz ----------
struct Col {
  static C4 scale(const C4& c, uint8_t k){
    C4 o;
    o.r = (uint16_t)c.r * k / 255;
    o.g = (uint16_t)c.g * k / 255;
    o.b = (uint16_t)c.b * k / 255;
    o.w = (uint16_t)c.w * k / 255;
    return o;
  }
};

// ---------- HEX -> C4 {r,g,b,w} ----------
namespace HBPConv {
  static inline C4 hexToRgbw(const char* hex) {
    if (!hex) return C4{0,0,0,0};
    auto hv = [](char c)->int {
      if (c>='0' && c<='9') return c - '0';
      if (c>='a' && c<='f') return 10 + (c - 'a');
      if (c>='A' && c<='F') return 10 + (c - 'A');
      return -1;
    };
    if (*hex == '#') ++hex;
    else if (hex[0]=='0' && (hex[1]=='x' || hex[1]=='X')) hex += 2;
    size_t n = 0; while (hex[n] && n < 8) ++n;

    uint8_t r=0,g=0,b=0;
    if (n == 6) {
      int h1=hv(hex[0]), h2=hv(hex[1]),
          h3=hv(hex[2]), h4=hv(hex[3]),
          h5=hv(hex[4]), h6=hv(hex[5]);
      if (h1<0||h2<0||h3<0||h4<0||h5<0||h6<0) return C4{0,0,0,0};
      r = (uint8_t)((h1<<4)|h2);
      g = (uint8_t)((h3<<4)|h4);
      b = (uint8_t)((h5<<4)|h6);
    } else if (n == 3) {
      int h1=hv(hex[0]), h2=hv(hex[1]), h3=hv(hex[2]);
      if (h1<0||h2<0||h3<0) return C4{0,0,0,0};
      r = (uint8_t)((h1<<4)|h1);
      g = (uint8_t)((h2<<4)|h2);
      b = (uint8_t)((h3<<4)|h3);
    } else {
      return C4{0,0,0,0};
    }

    auto clamp8 = [](int v)->uint8_t { return (uint8_t)(v<0?0:(v>255?255:v)); };

    float Ri = (float)r, Gi = (float)g, Bi = (float)b;
    float tM = (Ri > Gi ? (Ri > Bi ? Ri : Bi) : (Gi > Bi ? Gi : Bi));
    if (tM <= 0.0f) return C4{0,0,0,0};

    float multiplier = 255.0f / tM;
    float hR = Ri * multiplier;
    float hG = Gi * multiplier;
    float hB = Bi * multiplier;

    float M = (hR > hG ? (hR > hB ? hR : hB) : (hG > hB ? hG : hB));
    float m = (hR < hG ? (hR < hB ? hR : hB) : (hG < hB ? hG : hB));
    float Luminance = ((M + m) * 0.5f - 127.5f) * (255.0f/127.5f) / multiplier;

    int Wo = (int)lrintf(Luminance);
    int Bo = (int)lrintf(Bi - Luminance);
    int Ro = (int)lrintf(Ri - Luminance);
    int Go = (int)lrintf(Gi - Luminance);

    return C4{ clamp8(Ro), clamp8(Go), clamp8(Bo), clamp8(Wo) };
  }
}
using HBPConv::hexToRgbw;

// ---------- LED / HW ----------
const uint8_t PIN_STRIP_A = 4;   // D2
const uint8_t PIN_STRIP_B = 12;  // D6
const uint8_t PIN_STRIP_C = 1;   // TX (ne használj Serialt)
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

// ---------- Virtuális puffer ----------
struct VB {
  static C4 vbuf[VNUM];

  static inline void set(uint16_t i, const C4& c){ if (i<VNUM) vbuf[i]=c; }
  static inline void fill(const C4& c){ for (uint16_t i=0;i<VNUM;i++) vbuf[i]=c; }

  // Hue-lock dim
  static inline void dim(uint8_t k){
    if (k >= 255) return;
    float f = (float)k / 255.0f;
    for (uint16_t i=0;i<VNUM;i++){
      C4 c = vbuf[i];
      if ((c.r|c.g|c.b|c.w)==0) continue;
      float r=c.r/255.0f, g=c.g/255.0f, b=c.b/255.0f, w=c.w/255.0f;
      float m=r; if(g>m)m=g; if(b>m)m=b; if(w>m)m=w;
      if (m<=0.0f){ vbuf[i]=C4{0,0,0,0}; continue; }
      float kr=r/m, kg=g/m, kb=b/m, kw=w/m;
      float m2 = m*f;
      uint8_t rn = (uint8_t)(m2*255.0f*kr + 0.5f);
      uint8_t gn = (uint8_t)(m2*255.0f*kg + 0.5f);
      uint8_t bn = (uint8_t)(m2*255.0f*kb + 0.5f);
      uint8_t wn = (uint8_t)(m2*255.0f*kw + 0.5f);
      vbuf[i] = C4{rn,gn,bn,wn};
    }
  }

  static void show(){
    for (uint16_t i=0;i<VNUM;i++){
      uint8_t  sidx = i / NUM_LEDS;
      uint16_t p    = i % NUM_LEDS;
      const C4& c = vbuf[i];
      S[sidx]->setPixelColor(p, ColorW(c.r,c.g,c.b,c.w));
    }
    for (auto* s : S){ s->setBrightness(globalMaxBrightness); s->show(); }
  }
};
C4 VB::vbuf[VNUM];

static void VB_show_with_phase()
{
  static uint8_t s_lastBrightness = 255;
  bool brChanged = (s_lastBrightness != globalMaxBrightness);
  if (brChanged) s_lastBrightness = globalMaxBrightness;

  for (uint16_t i=0; i<VNUM; ++i) {
    uint8_t  sidx = i / NUM_LEDS;
    uint16_t p    = i % NUM_LEDS;
    const C4& c = VB::vbuf[i];
    S[sidx]->setPixelColor(p, ColorW(c.r, c.g, c.b, c.w));
  }

  const uint16_t PHASE_US = 3000;
  for (int i = 0; i < 3; ++i) {
    if (brChanged) S[i]->setBrightness(s_lastBrightness);
    S[i]->show();
    if (i < 2) delayMicroseconds(PHASE_US);
  }
}

// ---------- SZÍNPALETTA (MÓDOSÍTHATÓ) ----------
struct Palette {
  C4 BLUE, CYAN, MINT, MALLOW, ROSEGOLD, LIGHTPINK, GOLD, SILVER, GREY, DARKPURPLE, LIGHTPURPLE, PINK;
} PAL = {
  hexToRgbw("#00072D"), // BLUE
  hexToRgbw("#ADD8E6"), // CYAN
  hexToRgbw("#2F4F4F"), // MINT
  hexToRgbw("#B86B77"), // MALLOW
  hexToRgbw("#B76E79"), // ROSEGOLD
  hexToRgbw("#F5428D"), // LIGHTPINK
  hexToRgbw("#FFFAED"), // GOLD
  hexToRgbw("#808080"), // SILVER
  hexToRgbw("#696969"), // GREY
  hexToRgbw("#3A0475"), // DARKPURPLE
  hexToRgbw("#AB65F8"), // LIGHTPURPLE
  hexToRgbw("#BC4DC1")  // PINK
};

// ---------- Effekt lista ----------
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
  FX_STATIC_WARM, FX_STATIC_COOL, FX_STATIC_MINT,
  FX_STAR_COOL,   FX_STAR_PINK,   FX_STAR_PURPLE,
  FX_PULSE_MALLOW, FX_RUN_SILVER,
  FX_BLUE_GREY_SMOOTH, FX_CYAN_GREY_SMOOTH
};
static const uint8_t EFFECT_COUNT = sizeof(EFFECT_ORDER)/sizeof(EFFECT_ORDER[0]);

struct Params {
  uint8_t effect   = FX_STATIC_WARM;
  uint8_t speed    = 10;
  uint8_t density  = 10;
  uint8_t intensity= 40;
} P;

static inline uint16_t stepIntervalMs(uint8_t speed, uint16_t slow=90, uint16_t fast=10) {
  uint8_t s = speed < 5 ? 5 : speed;
  long r = map(s, 5, 100, slow, fast);
  if (r < fast) r = fast; if (r > slow) r = slow;
  return (uint16_t)r;
}

// ---------- Effektek ----------
static void fxStaticWarm(){ VB::fill(PAL.GOLD); }
static void fxStaticCool(){ VB::fill(PAL.SILVER); }
static void fxStaticMint(){ VB::fill(PAL.MINT); }

static void fxStarCool(uint8_t speed){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;
  VB::dim(230);
  C4 haze = Col::scale(PAL.SILVER, 30);
  for (uint16_t i=0;i<VNUM;i+=32) VB::set(i, haze);
  uint8_t n   = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity,0,100, 160, 255);
  for (uint8_t k=0;k<n;k++){ VB::set(random(VNUM), C4{0,0,0,amp}); }
}

static void fxStarPink(uint8_t speed){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;
  VB::dim(230);
  C4 haze = Col::scale(PAL.LIGHTPINK, 30);
  for (uint16_t i=0;i<VNUM;i+=32) VB::set(i, haze);
  uint8_t n   = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity,0,100, 160, 255);
  for (uint8_t k=0;k<n;k++){ VB::set(random(VNUM), Col::scale(PAL.ROSEGOLD, amp)); }
}

static void fxStarPurple(uint8_t speed){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;
  VB::dim(230);
  C4 haze = Col::scale(PAL.LIGHTPURPLE, 30);
  for (uint16_t i=0;i<VNUM;i+=32) VB::set(i, haze);
  uint8_t n   = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity, 0,100, 160, 255);
  const C4* T[3] = { &PAL.DARKPURPLE, &PAL.LIGHTPURPLE, &PAL.PINK };
  for (uint8_t k=0;k<n;k++){
    const C4& base = *T[random(3)];
    VB::set(random(VNUM), Col::scale(base, amp));
  }
}

// Selymes MALLOW pulzus
static void fxPulseMallow(uint8_t speed){
  uint8_t s = speed < 1 ? 1 : (speed > 100 ? 100 : speed);
  uint32_t periodMs = map(s, 1, 100, 180000UL, 8000UL);
  uint32_t now = millis();
  float phase = (float)(now % periodMs) / (float)periodMs;
  float wave  = 0.5f * (1.0f - cosf(6.2831853f * phase));
  const uint8_t kMin = 26, kMax = 89;
  float targetK = (float)kMin + (float)(kMax - kMin) * wave;
  static float kf = kMin;
  float alpha = 0.06f;
  kf += alpha * (targetK - kf);
  uint8_t k = (uint8_t)(kf + 0.5f);
  VB::fill(Col::scale(PAL.MALLOW, k));
}

// Ezüst futó, mindig elejéről indul
volatile uint8_t g_effectActive = 0xFF;
bool g_fxJustActivated = false;

static void fxRunSilver(uint8_t speed){
  static uint32_t t0 = 0;
  static int32_t  pos = -1;
  static int8_t   dir = +1;
  static bool     primed = false;

  if (g_fxJustActivated) { primed=false; pos=-1; dir=+1; t0=0; }

  uint32_t now = millis();
  if (now - t0 < stepIntervalMs(speed, 20, 4)) return;  // 2× gyorsabb
  t0 = now;

  if (!primed) { VB::fill(C4{0,0,0,0}); pos=-1; dir=+1; primed=true; }

  const uint8_t amp = map(P.intensity, 0, 100, 80, 255);
  const C4 on  = Col::scale(PAL.SILVER, amp);
  const C4 off = C4{0,0,0,0};

  if (dir > 0) {
    pos++; if (pos >= (int32_t)VNUM) pos = VNUM - 1;
    VB::set((uint16_t)pos, on);
    if (pos == (int32_t)VNUM - 1) dir = -1;
  } else {
    if (pos >= 0 && pos < (int32_t)VNUM) VB::set((uint16_t)pos, off);
    pos--;
    if (pos < 0) { VB::fill(off); dir = +1; pos = -1; }
  }
}

static void fxBlueGreySmooth(uint8_t speed){
  static uint32_t tTick = 0;
  uint32_t now = millis();
  if (now - tTick < stepIntervalMs(speed, 110, 15)) return;
  tTick = now;

  static bool     init = false;
  static C4       target[VNUM];
  static uint32_t tReseed = 0;

  auto lerpC4 = [](const C4& a, const C4& b, uint8_t t)->C4 {
    C4 o;
    o.r = a.r + ((int)b.r - a.r) * t / 255;
    o.g = a.g + ((int)b.g - a.g) * t / 255;
    o.b = a.b + ((int)b.b - a.b) * t / 255;
    o.w = a.w + ((int)b.w - a.w) * t / 255;
    return o;
  };

  uint8_t s = speed < 1 ? 1 : (speed > 100 ? 100 : speed);
  uint32_t reseedMs = map(s, 1, 100, 6000, 1200);

  const uint16_t blueCount = (uint32_t)VNUM * 60 / 100;
  auto reshuffleTargets = [&](){
    static uint16_t idx[VNUM];
    for (uint16_t i=0;i<VNUM;i++) idx[i]=i;
    for (uint16_t i=VNUM-1;i>0;i--) { uint16_t j = random(i+1); uint16_t tmp = idx[i]; idx[i]=idx[j]; idx[j]=tmp; }
    for (uint16_t k=0;k<VNUM;k++) target[idx[k]] = (k < blueCount) ? PAL.BLUE : PAL.GREY;
  };

  if (!init) { reshuffleTargets(); for (uint16_t i=0;i<VNUM;i++) VB::vbuf[i] = target[i]; tReseed = now; init = true; }
  if (now - tReseed >= reseedMs) { reshuffleTargets(); tReseed = now; }
  uint8_t alpha = map(s, 1, 100, 10, 32);
  for (uint16_t i=0;i<VNUM;i++) VB::vbuf[i] = lerpC4(VB::vbuf[i], target[i], alpha);
}

static void fxCyanGreySmooth(uint8_t speed){
  static uint32_t tTick = 0;
  uint32_t now = millis();
  if (now - tTick < stepIntervalMs(speed, 110, 15)) return;
  tTick = now;

  static bool     init = false;
  static C4       target[VNUM];
  static uint32_t tReseed = 0;

  auto lerpC4 = [](const C4& a, const C4& b, uint8_t t)->C4 {
    C4 o;
    o.r = a.r + ((int)b.r - a.r) * t / 255;
    o.g = a.g + ((int)b.g - a.g) * t / 255;
    o.b = a.b + ((int)b.b - a.b) * t / 255;
    o.w = a.w + ((int)b.w - a.w) * t / 255;
    return o;
  };

  uint8_t s = speed < 1 ? 1 : (speed > 100 ? 100 : speed);
  uint32_t reseedMs = map(s, 1, 100, 6000, 1200);

  auto reshuffleTargets = [&](){
    static uint16_t idx[VNUM];
    for (uint16_t i=0;i<VNUM;i++) idx[i]=i;
    for (uint16_t i=VNUM-1;i>0;i--) { uint16_t j = random(i+1); uint16_t tmp = idx[i]; idx[i]=idx[j]; idx[j]=tmp; }
    uint16_t half = VNUM / 2;
    for (uint16_t k=0;k<VNUM;k++) target[idx[k]] = (k < half) ? PAL.CYAN : PAL.GREY;
  };

  if (!init) { reshuffleTargets(); for (uint16_t i=0;i<VNUM;i++) VB::vbuf[i] = target[i]; tReseed = now; init = true; }
  if (now - tReseed >= reseedMs) { reshuffleTargets(); tReseed = now; }
  uint8_t alpha = map(s, 1, 100, 10, 32);
  for (uint16_t i=0;i<VNUM;i++) VB::vbuf[i] = lerpC4(VB::vbuf[i], target[i], alpha);
}

// ---------- Renderer ----------
uint32_t lastFrame   = 0;
const uint16_t MIN_FRAME_MS = 8;

static void renderAll() {
  uint32_t now = millis();
  if (now - lastFrame < MIN_FRAME_MS) return;
  lastFrame = now;

  static uint8_t prevEffect = 0xFF;
  if (P.effect != prevEffect) { prevEffect = P.effect; g_effectActive = P.effect; g_fxJustActivated = true; }
  else { g_fxJustActivated = false; }

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

  VB_show_with_phase();
  yield(); delay(1);
}

// ---------- Rotary & Button ----------
const uint8_t ENC_PIN_A   = 2;
const uint8_t ENC_PIN_B   = 3;
const uint8_t ENC_PIN_BTN = 16;

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

static int8_t indexOfEffect(uint8_t eff){
  for (uint8_t i=0;i<EFFECT_COUNT;i++) if (EFFECT_ORDER[i]==eff) return (int8_t)i;
  return 0;
}

static uint8_t defaultSpeedFor(uint8_t eff){
  switch (eff){
    case FX_STAR_COOL:          return 3;
    case FX_STAR_PINK:          return 3;
    case FX_PULSE_MALLOW:       return 80;
    case FX_RUN_SILVER:         return 50;
    case FX_BLUE_GREY_SMOOTH:   return 50;
    case FX_CYAN_GREY_SMOOTH:   return 50;
    case FX_STAR_PURPLE:        return 5;
    default:                    return 0;
  }
}

static void applyEncoderImmediate(){
  noInterrupts();
  int8_t hadSteps = encSteps;
  encSteps = 0;
  interrupts();
  if (!hadSteps) return;

  int8_t idx = indexOfEffect(P.effect);
  idx++; if (idx >= (int8_t)EFFECT_COUNT) idx = 0;

  P.effect = EFFECT_ORDER[idx];
  P.speed  = defaultSpeedFor(P.effect);

  VB::fill(C4{0,0,0,0});
  VB_show_with_phase();
  delay(1);
}

// gomb: lépcsőzetes globális max fényerő
bool btnLast = false;
uint32_t btnLastMs = 0;
const uint16_t BTN_DEBOUNCE_MS = 30;

static void buttonPoll(){
  bool s = digitalRead(ENC_PIN_BTN);
  uint32_t now = millis();
  if (s != btnLast && (now - btnLastMs) > BTN_DEBOUNCE_MS){
    btnLast = s; btnLastMs = now;
    if (s == HIGH){
      static const uint8_t stepsPct[] = { 13, 38, 64, 89, 115, 140, 166, 191, 217, 191, 166, 140, 115, 89, 64, 38, 13 };
      static uint8_t idx = 0;
      idx = (idx + 1) % (sizeof(stepsPct)/sizeof(stepsPct[0]));
      globalMaxBrightness = stepsPct[idx];
      for (auto* s : S) s->setBrightness(globalMaxBrightness);
    }
  }
}

// ---------- WiFi AP + Web UI ----------
const char* AP_SSID = "XmasTree";
const char* AP_PASS = "1230@1230"; // >=8 char (WPA2)

ESP8266WebServer server(80);
DNSServer dns;

// -- ministring helper for safe arg get
static String argOr(const String& name, const String& def=""){
  return server.hasArg(name) ? server.arg(name) : def;
}

// --- CORS / JSON headers
static void addCORS(){
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// name->C4 mutató
static C4* palettePtrByName(const String& n){
  if (n=="BLUE") return &PAL.BLUE;
  if (n=="CYAN") return &PAL.CYAN;
  if (n=="MINT") return &PAL.MINT;
  if (n=="MALLOW") return &PAL.MALLOW;
  if (n=="ROSEGOLD") return &PAL.ROSEGOLD;
  if (n=="LIGHTPINK") return &PAL.LIGHTPINK;
  if (n=="GOLD") return &PAL.GOLD;
  if (n=="SILVER") return &PAL.SILVER;
  if (n=="GREY") return &PAL.GREY;
  if (n=="DARKPURPLE") return &PAL.DARKPURPLE;
  if (n=="LIGHTPURPLE") return &PAL.LIGHTPURPLE;
  if (n=="PINK") return &PAL.PINK;
  return nullptr;
}

// state JSON
static void handleState(){
  addCORS();
  String js = "{";
  js += "\"effect\":" + String((int)P.effect) + ",";
  js += "\"speed\":" + String((int)P.speed) + ",";
  js += "\"density\":" + String((int)P.density) + ",";
  js += "\"intensity\":" + String((int)P.intensity) + ",";
  js += "\"brightness\":" + String((int)globalMaxBrightness) + ",";
  auto addC4 = [&](const C4& c){
    char buf[16];
    sprintf(buf,"#%02X%02X%02X", c.r, c.g, c.b); // fehéret külön kezeljük
    return String("{\"rgb\":\"") + buf + "\",\"w\":" + String((int)c.w) + "}";
  };
  js += "\"palette\":{";
  js += "\"BLUE\":"        + addC4(PAL.BLUE)        + ",";
  js += "\"CYAN\":"        + addC4(PAL.CYAN)        + ",";
  js += "\"MINT\":"        + addC4(PAL.MINT)        + ",";
  js += "\"MALLOW\":"      + addC4(PAL.MALLOW)      + ",";
  js += "\"ROSEGOLD\":"    + addC4(PAL.ROSEGOLD)    + ",";
  js += "\"LIGHTPINK\":"   + addC4(PAL.LIGHTPINK)   + ",";
  js += "\"GOLD\":"        + addC4(PAL.GOLD)        + ",";
  js += "\"SILVER\":"      + addC4(PAL.SILVER)      + ",";
  js += "\"GREY\":"        + addC4(PAL.GREY)        + ",";
  js += "\"DARKPURPLE\":"  + addC4(PAL.DARKPURPLE)  + ",";
  js += "\"LIGHTPURPLE\":" + addC4(PAL.LIGHTPURPLE) + ",";
  js += "\"PINK\":"        + addC4(PAL.PINK);
  js += "}}";
  server.send(200, "application/json", js);
}

// set FX: /api/setFX?effect=ID&speed=..&density=..&intensity=..
static void handleSetFX(){
  addCORS();
  if (server.hasArg("effect")) {
    int e = constrain(argOr("effect").toInt(), 0, (int)FX_CYAN_GREY_SMOOTH);
    P.effect = (uint8_t)e;
  }
  if (server.hasArg("speed"))     P.speed     = (uint8_t)constrain(argOr("speed").toInt(), 0, 100);
  if (server.hasArg("density"))   P.density   = (uint8_t)constrain(argOr("density").toInt(), 0, 100);
  if (server.hasArg("intensity")) P.intensity = (uint8_t)constrain(argOr("intensity").toInt(), 0, 100);
  server.send(200, "application/json", "{\"ok\":true}");
}

// brightness: /api/brightness?value=0..255
static void handleBrightness(){
  addCORS();
  if (server.hasArg("value")) {
    globalMaxBrightness = (uint8_t)constrain(argOr("value").toInt(), 0, 255);
    for (auto* s : S) s->setBrightness(globalMaxBrightness);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// set palette: /api/setColor?name=BLUE&hex=%23FFAABB&white=0..255
static void handleSetColor(){
  addCORS();
  String name = argOr("name");
  C4* p = palettePtrByName(name);
  if (!p){ server.send(400, "application/json", "{\"ok\":false,\"err\":\"name\"}"); return; }
  if (server.hasArg("hex")) {
    C4 c = hexToRgbw(argOr("hex").c_str());
    // ha külön fehér is jön, felülírjuk:
    if (server.hasArg("white")) c.w = (uint8_t)constrain(argOr("white").toInt(),0,255);
    *p = c;
  } else {
    if (server.hasArg("r")) p->r = (uint8_t)constrain(argOr("r").toInt(),0,255);
    if (server.hasArg("g")) p->g = (uint8_t)constrain(argOr("g").toInt(),0,255);
    if (server.hasArg("b")) p->b = (uint8_t)constrain(argOr("b").toInt(),0,255);
    if (server.hasArg("w")) p->w = (uint8_t)constrain(argOr("w").toInt(),0,255);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// OPTIONS preflight
static void handleOptions(){
  addCORS();
  server.send(204);
}

// ---- Minimal modern UI (HTML+JS, inline) ----
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Xmas Tree Controller</title>
<style>
:root{--bg:#0b1220;--fg:#e8f1ff;--mut:#97a6c1;--card:#121a2b;--acc:#50c8ff}
*{box-sizing:border-box}body{margin:0;font:16px/1.45 system-ui,Segoe UI,Roboto,Ubuntu;color:var(--fg);background:radial-gradient(600px 300px at 80% -10%,rgba(80,200,255,.10),transparent),radial-gradient(500px 300px at 10% 110%,rgba(80,255,200,.08),transparent),var(--bg)}
.wrap{max-width:980px;margin:24px auto;padding:16px}
h1{font-size:clamp(20px,3vw,28px);margin:0 0 8px} .mut{color:var(--mut)}
.card{background:var(--card);border:1px solid #1d2940;border-radius:16px;padding:16px;margin:16px 0;box-shadow:0 6px 24px rgba(0,0,0,.25)}
.row{display:grid;grid-template-columns:160px 1fr;gap:12px;align-items:center;margin:10px 0}
select, input[type=number], input[type=text]{width:100%;padding:10px 12px;border-radius:12px;border:1px solid #26324a;background:#0f1626;color:#e8f1ff;outline:none}
input[type=range]{width:100%}
.btn{display:inline-block;background:var(--acc);color:#032435;padding:10px 16px;border-radius:12px;border:none;font-weight:600;cursor:pointer}
.pgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px}
.pcell{display:flex;gap:8px;align-items:center;background:#0f1626;border:1px solid #26324a;padding:10px;border-radius:12px}
.pcell label{width:105px;font-size:13px;color:#b9c6e1}
.pcell input[type=color]{width:44px;height:32px;border:none;background:none}
.small{font-size:12px;color:#9fb0ce}
.footer{margin:16px 0;color:#7f8fb0;font-size:12px}
</style>
<div class="wrap">
  <h1>🎄 Xmas Tree Controller <span class="mut">(ESP8266 AP: <b>XmasTree</b>, pass: <b>1230@1230</b>)</span></h1>
  <div class="card">
    <div class="row"><div>Effect</div><div>
      <select id="effect">
        <option value="0">Static Warm</option>
        <option value="1">Static Cool</option>
        <option value="2">Static Mint</option>
        <option value="3">Stars Cool</option>
        <option value="4">Stars Pink</option>
        <option value="5">Stars Purple</option>
        <option value="6">Pulse Mallow</option>
        <option value="7">Run Silver</option>
        <option value="8">Blue↔Grey Smooth</option>
        <option value="9">Cyan↔Grey Smooth</option>
      </select>
    </div></div>
    <div class="row"><div>Speed</div><div><input id="speed" type="range" min="0" max="100"><div class="small"><span id="speedv"></span></div></div></div>
    <div class="row"><div>Density</div><div><input id="density" type="range" min="0" max="100"><div class="small"><span id="densityv"></span></div></div></div>
    <div class="row"><div>Intensity</div><div><input id="intensity" type="range" min="0" max="100"><div class="small"><span id="intensityv"></span></div></div></div>
    <div class="row"><div>Brightness</div><div><input id="brightness" type="range" min="0" max="255"><div class="small"><span id="brightv"></span></div></div></div>
    <div><button class="btn" id="apply">Apply</button></div>
  </div>

  <div class="card">
    <h3>Palette</h3>
    <div class="pgrid" id="pal"></div>
    <div class="small">White (W) komponens külön is állítható 0..255 tartományban.</div>
  </div>

  <div class="footer">Tip: tedd fel kedvenc effektjeidet, majd kapcsold le a Wi-Fi-t – a futás ettől független.</div>
</div>
<script>
const $ = (id)=>document.getElementById(id);
const get = (u)=>fetch(u).then(r=>r.json());
const call = (u)=>fetch(u).then(r=>r.json());

const state = async ()=>{
  const s = await get('/api/state');
  $('effect').value = s.effect;
  $('speed').value = s.speed; $('speedv').textContent = s.speed;
  $('density').value = s.density; $('densityv').textContent = s.density;
  $('intensity').value = s.intensity; $('intensityv').textContent = s.intensity;
  $('brightness').value = s.brightness; $('brightv').textContent = s.brightness;

  const pal = $('pal'); pal.innerHTML='';
  Object.entries(s.palette).forEach(([name, obj])=>{
    const wrap = document.createElement('div'); wrap.className='pcell';
    const lab = document.createElement('label'); lab.textContent = name; wrap.appendChild(lab);
    const col = document.createElement('input'); col.type='color'; col.value = obj.rgb; wrap.appendChild(col);
    const w = document.createElement('input'); w.type='number'; w.min=0; w.max=255; w.value=obj.w; w.style.width='64px'; wrap.appendChild(w);
    const b = document.createElement('button'); b.className='btn'; b.textContent='Save'; b.onclick=()=>{
      const hex=encodeURIComponent(col.value); const white=parseInt(w.value||'0',10);
      call(`/api/setColor?name=${name}&hex=${hex}&white=${white}`);
    }; wrap.appendChild(b);
    pal.appendChild(wrap);
  });
};

['speed','density','intensity','brightness'].forEach(id=>{
  $(id).addEventListener('input', e=>{
    const map = {speed:'speedv',density:'densityv',intensity:'intensityv',brightness:'brightv'};
    $(map[id]).textContent = e.target.value;
  });
});

$('apply').onclick = ()=>{
  call(`/api/setFX?effect=${$('effect').value}&speed=${$('speed').value}&density=${$('density').value}&intensity=${$('intensity').value}`);
  call(`/api/brightness?value=${$('brightness').value}`);
};

state();
</script>
)HTML";

static void handleRoot(){
  addCORS();
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// ---------- Setup / Loop ----------
void setup(){
  // LED init
  for (auto*s:S){ s->begin(); s->setBrightness(globalMaxBrightness); s->clear(); s->show(); delay(1); }
  pinMode(ENC_PIN_A,   INPUT_PULLUP);
  pinMode(ENC_PIN_B,   INPUT_PULLUP);
  pinMode(ENC_PIN_BTN, INPUT_PULLDOWN_16);

  encPrevAB = (digitalRead(ENC_PIN_A)?1:0) | (digitalRead(ENC_PIN_B)?2:0);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_A), isrEnc, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_B), isrEnc, CHANGE);

  P.effect = FX_STATIC_WARM;
  P.speed  = defaultSpeedFor(P.effect);
  fxStaticWarm();
  VB_show_with_phase();

  // ---- WiFi AP + DNS + Web ----
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS); // WPA2, min 8 char
  delay(100);
  dns.start(53, "*", WiFi.softAPIP()); // alap captive portal redirect
  server.on("/", handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/setFX", HTTP_GET, handleSetFX);
  server.on("/api/brightness", HTTP_GET, handleBrightness);
  server.on("/api/setColor", HTTP_GET, handleSetColor);
  server.on("/api/*", HTTP_OPTIONS, handleOptions); // CORS preflight
  server.begin();
}

void loop(){
  // Web kiszolgálás – nem blokkol, együtt fut az animációval
  dns.processNextRequest();
  server.handleClient();

  applyEncoderImmediate();
  buttonPoll();
  renderAll();
}
