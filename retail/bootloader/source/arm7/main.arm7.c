/*-----------------------------------------------------------------
 boot.c
 
 BootLoader
 Loads a file into memory and runs it

 All resetMemory and startBinary functions are based 
 on the MultiNDS loader by Darkain.
 Original source available at:
 http://cvs.sourceforge.net/viewcvs.py/ndslib/ndslib/examples/loader/boot/main.cpp

 License:
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
 
	Helpful information:
	This code runs from VRAM bank C on ARM7
------------------------------------------------------------------*/

#ifndef ARM7
# define ARM7
#endif
#include <string.h> // memcpy & memset
#include <stdlib.h> // malloc
#include <nds/ndstypes.h>
#include <nds/arm7/codec.h>
#include <nds/dma.h>
#include <nds/system.h>
#include <nds/interrupts.h>
#include <nds/timers.h>
#include <nds/arm7/audio.h>
#include <nds/memory.h> // tNDSHeader
#include <nds/arm7/i2c.h>
#include <nds/debug.h>

#include "tonccpy.h"
#include "my_fat.h"
#include "debug_file.h"
#include "nds_header.h"
#include "module_params.h"
#include "decompress.h"
#include "dldi_patcher.h"
#include "ips.h"
#include "patch.h"
#include "find.h"
#include "cheat_patch.h"
#include "hook.h"
#include "common.h"
#include "locations.h"
#include "loading_screen.h"

#include "deviceList.h"					// Modified to read from SD instead of NAND

//#define memcpy __builtin_memcpy

//#define resetCpu() __asm volatile("\tswi 0x000000\n");

extern void arm7clearRAM(void);

//extern u32 _start;
extern u32 storedFileCluster;
extern u32 initDisc;
extern u32 gameOnFlashcard;
extern u32 saveOnFlashcard;
extern u32 dsiSD;
extern u32 saveFileCluster;
extern u32 romSize;
extern u32 saveSize;
extern u32 wideCheatFileCluster;
extern u32 wideCheatSize;
extern u32 apPatchFileCluster;
extern u32 apPatchSize;
extern u32 cheatFileCluster;
extern u32 cheatSize;
extern u32 patchOffsetCacheFileCluster;
extern u32 fatTableFileCluster;
extern u32 language;
extern u32 dsiMode; // SDK 5
extern u32 donorSdkVer;
extern u32 patchMpuRegion;
extern u32 patchMpuSize;
extern u32 ceCached;
extern u32 consoleModel;
extern u32 romread_LED;
extern u32 gameSoftReset;
//extern u32 forceSleepPatch;
extern u32 volumeFix;
extern u32 preciseVolumeControl;
extern u32 boostVram;
extern u32 soundFreq;
extern u32 logging;

bool fcInited = false;
bool sdRead = true;

static u32 ce7Location = CARDENGINE_ARM7_LOCATION;
static u32 ce9Location = CARDENGINE_ARM9_LOCATION;

static void initMBK(void) {
	// Give all DSi WRAM to ARM7 at boot
	// This function has no effect with ARM7 SCFG locked
	
	// ARM7 is master of WRAM-A, arm9 of WRAM-B & C
	REG_MBK9 = 0x3000000F;
	
	// WRAM-A fully mapped to ARM7
	*(vu32*)REG_MBK1 = 0x8185898D; // Same as DSiWare
	
	// WRAM-B fully mapped to ARM7 // inverted order
	*(vu32*)REG_MBK2 = 0x9195999D;
	*(vu32*)REG_MBK3 = 0x8185898D;
	
	// WRAM-C fully mapped to arm7 // inverted order
	*(vu32*)REG_MBK4 = 0x9195999D;
	*(vu32*)REG_MBK5 = 0x8185898D;
	
	// WRAM mapped to the 0x3700000 - 0x37FFFFF area 
	// WRAM-A mapped to the 0x37C0000 - 0x37FFFFF area : 256k
	REG_MBK6 = 0x080037C0; // same as DSiWare
	// WRAM-B mapped to the 0x3740000 - 0x37BFFFF area : 512k // why? only 256k real memory is there
	REG_MBK7 = 0x07C03740; // same as DSiWare
	// WRAM-C mapped to the 0x3700000 - 0x373FFFF area : 256k
	REG_MBK8 = 0x07403700; // same as DSiWare
}

/*-------------------------------------------------------------------------
resetMemory_ARM7
Clears all of the NDS's RAM that is visible to the ARM7
Written by Darkain.
Modified by Chishm:
 * Added STMIA clear mem loop
--------------------------------------------------------------------------*/
static void resetMemory_ARM7(void) {
	register int i;
	
	REG_IME = 0;

	for (i = 0; i < 16; i++) {
		SCHANNEL_CR(i) = 0;
		SCHANNEL_TIMER(i) = 0;
		SCHANNEL_SOURCE(i) = 0;
		SCHANNEL_LENGTH(i) = 0;
	}

	REG_SOUNDCNT = 0;

	// Clear out ARM7 DMA channels and timers
	for (i = 0; i < 4; i++) {
		DMA_CR(i) = 0;
		DMA_SRC(i) = 0;
		DMA_DEST(i) = 0;
		TIMER_CR(i) = 0;
		TIMER_DATA(i) = 0;
	}

	arm7clearRAM();								// clear exclusive IWRAM
	toncset((u32*)0x02000000, 0, 0x340000);	// clear part of EWRAM - except before nds-bootstrap images
	toncset((u32*)0x02380000, 0, 0x74000);		// clear part of EWRAM - except before 0x023F4000, which has the arm9 code
	toncset((u32*)0x02400000, 0, 0x3CD000);	// clear part of EWRAM - except before ce7 and ce9 binaries
	toncset((u32*)0x027F8000, 0, 0x8000);		// clear part of EWRAM
	toncset((u32*)0x02D00000, 0, 0x2FE000);	// clear part of EWRAM
	toncset((u32*)0x02FFF000, 0, 0x1000);		// clear part of EWRAM: header
	REG_IE = 0;
	REG_IF = ~0;
	*(vu32*)(0x04000000 - 4) = 0;  // IRQ_HANDLER ARM7 version
	*(vu32*)(0x04000000 - 8) = ~0; // VBLANK_INTR_WAIT_FLAGS, ARM7 version
	REG_POWERCNT = 1;  // Turn off power to stuff
}

static void NDSTouchscreenMode(void) {
	u8 volLevel;
	
	// 0xAC: special setting (when found special gamecode)
	// 0xA7: normal setting (for any other gamecodes)
	volLevel = volumeFix ? 0xAC : 0xA7;

	// Touchscreen
	cdcReadReg (0x63, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x3A, 0x00);
	cdcReadReg (CDC_CONTROL, 0x51);
	cdcReadReg (CDC_TOUCHCNT, 0x02);
	cdcReadReg (CDC_CONTROL, 0x3F);
	cdcReadReg (CDC_SOUND, 0x28);
	cdcReadReg (CDC_SOUND, 0x2A);
	cdcReadReg (CDC_SOUND, 0x2E);
	cdcWriteReg(CDC_CONTROL, 0x52, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x40, 0x0C);
	cdcWriteReg(CDC_SOUND, 0x24, 0xFF);
	cdcWriteReg(CDC_SOUND, 0x25, 0xFF);
	cdcWriteReg(CDC_SOUND, 0x26, 0x7F);
	cdcWriteReg(CDC_SOUND, 0x27, 0x7F);
	cdcWriteReg(CDC_SOUND, 0x28, 0x4A);
	cdcWriteReg(CDC_SOUND, 0x29, 0x4A);
	cdcWriteReg(CDC_SOUND, 0x2A, 0x10);
	cdcWriteReg(CDC_SOUND, 0x2B, 0x10);
	cdcWriteReg(CDC_CONTROL, 0x51, 0x00);
	cdcReadReg (CDC_TOUCHCNT, 0x02);
	cdcWriteReg(CDC_TOUCHCNT, 0x02, 0x98);
	cdcWriteReg(CDC_SOUND, 0x23, 0x00);
	cdcWriteReg(CDC_SOUND, 0x1F, 0x14);
	cdcWriteReg(CDC_SOUND, 0x20, 0x14);
	cdcWriteReg(CDC_CONTROL, 0x3F, 0x00);
	cdcReadReg (CDC_CONTROL, 0x0B);
	cdcWriteReg(CDC_CONTROL, 0x05, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x0B, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x0C, 0x02);
	cdcWriteReg(CDC_CONTROL, 0x12, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x13, 0x02);
	cdcWriteReg(CDC_SOUND, 0x2E, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x3A, 0x60);
	cdcWriteReg(CDC_CONTROL, 0x01, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x39, 0x66);
	cdcReadReg (CDC_SOUND, 0x20);
	cdcWriteReg(CDC_SOUND, 0x20, 0x10);
	cdcWriteReg(CDC_CONTROL, 0x04, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x12, 0x81);
	cdcWriteReg(CDC_CONTROL, 0x13, 0x82);
	cdcWriteReg(CDC_CONTROL, 0x51, 0x82);
	cdcWriteReg(CDC_CONTROL, 0x51, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x04, 0x03);
	cdcWriteReg(CDC_CONTROL, 0x05, 0xA1);
	cdcWriteReg(CDC_CONTROL, 0x06, 0x15);
	cdcWriteReg(CDC_CONTROL, 0x0B, 0x87);
	cdcWriteReg(CDC_CONTROL, 0x0C, 0x83);
	cdcWriteReg(CDC_CONTROL, 0x12, 0x87);
	cdcWriteReg(CDC_CONTROL, 0x13, 0x83);
	cdcReadReg (CDC_TOUCHCNT, 0x10);
	cdcWriteReg(CDC_TOUCHCNT, 0x10, 0x08);
	cdcWriteReg(0x04, 0x08, 0x7F);
	cdcWriteReg(0x04, 0x09, 0xE1);
	cdcWriteReg(0x04, 0x0A, 0x80);
	cdcWriteReg(0x04, 0x0B, 0x1F);
	cdcWriteReg(0x04, 0x0C, 0x7F);
	cdcWriteReg(0x04, 0x0D, 0xC1);
	cdcWriteReg(CDC_CONTROL, 0x41, 0x08);
	cdcWriteReg(CDC_CONTROL, 0x42, 0x08);
	cdcWriteReg(CDC_CONTROL, 0x3A, 0x00);
	cdcWriteReg(0x04, 0x08, 0x7F);
	cdcWriteReg(0x04, 0x09, 0xE1);
	cdcWriteReg(0x04, 0x0A, 0x80);
	cdcWriteReg(0x04, 0x0B, 0x1F);
	cdcWriteReg(0x04, 0x0C, 0x7F);
	cdcWriteReg(0x04, 0x0D, 0xC1);
	cdcWriteReg(CDC_SOUND, 0x2F, 0x2B);
	cdcWriteReg(CDC_SOUND, 0x30, 0x40);
	cdcWriteReg(CDC_SOUND, 0x31, 0x40);
	cdcWriteReg(CDC_SOUND, 0x32, 0x60);
	cdcReadReg (CDC_CONTROL, 0x74);
	cdcWriteReg(CDC_CONTROL, 0x74, 0x02);
	cdcReadReg (CDC_CONTROL, 0x74);
	cdcWriteReg(CDC_CONTROL, 0x74, 0x10);
	cdcReadReg (CDC_CONTROL, 0x74);
	cdcWriteReg(CDC_CONTROL, 0x74, 0x40);
	cdcWriteReg(CDC_SOUND, 0x21, 0x20);
	cdcWriteReg(CDC_SOUND, 0x22, 0xF0);
	cdcReadReg (CDC_CONTROL, 0x51);
	cdcReadReg (CDC_CONTROL, 0x3F);
	cdcWriteReg(CDC_CONTROL, 0x3F, 0xD4);
	cdcWriteReg(CDC_SOUND, 0x23, 0x44);
	cdcWriteReg(CDC_SOUND, 0x1F, 0xD4);
	cdcWriteReg(CDC_SOUND, 0x28, 0x4E);
	cdcWriteReg(CDC_SOUND, 0x29, 0x4E);
	cdcWriteReg(CDC_SOUND, 0x24, 0x9E);
	cdcWriteReg(CDC_SOUND, 0x25, 0x9E);
	cdcWriteReg(CDC_SOUND, 0x20, 0xD4);
	cdcWriteReg(CDC_SOUND, 0x2A, 0x14);
	cdcWriteReg(CDC_SOUND, 0x2B, 0x14);
	cdcWriteReg(CDC_SOUND, 0x26, 0xA7);
	cdcWriteReg(CDC_SOUND, 0x27, 0xA7);
	cdcWriteReg(CDC_CONTROL, 0x40, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x3A, 0x60);
	cdcWriteReg(CDC_SOUND, 0x26, volLevel);
	cdcWriteReg(CDC_SOUND, 0x27, volLevel);
	cdcWriteReg(CDC_SOUND, 0x2E, 0x03);
	cdcWriteReg(CDC_TOUCHCNT, 0x03, 0x00);
	cdcWriteReg(CDC_SOUND, 0x21, 0x20);
	cdcWriteReg(CDC_SOUND, 0x22, 0xF0);
	cdcReadReg (CDC_SOUND, 0x22);
	cdcWriteReg(CDC_SOUND, 0x22, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x52, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x51, 0x00);
	
	// Set remaining values
	cdcWriteReg(CDC_CONTROL, 0x03, 0x44);
	cdcWriteReg(CDC_CONTROL, 0x0D, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x0E, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x0F, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x10, 0x08);
	cdcWriteReg(CDC_CONTROL, 0x14, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x15, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x16, 0x04);
	cdcWriteReg(CDC_CONTROL, 0x1A, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x1E, 0x01);
	cdcWriteReg(CDC_CONTROL, 0x24, 0x80);
	cdcWriteReg(CDC_CONTROL, 0x33, 0x34);
	cdcWriteReg(CDC_CONTROL, 0x34, 0x32);
	cdcWriteReg(CDC_CONTROL, 0x35, 0x12);
	cdcWriteReg(CDC_CONTROL, 0x36, 0x03);
	cdcWriteReg(CDC_CONTROL, 0x37, 0x02);
	cdcWriteReg(CDC_CONTROL, 0x38, 0x03);
	cdcWriteReg(CDC_CONTROL, 0x3C, 0x19);
	cdcWriteReg(CDC_CONTROL, 0x3D, 0x05);
	cdcWriteReg(CDC_CONTROL, 0x44, 0x0F);
	cdcWriteReg(CDC_CONTROL, 0x45, 0x38);
	cdcWriteReg(CDC_CONTROL, 0x49, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x4A, 0x00);
	cdcWriteReg(CDC_CONTROL, 0x4B, 0xEE);
	cdcWriteReg(CDC_CONTROL, 0x4C, 0x10);
	cdcWriteReg(CDC_CONTROL, 0x4D, 0xD8);
	cdcWriteReg(CDC_CONTROL, 0x4E, 0x7E);
	cdcWriteReg(CDC_CONTROL, 0x4F, 0xE3);
	cdcWriteReg(CDC_CONTROL, 0x58, 0x7F);
	cdcWriteReg(CDC_CONTROL, 0x74, 0xD2);
	cdcWriteReg(CDC_CONTROL, 0x75, 0x2C);
	cdcWriteReg(CDC_SOUND, 0x22, 0x70);
	cdcWriteReg(CDC_SOUND, 0x2C, 0x20);

	// Finish up!
	cdcReadReg (CDC_TOUCHCNT, 0x02);
	cdcWriteReg(CDC_TOUCHCNT, 0x02, 0x98);
	cdcWriteReg(0xFF, 0x05, 0x00); //writeTSC(0x00, 0xFF);

	// Power management
	writePowerManagement(PM_READ_REGISTER, 0x00); //*(unsigned char*)0x40001C2 = 0x80, 0x00; // read PWR[0]   ;<-- also part of TSC !
	writePowerManagement(PM_CONTROL_REG, 0x0D); //*(unsigned char*)0x40001C2 = 0x00, 0x0D; // PWR[0]=0Dh    ;<-- also part of TSC !
}

static module_params_t* buildModuleParams(u32 donorSdkVer) {
	//u32* moduleParamsOffset = malloc(sizeof(module_params_t));
	u32* moduleParamsOffset = malloc(0x100);

	//memset(moduleParamsOffset, 0, sizeof(module_params_t));
	memset(moduleParamsOffset, 0, 0x100);

	module_params_t* moduleParams = (module_params_t*)(moduleParamsOffset - 7);

	moduleParams->compressed_static_end = 0; // Avoid decompressing
	switch (donorSdkVer) {
		case 0:
		default:
			break;
		case 1:
			moduleParams->sdk_version = 0x1000500;
			break;
		case 2:
			moduleParams->sdk_version = 0x2001000;
			break;
		case 3:
			moduleParams->sdk_version = 0x3002001;
			break;
		case 4:
			moduleParams->sdk_version = 0x4002001;
			break;
		case 5:
			moduleParams->sdk_version = 0x5003001;
			break;
	}

	return moduleParams;
}

static module_params_t* getModuleParams(const tNDSHeader* ndsHeader) {
	nocashMessage("Looking for moduleparams...\n");

	u32* moduleParamsOffset = findModuleParamsOffset(ndsHeader);

	//module_params_t* moduleParams = (module_params_t*)((u32)moduleParamsOffset - 0x1C);
	return moduleParamsOffset ? (module_params_t*)(moduleParamsOffset - 7) : NULL;
}

static inline u32 getRomSizeNoArm9(const tNDSHeader* ndsHeader) {
	return ndsHeader->romSize - 0x4000 - ndsHeader->arm9binarySize;
}

// SDK 5
static bool ROMsupportsDsiMode(const tNDSHeader* ndsHeader) {
	return (ndsHeader->unitCode > 0);
}

// SDK 5
static bool ROMisDsiExclusive(const tNDSHeader* ndsHeader) {
	return (ndsHeader->unitCode == 0x03);
}

static void loadBinary_ARM7(const tDSiHeader* dsiHeaderTemp, aFile file) {
	nocashMessage("loadBinary_ARM7");

	//u32 ndsHeader[0x170 >> 2];
	//u32 dsiHeader[0x2F0 >> 2]; // SDK 5

	// Read DSi header (including NDS header)
	//fileRead((char*)ndsHeader, file, 0, 0x170, 3);
	//fileRead((char*)dsiHeader, file, 0, 0x2F0, 2); // SDK 5
	if (gameOnFlashcard) {
		tonccpy((char*)dsiHeaderTemp, (char*)DSI_HEADER_SDK5, sizeof(*dsiHeaderTemp));
	} else {
		fileRead((char*)dsiHeaderTemp, file, 0, sizeof(*dsiHeaderTemp), 0);
	}

	// Fix Pokemon games needing header data.
	//fileRead((char*)0x027FF000, file, 0, 0x170, 3);
	//tonccpy((char*)0x027FF000, &dsiHeaderTemp.ndshdr, sizeof(dsiHeaderTemp.ndshdr));
	tNDSHeader* ndsHeaderPokemon = (tNDSHeader*)NDS_HEADER_POKEMON;
	*ndsHeaderPokemon = dsiHeaderTemp->ndshdr;

	const char* romTid = getRomTid(&dsiHeaderTemp->ndshdr);
	if (
		strncmp(romTid, "ADA", 3) == 0    // Diamond
		|| strncmp(romTid, "APA", 3) == 0 // Pearl
		|| strncmp(romTid, "CPU", 3) == 0 // Platinum
		|| strncmp(romTid, "IPK", 3) == 0 // HG
		|| strncmp(romTid, "IPG", 3) == 0 // SS
	) {
		// Make the Pokemon game code ADAJ.
		const char gameCodePokemon[] = { 'A', 'D', 'A', 'J' };
		tonccpy(ndsHeaderPokemon->gameCode, gameCodePokemon, 4);
	}
    
    /*if (
		strncmp(romTid, "APDE", 4) == 0    // Pokemon Dash
	) {
		// read statically the 6D2C4 function data 
        fileRead(0x0218A960, file, 0x000CE000, 0x1000, 0);
        fileRead(0x020D6340, file, 0x000D2400, 0x200, 0);
        fileRead(0x020D6140, file, 0x000CFA00, 0x200, 0);
        fileRead(0x023B9F00, file, 0x000CFC00, 0x2800, 0);
	}*/

	/*isGSDD = (strncmp(romTid, "BO5", 3) == 0)			// Golden Sun: Dark Dawn
        || (strncmp(romTid, "TBR", 3) == 0)			    // Disney Pixar Brave 
        ;*/
    

	// Load binaries into memory
	if (gameOnFlashcard) {
		tonccpy(dsiHeaderTemp->ndshdr.arm9destination, (char*)0x02800000, dsiHeaderTemp->ndshdr.arm9binarySize);
		tonccpy(dsiHeaderTemp->ndshdr.arm7destination, (char*)0x02B80000, dsiHeaderTemp->ndshdr.arm7binarySize);
	} else {
		fileRead(dsiHeaderTemp->ndshdr.arm9destination, file, dsiHeaderTemp->ndshdr.arm9romOffset, dsiHeaderTemp->ndshdr.arm9binarySize, 0);
		fileRead(dsiHeaderTemp->ndshdr.arm7destination, file, dsiHeaderTemp->ndshdr.arm7romOffset, dsiHeaderTemp->ndshdr.arm7binarySize, 0);
	}
}

static void loadIBinary_ARM7(const tDSiHeader* dsiHeaderTemp, aFile file) {
	if (gameOnFlashcard) {
		if (dsiHeaderTemp->arm9ibinarySize > 0) {
			tonccpy(dsiHeaderTemp->arm9idestination, (char*)0x02C00000, dsiHeaderTemp->arm9ibinarySize);
		}
		if (dsiHeaderTemp->arm7ibinarySize > 0) {
			tonccpy(dsiHeaderTemp->arm7idestination, (char*)0x02C80000, dsiHeaderTemp->arm7ibinarySize);
		}
	} else {
		if (dsiHeaderTemp->arm9ibinarySize > 0) {
			fileRead(dsiHeaderTemp->arm9idestination, file, (u32)dsiHeaderTemp->arm9iromOffset, dsiHeaderTemp->arm9ibinarySize, 0);
		}
		if (dsiHeaderTemp->arm7ibinarySize > 0) {
			fileRead(dsiHeaderTemp->arm7idestination, file, (u32)dsiHeaderTemp->arm7iromOffset, dsiHeaderTemp->arm7ibinarySize, 0);
		}
	}
}

static module_params_t* loadModuleParams(const tNDSHeader* ndsHeader, bool* foundPtr) {
	module_params_t* moduleParams = getModuleParams(ndsHeader);
	*foundPtr = (bool)moduleParams;
	if (*foundPtr) {
		// Found module params
	} else {
		nocashMessage("No moduleparams?\n");
		moduleParams = buildModuleParams(donorSdkVer);
	}
	return moduleParams;
}

static bool isROMLoadableInRAM(const tNDSHeader* ndsHeader, const module_params_t* moduleParams, u32 consoleModel) {
	if (gameOnFlashcard && !fcInited) return false;

	const char* romTid = getRomTid(ndsHeader);
	if (strncmp(romTid, "APD", 3) == 0
	|| strncmp(romTid, "UBR", 3) == 0
	|| strncmp(romTid, "UOR", 3) == 0
	|| (consoleModel == 0 && strncmp(romTid, "BKW", 3) == 0)
	|| (consoleModel == 0 && strncmp(romTid, "VKG", 3) == 0)) {
		return false;
	} else return ((dsiModeConfirmed && consoleModel > 0 && getRomSizeNoArm9(ndsHeader) <= 0x01000000)
			|| (!dsiModeConfirmed && isSdk5(moduleParams) && consoleModel > 0 && getRomSizeNoArm9(ndsHeader) <= 0x01000000)
			|| (!dsiModeConfirmed && isSdk5(moduleParams) && consoleModel == 0 && getRomSizeNoArm9(ndsHeader) <= 0x00700000)
			|| (!dsiModeConfirmed && !isSdk5(moduleParams) && consoleModel > 0 && getRomSizeNoArm9(ndsHeader) <= 0x01800000)
			|| (!dsiModeConfirmed && !isSdk5(moduleParams) && consoleModel == 0 && getRomSizeNoArm9(ndsHeader) <= 0x00800000));
}

static vu32* storeArm9StartAddress(tNDSHeader* ndsHeader, const module_params_t* moduleParams) {
	vu32* arm9StartAddress = (vu32*)(isSdk5(moduleParams) ? ARM9_START_ADDRESS_SDK5_LOCATION : ARM9_START_ADDRESS_LOCATION);
	/*if (isGSDD) {
		arm9StartAddress = (vu32*)(ARM9_START_ADDRESS_4MB_LOCATION);
	}*/

	// Store for later
	*arm9StartAddress = (vu32)ndsHeader->arm9executeAddress;
	
	// Exclude the ARM9 start address, so as not to start it
	ndsHeader->arm9executeAddress = NULL; // 0
	
	return arm9StartAddress;
}

static tNDSHeader* loadHeader(tDSiHeader* dsiHeaderTemp, const module_params_t* moduleParams, int dsiMode) {
	tNDSHeader* ndsHeader = (tNDSHeader*)(isSdk5(moduleParams) ? NDS_HEADER_SDK5 : NDS_HEADER);
	/*if (isGSDD) {
		ndsHeader = (tNDSHeader*)(NDS_HEADER_4MB);
	}*/

	// Copy the header to its proper location
	//dmaCopyWords(3, &dsiHeaderTemp.ndshdr, (char*)ndsHeader, 0x170);
	//dmaCopyWords(3, &dsiHeaderTemp.ndshdr, ndsHeader, sizeof(dsiHeaderTemp.ndshdr));
	*ndsHeader = dsiHeaderTemp->ndshdr;
	if (dsiMode > 0) {
		//dmaCopyWords(3, &dsiHeaderTemp, ndsHeader, sizeof(dsiHeaderTemp));
		//*(tDSiHeader*)ndsHeader = *dsiHeaderTemp;
		tDSiHeader* dsiHeader = (tDSiHeader*)(isSdk5(moduleParams) ? DSI_HEADER_SDK5 : DSI_HEADER); // __DSiHeader
		*dsiHeader = *dsiHeaderTemp;
	}

	return ndsHeader;
}

static void my_readUserSettings(tNDSHeader* ndsHeader) {
	PERSONAL_DATA slot1;
	PERSONAL_DATA slot2;

	short slot1count, slot2count; //u8
	short slot1CRC, slot2CRC;

	u32 userSettingsBase;

	// Get settings location
	readFirmware(0x20, &userSettingsBase, 2);

	u32 slot1Address = userSettingsBase * 8;
	u32 slot2Address = userSettingsBase * 8 + 0x100;

	// Reload DS Firmware settings
	readFirmware(slot1Address, &slot1, sizeof(PERSONAL_DATA)); //readFirmware(slot1Address, personalData, 0x70);
	readFirmware(slot2Address, &slot2, sizeof(PERSONAL_DATA)); //readFirmware(slot2Address, personalData, 0x70);
	readFirmware(slot1Address + 0x70, &slot1count, 2); //readFirmware(slot1Address + 0x70, &slot1count, 1);
	readFirmware(slot2Address + 0x70, &slot2count, 2); //readFirmware(slot1Address + 0x70, &slot2count, 1);
	readFirmware(slot1Address + 0x72, &slot1CRC, 2);
	readFirmware(slot2Address + 0x72, &slot2CRC, 2);

	// Default to slot 1 user settings
	void *currentSettings = &slot1;

	short calc1CRC = swiCRC16(0xFFFF, &slot1, sizeof(PERSONAL_DATA));
	short calc2CRC = swiCRC16(0xFFFF, &slot2, sizeof(PERSONAL_DATA));

	// Bail out if neither slot is valid
	if (calc1CRC != slot1CRC && calc2CRC != slot2CRC) {
		return;
	}

	// If both slots are valid pick the most recent
	if (calc1CRC == slot1CRC && calc2CRC == slot2CRC) { 
		currentSettings = (slot2count == ((slot1count + 1) & 0x7f) ? &slot2 : &slot1); //if ((slot1count & 0x7F) == ((slot2count + 1) & 0x7F)) {
	} else {
		if (calc2CRC == slot2CRC) {
			currentSettings = &slot2;
		}
	}

	PERSONAL_DATA* personalData = (PERSONAL_DATA*)((u32)__NDSHeader - (u32)ndsHeader + (u32)PersonalData); //(u8*)((u32)ndsHeader - 0x180)
	
	tonccpy(PersonalData, currentSettings, sizeof(PERSONAL_DATA));
	
	if (language >= 0 && language < 6) {
		// Change language
		personalData->language = language; //*(u8*)((u32)ndsHeader - 0x11C) = language;
	}
	
	if (personalData->language != 6 && ndsHeader->reserved1[8] == 0x80) {
		ndsHeader->reserved1[8] = 0;	// Patch iQue game to region-free
	}
}

static void NTR_BIOS() {
	// Switch to NTR mode BIOS (no effect with locked ARM7 SCFG)
	nocashMessage("Switch to NTR mode BIOS");
	REG_SCFG_ROM = 0x703;
}

static void loadROMintoRAM(const tNDSHeader* ndsHeader, const module_params_t* moduleParams, aFile file) {
	// Load ROM into RAM
	fileRead((char*)((isSdk5(moduleParams) || dsiModeConfirmed) ? ROM_SDK5_LOCATION : ROM_LOCATION), file, 0x4000 + ndsHeader->arm9binarySize, getRomSizeNoArm9(ndsHeader), 0);

	if (!isSdk5(moduleParams)) {
		if(*(u32*)((ROM_LOCATION-0x4000-ndsHeader->arm9binarySize)+0x003128AC) == 0x4B434148){
			*(u32*)((ROM_LOCATION-0x4000-ndsHeader->arm9binarySize)+0x003128AC) = 0xA00;	// Primary fix for Mario's Holiday
		}
	}
}

static void loadOverlaysintoRAM(const tNDSHeader* ndsHeader, const module_params_t* moduleParams, aFile file) {
	// Load overlays into RAM
	u32 overlaysSize = 0;
	for (int i = ndsHeader->arm9romOffset+ndsHeader->arm9binarySize; i <= ndsHeader->arm7romOffset; i++) {
		overlaysSize = i;
	}
	u32 overlaysLocation = (u32)((isSdk5(moduleParams) || dsiModeConfirmed) ? ROM_SDK5_LOCATION : ROM_LOCATION);
	if (consoleModel == 0 && isSdk5(moduleParams)) {
		overlaysLocation = (u32)retail_CACHE_ADRESS_START_SDK5;

		const char* romTid = getRomTid(ndsHeader);
		if (strncmp(romTid, "VKG", 3) == 0) {
			overlaysLocation = (u32)CACHE_ADRESS_START_low;
		}
	}
	fileRead((char*)overlaysLocation, file, 0x4000 + ndsHeader->arm9binarySize, overlaysSize, 0);

	if (!isSdk5(moduleParams)) {
		if(*(u32*)((ROM_LOCATION-0x4000-ndsHeader->arm9binarySize)+0x003128AC) == 0x4B434148){
			*(u32*)((ROM_LOCATION-0x4000-ndsHeader->arm9binarySize)+0x003128AC) = 0xA00;	// Primary fix for Mario's Holiday
		}
	}
}

static bool supportsExceptionHandler(const tNDSHeader* ndsHeader) {
	const char* romTid = getRomTid(ndsHeader);

	// ExceptionHandler2 (red screen) blacklist
	return (strncmp(romTid, "ASM", 3) != 0	// SM64DS
	&& strncmp(romTid, "SMS", 3) != 0	// SMSW
	&& strncmp(romTid, "A2D", 3) != 0	// NSMB
	&& strncmp(romTid, "ADM", 3) != 0);	// AC:WW
}

/*-------------------------------------------------------------------------
startBinary_ARM7
Jumps to the ARM7 NDS binary in sync with the display and ARM9
Written by Darkain.
Modified by Chishm:
 * Removed MultiNDS specific stuff
--------------------------------------------------------------------------*/
static void startBinary_ARM7(const vu32* tempArm9StartAddress) {
	REG_IME = 0;

	while (REG_VCOUNT != 191);
	while (REG_VCOUNT == 191);

	// Copy NDS ARM9 start address into the header, starting ARM9
	ndsHeader->arm9executeAddress = (void*)*tempArm9StartAddress;

	// Get the ARM9 to boot
	arm9_stateFlag = ARM9_BOOTBIN;

	while (REG_VCOUNT != 191);
	while (REG_VCOUNT == 191);

	// Start ARM7
	VoidFn arm7code = (VoidFn)ndsHeader->arm7executeAddress;
	arm7code();
}

static void setMemoryAddress(const tNDSHeader* ndsHeader, const module_params_t* moduleParams, bool isDSiWare) {
	if (ROMsupportsDsiMode(ndsHeader)) {
		u8* deviceListAddr = (u8*)((u8*)0x02FFE1D4);
		tonccpy(deviceListAddr, deviceList_bin, deviceList_bin_len);

		const char *ndsPath = "nand:/dsiware.nds";
		tonccpy(deviceListAddr+0x3C0, ndsPath, sizeof(ndsPath));

		tonccpy((u32*)0x02FFC000, (u32*)DSI_HEADER_SDK5, 0x1000);		// Make a duplicate of DSi header
		tonccpy((u32*)0x02FFFA80, (u32*)NDS_HEADER_SDK5, 0x160);	// Make a duplicate of DS header

		*(u32*)(0x02FFA680) = 0x02FD4D80;
		*(u32*)(0x02FFA684) = 0x00000000;
		*(u32*)(0x02FFA688) = 0x00001980;

		*(u32*)(0x02FFF00C) = 0x0000007F;
		*(u32*)(0x02FFF010) = 0x550E25B8;
		*(u32*)(0x02FFF014) = 0x02FF4000;
	}

	if (isDSiWare) {
		*(u16*)(0x02FFFC40) = 0x3;						// Boot Indicator (NAND/SD)
		return;
	}

	u32 chipID = getChipId(ndsHeader, moduleParams);
    dbg_printf("chipID: ");
    dbg_hexa(chipID);
    dbg_printf("\n"); 

    // TODO
    // figure out what is 0x027ffc10, somehow related to cardId check
    //*((u32*)(isSdk5(moduleParams) ? 0x02fffc10 : 0x027ffc10)) = 1;

	/*if (isGSDD) {
		// Set memory values expected by loaded NDS
		// from NitroHax, thanks to Chism
		*((u32*)0x023ff800) = chipID;					// CurrentCardID
		*((u32*)0x023ff804) = chipID;					// Command10CardID
		*((u32*)0x023ffc00) = chipID;					// 3rd chip ID
		*((u32*)0x023ffc04) = chipID;					// 4th chip ID
		*((u16*)0x023ff808) = ndsHeader->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
		*((u16*)0x023ff80a) = ndsHeader->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]
		*((u16*)0x023ffc08) = ndsHeader->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
		*((u16*)0x023ffc0a) = ndsHeader->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]
		*((u16*)0x023ffc40) = 0x1;						// Booted from card -- EXTREMELY IMPORTANT!!! Thanks to cReDiAr
		return;
	}*/

    // Set memory values expected by loaded NDS
    // from NitroHax, thanks to Chism
	*((u32*)(isSdk5(moduleParams) ? 0x02fff800 : 0x027ff800)) = chipID;					// CurrentCardID
	*((u32*)(isSdk5(moduleParams) ? 0x02fff804 : 0x027ff804)) = chipID;					// Command10CardID
	*((u16*)(isSdk5(moduleParams) ? 0x02fff808 : 0x027ff808)) = ndsHeader->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
	*((u16*)(isSdk5(moduleParams) ? 0x02fff80a : 0x027ff80a)) = ndsHeader->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]

	// Copies of above
	*((u32*)(isSdk5(moduleParams) ? 0x02fffc00 : 0x027ffc00)) = chipID;					// CurrentCardID
	*((u32*)(isSdk5(moduleParams) ? 0x02fffc04 : 0x027ffc04)) = chipID;					// Command10CardID
	*((u16*)(isSdk5(moduleParams) ? 0x02fffc08 : 0x027ffc08)) = ndsHeader->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
	*((u16*)(isSdk5(moduleParams) ? 0x02fffc0a : 0x027ffc0a)) = ndsHeader->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]

	*((u16*)(isSdk5(moduleParams) ? 0x02fffc40 : 0x027ffc40)) = 0x1;						// Boot Indicator (Booted from card for SDK5) -- EXTREMELY IMPORTANT!!! Thanks to cReDiAr
}

int arm7_main(void) {
	nocashMessage("bootloader");

	initMBK();

	// Wait for ARM9 to at least start
	while (arm9_stateFlag < ARM9_START);

	// Get ARM7 to clear RAM
	nocashMessage("Getting ARM7 to clear RAM...\n");
	resetMemory_ARM7();

	// Init card
	if (dsiSD) {
		if (!FAT_InitFiles(true, 0)) {
			nocashMessage("!FAT_InitFiles");
			errorOutput();
			//return -1;
		}
	}

	if (gameOnFlashcard || saveOnFlashcard) {
		sdRead = false;
		// Init Slot-1 card
		fcInited = FAT_InitFiles(initDisc, 0);
		if (!fcInited) {
			nocashMessage("!FAT_InitFiles");
			//return -1;
		}
		sdRead = dsiSD;
	}

	if (logging) {
		enableDebug(getBootFileCluster("NDSBTSRP.LOG", 0));
	}

	// ROM file
	if (gameOnFlashcard) sdRead = false;
	aFile* romFile = (aFile*)ROM_FILE_LOCATION;
	*romFile = getFileFromCluster(storedFileCluster);

	/*const char* bootName = "BOOT.NDS";

	// Invalid file cluster specified
	if ((romFile->firstCluster < CLUSTER_FIRST) || (romFile->firstCluster >= CLUSTER_EOF)) {
		*romFile = getBootFileCluster(bootName, 0);
	}

	if (romFile->firstCluster == CLUSTER_FREE) {
		nocashMessage("fileCluster == CLUSTER_FREE");
		errorOutput();
		//return -1;
	}*/
	
	// FAT table file
	aFile fatTableFile = getFileFromCluster(fatTableFileCluster);
	if (fatTableFile.firstCluster != CLUSTER_FREE) {
		fileRead((char*)0x27C0000, fatTableFile, 0, 0x400, -1);
	}
	bool fatTableEmpty = (*(u32*)(0x27C0200) == 0);

	if (*(u32*)(0x27C0040) != storedFileCluster
	|| *(u32*)(0x27C0044) != romSize)
	{
		fatTableEmpty = true;
	}

	if (!gameOnFlashcard) {
		if (*(u32*)(0x27C0048) != saveFileCluster
		|| *(u32*)(0x27C004C) != saveSize)
		{
			fatTableEmpty = true;
		}
	}

	if (fatTableEmpty) {
		pleaseWaitOutput();
		buildFatTableCache(romFile, 0);
	} else {
		tonccpy((char*)ROM_FILE_LOCATION, (char*)0x27C0000, sizeof(aFile));
	}
	if (gameOnFlashcard) {
		romFile->fatTableCache = (u32*)0x2700000;	// Change fatTableCache addr for ce9 usage
		tonccpy((char*)ROM_FILE_LOCATION_MAINMEM, (char*)ROM_FILE_LOCATION, sizeof(aFile));
	}

	sdRead = (saveOnFlashcard ? false : dsiSD);

	// Sav file
	aFile* savFile = (aFile*)SAV_FILE_LOCATION;
	*savFile = getFileFromCluster(saveFileCluster);
	
	if (savFile->firstCluster != CLUSTER_FREE) {
		if (saveOnFlashcard) {
			tonccpy((char*)SAV_FILE_LOCATION_MAINMEM, (char*)SAV_FILE_LOCATION, sizeof(aFile));
		}
		if (!gameOnFlashcard) {
			if (fatTableEmpty) {
				buildFatTableCache(savFile, 0);		// Bugged, if ROM is being loaded from flashcard
			} else {
				tonccpy((char*)SAV_FILE_LOCATION, (char*)0x27C0020, sizeof(aFile));
			}
		}
	}

	if (gameOnFlashcard) sdRead = false;

	if (fatTableEmpty) {
		tonccpy((char*)0x27C0000, (char*)ROM_FILE_LOCATION, sizeof(aFile));
		if (!gameOnFlashcard) {
			tonccpy((char*)0x27C0020, (char*)SAV_FILE_LOCATION, sizeof(aFile));
		}
		*(u32*)(0x27C0040) = storedFileCluster;
		*(u32*)(0x27C0044) = romSize;
		if (!gameOnFlashcard) {
			*(u32*)(0x27C0048) = saveFileCluster;
			*(u32*)(0x27C004C) = saveSize;
		}
		fileWrite((char*)0x27C0000, fatTableFile, 0, 0x200, -1);
		fileWrite((char*)0x3700000, fatTableFile, 0x200, 0x80000, -1);
	} else {
		fileRead((char*)0x3700000, fatTableFile, 0x200, 0x80000, 0);
	}
	if (gameOnFlashcard) {
		tonccpy((char*)0x2700000, (char*)0x3700000, 0x7FFC0);
		romFile->fatTableCache = (u32*)0x3700000;	// Revert back for ce7 usage
	}

	toncset((u32*)0x027C0000, 0, 0x400);

	// File containing cached patch offsets
	aFile patchOffsetCacheFile = getFileFromCluster(patchOffsetCacheFileCluster);
	fileRead((char*)&patchOffsetCache, patchOffsetCacheFile, 0, sizeof(patchOffsetCacheContents), -1);
	u16 prevPatchOffsetCacheFileVersion = patchOffsetCache.ver;

	int errorCode;

	tDSiHeader dsiHeaderTemp;

	// Load the NDS file
	nocashMessage("Loading the NDS file...\n");

	//bool dsiModeConfirmed;
	loadBinary_ARM7(&dsiHeaderTemp, *romFile);
	bool isDSiWare = false;
	if (ROMisDsiExclusive(&dsiHeaderTemp.ndshdr) && (dsiHeaderTemp.access_control & BIT(4))) {
		dsiModeConfirmed = true;
		isDSiWare = true;
	} else if (dsiMode == 2) {
		dsiModeConfirmed = dsiMode;
	} else {
		dsiModeConfirmed = dsiMode && ROMsupportsDsiMode(&dsiHeaderTemp.ndshdr);
	}
	if (dsiModeConfirmed) {
		if (consoleModel == 0 && !isDSiWare) {
			nocashMessage("Cannot use DSi mode on DSi");
			dbg_printf("Cannot use DSi mode on DSi");
			dbg_printf("\n");
			errorOutput();
		}
		loadIBinary_ARM7(&dsiHeaderTemp, *romFile);
	}
	toncset((u32*)0x02800000, 0, 0x500000);	// clear buffered binaries

	nocashMessage("Loading the header...\n");

	bool foundModuleParams;
	module_params_t* moduleParams = loadModuleParams(&dsiHeaderTemp.ndshdr, &foundModuleParams);
    dbg_printf("sdk_version: ");
    dbg_hexa(moduleParams->sdk_version);
    dbg_printf("\n"); 

	ensureBinaryDecompressed(&dsiHeaderTemp.ndshdr, moduleParams, foundModuleParams);
	if (decrypt_arm9(&dsiHeaderTemp)) {
		nocashMessage("Secure area decrypted successfully");
		dbg_printf("Secure area decrypted successfully");
	} else {
		nocashMessage("Secure area already decrypted");
		dbg_printf("Secure area already decrypted");
	}
	dbg_printf("\n");

	vu32* arm9StartAddress = storeArm9StartAddress(&dsiHeaderTemp.ndshdr, moduleParams);
	ndsHeader = loadHeader(&dsiHeaderTemp, moduleParams, dsiModeConfirmed);

	my_readUserSettings(ndsHeader); // Header has to be loaded first

	if (isDSiWare) {
		if (ndsHeader->reserved1[7] & BIT(1)) {
			nocashMessage("DSiWare is modcrypted");
			dbg_printf("DSiWare is modcrypted");
			dbg_printf("\n");
			errorOutput();
		}
	} else {
		if (!dsiModeConfirmed || !ROMsupportsDsiMode(&dsiHeaderTemp.ndshdr)) {
			*(u16*)0x4004700 = (soundFreq ? 0xC00F : 0x800F);
			NDSTouchscreenMode();
			*(u16*)0x4000500 = 0x807F;
		}

		// If possible, set to load ROM into RAM
		u32 ROMinRAM = isROMLoadableInRAM(&dsiHeaderTemp.ndshdr, moduleParams, consoleModel);

		const char* romTid = getRomTid(ndsHeader);
		if (!dsiModeConfirmed) {
			if (
				strncmp(romTid, "APD", 3) != 0				// Pokemon Dash
			) {
				NTR_BIOS();
			}
		}

		nocashMessage("Trying to patch the card...\n");

		if (!dsiSD) {
			ce7Location = CARDENGINE_ARM7_LOCATION_ALT;
		}

		tonccpy((u32*)ce7Location, (u32*)CARDENGINE_ARM7_BUFFERED_LOCATION, 0x12000);
		if (fcInited) {
		  if (gameOnFlashcard || saveOnFlashcard) {
			if (!dldiPatchBinary((data_t*)ce7Location, 0x12000)) {
				nocashMessage("ce7 DLDI patch failed");
				dbg_printf("ce7 DLDI patch failed");
				dbg_printf("\n");
				errorOutput();
			}
		  }
		}

		if (isSdk5(moduleParams)) {
			if (gameOnFlashcard && !ROMinRAM) {
				ce9Location = CARDENGINE_ARM9_SDK5_DLDI_LOCATION;
				tonccpy((u32*)CARDENGINE_ARM9_SDK5_DLDI_LOCATION, (u32*)CARDENGINE_ARM9_SDK5_DLDI_BUFFERED_LOCATION, 0x7000);
				if (!dldiPatchBinary((data_t*)ce9Location, 0x7000)) {
					nocashMessage("ce9 DLDI patch failed");
					dbg_printf("ce9 DLDI patch failed");
					dbg_printf("\n");
					errorOutput();
				}
			} else {
				ce9Location = CARDENGINE_ARM9_SDK5_LOCATION;
				tonccpy((u32*)CARDENGINE_ARM9_SDK5_LOCATION, (u32*)CARDENGINE_ARM9_SDK5_BUFFERED_LOCATION, 0x2000);
			}
		} else if (gameOnFlashcard && !ROMinRAM) {
			ce9Location = CARDENGINE_ARM9_DLDI_LOCATION;
			tonccpy((u32*)CARDENGINE_ARM9_DLDI_LOCATION, (u32*)CARDENGINE_ARM9_DLDI_BUFFERED_LOCATION, 0x4000);
			if (!dldiPatchBinary((data_t*)ce9Location, 0x4000)) {
				nocashMessage("ce9 DLDI patch failed");
				dbg_printf("ce9 DLDI patch failed");
				dbg_printf("\n");
				errorOutput();
			}
		} else if (ceCached) {
			if (strncmp(romTid, "ACV", 3) == 0				// Castlevania DOS
			 || strncmp(romTid, "A2L", 3) == 0				// Anno 1701: Dawn of Discovery
			)
			{
				ce9Location = CARDENGINE_ARM9_CACHED_LOCATION;
				tonccpy((u32*)ce9Location, (u32*)CARDENGINE_ARM9_RELOC_BUFFERED_LOCATION, 0x1800);
				relocate_ce9(CARDENGINE_ARM9_LOCATION,ce9Location,0x1800);
			} else
			ce9Location = (u32)patchHeapPointer(moduleParams, ndsHeader);
			if(ce9Location) {
					tonccpy((u32*)ce9Location, (u32*)CARDENGINE_ARM9_RELOC_BUFFERED_LOCATION, 0x1800);
					relocate_ce9(CARDENGINE_ARM9_LOCATION,ce9Location,0x1800);
			} else {         
				ce9Location = CARDENGINE_ARM9_LOCATION;
				tonccpy((u32*)CARDENGINE_ARM9_LOCATION, (u32*)CARDENGINE_ARM9_BUFFERED_LOCATION, 0x1800);
			}
		} else {
			ce9Location = CARDENGINE_ARM9_LOCATION;
			tonccpy((u32*)CARDENGINE_ARM9_LOCATION, (u32*)CARDENGINE_ARM9_BUFFERED_LOCATION, 0x1800);
		}

		patchBinary(ndsHeader);
		errorCode = patchCardNds(
			(cardengineArm7*)ce7Location,
			(cardengineArm9*)ce9Location,
			ndsHeader,
			moduleParams,
			patchMpuRegion,
			patchMpuSize,
			ROMinRAM,
			saveFileCluster,
			saveSize
		);
		if (errorCode == ERR_NONE) {
			nocashMessage("Card patch successful");
		} else {
			nocashMessage("Card patch failed");
			errorOutput();
		}

		cheatPatch((cardengineArm7*)ce7Location, ndsHeader);
		errorCode = hookNdsRetailArm7(
			(cardengineArm7*)ce7Location,
			ndsHeader,
			moduleParams,
			romFile->firstCluster,
			wideCheatFileCluster,
			wideCheatSize,
			cheatFileCluster,
			cheatSize,
			gameOnFlashcard,
			saveOnFlashcard,
			language,
			dsiModeConfirmed,
			dsiSD,
			ROMinRAM,
			consoleModel,
			romread_LED,
			gameSoftReset,
			preciseVolumeControl
		);
		if (errorCode == ERR_NONE) {
			nocashMessage("Card hook successful");
		} else {
			nocashMessage("Card hook failed");
			errorOutput();
		}

		hookNdsRetailArm9(
			(cardengineArm9*)ce9Location,
			moduleParams,
			romFile->firstCluster,
			savFile->firstCluster,
			saveOnFlashcard,
			fcInited,
			ROMinRAM,
			dsiModeConfirmed,
			supportsExceptionHandler(ndsHeader),
			consoleModel
		);

		if (prevPatchOffsetCacheFileVersion != patchOffsetCacheFileVersion || patchOffsetCacheChanged) {
			fileWrite((char*)&patchOffsetCache, patchOffsetCacheFile, 0, sizeof(patchOffsetCacheContents), -1);
		}

		if (ROMinRAM) {
			loadROMintoRAM(ndsHeader, moduleParams, *romFile);
		} else {
			if (strncmp(romTid, "UBR", 3) != 0) {
				loadOverlaysintoRAM(ndsHeader, moduleParams, *romFile);
			}
			if (romread_LED > 0) {
				// Turn WiFi LED off
				i2cWriteRegister(0x4A, 0x30, 0x12);
			}
		}

		aFile apPatchFile = getFileFromCluster(apPatchFileCluster);
		if (apPatchFile.firstCluster != CLUSTER_FREE && apPatchSize <= 0x40000) {
			fileRead((char*)IMAGES_LOCATION, apPatchFile, 0, apPatchSize, 0);
			applyIpsPatch(ndsHeader, (u8*)IMAGES_LOCATION, (isSdk5(moduleParams) || dsiModeConfirmed), consoleModel);
		}
	}

	toncset((u32*)CARDENGINE_ARM7_BUFFERED_LOCATION, 0, 0x23000);

    


	arm9_boostVram = boostVram;

    /*if (isGSDD) {
	   *(vu32*)REG_MBK1 = 0x8185898C; // WRAM-A slot 0 mapped to ARM9
	}*/

	if (!dsiModeConfirmed && !isDSiWare) {
		REG_SCFG_EXT &= ~(1UL << 31); // Lock SCFG
	}

	toncset((u32*)IMAGES_LOCATION, 0, 0x40000);	// clear nds-bootstrap images and IPS patch
	clearScreen();

	while (arm9_stateFlag != ARM9_READY);
	arm9_stateFlag = ARM9_SETSCFG;
	while (arm9_stateFlag != ARM9_READY);

	nocashMessage("Starting the NDS file...");
    setMemoryAddress(ndsHeader, moduleParams, isDSiWare);
	startBinary_ARM7(arm9StartAddress);

	return 0;
}
