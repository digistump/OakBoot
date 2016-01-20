//////////////////////////////////////////////////
// OakBoot by Erik Kettenburg/Digistump
// Based on rBoot open source boot loader for ESP8266.
// Copyright 2015 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.
//////////////////////////////////////////////////
/*
PROVIDE(PIN_OUT_SET = 0x60000304);
PROVIDE(PIN_OUT_CLEAR = 0x60000308);
PROVIDE(PIN_DIR_OUTPUT = 0x60000310);
PROVIDE(PIN_DIR_INPUT = 0x60000314);
*/

#include "oakboot-private.h"
#include <oakboot-hex2a.h>

static uint32 check_image(uint32 readpos)
{
    uint8 buffer[BUFFER_SIZE];
    uint8 sectcount;
    uint8 sectcurrent;
    uint8* writepos;
    uint8 chksum = CHKSUM_INIT;
    uint32 loop;
    uint32 remaining;
    uint32 romaddr;

    rom_header_new* header = (rom_header_new*)buffer;
    section_header* section = (section_header*)buffer;

    if (readpos == 0 || readpos == 0xffffffff)
    {
        // ets_printf("EMPTY");
        return 0;
    }

    // read rom header
    if (SPIRead(readpos, header, sizeof(rom_header_new)) != 0)
    {
        // ets_printf("NO_HEADER");
        return 0;
    }

    // check header type
    if (header->magic == ROM_MAGIC)
    {
        // old type, no extra header or irom section to skip over
        romaddr = readpos;
        readpos += sizeof(rom_header);
        sectcount = header->count;
    }
    else if (header->magic == ROM_MAGIC_NEW1 && header->count == ROM_MAGIC_NEW2)
    {
        // new type, has extra header and irom section first
        romaddr = readpos + header->len + sizeof(rom_header_new);

        // we will set the real section count later, when we read the header
        sectcount = 0xff;
        // just skip the first part of the header
        // rest is processed for the chksum
        readpos += sizeof(rom_header);
        /*
                    // skip the extra header and irom section
                    readpos = romaddr;
                    // read the normal header that follows
                    if (SPIRead(readpos, header, sizeof(rom_header)) != 0) {
                            //ets_printf("NNH");
                            return 0;
                    }
                    sectcount = header->count;
                    readpos += sizeof(rom_header);
    */
    }
    else
    {
        // ets_printf("BH");
        return 0;
    }

    // test each section
    for (sectcurrent = 0; sectcurrent < sectcount; sectcurrent++)
    {
        // ets_printf("ST");

        // read section header
        if (SPIRead(readpos, section, sizeof(section_header)) != 0)
        {
            return 0;
        }
        readpos += sizeof(section_header);

        // get section address and length
        writepos = section->address;
        remaining = section->length;

        while (remaining > 0)
        {
            // work out how much to read, up to BUFFER_SIZE
            uint32 readlen = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
            // read the block
            if (SPIRead(readpos, buffer, readlen) != 0)
            {
                return 0;
            }
            // increment next read and write positions
            readpos += readlen;
            writepos += readlen;
            // decrement remaining count
            remaining -= readlen;
            // add to chksum
            for (loop = 0; loop < readlen; loop++)
            {
                chksum ^= buffer[loop];
            }
        }

        //#ifdef BOOT_IROM_CHKSUM
        if (sectcount == 0xff)
        {
            // just processed the irom section, now
            // read the normal header that follows
            if (SPIRead(readpos, header, sizeof(rom_header)) != 0)
            {
                // ets_printf("SPI");
                return 0;
            }
            sectcount = header->count + 1;
            readpos += sizeof(rom_header);
        }
        //#endif
    }

    // round up to next 16 and get checksum
    readpos = readpos | 0x0f;
    if (SPIRead(readpos, buffer, 1) != 0)
    {
        // ets_printf("CK");
        return 0;
    }

    // compare calculated and stored checksums
    if (buffer[0] != chksum)
    {
        // ets_printf("CKF");
        return 0;
    }

    return romaddr;
}
#define GPIO5 32
#define ETS_UNCACHED_ADDR(addr) (addr)
#define READ_PERI_REG(addr) (*((volatile uint32*)ETS_UNCACHED_ADDR(addr)))
#define WRITE_PERI_REG(addr, val) (*((volatile uint32*)ETS_UNCACHED_ADDR(addr))) = (uint32)(val)
#define PERIPHS_RTC_BASEADDR 0x60000700
#define REG_RTC_BASE PERIPHS_RTC_BASEADDR
#define RTC_GPIO_OUT (REG_RTC_BASE + 0x068)
#define RTC_GPIO_ENABLE (REG_RTC_BASE + 0x074)
#define RTC_GPIO_IN_DATA (REG_RTC_BASE + 0x08C)
#define RTC_GPIO_CONF (REG_RTC_BASE + 0x090)
#define PAD_XPD_DCDC_CONF (REG_RTC_BASE + 0x0A0)
static uint32 get_gpio16()
{
    // set output level to 1
    WRITE_PERI_REG(RTC_GPIO_OUT, (READ_PERI_REG(RTC_GPIO_OUT) & (uint32)0xfffffffe) | (uint32)(1));

    // read level
    WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
        (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc)
            | (uint32)0x1); // mux configuration for XPD_DCDC and
    // rtc_gpio0 connection
    WRITE_PERI_REG(RTC_GPIO_CONF,
        (READ_PERI_REG(RTC_GPIO_CONF) & (uint32)0xfffffffe)
            | (uint32)0x0); // mux configuration for out enable
    WRITE_PERI_REG(
        RTC_GPIO_ENABLE, READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32)0xfffffffe); // out disable

    uint32 x = (READ_PERI_REG(RTC_GPIO_IN_DATA) & 1);

    return x;
}

//#ifdef BOOT_CONFIG_CHKSUM
// calculate checksum for block of data
// from start up to (but excluding) end
static uint8 calc_chksum(uint8* start, uint8* end)
{
    uint8 chksum = CHKSUM_INIT;
    while (start < end)
    {
        chksum ^= *start;
        start++;
    }
    return chksum;
}
//#endif
//
struct rst_info
{
    uint32 reason;
    uint32 exccause;
    uint32 epc1;
    uint32 epc2;
    uint32 epc3;
    uint32 excvaddr;
    uint32 depc;
};

uint32 system_rtc_mem_read(int32 addr, void* buff, int32 length)
{
    int32 blocks;

    // validate reading a user block
    // if (addr < 64) return 0;
    if (buff == 0)
        return 0;
    // validate 4 byte aligned
    if (((uint32)buff & 0x3) != 0)
        return 0;
    // validate length is multiple of 4
    if ((length & 0x3) != 0)
        return 0;

    // check valid length from specified starting point
    if (length > (0x300 - (addr * 4)))
        return 0;

    // copy the data
    for (blocks = (length >> 2) - 1; blocks >= 0; blocks--)
    {
        volatile uint32* ram = ((uint32*)buff) + blocks;
        volatile uint32* rtc = ((uint32*)0x60001100) + addr + blocks;
        *ram = *rtc;
    }

    return 1;
}

uint32 rst_reason()
{
    struct rst_info rst;
    uint32 rtcReason;
    uint32 secondReason;
    uint32 firstReason;

    system_rtc_mem_read(0, &rst, sizeof(struct rst_info)); // this value doesn't actually matter
    firstReason = rst.reason;
    if (rst.reason >= 7)
    {
        ets_memset(&rst, 0, sizeof(struct rst_info));
    }
    rtcReason = rtc_get_reset_reason();
    secondReason = rst.reason;
    /*if (rtc_get_reset_reason() == 2) { //is this needed? not sure
      ets_memset(&rst, 0, sizeof(struct rst_info));
  }*/

    // ets_printf("\r\nR:%d F:%d S:%d",rtcReason,firstReason,secondReason);

    if (rtcReason == 4)
    {
        return 1;
    }
    else
    { // if(rtcReason == 2 || rtcReason == 1){
        if (secondReason == 1 || (secondReason == 0 && firstReason == 0))
            return 6;
        else
            return secondReason;
    }

    return 0; // unkown revert?
}

// prevent this function being placed inline with main
// to keep main's stack size as small as possible
// don't mark as static or it'll be optimised out when
// using the assembler stub
uint32 NOINLINE find_image()
{
    uint8 flag;
    uint32 runAddr;
    uint32 flashsize;
    int32 romToBoot;
    uint8 oldFactory;
    uint8 oldFailures;
    uint8 forced_reinit = 0;
    uint8 updateConfig = FALSE;
    uint8 buffer[SECTOR_SIZE];

    oakboot_config* romconf = (oakboot_config*)buffer;
    rom_header* header = (rom_header*)buffer;

    // delay to slow boot (help see messages when debugging)
    // ets_delay_us(5000000);

    ets_printf("\r\nOakBoot v1 - ");
    // uint8 temp;
    /// SPIRead(256 * SECTOR_SIZE, &temp, 1);
    /// ets_printf("t %d\r\n", temp);
    ///
    ///
    oakboot_bootkey bootkey;
    if (romconf->bootkey_disable != 1)
    {
        SPIRead(BOOT_KEY_SECTOR * SECTOR_SIZE, &bootkey, sizeof(bootkey));
        // ets_printf("bk %d,%d,%d\r\n", bootkey.mode,bootkey.reset,bootkey.gpio);
    }

    // read rom header
    SPIRead(0, header, sizeof(rom_header));

    // print and get flash size
    flashsize = 0x400000;

    // read boot config
    SPIRead(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
    // fresh install or old version?
    //
    uint8 new_config = FALSE;

    if (romconf->magic != BOOT_CONFIG_MAGIC || romconf->version != BOOT_CONFIG_VERSION
        || romconf->chksum != calc_chksum((uint8*)romconf, (uint8*)&romconf->chksum)
        || bootkey.reset == 'R')
    {
        // read backup boot config
        ets_printf("B,");
        SPIRead(BOOT_BACKUP_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
        // test backup
        if (romconf->magic != BOOT_CONFIG_MAGIC || romconf->version != BOOT_CONFIG_VERSION
            || romconf->chksum != calc_chksum((uint8*)romconf, (uint8*)&romconf->chksum)
            || bootkey.reset == 'R')
        {
            // create a default config for a standard 2 rom setup
            new_config = TRUE;
        }
        else
        {
            // write new or restore backup to main record
            SPIEraseSector(BOOT_CONFIG_SECTOR);
            SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
        }
    }

    if (romconf->reinit_config == 1 || new_config == TRUE)
    {
        if (romconf->reinit_config == 1)
            forced_reinit = 1;
        ets_printf("D,");
        ets_memset(romconf, 0x00, sizeof(oakboot_config));
        romconf->magic = BOOT_CONFIG_MAGIC;
        romconf->version = BOOT_CONFIG_VERSION;
        romconf->count = 16;
        // romconf->reinit_config = 0;
        // romconf->current_rom = 0;
        // romconf->program_rom = 0;
        // romconf->config_rom = 0;
        // romconf->update_rom = 0;
        //#define UPDATE_ROM 0
        //#define CONFIG_ROM 1
        //#define PROGRAM_ROM 2

        // romconf->rom_on_swdt = UPDATE_ROM;
        // romconf->rom_on_hwdt = UPDATE_ROM;
        // romconf->rom_on_exception = UPDATE_ROM;
        // romconf->rom_on_reinit = UPDATE_ROM;
        // romconf->rom_on_gpio = UPDATE_ROM;
        // romconf->rom_on_invalid = UPDATE_ROM;
        romconf->first_boot = 1;
        romconf->factory_reason = 'D';
        romconf->failures_allowed = 1;
        romconf->reset_write_skip = 1;
        //---------MEG 1 - ROM0
        romconf->roms[0] = 0x002000; // 0x40000,0x7F000,0xFE000
        romconf->roms[1] = 0x041000; // 0x3F000
        romconf->roms[2] = 0x081000; // 0x40000,0x7F000
        romconf->roms[3] = 0x0C0000; // 0x3F000
        //---------MEG 2 - ROM1
        romconf->roms[4] = 0x102000; // 0x40000,0x7F000,0xFE000
        romconf->roms[5] = 0x141000; // 0x3F000
        romconf->roms[6] = 0x181000; // 0x40000,0x7F000
        romconf->roms[7] = 0x1C0000; // 0x3F000
        //---------MEG 3 - ROM2
        romconf->roms[8] = 0x202000; // 0x40000,0x7F000,0xFE000
        romconf->roms[9] = 0x241000; // 0x3F000
        romconf->roms[10] = 0x281000; // 0x40000,0x7F000
        romconf->roms[11] = 0x2C0000; // 0x3F000
        //---------MEG 4
        romconf->roms[12] = 0x300000; // 0x3F000,0x7E000,0xFB000
        romconf->roms[13] = 0x33F000; // 0x3F000
        romconf->roms[14] = 0x37E000; // 0x3F000,0x7D000
        romconf->roms[15] = 0x3BD000; // 0x3E000

        romconf->chksum = calc_chksum((uint8*)romconf, (uint8*)&romconf->chksum);
        // write new to backup first
        SPIEraseSector(BOOT_BACKUP_CONFIG_SECTOR);
        SPIWrite(BOOT_BACKUP_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
        // write new or restore backup to main record
        SPIEraseSector(BOOT_CONFIG_SECTOR);
        SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
    }

    oldFactory = romconf->factory_reason;
    oldFailures = romconf->failures;

    romconf->failures = 0;

    // if gpio mode enabled check status of the gpio
    if ((romconf->mode == 1 || bootkey.gpio == 'G') && (get_gpio16() == 0))
    { // go to gpio if 16 low
        ets_printf("G,");

        switch (romconf->rom_on_gpio)
        {
            case CONFIG_ROM:
                romToBoot = romconf->config_rom;
                break;
            case PROGRAM_ROM:
                romToBoot = romconf->program_rom;
                break;
            case UPDATE_ROM:
                romToBoot = romconf->update_rom;
                break;
        }

        romconf->factory_reason = 'G';
    }
    else if (romconf->update == 1)
    { // go to update for one cycle
        ets_printf("U,");
        romconf->factory_reason = 'U';
        romToBoot = romconf->update_rom;
        romconf->update = 0;
        updateConfig = TRUE;
    }
    else if (romconf->config == 1)
    { // go to config for one cycle
        ets_printf("C,");
        romconf->factory_reason = 'C';
        romToBoot = romconf->config_rom;
        romconf->config = 0;
        updateConfig = TRUE;
    }
    else if (romconf->current_rom >= romconf->count)
    { // if rom selection is
        // invalid, go to user
        // then factory
        // if invalid rom selected try rom 0
        ets_printf("I,");
        if (romconf->program_rom >= romconf->count)
        {
            romconf->factory_reason = 'I';
            switch (romconf->rom_on_invalid)
            {
                case CONFIG_ROM:
                    romToBoot = romconf->config_rom;
                    break;
                case PROGRAM_ROM:
                    romToBoot = romconf->program_rom;
                    break;
                case UPDATE_ROM:
                    romToBoot = romconf->update_rom;
                    break;
            }
        }
        else
            romToBoot = romconf->program_rom;

        romconf->current_rom = romToBoot;
        updateConfig = TRUE;
    }
    else if (bootkey.reset == 'R' || forced_reinit == 1)
    { // factory
        // factory reason was already set in init
        romconf->factory_reason = 'R';
        switch (romconf->rom_on_reinit)
        {
            case CONFIG_ROM:
                romToBoot = romconf->config_rom;
                break;
            case PROGRAM_ROM:
                romToBoot = romconf->program_rom;
                break;
            case UPDATE_ROM:
                romToBoot = romconf->update_rom;
                break;
        }
        ets_printf("R,");
    }
    else
    {
        uint32 reason = rst_reason();
        // ets_printf("RR-%d,",reason);
        // 1 = HARDWARE WDT, 2 = EXCEPTION, 3 = SOFTWARE WDT
        // THE FACTORY FIRMWARE SHOULD READ RST.REASON and DECIDE WHETHER TO JUMP
        // BACK TO USER OR ENTER ANOTHER STATE LIKE CHECK FOR UPDATES
        // SHOULD ALSO SEND FAILURE NOTICE TO CLOUD

        // romconf->reset_reason = reason;

        if (reason == 1 && romconf->rom_on_hwdt != PROGRAM_ROM)
        {
            uint8 factory_rom;

            factory_rom = romconf->update_rom;
            if (romconf->rom_on_hwdt == CONFIG_ROM)
                factory_rom = romconf->config_rom;

            ets_printf("H,");
            if (romconf->reset_write_skip != 1)
                romconf->factory_reason = 'H';

            if (romconf->failures_allowed > 1)
            {
                romconf->failures = oldFailures + 1;
                if (romconf->failures >= romconf->failures_allowed)
                {
                    romToBoot = factory_rom;
                    romconf->failures = 0;
                    if (oldFailures != romconf->failures)
                        updateConfig = TRUE;
                }
            }
            else
                romToBoot = factory_rom;
        }
        else if (reason == 2 && romconf->rom_on_exception != PROGRAM_ROM)
        {
            uint8 factory_rom;
            factory_rom = romconf->update_rom;
            // NOTE BUG HERE SHOULD BE rom_on_exception
            if (romconf->rom_on_hwdt == CONFIG_ROM)
                factory_rom = romconf->config_rom;
            ets_printf("E,");
            if (romconf->reset_write_skip != 1)
                romconf->factory_reason = 'E';

            if (romconf->failures_allowed > 1)
            {
                romconf->failures = oldFailures + 1;
                if (romconf->failures >= romconf->failures_allowed)
                {
                    romToBoot = factory_rom;
                    romconf->failures = 0;
                    if (oldFailures != romconf->failures)
                        updateConfig = TRUE;
                }
            }
            else
                romToBoot = factory_rom;
        }
        else if (reason == 3 && romconf->rom_on_swdt != PROGRAM_ROM)
        {
            uint8 factory_rom;
            factory_rom = romconf->update_rom;
            // NOTE BUG HERE SHOULD BE rom_on_swdt
            if (romconf->rom_on_hwdt == CONFIG_ROM)
                factory_rom = romconf->config_rom;
            ets_printf("W,");
            if (romconf->reset_write_skip != 1)
                romconf->factory_reason = 'W';

            if (romconf->failures_allowed > 1)
            {
                romconf->failures = oldFailures + 1;
                if (romconf->failures >= romconf->failures_allowed)
                {
                    romToBoot = factory_rom;
                    romconf->failures = 0;
                    if (oldFailures != romconf->failures)
                        updateConfig = TRUE;
                }
            }
            else
                romToBoot = factory_rom;
        }
        else
        {
            romconf->factory_reason = 0;
            // first three are so we still report that we detected this even when
            // settings dictate that we don't act
            if (reason == 3)
            {
                ets_printf("W,");
            }
            else if (reason == 2)
            {
                ets_printf("E,");
            }
            else if (reason == 1)
            {
                ets_printf("H,");
            }
            else if (bootkey.mode == 'S' || romconf->serial_mode == 1)
            {
                // serial mode always boot 0
                romToBoot = 0;
                ets_printf("S,");
            }
            else if (bootkey.mode == 'N' || bootkey.mode == 255 || bootkey.mode == 0)
            {
                // normal mode
                // try rom selected in the config
                ets_printf("N,");
                romToBoot = romconf->current_rom;
            }
            else if (bootkey.mode == 'L')
            { // local ota
                // try rom selected in the config
                romToBoot = romconf->current_rom;
                ets_printf("L,");
            }
            else if (bootkey.mode == 'U')
            { // factory
                romToBoot = romconf->update_rom;
                ets_printf("U,");
                // always boot to factory
            }
            else if (bootkey.mode == 'C')
            { // factory
                romToBoot = romconf->config_rom;
                ets_printf("C,");
                // always boot to factory
            }
            else if (bootkey.mode == 'P')
            { // user
                romToBoot = romconf->program_rom;
                ets_printf("P,");
                // always boot to user
            }
            else
            {
                // try rom selected in the config
                romToBoot = romconf->current_rom;
                ets_printf("N,");
            }
        }
    }

    if (romconf->factory_reason != oldFactory
        && (romToBoot == romconf->config_rom || romToBoot == romconf->update_rom))
    { // since only factory mode needs to
        // know why it booted
        updateConfig = TRUE;
    }

    // try to find a good rom
    do
    {
        runAddr = check_image(romconf->roms[romToBoot]);
        if (runAddr == 0)
        {
            // ets_printf("Rom %d is bad: %d\r\n", romToBoot,runAddr);
            if (romToBoot == romconf->update_rom)
            {
                // don't switch to backup for gpio-selected rom
                romconf->factory_reason = 0;
                ets_printf("FF,0\r\n");

                // we're dead - stuck in bootloader loop - blink
                if (romconf->led_off)
                {
                    // make sure wdt is enabled
                    return 0;
                }
                PIN_DIR_OUTPUT = GPIO5;

                while (1)
                {
                    PIN_OUT_SET = GPIO5;
                    ets_delay_us(30000);
                    PIN_OUT_CLEAR = GPIO5;
                    ets_delay_us(30000);
                }

                // make sure wdt is enabled
                return 0;
            }
            else
            {
                // user rom is bad jump to update, update should go to config if
                // factory_reason = F and no updates are available
                ets_printf("PF,");
                updateConfig = TRUE;
                romToBoot = romconf->update_rom;
                romconf->factory_reason = 'F';
            }
        }
    } while (runAddr == 0);

    // re-write config, if required
    if (updateConfig)
    {
        ets_printf("O,");
        romconf->current_rom = romToBoot;

        romconf->chksum = calc_chksum((uint8*)romconf, (uint8*)&romconf->chksum);

        SPIEraseSector(BOOT_BACKUP_CONFIG_SECTOR);
        SPIWrite(BOOT_BACKUP_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);

        SPIEraseSector(BOOT_CONFIG_SECTOR);
        SPIWrite(BOOT_CONFIG_SECTOR * SECTOR_SIZE, buffer, SECTOR_SIZE);
    }

    if (romconf->update_rom == romToBoot)
        ets_printf("BU,");
    else if (romconf->config_rom == romToBoot)
        ets_printf("BC,");
    else
        ets_printf("BP,");

    ets_printf("%d\r\n\r\n", romToBoot);
    // ets_wdt_enable();
    // copy the loader to top of iram
    ets_memcpy((void*)_text_addr, _text_data, _text_len);

    // return address to load from
    return runAddr;
}

#ifdef BOOT_NO_ASM

// small stub method to ensure minimum stack space used
void call_user_start()
{
    uint32 addr;
    stage2a* loader;

    addr = find_image();
    if (addr != 0)
    {
        loader = (stage2a*)entry_addr;
        loader(addr);
    }
}

#else

// assembler stub uses no stack space
// works with gcc
void call_user_start()
{
    __asm volatile("mov a15, a0\n" // store return addr, hope nobody wanted a15!
                   "call0 find_image\n" // find a good rom to boot
                   "mov a0, a15\n" // restore return addr
                   "bnez a2, 1f\n" // ?success
                   "ret\n" // no, return
                   "1:\n" // yes...
                   "movi a3, entry_addr\n" // actually gives us a pointer to entry_addr
                   "l32i a3, a3, 0\n" // now really load entry_addr
                   "jx a3\n" // now jump to it
        );
}

#endif
