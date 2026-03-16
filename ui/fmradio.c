/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifdef ENABLE_FMRADIO

#include <string.h>

#include "app/fm.h"
#include "driver/bk1080.h"
#ifdef ENABLE_FM_SI4732
#include "driver/si473x.h"
#endif
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "settings.h"
#include "ui/fmradio.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#ifdef ENABLE_FM_SI4732
void UI_DisplayFmWait(void)
{
	/* 中间黑色长方块，内显 wait（白字，小写，方块与文字均居中） */
	const int16_t x1 = 40, y1 = 18, x2 = 87, y2 = 37;
	UI_FillRectangleBuffer(gFrameBuffer, x1, y1, x2, y2, true);
	UI_PrintStringInverted("wait", (uint8_t)x1, (uint8_t)(x2 + 1), 3, 8);
}
#endif

void UI_DisplayFM(void)
{
	char String[16] = {0};
	char *pPrintStr = String;
	UI_DisplayClear();

#ifdef ENABLE_FM_SI4732
	if (SI47XX_IsAMFamily()) {
		const char *mod = (si4732mode == SI47XX_AM) ? "AM" : (si4732mode == SI47XX_LSB) ? "LSB" : (si4732mode == SI47XX_USB) ? "USB" : "CW";
		UI_PrintString(mod, 2, 0, 0, 8);
		sprintf(String, "0.5-30M %uk", (unsigned)FM_GetAM_StepKHz());
	} else
#endif
	{
		UI_PrintString("FM", 2, 0, 0, 8);
		sprintf(String, "%d%s-%dM",
			BK1080_GetFreqLoLimit(gEeprom.FM_Band)/10,
			gEeprom.FM_Band == 0 ? ".5" : "",
			BK1080_GetFreqHiLimit(gEeprom.FM_Band)/10
		);
	}
	UI_PrintStringSmallNormal(String, 1, 0, 6);

#ifdef ENABLE_FM_SI4732
	/* RSSI/SNR on same line as step (line 6), far right; format "RSSI/SNR" */
	if (gInputBoxIndex == 0) {
		RSQ_GET();
		sprintf(String, "%u/%u", (unsigned)rsqStatus.resp.RSSI, (unsigned)rsqStatus.resp.SNR);
		UI_PrintStringSmallNormal(String, 88, 0, 6);
	}
#endif

	//uint8_t spacings[] = {20,10,5};
	//sprintf(String, "%d0k", spacings[gEeprom.FM_Space % 3]);
	//UI_PrintStringSmallNormal(String, 127 - 4*7, 0, 6);

	if (gAskToSave) {
		pPrintStr = "SAVE?";
	} else if (gAskToDelete) {
		pPrintStr = "DEL?";
	} else if (gFM_ScanState == FM_SCAN_OFF) {
#ifdef ENABLE_FM_SI4732
		if (SI47XX_IsAMFamily()) {
			pPrintStr = ""; /* AM/LSB/USB/CW: hide VFO label */
		} else
#endif
		{
			if (gEeprom.FM_IsMrMode) {
				sprintf(String, "MR(CH%02u)", gEeprom.FM_SelectedChannel + 1);
				pPrintStr = String;
			} else {
				pPrintStr = "VFO";
				for (unsigned int i = 0; i < 20; i++) {
					if (gEeprom.FM_FrequencyPlaying == gFM_Channels[i]) {
						sprintf(String, "VFO(CH%02u)", i + 1);
						pPrintStr = String;
						break;
					}
				}
			}
		}
	} else if (gFM_AutoScan) {
		sprintf(String, "A-SCAN(%u)", gFM_ChannelPosition + 1);
		pPrintStr = String;
	} else {
		pPrintStr = "M-SCAN";
	}

	UI_PrintString(pPrintStr, 0, 127, 3, 10); // memory, vfo, scan

	memset(String, 0, sizeof(String));
	if (gAskToSave || (gEeprom.FM_IsMrMode && gInputBoxIndex > 0)) {
		UI_GenerateChannelString(String, gFM_ChannelPosition);
	} else if (gAskToDelete) {
		sprintf(String, "CH-%02u", gEeprom.FM_SelectedChannel + 1);
	} else {
		if (gInputBoxIndex == 0) {
#ifdef ENABLE_FM_SI4732
			if (SI47XX_IsAMFamily())
				sprintf(String, "%u.%03u", (unsigned)(siCurrentFreq / 1000), (unsigned)(siCurrentFreq % 1000));
			else
#endif
				sprintf(String, "%3d.%d", gEeprom.FM_FrequencyPlaying / 10, gEeprom.FM_FrequencyPlaying % 10);
		} else {
			const char * ascii = INPUTBOX_GetAscii();
#ifdef ENABLE_FM_SI4732
			if (SI47XX_IsAMFamily())
				sprintf(String, "%.*sk", (int)gInputBoxIndex, ascii);
			else
#endif
				sprintf(String, "%.3s.%.1s", ascii, ascii + 3);
		}

		UI_DisplayFrequency(String, 36, 1, gInputBoxIndex == 0);  // frequency
		ST7565_BlitFullScreen();
		return;
	}

	UI_PrintString(String, 0, 127, 1, 10);

	ST7565_BlitFullScreen();
}

#endif
