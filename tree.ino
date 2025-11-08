// ==============================
// Xmas Tree RGBW (no dither)
// HBP (iamh2o) RGB->RGBW HEX konverter + hue-preserving dim
// ==============================

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <stdint.h>

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

// ---------- HBP-féle RGB->RGBW (HSI alap, hue-megőrző bontás) ----------
struct HBPConv {
  struct Cal {
    float gR=1.0f, gG=1.0f, gB=1.0f, gW=1.0f;   // lineáris gain
    float whiteScale=1.0f;                      // W skála (0.8..1.2 jellemző)
  };
  static Cal& calib(){ static Cal c; return c; }

  static inline float srgb_to_linear(float c01){
    return (c01 <= 0.04045f) ? (c01/12.92f) : powf((c01+0.055f)/1.055f, 2.4f);
  }
  static inline float linear_to_srgb(float c){
    return (c<=0.0031308f) ? (12.92f*c) : (1.055f*powf(c, 1.0f/2.4f)-0.055f);
  }
  static inline uint8_t to_u8(float c01){
    if (c01 <= 0.0f) return 0;
    if (c01 >= 1.0f) return 255;
    return (uint8_t)(c01*255.0f + 0.5f);
  }

  static RGBW8 rgb_to_rgbw(uint8_t R8, uint8_t G8, uint8_t B8){
    Cal &H = calib();
    // sRGB -> lineáris
    float R = srgb_to_linear(R8/255.0f);
    float G = srgb_to_linear(G8/255.0f);
    float B = srgb_to_linear(B8/255.0f);

    // lineáris gain
    R = fminf(1.f, R * H.gR);
    G = fminf(1.f, G * H.gG);
    B = fminf(1.f, B * H.gB);

    // HSI: I=(R+G+B)/3 ; S=1-min/I
    float I = (R + G + B) / 3.0f;
    float mn = fminf(R, fminf(G, B));
    float S = (I > 1e-6f) ? (1.0f - (mn / I)) : 0.0f;

    // W = (1-S)*I  (nem-szaturált rész W-be)
    float W = (1.0f - S) * I * H.whiteScale;
    W = fminf(fmaxf(W, 0.0f), 1.0f);

    // RGB-ből kivonjuk a W-t (lineáris tér)
    float Rl = fmaxf(0.0f, R - W);
    float Gl = fmaxf(0.0f, G - W);
    float Bl = fmaxf(0.0f, B - W);
    float Wl = W;

    // vissza sRGB + W gain
    uint8_t r = to_u8( linear_to_srgb(Rl) );
    uint8_t g = to_u8( linear_to_srgb(Gl) );
    uint8_t b = to_u8( linear_to_srgb(Bl) );
    uint8_t w = to_u8( linear_to_srgb( fminf(1.f, Wl * H.gW) ) );
    RGBW8 out{r,g,b,w};
    return out;
  }

  static RGBW8 hex_to_rgbw(const char* hex){
    auto hv = [](char c)->int{
      if(c>='0'&&c<='9') return c-'0';
      if(c>='a'&&c<='f') return 10+(c-'a');
      if(c>='A'&&c<='F') return 10+(c-'A');
      return -1;
    };
    if(!hex){ RGBW8 z{0,0,0,0}; return z; }
    if(*hex=='#') ++hex;
    else if(hex[0]=='0' && (hex[1]=='x'||hex[1]=='X')) hex+=2;

    size_t n=0; while(hex[n] && n<8) ++n;
    int r=0,g=0,b=0;
    if(n==6){
      int h1=hv(hex[0]),h2=hv(hex[1]),h3=hv(hex[2]),h4=hv(hex[3]),h5=hv(hex[4]),h6=hv(hex[5]);
      if(h1<0||h2<0||h3<0||h4<0||h5<0||h6<0){ RGBW8 z{0,0,0,0}; return z; }
      r=(h1<<4)|h2; g=(h3<<4)|h4; b=(h5<<4)|h6;
    }else if(n==3){
      int h1=hv(hex[0]),h2=hv(hex[1]),h3=hv(hex[2]);
      if(h1<0||h2<0||h3<0){ RGBW8 z{0,0,0,0}; return z; }
      r=(h1<<4)|h1; g=(h2<<4)|h2; b=(h3<<4)|h3;
    }else { RGBW8 z{0,0,0,0}; return z; }

    return rgb_to_rgbw((uint8_t)r,(uint8_t)g,(uint8_t)b);
  }

  static C4 hex_to_c4(const char* s){
    RGBW8 q = hex_to_rgbw(s);
    return C4{q.r,q.g,q.b,q.w};
  }
};

// ---------- LED / HW ----------
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

// ---------- Színek (HEX→HBP→C4) ----------
const C4 BLUE        = HBPConv::hex_to_c4("#00072D");
const C4 CYAN        = HBPConv::hex_to_c4("#ADD8E6");
const C4 MINT        = HBPConv::hex_to_c4("#2F4F4F");
const C4 MALLOW      = HBPConv::hex_to_c4("#B86B77");
const C4 ROSEGOLD    = HBPConv::hex_to_c4("#B76E79");
const C4 LIGHTPINK   = HBPConv::hex_to_c4("#F5428D");
const C4 GOLD        = HBPConv::hex_to_c4("#FFFAED");
const C4 SILVER      = HBPConv::hex_to_c4("#808080");
const C4 GREY        = HBPConv::hex_to_c4("#696969");
const C4 DARKPURPLE  = HBPConv::hex_to_c4("#3A0475");
const C4 LIGHTPURPLE = HBPConv::hex_to_c4("#AB65F8");
const C4 PINK        = HBPConv::hex_to_c4("#BC4DC1");

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
static void fxStaticWarm(){ VB::fill(GOLD); }
static void fxStaticCool(){ VB::fill(SILVER); }
static void fxStaticMint(){ VB::fill(MINT); }

static void fxStarCool(uint8_t speed){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;
  VB::dim(230);
  C4 haze = Col::scale(SILVER, 30);
  for (uint16_t i=0;i<VNUM;i+=32) VB::set(i, haze);
  uint8_t n   = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity,0,100, 160, 255);
  for (uint8_t k=0;k<n;k++){ VB::set(random(VNUM), C4{0,0,0,amp}); } // W-szikra
}

static void fxStarPink(uint8_t speed){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;
  VB::dim(230);
  C4 haze = Col::scale(LIGHTPINK, 30);
  for (uint16_t i=0;i<VNUM;i+=32) VB::set(i, haze);
  uint8_t n   = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity,0,100, 160, 255);
  for (uint8_t k=0;k<n;k++){ VB::set(random(VNUM), Col::scale(ROSEGOLD, amp)); }
}

static void fxStarPurple(uint8_t speed){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(speed, 90, 12)) return;
  t0=now;
  VB::dim(230);
  C4 haze = Col::scale(LIGHTPURPLE, 30);
  for (uint16_t i=0;i<VNUM;i+=32) VB::set(i, haze);
  uint8_t n   = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity, 0,100, 160, 255);
  const C4* PAL[3] = { &DARKPURPLE, &LIGHTPURPLE, &PINK };
  for (uint8_t k=0;k<n;k++){
    const C4& base = *PAL[random(3)];
    VB::set(random(VNUM), Col::scale(base, amp));
  }
}

// Selymes MALLOW pulzus
static void fxPulseMallow(uint8_t speed){
  uint8_t s = speed < 1 ? 1 : (speed > 100 ? 100 : speed);
  uint32_t periodMs = map(s, 1, 100, 180000UL, 8000UL);
  uint32_t now = millis();
  float phase = (float)(now % periodMs) / (float)periodMs;
  float wave  = 0.5f * (1.0f - cosf(6.2831853f * phase)); // 0..1
  const uint8_t kMin = 26, kMax = 89;
  float targetK = (float)kMin + (float)(kMax - kMin) * wave;
  static float kf = kMin;
  float alpha = 0.06f; // kis simítás
  kf += alpha * (targetK - kf);
  uint8_t k = (uint8_t)(kf + 0.5f);
  VB::fill(Col::scale(MALLOW, k));
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
  // régi: if (now - t0 < stepIntervalMs(speed, 70, 8)) return;
  if (now - t0 < stepIntervalMs(speed, 20, 4)) return;  // 2× gyorsabb
  t0 = now;

  if (!primed) { VB::fill(C4{0,0,0,0}); pos=-1; dir=+1; primed=true; }

  const uint8_t amp = map(P.intensity, 0, 100, 80, 255);
  const C4 on  = Col::scale(SILVER, amp);
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
    for (uint16_t k=0;k<VNUM;k++) target[idx[k]] = (k < blueCount) ? BLUE : GREY;
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
    for (uint16_t k=0;k<VNUM;k++) target[idx[k]] = (k < half) ? CYAN : GREY;
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

  VB::show();
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

  // azonnali blackout váltáskor
  VB::fill(C4{0,0,0,0});
  VB::show();
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
      static const uint8_t stepsPct[] = { 13, 64, 102, 140, 178, 216, 255, 216, 178, 140, 102, 64, 13 };
      static uint8_t idx = 0;
      idx = (idx + 1) % (sizeof(stepsPct)/sizeof(stepsPct[0]));
      globalMaxBrightness = stepsPct[idx];
      for (auto* s : S) s->setBrightness(globalMaxBrightness);
    }
  }
}

// ---------- Setup / Loop ----------
void setup(){
  for (auto*s:S){ s->begin(); s->setBrightness(globalMaxBrightness); s->clear(); s->show(); delay(1); }
  pinMode(ENC_PIN_A,   INPUT_PULLUP);
  pinMode(ENC_PIN_B,   INPUT_PULLUP);
  pinMode(ENC_PIN_BTN, INPUT_PULLDOWN_16); // ha AVR: INPUT_PULLUP + invert logika

  encPrevAB = (digitalRead(ENC_PIN_A)?1:0) | (digitalRead(ENC_PIN_B)?2:0);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_A), isrEnc, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_PIN_B), isrEnc, CHANGE);

  P.effect = FX_STATIC_WARM;
  P.speed  = defaultSpeedFor(P.effect);
  fxStaticWarm();
  VB::show();
}

void loop(){
  applyEncoderImmediate();
  buttonPoll();
  renderAll();
}
