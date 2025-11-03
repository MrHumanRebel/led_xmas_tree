# 🎄 Karácsonyfa Világító Effekt Kontroller – Arduino + LED szalag

Ez a projekt egy **három csatornás LED szalag vezérlő**, kifejezetten **karácsonyfa világításhoz**, ahol különféle fény-effektek jeleníthetők meg, és az egész egyszerűen vezérelhető egy rotary encoderrel és gombbal.

---

## ✨ Funkciók

- 7 beépített fény-effekt, ideális karácsonyi hangulathoz:
  1. Statikus meleg fehér – finom, elegáns arany-tónus  
  2. Statikus hideg fehér – modern, „havas” hatás  
  3. Lágy futófény – meleg fehér – mint fénycsík fut körbe  
  4. Lágy futófény – hideg fehér – elegáns hideg tónusban  
  5. Csillag effekt – hideg fehér – mint apró szikrák a fa ágai között  
  6. Gyertya effekt – meleg fehér – mint apró mécsesek a fa körül  
  7. Elegáns lassú tűz – borostyán/meleg hullámzás – lassú, nyugodt fényjáték  

- **Rotary encoder** vezérlés:
  - Forgatás balra → **előző effekt**
  - Forgatás jobbra → **következő effekt**
  - Effektváltás után az adott effekt **fix, előre beállított sebességgel** fut – így stabil, kényelmes karácsonyi fényhatás.

- **Encoder gomb**:
  - Rövid gombnyomás → lépteti a **globális fényerőt** a fa világításához.  
  - A fényerő **15 %–100 %** között változik egyenletes lépésekben, így nem lehet teljesen leoltani, mindig “fényben” marad a fa.

---

## ⚙️ Hardver konfiguráció

| Funkció        | Arduino pin | Megjegyzés                      |
|----------------|-------------|--------------------------------|
| LED szalag A   | D2 / GPIO4  | Üzenetblokként – első szalag    |
| LED szalag B   | D6 / GPIO12 | Második szalag                  |
| LED szalag C   | TXD / GPIO1 | Harmadik szalag – Serial-t ne használd runtime-ban |
| Encoder A      | GPIO2       | INPUT_PULLUP                    |
| Encoder B      | GPIO3       | INPUT_PULLUP                    |
| Encoder gomb   | GPIO16      | INPUT_PULLDOWN_16, SW → 3.3 V   |
| Táp            | 5 V / GND   | LED szalag teljesítményétől függ |

> **Megjegyzés:** Ha az AliExpress-ről vett szalag **RGBW** típusú, akkor hagyd bekapcsolva a `#define LED_TYPE_RGBW` sort. Ha csak RGB-s, akkor kommenteld ki.

---

## 🧩 Kód felépítés

- Virtuális LED puffer (`vbuf`) → egy logikus hossz, három fizikai szalagra osztva.  
- Effekt motor (`renderAll()`) választja ki az aktuális effektet és hívja meg.  
- Rotary dekóder: `encoderPoll()`, `applyEncoder()` – kizárólag effektváltásra használjuk.  
- Gombkezelés: `buttonPoll()` – globális fényerő váltás 15–100 % között.  
- Effekt-sebességek: minden effekt saját fix sebességgel fut, amit az `EFFECT_SPEED[]` tömbben állíthatsz.

---

## 🛠️ Testreszabás karácsonyfa-kontextusban

- **Színek módosítása**: Ha pl. piros-zöld-arany tónust szeretnél, érdemes a `WHITEW`, `GOLD1`, `GOLD2` színeket átszerkeszteni.  
- **Sebességek beállítása**: Ha túl gyors vagy túl lassú effektet tapasztalsz, az `EFFECT_SPEED[]` tömbben finomhangolhatod (0-100 skálán).  
- **Fényerőlépcsők finomítása**: A 15% kezdet illeszkedik a fa világításához, de ha pl. 20%–100% jobban megfelel, akkor módosíthatod.

---

## 📷 Eurokarácsonyi használat

Ez a vezérlő ideális arra a célra, hogy a karácsonyfa világítás **hangulatos**, **vezérelhető**, és ne “boldog-karácsonyt” automatikusan, hanem **kívánság szerint** változtatható legyen (pl. vendégek érkezésekor effektváltás).  
A három szalaggal (három ág, mennyezet körül, fa körül) látványos eredményt kapsz.

---

## 🧑‍💻 Szerző & Licenc

- **Licenc:** MIT  
- **Készült:** 2025  

---

🎅 Kellemes készülődést és hangulatos karácsonyi fényeket kívánok! 🎄  
