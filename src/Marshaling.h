/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __MARSHALING_H
#define __MARSHALING_H

#include <SDL/SDL.h>
#include <SDL/SDL_endian.h>
// Usefull function for marshalling
// You have to use the "safe" version, in case you may access unaligned data.

// 32 bit, Uint8* versions:

inline void addSint32(const Uint8 *data, Sint32 val, int pos)
{
	*((Sint32 *)(data+pos))=SDL_SwapBE32(val);
}

inline Sint32 getSint32(const Uint8 *data, int pos)
{
	return (Sint32)SDL_SwapBE32( *((Sint32 *)(data+pos)) );
}

inline void addUint32(const Uint8 *data, Uint32 val, int pos)
{
	*((Uint32 *)(data+pos))=SDL_SwapBE32(val);
}

inline Uint32 getUint32(const Uint8 *data, int pos)
{
	return (Uint32)SDL_SwapBE32( *((Uint32 *)(data+pos)) );
}

inline Uint32 getUint32safe(const Uint8 *data, int pos)
{
	Uint8 temp[4];
	memcpy(temp, data+pos, 4);
	return (Uint32)SDL_SwapBE32(*((Uint32 *)temp));
}

inline Uint32 getUint32RAW(const Uint8 *data, int pos)
{
	return *(Uint32 *)(((Uint8 *)data) +pos) ;
}

// 32 bits, char* versions:

inline void addSint32(const char *data, Sint32 val, int pos)
{
	*((Sint32 *)(((Uint8 *)data)+pos))=SDL_SwapBE32(val);
}

inline Sint32 getSint32(const char *data, int pos)
{
	return (Sint32)SDL_SwapBE32( *( (Sint32 *) (((Uint8 *)data) +pos) ) );
}

inline void addUint32(const char *data, Uint32 val, int pos)
{
	*((Uint32 *)(((Uint8 *)data)+pos))=SDL_SwapBE32(val);
}

inline Uint32 getUint32(const char *data, int pos)
{
	return (Uint32)SDL_SwapBE32( *( (Uint32 *) (((Uint8 *)data) +pos) ) );
}

// 16 bit, Uint8* version:

inline void addSint16(const Uint8 *data, Sint16 val, int pos)
{
	*((Sint16 *)(data+pos))=SDL_SwapBE16(val);
}

inline void addUint16(const Uint8 *data, Uint16 val, int pos)
{
	*((Sint16 *)(data+pos))=SDL_SwapBE16(val);
}

inline Sint16 getSint16(const Uint8 *data, int pos)
{
	return (Sint16)SDL_SwapBE16(*((Sint16 *)(data+pos)));
}

inline Uint16 getUint16(const Uint8 *data, int pos)
{
	return (Uint16)SDL_SwapBE16(*((Uint16 *)(data+pos)));
}

inline Uint16 getUint16safe(const Uint8 *data, int pos)
{
	Uint8 temp[2];
	memcpy(temp, data+pos, 2);
	return (Uint16)SDL_SwapBE16(*((Uint16 *)temp));
}

// 16 bit, char* version:

inline Uint16 getUint16(const char *data, int pos)
{
	return (Uint16)SDL_SwapBE16(*((Uint16 *)(data+pos)));
}

// 8 bit, Uint8* version:

inline void addUint8(const Uint8 *data, Uint8 val, int pos)
{
	*(((Uint8 *)data)+pos)=val;
}

inline Uint8 getUint8(const Uint8 *data, int pos)
{
	return *(((Uint8 *)data)+pos);
}

// 8 bit, char* version:

inline void addUint8(const char *data, Uint8 val, int pos)
{
	*(((Uint8 *)data)+pos)=val;
}

inline Uint8 getUint8(const char *data, int pos)
{
	return *(((Uint8 *)data)+pos);
}

// 8 bit, Uint8* version:

inline void addSint8(const Uint8 *data, Sint8 val, int pos)
{
	*(((Uint8 *)data)+pos)=val;
}

inline Sint8 getSint8(const Uint8 *data, int pos)
{
	return *(((Sint8 *)data)+pos);
}

// 8 bit, char* version:

inline void addSint8(const char *data, Sint8 val, int pos)
{
	*(((Uint8 *)data)+pos)=val;
}

inline Sint8 getSint8(const char *data, int pos)
{
	return *(((Sint8 *)data)+pos);
}

#endif
 
