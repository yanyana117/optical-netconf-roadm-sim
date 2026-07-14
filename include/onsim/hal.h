/* Hardware abstraction layer: the C API a management-plane process uses to
 * drive the (simulated) optical hardware. Mirrors the pattern of vendor SDKs:
 * opaque handles, integer status codes, no C++ types across the boundary. */
#ifndef ONSIM_HAL_H
#define ONSIM_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onsim_device onsim_device; /* opaque */

typedef enum {
    ONSIM_OK = 0,
    ONSIM_ERR_INVALID_ARG = 1,
    ONSIM_ERR_COLLISION = 2,
    ONSIM_ERR_NOT_FOUND = 3,
    ONSIM_ERR_BUSY = 4 /* e.g. reconfig attempted while admin-up */
} onsim_status;

typedef struct {
    int enabled;            /* 0/1 */
    double input_power_dbm;
    double output_power_dbm;
    int los_alarm;          /* 0/1 */
} onsim_port_state;

typedef struct {
    int admin_up;           /* 0/1 */
    int line_rate_gbps;     /* 100 / 200 / 400 */
    int modulation;         /* 0=QPSK 1=8QAM 2=16QAM */
    double osnr_db;
    double pre_fec_ber;
    int ber_degrade_alarm;  /* 0/1 */
} onsim_xpdr_state;

/* Device lifecycle. A device bundles one ROADM (N degrees) + one transponder. */
onsim_device* onsim_create(int degrees, uint64_t seed);
void onsim_destroy(onsim_device* d);

/* ROADM provisioning */
onsim_status onsim_xc_add(onsim_device* d, const char* name, int in_port,
                          int out_port, int channel);
onsim_status onsim_xc_delete(onsim_device* d, const char* name);
onsim_status onsim_port_enable(onsim_device* d, int port, int enabled);
onsim_status onsim_port_set_input_power(onsim_device* d, int port, double dbm);
onsim_status onsim_port_get(onsim_device* d, int port, onsim_port_state* out);

/* Transponder provisioning */
onsim_status onsim_xpdr_set_admin(onsim_device* d, int up);
onsim_status onsim_xpdr_set_rate(onsim_device* d, int gbps);
onsim_status onsim_xpdr_set_modulation(onsim_device* d, int modulation);
onsim_status onsim_xpdr_set_osnr(onsim_device* d, double db);
onsim_status onsim_xpdr_get(onsim_device* d, onsim_xpdr_state* out);

/* Advance simulated time by one step (power/OSNR drift, alarm evaluation). */
void onsim_tick(onsim_device* d);

/* Human-readable description of the last error on this device. */
const char* onsim_last_error(onsim_device* d);

#ifdef __cplusplus
}
#endif

#endif /* ONSIM_HAL_H */
