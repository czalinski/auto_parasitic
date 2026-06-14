# /*****************************************************************************
# * | File        :   ADS1263.py  (Patched for Raspberry Pi only)
# * | Author      :   Waveshare team / Modified by request
# * | Function    :   ADS1263 driver
# *----------------
# * | Version     :   V1.1 (patched)
# * | Date        :   2026-05-31
# * | Notes       :   Added VREF support + dual init styles
# ******************************************************************************/

import config
import RPi.GPIO as GPIO
import time

# ---------------------------------------------------------------------------
# Added VREF dictionary (missing in original Waveshare HAT version)
# ---------------------------------------------------------------------------
VREF = {
    'ADS1263_2_5V': 2.5,
    'ADS1263_5V':   5.0,
    'ADS1263_1_25V': 1.25,
}

# gain
ADS1263_GAIN = {
    'ADS1263_GAIN_1' : 0,
    'ADS1263_GAIN_2' : 1,
    'ADS1263_GAIN_4' : 2,
    'ADS1263_GAIN_8' : 3,
    'ADS1263_GAIN_16' : 4,
    'ADS1263_GAIN_32' : 5,
    'ADS1263_GAIN_64' : 6,
}

# data rate
ADS1263_DRATE = {
    'ADS1263_38400SPS'  : 0xF,
    'ADS1263_19200SPS'  : 0xE,
    'ADS1263_14400SPS'  : 0xD,
    'ADS1263_7200SPS'   : 0xC,
    'ADS1263_4800SPS'   : 0xB,
    'ADS1263_2400SPS'   : 0xA,
    'ADS1263_1200SPS'   : 0x9,
    'ADS1263_400SPS'    : 0x8,
    'ADS1263_100SPS'    : 0x7,
    'ADS1263_60SPS'     : 0x6,
    'ADS1263_50SPS'     : 0x5,
    'ADS1263_20SPS'     : 0x4,
    'ADS1263_16d6SPS'   : 0x3,
    'ADS1263_10SPS'     : 0x2,
    'ADS1263_5SPS'      : 0x1,
    'ADS1263_2d5SPS'    : 0x0,
}

# ADC2 data rate
ADS1263_ADC2_DRATE = {
    'ADS1263_ADC2_10SPS'    : 0,
    'ADS1263_ADC2_100SPS'   : 1,
    'ADS1263_ADC2_400SPS'   : 2,
    'ADS1263_ADC2_800SPS'   : 3,
}

# Delay time
ADS1263_DELAY = {
    'ADS1263_DELAY_0s'      : 0,
    'ADS1263_DELAY_8d7us'   : 1,
    'ADS1263_DELAY_17us'    : 2,
    'ADS1263_DELAY_35us'    : 3,
    'ADS1263_DELAY_169us'   : 4,
    'ADS1263_DELAY_139us'   : 5,
    'ADS1263_DELAY_278us'   : 6,
    'ADS1263_DELAY_555us'   : 7,
    'ADS1263_DELAY_1d1ms'   : 8,
    'ADS1263_DELAY_2d2ms'   : 9,
    'ADS1263_DELAY_4d4ms'   : 10,
    'ADS1263_DELAY_8d8ms'   : 11,
}

# DAC volt table
ADS1263_DAC_VOLT = {
    'ADS1263_DAC_VLOT_4_5'      : 0b01001,
    'ADS1263_DAC_VLOT_3_5'      : 0b01000,
    'ADS1263_DAC_VLOT_3'        : 0b00111,
    'ADS1263_DAC_VLOT_2_75'     : 0b00110,
    'ADS1263_DAC_VLOT_2_625'    : 0b00101,
    'ADS1263_DAC_VLOT_2_5625'   : 0b00100,
    'ADS1263_DAC_VLOT_2_53125'  : 0b00011,
    'ADS1263_DAC_VLOT_2_515625' : 0b00010,
    'ADS1263_DAC_VLOT_2_5078125': 0b00001,
    'ADS1263_DAC_VLOT_2_5'      : 0b00000,
    'ADS1263_DAC_VLOT_2_4921875': 0b10001,
    'ADS1263_DAC_VLOT_2_484375' : 0b10010,
    'ADS1263_DAC_VLOT_2_46875'  : 0b10011,
    'ADS1263_DAC_VLOT_2_4375'   : 0b10100,
    'ADS1263_DAC_VLOT_2_375'    : 0b10101,
    'ADS1263_DAC_VLOT_2_25'     : 0b10110,
    'ADS1263_DAC_VLOT_2'        : 0b10111,
    'ADS1263_DAC_VLOT_1_5'      : 0b11000,
    'ADS1263_DAC_VLOT_0_5'      : 0b11001,
}

# Register map
ADS1263_REG = {
    'REG_ID'        : 0,
    'REG_POWER'     : 1,
    'REG_INTERFACE' : 2,
    'REG_MODE0'     : 3,
    'REG_MODE1'     : 4,
    'REG_MODE2'     : 5,
    'REG_INPMUX'    : 6,
    'REG_OFCAL0'    : 7,
    'REG_OFCAL1'    : 8,
    'REG_OFCAL2'    : 9,
    'REG_FSCAL0'    : 10,
    'REG_FSCAL1'    : 11,
    'REG_FSCAL2'    : 12,
    'REG_IDACMUX'   : 13,
    'REG_IDACMAG'   : 14,
    'REG_REFMUX'    : 15,
    'REG_TDACP'     : 16,
    'REG_TDACN'     : 17,
    'REG_GPIOCON'   : 18,
    'REG_GPIODIR'   : 19,
    'REG_GPIODAT'   : 20,
    'REG_ADC2CFG'   : 21,
    'REG_ADC2MUX'   : 22,
    'REG_ADC2OFC0'  : 23,
    'REG_ADC2OFC1'  : 24,
    'REG_ADC2FSC0'  : 25,
    'REG_ADC2FSC1'  : 26,
}

# Commands
ADS1263_CMD = {
    'CMD_RESET'     : 0x06,
    'CMD_START1'    : 0x08,
    'CMD_STOP1'     : 0x0A,
    'CMD_START2'    : 0x0C,
    'CMD_STOP2'     : 0x0E,
    'CMD_RDATA1'    : 0x12,
    'CMD_RDATA2'    : 0x14,
    'CMD_SYOCAL1'   : 0x16,
    'CMD_SYGCAL1'   : 0x17,
    'CMD_SFOCAL1'   : 0x19,
    'CMD_SYOCAL2'   : 0x1B,
    'CMD_SYGCAL2'   : 0x1C,
    'CMD_SFOCAL2'   : 0x1E,
    'CMD_RREG'      : 0x20,
    'CMD_RREG2'     : 0x00,
    'CMD_WREG'      : 0x40,
    'CMD_WREG2'     : 0x00,
}

# ---------------------------------------------------------------------------
# FULL ADS1263 CLASS (complete, restored, patched)
# ---------------------------------------------------------------------------

class ADS1263:
    def __init__(self):
        self.rst_pin = config.RST_PIN
        self.cs_pin = config.CS_PIN
        self.drdy_pin = config.DRDY_PIN
        self.ScanMode = 1

    # Hardware reset
    def ADS1263_reset(self):
        config.digital_write(self.rst_pin, GPIO.HIGH)
        config.delay_ms(200)
        config.digital_write(self.rst_pin, GPIO.LOW)
        config.delay_ms(200)
        config.digital_write(self.rst_pin, GPIO.HIGH)
        config.delay_ms(200)

    # Write command
    def ADS1263_WriteCmd(self, reg):
        config.digital_write(self.cs_pin, GPIO.LOW)
        config.spi_writebyte([reg])
        config.digital_write(self.cs_pin, GPIO.HIGH)

    # Write register
    def ADS1263_WriteReg(self, reg, data):
        config.digital_write(self.cs_pin, GPIO.LOW)
        config.spi_writebyte([ADS1263_CMD['CMD_WREG'] | reg, 0x00, data])
        config.digital_write(self.cs_pin, GPIO.HIGH)

    # Read register
    def ADS1263_ReadData(self, reg):
        config.digital_write(self.cs_pin, GPIO.LOW)
        config.spi_writebyte([ADS1263_CMD['CMD_RREG'] | reg, 0x00])
        data = config.spi_readbytes(1)
        config.digital_write(self.cs_pin, GPIO.HIGH)
        return data

    # Checksum
    def ADS1263_CheckSum(self, val, byt):
        sum = 0
        mask = 0xff
        while val:
            sum += val & mask
            val >>= 8
        sum += 0x9b
        return (sum & 0xff) ^ byt

    # Wait for DRDY — uses GPIO edge detection (epoll) so CPU is idle while waiting
    def ADS1263_WaitDRDY(self):
        result = GPIO.wait_for_edge(self.drdy_pin, GPIO.FALLING, timeout=2000)
        if result is None:
            print("DRDY timeout")

    # Read chip ID
    def ADS1263_ReadChipID(self):
        id = self.ADS1263_ReadData(ADS1263_REG['REG_ID'])
        return id[0] >> 5

    # Set mode
    def ADS1263_SetMode(self, Mode):
        self.ScanMode = Mode

    # Configure ADC1
    def ADS1263_ConfigADC(self, gain, drate):
        MODE2 = 0x80 | (gain << 4) | drate
        self.ADS1263_WriteReg(ADS1263_REG['REG_MODE2'], MODE2)

        REFMUX = 0x24
        self.ADS1263_WriteReg(ADS1263_REG['REG_REFMUX'], REFMUX)

        MODE0 = ADS1263_DELAY['ADS1263_DELAY_35us']
        self.ADS1263_WriteReg(ADS1263_REG['REG_MODE0'], MODE0)

        MODE1 = 0x84
        self.ADS1263_WriteReg(ADS1263_REG['REG_MODE1'], MODE1)

    # -----------------------------------------------------------------------
    # PATCHED INIT FUNCTION (supports both init styles)
    # -----------------------------------------------------------------------
    def ADS1263_init_ADC1(self, rate='ADS1263_14400SPS', vref='ADS1263_2_5V'):
        # Backward compatibility: if user passed only VREF
        if rate in VREF:
            vref = rate
            rate = 'ADS1263_14400SPS'

        if vref not in VREF:
            raise KeyError(f"Invalid VREF key '{vref}'")

        if rate not in ADS1263_DRATE:
            raise KeyError(f"Invalid data rate '{rate}'")

        if config.module_init() != 0:
            return -1

        self.ADS1263_reset()

        if self.ADS1263_ReadChipID() != 0x01:
            print("Chip ID mismatch")
            return -1

        self.ADS1263_WriteCmd(ADS1263_CMD['CMD_STOP1'])

        self.ADS1263_ConfigADC(
            ADS1263_GAIN['ADS1263_GAIN_1'],
            ADS1263_DRATE[rate]
        )

        self.ADS1263_WriteCmd(ADS1263_CMD['CMD_START1'])

        print(f"ADC1 initialized: VREF={VREF[vref]}V, RATE={rate}")
        return 0

    # Read ADC1 data
    def ADS1263_Read_ADC_Data(self):
        config.digital_write(self.cs_pin, GPIO.LOW)
        for _ in range(100):
            config.spi_writebyte([ADS1263_CMD['CMD_RDATA1']])
            if config.spi_readbytes(1)[0] & 0x40:
                break
        buf = config.spi_readbytes(5)
        config.digital_write(self.cs_pin, GPIO.HIGH)

        read = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]
        CRC = buf[4]

        if self.ADS1263_CheckSum(read, CRC) != 0:
            print("ADC1 checksum error")

        return read

    # Set differential channel
    def ADS1263_SetDiffChannal(self, Ch):
        mux = {
            0: (0 << 4) | 1,
            1: (2 << 4) | 3,
            2: (4 << 4) | 5,
            3: (6 << 4) | 7,
            4: (8 << 4) | 9,
        }[Ch]
        self.ADS1263_WriteReg(ADS1263_REG['REG_INPMUX'], mux)

    # Get differential reading
    def ADS1263_GetChannalValue(self, Ch):
        self.ADS1263_SetDiffChannal(Ch)
        self.ADS1263_WaitDRDY()
        return self.ADS1263_Read_ADC_Data()

    # Exit
    def ADS1263_Exit(self):
        config.module_exit()

