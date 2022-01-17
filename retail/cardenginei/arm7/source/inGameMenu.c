#include <nds/ndstypes.h>
#include <nds/ipc.h>
#include <nds/interrupts.h>
#include <nds/input.h>
#include <nds/arm7/audio.h>
#include <nds/arm7/i2c.h>

#include "igm_text.h"
#include "locations.h"
#include "cardengine.h"
#include "nds_header.h"
#include "tonccpy.h"

#define REG_EXTKEYINPUT (*(vuint16*)0x04000136)

extern vu32* volatile sharedAddr;
extern bool returnToMenu;

extern struct IgmText *igmText;

extern void reset(void);
extern void dumpRam(void);
extern void returnToLoader(void);
extern void prepareScreenshot(void);
extern void saveScreenshot(void);

volatile int timeTilBatteryLevelRefresh = 7;

void inGameMenu(void) {
	returnToMenu = false;
	sharedAddr[4] = 'MENU';
	IPC_SendSync(0x9);
	REG_MASTER_VOLUME = 0;
	int oldIME = enterCriticalSection();

	int timeOut = 0;
	while (sharedAddr[5] != 'REDY') {
		while (REG_VCOUNT != 191) swiDelay(100);
		while (REG_VCOUNT == 191) swiDelay(100);

		timeOut++;
		if (timeOut == 60*2) {
			returnToLoader();
			timeOut = 0;
		}
	}

	if (sharedAddr[4] == 'MENU') {
		bool exitMenu = false;
		while (!exitMenu) {
			sharedAddr[5] = ~REG_KEYINPUT & 0x3FF;
			sharedAddr[5] |= ((~REG_EXTKEYINPUT & 0x3) << 10) | ((~REG_EXTKEYINPUT & 0xC0) << 6);
			timeTilBatteryLevelRefresh++;
			if (timeTilBatteryLevelRefresh >= 8) {
				sharedAddr[6] = i2cReadRegister(I2C_PM, I2CREGPM_BATTERY);
				timeTilBatteryLevelRefresh = 0;
			}

			while (REG_VCOUNT != 191) swiDelay(100);
			while (REG_VCOUNT == 191) swiDelay(100);

			switch (sharedAddr[4]) {
				case 'EXIT':
					exitMenu = true;
					break;
				case 'RSET':
					exitMenu = true;
					timeTilBatteryLevelRefresh = 7;
					#ifdef TWLSDK
					extern void restoreBakData(void);
					restoreBakData();
					#endif
					reset();
					break;
				case 'QUIT':
					returnToLoader();
					exitMenu = true;
					break;
				case 'RAMD':
					dumpRam();
					exitMenu = true;
					break;
				case 'STEP':
					returnToMenu = true;
					exitMenu = true;
					break;
				case 'SSPP':
					#ifdef TWLSDK
					prepareScreenshot();
					#endif
					break;
				case 'SHOT':
					saveScreenshot();
					break;
				case 'RAMR':
					tonccpy((u32*)((u32)sharedAddr[0]), (u32*)((u32)sharedAddr[1]), 0xC0);
					break;
				case 'RAMW':
					tonccpy((u8*)((u32)sharedAddr[1])+sharedAddr[2], (u8*)((u32)sharedAddr[0])+sharedAddr[2], 1);
					break;
				default:
					break;
			}

			sharedAddr[4] = 'MENU'; // MENU
		}
	}

	sharedAddr[4] = 'EXIT'; // EXIT
	timeTilBatteryLevelRefresh = 7;

	leaveCriticalSection(oldIME);
	REG_MASTER_VOLUME = 127;
}
