#include <fat.h>
#include <wiiuse/wpad.h>
#include <unistd.h>
#include <sys/iosupport.h>

extern const devoptab_t dotab_stdnull;

static u8 wiiButton = 0;

void WiiReset() { wiiButton = SYS_RETURNTOMENU; }
void WiiPower() { wiiButton = SYS_POWEROFF_STANDBY; }
void WiiPowerButton(s32 chan) { wiiButton = SYS_POWEROFF_STANDBY; }

void InitWii()
{
	// disable console
	//devoptab_list[STD_OUT] = &dotab_stdnull;
	//devoptab_list[STD_ERR] = &dotab_stdnull;
	
	SYS_SetResetCallback(WiiReset);
	SYS_SetPowerCallback(WiiPower);
	WPAD_SetPowerButtonCallback(WiiPowerButton);
	fatInitDefault();
	
	//lwp_t wiieventthread = LWP_THREAD_NULL;
	//LWP_CreateThread (&wiieventthread, CheckWiiEvents, NULL, NULL, 0, 78);
}
