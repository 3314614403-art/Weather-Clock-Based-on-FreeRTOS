#ifndef __WIFI_H__
#define __WIFI_H__

#define APP_VERSION "v1.0-ble"
/* Copy local_config.example.h to local_config.h and set local credentials. */
#include "local_config.h"

void wifi_init(void);
void wifi_wait_connect(void);


#endif /* __WIFI_H__ */
