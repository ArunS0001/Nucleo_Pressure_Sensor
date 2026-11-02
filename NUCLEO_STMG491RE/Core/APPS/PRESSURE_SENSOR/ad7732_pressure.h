#ifndef AD7732_PRESSURE_H
#define AD7732_PRESSURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ======================= User configuration ======================= */
#define AD7732_MCLK_MHZ       6.144
/* Filter word, chopping on. Conv. time = (FW*128 + 248)/MCLK.
 *   FW=127: 2686 us (372 Hz)   FW=46: 999 us (1 kHz)   FW=22: 499 us (2 kHz)
 *   FW=8  :  207 us (4.8 kHz)  FW>=6 keeps all 24 bits (no missing codes)   */
#define AD7732_FW             22
#define AD7732_CONV_TIME_US   ((AD7732_FW * 128.0 + 248.0) / AD7732_MCLK_MHZ)

/* Raw-sample capture buffer (for dumping the waveform). 0 = disabled. */
#define AD7732_WAVE_LEN       256

/* Signal chain: 26PCDFG2G -> INA125 -> AD7732 (+/-10 V range) */
#define SIG_ADC_RANGE_V       10.0
#define SIG_INA125_RG_OHM     265.0                         /* (330||330)+100 */
#define SIG_INA125_GAIN       (4.0 + 60000.0 / SIG_INA125_RG_OHM)
#define SIG_SENSOR_FS_MV      100.0   /* nominal span at 10 V excitation */
#define SIG_SENSOR_FS_PSI     30.0
/* ================================================================== */

/* Bits in status / flags (AD7732 channel status register) */
#define AD7732_CH_STATUS_OVR     0x01   /* input outside nominal +/-10 V range */
#define AD7732_CH_STATUS_SIGN    0x02   /* 1 = negative input                  */
#define AD7732_CH_STATUS_NOREF   0x04   /* 1 = reference voltage missing       */
#define AD7732_CH_STATUS_RDY     0x08

/* Most recent conversion */
typedef struct {
    int32_t  raw;        /* code - 0x800000 (0 = 0 V)                 */
    uint8_t  status;     /* AD7732_CH_STATUS_* flags                  */
    double   adc_v;      /* voltage at AD7732 input (INA125 output)   */
    double   sensor_mv;  /* referred back to the bridge output        */
    double   psi;        /* nominal, zero-corrected                   */
} ad7732_sample_t;

/* Statistics over every conversion since the previous ad7732_take_stats() */
typedef struct {
    uint32_t n;          /* conversions in the window                 */
    int32_t  raw_min;
    int32_t  raw_max;
    int32_t  raw_mean;
    uint8_t  flags;      /* OR of OVR and NOREF seen in the window    */
} ad7732_stats_t;

/*
 * Reset the AD7732, check it responds, configure CH0 (+/-10 V, 24-bit,
 * chopping, FW = AD7732_FW) and start free-running conversion.
 * After this returns true, ONLY ad7732_rdy_isr() talks to the SPI bus.
 */
bool ad7732_init(void);

/* Call from the RDY-pin falling-edge interrupt (see ad7732_port_stm32.c). */
void ad7732_rdy_isr(void);

/* Copy and clear the window statistics. Returns false if no samples arrived. */
bool ad7732_take_stats(ad7732_stats_t *out);

/* Latest conversion (converted). Returns false if none yet. */
bool ad7732_get_latest(ad7732_sample_t *out);

/* Conversions. psi/zero apply the offset set by ad7732_set_zero(). */
double ad7732_raw_to_volts(int32_t raw);
double ad7732_raw_to_sensor_mv(int32_t raw);
double ad7732_raw_to_psi(int32_t raw);
void   ad7732_set_zero(int32_t raw);      /* call with the mean raw at 0 psi */

#if AD7732_WAVE_LEN > 0
void           ad7732_wave_arm(void);     /* start filling the buffer         */
bool           ad7732_wave_done(void);
const int32_t *ad7732_wave_data(void);    /* AD7732_WAVE_LEN raw samples       */
#endif

#ifdef __cplusplus
}
#endif

#endif /* AD7732_PRESSURE_H */
