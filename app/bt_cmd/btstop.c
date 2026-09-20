#include <nuttx/config.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bt_process_guard.h"

extern int aic_bt_core_stop(void);

#ifdef CONFIG_AIC_BT_BT8858A_VELA_HCI
extern int aic_bt_vela_hci_stop(void);
#endif

int btstop_main(int argc, char *argv[])
{
    int ret;
    int bluetoothd_count;
    int killed;
    bool force_daemon = false;

    if (argc > 1 && strcmp(argv[1], "--legacy") == 0)
    {
        ret = aic_bt_core_stop();
        if (ret < 0)
        {
            fprintf(stderr, "Legacy BT stop failed: %d\n", ret);
            return 1;
        }

        printf("Legacy BT stopped.\n");
        return 0;
    }

    if (argc > 1 && (strcmp(argv[1], "--force-daemon") == 0 ||
                     strcmp(argv[1], "--daemon") == 0))
    {
        force_daemon = true;
    }

#ifdef CONFIG_AIC_BT_BT8858A_VELA_HCI
    bluetoothd_count = bt_cmd_count_processes("bluetoothd");
    if (bluetoothd_count > 0 && !force_daemon)
    {
        printf("AIC BT HCI: bluetoothd is still running (%d); refuse to "
               "stop HCI to avoid a half-stopped Bluetooth service.\n",
               bluetoothd_count);
        printf("AIC BT HCI: stop active btle command with Ctrl+C, then keep "
               "bluetoothd alive for the next BLE operation.\n");
        printf("AIC BT HCI: use \"btstop --force-daemon\" only for daemon "
               "lifecycle diagnostics or before reboot.\n");
        return 1;
    }

    if (force_daemon)
    {
        killed = bt_cmd_terminate_processes("bluetoothd", 5000, 1000);
        if (killed > 0)
        {
            printf("AIC BT HCI: terminated %d bluetoothd instance(s).\n",
                   killed);
        }
        else if (killed < 0)
        {
            printf("AIC BT HCI: failed to terminate bluetoothd: %d; "
                   "keep HCI running to avoid leaving controller state "
                   "dirty.\n", killed);
            return 1;
        }
    }

    ret = aic_bt_vela_hci_stop();
    if (ret < 0)
    {
        fprintf(stderr, "AIC BT HCI stop failed: %d\n", ret);
        return 1;
    }

    printf("AIC BT HCI stopped.\n");
    return 0;
#else
    ret = aic_bt_core_stop();
    if (ret < 0)
    {
        fprintf(stderr, "BT stop failed: %d\n", ret);
        return 1;
    }

    printf("BT stopped.\n");
    return 0;
#endif
}
