#include <nds/ndstypes.h>
#include <nds/ipc.h>
#include <nds/interrupts.h>
#include <nds/input.h>
#include <nds/arm7/audio.h>

#include "locations.h"
#include "cardengine.h"
#include "nds_header.h"
#include "tonccpy.h"

#define	REG_EXTKEYINPUT	(*(vuint16*)0x04000136)

extern vu32* volatile sharedAddr;
extern bool returnToMenu;

extern void rebootConsole(void);

void inGameMenu(void) {
	sharedAddr[4] = 'MENU';
	IPC_SendSync(0x9);
	int volBak = REG_MASTER_VOLUME;
	REG_MASTER_VOLUME = 0;
	int oldIME = enterCriticalSection();

	int timeOut = 0;
	while (sharedAddr[5] != 'REDY') {
		while (REG_VCOUNT != 191) swiDelay(100);
		while (REG_VCOUNT == 191) swiDelay(100);

		timeOut++;
		if (timeOut == 60*2) {
			rebootConsole();
			timeOut = 0;
		}
	}

	if (sharedAddr[4] == 'MENU') {
		bool exitMenu = false;
		while (!exitMenu) {
			sharedAddr[5] = ~REG_KEYINPUT & 0x3FF;
			sharedAddr[5] |= ((~REG_EXTKEYINPUT & 0x3) << 10) | ((~REG_EXTKEYINPUT & 0xC0) << 6);

			while (REG_VCOUNT != 191) swiDelay(100);
			while (REG_VCOUNT == 191) swiDelay(100);

			switch (sharedAddr[4]) {
				case 'EXIT':
					exitMenu = true;
					break;
				case 'QUIT':
					rebootConsole();
					exitMenu = true;
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

			sharedAddr[4] = 'MENU';
		}
	}

	sharedAddr[4] = 'EXIT';

	leaveCriticalSection(oldIME);
	REG_MASTER_VOLUME = volBak;
}
