#ifndef AP_H
#define AP_H

#include <stdbool.h>

extern bool g_is_ntp_synced;

void wifi_init_softap(void);

#endif // AP_H