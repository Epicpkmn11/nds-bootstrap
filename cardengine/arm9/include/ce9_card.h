/*-----------------------------------------------------------------

 Copyright (C) 2005  Michael "Chishm" Chisholm

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 If you use this code, please give due credit and email me about your
 project at chishm@hotmail.com
------------------------------------------------------------------*/

#ifndef CARD9_H
#define CARD9_H

#include "my_disc_io.h"

extern vu32* volatile sharedAddr;

static inline bool CARD_StartUp(void) {
	/*sharedAddr[4] = 0x53545254;

	while (sharedAddr[4] != (vu32)0);
	return sharedAddr[4];*/
	return true;
}

static inline bool CARD_IsInserted(void) {
	return true;
}

static inline bool CARD_ReadSector(u32 sector, void *buffer, u32 startOffset, u32 endOffset) {
	sharedAddr[0] = sector;
	sharedAddr[1] = (vu32)buffer;
	sharedAddr[2] = startOffset;
	sharedAddr[3] = endOffset;
	sharedAddr[4] = 0x52534354;

	while (sharedAddr[4] == (vu32)0x52534354);
	return sharedAddr[4];
}

static inline bool CARD_ReadSectors(u32 sector, int count, void *buffer, int ndmaSlot) {
	sharedAddr[0] = sector;
	sharedAddr[1] = count;
	sharedAddr[2] = (vu32)buffer;
	sharedAddr[3] = ndmaSlot;
	sharedAddr[4] = 0x52534353;

	while (sharedAddr[4] == (vu32)0x52534353);
	return sharedAddr[4];
}

/*static inline bool CARD_WriteSector(u32 sector, const void *buffer, int ndmaSlot) {
	return __myio_dsisd.writeSectors(sector, 1, buffer, ndmaSlot);
}

static inline bool CARD_WriteSectors(u32 sector, int count, const void *buffer, int ndmaSlot) {
	return __myio_dsisd.writeSectors(sector, count, buffer, ndmaSlot);
}*/

#endif // CARD9_H
