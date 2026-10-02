// raw sd card over spi
// chip select is driven by hand, not by the spi peripheral, so it stays low
// across a whole command and its response. crc stays off, we never send cmd59

#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "sdcard.h"

static const char *tag = "sdcard";

#ifdef CONFIG_HG_SD

#include "diskio_impl.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "ff.h"

// the card must be spoken to slowly until it is out of idle. it will not answer
// reliably above 400 khz before that, and it is only a few dozen bytes
#define INIT_HZ    400000

// how long to keep asking the card to finish starting up. the spec allows a
// second, a tired card on a cold morning wants the room
#define READY_MS   1500

// the card answers a command somewhere in the next few bytes, not immediately
#define RESP_TRIES 16

// tokens the card sends around a block of data
#define TOKEN_READ    0xfe
#define TOKEN_WRITE   0xfe

static spi_device_handle_t dev;
static int ready;
static int high_capacity;   // sdhc and sdxc address by block, sdsc by byte

// true until the card has finished starting up. while it is idle it drops the
// data line low for a byte before answering, so a zero there is the line and
// not a reply. once it is ready that stops and zero becomes the normal answer
// for success, so the rule has to come off with it
static int in_idle;
static uint32_t sectors;

// the last command's answer, pulled back onto byte boundaries. the bytes after
// the r1 belong to the same answer, the echo from cmd8 and the ocr from cmd58,
// so they are taken from here rather than read again off a stream that has
// already moved on
#define ALIGNED_MAX 8
static uint8_t aligned[ALIGNED_MAX];
static int have;

// one byte out, one byte back. every exchange with a card is full duplex, even
// when we only care about one direction, so this is the whole conversation
static uint8_t xfer(uint8_t out)
{
    uint8_t in = 0xff;

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &out,
        .rx_buffer = &in,
    };

    spi_device_polling_transmit(dev, &t);
    return in;
}

static void xfer_block(const uint8_t *out, uint8_t *in, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = out,
        .rx_buffer = in,
    };

    spi_device_polling_transmit(dev, &t);
}

// ones to clock out while listening. one transaction per byte can leave a stray
// clock edge at the boundary, so everything moves in block transactions
#define ONES_MAX 32
static uint8_t ones[ONES_MAX];

static void read_bytes(uint8_t *in, size_t len)
{
    while (len > 0) {
        size_t n = len > ONES_MAX ? ONES_MAX : len;
        xfer_block(ones, in, n);
        in += n;
        len -= n;
    }
}

// clocks with nothing selected. a card needs them to wake, and it needs them
// again after a command so it can let go of the bus
static void idle_clocks(int bytes)
{
    uint8_t sink[ONES_MAX];

    while (bytes > 0) {
        int n = bytes > ONES_MAX ? ONES_MAX : bytes;
        xfer_block(ones, sink, (size_t)n);
        bytes -= n;
    }
}

static void cs_low(void)
{
    gpio_set_level(CONFIG_HG_SD_CS_GPIO, 0);
}

static void cs_high(void)
{
    gpio_set_level(CONFIG_HG_SD_CS_GPIO, 1);
    // one byte with it released so the card stops driving the data line
    xfer(0xff);
}

// crc7 over the command, the low bit is always set. only cmd0 and cmd8 are
// checked by the card, but sending the right one every time costs nothing and
// means turning crc on later is a one line change rather than a rewrite
static uint8_t crc7(const uint8_t *in, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = in[i];

        for (int bit = 0; bit < 8; bit++) {
            crc <<= 1;
            if ((b & 0x80) ^ (crc & 0x80))
                crc ^= 0x09;
            b <<= 1;
        }
    }

    return (uint8_t)((crc << 1) | 1);
}

// these commands must answer with the idle bit set, so a zero from them is the
// line settling. acmd41 and the block commands say success with zero, so this
// cannot be applied to everything
static int settles_low(uint8_t index)
{
    if (!in_idle)
        return 0;

    return index == 0 || index == 8 || index == 41 || index == 55;
}

// sends a command and returns the first response byte. 0xff means the card
// never answered at all
static uint8_t command(uint8_t index, uint32_t arg)
{
    uint8_t frame[6];

    frame[0] = (uint8_t)(0x40 | index);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)arg;
    frame[5] = crc7(frame, 5);

    // a byte of nothing, then the whole frame, in a single transaction. sent a
    // byte at a time the card was picking up a stray clock edge at every
    // boundary and every answer came back very slightly wrong
    uint8_t out[7];
    uint8_t sink[7];

    out[0] = 0xff;
    memcpy(out + 1, frame, 6);
    xfer_block(out, sink, sizeof out);

    // cmd12 answers with a stuff byte in front of the real one
    if (index == 12)
        idle_clocks(1);

    // the answer does not land on our byte boundary on this chip, it arrives a
    // few bits early, so read a window and line it up afterwards
    uint8_t seen[RESP_TRIES];
    read_bytes(seen, RESP_TRIES);

    // the line idles high, so the answer starts at the first zero bit. find it
    // and read everything from there, bit by bit rather than byte by byte
    int start = -1;

    for (int bit = 0; bit < RESP_TRIES * 8 && start < 0; bit++) {
        if (((seen[bit / 8] >> (7 - (bit % 8))) & 1) == 0)
            start = bit;
    }

    if (start < 0)
        return 0xff;

    have = 0;

    for (int i = 0; i < ALIGNED_MAX; i++) {
        uint8_t b = 0;

        for (int bit = 0; bit < 8; bit++) {
            int at = start + i * 8 + bit;
            if (at >= RESP_TRIES * 8)
                return have ? aligned[0] : (uint8_t)0xff;

            b = (uint8_t)((b << 1) | ((seen[at / 8] >> (7 - (at % 8))) & 1));
        }

        aligned[i] = b;
        have++;
    }

    // a zero here is the line settling rather than an answer, but only while
    // the card is still idle. after that zero is what success looks like
    if (settles_low(index) && aligned[0] == 0x00) {
        for (int i = 1; i < have; i++) {
            if (aligned[i] != 0x00) {
                uint8_t r = aligned[i];
                memmove(aligned, aligned + i, (size_t)(have - i));
                have -= i;
                return r;
            }
        }
        return 0xff;
    }

    return aligned[0];
}

// waits for the token that says a block of data follows. the line idles high
// and settles low in between, so neither 0xff nor 0x00 means anything here,
// only 0xfe does. stopping at the first byte that is not 0xff catches the
// settling and calls it an error
static uint8_t wait_token(int tries)
{
    uint8_t buf[ONES_MAX];

    for (int done = 0; done < tries; done += ONES_MAX) {
        read_bytes(buf, ONES_MAX);

        for (int i = 0; i < ONES_MAX; i++) {
            if (buf[i] == TOKEN_READ)
                return buf[i];

            // the card reports a read problem with a byte whose top three bits
            // are clear and at least one of the low four set
            if (buf[i] != 0xff && buf[i] != 0x00 && (buf[i] & 0xe0) == 0)
                return buf[i];
        }
    }

    return 0xff;
}

// a real r1 with the in idle state bit set. 0xff is no answer at all and its
// bit 0 is set too, so that has to be excluded by hand
static int idle(uint8_t r)
{
    return r != 0xff && (r & 0x80) == 0 && (r & 0x01) != 0;
}

// cmd55 then the real one. the application commands all go in pairs
static uint8_t app_command(uint8_t index, uint32_t arg)
{
    command(55, 0);
    return command(index, arg);
}

static esp_err_t set_speed(int hz)
{
    if (dev != NULL) {
        spi_bus_remove_device(dev);
        dev = NULL;
    }

    spi_device_interface_config_t dcfg = {
        .clock_speed_hz = hz,
        // SPI mode 0 for this experimental block driver.
        .mode = 0,
        // chip select is software-controlled. the SPI driver would raise it
        // between transactions and a card needs it held down across a whole
        // command and its response
        .spics_io_num = -1,
        .queue_size = 1,
        // the driver will otherwise put compensation clocks in front of a read
        // to allow for input delay. that is right for a fast bus and wrong for
        // us, it shifts the card's answer off the byte boundary and every value
        // comes back three bits out
        .flags = SPI_DEVICE_NO_DUMMY,
    };

    esp_err_t err = spi_bus_add_device(SPI2_HOST, &dcfg, &dev);
    if (err != ESP_OK)
        return err;

    // what we asked for and what the divider can actually produce are not the
    // same number. a card that is being clocked far faster than we think would
    // mangle every byte and report crc errors on commands we know are correct
    int real = 0;
    if (spi_device_get_actual_freq(dev, &real) == ESP_OK)
        ESP_LOGI(tag, "asked for %d hz, the bus is really running at %d khz", hz, real);

    return ESP_OK;
}

static esp_err_t bus_up(void)
{
    memset(ones, 0xff, sizeof ones);

    spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_HG_SD_MOSI_GPIO,
        .miso_io_num = CONFIG_HG_SD_MISO_GPIO,
        .sclk_io_num = CONFIG_HG_SD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SDCARD_SECTOR + 16,
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;

    // Enable the input pull-up; external pull-ups must match the board design.
    gpio_set_pull_mode(CONFIG_HG_SD_MISO_GPIO, GPIO_PULLUP_ONLY);

    gpio_reset_pin(CONFIG_HG_SD_CS_GPIO);
    gpio_set_direction(CONFIG_HG_SD_CS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_HG_SD_CS_GPIO, 1);

    return ESP_OK;
}

// reads the card's csd and works out how many sectors it holds
static esp_err_t read_size(void)
{
    cs_low();

    if (command(9, 0) != 0) {
        cs_high();
        ESP_LOGE(tag, "the card would not give up its csd");
        return ESP_FAIL;
    }

    uint8_t token = wait_token(200);

    if (token != TOKEN_READ) {
        cs_high();
        ESP_LOGE(tag, "no csd data token, got 0x%02x", token);
        return ESP_FAIL;
    }

    uint8_t csd[16];
    read_bytes(csd, sizeof csd);

    // the two crc bytes, read and thrown away
    idle_clocks(2);
    cs_high();

    int version = csd[0] >> 6;

    if (version == 1) {
        // v2, the size is a straight count of 512 kilobyte units
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3f) << 16) |
                          ((uint32_t)csd[8] << 8) | csd[9];
        sectors = (c_size + 1) * 1024;
    } else {
        // v1, the awkward one, size and two multipliers spread across bytes
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) |
                          ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        uint32_t mult = (uint32_t)(((csd[9] & 0x03) << 1) | (csd[10] >> 7));
        uint32_t read_len = csd[5] & 0x0f;

        sectors = (c_size + 1) << (mult + 2 + read_len - 9);
    }

    return ESP_OK;
}

esp_err_t sdcard_init(void)
{
    if (ready)
        return ESP_OK;

    esp_err_t err = bus_up();
    if (err != ESP_OK) {
        ESP_LOGE(tag, "spi bus would not start, %s", esp_err_to_name(err));
        return err;
    }

    err = set_speed(INIT_HZ);
    if (err != ESP_OK) {
        ESP_LOGE(tag, "could not add the card to the bus, %s", esp_err_to_name(err));
        return err;
    }

    // eighty clocks with nothing selected. this is what puts a card into a
    // state where it will listen to spi at all, and it has to happen with chip
    // select high or it is ignored
    cs_high();
    idle_clocks(10);

    in_idle = 1;

    // cmd0, go idle. a card answers 0x01 once it is in spi mode. the first try
    // often misses because the card is still waking, so ask a few times
    // bit 0 is the in idle state flag and the only one that must be set. cards
    // raise others alongside it, 0x03 with erase reset is legal
    uint8_t r = 0xff;

    cs_low();
    for (int i = 0; i < 10 && !idle(r); i++) {
        r = command(0, 0);
        if (!idle(r)) {
            cs_high();
            idle_clocks(2);
            cs_low();
        }
    }

    if (!idle(r)) {
        cs_high();
        ESP_LOGE(tag, "no card, cmd0 answered 0x%02x and we wanted the idle bit set", r);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(tag, "card is in spi mode");

    // cmd8, tell it we are a 2.7 to 3.6 volt host and give it a pattern to echo
    r = command(8, 0x000001aa);

    if (r == 0x01) {
        uint8_t echo[4];
        memcpy(echo, aligned + 1, sizeof echo);

        if (echo[2] != 0x01 || echo[3] != 0xaa) {
            cs_high();
            ESP_LOGE(tag, "cmd8 came back %02x %02x %02x %02x, this card will not run at our voltage",
                     echo[0], echo[1], echo[2], echo[3]);
            return ESP_ERR_NOT_SUPPORTED;
        }
    } else if (!(r & 0x04)) {
        // anything other than an illegal command reply is a card we cannot read
        cs_high();
        ESP_LOGE(tag, "cmd8 answered 0x%02x", r);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // acmd41, come out of idle. the high capacity bit tells a modern card we
    // understand block addressing. this is the slow one, a card takes its time
    int64_t give_up = esp_timer_get_time() + READY_MS * 1000;

    // busy is 0x01 and ready is 0x00, but the settling byte is 0x00 too. zeros
    // are skipped, so ready shows up as the read running out
    do {
        r = app_command(41, 0x40000000);
        if (r == 0xff) {
            r = 0;
            break;
        }
        // bit 2 is illegal command. a card that will not take acmd41 is an mmc
        // style part and wants cmd1 instead, which does the same job
        if (r & 0x04)
            break;
        esp_rom_delay_us(1000);
    } while (esp_timer_get_time() < give_up);

    if (r & 0x04) {
        ESP_LOGW(tag, "acmd41 refused, this is an mmc style card, using cmd1");

        give_up = esp_timer_get_time() + READY_MS * 1000;

        do {
            r = command(1, 0);
            if (r == 0xff || r == 0x00) {
                r = 0;
                break;
            }
            esp_rom_delay_us(1000);
        } while (esp_timer_get_time() < give_up);
    }

    if (r != 0) {
        cs_high();
        ESP_LOGE(tag, "the card never came out of idle, last answer 0x%02x", r);
        return ESP_ERR_TIMEOUT;
    }

    in_idle = 0;
    ESP_LOGI(tag, "card is out of idle and ready");

    // cmd58, read the operating conditions. bit 30 says whether it addresses by
    // block or by byte, and getting that wrong reads the wrong part of the card
    uint8_t r58 = command(58, 0);

    if (r58 == 0) {
        uint8_t ocr[4];
        memcpy(ocr, aligned + 1, sizeof ocr);

        high_capacity = (ocr[0] & 0x40) != 0;

        ESP_LOGI(tag, "ocr %02x %02x %02x %02x, %s",
                 ocr[0], ocr[1], ocr[2], ocr[3],
                 high_capacity ? "block addressed" : "byte addressed");
    } else {
        ESP_LOGW(tag, "cmd58 answered 0x%02x, assuming byte addressed", r58);
    }

    // a byte addressed card needs telling the block size. a block addressed one
    // is fixed at 512 and refuses the command
    if (!high_capacity)
        command(16, SDCARD_SECTOR);

    cs_high();

    // capacity is nice to have, not required. the filesystem only needs it to
    // format a card and we never format one. a card that will not give up its
    // csd can still be read and written, so this is a warning and not a stop
    if (read_size() != ESP_OK)
        ESP_LOGW(tag, "no capacity from the card, carrying on without it");

    // now it is up, go as fast as the wiring will take
    err = set_speed(CONFIG_HG_SD_FREQ_KHZ * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(tag, "could not move to %d khz, %s",
                 CONFIG_HG_SD_FREQ_KHZ, esp_err_to_name(err));
        return err;
    }

    ready = 1;

    ESP_LOGI(tag, "%s card up, %lu sectors, %lu mb, running at %d khz",
             sdcard_type(), (unsigned long)sectors,
             (unsigned long)(sectors / 2048), CONFIG_HG_SD_FREQ_KHZ);

    // read the very first sector as a proof of life. a card that answers its
    // init commands and then will not hand over a block is no use to us, and
    // finding that out here is far cheaper than finding it out mid run
    uint8_t first[SDCARD_SECTOR];

    if (sdcard_read(0, first, 1) != ESP_OK) {
        ESP_LOGE(tag, "the card came up but sector 0 would not read");
        ready = 0;
        return ESP_FAIL;
    }

    // the last two bytes of a formatted first sector are 0x55 0xaa. it is not
    // proof of a filesystem but it does say real data came back rather than a
    // block of ones or zeros off an idle line
    ESP_LOGI(tag, "sector 0 read, first bytes %02x %02x %02x, boot signature %02x %02x",
             first[0], first[1], first[2],
             first[SDCARD_SECTOR - 2], first[SDCARD_SECTOR - 1]);

    return ESP_OK;
}

int sdcard_ready(void)
{
    return ready;
}

uint32_t sdcard_sectors(void)
{
    return sectors;
}

const char *sdcard_type(void)
{
    if (!sectors)
        return "unknown";
    return high_capacity ? "SDHC" : "SDSC";
}

// a block addressed card counts in sectors, a byte addressed one counts in
// bytes. one place decides it so no caller has to think about it
static uint32_t address(uint32_t sector)
{
    return high_capacity ? sector : sector * SDCARD_SECTOR;
}

esp_err_t sdcard_read(uint32_t sector, uint8_t *out, uint32_t count)
{
    if (!ready)
        return ESP_ERR_INVALID_STATE;

    for (uint32_t n = 0; n < count; n++) {
        cs_low();

        if (command(17, address(sector + n)) != 0) {
            cs_high();
            ESP_LOGE(tag, "read of sector %lu was refused", (unsigned long)(sector + n));
            return ESP_FAIL;
        }

        uint8_t token = wait_token(2000);

        if (token != TOKEN_READ) {
            cs_high();
            ESP_LOGE(tag, "sector %lu gave token 0x%02x", (unsigned long)(sector + n), token);
            return ESP_FAIL;
        }

        memset(out + n * SDCARD_SECTOR, 0xff, SDCARD_SECTOR);
        xfer_block(out + n * SDCARD_SECTOR, out + n * SDCARD_SECTOR, SDCARD_SECTOR);

        // the two crc bytes, read and thrown away. crc is off in spi mode
        idle_clocks(2);

        cs_high();
    }

    return ESP_OK;
}

esp_err_t sdcard_write(uint32_t sector, const uint8_t *in, uint32_t count)
{
    if (!ready)
        return ESP_ERR_INVALID_STATE;

    for (uint32_t n = 0; n < count; n++) {
        cs_low();

        if (command(24, address(sector + n)) != 0) {
            cs_high();
            ESP_LOGE(tag, "write to sector %lu was refused", (unsigned long)(sector + n));
            return ESP_FAIL;
        }

        // a gap, then the token that says a block follows, in one go
        uint8_t lead[2] = { 0xff, TOKEN_WRITE };
        uint8_t lead_in[2];
        xfer_block(lead, lead_in, sizeof lead);

        xfer_block(in + n * SDCARD_SECTOR, NULL, SDCARD_SECTOR);

        // two crc bytes the card ignores while crc is off, then the answer.
        // the low five bits say whether it took the block
        uint8_t tail[2 + RESP_TRIES];
        read_bytes(tail, sizeof tail);

        uint8_t resp = 0xff;
        for (size_t i = 2; i < sizeof tail; i++) {
            if ((tail[i] & 0x11) == 0x01) {
                resp = tail[i];
                break;
            }
        }

        if ((resp & 0x1f) != 0x05) {
            cs_high();
            ESP_LOGE(tag, "sector %lu was rejected, response 0x%02x",
                     (unsigned long)(sector + n), resp);
            return ESP_FAIL;
        }

        // the card holds the line low while it is actually writing. this is the
        // one wait that genuinely matters, cutting it short corrupts the block
        int64_t give_up = esp_timer_get_time() + 500 * 1000;
        uint8_t busy[1];
        read_bytes(busy, 1);
        while (busy[0] == 0x00) {
            read_bytes(busy, 1);
            if (esp_timer_get_time() > give_up) {
                cs_high();
                ESP_LOGE(tag, "sector %lu never finished writing", (unsigned long)(sector + n));
                return ESP_ERR_TIMEOUT;
            }
        }

        cs_high();
    }

    return ESP_OK;
}

// fatfs sits on top through these four. nothing above the filesystem knows or
// depends on which SD block driver is used underneath

static DSTATUS fat_init(BYTE pdrv)
{
    (void)pdrv;
    return ready ? 0 : STA_NOINIT;
}

static DSTATUS fat_status(BYTE pdrv)
{
    (void)pdrv;
    return ready ? 0 : STA_NOINIT;
}

static DRESULT fat_read(BYTE pdrv, BYTE *buf, LBA_t sector, UINT count)
{
    (void)pdrv;
    return sdcard_read((uint32_t)sector, buf, count) == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT fat_write(BYTE pdrv, const BYTE *buf, LBA_t sector, UINT count)
{
    (void)pdrv;
    return sdcard_write((uint32_t)sector, buf, count) == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT fat_ioctl(BYTE pdrv, BYTE cmd, void *buf)
{
    (void)pdrv;

    switch (cmd) {
    case CTRL_SYNC:
        // every write already waits for the card to finish before it returns
        return RES_OK;
    case GET_SECTOR_COUNT:
        *((LBA_t *)buf) = sectors;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *((WORD *)buf) = SDCARD_SECTOR;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *((DWORD *)buf) = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

static FATFS *fs;
static BYTE drive = 0xff;

esp_err_t sdcard_mount(const char *mount_point)
{
    esp_err_t err = sdcard_init();
    if (err != ESP_OK)
        return err;

    if (drive == 0xff) {
        if (ff_diskio_get_drive(&drive) != ESP_OK || drive == 0xff) {
            ESP_LOGE(tag, "no free fatfs drive");
            return ESP_FAIL;
        }

        static const ff_diskio_impl_t impl = {
            .init = fat_init,
            .status = fat_status,
            .read = fat_read,
            .write = fat_write,
            .ioctl = fat_ioctl,
        };

        ff_diskio_register(drive, &impl);
    }

    char path[3] = { (char)('0' + drive), ':', '\0' };

    err = esp_vfs_fat_register(mount_point, path, 4, &fs);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(tag, "could not register %s, %s", mount_point, esp_err_to_name(err));
        return err;
    }

    // 1 means mount now rather than on first use, so a bad card is found here
    FRESULT res = f_mount(fs, path, 1);
    if (res != FR_OK) {
        ESP_LOGE(tag, "the card came up but has no readable filesystem, f_mount said %d. it wants fat32",
                 res);
        esp_vfs_fat_unregister_path(mount_point);
        return ESP_FAIL;
    }

    ESP_LOGI(tag, "%s mounted at %s", sdcard_type(), mount_point);
    return ESP_OK;
}

#else

esp_err_t sdcard_init(void)
{
    ESP_LOGI(tag, "not built in");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sdcard_mount(const char *mount_point)
{
    (void)mount_point;
    return ESP_ERR_NOT_SUPPORTED;
}

int sdcard_ready(void)        { return 0; }
uint32_t sdcard_sectors(void) { return 0; }
const char *sdcard_type(void) { return "unknown"; }

esp_err_t sdcard_read(uint32_t sector, uint8_t *out, uint32_t count)
{
    (void)sector; (void)out; (void)count;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sdcard_write(uint32_t sector, const uint8_t *in, uint32_t count)
{
    (void)sector; (void)in; (void)count;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
