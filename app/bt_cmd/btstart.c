#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

#include "bt_process_guard.h"

extern int aic_bt_core_start(void);

#ifdef CONFIG_AIC_BT_BT8858A_VELA_HCI
extern int aic_bt_vela_hci_start(void);
#endif

int btstart_main(int argc, char *argv[])
{
    int ret;
    int bluetoothd_count;

    if (argc > 1 && strcmp(argv[1], "--legacy") == 0)
    {
        ret = aic_bt_core_start();
        if (ret < 0)
        {
            fprintf(stderr, "Legacy BT start failed: %d\n", ret);
            return 1;
        }

        printf("Legacy BT started.\n");
        return 0;
    }

#ifdef CONFIG_AIC_BT_BT8858A_VELA_HCI
    ret = aic_bt_vela_hci_start();
    if (ret < 0)
    {
        fprintf(stderr, "AIC BT HCI start failed: %d\n", ret);
        return 1;
    }

    bluetoothd_count = bt_cmd_count_processes("bluetoothd");
    if (bluetoothd_count > 1)
    {
        fprintf(stderr, "AIC BT HCI: duplicate bluetoothd instances (%d); "
                "use btstop --force-daemon only for diagnostics, or reboot.\n",
                bluetoothd_count);
        return 1;
    }

    if (bluetoothd_count == 1)
    {
        printf("AIC BT HCI: bluetoothd already running (%d); do not run "
               "another \"bluetoothd &\"; run btle directly.\n",
               bluetoothd_count);
    }

    printf("AIC BT HCI ready on /dev/ttyHCI0.\n");
    return 0;
#else
    ret = aic_bt_core_start();
    if (ret < 0)
    {
        fprintf(stderr, "BT start failed: %d\n", ret);
        return 1;
    }

    printf("BT started.\n");
    return 0;
#endif
}
