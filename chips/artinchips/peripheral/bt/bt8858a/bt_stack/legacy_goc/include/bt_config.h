#ifndef __BT_CONFIG_H__
#define __BT_CONFIG_H__

//#define   RTOS   1
//#define    RTT   1
#define    NUTTX   1
//#define LINUX   1

#ifndef RTOS
#define RTOS 0
#endif

#ifndef RTT
#define RTT 0
#endif

#ifndef NUTTX
#define NUTTX 0
#endif

#ifndef LINUX
#define LINUX 0
#endif

#define openmode    1
#define runmode     1
#define comm_mode       1

#endif
