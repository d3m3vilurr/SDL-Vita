/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

#include <psp2/touch.h>

#include "SDL_timer.h"
#include "SDL_mouse.h"
#include "../../events/SDL_events_c.h"
#include "../../events/SDL_sysevents.h"

#include "SDL_psp2video.h"
#include "SDL_psp2touch_c.h"

#define TOUCH_ACTION_TIMEOUT 100
#define TOUCH_TAP_TIMEOUT 10

typedef enum TouchActionState {
	NONE,
	TOUCH,
	TAP,
	HOLD,
} TouchActionState ;

typedef struct Point {
	int16_t x;
	int16_t y;
} Point;

typedef struct TouchData {
	int finger;
	Point points[SCE_TOUCH_MAX_REPORT];
} TouchData;

TouchData old_front = {0};
Point prev_points[SCE_TOUCH_MAX_REPORT];
TouchActionState front_touch_state = NONE;
uint8_t curr_finger = 0;
Uint32 current_tick, timeout;

void
PSP2_InitTouch(void)
{
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
}

inline void
PSP2_EmulateMouseAction(uint8_t button, uint8_t press)
{
	SDL_PrivateMouseButton(press ? SDL_PRESSED : SDL_RELEASED, button, 0, 0);
}

inline void
PSP2_ReadTouchDevice(uint32_t port, TouchData *out)
{
	SceTouchData raw;
	sceTouchPeek(port, &raw, 1);

	memset(out, 0, sizeof(TouchData));
	out->finger = raw.reportNum;
	for (int i = 0; i < raw.reportNum; i++) {
		out->points[i].x = raw.report[i].x;
		out->points[i].y = raw.report[i].y;
	}
}

inline void
PSP2_TouchPointDelta(Point *out, Point *olds, Point *news, uint8_t len)
{
	int old_x = 0;
	int old_y = 0;
	int new_x = 0;
	int new_y = 0;
	for (int i = 0; i < len; i++) {
		old_x += olds[i].x;
		old_y += olds[i].y;
		new_x += news[i].x;
		new_y += news[i].y;
	}
	old_x = old_x / len;
	old_y = old_y / len;
	new_x = new_x / len;
	new_y = new_y / len;
	out->x = (new_x - old_x) / 2;
	out->y = (new_y - old_y) / 2;
}

inline void
PSP2_HandleFrontTouchScreen(TouchData *data)
{
	switch (front_touch_state) {
		case NONE:
			if (data->finger <= 0) {
				return;
			}
			front_touch_state = TOUCH;
			timeout = current_tick + TOUCH_ACTION_TIMEOUT;
			curr_finger = data->finger;
			return;
		case TOUCH:
			if (current_tick >= timeout) {
				front_touch_state = HOLD;
                memcpy(prev_points, data->points, sizeof(Point) * SCE_TOUCH_MAX_REPORT);
				return;
			}
			if (data->finger > curr_finger) {
				curr_finger = data->finger;
				return;
			}
			if (data->finger == curr_finger) {
				return;
			}
			// reduced finger count
			switch (curr_finger) {
				case 1:
					PSP2_EmulateMouseAction(SDL_BUTTON_LEFT, 1);
					break;
				case 2:
					PSP2_EmulateMouseAction(SDL_BUTTON_RIGHT, 1);
					break;
				case 3:
					PSP2_EmulateMouseAction(SDL_BUTTON_MIDDLE, 1);
					break;
			}
			front_touch_state = TAP;
			timeout = current_tick + TOUCH_TAP_TIMEOUT;
			return;
		case TAP:
			if (current_tick < timeout) {
				return;
			}
			switch (curr_finger) {
				case 1:
					PSP2_EmulateMouseAction(SDL_BUTTON_LEFT, 0);
					break;
				case 2:
					PSP2_EmulateMouseAction(SDL_BUTTON_RIGHT, 0);
					break;
				case 3:
					PSP2_EmulateMouseAction(SDL_BUTTON_MIDDLE, 0);
					break;
			}
			front_touch_state = NONE;
			return;
		case HOLD:
			if (data->finger <= 0) {
				front_touch_state = NONE;
				return;
			}
			{
				Point delta;
				PSP2_TouchPointDelta(&delta, prev_points, data->points, curr_finger);
				switch (curr_finger) {
					case 1:
						SDL_PrivateMouseMotion(0, 1, delta.x, delta.y);
						timeout = current_tick + TOUCH_ACTION_TIMEOUT;
						break;
					case 2: {
						if (delta.y == 0) {
							PSP2_EmulateMouseAction(SDL_BUTTON_WHEELUP, 0);
							PSP2_EmulateMouseAction(SDL_BUTTON_WHEELDOWN, 0);
							break;
						}
						PSP2_EmulateMouseAction(delta.y < 0 ? SDL_BUTTON_WHEELUP : SDL_BUTTON_WHEELDOWN, 1);
						break;
					}
				}
				memcpy(prev_points, data->points, sizeof(Point) * SCE_TOUCH_MAX_REPORT);
			}
			return;
	}
}

void
PSP2_PollTouch(void)
{
	TouchData front;
	PSP2_ReadTouchDevice(SCE_TOUCH_PORT_FRONT, &front);

	current_tick = SDL_GetTicks();

	if (memcmp(&front, &old_front, sizeof(TouchData)) != 0) {
		PSP2_HandleFrontTouchScreen(&front);
		memcpy(&old_front, &front, sizeof(TouchData));
	}
	// TODO backscreen
}

