/*
 * AD7732 driver for 26PCDFG2G -> INA125UA -> AD7732 AIN0
 *
 * Design for a fast, pulsatile pressure signal:
 *  - CH0, +/-10 V, 24-bit, chopping on, continuous conversion
 *  - RDY pin interrupt reads EVERY conversion (dump mode: status + data)
 *  - the ISR only accumulates min / max / sum, so main() can report
 *    peak, minimum and mean over any window (e.g. once per second)
 *  - no polling, no timeouts, no floating point in the ISR
 */
#include "ad7732_pressure.h"

/* ---------------- Platform hooks (see ad7732_port_stm32.c) ---------------- */
extern uint8_t spi_xfer(uint8_t tx);
extern void    spi_xfer_buf(const uint8_t *tx, uint8_t *rx, uint16_t len);
extern void    ad7732_cs(bool select);        /* true = CS low               */
extern void    ad7732_irq_disable(void);      /* mask the RDY interrupt only */
extern void    ad7732_irq_enable(void);
extern void    delay_ms(uint32_t ms);

/* ---------------- Register map ---------------- */
#define REG_REVISION        0x02
#define REG_CH_DATA(ch)     (0x08 + 2 * (ch))
#define REG_CH_SETUP(ch)    (0x28 + 2 * (ch))
#define REG_CH_CTIME(ch)    (0x30 + 2 * (ch))
#define REG_MODE(ch)        (0x38 + 2 * (ch))
#define COMM_READ           0x40

#define SETUP_ENABLE        0x08
#define SETUP_RNG_PM10V     0x00
#define CTIME_CHOP          0x80
#define MODE_CONT_CONV      0x20
#define MODE_DUMP           0x08
#define MODE_24BIT          0x02

/* ---------------- State shared with the ISR ---------------- */
typedef struct {
    uint32_t n;
    int32_t  min, max;
    int64_t  sum;
    uint8_t  flags;
} acc_t;

static acc_t             s_acc;
static volatile bool     s_run;
static volatile bool     s_have_latest;
static volatile int32_t  s_latest_raw;
static volatile uint8_t  s_latest_status;
static int32_t           s_zero_raw;

#if AD7732_WAVE_LEN > 0
static int32_t           s_wave[AD7732_WAVE_LEN];
static volatile uint16_t s_wave_idx = AD7732_WAVE_LEN;   /* idle */
#endif

/* ---------------- Low-level register access (init only) ---------------- */
static void wr8(uint8_t addr, uint8_t val)
{
    ad7732_cs(true);
    spi_xfer(addr & 0x3F);
    spi_xfer(val);
    ad7732_cs(false);
}

static uint8_t rd8(uint8_t addr)
{
    uint8_t v;
    ad7732_cs(true);
    spi_xfer(COMM_READ | (addr & 0x3F));
    v = spi_xfer(0x00);
    ad7732_cs(false);
    return v;
}

static void ad7732_reset(void)
{
    ad7732_cs(true);
    spi_xfer(0x00);                         /* 00h + 32 ones (datasheet) */
    for (int i = 0; i < 4; i++) spi_xfer(0xFF);
    ad7732_cs(false);
    delay_ms(2);
}

/* ---------------- Init ---------------- */
bool ad7732_init(void)
{
    s_run = false;
    s_have_latest = false;
    s_acc = (acc_t){0};

    ad7732_reset();

    if ((rd8(REG_REVISION) & 0x0F) != 0x04)       /* generic code of AD7732 */
        return false;

    wr8(REG_CH_SETUP(0), SETUP_ENABLE | SETUP_RNG_PM10V);   /* CH0 on, +/-10 V */
    wr8(REG_CH_SETUP(1), 0x00);                             /* CH1 off         */
    wr8(REG_CH_CTIME(0), CTIME_CHOP | (AD7732_FW & 0x7F));

    s_run = true;                                            /* arm the ISR     */
    wr8(REG_MODE(0), MODE_CONT_CONV | MODE_DUMP | MODE_24BIT);  /* start        */
    return true;
}

/* ---------------- RDY interrupt: read one conversion ---------------- */
void ad7732_rdy_isr(void)
{
    if (!s_run) return;

    uint8_t tx[5] = { COMM_READ | REG_CH_DATA(0), 0, 0, 0, 0 };  /* DIN low */
    uint8_t rx[5];

    ad7732_cs(true);
    spi_xfer_buf(tx, rx, 5);               /* cmd, then STATUS, D23-16, D15-8, D7-0 */
    ad7732_cs(false);

    uint8_t st = rx[1];
    if (st & 0x20) return;                 /* channel bits say CH1: not ours */

    int32_t raw = (int32_t)(((uint32_t)rx[2] << 16) |
                            ((uint32_t)rx[3] << 8)  |
                             (uint32_t)rx[4]) - 0x800000;

    if (s_acc.n == 0) { s_acc.min = raw; s_acc.max = raw; }
    else {
        if (raw < s_acc.min) s_acc.min = raw;
        if (raw > s_acc.max) s_acc.max = raw;
    }
    s_acc.sum   += raw;
    s_acc.n++;
    s_acc.flags |= st & (AD7732_CH_STATUS_OVR | AD7732_CH_STATUS_NOREF);

    s_latest_raw    = raw;
    s_latest_status = st;
    s_have_latest   = true;

#if AD7732_WAVE_LEN > 0
    if (s_wave_idx < AD7732_WAVE_LEN) s_wave[s_wave_idx++] = raw;
#endif
}

/* ---------------- Results ---------------- */
bool ad7732_take_stats(ad7732_stats_t *out)
{
    acc_t a;

    ad7732_irq_disable();
    a = s_acc;
    s_acc = (acc_t){0};
    ad7732_irq_enable();

    out->n     = a.n;
    out->flags = a.flags;
    if (a.n == 0) { out->raw_min = out->raw_max = out->raw_mean = 0; return false; }
    out->raw_min  = a.min;
    out->raw_max  = a.max;
    out->raw_mean = (int32_t)(a.sum / (int64_t)a.n);
    return true;
}

bool ad7732_get_latest(ad7732_sample_t *o)
{
    int32_t raw; uint8_t st; bool ok;

    ad7732_irq_disable();
    ok = s_have_latest; raw = s_latest_raw; st = s_latest_status;
    ad7732_irq_enable();
    if (!ok) return false;

    o->raw       = raw;
    o->status    = st;
    o->adc_v     = ad7732_raw_to_volts(raw);
    o->sensor_mv = ad7732_raw_to_sensor_mv(raw);
    o->psi       = ad7732_raw_to_psi(raw);
    return true;
}

double ad7732_raw_to_volts(int32_t raw)
{
    return (double)raw * (SIG_ADC_RANGE_V / 8388608.0);
}

double ad7732_raw_to_sensor_mv(int32_t raw)
{
    return ad7732_raw_to_volts(raw) / SIG_INA125_GAIN * 1000.0;
}

double ad7732_raw_to_psi(int32_t raw)
{
    return ad7732_raw_to_sensor_mv(raw - s_zero_raw) * (SIG_SENSOR_FS_PSI / SIG_SENSOR_FS_MV);
}

void ad7732_set_zero(int32_t raw) { s_zero_raw = raw; }

#if AD7732_WAVE_LEN > 0
void ad7732_wave_arm(void)  { s_wave_idx = 0; }
bool ad7732_wave_done(void) { return s_wave_idx >= AD7732_WAVE_LEN; }
const int32_t *ad7732_wave_data(void) { return s_wave; }
#endif
