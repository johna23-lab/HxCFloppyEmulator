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

#define WRITEREG_B(addr,value) *((volatile unsigned char  *) (addr)) = ((unsigned char)(value))
#define WRITEREG_W(addr,value) *((volatile unsigned short *) (addr)) = ((unsigned short)(value))
#define WRITEREG_L(addr,value) *((volatile unsigned long  *) (addr)) = ((unsigned long)(value))

#define READREG_B(addr) *((volatile unsigned char  *) (addr))
#define READREG_W(addr) *((volatile unsigned short *) (addr))
#define READREG_L(addr) *((volatile unsigned long  *) (addr))


