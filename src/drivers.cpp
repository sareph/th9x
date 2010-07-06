/*
 * Author	Thomas Husterer <thus1@t-online.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 This file contains any code to immediatley access the hardware except 
 initialisation and interrupt handling (this is in th9x.cpp).

 */


#include "th9x.h"

#ifndef SIM
#include "avr/interrupt.h"

///opt/cross/avr/include/avr/eeprom.h
static inline void __attribute__ ((always_inline))
eeprom_write_byte_cmp (uint8_t dat, uint16_t pointer_eeprom)
{
  //see /home/thus/work/avr/avrsdk4/avr-libc-1.4.4/libc/misc/eeprom.S:98 143
  while(EECR & (1<<EEWE)) /* make sure EEPROM is ready */
    ;
  EEAR  = pointer_eeprom;

  EECR |= 1<<EERE;
  if(dat == EEDR) return;

  EEDR  = dat;
  uint8_t flags=SREG;
  cli();
  EECR |= 1<<EEMWE;
  EECR |= 1<<EEWE;
  SREG = flags;
}

void eeWriteBlockCmp(const void *i_pointer_ram, void *i_pointer_eeprom, size_t size)
{
  const char* pointer_ram = (const char*)i_pointer_ram;
  uint16_t    pointer_eeprom = (uint16_t)i_pointer_eeprom;
  while(size){
    eeprom_write_byte_cmp(*pointer_ram++,pointer_eeprom++);
    size--;
  }
}

//inline uint16_t anaIn(uint8_t chan)
//{
//  //                     ana-in:   3 1 2 0 4 5 6 7          
//  static prog_char APM crossAna[]={4,2,3,1,5,6,7,0}; // wenn schon Tabelle, dann muss sich auch lohnen
//  return s_ana[pgm_read_byte(crossAna+chan)] / 4;
//}
#endif




static uint8_t s_evt;
void putEvent(uint8_t evt)
{
  //#ifdef SIM
  //  printf("putEvent %d %x\n",evt,evt);
  //#endif
  s_evt = evt;
}
uint8_t getEvent()
{
  uint8_t evt = s_evt;
  s_evt=0;
  return evt;
}

class Key
{
#define FFVAL        0x03
#define KSTATE_OFF      0
#define KSTATE_SHORT   96
#define KSTATE_START   97
#define KSTATE_PAUSE   98
#define KSTATE_KILLED  99
  uint8_t m_vals:2;
  uint8_t m_dblcnt:2;
  uint8_t m_cnt;
  uint8_t m_state;
public:
  void input(bool val, EnumKeys enuk);
  bool state()       { return m_vals==FFVAL;                }
  void pauseEvents() { m_state = KSTATE_PAUSE;  m_cnt   = 0;}
  void killEvents()  { m_state = KSTATE_KILLED;             }
};


Key keys[NUM_KEYS];
void Key::input(bool val, EnumKeys enuk)
{       
  //  uint8_t old=m_vals;
  m_vals <<= 1;  if(val) m_vals |= 1; //portbit einschieben
  m_cnt++;

  if(m_state && m_vals==0){  //gerade eben sprung auf 0
    if(m_state!=KSTATE_KILLED) {
      if(m_state == KSTATE_SHORT){
        m_dblcnt++;
  #ifdef SIM
        printf("Key m_dblcnt=%d, m_cnt=%d\n",m_dblcnt,m_cnt);
  #endif
        if(m_dblcnt==2){
          putEvent(EVT_KEY_DBL(enuk));
          m_dblcnt=0;
        }
      }else{
        putEvent(EVT_KEY_GEN_BREAK(enuk));
        m_dblcnt=0;
      }
    }
    m_cnt   = 0;
    m_state = KSTATE_OFF;
  }
  switch(m_state){
    case KSTATE_OFF: 
      if(m_vals==FFVAL){ //gerade eben sprung auf ff
        m_state = KSTATE_SHORT;
        if(m_cnt>20) m_dblcnt=0; //pause zu lang fuer double
        m_cnt   = 0;
      }
      break;
    case KSTATE_SHORT: 
      if(m_cnt<8)  break;
      m_state = KSTATE_START;
      //fallthrough
    case KSTATE_START: 
      putEvent(EVT_KEY_FIRST(enuk));
      m_state   = 16;
      m_cnt     = 0;
      break;
    case 16: 
      if(m_cnt == 30)        putEvent(EVT_KEY_LONG(enuk));
      //fallthrough
    case 8: 
    case 4: 
    case 2: 
      if(m_cnt >= 48)  { //3 6 12 24 48 pulses in every 480ms
        m_state >>= 1;
        m_cnt     = 0;
      }
      //fallthrough
    case 1: 
      if( (m_cnt & (m_state-1)) == 0)  putEvent(EVT_KEY_REPT(enuk));
      break;

    case KSTATE_PAUSE: //pause 
      if(m_cnt >= 64)      {
        m_state = 8;
        m_cnt   = 0;
      }
      break;

    case KSTATE_KILLED: //killed
      break;
  }
}

bool keyState(EnumKeys enuk)
{
  if(enuk < (int)DIM(keys))  return keys[enuk].state() ? 1 : 0;
  switch(enuk){
    case SW_ElevDR : return PINE & (1<<INP_E_ElevDR);
    case SW_AileDR : return PINE & (1<<INP_E_AileDR);
    case SW_RuddDR : return PING & (1<<INP_G_RuddDR);
      //     INP_G_ID1 INP_E_ID2
      // id0    0        1
      // id1    1        1
      // id2    1        0
    case SW_ID0    : return !(PING & (1<<INP_G_ID1));
    case SW_ID1    : return (PING & (1<<INP_G_ID1))&& (PINE & (1<<INP_E_ID2));
    case SW_ID2    : return !(PINE & (1<<INP_E_ID2));
    case SW_Gear   : return PINE & (1<<INP_E_Gear);
    case SW_ThrCt  : return PINE & (1<<INP_E_ThrCt);
    case SW_Trainer: return PINE & (1<<INP_E_Trainer);
    default:;
  }
  return 0;
}

void pauseEvents(uint8_t event)
{
  event=event & EVT_KEY_MASK;
  if(event < (int)DIM(keys))  keys[event].pauseEvents();
}
void killEvents(uint8_t event)
{
  event=event & EVT_KEY_MASK;
  if(event < (int)DIM(keys))  keys[event].killEvents();
}


//uint16_t g_anaIns[8];
uint8_t  g_vbat100mV;
volatile uint16_t g_tmr10ms;
volatile uint8_t  g_blinkTmr10ms;

void per10ms()
{
  g_tmr10ms++;
  g_blinkTmr10ms++;
  uint8_t enuk = KEY_MENU;
  uint8_t    in = ~PINB;
  for(int i=1; i<7; i++)
  {
    //INP_B_KEY_MEN 1  .. INP_B_KEY_LFT 6
    keys[enuk].input(in & (1<<i),(EnumKeys)enuk);
    ++enuk;
  }
  static  prog_uchar  APM crossTrim[]={
    1<<INP_D_TRM_LH_DWN,
    1<<INP_D_TRM_LH_UP,
    1<<INP_D_TRM_LV_DWN,
    1<<INP_D_TRM_LV_UP,
    1<<INP_D_TRM_RV_DWN,
    1<<INP_D_TRM_RV_UP,
    1<<INP_D_TRM_RH_DWN,
    1<<INP_D_TRM_RH_UP
  };
  in = ~PIND;
  for(int i=0; i<8; i++)
  {
    // INP_D_TRM_RH_UP   0 .. INP_D_TRM_LH_UP   7
    keys[enuk].input(in & pgm_read_byte(crossTrim+i),(EnumKeys)enuk);
    ++enuk;
  }
  //  for(int i=0; i<8; i++)
  //  {
  //    g_anaIns[i] = anaIn(i);
  //  }
  //14.2246465682983   -> 10.7 V  ((2.65+5.07)/2.65*5/1024)*1000  mV
  //0.142246465682983   -> 10.7 V  ((2.65+5.07)/2.65*5/1024)*10    1/10 V
  //0.137176291331963    k=((2.65+5.07)/2.65*5/1024)*10*9.74/10.1
  // g_vbat100mV=g_anaIns[7]*35/256; //34/239;
  // g_vbat100mV += g_vbat100mV*g_eeGeneral.vBatCalib/256;
  //g_vbat100mV = (g_anaIns[7]*35+g_anaIns[7]/4*g_eeGeneral.vBatCalib) / 256; 
  uint16_t ab = anaIn(7);
  g_vbat100mV = (ab*35 + ab / 4 * g_eeGeneral.vBatCalib) / 256; 

  static uint8_t s_batCheck;
  s_batCheck++;
  if(s_batCheck==0 && g_vbat100mV < g_eeGeneral.vBatWarn){
    beepWarn1();
  }
#ifndef SIM
  //  STARTADCONV;            // Analogkanäle lesen
#endif
}
