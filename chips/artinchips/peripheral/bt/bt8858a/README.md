# BT8858A/AIC8800 BLE Porting Layout

This directory keeps two different Bluetooth paths separated:

- `bt_controller/hci_h4/`: current openVela/Zblue controller transport. It powers and configures the AIC controller, owns UART2 H4 traffic, and registers `/dev/ttyHCI0`.
- `bt_controller/vela_compat/`: small compatibility shims required by the openVela Bluetooth service when only BLE is enabled.
- `bt_stack/vela_ble/`: current Vela BLE host integration helpers. These files wrap Vela/Zblue APIs for local commands; they do not replace the Vela Bluetooth host stack.
  - `common/`: shared helper code used by the local BLE command tools.
  - `gap/`: GAP advertising helpers.
  - `gatt/`: GATT server lifecycle helpers.
- `bt_osal/`: legacy OS adaptation layer. Only the NuttX implementation is kept in the Vela tree.
- `bt_stack/legacy_goc/`: old vendor/GOC command stack code. It is not the current Vela BLE data path, but is kept for `btstart --legacy` / `btstop --legacy` and reference.
  - `include/`: legacy public API/types/config headers copied from the old stack.
  - `profiles/hid/`: legacy HID wrapper kept from the old GOC command stack.

Current BLE testing should use the Vela HCI path plus GAP/GATT helpers. The legacy GOC path should not be extended for new BLE features unless a legacy compatibility issue must be diagnosed.

Runtime lifecycle:

- Start the controller once with `btstart`, then start `bluetoothd &` once.
- Application-level start/stop should control GAP/GATT activity, such as advertising, connections, notifications, and scan sessions.
- Do not repeatedly kill and restart `bluetoothd` for normal BLE business logic. The current Vela bluetoothd lifecycle is not treated as a stable app-level toggle.
- `btstop --force-daemon` is reserved for diagnostics or reboot preparation. Default `btstop` refuses to stop HCI while `bluetoothd` is still alive, so it does not leave a half-stopped service behind.

## `nsh_lvgl/defconfig` Notes

The D13X Vela board uses AIC8800D40L, so the verified Bluetooth target is BLE
GAP/GATT. Some framework options still look broader than BLE because the current
openVela Bluetooth service and Zblue SAL are not completely split into pure-BLE
and BR/EDR-only build units.

- `CONFIG_AIC_TEST_ADC`, `CONFIG_DRIVERS_WIRELESS`, and `CONFIG_PIPES` are not
  BLE-specific requirements. `DRIVERS_WIRELESS` is selected by
  `AIC_BT_BT8858A_VELA_HCI`, and `PIPES` is selected by `NET_LOCAL`, so
  `savedefconfig` may omit their explicit lines while the generated `.config`
  still enables them. `AIC_TEST_ADC` is different: the current
  `app/Kconfig`/`app/Make.defs` do not source or build `app/testadc`, so adding
  `CONFIG_AIC_TEST_ADC=y` to this defconfig is not an effective BT change and
  will not make the test command part of the image unless the testadc app is
  wired into the board application Kconfig/Make.defs separately.

- `CONFIG_KVDB`, `CONFIG_KVDB_DIRECT`, and `CONFIG_UNQLITE` provide Bluetooth
  service storage support. The openVela Bluetooth service has a storage-method
  choice based on KVDB or UnQLite, and it stores adapter/device properties and
  bond-related state through that layer. `KVDB_DIRECT` keeps access local and
  avoids requiring a separate KVDB server process for this board. `UNQLITE` is
  the persistent database backend used by the KVDB path.

- `CONFIG_LIBUV_EXTENSION` is required by `CONFIG_BLUETOOTH`; the Bluetooth
  framework depends on both `LIBUV` and `LIBUV_EXTENSION`. The extension also
  provides Vela utility wrappers used by framework/service code.

- `CONFIG_MBEDTLS_THREADING_C` and `CONFIG_MBEDTLS_THREADING_PTHREAD` enable the
  Mbed TLS threading abstraction and back it with pthread mutexes. Bluetooth
  service, Zblue, libuv, and KVDB/UnQLite run in multiple tasks/threads, so the
  Mbed TLS global crypto state should use real locks. This is independent from
  `CONFIG_MBEDTLS_NET_C`; the BLE port does not explicitly change the Mbed TLS
  net socket helper setting.

- `CONFIG_NET_LOCAL` enables Unix domain sockets. It is needed when the Bluetooth
  framework uses socket IPC: `bluetoothd` and clients create `PF_LOCAL/AF_LOCAL`
  sockets under the local VFS path. `NET_LOCAL` also selects `PIPES`.

- `CONFIG_PTHREAD_MUTEX_TYPES` enables recursive/errorcheck pthread mutex types
  and the corresponding `pthread_mutexattr_settype()` APIs. UnQLite's threaded
  build and some Vela/Zblue profile code expect recursive mutex support, so this
  option prevents build or runtime gaps in those dependencies.

- `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=8192` raises the Zblue system workqueue
  stack from the small default. Zblue schedules connection, GATT, RPA, and other
  host callbacks through workqueues; 8192 keeps enough margin for the current
  BLE service path and avoids stack-sensitive crashes during GATT activity.

- `CONFIG_MBEDTLS_NET_C` and `CONFIG_MEDIA` are not kept as BLE-specific changes.
  `CONFIG_MBEDTLS_NET_C` controls Mbed TLS TCP/UDP socket helpers, which are not
  required by GAP/GATT. `CONFIG_MEDIA` is only needed by media profiles such as
  A2DP, and AIC8800D40L is currently treated as BLE-only.
