#ifndef JASZCZURHAL_IMPL_SHARED_FRAMEWORKS_CYW43_NAMESPACE_H
#define JASZCZURHAL_IMPL_SHARED_FRAMEWORKS_CYW43_NAMESPACE_H

/*
 * Symbol isolation for compiling the vendored driver beside a carrier-owned
 * cyw43-driver. Keep this list explicit: additions are reviewable ABI changes.
 */
#define cyw43_state jh_cyw43_state
#define cyw43_init jh_cyw43_init
#define cyw43_deinit jh_cyw43_deinit
#define cyw43_is_initialized jh_cyw43_is_initialized
#define cyw43_poll jh_cyw43_poll
#define cyw43_sleep jh_cyw43_sleep
#define cyw43_wifi_set_up jh_cyw43_wifi_set_up
#define cyw43_wifi_get_mac jh_cyw43_wifi_get_mac
#define cyw43_wifi_get_rssi jh_cyw43_wifi_get_rssi
#define cyw43_wifi_join jh_cyw43_wifi_join
#define cyw43_wifi_leave jh_cyw43_wifi_leave
#define cyw43_wifi_scan jh_cyw43_wifi_scan
#define cyw43_wifi_scan_active jh_cyw43_wifi_scan_active
#define cyw43_wifi_link_status jh_cyw43_wifi_link_status
#define cyw43_tcpip_link_status jh_cyw43_tcpip_link_status
#define cyw43_wifi_update_multicast_filter jh_cyw43_wifi_update_multicast_filter
#define cyw43_send_ethernet jh_cyw43_send_ethernet
#define cyw43_gpio_set jh_cyw43_gpio_set
#define cyw43_gpio_get jh_cyw43_gpio_get
#define cyw43_ll_init jh_cyw43_ll_init
#define cyw43_ll_deinit jh_cyw43_ll_deinit
#define cyw43_ll_process_packets jh_cyw43_ll_process_packets
#define cyw43_ll_bus_init jh_cyw43_ll_bus_init
#define cyw43_ll_bus_deinit jh_cyw43_ll_bus_deinit
#define cyw43_ll_bus_sleep jh_cyw43_ll_bus_sleep
#define cyw43_ll_bus_read_host_interrupt_pin                                   \
  jh_cyw43_ll_bus_read_host_interrupt_pin
#define cyw43_ll_bus_wait_for_high_water_mark                                  \
  jh_cyw43_ll_bus_wait_for_high_water_mark
#define cyw43_ll_bus_transfer jh_cyw43_ll_bus_transfer
#define cyw43_cb_tcpip_init jh_cyw43_cb_tcpip_init
#define cyw43_cb_tcpip_deinit jh_cyw43_cb_tcpip_deinit
#define cyw43_cb_tcpip_set_link_up jh_cyw43_cb_tcpip_set_link_up
#define cyw43_cb_tcpip_set_link_down jh_cyw43_cb_tcpip_set_link_down
#define cyw43_cb_process_ethernet jh_cyw43_cb_process_ethernet

#endif
