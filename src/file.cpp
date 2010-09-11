/*
 * Author Thomas Husterer <thus1@t-online.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 */


#include "th9x.h"
#include "stdio.h"
#include "inttypes.h"
#include "string.h"


#ifdef TEST
#include "assert.h"
#  define EESIZE   150
#  define BS       4
#  define RESV     64  //reserv for eeprom header with directory (eeFs)
#else
//
// bs=16  128 blocks    verlust link:128  16files:16*8  128     sum 256
// bs=32   64 blocks    verlust link: 64  16files:16*16 256     sum 320
//
#  define EESIZE   2048
#  define BS       16
#  define RESV     64  //reserv for eeprom header with directory (eeFs)
#endif
#define FIRSTBLK (RESV/BS)
#define BLOCKS   (EESIZE/BS)

#define EEFS_VERS 4
struct DirEnt{
  uint8_t  startBlk;
  uint16_t size:12;
  uint16_t typ:4;
}__attribute__((packed));
#define MAXFILES (1+MAX_MODELS+3)
struct EeFs{
  uint8_t  version;
  uint8_t  mySize;
  uint8_t  freeList;
  uint8_t  bs;
  DirEnt   files[MAXFILES];
}__attribute__((packed)) eeFs;


static uint8_t EeFsRead(uint8_t blk,uint8_t ofs){
  uint8_t ret;
  eeprom_read_block(&ret,(const void*)(blk*BS+ofs),1);
  return ret;
}
static void EeFsWrite(uint8_t blk,uint8_t ofs,uint8_t val){
  eeWriteBlockCmp(&val, (void*)(blk*BS+ofs), 1);
}

static uint8_t EeFsGetLink(uint8_t blk){
  return EeFsRead( blk,0);
}
static void EeFsSetLink(uint8_t blk,uint8_t val){
  EeFsWrite( blk,0,val);
}
static uint8_t EeFsGetDat(uint8_t blk,uint8_t ofs){
  return EeFsRead( blk,ofs+1);
}
static void EeFsSetDat(uint8_t blk,uint8_t ofs,uint8_t*buf,uint8_t len){
  //EeFsWrite( blk,ofs+1,val);
  eeWriteBlockCmp(buf, (void*)(blk*BS+ofs+1), len);
}
static void EeFsFlushFreelist()
{
  eeWriteBlockCmp(&eeFs.freeList,&((EeFs*)0)->freeList ,sizeof(eeFs.freeList));
}
static void EeFsFlush()
{
  eeWriteBlockCmp(&eeFs, 0,sizeof(eeFs));
}
  
uint16_t EeFsGetFree()
{
  uint16_t  ret = 0;
  uint8_t i = eeFs.freeList;
  while( i ){
    ret += BS-1;
    i = EeFsGetLink(i);
  }
  return ret;
}
static void EeFsFree(uint8_t blk){///free one or more blocks
  uint8_t i = blk;
  while( EeFsGetLink(i)) i = EeFsGetLink(i);
  EeFsSetLink(i,eeFs.freeList);
  eeFs.freeList = blk; //chain in front
  EeFsFlushFreelist();
}
static uint8_t EeFsAlloc(){ ///alloc one block from freelist
  uint8_t ret=eeFs.freeList;
  if(ret){
    eeFs.freeList = EeFsGetLink(ret);
    EeFsFlushFreelist();
    EeFsSetLink(ret,0);
  }
  return ret;
}

int8_t EeFsck()
{
  uint8_t *bufp;
  static uint8_t buffer[BLOCKS];
  bufp = buffer;
  memset(bufp,0,BLOCKS);
  uint8_t blk ;
  int8_t ret=0;
  for(uint8_t i = 0; i <= MAXFILES; i++){
    uint8_t *startP = i==MAXFILES ? &eeFs.freeList : &eeFs.files[i].startBlk;
    uint8_t lastBlk = 0;
    blk = *startP;
      //if(i == MAXFILES) blk = eeFs.freeList;
      //    else              blk = eeFs.files[i].startBlk;
    while(blk){
      //      if(blk <  FIRSTBLK ) goto err_1; //bad blk index
      //      if(blk >= BLOCKS   ) goto err_2; //bad blk index
      //      if(bufp[blk])        goto err_3; //blk double usage
      if( (   blk <  FIRSTBLK ) //goto err_1; //bad blk index
          || (blk >= BLOCKS   ) //goto err_2; //bad blk index
          || (bufp[blk]       ))//goto err_3; //blk double usage
      {
        if(lastBlk){
          EeFsSetLink(lastBlk,0);
        }else{
          *startP = 0; //interrupt chain at startpos
          EeFsFlush();
        }
        blk=0; //abort
      }else{
        bufp[blk] = i+1;
        lastBlk   = blk;
        blk       = EeFsGetLink(blk);
      }
    }
  }
  for(blk = FIRSTBLK; blk < BLOCKS; blk++){
    if(bufp[blk]==0)       //goto err_4; //unused block
    {
#ifdef SIM
      printf("ERROR fsck -4 blk=%d readding..\n",blk);
#endif
      EeFsSetLink(blk,eeFs.freeList);
      eeFs.freeList = blk; //chain in front
      EeFsFlushFreelist();
    }
  }
  //  if(0){
    //err_4: ret--;
    //err_3: ret--;
    //    err_2: ret--;
    //    err_1: ret--;
    //#ifdef SIM
    //    printf("ERROR fsck %d blk=%d\n",ret,blk);
    //#endif
  //  }
  return ret;
}
void EeFsFormat()
{
  if(sizeof(eeFs) != RESV){
    extern void eeprom_RESV_mismatch();
    eeprom_RESV_mismatch();
  }
  memset(&eeFs,0, sizeof(eeFs));
  eeFs.version  = EEFS_VERS;
  eeFs.mySize   = sizeof(eeFs);
  eeFs.freeList = 0;
  eeFs.bs       = BS;
  for(uint8_t i = FIRSTBLK; i < BLOCKS; i++) EeFsSetLink(i,i+1);
  EeFsSetLink(BLOCKS-1, 0);
  eeFs.freeList = FIRSTBLK;
  EeFsFlush();
}
bool EeFsOpen()
{
  eeprom_read_block(&eeFs,0,sizeof(eeFs));
#ifdef SIM
  if(eeFs.version != EEFS_VERS)    printf("bad eeFs.version\n");
  if(eeFs.mySize  != sizeof(eeFs)) printf("bad eeFs.mySize\n");
#endif  
  return eeFs.version == EEFS_VERS && eeFs.mySize  == sizeof(eeFs);
}

bool EFile::exists(uint8_t i_fileId)
{
  return eeFs.files[i_fileId].startBlk;
}

void EFile::swap(uint8_t i_fileId1,uint8_t i_fileId2)
{
  DirEnt            tmp = eeFs.files[i_fileId1];
  eeFs.files[i_fileId1] = eeFs.files[i_fileId2];
  eeFs.files[i_fileId2] = tmp;;
  EeFsFlush();
}

void EFile::rm(uint8_t i_fileId){
  uint8_t i = eeFs.files[i_fileId].startBlk;
  memset(&eeFs.files[i_fileId], 0, sizeof(eeFs.files[i_fileId]));
  EeFsFlush(); //chained out

  if(i) EeFsFree( i ); //chain in
}

uint16_t EFile::size(){
  return eeFs.files[m_fileId].size;
}
uint8_t EFile::openRd(uint8_t i_fileId){
  m_fileId = i_fileId;
  m_pos      = 0;
  m_currBlk  = eeFs.files[m_fileId].startBlk; 
  m_ofs      = 0;
  m_bRlc     = 0;
  m_err      = ERR_NONE;       //error reasons
  return  eeFs.files[m_fileId].typ; 
}
uint8_t EFile::read(uint8_t*buf,uint8_t i_len){
  uint8_t len = eeFs.files[m_fileId].size - m_pos; 
  if(len < i_len) i_len = len;
  len = i_len;
  while(len)
  {
    if(!m_currBlk) break;
    *buf++ = EeFsGetDat(m_currBlk, m_ofs++);
    if(m_ofs>=(BS-1)){
      m_ofs=0;
      m_currBlk=EeFsGetLink(m_currBlk);
    }
    len--;
  }
  m_pos += i_len - len;
  return i_len - len;
}
uint16_t EFile::readRlc(uint8_t*buf,uint16_t i_len){
  uint16_t i;
  for( i=0; i<i_len; ){
    if((m_bRlc&0x7f) == 0) {
      if(read(&m_bRlc,1)!=1) break;
    }
    assert(m_bRlc & 0x7f);
    uint8_t l=m_bRlc&0x7f;
    if((uint16_t)l>(i_len-i)) l = (uint8_t)(i_len-i);
    if(m_bRlc&0x80){
      memset(&buf[i],0,l);
    }else{
      uint8_t lr = read(&buf[i],l);
      if(lr!=l) return i+lr;
    }
    i    += l;
    m_bRlc -= l;
  }
  return i;
}
uint8_t EFile::write(uint8_t*buf,uint8_t i_len){
  uint8_t len=i_len;
  if(!m_currBlk && m_pos==0)
  {
    eeFs.files[m_fileId].startBlk = m_currBlk = EeFsAlloc();
  }
  while(len)
  {
    if( (int16_t)(m_stopTime10ms - g_tmr10ms) < 0)
    {
      m_err = ERR_TMO;
      break;
    }
    if(!m_currBlk) {
      m_err = ERR_FULL;
      break;
    }
    if(m_ofs>=(BS-1)){
      m_ofs=0;
      if( ! EeFsGetLink(m_currBlk) ){
        EeFsSetLink(m_currBlk, EeFsAlloc());
      }
      m_currBlk = EeFsGetLink(m_currBlk);
    }
    if(!m_currBlk) {
      m_err = ERR_FULL;
      break;
    }
    uint8_t l = BS-1-m_ofs; if(l>len) l=len;
    EeFsSetDat(m_currBlk, m_ofs, buf, l);
    buf   +=l;
    m_ofs +=l;
    len   -=l;
  }
  m_pos += i_len - len;
  return   i_len - len;
}
void EFile::create(uint8_t i_fileId, uint8_t typ, uint8_t maxTme10ms){
  openRd(i_fileId); //internal use
  eeFs.files[i_fileId].typ      = typ; 
  eeFs.files[i_fileId].size     = 0; 
  m_stopTime10ms = g_tmr10ms + maxTme10ms;
}
void EFile::closeTrunc()
{
  uint8_t fri=0;
  eeFs.files[m_fileId].size     = m_pos; 
  if(m_currBlk && ( fri = EeFsGetLink(m_currBlk)))    EeFsSetLink(m_currBlk, 0);
  EeFsFlush(); //chained out

  if(fri) EeFsFree( fri );  //chain in
}

uint16_t EFile::writeRlc(uint8_t i_fileId, uint8_t typ,uint8_t*buf,uint16_t i_len, uint8_t maxTme10ms){
  create(i_fileId,typ,maxTme10ms);
  bool    state0 = true;
  uint8_t cnt    = 0;
  uint16_t i;
  for( i=0; i<=i_len; i++)
  {
    bool nst0 = buf[i] == 0;
    if( nst0 && !state0 && buf[i+1]!=0) nst0 = false ;
    if(nst0 != state0 || cnt>=0x7f || i==i_len){
      if(state0){
        if(cnt>0){
          cnt|=0x80;
          if( write(&cnt,1)!=1)           goto error;
          cnt=0;
        }
      }else{
        if( write(&cnt,1) !=1)            goto error;
        uint8_t ret=write(&buf[i-cnt],cnt);
        if( ret !=cnt) { cnt-=ret;        goto error;}
        cnt=0;
      }
      state0 = nst0;
    }
    cnt++;
  }
  if(0){
    error:
    i_len = i - (cnt & 0x7f);
#ifdef SIM
    switch(m_err){
      default:
      case ERR_NONE:
        assert(!"missing errno");
        break;  
      case ERR_FULL:
        printf("ERROR filesystem overflow! written: %d missing: %d\n",i_len,cnt&0x7f);
        break;  
      case ERR_TMO:
        printf("ERROR filesystem write timeout %d 0ms\n",(int16_t)(m_stopTime10ms - g_tmr10ms));
        break;  
    }
#endif
  }
  closeTrunc();
  return i_len;
}

#ifdef TEST
uint8_t eeprom[EESIZE];
static void EeFsDump(){
  for(int i=0; i<EESIZE; i++)
  {
    printf("%02x ",eeprom[i]);
    if(i%16 == 15) puts("");
  }
  puts("");
}
void eeprom_read_block (void *pointer_ram,
                   const void *pointer_eeprom,
                        size_t size)
{
  memcpy(pointer_ram,&eeprom[(int)pointer_eeprom],size);
}
void eeWriteBlockCmp(const void *i_pointer_ram, void *i_pointer_eeprom, size_t size)
{
  memcpy(&eeprom[(int)i_pointer_eeprom],i_pointer_ram,size);
}

#define FILES 5
int main();
void showfiles()
{
  printf("------ free: %d\n",EeFsGetFree());
  int8_t err;
  if((err=EeFsck())) printf("ERROR fsck %d\n",err);    
  EeFsDump();
  EFile f;
  for(int i=0; i<MAXFILES; i++){
    if(f.open(i)==0) continue;
    printf("file%d %4d ",i,f.size());
    for(int j=0; j<100; j++){ 
      uint8_t buf[2];
      if(f.readRlc(buf,1)==0) break;
      printf("%c",buf[0]);
    }
    printf("\n");
  }
  
}
int main()
{
  if(!EeFsOpen()) EeFsFormat();
  showfiles();
  EFile    f[FILES];
  uint8_t buf[1000];
  //for(int i=0; i<FILES; i++){ f[i].create(i,5); }

  
  for(int i=0; i<FILES; i++){
    for(int j=0; j<10; j++){ 
      buf[j*2]=i+'0';
      buf[j*2+1]=j+'a';
    }
    f[i].writeRlc(i,3,buf,15,100);
  }
  //for(int i=0; i<FILES; i++){ f[i].trunc(); }
  
 
  showfiles();
  EFile::rm(3);
  EFile::rm(1);
  EFile::rm(5);
  EFile::rm(6);
  showfiles();
  
  //f[0].create(6,6);
  for(int i=0; i<1000; i++) buf[i]='6';
  f[0].writeRlc(6,6,buf,255,100);   
  //f[0].trunc(); 

  f[0].writeRlc(5,5,buf,5,100);   

  showfiles();

  f[0].open(6);
  //  for(int i=0; i<9; i++){ uint8_t b; f[0].readRlc(&b,1);   }
//f[0].trunc(); 

  showfiles();
}


#endif





