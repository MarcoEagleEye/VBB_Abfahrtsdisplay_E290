#include <Arduino.h>
#include "../config.h"
#include "buttons.h"

static volatile bool bootLatched=false, qrLatched=false;
static volatile unsigned long bootAt=0, qrAt=0, bootEdge=0, qrEdge=0;
static unsigned long bootLast=0, qrLast=0;
static bool bootAccepted=false, qrAccepted=false;
static bool bootHeld=false, qrHeld=false, bootLong=false, qrLong=false;
static unsigned long bootStart=0, qrStart=0;

static void IRAM_ATTR onBootEdge(){unsigned long n=millis();if(digitalRead(BOOT_BUTTON)==LOW&&n-bootEdge>=BUTTON_EDGE_STABLE_MS){bootLatched=true;bootAt=n;}bootEdge=n;}
static void IRAM_ATTR onQrEdge(){unsigned long n=millis();if(digitalRead(QR_BUTTON)==LOW&&n-qrEdge>=BUTTON_EDGE_STABLE_MS){qrLatched=true;qrAt=n;}qrEdge=n;}
static bool take(volatile bool& l, volatile unsigned long& at,unsigned long& last,bool& once,unsigned long& out){noInterrupts();bool h=l;unsigned long a=at;l=false;interrupts();if(!h)return false;if(millis()-a>BUTTON_LATCH_MAX_AGE_MS)return false;if(once&&a-last<BUTTON_DEBOUNCE_MS)return false;last=a;once=true;out=a;return true;}
void buttonsInit(){pinMode(BOOT_BUTTON,INPUT_PULLUP);pinMode(QR_BUTTON,INPUT_PULLUP);attachInterrupt(digitalPinToInterrupt(BOOT_BUTTON),onBootEdge,CHANGE);attachInterrupt(digitalPinToInterrupt(QR_BUTTON),onQrEdge,CHANGE);}

static void handleButton(volatile bool& latched, volatile unsigned long& at, volatile unsigned long& edge,
                         unsigned long& last, bool& accepted, bool& held, unsigned long& start, bool& longDone,
                         int pin, unsigned long longMs, void(*shortCb)(), void(*longCb)()) {
  unsigned long pressAt;
  if(!held && take(latched,at,last,accepted,pressAt)){held=true;start=pressAt;longDone=false;}
  if(!held)return;
  bool pressed=(digitalRead(pin)==LOW);
  if(pressed && !longDone && millis()-start>=longMs){longDone=true;if(longCb)longCb();}
  noInterrupts();unsigned long e=edge;interrupts();
  if(!pressed && millis()-e>=BUTTON_EDGE_STABLE_MS){
    // Falls der komplette Tastendruck waehrend eines blockierenden E-Ink-Updates
    // stattfand, sehen wir die Taste erst nach dem Loslassen wieder. Die im ISR
    // gemerkte letzte Flanke erlaubt trotzdem die echte Haltedauer zu erkennen.
    if(!longDone && e-start>=longMs){longDone=true;if(longCb)longCb();}
    held=false;if(!longDone&&shortCb)shortCb();longDone=false;
  }
}
void handleBootButton(void(*s)(),void(*l)()){handleButton(bootLatched,bootAt,bootEdge,bootLast,bootAccepted,bootHeld,bootStart,bootLong,BOOT_BUTTON,BOOT_LONG_PRESS_MS,s,l);}
void handleQrButton(void(*s)(),void(*l)()){handleButton(qrLatched,qrAt,qrEdge,qrLast,qrAccepted,qrHeld,qrStart,qrLong,QR_BUTTON,LONG_PRESS_MS,s,l);}
