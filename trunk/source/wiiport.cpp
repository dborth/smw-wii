#include <fat.h>
#include <wiiuse/wpad.h>
#include <unistd.h>
#include <sys/iosupport.h>

#include "SDL.h"
#include "global.h"

extern short controlkeys[2][2][4][NUM_KEYS];
extern const devoptab_t dotab_stdnull;

static u8 wiiButton = 0;
static lwp_t wiieventthread = LWP_THREAD_NULL;
static void WiiReset() { wiiButton = SYS_RETURNTOMENU; }
static void WiiPower() { wiiButton = SYS_POWEROFF_STANDBY; }
static void WiiPowerButton(s32 chan) { wiiButton = SYS_POWEROFF_STANDBY; }

/*
GAME - left, right, jump, down, turbo, powerup, start, cancel
{SDLK_LEFT, SDLK_RIGHT, SDLK_UP, SDLK_DOWN, SDLK_RCTRL, SDLK_RSHIFT, SDLK_RETURN, SDLK_ESCAPE},
{SDLK_a, SDLK_d, SDLK_w, SDLK_s, SDLK_e, SDLK_q, SDLK_UNKNOWN, SDLK_UNKNOWN},
{SDLK_g, SDLK_j, SDLK_y, SDLK_h, SDLK_u, SDLK_t, SDLK_UNKNOWN, SDLK_UNKNOWN},
{SDLK_l, SDLK_QUOTE, SDLK_p, SDLK_SEMICOLON, SDLK_LEFTBRACKET, SDLK_o, SDLK_UNKNOWN, SDLK_UNKNOWN}

MENU - up, down, left, right, select, cancel, random, fast scroll
{SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_RETURN, SDLK_ESCAPE, SDLK_SPACE,  SDLK_LSHIFT},
{SDLK_w, SDLK_s, SDLK_a, SDLK_d, SDLK_e, SDLK_q, SDLK_UNKNOWN, SDLK_UNKNOWN},
{SDLK_y, SDLK_h, SDLK_g, SDLK_j, SDLK_u, SDLK_t, SDLK_UNKNOWN, SDLK_UNKNOWN},
{SDLK_p, SDLK_SEMICOLON, SDLK_l, SDLK_QUOTE, SDLK_LEFTBRACKET, SDLK_o, SDLK_UNKNOWN, SDLK_UNKNOWN}
*/

unsigned int game_map[8];
unsigned int menu_map[8];

static void * CheckWiiEvents(void *arg)
{
	WPADData wpad;
	SDL_Event event[32];
	int i, j, k;
	u32 exp_type = 0;

	while(1)
	{
		WPAD_ScanPads();
		memset(&event, 0, sizeof(SDL_Event)*32);
		for(i=0; i < 4; i++)
		{
			if (WPAD_Probe(i, &exp_type) != 0)
				exp_type = WPAD_EXP_NONE;

			if(exp_type == WPAD_EXP_CLASSIC)
			{
				k=0;
				game_map[k++] = WPAD_CLASSIC_BUTTON_LEFT;
				game_map[k++] = WPAD_CLASSIC_BUTTON_RIGHT;
				game_map[k++] = WPAD_CLASSIC_BUTTON_B;
				game_map[k++] = WPAD_CLASSIC_BUTTON_DOWN;
				game_map[k++] = WPAD_CLASSIC_BUTTON_Y;
				game_map[k++] = WPAD_CLASSIC_BUTTON_A;
				game_map[k++] = WPAD_CLASSIC_BUTTON_PLUS;
				game_map[k++] = WPAD_CLASSIC_BUTTON_MINUS;
				k=0;
				menu_map[k++] = WPAD_CLASSIC_BUTTON_UP;
				menu_map[k++] = WPAD_CLASSIC_BUTTON_DOWN;
				menu_map[k++] = WPAD_CLASSIC_BUTTON_LEFT;
				menu_map[k++] = WPAD_CLASSIC_BUTTON_RIGHT;
				menu_map[k++] = WPAD_CLASSIC_BUTTON_Y;
				menu_map[k++] = WPAD_CLASSIC_BUTTON_B;
				menu_map[k++] = 0;
				menu_map[k++] = 0;
			}
			else
			{
				k=0;
				game_map[k++] = WPAD_BUTTON_UP;
				game_map[k++] = WPAD_BUTTON_DOWN;
				game_map[k++] = WPAD_BUTTON_2;
				game_map[k++] = WPAD_BUTTON_LEFT;
				game_map[k++] = WPAD_BUTTON_1;
				game_map[k++] = WPAD_BUTTON_A;
				game_map[k++] = WPAD_BUTTON_PLUS;
				game_map[k++] = WPAD_BUTTON_MINUS;
				k=0;
				menu_map[k++] = WPAD_BUTTON_RIGHT;
				menu_map[k++] = WPAD_BUTTON_LEFT;
				menu_map[k++] = WPAD_BUTTON_UP;
				menu_map[k++] = WPAD_BUTTON_DOWN;
				menu_map[k++] = WPAD_BUTTON_2;
				menu_map[k++] = WPAD_BUTTON_1;
				menu_map[k++] = 0;
				menu_map[k++] = 0;
			}

			memcpy(&wpad, WPAD_Data(i), sizeof(WPADData));

			for(j=0; j < 8; j++)
			{
				if(game_values.gamestate == GS_GAME)
				{
					if(wpad.btns_d & game_map[j])
					{
						event[(j+8*i)].type = SDL_KEYDOWN;
						event[(j+8*i)].key.keysym.sym = (SDLKey)controlkeys[0][0][i][j];
						SDL_PushEvent(&event[(j+8*i)]);
					}
					else if(wpad.btns_u & game_map[j])
					{
						event[(j+8*i)].type = SDL_KEYUP;
						event[(j+8*i)].key.keysym.sym = (SDLKey)controlkeys[0][0][i][j];
						SDL_PushEvent(&event[(j+8*i)]);
					}
				}
				else
				{
					if(wpad.btns_d & menu_map[j])
					{
						event[(j+8*i)].type = SDL_KEYDOWN;
						event[(j+8*i)].key.keysym.sym = (SDLKey)controlkeys[0][1][i][j];
						SDL_PushEvent(&event[(j+8*i)]);
					}
					else if(wpad.btns_u & menu_map[j])
					{
						event[(j+8*i)].type = SDL_KEYUP;
						event[(j+8*i)].key.keysym.sym = (SDLKey)controlkeys[0][1][i][j];
						SDL_PushEvent(&event[(j+8*i)]);
					}
				}
			}

			if(wpad.btns_d & WPAD_BUTTON_HOME)
			{
				event[0].type = SDL_QUIT;
				SDL_PushEvent(&event[0]);
			}
		}
		usleep(5000);
	}
	return NULL;
}

void InitWii()
{
	// disable console
	devoptab_list[STD_OUT] = &dotab_stdnull;
	devoptab_list[STD_ERR] = &dotab_stdnull;
	
	SYS_SetResetCallback(WiiReset);
	SYS_SetPowerCallback(WiiPower);
	WPAD_SetPowerButtonCallback(WiiPowerButton);
	fatInitDefault();
	LWP_CreateThread (&wiieventthread, CheckWiiEvents, NULL, NULL, 0, 78);
}
