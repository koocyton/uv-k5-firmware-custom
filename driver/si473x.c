#include "si473x.h"
#include "audio.h"
#include "settings.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/system.h"
#include "driver/systick.h"

/* I2C 7-bit: 0x11 (A0 low, default); 0x63 (A0 high). Override with -DSI47XX_I2C_ADDR_7BIT=0x63 if needed. */
#ifndef SI47XX_I2C_ADDR_7BIT
#define SI47XX_I2C_ADDR_7BIT 0x11U
#endif
static const uint8_t SI47XX_I2C_ADDR = (SI47XX_I2C_ADDR_7BIT << 1) | 0U;

#define RST_HIGH GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BK1080)
#define RST_LOW GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_BK1080)

RSQStatus rsqStatus;
uint16_t divider = 1000;

SI47XX_MODE si4732mode = SI47XX_FM;
uint16_t siCurrentFreq = 10210;

void SI47XX_ReadBuffer(uint8_t *buf, uint8_t size) {
  I2C_Start();
  I2C_Write(SI47XX_I2C_ADDR + 1);
  I2C_ReadBuffer(buf, size);
  I2C_Stop();
}

void SI47XX_WriteBuffer(uint8_t *buf, uint8_t size) {
  I2C_Start();
  I2C_Write(SI47XX_I2C_ADDR);
  I2C_WriteBuffer(buf, size);
  I2C_Stop();
}

void waitToSend() {
  uint8_t tmp = 0;
  SI47XX_ReadBuffer((uint8_t *)&tmp, 1);
  while (!(tmp & STATUS_CTS)) {
    SYSTICK_DelayUs(1);
    SI47XX_ReadBuffer((uint8_t *)&tmp, 1);
  }
}


static void sendProperty(uint16_t prop, uint16_t parameter) {
  waitToSend();
  uint8_t tmp[6] = {CMD_SET_PROPERTY, 0, (uint8_t)(prop >> 8), (uint8_t)(prop & 0xff),
                    (uint8_t)(parameter >> 8), (uint8_t)(parameter & 0xff)};
  SI47XX_WriteBuffer(tmp, 6);
  SYSTEM_DelayMs(2);
}

void RSQ_GET() {
  uint8_t cmd[2] = {CMD_FM_RSQ_STATUS, 0x01};
  if (si4732mode != SI47XX_FM) {
    cmd[0] = CMD_AM_RSQ_STATUS;
  }

  waitToSend();
  SI47XX_WriteBuffer(cmd, 2);
  SI47XX_ReadBuffer(rsqStatus.raw, si4732mode == SI47XX_FM ? 8 : 6);
}

void setVolume(uint8_t volume) {
  if (volume > 63)
    volume = 63;
  sendProperty(PROP_RX_VOLUME, volume);
}

/* AN332: RX_HARD_MUTE is command 0x40; ARG1=0, ARG2=0 unmute / 3 mute both */
void SI47XX_Mute(bool mute) {
  waitToSend();
  uint8_t cmd[3] = {0x40, 0x00, mute ? 0x03U : 0x00U};
  SI47XX_WriteBuffer(cmd, 3);
}

/* K5 does not use RDS; enableRDS is no-op to save flash. */
static void enableRDS(void) {
  (void)0;
}

void SI47XX_SetAutomaticGainControl(uint8_t AGCDIS, uint8_t AGCIDX) {
  uint8_t cmd = (si4732mode == SI47XX_FM) ? CMD_FM_AGC_OVERRIDE : CMD_AM_AGC_OVERRIDE;
  waitToSend();
  uint8_t cmd2[3] = {cmd, (uint8_t)(AGCDIS & 1U), AGCIDX};
  SI47XX_WriteBuffer(cmd2, 3);
}


static bool FreqCheck(uint32_t f) {
    if (si4732mode == SI47XX_FM)
        return f >= 6400000 && f <= 10800000;
    return f >= 500000 && f <= 30000000; /* AM 500 kHz–30 MHz */
}
/* K5 EEPROM: FM at 0x0E8C (4 bytes); AM at 0x0E68 (2 bytes, kHz) to avoid clash with 0x0E90 */
#define SI473X_EEPROM_FREQ_BASE  0x0E8CU
#define SI473X_EEPROM_AM_KHZ    0x0E68U

static uint32_t Read_FreqSaved(void)
{
    if (si4732mode == SI47XX_AM) {
        uint16_t khz;
        EEPROM_ReadBuffer(SI473X_EEPROM_AM_KHZ, (uint8_t *)&khz, 2);
        if (khz < 500 || khz > 30000)
            khz = 720;
        return (uint32_t)khz * 1000U;
    }
    uint32_t tmpF;
    EEPROM_ReadBuffer(SI473X_EEPROM_FREQ_BASE, (uint8_t *)&tmpF, 4);
    if (!FreqCheck(tmpF))
        tmpF = 10210000;
    return tmpF;
}
/* Call after caller has done reset and RST_HIGH: send POWER_UP without CTS wait, 500ms, then path/volume/RDS/SetFreq. */
void SI47XX_FirstPowerUp(uint16_t freq_10k) {
  uint8_t cmd[3] = {CMD_POWER_UP, FLG_XOSCEN | FUNC_FM, OUT_ANALOG};
  SI47XX_WriteBuffer(cmd, 3);
  SYSTEM_DelayMs(500);
  waitToSend();
#ifdef ENABLE_FM_SI4732_AUDIO_PATH_INVERTED
  AUDIO_AudioPathOn_FM();
#else
  AUDIO_AudioPathOn();
#endif
  setVolume(63);
  SI47XX_SetSeekFmLimits(8750, 10800); /* 87.5–108 MHz */
  enableRDS();
  SI47XX_SetFreq(freq_10k);
}

void SI47XX_PowerUp() {
  RST_HIGH;

  uint8_t cmd[3] = {CMD_POWER_UP, FLG_XOSCEN | FUNC_FM, OUT_ANALOG};
  if (si4732mode == SI47XX_AM) {
    cmd[1] = FLG_XOSCEN | FUNC_AM;
  }
  waitToSend();
  SI47XX_WriteBuffer(cmd, 3);
  SYSTEM_DelayMs(500);

    AUDIO_AudioPathOn();
  setVolume(63);

  if (si4732mode == SI47XX_FM) {
    enableRDS();
  } else if (si4732mode == SI47XX_AM) {
    SI47XX_SetAutomaticGainControl(1, 0);
    sendProperty(PROP_AM_SOFT_MUTE_MAX_ATTENUATION, 0);
    sendProperty(PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN, 0x7800);
    SI47XX_SetSeekAmLimits(500, 30000); /* 500 kHz–30 MHz */
  }
  SI47XX_SetFreq(  Read_FreqSaved()/divider);
}

void SI47XX_PowerDown() {
#ifdef ENABLE_FM_SI4732_AUDIO_PATH_INVERTED
  AUDIO_AudioPathOff_FM();
#else
  AUDIO_AudioPathOff();
#endif
  uint8_t cmd[1] = {CMD_POWER_DOWN};

  waitToSend();
  SI47XX_WriteBuffer(cmd, 1);
  SYSTICK_DelayUs(10);
  RST_LOW;
}

void SI47XX_SwitchMode(SI47XX_MODE mode) {
    if (si4732mode != mode) {
        si4732mode = mode;
        SI47XX_PowerDown();
        SI47XX_PowerUp();
    }
}

void SI47XX_SetFreq(uint16_t freq) {
  uint8_t hb = (freq >> 8) & 0xFF;
  uint8_t lb = freq & 0xFF;
  uint8_t size = 4;
  uint8_t cmd[6] = {CMD_FM_TUNE_FREQ, 0x01, hb, lb, 0, 0};

  if (si4732mode == SI47XX_AM) {
    cmd[0] = CMD_AM_TUNE_FREQ;
    size = 5;
    if (freq > 1800)
      cmd[5] = 1;
  }

  waitToSend();
  SI47XX_WriteBuffer(cmd, size);
  siCurrentFreq = freq;
  SYSTEM_DelayMs(30);
}

void SI47XX_SetSeekFmLimits(uint16_t bottom, uint16_t top) {
  sendProperty(PROP_FM_SEEK_BAND_BOTTOM, bottom);
  sendProperty(PROP_FM_SEEK_BAND_TOP, top);
}

void SI47XX_SetSeekAmLimits(uint16_t bottom, uint16_t top) {
  sendProperty(PROP_AM_SEEK_BAND_BOTTOM, bottom);
  sendProperty(PROP_AM_SEEK_BAND_TOP, top);
}
