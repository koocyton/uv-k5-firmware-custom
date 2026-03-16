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
	/* 中间黑色长方块，内显 waiting（白字）；方块上移 3 像素 */
	const int16_t x1 = 16, y1 = 15, x2 = 111, y2 = 34;
	UI_FillRectangleBuffer(gFrameBuffer, x1, y1, x2, y2, true);
	UI_PrintStringInverted("waiting", (uint8_t)x1, (uint8_t)(x2 + 1), 2, 8);
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
		/* 底部倒数第二行(line 5)：LNA/BW/STP 右移 4px，选中时反色块包住文字 */
		{
			const uint8_t focus = FM_GetAM_OptionFocus();
			#define AM_OPT_X0  4
			#define AM_OPT_X1 46
			#define AM_OPT_X2 88
			#define AM_OPT_W  36
			UI_PrintStringSmallNormal("LNA", AM_OPT_X0, 0, 5);
			UI_PrintStringSmallNormal("BW", AM_OPT_X1, 0, 5);
			UI_PrintStringSmallNormal("STP", AM_OPT_X2, 0, 5);
			if (focus == 0) UI_InvertRectangleBuffer(gFrameBuffer, AM_OPT_X0, 40, AM_OPT_X0 + AM_OPT_W - 1, 47);
			else if (focus == 1) UI_InvertRectangleBuffer(gFrameBuffer, AM_OPT_X1, 40, AM_OPT_X1 + AM_OPT_W - 1, 47);
			else UI_InvertRectangleBuffer(gFrameBuffer, AM_OPT_X2, 40, AM_OPT_X2 + AM_OPT_W - 1, 47);
			#undef AM_OPT_X0
			#undef AM_OPT_X1
			#undef AM_OPT_X2
			#undef AM_OPT_W
		}
		/* 底行(line 6)左侧：当前焦点对应的子选项值；右侧：RSSI/SNR */
		{
			char valStr[12];
			const uint8_t focus = FM_GetAM_OptionFocus();
			const uint8_t lna = FM_GetAM_LNA_Index();
			const uint8_t bw = FM_GetAM_BW_Index();
			static const char * const lnaVals[] = { "AGC ON", "ATT 0", "ATT 1", "ATT 5", "ATT 15", "ATT 26" };
			static const char * const bwVals[] = { "0.5", "1.0", "1.2", "2.2", "3.0", "4.0", "5.0" };
			if (focus == 0) sprintf(valStr, "%s", lna < 6u ? lnaVals[lna] : "AGC ON");
			else if (focus == 1) sprintf(valStr, "%sk", bw < 7u ? bwVals[bw] : "1.2");
			else { const uint16_t s = FM_GetAM_StepKHz(); if (s >= 1000) sprintf(valStr, "%uk", (unsigned)(s/1000)); else sprintf(valStr, "%u", (unsigned)s); }
			UI_PrintStringSmallNormal(valStr, 0, 0, 6);
		}
		if (gInputBoxIndex == 0) {
			RSQ_GET();
			sprintf(String, "%u/%u", (unsigned)rsqStatus.resp.RSSI, (unsigned)rsqStatus.resp.SNR);
			UI_PrintStringSmallNormal(String, 88, 0, 6);
		}
	} else
#endif
	{
		UI_PrintString("FM", 2, 0, 0, 8);
		sprintf(String, "%d%s-%dM",
			BK1080_GetFreqLoLimit(gEeprom.FM_Band)/10,
			gEeprom.FM_Band == 0 ? ".5" : "",
			BK1080_GetFreqHiLimit(gEeprom.FM_Band)/10
		);
		UI_PrintStringSmallNormal(String, 1, 0, 6);
	}

#ifdef ENABLE_FM_SI4732
	/* FM 时 RSSI/SNR 在 line 6 右侧 */
	if (!SI47XX_IsAMFamily() && gInputBoxIndex == 0) {
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
