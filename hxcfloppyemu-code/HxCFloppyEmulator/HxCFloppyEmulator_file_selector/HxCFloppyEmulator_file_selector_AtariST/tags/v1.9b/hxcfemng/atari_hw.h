/*
//
// Copyright (C) 2009, 2010, 2011 Jean-Fran�ois DEL NERO
//
// This file is part of the HxCFloppyEmulator file selector.
//
// HxCFloppyEmulator file selector may be used and distributed without restriction
// provided that this copyright statement is not removed from the file and that any
// derivative work contains the original copyright notice and the associated
// disclaimer.
//
// HxCFloppyEmulator file selector is free software; you can redistribute it
// and/or modify  it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// HxCFloppyEmulator file selector is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//   See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with HxCFloppyEmulator file selector; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//
*/

void init_atari_fdc(unsigned char drive);
unsigned char readsector(unsigned char sectornum,unsigned char * data,unsigned char invalidate_cache);
unsigned char writesector(unsigned char sectornum,unsigned char * data);
unsigned char Keyboard();
void flush_char();
unsigned char get_char();
unsigned char wait_function_key();
void jumptotrack0();
void reboot();
unsigned long read_long_odd(unsigned char * adr);
void write_long_odd(unsigned char * adr, unsigned long value);

unsigned long get_vid_mode();

#define L_INDIAN(var) (((var&0x000000FF)<<24) |((var&0x0000FF00)<<8) |((var&0x00FF0000)>>8) |((var&0xFF000000)>>24))

#ifndef UWORD
#define UWORD unsigned short
#endif
#ifndef UBYTE
#define UBYTE unsigned char
#endif
#ifndef ULONG
#define ULONG unsigned long
#endif
#ifndef LONG
#define LONG long
#endif
#ifndef WORD
#define  WORD short
#endif


#ifndef KEYTAB
typedef struct {
    unsigned char   *unshift;
    unsigned char   *shift;
    unsigned char   *capslock;
} KEYTAB;
#endif


struct dma {
    UWORD   pad0[2];   
     WORD   data;       /* sector count, data register */
     WORD   control;    /* status/control register */
    UBYTE   pad1;
    UBYTE   addr_high;  
    UBYTE   pad2;
    UBYTE   addr_med;
    UBYTE   pad3;
    UBYTE   addr_low;
};

#define DMA     ((volatile struct dma *) 0xFFFF8600)

/* Control register bits */
#define DMA_A0      0x0002
#define DMA_A1      0x0004
#define DMA_HDC     0x0008
#define DMA_SCREG   0x0010
#define DMA_NODMA   0x0040
#define DMA_FDC     0x0080
#define DMA_WRBIT   0x0100

/* Status register bits */
#define DMA_OK      0x0001
#define DMA_SCNOT0  0x0002
#define DMA_DATREQ  0x0004

#define FDC_CS  (DMA_FDC              )
#define FDC_TR  (DMA_FDC|       DMA_A0)
#define FDC_SR  (DMA_FDC|DMA_A1       )
#define FDC_DR  (DMA_FDC|DMA_A1|DMA_A0)

#define FDC_RESTORE 0x00
#define FDC_SEEK    0x10
#define FDC_STEP    0x20
#define FDC_STEPI   0x40
#define FDC_STEPO   0x60
#define FDC_READ    0x80
#define FDC_WRITE   0xA0
#define FDC_READID  0xC0
#define FDC_READTR  0xE0
#define FDC_WRITETR 0xF0
#define FDC_IRUPT   0xD0

#define FDC_BUSY    0x01
#define FDC_DRQ     0x02
#define FDC_LOSTDAT 0x04
#define FDC_TRACK0  0x04
#define FDC_CRCERR  0x08
#define FDC_RNF     0x10
#define FDC_RT_SU   0x20
#define FDC_WRI_PRO 0x40
#define FDC_MOTORON 0x80

struct psg {
    UBYTE   regdata;
    UBYTE   pad1;
    UBYTE   write;
    UBYTE   pad2;
};

#define PSG        ((volatile struct psg *) 0xffff8800)


typedef struct
{
        UBYTE   dum1;
        volatile UBYTE  gpip;
        UBYTE   dum2;
        volatile UBYTE  aer; 
        UBYTE   dum3;
        volatile UBYTE  ddr; 
        UBYTE   dum4;
        volatile UBYTE  iera;
        UBYTE   dum5;
        volatile UBYTE  ierb;
        UBYTE   dum6;
        volatile UBYTE  ipra;
        UBYTE   dum7;
        volatile UBYTE  iprb;
        UBYTE   dum8;
        volatile UBYTE  isra;
        UBYTE   dum9;
        volatile UBYTE  isrb;
        UBYTE   dum10;
        volatile UBYTE  imra;
        UBYTE   dum11;
        volatile UBYTE  imrb;
        UBYTE   dum12;
        volatile UBYTE  vr;  
        UBYTE   dum13;
        volatile UBYTE  tacr;
        UBYTE   dum14;
        volatile UBYTE  tbcr;
        UBYTE   dum15;
        volatile UBYTE  tcdcr;
        UBYTE   dum16;
        volatile UBYTE  tadr; 
        UBYTE   dum17;
        volatile UBYTE  tbdr; 
        UBYTE   dum18;
        volatile UBYTE  tcdr; 
        UBYTE   dum19;
        volatile UBYTE  tddr; 
        UBYTE   dum20;
        volatile UBYTE  scr;  
        UBYTE   dum21;
        volatile UBYTE  ucr;  
        UBYTE   dum22;
        volatile UBYTE  rsr;  
        UBYTE   dum23;
        volatile UBYTE  tsr;  
        UBYTE   dum24;
        volatile UBYTE  udr;  
} MFP;

#define MFP_BASE        ((MFP *)(0xfffffa00L))

#ifdef __VBCC__
// This one comes from TOS.H, but the prototype was LONG Supexec(__reg("a0")LONG(*)())
__regsused("d0/d1/d2/a0/a1/a2") LONG my_Supexec(__reg("a0")LONG * function) =
  "\tpea\t(a0)\n"
  "\tmove.w\t#38,-(sp)\n"
  "\ttrap\t#14\n"
  "\taddq.l\t#6,sp";
#else
#    define my_Supexec Supexec
#endif
