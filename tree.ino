#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// ---------- Color helpers (ELŐRE HOZVA, hogy az automata prototípusok lássák) ----------
inline uint32_t ColorW(uint8_t r,uint8_t g,uint8_t b,uint8_t w=0); // fwd, lent definiáljuk

// Felső szintű színstruktúra (nem beágyazott), hogy az Arduino auto-prototípus biztosan lássa
struct C4 { uint8_t r,g,b,w; };

class Col {
public:
  static C4 lerp(const C4& a,const C4& b,uint8_t t){
    C4 o; 
    o.r=a.r+((int)b.r-a.r)*t/255; 
    o.g=a.g+((int)b.g-a.g)*t/255;
    o.b=a.b+((int)b.b-a.b)*t/255; 
    o.w=a.w+((int)b.w-a.w)*t/255; 
    return o;
  }
  static uint32_t toCol(const C4& c){ return ColorW(c.r,c.g,c.b,c.w); }
  static C4 scale(const C4& c, uint8_t k){
    C4 o;
    o.r = (uint16_t)c.r * k / 255;
    o.g = (uint16_t)c.g * k / 255;
    o.b = (uint16_t)c.b * k / 255;
    o.w = (uint16_t)c.w * k / 255;
    return o;
  }
};

const C4 GOLD1  ={255,180, 40, 40};
const C4 GOLD2  ={220,150, 30, 80};
const C4 SILVER ={ 40, 40, 60,190};
const C4 WHITEW ={  0,  0,  0,255};

// ---------- LED / HW ----------
#define LED_TYPE_RGBW
#define NEOCOLORTYPE NEO_GRBW

const uint8_t PIN_STRIP_A = 4;    // GPIO4  (D2)
const uint8_t PIN_STRIP_B = 12;   // GPIO12 (D6)
const uint8_t PIN_STRIP_C = 1;    // GPIO1  (TXD)  // DO NOT use Serial at runtime

const uint16_t NUM_LEDS = 300;    // 5m × 60/m
const uint16_t VNUM     = NUM_LEDS * 3; // virtuális hossz: 900
uint8_t globalMaxBrightness = 64; // ~25%

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

// ---------- Virtuális puffer (egyetlen hosszú szalag) ----------
static uint32_t vbuf[VNUM];

inline void vSet(uint16_t i, uint32_t c){ if (i<VNUM) vbuf[i]=c; }
inline uint32_t vGet(uint16_t i){ return (i<VNUM)? vbuf[i] : 0; }
inline void vFill(uint32_t c){ for (uint16_t i=0;i<VNUM;i++) vbuf[i]=c; }

inline void vDim(uint8_t k){
  for (uint16_t i=0;i<VNUM;i++){
    uint32_t c = vbuf[i];
#ifdef LED_TYPE_RGBW
    uint8_t r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF, w=(c>>24)&0xFF;
    r=(uint16_t)r*k/255; g=(uint16_t)g*k/255; b=(uint16_t)b*k/255; w=(uint16_t)w*k/255;
    vbuf[i]=ColorW(r,g,b,w);
#else
    uint8_t r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF;
    r=(uint16_t)r*k/255; g=(uint16_t)g*k/255; b=(uint16_t)b*k/255;
    vbuf[i]=ColorW(r,g,b);
#endif
  }
}

// vbuf -> valós szalagok (egyszerre show)
void vShow(){
  for (uint16_t i=0;i<VNUM;i++){
    uint8_t sidx = i / NUM_LEDS;   // 0..2
    uint16_t p   = i % NUM_LEDS;   // 0..299
    S[sidx]->setPixelColor(p, vbuf[i]);
  }
  for (auto* s : S){ s->setBrightness(globalMaxBrightness); s->show(); }
}

// ---------- Effekt lista (csak a kért 7 darab) ----------
enum Effect: uint8_t {
  // 1) Statikus meleg fehér – puha, elegáns melegfehér
  FX_STATIC_WARM = 0,
  // 2) Statikus hideg fehér – tiszta, modern „jégfehér”
  FX_STATIC_COOL,
  // 3) Lágy futófény – meleg fehér – széles, gaussian futófény
  FX_SOFT_RUNNER_WARM,
  // 4) Lágy futófény – hideg fehér
  FX_SOFT_RUNNER_COOL,
  // 5) Csillag effekt – hideg fehér – ritka selymes szikrák
  FX_STAR_COOL,
  // 6) Gyertya effekt – meleg fehér – több apró „láng” finom pislákolással
  FX_CANDLE_WARM,
  // 7) Elegáns lassú tűz – borostyán/meleg hullámzás, nagyon lassú mozgás
  FX_ELEGANT_FIRE,
  FX_COUNT
};

struct Params {
  uint8_t effect   = FX_STATIC_WARM; // DEFAULT
  uint8_t speed    = 20;   // 0..100 (NEM rotaryról áll most) // <<<
  uint8_t density  = 10;   // 0..100 (pl. csillag/gyertya mennyiség)
  uint8_t intensity= 40;   // 0..100 (fényerő a mintákon belül)
} P;

struct FxState { uint32_t i=0; } F;

// --------- Effektenként fix sebességek (0..100) – SZABADON ÁTÍRHATÓ --------- // <<<
uint8_t EFFECT_SPEED[FX_COUNT] = {
  10, // FX_STATIC_WARM
  10, // FX_STATIC_COOL
  40, // FX_SOFT_RUNNER_WARM
  40, // FX_SOFT_RUNNER_COOL
  30, // FX_STAR_COOL
  20, // FX_CANDLE_WARM
  15  // FX_ELEGANT_FIRE
};

// sebesség -> lépésköz (ms), min. 5% tempó
inline uint8_t  effectiveSpeed() {               // <<< most az effekt saját sebességét használjuk
  return max<uint8_t>(5, EFFECT_SPEED[P.effect]);
}
inline uint16_t stepIntervalMs(uint16_t slow=90, uint16_t fast=10) {
  return map(effectiveSpeed(), 5, 100, slow, fast);
}

// ---------- EFFEKT IMPLEMENTÁCIÓK ----------

// 1) Statikus meleg fehér
void fxStaticWarm(){
  C4 c = Col::lerp(WHITEW, GOLD2, 150);
  vFill(Col::toCol(c));
}

// 2) Statikus hideg fehér
void fxStaticCool(){
  C4 c = Col::lerp(SILVER, WHITEW, 220);
  vFill(Col::toCol(c));
}

// magfüggvény: lágy futófény (paraméterezhető paletta)
void fxSoftRunnerCore(const C4& base, uint8_t bgScale){
  static uint32_t t0=0; 
  uint32_t now=millis();
  if (now - t0 < stepIntervalMs(80, 8)) return; 
  t0=now; 
  F.i++;

  // puha háttér
  C4 bg = Col::scale(base, bgScale);
  vFill(Col::toCol(bg));

  // futó gaussian „púp”
  float pos   = fmodf(F.i * (0.6f + effectiveSpeed()/160.0f), (float)VNUM); // <<< speed innen jön
  float sigma = max<float>(6.0f, VNUM / 28.0f); // széles, lágy
  uint8_t peak = map(P.intensity, 0,100, 140, 255);

  for (int32_t dx = -(int32_t)(sigma*3); dx <= (int32_t)(sigma*3); ++dx){
    int32_t p = (int32_t)pos + dx;
    if (p < 0 || p >= (int32_t)VNUM) continue;

    float   gauss = expf(-(dx*dx)/(2.0f*sigma*sigma)); // 0..1
    uint8_t a     = (uint8_t)constrain((int)(gauss * peak), 0, 255);

    C4 add = Col::scale(base, a);
    uint32_t prev = vGet((uint16_t)p);
#ifdef LED_TYPE_RGBW
    uint8_t rr=(prev>>16)&0xFF, gg=(prev>>8)&0xFF, bb=prev&0xFF, ww=(prev>>24)&0xFF;
    rr = min<int>(255, rr+add.r);
    gg = min<int>(255, gg+add.g);
    bb = min<int>(255, bb+add.b);
    ww = min<int>(255, ww+add.w);
    vSet((uint16_t)p, ColorW(rr,gg,bb,ww));
#else
    uint8_t rr=(prev>>16)&0xFF, gg=(prev>>8)&0xFF, bb=prev&0xFF;
    rr = min<int>(255, rr+add.r);
    gg = min<int>(255, gg+add.g);
    bb = min<int>(255, bb+add.b);
    vSet((uint16_t)p, ColorW(rr,gg,bb));
#endif
  }
}

// 3) Lágy futófény – meleg fehér
void fxSoftRunnerWarm(){
  C4 warm = Col::lerp(WHITEW, GOLD2, 160);
  fxSoftRunnerCore(warm, 80);
}

// 4) Lágy futófény – hideg fehér
void fxSoftRunnerCool(){
  C4 cool = Col::lerp(SILVER, WHITEW, 200);
  fxSoftRunnerCore(cool, 70);
}

// 5) Csillag effekt – hideg fehér
void fxStarCool(){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(90, 12)) return; t0=now; F.i++;

  // sötétebb, hideg háttér + enyhe lecsengés
  vDim(230);
  C4 haze = Col::scale(Col::lerp(SILVER, WHITEW, 160), 30);
  for (uint16_t i=0;i<VNUM;i+=32){ vSet(i, Col::toCol(haze)); }

  // ritka, rövid fehér szikrák
  uint8_t n = map(P.density, 0,100, 0, 10);
  uint8_t amp = map(P.intensity,0,100, 160, 255);
  for (uint8_t k=0;k<n;k++){
    uint16_t p = random(VNUM);
    vSet(p, ColorW(0,0,0,amp)); // fehér szikra W csatornán
  }
}

// 6) Gyertya effekt – meleg fehér (több apró „láng”)
void fxCandleWarm(){
  static bool init=false;
  static const uint8_t CNUM = 10;      // gyertyák száma
  static uint16_t pos[CNUM];           // véletlen pozíciók
  if (!init){
    randomSeed(micros());
    for (uint8_t i=0;i<CNUM;i++) pos[i]=random(VNUM);
    init=true;
  }

  static uint32_t t0=0; 
  uint32_t now=millis();
  if (now - t0 < stepIntervalMs(120, 16)) return; 
  t0=now; 
  F.i++;

  // nagyon finom, sötétebb meleg háttér
  C4 base = Col::scale(Col::lerp(WHITEW, GOLD2, 150), 40);
  vFill(Col::toCol(base));

  // minden „gyertya” saját, kicsit eltérő légzése + apró jitter
  for (uint8_t i=0;i<CNUM;i++){
    float ph = fmodf((F.i + i*37) / 170.0f, 1.0f);
    float breathe = 0.6f + 0.4f * sinf(2*PI*ph);
    int jitter = random(-8, 10);

    uint8_t amp = map(P.intensity,0,100, 120, 230);
    C4 flame = Col::lerp(GOLD2, WHITEW, 60);
    flame.w = constrain((int)(flame.w * breathe) + jitter + amp/3, 0, 255);
    flame.r = constrain((int)(flame.r * (0.8f+0.2f*breathe)) + jitter/2, 0, 255);

    // kis, lágy aura (mini-gaussian körülötte)
    uint16_t center = pos[i];
    uint8_t radius = 6;
    for (int8_t dx=-radius; dx<=radius; dx++){
      int32_t p = (int32_t)center + dx;
      if (p<0 || p>= (int32_t)VNUM) continue;

      float gauss = expf(-(dx*dx)/(2.0f*radius*radius*0.6f));
      C4 add = Col::scale(flame, (uint8_t)constrain((int)(gauss*255),0,255));

      uint32_t prev = vGet((uint16_t)p);
#ifdef LED_TYPE_RGBW
      uint8_t rr=(prev>>16)&0xFF, gg=(prev>>8)&0xFF, bb=prev&0xFF, ww=(prev>>24)&0xFF;
      rr = min<int>(255, rr+add.r);
      gg = min<int>(255, gg+add.g);
      bb = min<int>(255, bb+add.b);
      ww = min<int>(255, ww+add.w);
      vSet((uint16_t)p, ColorW(rr,gg,bb,ww));
#else
      uint8_t rr=(prev>>16)&0xFF, gg=(prev>>8)&0xFF, bb=prev&0xFF;
      rr = min<int>(255, rr+add.r);
      gg = min<int>(255, gg+add.g);
      bb = min<int>(255, bb+add.b);
      vSet((uint16_t)p, ColorW(rr,gg,bb));
#endif
    }
  }
}

// 7) Elegáns lassú tűz – meleg borostyán hullámzás, selymes mozgás
void fxElegantFire(){
  static uint32_t t0=0; uint32_t now=millis();
  if (now - t0 < stepIntervalMs(95, 12)) return; t0=now; F.i++;

  // sötétebb meleg alap
  C4 base = Col::scale(Col::lerp(GOLD1, GOLD2, 180), 40);
  vFill(Col::toCol(base));

  // több, nagyon lassú szinusz réteg összegzése (lágy "tűznyelv")
  float t = F.i / (220.0f - effectiveSpeed()*1.2f); // <<< speed innen jön
  for (uint16_t i=0;i<VNUM;i++){
    float u = (float)i / VNUM;
    float s =
      0.55f + 0.25f * sinf(2*PI*(u*1.3f + t*0.9f)) +
      0.15f * sinf(2*PI*(u*3.7f - t*0.4f)) +
      0.10f * sinf(2*PI*(u*6.1f + t*0.2f));
    s = constrain(s, 0.0f, 1.0f);

    uint8_t a = (uint8_t) (map(P.intensity,0,100, 80, 220) * s);

    C4 ember = Col::lerp(GOLD1, WHITEW, 40);
    ember = Col::scale(ember, a);

    uint32_t prev = vGet(i);
#ifdef LED_TYPE_RGBW
    uint8_t rr=(prev>>16)&0xFF, gg=(prev>>8)&0xFF, bb=prev&0xFF, ww=(prev>>24)&0xFF;
    rr = min<int>(255, rr+ember.r);
    gg = min<int>(255, gg+ember.g);
    bb = min<int>(255, bb+ember.b);
    ww = min<int>(255, ww+ember.w);
    vSet(i, ColorW(rr,gg,bb,ww));
#else
    uint8_t rr=(prev>>16)&0xFF, gg=(prev>>8)&0xFF, bb=prev&0xFF;
    rr = min<int>(255, rr+ember.r);
    gg = min<int>(255, gg+ember.g);
    bb = min<int>(255, bb+ember.b);
    vSet(i, ColorW(rr,gg,bb));
#endif
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
    case FX_STATIC_WARM:         fxStaticWarm();        break;
    case FX_STATIC_COOL:         fxStaticCool();        break;
    case FX_SOFT_RUNNER_WARM:    fxSoftRunnerWarm();    break;
    case FX_SOFT_RUNNER_COOL:    fxSoftRunnerCool();    break;
    case FX_STAR_COOL:           fxStarCool();          break;
    case FX_CANDLE_WARM:         fxCandleWarm();        break;
    case FX_ELEGANT_FIRE:        fxElegantFire();       break;
  }
  vShow();
  yield(); delay(1);
}

// ---------- Rotary encoder pins ----------
const uint8_t ENC_PIN_A   = 2;   // GPIO2  (A csatorna) – INPUT_PULLUP
const uint8_t ENC_PIN_B   = 3;   // GPIO3/RXD (B csatorna) – INPUT_PULLUP
const uint8_t ENC_PIN_BTN = 16;  // GPIO16 (gomb) – INPUT_PULLDOWN_16, SW -> 3.3V

// Encoder state (robusztus kvadratúra-dekóder)
uint8_t encPrevAB = 0;
volatile int8_t encDelta = 0;         // +1 jobbra, -1 balra
uint32_t lastEncSampleUs = 0;
const uint16_t ENC_SAMPLE_US = 500;   // ~2 kHz mintavétel

// Gray-átmenet tábla: 4-bit (előzőAB<<2 | mostAB) -> delta
const int8_t QDEC_LUT[16] = {
  0, -1, +1,  0,
  +1, 0,  0, -1,
  -1, 0,  0, +1,
   0, +1, -1, 0
};

// háromszög-sebesség számláló – NEM használjuk többé rotaryhoz // <<<
// Meghagyom a helper-t, de nem hívjuk sehol.
int triangleStep(int current, int step, int minV=0, int maxV=100){
  static int dir = +1;
  int v = current + dir * step;
  if (v >= maxV){ v = maxV; dir = -1; }
  else if (v <= minV){ v = minV; dir = +1; }
  return v;
}

void encoderPoll(){
  uint32_t now = micros();
  if (now - lastEncSampleUs < ENC_SAMPLE_US) return;
  lastEncSampleUs = now;

  uint8_t a = digitalRead(ENC_PIN_A);
  uint8_t b = digitalRead(ENC_PIN_B);
  uint8_t ab = (a ? 1 : 0) | (b ? 2 : 0);           // A=bit0, B=bit1
  uint8_t idx = ((encPrevAB & 0x03) << 2) | (ab & 0x03);
  encPrevAB = ab;
  int8_t d = QDEC_LUT[idx];
  encDelta += d; // (ha kell irányt cserélni: d = -d;)
}

void applyEncoder(){
  int8_t d = encDelta;
  encDelta = 0;
  if (!d) return;

  // <<< ROTARY CSAK EFFEKT LÉPTETÉS >>>
  if (d < 0){
    for (int8_t k=d; k<0; ++k){
      int e = (int)P.effect - 1;
      if (e < 0) e = FX_COUNT - 1;
      P.effect = (uint8_t)e;
    }
  } else if (d > 0){
    for (int8_t k=0; k<d; ++k){
      int e = (int)P.effect + 1;
      if (e >= FX_COUNT) e = 0;
      P.effect = (uint8_t)e;
    }
  }

  // Effektváltás után vegye fel az adott effekt fix sebességét // <<<
  P.speed = EFFECT_SPEED[P.effect];
}

bool btnLast = false;               // PULLDOWN -> alap LOW
uint32_t btnLastMs = 0;
const uint16_t BTN_DEBOUNCE_MS = 30;

void buttonPoll(){
  bool s = digitalRead(ENC_PIN_BTN); // INPUT_PULLDOWN_16 -> nyomva: HIGH
  uint32_t now = millis();
  if (s != btnLast && (now - btnLastMs) > BTN_DEBOUNCE_MS){
    btnLast = s;
    btnLastMs = now;
    if (s == HIGH){
      // 15–100% közötti egyenletes lépcsők oda-vissza háromszögben
      static const uint8_t stepsPct[] = {
        38,   // ≈15%
        64,   // ≈25%
        102,  // ≈40%
        140,  // ≈55%
        178,  // ≈70%
        216,  // ≈85%
        255,  // 100%
        216,  // vissza 85%
        178,  // vissza 70%
        140,  // vissza 55%
        102,  // vissza 40%
        64,   // vissza 25%
        38    // vissza 15%
      };
      static uint8_t idx = 0;
      idx = (idx + 1) % (sizeof(stepsPct) / sizeof(stepsPct[0]));
      globalMaxBrightness = stepsPct[idx];
      for (auto* s : S) s->setBrightness(globalMaxBrightness);
    }
  }
}

void setup(){
  // LED szalagok init
  for (auto*s:S){ s->begin(); s->setBrightness(globalMaxBrightness); s->clear(); s->show(); delay(1); }
  // Encoder pinek
  pinMode(ENC_PIN_A,   INPUT_PULLUP);
  pinMode(ENC_PIN_B,   INPUT_PULLUP);
  pinMode(ENC_PIN_BTN, INPUT_PULLDOWN_16); // SW -> 3.3V
  // kezdő állapot beolvasása
  encPrevAB = (digitalRead(ENC_PIN_A)?1:0) | (digitalRead(ENC_PIN_B)?2:0);

  // alap: Statikus meleg fehér + effekt fix speed felvétele
  P.speed = EFFECT_SPEED[P.effect]; // <<< induló speed az effekté
  fxStaticWarm();
  vShow();
}

void loop(){
  encoderPoll();
  applyEncoder();
  buttonPoll();
  renderAll();
}
