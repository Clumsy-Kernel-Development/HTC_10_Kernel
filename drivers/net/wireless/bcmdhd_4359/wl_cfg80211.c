/*
 * Linux cfg80211 driver
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: wl_cfg80211.c 620987 2016-02-24 18:25:52Z $
 */
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <linux/kernel.h>

#include <bcmutils.h>
#include <bcmwifi_channels.h>
#include <bcmendian.h>
#include <proto/ethernet.h>
#include <proto/802.11.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>

#include <proto/ethernet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/wait.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>

#include <wlioctl.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <wl_cfgp2p.h>
#include <wl_android.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <dhd_cfg80211.h>
#include <dhd_bus.h>
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif 

#if defined(WL_VENDOR_EXT_SUPPORT)
#include <wl_cfgvendor.h>
#endif 

#ifdef WL_NAN
#include <wl_cfgnan.h>
#endif 

#ifdef PROP_TXSTATUS
#include <dhd_wlfc.h>
#endif

#include <linux/htc_flags.h>

#ifdef BCMPCIE
#include <dhd_flowring.h>
#endif

#ifdef WL11U
#if !defined(WL_ENABLE_P2P_IF) && !defined(WL_CFG80211_P2P_DEV_IF)
#error You should enable 'WL_ENABLE_P2P_IF' or 'WL_CFG80211_P2P_DEV_IF' \
	according to Kernel version and is supported only in Android-JB
#endif 
#endif 

#ifdef BCMWAPI_WPI
#ifndef IW_ENCODE_ALG_SM4
#define IW_ENCODE_ALG_SM4 0x20
#endif

#ifndef IW_AUTH_WAPI_ENABLED
#define IW_AUTH_WAPI_ENABLED 0x20
#endif

#ifndef IW_AUTH_WAPI_VERSION_1
#define IW_AUTH_WAPI_VERSION_1  0x00000008
#endif

#ifndef IW_AUTH_CIPHER_SMS4
#define IW_AUTH_CIPHER_SMS4     0x00000020
#endif

#ifndef IW_AUTH_KEY_MGMT_WAPI_PSK
#define IW_AUTH_KEY_MGMT_WAPI_PSK 4
#endif

#ifndef IW_AUTH_KEY_MGMT_WAPI_CERT
#define IW_AUTH_KEY_MGMT_WAPI_CERT 8
#endif
#endif 

#ifdef BCMWAPI_WPI
#define IW_WSEC_ENABLED(wsec)   ((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED | SMS4_ENABLED))
#else 
#define IW_WSEC_ENABLED(wsec)   ((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))
#endif 

static struct device *cfg80211_parent_dev = NULL;
static struct bcm_cfg80211 *g_bcm_cfg = NULL;
u32 wl_dbg_level = WL_DBG_ERR;

#define MAX_WAIT_TIME 1500
#ifdef WLAIBSS_MCHAN
#define IBSS_IF_NAME "ibss%d"
#endif 

#ifdef VSDB
#define DEFAULT_SLEEP_TIME_VSDB		120
#define OFF_CHAN_TIME_THRESHOLD_MS	200
#define AF_RETRY_DELAY_TIME			40

#define WL_AF_TX_KEEP_PRI_CONNECTION_VSDB(cfg)	\
	do {	\
		if (wl_get_drv_status(cfg, CONNECTED, bcmcfg_to_prmry_ndev(cfg)) ||	\
			wl_get_drv_status(cfg, CONNECTING, bcmcfg_to_prmry_ndev(cfg))) {	\
			OSL_SLEEP(DEFAULT_SLEEP_TIME_VSDB);			\
		}	\
	} while (0)
#else 
#define WL_AF_TX_KEEP_PRI_CONNECTION_VSDB(cfg)
#endif 

#ifdef WL_CFG80211_SYNC_GON
#define WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg) \
	(wl_get_drv_status_all(cfg, SENDING_ACT_FRM) || \
		wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM_LISTEN))
#else
#define WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg) wl_get_drv_status_all(cfg, SENDING_ACT_FRM)
#endif 

#define DNGL_FUNC(func, parameters) func parameters
#define COEX_DHCP

#define WLAN_EID_SSID	0
#define CH_MIN_5G_CHANNEL 34
#define CH_MIN_2G_CHANNEL 1
#define ACTIVE_SCAN 1
#define PASSIVE_SCAN 0
#ifdef WLAIBSS
enum abiss_event_type {
	AIBSS_EVENT_TXFAIL
};
#endif

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
#define BCM_SET_LIST_FIRST_ENTRY(entry, ptr, type, member) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
(entry) = list_first_entry((ptr), type, member); \
_Pragma("GCC diagnostic pop") \

#define BCM_SET_CONTAINER_OF(entry, ptr, type, member) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
entry = container_of((ptr), type, member); \
_Pragma("GCC diagnostic pop") \

#else
#define BCM_SET_LIST_FIRST_ENTRY(entry, ptr, type, member) \
(entry) = list_first_entry((ptr), type, member); \

#define BCM_SET_CONTAINER_OF(entry, ptr, type, member) \
entry = container_of((ptr), type, member); \

#endif 

enum rmc_event_type {
	RMC_EVENT_NONE,
	RMC_EVENT_LEADER_CHECK_FAIL
};

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
#endif
static const struct ieee80211_regdomain brcm_regdom = {
	.n_reg_rules = 4,
	.alpha2 =  "99",
	.reg_rules = {
		
		REG_RULE(2412-10, 2472+10, 40, 6, 20, 0),
		
		REG_RULE(2484-10, 2484+10, 20, 6, 20, 0),
		
		REG_RULE(5150-10, 5350+10, 40, 6, 20, 0),
		
		REG_RULE(5470-10, 5850+10, 40, 6, 20, 0), }
};
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
	(defined(WL_IFACE_COMB_NUM_CHANNELS) || defined(WL_CFG80211_P2P_DEV_IF))
static const struct ieee80211_iface_limit common_if_limits[] = {
	{
	.max = 2,
	.types = BIT(NL80211_IFTYPE_AP),
	},
	{
#ifdef WL_ENABLE_P2P_IF
	.max = 3,
#else
	.max = 2,
#endif 
	.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
	.max = 2,
	.types = BIT(NL80211_IFTYPE_P2P_GO) | BIT(NL80211_IFTYPE_P2P_CLIENT),
	},
#if defined(WL_CFG80211_P2P_DEV_IF)
	{
	.max = 1,
	.types = BIT(NL80211_IFTYPE_P2P_DEVICE),
	},
#endif 
	{
	.max = 1,
	.types = BIT(NL80211_IFTYPE_ADHOC),
	},
};
#ifdef BCM4330_CHIP
#define NUM_DIFF_CHANNELS 1
#else
#define NUM_DIFF_CHANNELS 2
#endif
static const struct ieee80211_iface_combination
common_iface_combinations[] = {
	{
	.num_different_channels = NUM_DIFF_CHANNELS,
	.max_interfaces = 4,
	.limits = common_if_limits,
	.n_limits = ARRAY_SIZE(common_if_limits),
	},
};
#endif 

#define WPS_ID_CONFIG_METHODS     0x1008
#define WPS_ID_REQ_TYPE           0x103A
#define WPS_ID_DEVICE_NAME        0x1011
#define WPS_ID_VERSION            0x104A
#define WPS_ID_DEVICE_PWD_ID      0x1012
#define WPS_ID_REQ_DEV_TYPE       0x106A
#define WPS_ID_SELECTED_REGISTRAR_CONFIG_METHODS 0x1053
#define WPS_ID_PRIM_DEV_TYPE      0x1054

#define DEV_PW_DEFAULT 0x0000
#define DEV_PW_USER_SPECIFIED 0x0001,
#define DEV_PW_MACHINE_SPECIFIED 0x0002
#define DEV_PW_REKEY 0x0003
#define DEV_PW_PUSHBUTTON 0x0004
#define DEV_PW_REGISTRAR_SPECIFIED 0x0005

#define WPS_CONFIG_USBA 0x0001
#define WPS_CONFIG_ETHERNET 0x0002
#define WPS_CONFIG_LABEL 0x0004
#define WPS_CONFIG_DISPLAY 0x0008
#define WPS_CONFIG_EXT_NFC_TOKEN 0x0010
#define WPS_CONFIG_INT_NFC_TOKEN 0x0020
#define WPS_CONFIG_NFC_INTERFACE 0x0040
#define WPS_CONFIG_PUSHBUTTON 0x0080
#define WPS_CONFIG_KEYPAD 0x0100
#define WPS_CONFIG_VIRT_PUSHBUTTON 0x0280
#define WPS_CONFIG_PHY_PUSHBUTTON 0x0480
#define WPS_CONFIG_VIRT_DISPLAY 0x2008
#define WPS_CONFIG_PHY_DISPLAY 0x4008

#define PM_BLOCK 1
#define PM_ENABLE 0

#ifdef BCMCCX
#ifndef WLAN_AKM_SUITE_CCKM
#define WLAN_AKM_SUITE_CCKM 0x00409600
#endif
#define DOT11_LEAP_AUTH	0x80 
#endif 

#define WL_AKM_SUITE_SHA256_1X  0x000FAC05
#define WL_AKM_SUITE_SHA256_PSK 0x000FAC06

#ifndef IBSS_COALESCE_ALLOWED
#define IBSS_COALESCE_ALLOWED 0
#endif

#ifndef IBSS_INITIAL_SCAN_ALLOWED
#define IBSS_INITIAL_SCAN_ALLOWED 0
#endif

#define CUSTOM_RETRY_MASK 0xff000000 
#define LONG_LISTEN_TIME 2000

#ifdef CUSTOMER_HW_ONE
extern int dhd_tcpack_suppress_set(dhd_pub_t *dhdp, uint8 on);
enum {
	WIFI_LOADING_LOW = 0,
	WIFI_LOADING_HIGH,
	WIFI_LOADING_TCPACK_SUPPRESS_ON
};
#if defined(SOFTAP)
bool dhd_APUP = false;
int sta_event_sent = 0;
static void wl_cfg80211_hotspot_event_process(struct net_device *ndev,
	const wl_event_msg_t *e, void *data);

static s32
wl_cfg80211_custom_event_process(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
        const wl_event_msg_t *e, void *data);
#endif 
#ifdef SUPPORT_2G_HT40
static int hotspot_bw = 40;
#endif 
#define CHANNEL_2G_MAX 14
bool disc_called = false;
bool scan_one = false;
#endif 

static s32 wl_frame_get_mgmt(u16 fc, const struct ether_addr *da,
	const struct ether_addr *sa, const struct ether_addr *bssid,
	u8 **pheader, u32 *body_len, u8 *pbody);
static s32 __wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request,
	struct cfg80211_ssid *this_ssid);
#if defined(WL_CFG80211_P2P_DEV_IF)
static s32
wl_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request);
#else
static s32
wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request);
#endif 
static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed);
#ifdef WLAIBSS_MCHAN
static bcm_struct_cfgdev* bcm_cfg80211_add_ibss_if(struct wiphy *wiphy, char *name);
static s32 bcm_cfg80211_del_ibss_if(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev);
#endif 
static s32 wl_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_ibss_params *params);
static s32 wl_cfg80211_leave_ibss(struct wiphy *wiphy,
	struct net_device *dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_get_station(struct wiphy *wiphy,
	struct net_device *dev, const u8 *mac,
	struct station_info *sinfo);
#else
static s32 wl_cfg80211_get_station(struct wiphy *wiphy,
	struct net_device *dev, u8 *mac,
	struct station_info *sinfo);
#endif
static s32 wl_cfg80211_set_power_mgmt(struct wiphy *wiphy,
	struct net_device *dev, bool enabled,
	s32 timeout);
static int wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
	u16 reason_code);
#if defined(WL_CFG80211_P2P_DEV_IF)
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
	enum nl80211_tx_power_setting type, s32 mbm);
#else
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy,
	enum nl80211_tx_power_setting type, s32 dbm);
#endif 
#if defined(WL_CFG80211_P2P_DEV_IF)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy,
	struct wireless_dev *wdev, s32 *dbm);
#else
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm);
#endif 
static s32 wl_cfg80211_config_default_key(struct wiphy *wiphy,
	struct net_device *dev,
	u8 key_idx, bool unicast, bool multicast);
static s32 wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr,
	struct key_params *params);
static s32 wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr);
static s32 wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr,
	void *cookie, void (*callback) (void *cookie,
	struct key_params *params));
static s32 wl_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
	struct net_device *dev,	u8 key_idx);
static s32 wl_cfg80211_resume(struct wiphy *wiphy);
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	2, 0))
static s32 wl_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
	bcm_struct_cfgdev *cfgdev, u64 cookie);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_del_station(struct wiphy *wiphy,
	struct net_device *ndev, const u8* mac_addr);
#else
static s32 wl_cfg80211_del_station(struct wiphy *wiphy,
	struct net_device *ndev, u8* mac_addr);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_change_station(struct wiphy *wiphy,
	struct net_device *dev, const u8 *mac, struct station_parameters *params);
#else
static s32 wl_cfg80211_change_station(struct wiphy *wiphy,
	struct net_device *dev, u8 *mac, struct station_parameters *params);
#endif
#endif 
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)
static s32 wl_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow);
#else
static s32 wl_cfg80211_suspend(struct wiphy *wiphy);
#endif
static s32 wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_flush_pmksa(struct wiphy *wiphy,
	struct net_device *dev);
static void wl_cfg80211_scan_abort(struct bcm_cfg80211 *cfg);
static void wl_cfg80211_cancel_scan(struct bcm_cfg80211 *cfg);
static s32 wl_notify_escan_complete(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, bool aborted, bool fw_abort);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0)) || defined(WL_COMPAT_WIRELESS)
#if (defined(CONFIG_ARCH_MSM) && defined(TDLS_MGMT_VERSION2)) || (LINUX_VERSION_CODE < \
	KERNEL_VERSION(3, 16, 0) && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
static s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *data, size_t len);
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)) && \
		(LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)))
static s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *data, size_t len);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
static s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, bool initiator, const u8 *data, size_t len);
#else
static s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code, const u8 *data,
	size_t len);
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, enum nl80211_tdls_operation oper);
#else
static s32 wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, enum nl80211_tdls_operation oper);
#endif
#endif 
#ifdef WL_SCHED_SCAN
static int wl_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev);
#endif
#if defined(WL_VIRTUAL_APSTA) || defined(DUAL_STA_STATIC_IF)
bcm_struct_cfgdev*
wl_cfg80211_create_iface(struct wiphy *wiphy, enum nl80211_iftype
		 iface_type, u8 *mac_addr, const char *name);
s32
wl_cfg80211_del_iface(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev);
#endif 

s32 wl_cfg80211_interface_ops(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr);
s32 wl_cfg80211_add_del_bss(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr);
chanspec_t wl_chspec_driver_to_host(chanspec_t chanspec);
chanspec_t wl_chspec_host_to_driver(chanspec_t chanspec);
#ifdef WL11ULB
static s32 wl_cfg80211_get_ulb_bw(struct wireless_dev *wdev);
static chanspec_t wl_cfg80211_ulb_get_min_bw_chspec(struct wireless_dev *wdev, s32 bssidx);
static s32 wl_cfg80211_ulbbw_to_ulbchspec(u32 ulb_bw);
#else
static inline chanspec_t wl_cfg80211_ulb_get_min_bw_chspec(
		struct wireless_dev *wdev, s32 bssidx)
{
	return WL_CHANSPEC_BW_20;
}
#endif 

static s32 wl_create_event_handler(struct bcm_cfg80211 *cfg);
static void wl_destroy_event_handler(struct bcm_cfg80211 *cfg);
static s32 wl_event_handler(void *data);
static void wl_init_eq(struct bcm_cfg80211 *cfg);
static void wl_flush_eq(struct bcm_cfg80211 *cfg);
static unsigned long wl_lock_eq(struct bcm_cfg80211 *cfg);
static void wl_unlock_eq(struct bcm_cfg80211 *cfg, unsigned long flags);
static void wl_init_eq_lock(struct bcm_cfg80211 *cfg);
static void wl_init_event_handler(struct bcm_cfg80211 *cfg);
static struct wl_event_q *wl_deq_event(struct bcm_cfg80211 *cfg);
static s32 wl_enq_event(struct bcm_cfg80211 *cfg, struct net_device *ndev, u32 type,
	const wl_event_msg_t *msg, void *data);
static void wl_put_event(struct wl_event_q *e);
static void wl_wakeup_event(struct bcm_cfg80211 *cfg);
static s32 wl_notify_connect_status_ap(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
static s32 wl_notify_connect_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static s32 wl_notify_roaming_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static s32 wl_notify_scan_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
static s32 wl_bss_connect_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data, bool completed);
static s32 wl_bss_roaming_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
static s32 wl_notify_mic_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#ifdef BT_WIFI_HANDOVER
static s32 wl_notify_bt_wifi_handover_req(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
#endif 
#ifdef WL_SCHED_SCAN
static s32
wl_notify_sched_scan_results(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
#endif 
#ifdef PNO_SUPPORT
static s32 wl_notify_pfn_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif 
#ifdef GSCAN_SUPPORT
static s32 wl_notify_gscan_event(struct bcm_cfg80211 *wl, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
static s32 wl_handle_roam_exp_event(struct bcm_cfg80211 *wl, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif 
static s32 wl_handle_rssi_monitor_event(struct bcm_cfg80211 *wl, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
static s32 wl_notifier_change_state(struct bcm_cfg80211 *cfg, struct net_info *_net_info,
	enum wl_status state, bool set);
#ifdef DHD_LOSSLESS_ROAMING
static s32 wl_notify_roam_prep_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static void wl_del_roam_timeout(struct bcm_cfg80211 *cfg);
#endif 
#ifdef WL_SDO
static s32 wl_svc_resp_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
static s32 wl_notify_device_discovery(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif

#ifdef WLTDLS
static s32 wl_tdls_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif 
static void wl_cfg80211_clear_parent_dev(void);

static s32 wl_set_frag(struct net_device *dev, u32 frag_threshold);
static s32 wl_set_rts(struct net_device *dev, u32 frag_threshold);
static s32 wl_set_retry(struct net_device *dev, u32 retry, bool l);

static s32 wl_update_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, const void *data, s32 item);
static void *wl_read_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 item);
static void wl_init_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev);

static s32 wl_set_wpa_version(struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_set_auth_type(struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_set_set_cipher(struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_set_key_mgmt(struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_set_set_sharedkey(struct net_device *dev,
	struct cfg80211_connect_params *sme);
#ifdef BCMWAPI_WPI
static s32 wl_set_set_wapi_ie(struct net_device *dev,
        struct cfg80211_connect_params *sme);
#endif
static s32 wl_get_assoc_ies(struct bcm_cfg80211 *cfg, struct net_device *ndev);
static s32 wl_ch_to_chanspec(struct net_device *dev, int ch,
	struct wl_join_params *join_params, size_t *join_params_size);
void wl_cfg80211_clear_security(struct bcm_cfg80211 *cfg);

static void wl_rst_ie(struct bcm_cfg80211 *cfg);
static __used s32 wl_add_ie(struct bcm_cfg80211 *cfg, u8 t, u8 l, u8 *v);
static void wl_update_hidden_ap_ie(struct wl_bss_info *bi, const u8 *ie_stream, u32 *ie_size,
	bool roam);
static s32 wl_mrg_ie(struct bcm_cfg80211 *cfg, u8 *ie_stream, u16 ie_size);
static s32 wl_cp_ie(struct bcm_cfg80211 *cfg, u8 *dst, u16 dst_size);
static u32 wl_get_ielen(struct bcm_cfg80211 *cfg);
#ifdef MFP
static int wl_cfg80211_get_rsn_capa(bcm_tlv_t *wpa2ie, u8* capa);
#endif

#ifdef WL11U
bcm_tlv_t *
wl_cfg80211_find_interworking_ie(u8 *parse, u32 len);
static s32
wl_cfg80211_add_iw_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx, s32 pktflag,
            uint8 ie_id, uint8 *data, uint8 data_len);
#endif 

static s32 wl_setup_wiphy(struct wireless_dev *wdev, struct device *dev, void *data);
static void wl_free_wdev(struct bcm_cfg80211 *cfg);
#ifdef CONFIG_CFG80211_INTERNAL_REGDB
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
static int
#else
static void
#endif 
wl_cfg80211_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request);
#endif 

static s32 wl_inform_bss(struct bcm_cfg80211 *cfg);
static s32 wl_inform_single_bss(struct bcm_cfg80211 *cfg, struct wl_bss_info *bi, bool roam);
static s32 wl_update_bss_info(struct bcm_cfg80211 *cfg, struct net_device *ndev, bool roam);
static chanspec_t wl_cfg80211_get_shared_freq(struct wiphy *wiphy);
s32 wl_cfg80211_channel_to_freq(u32 channel);

#if defined(DHCP_SCAN_SUPPRESS)
static void wl_cfg80211_work_handler(struct work_struct *work);
static void wl_cfg80211_scan_supp_timerfunc(ulong data);
#endif 

static void wl_cfg80211_work_handler(struct work_struct *work);
static s32 wl_add_keyext(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, const u8 *mac_addr,
	struct key_params *params);
static void swap_key_from_BE(struct wl_wsec_key *key);
static void swap_key_to_BE(struct wl_wsec_key *key);

static s32 wl_init_priv_mem(struct bcm_cfg80211 *cfg);
static void wl_deinit_priv_mem(struct bcm_cfg80211 *cfg);

static void wl_delay(u32 ms);

static bool wl_is_ibssmode(struct bcm_cfg80211 *cfg, struct net_device *ndev);
static __used bool wl_is_ibssstarter(struct bcm_cfg80211 *cfg);

static s32 __wl_cfg80211_up(struct bcm_cfg80211 *cfg);
static s32 __wl_cfg80211_down(struct bcm_cfg80211 *cfg);
static bool wl_is_linkdown(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e);
static bool wl_is_linkup(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e,
	struct net_device *ndev);
static bool wl_is_nonetwork(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e);
static void wl_link_up(struct bcm_cfg80211 *cfg);
static void wl_link_down(struct bcm_cfg80211 *cfg);
static s32 wl_config_ifmode(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 iftype);
static void wl_init_conf(struct wl_conf *conf);
static s32 wl_cfg80211_handle_ifdel(struct bcm_cfg80211 *cfg, wl_if_event_info *if_event_info,
	struct net_device* ndev);

int wl_cfg80211_get_ioctl_version(void);

static __used u32 wl_find_msb(u16 bit16);

static int wl_setup_rfkill(struct bcm_cfg80211 *cfg, bool setup);
static int wl_rfkill_set(void *data, bool blocked);
#ifdef DEBUGFS_CFG80211
static s32 wl_setup_debugfs(struct bcm_cfg80211 *cfg);
static s32 wl_free_debugfs(struct bcm_cfg80211 *cfg);
#endif

static wl_scan_params_t *wl_cfg80211_scan_alloc_params(int channel,
	int nprobes, int *out_params_size);
static bool check_dev_role_integrity(struct bcm_cfg80211 *cfg, u32 dev_role);

#ifdef WL_CFG80211_ACL
static int wl_cfg80211_set_mac_acl(struct wiphy *wiphy, struct net_device *cfgdev,
	const struct cfg80211_acl_data *acl);
#endif 

int dhd_add_monitor(char *name, struct net_device **new_ndev);
int dhd_del_monitor(struct net_device *ndev);
int dhd_monitor_init(void *dhd_pub);
int dhd_monitor_uninit(void);
int dhd_start_xmit(struct sk_buff *skb, struct net_device *net);


static int wl_cfg80211_delayed_roam(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const struct ether_addr *bssid);

#ifdef CUSTOMER_HW_ONE
extern int sta_connected;
#endif 

#ifdef WL_SDO
s32 wl_cfg80211_sdo_init(struct bcm_cfg80211 *cfg);
s32 wl_cfg80211_sdo_deinit(struct bcm_cfg80211 *cfg);
#define MAX_SDO_PROTO 5
wl_sdo_proto_t wl_sdo_protos [] = {
	{ "all", SVC_RPOTYPE_ALL },
	{ "upnp", SVC_RPOTYPE_UPNP },
	{ "bonjour", SVC_RPOTYPE_BONJOUR },
	{ "wsd", SVC_RPOTYPE_WSD },
	{ "vendor", SVC_RPOTYPE_VENDOR },
};
#endif
static int bw2cap[] = { 0, 0, WLC_BW_CAP_20MHZ, WLC_BW_CAP_40MHZ, WLC_BW_CAP_80MHZ,
	WLC_BW_CAP_160MHZ, WLC_BW_CAP_160MHZ };

#ifdef RSSI_OFFSET
static s32 wl_rssi_offset(s32 rssi)
{
	rssi += RSSI_OFFSET;
	if (rssi > 0)
		rssi = 0;
	return rssi;
}
#else
#define wl_rssi_offset(x)	x
#endif

#define IS_WPA_AKM(akm) ((akm) == RSN_AKM_NONE || 			\
				 (akm) == RSN_AKM_UNSPECIFIED || 	\
				 (akm) == RSN_AKM_PSK)


extern int dhd_wait_pend8021x(struct net_device *dev);
#ifdef PROP_TXSTATUS_VSDB
extern int disable_proptx;
#endif 


extern int passive_channel_skip;

static s32
wl_csa_complete_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0)) && (LINUX_VERSION_CODE <= (3, 7, \
	0)))
struct chan_info {
	int freq;
	int chan_type;
};
#endif


#if (WL_DBG_LEVEL > 0)
#define WL_DBG_ESTR_MAX	50
static s8 wl_dbg_estr[][WL_DBG_ESTR_MAX] = {
	"SET_SSID", "JOIN", "START", "AUTH", "AUTH_IND",
	"DEAUTH", "DEAUTH_IND", "ASSOC", "ASSOC_IND", "REASSOC",
	"REASSOC_IND", "DISASSOC", "DISASSOC_IND", "QUIET_START", "QUIET_END",
	"BEACON_RX", "LINK", "MIC_ERROR", "NDIS_LINK", "ROAM",
	"TXFAIL", "PMKID_CACHE", "RETROGRADE_TSF", "PRUNE", "AUTOAUTH",
	"EAPOL_MSG", "SCAN_COMPLETE", "ADDTS_IND", "DELTS_IND", "BCNSENT_IND",
	"BCNRX_MSG", "BCNLOST_MSG", "ROAM_PREP", "PFN_NET_FOUND",
	"PFN_NET_LOST",
	"RESET_COMPLETE", "JOIN_START", "ROAM_START", "ASSOC_START",
	"IBSS_ASSOC",
	"RADIO", "PSM_WATCHDOG", "WLC_E_CCX_ASSOC_START", "WLC_E_CCX_ASSOC_ABORT",
	"PROBREQ_MSG",
	"SCAN_CONFIRM_IND", "PSK_SUP", "COUNTRY_CODE_CHANGED",
	"EXCEEDED_MEDIUM_TIME", "ICV_ERROR",
	"UNICAST_DECODE_ERROR", "MULTICAST_DECODE_ERROR", "TRACE",
	"WLC_E_BTA_HCI_EVENT", "IF", "WLC_E_P2P_DISC_LISTEN_COMPLETE",
	"RSSI", "PFN_SCAN_COMPLETE", "WLC_E_EXTLOG_MSG",
	"ACTION_FRAME", "ACTION_FRAME_COMPLETE", "WLC_E_PRE_ASSOC_IND",
	"WLC_E_PRE_REASSOC_IND", "WLC_E_CHANNEL_ADOPTED", "WLC_E_AP_STARTED",
	"WLC_E_DFS_AP_STOP", "WLC_E_DFS_AP_RESUME", "WLC_E_WAI_STA_EVENT",
	"WLC_E_WAI_MSG", "WLC_E_ESCAN_RESULT", "WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE",
	"WLC_E_PROBRESP_MSG", "WLC_E_P2P_PROBREQ_MSG", "WLC_E_DCS_REQUEST", "WLC_E_FIFO_CREDIT_MAP",
	"WLC_E_ACTION_FRAME_RX", "WLC_E_WAKE_EVENT", "WLC_E_RM_COMPLETE"
};
#endif				

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define RATE_TO_BASE100KBPS(rate)   (((rate) * 10) / 2)
#define RATETAB_ENT(_rateid, _flags) \
	{								\
		.bitrate	= RATE_TO_BASE100KBPS(_rateid),     \
		.hw_value	= (_rateid),			    \
		.flags	  = (_flags),			     \
	}

static struct ieee80211_rate __wl_rates[] = {
	RATETAB_ENT(DOT11_RATE_1M, 0),
	RATETAB_ENT(DOT11_RATE_2M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_5M5, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_11M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_6M, 0),
	RATETAB_ENT(DOT11_RATE_9M, 0),
	RATETAB_ENT(DOT11_RATE_12M, 0),
	RATETAB_ENT(DOT11_RATE_18M, 0),
	RATETAB_ENT(DOT11_RATE_24M, 0),
	RATETAB_ENT(DOT11_RATE_36M, 0),
	RATETAB_ENT(DOT11_RATE_48M, 0),
	RATETAB_ENT(DOT11_RATE_54M, 0)
};

#define wl_a_rates		(__wl_rates + 4)
#define wl_a_rates_size	8
#define wl_g_rates		(__wl_rates + 0)
#define wl_g_rates_size	12

static struct ieee80211_channel __wl_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0)
};

static struct ieee80211_channel __wl_5ghz_a_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(38, 0), CHAN5G(40, 0),
	CHAN5G(42, 0), CHAN5G(44, 0),
	CHAN5G(46, 0), CHAN5G(48, 0),
	CHAN5G(52, 0), CHAN5G(56, 0),
	CHAN5G(60, 0), CHAN5G(64, 0),
	CHAN5G(100, 0), CHAN5G(104, 0),
	CHAN5G(108, 0), CHAN5G(112, 0),
	CHAN5G(116, 0), CHAN5G(120, 0),
	CHAN5G(124, 0), CHAN5G(128, 0),
	CHAN5G(132, 0), CHAN5G(136, 0),
	CHAN5G(140, 0), CHAN5G(144, 0),
	CHAN5G(149, 0), CHAN5G(153, 0),
	CHAN5G(157, 0), CHAN5G(161, 0),
	CHAN5G(165, 0)
};

static struct ieee80211_supported_band __wl_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,
	.channels = __wl_2ghz_channels,
	.n_channels = ARRAY_SIZE(__wl_2ghz_channels),
	.bitrates = wl_g_rates,
	.n_bitrates = wl_g_rates_size
};

static struct ieee80211_supported_band __wl_band_5ghz_a = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = __wl_5ghz_a_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_a_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size
};

static const u32 __wl_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
#ifdef BCMWAPI_WPI
	WLAN_CIPHER_SUITE_SMS4,
#endif
#if defined(WLFBT) && defined(WLAN_CIPHER_SUITE_PMK)
	WLAN_CIPHER_SUITE_PMK,
#endif
};

#ifdef WL_SUPPORT_ACS
struct wl_dump_survey {
	u32 obss;
	u32 ibss;
	u32 no_ctg;
	u32 no_pckt;
	u32 tx;
	u32 idle;
};
#endif 


#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
static int maxrxpktglom = 0;
#endif

static int ioctl_version;
#ifdef DEBUGFS_CFG80211
#define S_SUBLOGLEVEL 20
static const struct {
	u32 log_level;
	char *sublogname;
} sublogname_map[] = {
	{WL_DBG_ERR, "ERR"},
	{WL_DBG_INFO, "INFO"},
	{WL_DBG_DBG, "DBG"},
	{WL_DBG_SCAN, "SCAN"},
	{WL_DBG_TRACE, "TRACE"},
	{WL_DBG_P2P_ACTION, "P2PACTION"}
};
#endif


static void wl_add_remove_pm_enable_work(struct bcm_cfg80211 *cfg, bool add_remove,
	enum wl_handler_del_type type)
{
	if (cfg == NULL)
		return;

	if (cfg->pm_enable_work_on) {
		if (add_remove) {
			schedule_delayed_work(&cfg->pm_enable_work,
				msecs_to_jiffies(WL_PM_ENABLE_TIMEOUT));
		} else {
			cancel_delayed_work_sync(&cfg->pm_enable_work);

			switch (type) {
				case WL_HANDLER_MAINTAIN:
					schedule_delayed_work(&cfg->pm_enable_work,
						msecs_to_jiffies(WL_PM_ENABLE_TIMEOUT));
					break;
				case WL_HANDLER_PEND:
					schedule_delayed_work(&cfg->pm_enable_work,
						msecs_to_jiffies(WL_PM_ENABLE_TIMEOUT*2));
					break;
				case WL_HANDLER_DEL:
				default:
					cfg->pm_enable_work_on = false;
					break;
			}
		}
	}
}

static chanspec_t
wl_chspec_from_legacy(chanspec_t legacy_chspec)
{
	chanspec_t chspec;

	
	chspec = LCHSPEC_CHANNEL(legacy_chspec);

	
	if (LCHSPEC_IS2G(legacy_chspec)) {
		chspec |= WL_CHANSPEC_BAND_2G;
	} else {
		chspec |= WL_CHANSPEC_BAND_5G;
	}

	
	if (LCHSPEC_IS20(legacy_chspec)) {
		chspec |= WL_CHANSPEC_BW_20;
	} else {
		chspec |= WL_CHANSPEC_BW_40;
		if (LCHSPEC_CTL_SB(legacy_chspec) == WL_LCHANSPEC_CTL_SB_LOWER) {
			chspec |= WL_CHANSPEC_CTL_SB_L;
		} else {
			chspec |= WL_CHANSPEC_CTL_SB_U;
		}
	}

	if (wf_chspec_malformed(chspec)) {
		WL_ERR(("wl_chspec_from_legacy: output chanspec (0x%04X) malformed\n",
		        chspec));
		return INVCHANSPEC;
	}

	return chspec;
}

static chanspec_t
wl_chspec_to_legacy(chanspec_t chspec)
{
	chanspec_t lchspec;

	if (wf_chspec_malformed(chspec)) {
		WL_ERR(("wl_chspec_to_legacy: input chanspec (0x%04X) malformed\n",
		        chspec));
		return INVCHANSPEC;
	}

	
	lchspec = CHSPEC_CHANNEL(chspec);

	
	if (CHSPEC_IS2G(chspec)) {
		lchspec |= WL_LCHANSPEC_BAND_2G;
	} else {
		lchspec |= WL_LCHANSPEC_BAND_5G;
	}

	
	if (CHSPEC_IS20(chspec)) {
		lchspec |= WL_LCHANSPEC_BW_20;
		lchspec |= WL_LCHANSPEC_CTL_SB_NONE;
	} else if (CHSPEC_IS40(chspec)) {
		lchspec |= WL_LCHANSPEC_BW_40;
		if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_L) {
			lchspec |= WL_LCHANSPEC_CTL_SB_LOWER;
		} else {
			lchspec |= WL_LCHANSPEC_CTL_SB_UPPER;
		}
	} else {
		
		char chanbuf[CHANSPEC_STR_LEN];
		WL_ERR((
		        "wl_chspec_to_legacy: unable to convert chanspec %s (0x%04X) "
		        "to pre-11ac format\n",
		        wf_chspec_ntoa(chspec, chanbuf), chspec));
		return INVCHANSPEC;
	}

	return lchspec;
}

chanspec_t
wl_chspec_host_to_driver(chanspec_t chanspec)
{
	if (ioctl_version == 1) {
		chanspec = wl_chspec_to_legacy(chanspec);
		if (chanspec == INVCHANSPEC) {
			return chanspec;
		}
	}
	chanspec = htodchanspec(chanspec);

	return chanspec;
}

chanspec_t
wl_ch_host_to_driver(s32 bssidx, u16 channel)
{
	chanspec_t chanspec;

	chanspec = channel & WL_CHANSPEC_CHAN_MASK;

	if (channel <= CH_MAX_2G_CHANNEL)
		chanspec |= WL_CHANSPEC_BAND_2G;
	else
		chanspec |= WL_CHANSPEC_BAND_5G;

	chanspec |= wl_cfg80211_ulb_get_min_bw_chspec(NULL, bssidx);

	chanspec |= WL_CHANSPEC_CTL_SB_NONE;

	return wl_chspec_host_to_driver(chanspec);
}

chanspec_t
wl_chspec_driver_to_host(chanspec_t chanspec)
{
	chanspec = dtohchanspec(chanspec);
	if (ioctl_version == 1) {
		chanspec = wl_chspec_from_legacy(chanspec);
	}

	return chanspec;
}

int
wl_cfg80211_ether_atoe(const char *a, struct ether_addr *n)
{
	char *c = NULL;
	int count = 0;

	memset(n, 0, ETHER_ADDR_LEN);
	for (;;) {
		n->octet[count++] = (uint8)simple_strtoul(a, &c, 16);
		if (!*c++ || count == ETHER_ADDR_LEN)
			break;
		a = c;
	}
	return (count == ETHER_ADDR_LEN);
}

static const struct ieee80211_txrx_stypes
wl_cfg80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_AP_VLAN] = {
		
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
#if defined(WL_CFG80211_P2P_DEV_IF)
	[NL80211_IFTYPE_P2P_DEVICE] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
#endif 
};

static void swap_key_from_BE(struct wl_wsec_key *key)
{
	key->index = htod32(key->index);
	key->len = htod32(key->len);
	key->algo = htod32(key->algo);
	key->flags = htod32(key->flags);
	key->rxiv.hi = htod32(key->rxiv.hi);
	key->rxiv.lo = htod16(key->rxiv.lo);
	key->iv_initialized = htod32(key->iv_initialized);
}

static void swap_key_to_BE(struct wl_wsec_key *key)
{
	key->index = dtoh32(key->index);
	key->len = dtoh32(key->len);
	key->algo = dtoh32(key->algo);
	key->flags = dtoh32(key->flags);
	key->rxiv.hi = dtoh32(key->rxiv.hi);
	key->rxiv.lo = dtoh16(key->rxiv.lo);
	key->iv_initialized = dtoh32(key->iv_initialized);
}

static void
wl_validate_wps_ie(char *wps_ie, s32 wps_ie_len, bool *pbc)
{
	#define WPS_IE_FIXED_LEN 6
	u16 len;
	u8 *subel = NULL;
	u16 subelt_id;
	u16 subelt_len;
	u16 val;
	u8 *valptr = (uint8*) &val;
	if (wps_ie == NULL || wps_ie_len < WPS_IE_FIXED_LEN) {
		WL_ERR(("invalid argument : NULL\n"));
		return;
	}
	len = (u16)wps_ie[TLV_LEN_OFF];

	if (len > wps_ie_len) {
		WL_ERR(("invalid length len %d, wps ie len %d\n", len, wps_ie_len));
		return;
	}
	WL_DBG(("wps_ie len=%d\n", len));
	len -= 4;	
	subel = wps_ie + WPS_IE_FIXED_LEN;
	while (len >= 4) {		
		valptr[0] = *subel++;
		valptr[1] = *subel++;
		subelt_id = HTON16(val);

		valptr[0] = *subel++;
		valptr[1] = *subel++;
		subelt_len = HTON16(val);

		len -= 4;			
		if (len < subelt_len) {
			WL_ERR(("not enough data, len %d, subelt_len %d\n", len,
				subelt_len));
			break;
		}
		len -= subelt_len;	
		WL_DBG((" subel=%p, subelt_id=0x%x subelt_len=%u\n",
			subel, subelt_id, subelt_len));

		if (subelt_id == WPS_ID_VERSION) {
			WL_DBG(("  attr WPS_ID_VERSION: %u\n", *subel));
		} else if (subelt_id == WPS_ID_REQ_TYPE) {
			WL_DBG(("  attr WPS_ID_REQ_TYPE: %u\n", *subel));
		} else if (subelt_id == WPS_ID_CONFIG_METHODS) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_CONFIG_METHODS: %x\n", HTON16(val)));
		} else if (subelt_id == WPS_ID_DEVICE_NAME) {
			char devname[100];
			int namelen = MIN(subelt_len, (sizeof(devname) - 1));

			if (namelen) {
				memcpy(devname, subel, namelen);
				devname[namelen] = '\0';
				
				WL_DBG(("  attr WPS_ID_DEVICE_NAME: %s (len %u)\n",
					devname, subelt_len));
			}
		} else if (subelt_id == WPS_ID_DEVICE_PWD_ID) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_DEVICE_PWD_ID: %u\n", HTON16(val)));
			*pbc = (HTON16(val) == DEV_PW_PUSHBUTTON) ? true : false;
		} else if (subelt_id == WPS_ID_PRIM_DEV_TYPE) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_PRIM_DEV_TYPE: cat=%u \n", HTON16(val)));
			valptr[0] = *(subel + 6);
			valptr[1] = *(subel + 7);
			WL_DBG(("  attr WPS_ID_PRIM_DEV_TYPE: subcat=%u\n", HTON16(val)));
		} else if (subelt_id == WPS_ID_REQ_DEV_TYPE) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_REQ_DEV_TYPE: cat=%u\n", HTON16(val)));
			valptr[0] = *(subel + 6);
			valptr[1] = *(subel + 7);
			WL_DBG(("  attr WPS_ID_REQ_DEV_TYPE: subcat=%u\n", HTON16(val)));
		} else if (subelt_id == WPS_ID_SELECTED_REGISTRAR_CONFIG_METHODS) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_SELECTED_REGISTRAR_CONFIG_METHODS"
				": cat=%u\n", HTON16(val)));
		} else {
			WL_DBG(("  unknown attr 0x%x\n", subelt_id));
		}

		subel += subelt_len;
	}
}

s32 wl_set_tx_power(struct net_device *dev,
	enum nl80211_tx_power_setting type, s32 dbm)
{
	s32 err = 0;
	s32 disable = 0;
	s32 txpwrqdbm;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	
	disable = WL_RADIO_SW_DISABLE << 16;
	disable = htod32(disable);
	err = wldev_ioctl(dev, WLC_SET_RADIO, &disable, sizeof(disable), true);
	if (unlikely(err)) {
		WL_ERR(("WLC_SET_RADIO error (%d)\n", err));
		return err;
	}

	if (dbm > 0xffff)
		dbm = 0xffff;
	txpwrqdbm = dbm * 4;
#ifdef SUPPORT_WL_TXPOWER
	if (type == NL80211_TX_POWER_AUTOMATIC)
		txpwrqdbm = 127;
	else
		txpwrqdbm |= WL_TXPWR_OVERRIDE;
#endif 
	err = wldev_iovar_setbuf_bsscfg(dev, "qtxpower", (void *)&txpwrqdbm,
		sizeof(txpwrqdbm), cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0,
		&cfg->ioctl_buf_sync);
	if (unlikely(err))
		WL_ERR(("qtxpower error (%d)\n", err));
	else
		WL_ERR(("dBm=%d, txpwrqdbm=0x%x\n", dbm, txpwrqdbm));

	return err;
}

s32 wl_get_tx_power(struct net_device *dev, s32 *dbm)
{
	s32 err = 0;
	s32 txpwrdbm;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	err = wldev_iovar_getbuf_bsscfg(dev, "qtxpower",
		NULL, 0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	memcpy(&txpwrdbm, cfg->ioctl_buf, sizeof(txpwrdbm));
	txpwrdbm = dtoh32(txpwrdbm);
	*dbm = (txpwrdbm & ~WL_TXPWR_OVERRIDE) / 4;

	WL_INFORM(("dBm=%d, txpwrdbm=0x%x\n", *dbm, txpwrdbm));

	return err;
}

static chanspec_t wl_cfg80211_get_shared_freq(struct wiphy *wiphy)
{
	chanspec_t chspec;
	int err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	struct ether_addr bssid;
	struct wl_bss_info *bss = NULL;
	s32 bssidx = 0; 
#ifdef CUSTOMER_HW_ONE
	s32 cur_band = -1;
	uint32 chan_buf[WL_NUMCHANNELS + 1];
	wl_uint32_list_t *list;
	int chan;
#endif 

	if ((err = wldev_ioctl(dev, WLC_GET_BSSID, &bssid, sizeof(bssid), false))) {
#ifdef CUSTOMER_HW_ONE
		err = wldev_ioctl(dev, WLC_GET_BAND, &cur_band, sizeof(s32), false);
		if (unlikely(err)) {
			WL_ERR(("failed to get band error (%d)\n", err));
			goto exit;
		}
		if (cur_band == WLC_BAND_5G) {
			list = (wl_uint32_list_t *) (void *)chan_buf;
			list->count = htod32(WL_NUMCHANNELS);
			err = wldev_ioctl(dev, WLC_GET_VALID_CHANNELS,
				chan_buf, sizeof(chan_buf), FALSE);
			if (err < 0) {
				WL_ERR(("failed to get channel list (err: %d)\n", err));
				goto exit;
			}
			chan = dtoh32(list->element[0]);
			if (chan > CHANNEL_2G_MAX) {
				WL_DBG(("Not associated. Return a temp channel = %d\n", chan));
				return wl_ch_host_to_driver(bssidx, chan);
			}
		}
exit:
#endif 
		WL_DBG(("Not associated. Return a temp channel. \n"));
		return wl_ch_host_to_driver(bssidx, WL_P2P_TEMP_CHAN);
	}


	*(u32 *) cfg->extra_buf = htod32(WL_EXTRA_BUF_MAX);
	if ((err = wldev_ioctl(dev, WLC_GET_BSS_INFO, cfg->extra_buf,
		WL_EXTRA_BUF_MAX, false))) {
			WL_ERR(("Failed to get associated bss info, use temp channel \n"));
			chspec = wl_ch_host_to_driver(bssidx, WL_P2P_TEMP_CHAN);
	}
	else {
			bss = (struct wl_bss_info *) (cfg->extra_buf + 4);
			chspec =  bss->chanspec;

			WL_DBG(("Valid BSS Found. chanspec:%d \n", chspec));
	}
	return chspec;
}

static bcm_struct_cfgdev *
wl_cfg80211_add_monitor_if(char *name)
{
#if defined(WL_ENABLE_P2P_IF) || defined(WL_CFG80211_P2P_DEV_IF)
	WL_INFORM(("wl_cfg80211_add_monitor_if: No more support monitor interface\n"));
	return ERR_PTR(-EOPNOTSUPP);
#else
	struct net_device* ndev = NULL;

	dhd_add_monitor(name, &ndev);
	WL_INFORM(("wl_cfg80211_add_monitor_if net device returned: 0x%p\n", ndev));
	return ndev_to_cfgdev(ndev);
#endif 
}

static bcm_struct_cfgdev *
wl_cfg80211_add_virtual_iface(struct wiphy *wiphy,
#if defined(WL_CFG80211_P2P_DEV_IF)
	const char *name,
#else
	char *name,
#endif 
	enum nl80211_iftype type, u32 *flags,
	struct vif_params *params)
{
	s32 err = -ENODEV;
	s32 timeout = -1;
	s32 wlif_type = -1;
	s32 mode = 0;
	s32 val = 0;
	s32 cfg_type;
	s32 dhd_mode = 0;
	chanspec_t chspec;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *primary_ndev;
	struct net_device *new_ndev;
	struct ether_addr primary_mac;
#ifdef WL_VIRTUAL_APSTA
	bcm_struct_cfgdev *new_cfgdev;
#endif 
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
	s32 up = 1;
	dhd_pub_t *dhd;
	bool enabled;
#endif 
#endif 
#if defined(SUPPORT_AP_POWERSAVE)
	dhd_pub_t *dhd;
#endif 

	if (!cfg)
		return ERR_PTR(-EINVAL);

#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
	dhd = (dhd_pub_t *)(cfg->pub);
#endif 
#endif 
#if defined(SUPPORT_AP_POWERSAVE)
	dhd = (dhd_pub_t *)(cfg->pub);
#endif 

	
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	if (unlikely(!wl_get_drv_status(cfg, READY, primary_ndev))) {
		WL_ERR(("device is not ready\n"));
		return ERR_PTR(-ENODEV);
	}

	WL_DBG(("if name: %s, type: %d\n", name, type));
	switch (type) {
	case NL80211_IFTYPE_ADHOC:
#ifdef WLAIBSS_MCHAN
		return bcm_cfg80211_add_ibss_if(wiphy, (char *)name);
#endif 
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MESH_POINT:
		WL_ERR(("Unsupported interface type\n"));
		mode = WL_MODE_IBSS;
		return NULL;
	case NL80211_IFTYPE_MONITOR:
		return wl_cfg80211_add_monitor_if((char *)name);
#if defined(WL_CFG80211_P2P_DEV_IF)
	case NL80211_IFTYPE_P2P_DEVICE:
		return wl_cfgp2p_add_p2p_disc_if(cfg);
#endif 
	case NL80211_IFTYPE_STATION:
#ifdef WL_VIRTUAL_APSTA
#ifdef WLAIBSS_MCHAN
		if (cfg->ibss_cfgdev) {
			WL_ERR(("AIBSS is already operational. "
					" AIBSS & DUALSTA can't be used together \n"));
			return ERR_PTR(-ENOMEM);
		}
#endif 
		if (!name) {
			WL_ERR(("Interface name not provided \n"));
			return ERR_PTR(-ENODEV);
		}

		if (wl_cfgp2p_vif_created(cfg)) {
			WL_ERR(("Could not create new iface."
				"Already one p2p interface is running"));
			return ERR_PTR(-ENODEV);
		}
		new_cfgdev = wl_cfg80211_create_iface(cfg->wdev->wiphy,
			NL80211_IFTYPE_STATION, NULL, name);
		if (!new_cfgdev)
			return ERR_PTR(-ENOMEM);
		else
			return new_cfgdev;
#endif 
	case NL80211_IFTYPE_P2P_CLIENT:
		wlif_type = WL_P2P_IF_CLIENT;
		mode = WL_MODE_BSS;
		break;
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_AP:
		wlif_type = WL_P2P_IF_GO;
		mode = WL_MODE_AP;
		break;
	default:
		WL_ERR(("Unsupported interface type\n"));
		return ERR_PTR(-ENODEV);
		break;
	}

	if (!name) {
		WL_ERR(("name is NULL\n"));
		return ERR_PTR(-ENODEV);
	}
	if (cfg->p2p_supported && (wlif_type != -1)) {
		ASSERT(cfg->p2p); 

#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
		if (!dhd)
			return ERR_PTR(-ENODEV);
#endif 
#endif 
		if (!cfg->p2p)
			return ERR_PTR(-ENODEV);

		if (cfg->cfgdev_bssidx != -1) {
			WL_ERR(("Failed to start p2p, Maximum no of interface reached"));
			return ERR_PTR(-ENODEV);
		}

		if (cfg->p2p && !cfg->p2p->on && strstr(name, WL_P2P_INTERFACE_PREFIX)) {
			p2p_on(cfg) = true;
			wl_cfgp2p_set_firm_p2p(cfg);
			wl_cfgp2p_init_discovery(cfg);
			get_primary_mac(cfg, &primary_mac);
			wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
		}

		strncpy(cfg->p2p->vir_ifname, name, IFNAMSIZ - 1);
		cfg->p2p->vir_ifname[IFNAMSIZ - 1] = '\0';

		wl_cfg80211_scan_abort(cfg);
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
		if (!cfg->wlfc_on && !disable_proptx) {
			dhd_wlfc_get_enable(dhd, &enabled);
			if (!enabled && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
				dhd->op_mode != DHD_FLAG_IBSS_MODE) {
				dhd_wlfc_init(dhd);
				err = wldev_ioctl(primary_ndev, WLC_UP, &up, sizeof(s32), true);
				if (err < 0)
					WL_ERR(("WLC_UP return err:%d\n", err));
			}
			cfg->wlfc_on = true;
		}
#endif 
#endif 

		if ((cfg->p2p->p2p_go_count > 0) && (type == NL80211_IFTYPE_P2P_GO)) {
			WL_ERR(("Fw doesnot support  multiple Go"));
			return ERR_PTR(-ENOMEM);
		}
		 chspec = wl_cfg80211_get_shared_freq(wiphy);

		wl_set_p2p_status(cfg, IF_ADDING);
		memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
		if (wlif_type == WL_P2P_IF_GO)
			wldev_iovar_setint(primary_ndev, "mpc", 0);
		cfg_type = wl_cfgp2p_get_conn_idx(cfg);
		if (cfg_type == BCME_ERROR) {
			WL_ERR(("Failed to get connection idx for p2p interface"));
			goto fail;
		}
		err = wl_cfgp2p_ifadd(cfg, wl_to_p2p_bss_macaddr(cfg, cfg_type),
			htod32(wlif_type), chspec);
		if (unlikely(err)) {
			wl_clr_p2p_status(cfg, IF_ADDING);
			WL_ERR((" virtual iface add failed (%d) \n", err));
			return ERR_PTR(-ENOMEM);
		}

		timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
			((wl_get_p2p_status(cfg, IF_ADDING) == false) &&
			(cfg->if_event_info.valid)), msecs_to_jiffies(MAX_WAIT_TIME));

		if (timeout > 0 && !wl_get_p2p_status(cfg, IF_ADDING) && cfg->if_event_info.valid) {
			struct wireless_dev *vwdev;
			int pm_mode = PM_ENABLE;
			wl_if_event_info *event = &cfg->if_event_info;
			new_ndev = wl_cfg80211_allocate_if(cfg, event->ifidx, cfg->p2p->vir_ifname,
				event->mac, event->bssidx, event->name);
			if (new_ndev == NULL)
				goto fail;

			wl_to_p2p_bss_ndev(cfg, cfg_type) = new_ndev;
			wl_to_p2p_bss_bssidx(cfg, cfg_type) = event->bssidx;
			vwdev = kzalloc(sizeof(*vwdev), GFP_KERNEL);
			if (unlikely(!vwdev)) {
				WL_ERR(("Could not allocate wireless device\n"));
				err = -ENOMEM;
				goto fail;
			}
			vwdev->wiphy = cfg->wdev->wiphy;
			WL_INFORM(("virtual interface(%s) is created\n", cfg->p2p->vir_ifname));
			if (type == NL80211_IFTYPE_P2P_GO) {
				cfg->p2p->p2p_go_count++;
			}
			vwdev->iftype = type;
			vwdev->netdev = new_ndev;
			new_ndev->ieee80211_ptr = vwdev;
			SET_NETDEV_DEV(new_ndev, wiphy_dev(vwdev->wiphy));
			wl_set_drv_status(cfg, READY, new_ndev);
			wl_set_mode_by_netdev(cfg, new_ndev, mode);

			if (wl_cfg80211_register_if(cfg, event->ifidx, new_ndev) != BCME_OK) {
				wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev);
				err = -ENODEV;
				goto fail;
			}
			err = wl_alloc_netinfo(cfg, new_ndev, vwdev, mode, pm_mode, event->bssidx);
			if (unlikely(err != 0)) {
				WL_ERR(("Allocation of netinfo failed (%d) \n", err));
				goto fail;
			}
			val = 1;
			
			wldev_iovar_setint(new_ndev, "roam_off", val);
#ifdef WL11ULB
			if (cfg->p2p_wdev && is_p2p_group_iface(new_ndev->ieee80211_ptr)) {
				u32 ulb_bw = wl_cfg80211_get_ulb_bw(cfg->p2p_wdev);
				if (ulb_bw) {
					
					WL_DBG(("[ULB] Applying ULB BW for the newly"
						"created P2P interface \n"));
					if (wl_cfg80211_set_ulb_bw(new_ndev,
						ulb_bw, new_ndev->name) < 0) {
						wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev);
						err = -EINVAL;
						goto fail;
					}
				}
			}
#endif 

			if (mode != WL_MODE_AP)
				wldev_iovar_setint(new_ndev, "buf_key_b4_m4", 1);

			WL_ERR((" virtual interface(%s) is "
				"created net attach done\n", cfg->p2p->vir_ifname));
			if (mode == WL_MODE_AP)
				wl_set_drv_status(cfg, CONNECTED, new_ndev);
#ifdef SUPPORT_AP_POWERSAVE
			if (mode == WL_MODE_AP) {
				dhd_set_ap_powersave(dhd, 0, TRUE);
			}
#endif 
			if (type == NL80211_IFTYPE_P2P_CLIENT)
				dhd_mode = DHD_FLAG_P2P_GC_MODE;
			else if (type == NL80211_IFTYPE_P2P_GO)
				dhd_mode = DHD_FLAG_P2P_GO_MODE;
			DNGL_FUNC(dhd_cfg80211_set_p2p_info, (cfg, dhd_mode));
			
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
			INIT_COMPLETION(cfg->iface_disable);
#else
			init_completion(&cfg->iface_disable);
#endif
			return ndev_to_cfgdev(new_ndev);
		} else {
			wl_clr_p2p_status(cfg, IF_ADDING);
			WL_ERR((" virtual interface(%s) is not created \n", cfg->p2p->vir_ifname));

			WL_ERR(("left timeout : %d\n", timeout));
			WL_ERR(("IF_ADDING status : %d\n", wl_get_p2p_status(cfg, IF_ADDING)));
			WL_ERR(("event valid : %d\n", cfg->if_event_info.valid));

			wl_clr_p2p_status(cfg, GO_NEG_PHASE);
			wl_set_p2p_status(cfg, IF_DELETING);

			err = wl_cfgp2p_ifdel(cfg, wl_to_p2p_bss_macaddr(cfg, cfg_type));
			if (err == BCME_OK) {
				timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
					((wl_get_p2p_status(cfg, IF_DELETING) == false) &&
					(cfg->if_event_info.valid)),
					msecs_to_jiffies(MAX_WAIT_TIME));
				if (timeout > 0 && !wl_get_p2p_status(cfg, IF_DELETING) &&
					cfg->if_event_info.valid) {
					WL_ERR(("IFDEL operation done\n"));
				} else {
					WL_ERR(("IFDEL didn't complete properly\n"));
					err = BCME_ERROR;
				}
			}
			if (err != BCME_OK) {
				struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);

				WL_ERR(("p2p_ifdel failed, error %d, sent HANG event to %s\n",
					err, ndev->name));
				net_os_send_hang_message(ndev);
			}

			memset(cfg->p2p->vir_ifname, '\0', IFNAMSIZ);
			wl_to_p2p_bss_bssidx(cfg, cfg_type) = -1;
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
			dhd_wlfc_get_enable(dhd, &enabled);
		if (enabled && cfg->wlfc_on && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
			dhd->op_mode != DHD_FLAG_IBSS_MODE) {
			dhd_wlfc_deinit(dhd);
			cfg->wlfc_on = false;
		}
#endif 
#endif 
			if (err == BCME_OK) {
				WL_ERR(("P2P interface creation failed and returning ENODEV\n"));
				err = -ENODEV;
			}
		}
	}
fail:
	if (wlif_type == WL_P2P_IF_GO)
		wldev_iovar_setint(primary_ndev, "mpc", 1);
	return ERR_PTR(err);
}

static s32
wl_cfg80211_del_virtual_iface(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev)
{
	struct net_device *dev = NULL;
	struct ether_addr p2p_mac;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 timeout = -1;
	s32 ret = 0;
	s32 index = -1;
	s32 type = -1;
#ifdef CUSTOM_SET_CPUCORE
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
#endif 
	WL_DBG(("Enter\n"));

#ifdef CUSTOM_SET_CPUCORE
	dhd->chan_isvht80 &= ~DHD_FLAG_P2P_MODE;
	if (!(dhd->chan_isvht80))
		dhd_set_cpucore(dhd, FALSE);
#endif 
#if defined(WL_CFG80211_P2P_DEV_IF)
	if (cfgdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		return wl_cfgp2p_del_p2p_disc_if(cfgdev, cfg);
	}
#endif 
	dev = cfgdev_to_wlc_ndev(cfgdev, cfg);

#ifdef WLAIBSS_MCHAN
	if (cfgdev == cfg->ibss_cfgdev)
		return bcm_cfg80211_del_ibss_if(wiphy, cfgdev);
#endif 

#ifdef WL_VIRTUAL_APSTA
	if (cfgdev == cfg->bss_cfgdev)
		return wl_cfg80211_del_iface(wiphy, cfgdev);
#endif 
	if ((index = wl_get_bssidx_by_wdev(cfg, cfgdev_to_wdev(cfgdev))) < 0) {
		WL_ERR(("Find p2p index from wdev failed\n"));
		return BCME_ERROR;
	}
	if (cfg->p2p_supported) {
		if (wl_cfgp2p_find_type(cfg, index, &type) != BCME_OK)
			return BCME_ERROR;
		memcpy(p2p_mac.octet, wl_to_p2p_bss_macaddr(cfg, type).octet, ETHER_ADDR_LEN);

		WL_DBG(("P2P: GO_NEG_PHASE status cleared "));
		wl_clr_p2p_status(cfg, GO_NEG_PHASE);
		if (wl_cfgp2p_vif_created(cfg)) {
			if (wl_get_drv_status(cfg, SCANNING, dev)) {
				wl_notify_escan_complete(cfg, dev, true, true);
			}
			wldev_iovar_setint(dev, "mpc", 1);
			
			wl_add_remove_pm_enable_work(cfg, FALSE, WL_HANDLER_DEL);

			
			if (wl_get_drv_status(cfg, DISCONNECTING, dev) &&
				(wl_get_mode_by_netdev(cfg, dev) != WL_MODE_AP)) {
				WL_ERR(("Wait for Link Down event for GC !\n"));
				wait_for_completion_timeout
					(&cfg->iface_disable, msecs_to_jiffies(500));
			}

			memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
			wl_set_p2p_status(cfg, IF_DELETING);
			DNGL_FUNC(dhd_cfg80211_clean_p2p_info, (cfg));

			
			if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP) {
				wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, false);
				cfg->p2p->p2p_go_count--;
				
				ret = wl_cfgp2p_ifdisable(cfg, &p2p_mac);
				if (ret == 0) {
					WL_ERR(("Wait for Link Down event for GO !!!\n"));
					wait_for_completion_timeout(&cfg->iface_disable,
						msecs_to_jiffies(500));
				} else if (ret != BCME_UNSUPPORTED) {
					msleep(300);
				}
			}
			wl_cfg80211_clear_per_bss_ies(cfg, index);

			if (wl_get_mode_by_netdev(cfg, dev) != WL_MODE_AP)
				wldev_iovar_setint(dev, "buf_key_b4_m4", 0);
			memcpy(p2p_mac.octet, wl_to_p2p_bss_macaddr(cfg, type).octet,
			ETHER_ADDR_LEN);
			CFGP2P_INFO(("primary idx %d : cfg p2p_ifdis "MACDBG"\n",
			       dev->ifindex, MAC2STRDBG(p2p_mac.octet)));

			
			ret = wl_cfgp2p_ifdel(cfg, &p2p_mac);
			if (ret != BCME_OK) {
				struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);

				WL_ERR(("p2p_ifdel failed, error %d, sent HANG event to %s\n",
					ret, ndev->name));
				net_os_send_hang_message(ndev);
			} else {
				
				timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
					((wl_get_p2p_status(cfg, IF_DELETING) == false) &&
					(cfg->if_event_info.valid)),
					msecs_to_jiffies(MAX_WAIT_TIME));
				if (timeout > 0 && !wl_get_p2p_status(cfg, IF_DELETING) &&
					cfg->if_event_info.valid) {

					WL_DBG(("IFDEL operation done\n"));
					wl_cfg80211_handle_ifdel(cfg, &cfg->if_event_info, dev);
				} else {
					WL_ERR(("IFDEL didn't complete properly\n"));
				}
			}

			ret = dhd_del_monitor(dev);
			if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP) {
				DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_CANCEL((dhd_pub_t *)(cfg->pub));
			}
		}
	}
	return ret;
}

static s32
wl_cfg80211_change_virtual_iface(struct wiphy *wiphy, struct net_device *ndev,
	enum nl80211_iftype type, u32 *flags,
	struct vif_params *params)
{
	s32 ap = 0;
	s32 infra = 0;
	s32 ibss = 0;
	s32 wlif_type;
	s32 mode = 0;
	s32 err = BCME_OK;
	s32 index;
	s32 conn_idx = -1;
	chanspec_t chspec;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *primary_ndev = bcmcfg_to_prmry_ndev(cfg);
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("Enter type %d\n", type));
	switch (type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MESH_POINT:
		ap = 1;
		WL_ERR(("type (%d) : currently we do not support this type\n",
			type));
		break;
	case NL80211_IFTYPE_ADHOC:
		mode = WL_MODE_IBSS;
		ibss = 1;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		mode = WL_MODE_BSS;
		infra = 1;
		break;
	case NL80211_IFTYPE_AP:
		dhd->op_mode |= DHD_FLAG_HOSTAP_MODE;
		
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		mode = WL_MODE_AP;
		ap = 1;
		break;
	default:
		return -EINVAL;
	}
	if (!dhd)
		return -EINVAL;
	if (ap) {
		wl_set_mode_by_netdev(cfg, ndev, mode);
		if (is_p2p_group_iface(ndev->ieee80211_ptr) &&
			cfg->p2p && wl_cfgp2p_vif_created(cfg)) {
			WL_DBG(("p2p_vif_created p2p_on (%d)\n", p2p_on(cfg)));
			wldev_iovar_setint(ndev, "mpc", 0);
			wl_notify_escan_complete(cfg, ndev, true, true);

			if ((cfg->p2p->p2p_go_count > 0) && (type == NL80211_IFTYPE_P2P_GO)) {
				wl_set_mode_by_netdev(cfg, ndev, WL_MODE_BSS);
				WL_ERR(("Fw doesnot support  multiple GO "));
				return BCME_ERROR;
			}
			chspec = wl_cfg80211_get_shared_freq(wiphy);
			index = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
			if (index < 0) {
				WL_ERR(("Find p2p index from ndev(%p) failed\n", ndev));
				return BCME_ERROR;
			}
			if (wl_cfgp2p_find_type(cfg, index, &conn_idx) != BCME_OK)
				return BCME_ERROR;

			wlif_type = WL_P2P_IF_GO;
			WL_DBG(("%s : ap (%d), infra (%d), iftype (%d) conn_idx (%d)\n",
				ndev->name, ap, infra, type, conn_idx));
			wl_set_p2p_status(cfg, IF_CHANGING);
			wl_clr_p2p_status(cfg, IF_CHANGED);
			wl_cfgp2p_ifchange(cfg, wl_to_p2p_bss_macaddr(cfg, conn_idx),
				htod32(wlif_type), chspec, conn_idx);
			wait_event_interruptible_timeout(cfg->netif_change_event,
				(wl_get_p2p_status(cfg, IF_CHANGED) == true),
				msecs_to_jiffies(MAX_WAIT_TIME));
			wl_set_mode_by_netdev(cfg, ndev, mode);
			dhd->op_mode &= ~DHD_FLAG_P2P_GC_MODE;
			dhd->op_mode |= DHD_FLAG_P2P_GO_MODE;
			wl_clr_p2p_status(cfg, IF_CHANGING);
			wl_clr_p2p_status(cfg, IF_CHANGED);
			if (mode == WL_MODE_AP)
				wl_set_drv_status(cfg, CONNECTED, ndev);
#ifdef SUPPORT_AP_POWERSAVE
			dhd_set_ap_powersave(dhd, 0, TRUE);
#endif 
		} else if (((ndev == primary_ndev) ||
			(ndev == ((struct net_device *)cfgdev_to_ndev(cfg->bss_cfgdev)))) &&
			!wl_get_drv_status(cfg, AP_CREATED, ndev)) {
			wl_set_drv_status(cfg, AP_CREATING, ndev);
		} else {
			WL_ERR(("Cannot change the interface for GO or SOFTAP\n"));
			return -EINVAL;
		}
	} else {
		if (ndev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
			wl_set_mode_by_netdev(cfg, ndev, mode);
		WL_DBG(("Change_virtual_iface for transition from GO/AP to client/STA"));
#ifdef SUPPORT_AP_POWERSAVE
		dhd_set_ap_powersave(dhd, 0, FALSE);
#endif 
	}

	if (ibss) {
		infra = 0;
		wl_set_mode_by_netdev(cfg, ndev, mode);
		err = wldev_ioctl(ndev, WLC_SET_INFRA, &infra, sizeof(s32), true);
		if (err < 0) {
			WL_ERR(("SET Adhoc error %d\n", err));
			return -EINVAL;
		}
	}

	ndev->ieee80211_ptr->iftype = type;
	return 0;
}

s32
wl_cfg80211_notify_ifadd(int ifidx, char *name, uint8 *mac, uint8 bssidx)
{
	bool ifadd_expected = FALSE;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	if (wl_get_p2p_status(cfg, IF_CHANGING))
		return wl_cfg80211_notify_ifchange(ifidx, name, mac, bssidx);

	
	if (wl_get_p2p_status(cfg, IF_ADDING)) {
		ifadd_expected = TRUE;
		wl_clr_p2p_status(cfg, IF_ADDING);
	} else if (cfg->bss_pending_op) {
		ifadd_expected = TRUE;
		cfg->bss_pending_op = FALSE;
	}

	if (ifadd_expected) {
		wl_if_event_info *if_event_info = &cfg->if_event_info;

		if_event_info->valid = TRUE;
		if_event_info->ifidx = ifidx;
		if_event_info->bssidx = bssidx;
		strncpy(if_event_info->name, name, IFNAMSIZ);
		if_event_info->name[IFNAMSIZ] = '\0';
		if (mac)
			memcpy(if_event_info->mac, mac, ETHER_ADDR_LEN);
		wake_up_interruptible(&cfg->netif_change_event);
		return BCME_OK;
	}

	return BCME_ERROR;
}

s32
wl_cfg80211_notify_ifdel(int ifidx, char *name, uint8 *mac, uint8 bssidx)
{
	bool ifdel_expected = FALSE;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	wl_if_event_info *if_event_info = &cfg->if_event_info;

	if (wl_get_p2p_status(cfg, IF_DELETING)) {
		ifdel_expected = TRUE;
		wl_clr_p2p_status(cfg, IF_DELETING);
	} else if (cfg->bss_pending_op) {
		ifdel_expected = TRUE;
		cfg->bss_pending_op = FALSE;
	}

	if (ifdel_expected) {
		if_event_info->valid = TRUE;
		if_event_info->ifidx = ifidx;
		if_event_info->bssidx = bssidx;
		wake_up_interruptible(&cfg->netif_change_event);
		return BCME_OK;
	}

	return BCME_ERROR;
}

s32
wl_cfg80211_notify_ifchange(int ifidx, char *name, uint8 *mac, uint8 bssidx)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	if (wl_get_p2p_status(cfg, IF_CHANGING)) {
		wl_set_p2p_status(cfg, IF_CHANGED);
		wake_up_interruptible(&cfg->netif_change_event);
		return BCME_OK;
	}

	return BCME_ERROR;
}

static s32 wl_cfg80211_handle_ifdel(struct bcm_cfg80211 *cfg, wl_if_event_info *if_event_info,
	struct net_device* ndev)
{
	s32 type = -1;
	s32 bssidx = -1;
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	bool enabled;
#endif 
#endif 

	bssidx = if_event_info->bssidx;
	if (bssidx != wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION1) &&
		bssidx != wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION2)) {
		WL_ERR(("got IF_DEL for if %d, not owned by cfg driver\n", bssidx));
		return BCME_ERROR;
	}

	if (p2p_is_on(cfg) && wl_cfgp2p_vif_created(cfg)) {
		if (cfg->scan_request && (cfg->escan_info.ndev == ndev)) {
			
			cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
			WL_DBG(("ESCAN COMPLETED\n"));
			wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, false);
		}

		memset(cfg->p2p->vir_ifname, '\0', IFNAMSIZ);
		if (wl_cfgp2p_find_type(cfg, bssidx, &type) == BCME_OK) {
			
			wl_clr_drv_status(cfg, CONNECTED, wl_to_p2p_bss_ndev(cfg, type));
			wl_to_p2p_bss_ndev(cfg, type) = NULL;
			wl_to_p2p_bss_bssidx(cfg, type) = -1;
		} else if (wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr) < 0) {
			WL_ERR(("bssidx not known for the given ndev as per net_info data \n"));
			return BCME_ERROR;
		}

#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
		dhd_wlfc_get_enable(dhd, &enabled);
		if (enabled && cfg->wlfc_on && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
			dhd->op_mode != DHD_FLAG_IBSS_MODE) {
			dhd_wlfc_deinit(dhd);
			cfg->wlfc_on = false;
		}
#endif 
#endif 
	}

	dhd_net_if_lock(ndev);
	wl_cfg80211_remove_if(cfg, if_event_info->ifidx, ndev);
	dhd_net_if_unlock(ndev);

	return BCME_OK;
}

static s32 wl_find_listen_channel(struct bcm_cfg80211 *cfg,
	const u8 *ie, u32 ie_len)
{
	wifi_p2p_ie_t *p2p_ie;
	u8 *end, *pos;
	s32 listen_channel;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	pos = (u8 *)ie;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	p2p_ie = wl_cfgp2p_find_p2pie(pos, ie_len);

	if (p2p_ie == NULL)
		return 0;

	pos = p2p_ie->subelts;
	end = p2p_ie->subelts + (p2p_ie->len - 4);

	CFGP2P_DBG((" found p2p ie ! lenth %d \n",
		p2p_ie->len));

	while (pos < end) {
		uint16 attr_len;
		if (pos + 2 >= end) {
			CFGP2P_DBG((" -- Invalid P2P attribute"));
			return 0;
		}
		attr_len = ((uint16) (((pos + 1)[1] << 8) | (pos + 1)[0]));

		if (pos + 3 + attr_len > end) {
			CFGP2P_DBG(("P2P: Attribute underflow "
				   "(len=%u left=%d)",
				   attr_len, (int) (end - pos - 3)));
			return 0;
		}

		if (pos[0] == 6) {
			listen_channel = pos[1 + 2 + 3 + 1];

			if (listen_channel == SOCIAL_CHAN_1 ||
				listen_channel == SOCIAL_CHAN_2 ||
				listen_channel == SOCIAL_CHAN_3) {
				CFGP2P_DBG((" Found my Listen Channel %d \n", listen_channel));
				return listen_channel;
			}
		}
		pos += 3 + attr_len;
	}
	return 0;
}

static void wl_scan_prep(struct wl_scan_params *params, struct cfg80211_scan_request *request)
{
	u32 n_ssids;
	u32 n_channels;
	u16 channel;
	chanspec_t chanspec;
	s32 i = 0, j = 0, offset;
	char *ptr;
	wlc_ssid_t ssid;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wireless_dev *wdev;

	memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
	params->bss_type = DOT11_BSSTYPE_ANY;
	params->scan_type = 0;
	params->nprobes = -1;
	params->active_time = -1;
	params->passive_time = -1;
	params->home_time = -1;
	params->channel_num = 0;
	memset(&params->ssid, 0, sizeof(wlc_ssid_t));

	WL_SCAN(("Preparing Scan request\n"));
	WL_SCAN(("nprobes=%d\n", params->nprobes));
	WL_SCAN(("active_time=%d\n", params->active_time));
	WL_SCAN(("passive_time=%d\n", params->passive_time));
	WL_SCAN(("home_time=%d\n", params->home_time));
	WL_SCAN(("scan_type=%d\n", params->scan_type));

	params->nprobes = htod32(params->nprobes);
	params->active_time = htod32(params->active_time);
	params->passive_time = htod32(params->passive_time);
	params->home_time = htod32(params->home_time);

	
	if (!request)
		return;

	n_ssids = request->n_ssids;
	n_channels = request->n_channels;

	
	WL_SCAN(("### List of channelspecs to scan ###\n"));
	if (n_channels > 0) {
		for (i = 0; i < n_channels; i++) {
			chanspec = 0;
			channel = ieee80211_frequency_to_channel(request->channels[i]->center_freq);
			
			if ((cfg->escan_info.ndev != bcmcfg_to_prmry_ndev(cfg)) &&
				(request->channels[i]->flags &
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
				(IEEE80211_CHAN_RADAR | IEEE80211_CHAN_PASSIVE_SCAN)))
#else
				(IEEE80211_CHAN_RADAR | IEEE80211_CHAN_NO_IR)))
#endif 
				continue;

#if defined(WL_CFG80211_P2P_DEV_IF)
			wdev = request->wdev;
#else
			wdev = request->dev->ieee80211_ptr;
#endif 
			chanspec = wl_cfg80211_ulb_get_min_bw_chspec(wdev, -1);
			if (chanspec == INVCHANSPEC) {
				WL_ERR(("Invalid chanspec! Skipping channel\n"));
				continue;
			}

			if (request->channels[i]->band == IEEE80211_BAND_2GHZ) {
#ifdef WL_HOST_BAND_MGMT
				if (cfg->curr_band == WLC_BAND_5G) {
					WL_DBG(("In 5G only mode, omit 2G channel:%d\n", channel));
					continue;
				}
#endif 
				chanspec |= WL_CHANSPEC_BAND_2G;
			} else {
#ifdef WL_HOST_BAND_MGMT
				if (cfg->curr_band == WLC_BAND_2G) {
					WL_DBG(("In 2G only mode, omit 5G channel:%d\n", channel));
					continue;
				}
#endif 
				chanspec |= WL_CHANSPEC_BAND_5G;
			}
			params->channel_list[j] = channel;
			params->channel_list[j] &= WL_CHANSPEC_CHAN_MASK;
			params->channel_list[j] |= chanspec;
			WL_SCAN(("Chan : %d, Channel spec: %x \n",
				channel, params->channel_list[j]));
			params->channel_list[j] = wl_chspec_host_to_driver(params->channel_list[j]);
			j++;
		}
	} else {
		WL_SCAN(("Scanning all channels\n"));
	}
	n_channels = j;
	
	WL_SCAN(("### List of SSIDs to scan ###\n"));
	if (n_ssids > 0) {
		offset = offsetof(wl_scan_params_t, channel_list) + n_channels * sizeof(u16);
		offset = roundup(offset, sizeof(u32));
		ptr = (char*)params + offset;
		for (i = 0; i < n_ssids; i++) {
			memset(&ssid, 0, sizeof(wlc_ssid_t));
			ssid.SSID_len = request->ssids[i].ssid_len;
#ifdef CUSTOMER_HW_ONE
			if (ssid.SSID_len > DOT11_MAX_SSID_LEN) {
				WL_ERR(("%d: invalid len for %s size =%d\n", i,
					ssid.SSID, ssid.SSID_len));
				ssid.SSID_len = DOT11_MAX_SSID_LEN;
			}
#endif 
			memcpy(ssid.SSID, request->ssids[i].ssid, ssid.SSID_len);
			if (!ssid.SSID_len)
				WL_SCAN(("%d: Broadcast scan\n", i));
			else
				WL_SCAN(("%d: scan  for  %s size =%d\n", i,
				ssid.SSID, ssid.SSID_len));
			memcpy(ptr, &ssid, sizeof(wlc_ssid_t));
			ptr += sizeof(wlc_ssid_t);
		}
	} else {
		WL_SCAN(("Broadcast scan\n"));
	}
	
	params->channel_num =
	        htod32((n_ssids << WL_SCAN_PARAMS_NSSID_SHIFT) |
	               (n_channels & WL_SCAN_PARAMS_COUNT_MASK));

	if (n_channels == 1) {
		params->active_time = htod32(WL_SCAN_CONNECT_DWELL_TIME_MS);
		params->nprobes = htod32(params->active_time / WL_SCAN_JOIN_PROBE_INTERVAL_MS);
	}
}

static s32
wl_get_valid_channels(struct net_device *ndev, u8 *valid_chan_list, s32 size)
{
	wl_uint32_list_t *list;
	s32 err = BCME_OK;
	if (valid_chan_list == NULL || size <= 0)
		return -ENOMEM;

	memset(valid_chan_list, 0, size);
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	err = wldev_ioctl(ndev, WLC_GET_VALID_CHANNELS, valid_chan_list, size, false);
	if (err != 0) {
		WL_ERR(("get channels failed with %d\n", err));
	}

	return err;
}

#if defined(USE_INITIAL_SHORT_DWELL_TIME)
#define FIRST_SCAN_ACTIVE_DWELL_TIME_MS 40
bool g_first_broadcast_scan = TRUE;
#endif 

static s32
wl_run_escan(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	struct cfg80211_scan_request *request, uint16 action)
{
	s32 err = BCME_OK;
	u32 n_channels;
	u32 n_ssids;
	s32 params_size = (WL_SCAN_PARAMS_FIXED_SIZE + OFFSETOF(wl_escan_params_t, params));
	wl_escan_params_t *params = NULL;
	u8 chan_buf[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	u32 num_chans = 0;
	s32 channel;
	u32 n_valid_chan;
	s32 search_state = WL_P2P_DISC_ST_SCAN;
	u32 i, j, n_nodfs = 0;
	u16 *default_chan_list = NULL;
	wl_uint32_list_t *list;
	s32 bssidx = -1;
	struct net_device *dev = NULL;
#if defined(USE_INITIAL_SHORT_DWELL_TIME)
	bool is_first_init_2g_scan = false;
#endif 
	p2p_scan_purpose_t	p2p_scan_purpose = P2P_SCAN_PURPOSE_MIN;

	WL_DBG(("Enter \n"));

	
	if (!cfg) {
		err = -EINVAL;
		goto exit;
	}
	if (!cfg->p2p_supported || !p2p_scan(cfg)) {
		
		WL_SCAN((" LEGACY E-SCAN START\n"));

#if defined(USE_INITIAL_SHORT_DWELL_TIME)
		if (!request) {
			err = -EINVAL;
			goto exit;
		}
		if (ndev == bcmcfg_to_prmry_ndev(cfg) && g_first_broadcast_scan == true) {
			is_first_init_2g_scan = true;
			g_first_broadcast_scan = false;
		}
#endif 

		
		if (request != NULL) {
			n_channels = request->n_channels;
			n_ssids = request->n_ssids;
			if (n_channels % 2)
				
				params_size += sizeof(u16) * (n_channels + 1);
			else
				params_size += sizeof(u16) * n_channels;

			
			params_size += sizeof(struct wlc_ssid) * n_ssids;
		}
		params = (wl_escan_params_t *) kzalloc(params_size, GFP_KERNEL);
		if (params == NULL) {
			err = -ENOMEM;
			goto exit;
		}
		wl_scan_prep(&params->params, request);

#if defined(USE_INITIAL_SHORT_DWELL_TIME)
		
		if (is_first_init_2g_scan)
			params->params.active_time = FIRST_SCAN_ACTIVE_DWELL_TIME_MS;
#endif 

		params->version = htod32(ESCAN_REQ_VERSION);
		params->action =  htod16(action);
		wl_escan_set_sync_id(params->sync_id, cfg);
		wl_escan_set_type(cfg, WL_SCANTYPE_LEGACY);
		if (params_size + sizeof("escan") >= WLC_IOCTL_MEDLEN) {
			WL_ERR(("ioctl buffer length not sufficient\n"));
			kfree(params);
			err = -ENOMEM;
			goto exit;
		}
		if (cfg->active_scan == PASSIVE_SCAN) {
			params->params.scan_type = DOT11_SCANTYPE_PASSIVE;
			WL_DBG(("Passive scan_type %d \n", params->params.scan_type));
		}

		bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);

		err = wldev_iovar_setbuf(ndev, "escan", params, params_size,
			cfg->escan_ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
		WL_ERR(("LEGACY_SCAN sync ID: %d, bssidx: %d\n", params->sync_id, bssidx));
		if (unlikely(err)) {
			if (err == BCME_EPERM)
				
				WL_DBG((" Escan not permitted at this time (%d)\n", err));
			else
				WL_ERR((" Escan set error (%d)\n", err));
		}
		kfree(params);
	}
	else if (p2p_is_on(cfg) && p2p_scan(cfg)) {
		
		s32 _freq = 0;
		n_nodfs = 0;
		if (request && request->n_channels) {
			num_chans = request->n_channels;
			WL_SCAN((" chann number : %d\n", num_chans));
			default_chan_list = kzalloc(num_chans * sizeof(*default_chan_list),
				GFP_KERNEL);
			if (default_chan_list == NULL) {
				WL_ERR(("channel list allocation failed \n"));
				err = -ENOMEM;
				goto exit;
			}
			if (!wl_get_valid_channels(ndev, chan_buf, sizeof(chan_buf))) {
				list = (wl_uint32_list_t *) chan_buf;
				n_valid_chan = dtoh32(list->count);
				for (i = 0; i < num_chans; i++)
				{
#ifdef WL_HOST_BAND_MGMT
					int channel_band = 0;
#endif 
					_freq = request->channels[i]->center_freq;
					channel = ieee80211_frequency_to_channel(_freq);
#ifdef WL_HOST_BAND_MGMT
					channel_band = (channel > CH_MAX_2G_CHANNEL) ?
						WLC_BAND_5G : WLC_BAND_2G;
					if ((cfg->curr_band != WLC_BAND_AUTO) &&
						(cfg->curr_band != channel_band) &&
						!IS_P2P_SOCIAL_CHANNEL(channel))
							continue;
#endif 

					
					if (request->channels[i]->flags &
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
						(IEEE80211_CHAN_NO_IR
						| IEEE80211_CHAN_RADAR))
#else
						(IEEE80211_CHAN_RADAR
						| IEEE80211_CHAN_PASSIVE_SCAN))
#endif
						continue;

					for (j = 0; j < n_valid_chan; j++) {
						if (channel == (dtoh32(list->element[j])))
							default_chan_list[n_nodfs++] =
								channel;
					}

				}
			}
			if (num_chans == SOCIAL_CHAN_CNT && (
						(default_chan_list[0] == SOCIAL_CHAN_1) &&
						(default_chan_list[1] == SOCIAL_CHAN_2) &&
						(default_chan_list[2] == SOCIAL_CHAN_3))) {
				
				search_state = WL_P2P_DISC_ST_SEARCH;
				p2p_scan_purpose = P2P_SCAN_SOCIAL_CHANNEL;
				WL_INFORM(("P2P SEARCH PHASE START \n"));
			} else if (((dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION1)) &&
				(wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP)) ||
				((dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION2)) &&
				(wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP))) {
				
				WL_INFORM(("Already a GO. Do SEARCH Only"));
				search_state = WL_P2P_DISC_ST_SEARCH;
				num_chans = n_nodfs;
				p2p_scan_purpose = P2P_SCAN_NORMAL;

			} else if (num_chans == 1) {
				p2p_scan_purpose = P2P_SCAN_CONNECT_TRY;
			} else if (num_chans == SOCIAL_CHAN_CNT + 1) {
				p2p_scan_purpose = P2P_SCAN_SOCIAL_CHANNEL;
			} else {
				WL_INFORM(("P2P SCAN STATE START \n"));
				num_chans = n_nodfs;
				p2p_scan_purpose = P2P_SCAN_NORMAL;
			}
		} else {
			err = -EINVAL;
			goto exit;
		}
		err = wl_cfgp2p_escan(cfg, ndev, ACTIVE_SCAN, num_chans, default_chan_list,
			search_state, action,
			wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE), NULL,
			p2p_scan_purpose);

		if (!err)
			cfg->p2p->search_state = search_state;

		kfree(default_chan_list);
	}
exit:
	if (unlikely(err)) {
		
		if ((err == BCME_EPERM) && cfg->scan_suppressed)
			WL_DBG(("Escan failed: Scan Suppressed \n"));
		else
			WL_ERR(("error (%d)\n", err));
	}
	return err;
}


static s32
wl_do_escan(struct bcm_cfg80211 *cfg, struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request)
{
	s32 err = BCME_OK;
	s32 passive_scan;
	s32 passive_scan_time;
	s32 passive_scan_time_org;
	wl_scan_results_t *results;
	WL_SCAN(("Enter \n"));
	mutex_lock(&cfg->usr_sync);

	results = wl_escan_get_buf(cfg, FALSE);
	results->version = 0;
	results->count = 0;
	results->buflen = WL_SCAN_RESULTS_FIXED_SIZE;

	cfg->escan_info.ndev = ndev;
	cfg->escan_info.wiphy = wiphy;
	cfg->escan_info.escan_state = WL_ESCAN_STATE_SCANING;
	passive_scan = cfg->active_scan ? 0 : 1;
	err = wldev_ioctl(ndev, WLC_SET_PASSIVE_SCAN,
		&passive_scan, sizeof(passive_scan), true);
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		goto exit;
	}

	if (passive_channel_skip) {

		err = wldev_ioctl(ndev, WLC_GET_SCAN_PASSIVE_TIME,
			&passive_scan_time_org, sizeof(passive_scan_time_org), false);
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN time : %d \n", passive_scan_time_org));

		passive_scan_time = 0;
		err = wldev_ioctl(ndev, WLC_SET_SCAN_PASSIVE_TIME,
			&passive_scan_time, sizeof(passive_scan_time), true);
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN SKIPED!! (passive_channel_skip:%d) \n",
			passive_channel_skip));
	}

	err = wl_run_escan(cfg, ndev, request, WL_SCAN_ACTION_START);

	if (passive_channel_skip) {
		err = wldev_ioctl(ndev, WLC_SET_SCAN_PASSIVE_TIME,
			&passive_scan_time_org, sizeof(passive_scan_time_org), true);
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN RECOVERED!! (passive_scan_time_org:%d) \n",
			passive_scan_time_org));
	}

exit:
	mutex_unlock(&cfg->usr_sync);
	return err;
}

static s32
__wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request,
	struct cfg80211_ssid *this_ssid)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct cfg80211_ssid *ssids;
	struct ether_addr primary_mac;
	bool p2p_ssid;
#ifdef WL11U
	bcm_tlv_t *interworking_ie;
#endif
	s32 err = 0;
	s32 bssidx = -1;
	s32 i;

	unsigned long flags;
	static s32 busy_count = 0;
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	struct net_device *remain_on_channel_ndev = NULL;
#endif

	if (request && (scan_req_iftype(request) == NL80211_IFTYPE_AP)) {
		WL_INFORM(("Scan Command on SoftAP Interface. Ignoring...\n"));
		return 0;
	}

	ndev = ndev_to_wlc_ndev(ndev, cfg);

	if (WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg)) {
		WL_ERR(("Sending Action Frames. Try it again.\n"));
		return -EAGAIN;
	}

	WL_DBG(("Enter wiphy (%p)\n", wiphy));
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		if (cfg->scan_request == NULL) {
			wl_clr_drv_status_all(cfg, SCANNING);
			WL_DBG(("<<<<<<<<<<<Force Clear Scanning Status>>>>>>>>>>>\n"));
		} else {
			WL_ERR(("Scanning already\n"));
			return -EAGAIN;
		}
	}
	if (wl_get_drv_status(cfg, SCAN_ABORTING, ndev)) {
		WL_ERR(("Scanning being aborted\n"));
		return -EAGAIN;
	}
	if (request && request->n_ssids > WL_SCAN_PARAMS_SSID_MAX) {
		WL_ERR(("request null or n_ssids > WL_SCAN_PARAMS_SSID_MAX\n"));
		return -EOPNOTSUPP;
	}
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	remain_on_channel_ndev = wl_cfg80211_get_remain_on_channel_ndev(cfg);
	if (remain_on_channel_ndev) {
		WL_DBG(("Remain_on_channel bit is set, somehow it didn't get cleared\n"));
		wl_notify_escan_complete(cfg, remain_on_channel_ndev, true, true);
	}
#endif 

#ifdef WL_SDO
	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
		wl_cfg80211_pause_sdo(ndev, cfg);
	}
#endif

	
	mod_timer(&cfg->scan_timeout, jiffies + msecs_to_jiffies(WL_SCAN_TIMER_INTERVAL_MS));
	if (request) {		
		ssids = request->ssids;
		p2p_ssid = false;
		for (i = 0; i < request->n_ssids; i++) {
			if (ssids[i].ssid_len &&
				IS_P2P_SSID(ssids[i].ssid, ssids[i].ssid_len)) {
				p2p_ssid = true;
				break;
			}
		}
		if (p2p_ssid) {
			if (cfg->p2p_supported) {
				
				if (p2p_on(cfg) == false) {
					
					p2p_on(cfg) = true;
					wl_cfgp2p_set_firm_p2p(cfg);
					get_primary_mac(cfg, &primary_mac);
					wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
#if defined(P2P_IE_MISSING_FIX)
					cfg->p2p_prb_noti = false;
#endif
				}
				wl_clr_p2p_status(cfg, GO_NEG_PHASE);
				WL_DBG(("P2P: GO_NEG_PHASE status cleared \n"));
				p2p_scan(cfg) = true;
			}
		} else {
			if (cfg->p2p_supported) {
				p2p_scan(cfg) = false;

				if (p2p_scan(cfg) == false) {
					if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
						err = wl_cfgp2p_discover_enable_search(cfg,
						false);
						if (unlikely(err)) {
							goto scan_out;
						}

					}
				}
			}
			if (!cfg->p2p_supported || !p2p_scan(cfg)) {
				if ((bssidx = wl_get_bssidx_by_wdev(cfg,
					ndev->ieee80211_ptr)) < 0) {
					WL_ERR(("Find p2p index from ndev(%p) failed\n",
						ndev));
					err = BCME_ERROR;
					goto scan_out;
				}
#ifdef WL11U
				if ((interworking_ie = wl_cfg80211_find_interworking_ie(
					(u8 *)request->ie, request->ie_len)) != NULL) {
					err = wl_cfg80211_add_iw_ie(cfg, ndev, bssidx,
					       VNDR_IE_CUSTOM_FLAG, interworking_ie->id,
					       interworking_ie->data, interworking_ie->len);

					if (unlikely(err)) {
						goto scan_out;
					}
				} else if (cfg->iw_ie_len != 0) {
				
					wl_cfg80211_add_iw_ie(cfg, ndev, bssidx,
						VNDR_IE_CUSTOM_FLAG,
						DOT11_MNG_INTERWORKING_ID,
						0, 0);

					(void)wldev_iovar_setint_bsscfg(ndev, "grat_arp", 0,
						bssidx);
					cfg->wl11u = FALSE;
					cfg->iw_ie_len = 0;
					memset(cfg->iw_ie, 0, IW_IES_MAX_BUF_LEN);
					
				}
#endif 
				err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(ndev),
					bssidx, VNDR_IE_PRBREQ_FLAG, request->ie,
					request->ie_len);

				if (unlikely(err)) {
					goto scan_out;
				}

			}
		}
	} else {		
		ssids = this_ssid;
	}

	if (request && cfg->p2p_supported && !p2p_scan(cfg)) {
		WL_TRACE_HW4(("START SCAN\n"));
		DHD_DISABLE_RUNTIME_PM((dhd_pub_t *)(cfg->pub));
	}

	cfg->scan_request = request;
	wl_set_drv_status(cfg, SCANNING, ndev);

	if (cfg->p2p_supported) {
		if (p2p_on(cfg) && p2p_scan(cfg)) {

#ifdef WL_SDO
			if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
				WL_SD(("P2P_FIND: Discovery offload is in progress."
					" Do nothing\n"));
				err = -EINVAL;
				goto scan_out;
			}
#endif
			
			cfg->afx_hdl->my_listen_chan =
				wl_find_listen_channel(cfg, request->ie,
				request->ie_len);
			err = wl_cfgp2p_enable_discovery(cfg, ndev,
			request->ie, request->ie_len);

			if (unlikely(err)) {
				goto scan_out;
			}
		}
	}
	err = wl_do_escan(cfg, wiphy, ndev, request);
	if (likely(!err))
		goto scan_success;
	else
		goto scan_out;

scan_success:
	busy_count = 0;

	return 0;

scan_out:
	if (err == BCME_BUSY || err == BCME_NOTREADY) {
		WL_ERR(("Scan err = (%d), busy?%d", err, -EBUSY));
		err = -EBUSY;
	}

#define SCAN_EBUSY_RETRY_LIMIT 10
	if (err == -EBUSY) {
		if (busy_count++ > SCAN_EBUSY_RETRY_LIMIT) {
			struct ether_addr bssid;
			s32 ret = 0;
			busy_count = 0;
			WL_ERR(("Unusual continuous EBUSY error, %d %d %d %d %d %d %d %d %d\n",
				wl_get_drv_status(cfg, SCANNING, ndev),
				wl_get_drv_status(cfg, SCAN_ABORTING, ndev),
				wl_get_drv_status(cfg, CONNECTING, ndev),
				wl_get_drv_status(cfg, CONNECTED, ndev),
				wl_get_drv_status(cfg, DISCONNECTING, ndev),
				wl_get_drv_status(cfg, AP_CREATING, ndev),
				wl_get_drv_status(cfg, AP_CREATED, ndev),
				wl_get_drv_status(cfg, SENDING_ACT_FRM, ndev),
				wl_get_drv_status(cfg, SENDING_ACT_FRM, ndev)));

			bzero(&bssid, sizeof(bssid));
			if ((ret = wldev_ioctl(ndev, WLC_GET_BSSID,
				&bssid, ETHER_ADDR_LEN, false)) == 0)
				WL_ERR(("FW is connected with " MACDBG "/n",
					MAC2STRDBG(bssid.octet)));
			else
				WL_ERR(("GET BSSID failed with %d\n", ret));

			wl_cfg80211_scan_abort(cfg);

		}
	} else {
		busy_count = 0;
	}

	wl_clr_drv_status(cfg, SCANNING, ndev);
	if (timer_pending(&cfg->scan_timeout))
		del_timer_sync(&cfg->scan_timeout);
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	cfg->scan_request = NULL;
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);

#ifdef WL_SDO
	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
		wl_cfg80211_resume_sdo(ndev, cfg);
	}
#endif
	return err;
}

#if defined(WL_CFG80211_P2P_DEV_IF)
static s32
wl_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
#else
static s32
wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request)
#endif 
{
	s32 err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#if defined(WL_CFG80211_P2P_DEV_IF)
	struct net_device *ndev = wdev_to_wlc_ndev(request->wdev, cfg);
#endif 

	WL_DBG(("Enter\n"));
	RETURN_EIO_IF_NOT_UP(cfg);

	if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
		if (wl_cfg_multip2p_operational(cfg)) {
			WL_ERR(("wlan0 scan failed, p2p devices are operational"));
			 return -ENODEV;
		}
	}

	err = __wl_cfg80211_scan(wiphy, ndev, request, NULL);
	if (unlikely(err)) {
		if ((err == BCME_EPERM) && cfg->scan_suppressed)
			WL_DBG(("scan not permitted at this time (%d)\n", err));
		else
			WL_ERR(("scan error (%d)\n", err));
		return err;
	}

	return err;
}

static s32 wl_set_rts(struct net_device *dev, u32 rts_threshold)
{
	s32 err = 0;

	err = wldev_iovar_setint(dev, "rtsthresh", rts_threshold);
	if (unlikely(err)) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}
	return err;
}

static s32 wl_set_frag(struct net_device *dev, u32 frag_threshold)
{
	s32 err = 0;

	err = wldev_iovar_setint_bsscfg(dev, "fragthresh", frag_threshold, 0);
	if (unlikely(err)) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}
	return err;
}

static s32 wl_set_retry(struct net_device *dev, u32 retry, bool l)
{
	s32 err = 0;
	u32 cmd = (l ? WLC_SET_LRL : WLC_SET_SRL);

	retry = htod32(retry);
	err = wldev_ioctl(dev, cmd, &retry, sizeof(retry), true);
	if (unlikely(err)) {
		WL_ERR(("cmd (%d) , error (%d)\n", cmd, err));
		return err;
	}
	return err;
}

static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = 0;

	RETURN_EIO_IF_NOT_UP(cfg);
	WL_DBG(("Enter\n"));
	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
		(cfg->conf->rts_threshold != wiphy->rts_threshold)) {
		cfg->conf->rts_threshold = wiphy->rts_threshold;
		err = wl_set_rts(ndev, cfg->conf->rts_threshold);
		if (err != BCME_OK)
			return err;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
		(cfg->conf->frag_threshold != wiphy->frag_threshold)) {
		cfg->conf->frag_threshold = wiphy->frag_threshold;
		err = wl_set_frag(ndev, cfg->conf->frag_threshold);
		if (err != BCME_OK)
			return err;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG &&
		(cfg->conf->retry_long != wiphy->retry_long)) {
		cfg->conf->retry_long = wiphy->retry_long;
		err = wl_set_retry(ndev, cfg->conf->retry_long, true);
		if (err != BCME_OK)
			return err;
	}
	if (changed & WIPHY_PARAM_RETRY_SHORT &&
		(cfg->conf->retry_short != wiphy->retry_short)) {
		cfg->conf->retry_short = wiphy->retry_short;
		err = wl_set_retry(ndev, cfg->conf->retry_short, false);
		if (err != BCME_OK) {
			return err;
		}
	}

	return err;
}
static chanspec_t
channel_to_chanspec(struct wiphy *wiphy, struct net_device *dev, u32 channel, u32 bw_cap)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	u8 *buf = NULL;
	wl_uint32_list_t *list;
	int err = BCME_OK;
	chanspec_t c = 0, ret_c = 0;
	int bw = 0, tmp_bw = 0;
	int i;
	u32 tmp_c;
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
#define LOCAL_BUF_SIZE	1024
	buf = (u8 *) kzalloc(LOCAL_BUF_SIZE, kflags);
	if (!buf) {
		WL_ERR(("buf memory alloc failed\n"));
		goto exit;
	}
	list = (wl_uint32_list_t *)(void *)buf;
	list->count = htod32(WL_NUMCHANSPECS);
	err = wldev_iovar_getbuf_bsscfg(dev, "chanspecs", NULL,
		0, buf, LOCAL_BUF_SIZE, 0, &cfg->ioctl_buf_sync);
	if (err != BCME_OK) {
		WL_ERR(("get chanspecs failed with %d\n", err));
		goto exit;
	}
	for (i = 0; i < dtoh32(list->count); i++) {
		c = dtoh32(list->element[i]);
		if (channel <= CH_MAX_2G_CHANNEL) {
			if (!CHSPEC_IS20(c))
				continue;
			if (channel == CHSPEC_CHANNEL(c)) {
				ret_c = c;
				bw = 20;
				goto exit;
			}
		}
		tmp_c = wf_chspec_ctlchan(c);
		tmp_bw = bw2cap[CHSPEC_BW(c) >> WL_CHANSPEC_BW_SHIFT];
		if (tmp_c != channel)
			continue;

		if ((tmp_bw > bw) && (tmp_bw <= bw_cap)) {
			bw = tmp_bw;
			ret_c = c;
			if (bw == bw_cap)
				goto exit;
		}
	}
exit:
	if (buf)
		kfree(buf);
#undef LOCAL_BUF_SIZE
	WL_INFORM(("return chanspec %x %d\n", ret_c, bw));
	return ret_c;
}

void
wl_cfg80211_ibss_vsie_set_buffer(vndr_ie_setbuf_t *ibss_vsie, int ibss_vsie_len)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	if (cfg != NULL && ibss_vsie != NULL) {
		if (cfg->ibss_vsie != NULL) {
			kfree(cfg->ibss_vsie);
		}
		cfg->ibss_vsie = ibss_vsie;
		cfg->ibss_vsie_len = ibss_vsie_len;
	}
}

static void
wl_cfg80211_ibss_vsie_free(struct bcm_cfg80211 *cfg)
{
	
	if (cfg->ibss_vsie != NULL) {
		kfree(cfg->ibss_vsie);
		cfg->ibss_vsie = NULL;
		cfg->ibss_vsie_len = 0;
	}
}

s32
wl_cfg80211_ibss_vsie_delete(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	char *ioctl_buf = NULL;
	s32 ret = BCME_OK;

	if (cfg != NULL && cfg->ibss_vsie != NULL) {
		ioctl_buf = kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
		if (!ioctl_buf) {
			WL_ERR(("ioctl memory alloc failed\n"));
			return -ENOMEM;
		}

		
		strncpy(cfg->ibss_vsie->cmd, "del", VNDR_IE_CMD_LEN - 1);
		cfg->ibss_vsie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

		ret = wldev_iovar_setbuf(dev, "ie",
			cfg->ibss_vsie, cfg->ibss_vsie_len,
			ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
		WL_ERR(("ret=%d\n", ret));

		if (ret == BCME_OK) {
			
			kfree(cfg->ibss_vsie);
			cfg->ibss_vsie = NULL;
			cfg->ibss_vsie_len = 0;
		}

		if (ioctl_buf) {
			kfree(ioctl_buf);
		}
	}

	return ret;
}

#ifdef WLAIBSS_MCHAN
static bcm_struct_cfgdev*
bcm_cfg80211_add_ibss_if(struct wiphy *wiphy, char *name)
{
	int err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wireless_dev* wdev = NULL;
	struct net_device *new_ndev = NULL;
	struct net_device *primary_ndev = NULL;
	s32 timeout;
	wl_aibss_if_t aibss_if;
	wl_if_event_info *event = NULL;

	if (cfg->ibss_cfgdev != NULL) {
		WL_ERR(("IBSS interface %s already exists\n", name));
		return NULL;
	}

	WL_ERR(("Try to create IBSS interface %s\n", name));
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);
	
	get_primary_mac(cfg, &cfg->ibss_if_addr);
	cfg->ibss_if_addr.octet[4] ^= 0x40;
	memset(&aibss_if, sizeof(aibss_if), 0);
	memcpy(&aibss_if.addr, &cfg->ibss_if_addr, sizeof(aibss_if.addr));
	aibss_if.chspec = 0;
	aibss_if.len = sizeof(aibss_if);

	cfg->bss_pending_op = TRUE;
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
	err = wldev_iovar_setbuf(primary_ndev, "aibss_ifadd", &aibss_if,
		sizeof(aibss_if), cfg->ioctl_buf, WLC_IOCTL_MAXLEN, NULL);
	if (err) {
		WL_ERR(("IOVAR aibss_ifadd failed with error %d\n", err));
		goto fail;
	}
	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op)
		goto fail;

	event = &cfg->if_event_info;
	new_ndev = wl_cfg80211_allocate_if(cfg, event->ifidx, event->name,
		event->mac, event->bssidx, event->name);
	if (new_ndev == NULL)
		goto fail;
	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (wdev == NULL)
		goto fail;
	wdev->wiphy = wiphy;
	wdev->iftype = NL80211_IFTYPE_ADHOC;
	wdev->netdev = new_ndev;
	new_ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(new_ndev, wiphy_dev(wdev->wiphy));

	ASSERT_RTNL();
	if (wl_cfg80211_register_if(cfg, event->ifidx, new_ndev) != BCME_OK)
		goto fail;

	wl_alloc_netinfo(cfg, new_ndev, wdev, WL_MODE_IBSS, PM_ENABLE, event->bssidx);
	cfg->ibss_cfgdev = ndev_to_cfgdev(new_ndev);
	WL_ERR(("IBSS interface %s created\n", new_ndev->name));
	return cfg->ibss_cfgdev;

fail:
	WL_ERR(("failed to create IBSS interface %s \n", name));
	cfg->bss_pending_op = FALSE;
	if (new_ndev)
		wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev);
	if (wdev)
		kfree(wdev);
	return NULL;
}

static s32
bcm_cfg80211_del_ibss_if(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev)
{
	int err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = NULL;
	struct net_device *primary_ndev = NULL;
	s32 timeout;

	if (!cfgdev || cfg->ibss_cfgdev != cfgdev || ETHER_ISNULLADDR(&cfg->ibss_if_addr.octet))
		return -EINVAL;
	ndev = (struct net_device *)cfgdev_to_ndev(cfg->ibss_cfgdev);
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	cfg->bss_pending_op = TRUE;
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
	err = wldev_iovar_setbuf(primary_ndev, "aibss_ifdel", &cfg->ibss_if_addr,
		sizeof(cfg->ibss_if_addr), cfg->ioctl_buf, WLC_IOCTL_MAXLEN, NULL);
	if (err) {
		WL_ERR(("IOVAR aibss_ifdel failed with error %d\n", err));
		goto fail;
	}
	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op) {
		WL_ERR(("timeout in waiting IF_DEL event\n"));
		goto fail;
	}

	wl_cfg80211_remove_if(cfg, cfg->if_event_info.ifidx, ndev);
	cfg->ibss_cfgdev = NULL;
	return 0;

fail:
	cfg->bss_pending_op = FALSE;
	return -1;
}
#endif 

s32
wl_cfg80211_interface_ops(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr)
{
	wl_interface_create_t iface;
	s32 ret;
	wl_interface_info_t *info;

	bzero(&iface, sizeof(wl_interface_create_t));

	iface.ver = WL_INTERFACE_CREATE_VER;

	if (iface_type == NL80211_IFTYPE_AP)
		iface.flags = WL_INTERFACE_CREATE_AP;
	else
		iface.flags = WL_INTERFACE_CREATE_STA;

	if (del) {
		ret = wldev_iovar_setbuf(ndev, "interface_remove",
			NULL, 0, cfg->ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
	} else {
		if (addr) {
			memcpy(&iface.mac_addr.octet, addr, ETH_ALEN);
			iface.flags |= WL_INTERFACE_MAC_USE;
		}
		ret = wldev_iovar_getbuf(ndev, "interface_create",
			&iface, sizeof(wl_interface_create_t),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (ret == 0) {
			
			info = (wl_interface_info_t *)cfg->ioctl_buf;
			WL_DBG(("wl interface create success!! bssidx:%d \n",
				info->bsscfgidx));
			ret = info->bsscfgidx;
		}
	}

	if (ret < 0)
		WL_ERR(("Interface %s failed!! ret %d\n",
			del ? "remove" : "create", ret));

	return ret;
}


s32
wl_cfg80211_add_del_bss(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr)
{
	s32 ret = BCME_OK;
	s32 val = 0;

	struct {
		s32 cfg;
		s32 val;
		struct ether_addr ea;
	} bss_setbuf;

	WL_INFORM(("iface_type:%d del:%d \n", iface_type, del));

	bzero(&bss_setbuf, sizeof(bss_setbuf));

	
	if (del) {
		val = -1;
	} else if (iface_type == NL80211_IFTYPE_AP) {
		
		WL_DBG(("Adding AP Interface \n"));
		val = 3;
	} else if (iface_type == NL80211_IFTYPE_STATION) {
		WL_DBG(("Adding STA Interface \n"));
		val = 2;
	} else {
		WL_ERR((" add_del_bss NOT supported for IFACE type:0x%x", iface_type));
		return -EINVAL;
	}

	bss_setbuf.cfg = htod32(bsscfg_idx);
	bss_setbuf.val = htod32(val);

	if (addr) {
		memcpy(&bss_setbuf.ea.octet, addr, ETH_ALEN);
	}

	ret = wldev_iovar_setbuf(ndev, "bss", &bss_setbuf, sizeof(bss_setbuf),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (ret != 0)
		WL_ERR(("'bss %d' failed with %d\n", val, ret));

	return ret;
}

#if defined(WL_VIRTUAL_APSTA) || defined(DUAL_STA_STATIC_IF)
bcm_struct_cfgdev*
wl_cfg80211_create_iface(struct wiphy *wiphy,
	enum nl80211_iftype iface_type,
	u8 *mac_addr, const char *name)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *new_ndev = NULL;
	struct net_device *primary_ndev = NULL;
	s32 ret = BCME_OK;
	s32 bsscfg_idx = 0;
	u32 timeout;
	wl_if_event_info *event = NULL;
	struct wireless_dev *wdev = NULL;
	u8 addr[ETH_ALEN];

	WL_DBG(("Enter\n"));

	if (!name) {
		WL_ERR(("Interface name not provided\n"));
		return NULL;
	}

	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	if (likely(!mac_addr)) {
		memcpy(addr, primary_ndev->dev_addr, ETH_ALEN);
		addr[0] |= 0x02;
	} else {
		
		memcpy(addr, mac_addr, ETH_ALEN);
	}

	if ((iface_type != NL80211_IFTYPE_STATION) && (iface_type != NL80211_IFTYPE_AP)) {
		WL_ERR(("IFACE type:%d not supported. STA "
					"or AP IFACE is only supported\n", iface_type));
		return NULL;
	}

	cfg->bss_pending_op = TRUE;
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));

	
	if (p2p_is_on(cfg)) {
		WL_DBG(("Disabling P2P Discovery Interface \n"));
#ifdef WL_CFG80211_P2P_DEV_IF
		ret = wl_cfg80211_scan_stop(bcmcfg_to_p2p_wdev(cfg));
#else
		ret = wl_cfg80211_scan_stop(cfg->p2p_net);
#endif
		if (unlikely(ret < 0)) {
			CFGP2P_ERR(("P2P scan stop failed, ret=%d\n", ret));
		}

		wl_cfgp2p_disable_discovery(cfg);
		wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = 0;
		p2p_on(cfg) = false;
	}

	ret = wl_cfg80211_interface_ops(cfg, primary_ndev, bsscfg_idx,
		NL80211_IFTYPE_STATION, 0, addr);
	if (ret == BCME_UNSUPPORTED) {
		
		bsscfg_idx = 1;
		if ((ret = wl_cfg80211_add_del_bss(cfg, primary_ndev,
			bsscfg_idx, iface_type, 0, addr)) < 0) {
			return NULL;
		}
	} else if (ret < 0) {
		WL_ERR(("Interface create failed!! ret:%d \n", ret));
		goto fail;
	} else {
		
		bsscfg_idx = ret;
	}

	WL_DBG(("Interface created!! bssidx:%d \n", bsscfg_idx));

	WL_DBG(("Wait for the FW I/F Event\n"));
	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op) {
		WL_ERR(("ADD_IF event, didn't come. Return \n"));
		goto fail;
	}

	event = &cfg->if_event_info;
	new_ndev = wl_cfg80211_allocate_if(cfg, event->ifidx,
		(char*)name, addr, event->bssidx, event->name);
	if (!new_ndev) {
		WL_ERR(("I/F allocation failed! \n"));
		goto fail;
	} else
		WL_DBG(("I/F allocation succeeded! ifidx:0x%x bssidx:0x%x \n",
		 event->ifidx, event->bssidx));

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (!wdev) {
		WL_ERR(("wireless_dev alloc failed! \n"));
		goto fail;
	}

	wdev->wiphy = wiphy;
	wdev->iftype = iface_type;
	new_ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(new_ndev, wiphy_dev(wdev->wiphy));

	
	ASSERT_RTNL();

	
	if (memcmp(addr, event->mac, ETH_ALEN) != 0) {
		ret = wldev_iovar_setbuf_bsscfg(primary_ndev, "cur_etheraddr",
			addr, ETH_ALEN, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
			event->bssidx, &cfg->ioctl_buf_sync);
		if (unlikely(ret)) {
				WL_ERR(("set cur_etheraddr Error (%d)\n", ret));
				goto fail;
		}
		memcpy(new_ndev->dev_addr, addr, ETH_ALEN);
	}

	if (wl_cfg80211_register_if(cfg, event->ifidx, new_ndev) != BCME_OK) {
		WL_ERR(("IFACE register failed \n"));
		goto fail;
	}

	
	wl_alloc_netinfo(cfg, new_ndev, wdev,
		(iface_type == NL80211_IFTYPE_STATION) ?
		WL_MODE_BSS : WL_MODE_AP, PM_ENABLE, event->bssidx);
	cfg->bss_cfgdev = ndev_to_cfgdev(new_ndev);
	cfg->cfgdev_bssidx = event->bssidx;

	WL_DBG(("Host Network Interface for Secondary I/F created"));

	return cfg->bss_cfgdev;

fail:
	cfg->bss_pending_op = FALSE;
	cfg->cfgdev_bssidx = -1;
	if (wdev)
		kfree(wdev);
	if (new_ndev)
		wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev);

	return NULL;
}

s32
wl_cfg80211_del_iface(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = NULL;
	struct net_device *primary_ndev = NULL;
	s32 ret = BCME_OK;
	s32 bsscfg_idx = 1;
	u32 timeout;
	u32 ifidx;
	enum nl80211_iftype iface_type = NL80211_IFTYPE_STATION;

	WL_DBG(("Enter\n"));

	if (!cfg->bss_cfgdev)
		return 0;

	
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		WL_DBG(("Scan in progress. Aborting the scan!\n"));
		wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
	}

	ndev = (struct net_device *)cfgdev_to_ndev(cfg->bss_cfgdev);
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	cfg->bss_pending_op = TRUE;
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));

	ret = wl_cfg80211_interface_ops(cfg, ndev, cfg->cfgdev_bssidx,
		NL80211_IFTYPE_STATION, 1, NULL);
	if (ret == BCME_UNSUPPORTED) {
		if ((ret = wl_cfg80211_add_del_bss(cfg, ndev,
			bsscfg_idx, iface_type, true, NULL)) < 0) {
			WL_ERR(("DEL bss failed ret:%d \n", ret));
			goto exit;
		}
	} else if (ret < 0) {
		WL_ERR(("Interface DEL failed ret:%d \n", ret));
		goto exit;
	}

	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op) {
		WL_ERR(("timeout in waiting IF_DEL event\n"));
	}

exit:
	ifidx = dhd_net2idx(((struct dhd_pub *)(cfg->pub))->info, ndev);
	wl_cfg80211_remove_if(cfg, ifidx, ndev);
	cfg->bss_cfgdev = NULL;
	cfg->cfgdev_bssidx = -1;
	cfg->bss_pending_op = FALSE;

	WL_DBG(("IF_DEL Done.\n"));

	return ret;
}
#endif 

static s32
wl_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_ibss_params *params)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct cfg80211_bss *bss;
	struct ieee80211_channel *chan;
	struct wl_join_params join_params;
	int scan_suppress;
	struct cfg80211_ssid ssid;
	s32 scan_retry = 0;
	s32 err = 0;
	size_t join_params_size;
	chanspec_t chanspec = 0;
	u32 param[2] = {0, 0};
	u32 bw_cap = 0;
#if defined(WLAIBSS) && defined(WLAIBSS_PS)
	s32 atim = 0;

#endif 

	WL_TRACE(("In\n"));
	RETURN_EIO_IF_NOT_UP(cfg);
	WL_INFORM(("JOIN BSSID:" MACDBG "\n", MAC2STRDBG(params->bssid)));
	if (!params->ssid || params->ssid_len <= 0) {
		WL_ERR(("Invalid parameter\n"));
		return -EINVAL;
	}
#if defined(WL_CFG80211_P2P_DEV_IF)
	chan = params->chandef.chan;
#else
	chan = params->channel;
#endif 
	if (chan)
		cfg->channel = ieee80211_frequency_to_channel(chan->center_freq);
	if (wl_get_drv_status(cfg, CONNECTED, dev)) {
		struct wlc_ssid *lssid = (struct wlc_ssid *)wl_read_prof(cfg, dev, WL_PROF_SSID);
		u8 *bssid = (u8 *)wl_read_prof(cfg, dev, WL_PROF_BSSID);
		u32 *channel = (u32 *)wl_read_prof(cfg, dev, WL_PROF_CHAN);
		if (!params->bssid || ((memcmp(params->bssid, bssid, ETHER_ADDR_LEN) == 0) &&
			(memcmp(params->ssid, lssid->SSID, lssid->SSID_len) == 0) &&
			(*channel == cfg->channel))) {
			WL_ERR(("Connection already existed to " MACDBG "\n",
				MAC2STRDBG((u8 *)wl_read_prof(cfg, dev, WL_PROF_BSSID))));
			return -EISCONN;
		}
		WL_ERR(("Ignore Previous connecton to %s (" MACDBG ")\n",
			lssid->SSID, MAC2STRDBG(bssid)));
	}

	
	wl_cfg80211_ibss_vsie_delete(dev);

	bss = cfg80211_get_ibss(wiphy, NULL, params->ssid, params->ssid_len);
	if (!bss) {
		if (IBSS_INITIAL_SCAN_ALLOWED == TRUE) {
			memcpy(ssid.ssid, params->ssid, params->ssid_len);
			ssid.ssid_len = params->ssid_len;
			do {
				if (unlikely
					(__wl_cfg80211_scan(wiphy, dev, NULL, &ssid) ==
					 -EBUSY)) {
					wl_delay(150);
				} else {
					break;
				}
			} while (++scan_retry < WL_SCAN_RETRY_MAX);


			
			schedule_timeout_interruptible(msecs_to_jiffies(4000));

			bss = cfg80211_get_ibss(wiphy, NULL,
				params->ssid, params->ssid_len);
		}
	}
	if (bss && ((IBSS_COALESCE_ALLOWED == TRUE) ||
		((IBSS_COALESCE_ALLOWED == FALSE) && params->bssid &&
		!memcmp(bss->bssid, params->bssid, ETHER_ADDR_LEN)))) {
		cfg->ibss_starter = false;
		WL_DBG(("Found IBSS\n"));
	} else {
		cfg->ibss_starter = true;
	}
	if (chan) {
		if (chan->band == IEEE80211_BAND_5GHZ)
			param[0] = WLC_BAND_5G;
		else if (chan->band == IEEE80211_BAND_2GHZ)
			param[0] = WLC_BAND_2G;
		err = wldev_iovar_getint(dev, "bw_cap", param);
		WL_ERR(("bw_cap param %p\n", &param));
		if (unlikely(err)) {
			WL_ERR(("Get bw_cap Failed (%d)\n", err));
			return err;
		}
		bw_cap = param[0];
		chanspec = channel_to_chanspec(wiphy, dev, cfg->channel, bw_cap);
	}
	memset(&join_params, 0, sizeof(join_params));
	memcpy((void *)join_params.ssid.SSID, (void *)params->ssid,
		params->ssid_len);
	join_params.ssid.SSID_len = htod32(params->ssid_len);
	if (params->bssid) {
		memcpy(&join_params.params.bssid, params->bssid, ETHER_ADDR_LEN);
		err = wldev_ioctl(dev, WLC_SET_DESIRED_BSSID, &join_params.params.bssid,
			ETHER_ADDR_LEN, true);
		if (unlikely(err)) {
			WL_ERR(("Error (%d)\n", err));
			return err;
		}
	} else
		memset(&join_params.params.bssid, 0, ETHER_ADDR_LEN);
	wldev_iovar_setint(dev, "ibss_coalesce_allowed", IBSS_COALESCE_ALLOWED);

	if (IBSS_INITIAL_SCAN_ALLOWED == FALSE) {
		scan_suppress = TRUE;
		
		err = wldev_ioctl(dev, WLC_SET_SCANSUPPRESS,
			&scan_suppress, sizeof(int), true);
		if (unlikely(err)) {
			WL_ERR(("Scan Suppress Setting Failed (%d)\n", err));
			return err;
		}
	}

	join_params.params.chanspec_list[0] = chanspec;
	join_params.params.chanspec_num = 1;
	wldev_iovar_setint(dev, "chanspec", chanspec);
	join_params_size = sizeof(join_params);

	
	wldev_iovar_setint(dev, "wpa_auth", WPA_AUTH_DISABLED);
	wldev_iovar_setint(dev, "wsec", 0);

#ifdef WLAIBSS
	
	err = wldev_iovar_setint(dev, "aibss", TRUE);

	if (unlikely(err)) {
		WL_ERR(("Enable custom IBSS mode failed (%d)\n", err));
		return err;
	}
#ifdef WLAIBSS_PS
	err = wldev_ioctl(dev, WLC_GET_ATIM, &atim, sizeof(int), true);
	if (unlikely(err)) {
		WL_ERR(("Getting custom IBSS ATIM value failed (%d)\n", err));
		return err;
	}
	if (!atim) {
		atim = 10;
	}
	err = wldev_ioctl(dev, WLC_SET_ATIM, &atim, sizeof(int), true);
	if (unlikely(err)) {
		WL_ERR(("Enable custom IBSS ATIM mode failed (%d)\n", err));
		return err;
	}
#endif 
#endif 

	err = wldev_ioctl(dev, WLC_SET_SSID, &join_params,
		join_params_size, true);
	if (unlikely(err)) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}

	if (IBSS_INITIAL_SCAN_ALLOWED == FALSE) {
		scan_suppress = FALSE;
		
		err = wldev_ioctl(dev, WLC_SET_SCANSUPPRESS,
			&scan_suppress, sizeof(int), true);
		if (unlikely(err)) {
			WL_ERR(("Reset Scan Suppress Flag Failed (%d)\n", err));
			return err;
		}
	}
	wl_update_prof(cfg, dev, NULL, &join_params.ssid, WL_PROF_SSID);
	wl_update_prof(cfg, dev, NULL, &cfg->channel, WL_PROF_CHAN);
#ifdef WLAIBSS
	cfg->aibss_txfail_seq = 0;	
#endif 
	cfg->rmc_event_seq = 0; 
	return err;
}

static s32 wl_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	scb_val_t scbval;
	u8 *curbssid;

	RETURN_EIO_IF_NOT_UP(cfg);
	wl_link_down(cfg);

	WL_ERR(("Leave IBSS\n"));
	curbssid = wl_read_prof(cfg, dev, WL_PROF_BSSID);
	wl_set_drv_status(cfg, DISCONNECTING, dev);
	scbval.val = 0;
	memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
	err = wldev_ioctl(dev, WLC_DISASSOC, &scbval,
		sizeof(scb_val_t), true);
	if (unlikely(err)) {
		wl_clr_drv_status(cfg, DISCONNECTING, dev);
		WL_ERR(("error(%d)\n", err));
		return err;
	}

	
	wl_cfg80211_ibss_vsie_delete(dev);

	return err;
}

#ifdef MFP
static int wl_cfg80211_get_rsn_capa(bcm_tlv_t *wpa2ie, u8* capa)
{
	u16 suite_count;
	wpa_suite_mcast_t *mcast;
	wpa_suite_ucast_t *ucast;
	u16 len;
	wpa_suite_auth_key_mgmt_t *mgmt;

	if (!wpa2ie)
		return -1;

	len = wpa2ie->len;
	mcast = (wpa_suite_mcast_t *)&wpa2ie->data[WPA2_VERSION_LEN];
	if ((len -= WPA_SUITE_LEN) <= 0)
		return BCME_BADLEN;
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	suite_count = ltoh16_ua(&ucast->count);
	if ((suite_count > NL80211_MAX_NR_CIPHER_SUITES) ||
		(len -= (WPA_IE_SUITE_COUNT_LEN +
		(WPA_SUITE_LEN * suite_count))) <= 0)
		return BCME_BADLEN;

	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[suite_count];
	suite_count = ltoh16_ua(&mgmt->count);

	if ((suite_count > NL80211_MAX_NR_CIPHER_SUITES) ||
		(len -= (WPA_IE_SUITE_COUNT_LEN +
		(WPA_SUITE_LEN * suite_count))) >= RSN_CAP_LEN) {
		capa[0] = *(u8 *)&mgmt->list[suite_count];
		capa[1] = *((u8 *)&mgmt->list[suite_count] + 1);
	} else
		return BCME_BADLEN;

	return 0;
}
#endif 

static s32
wl_set_wpa_version(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		val = WPA_AUTH_PSK |
#ifdef BCMCCX
			WPA_AUTH_CCKM |
#endif
			WPA_AUTH_UNSPECIFIED;
	else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		val = WPA2_AUTH_PSK|
#ifdef BCMCCX
			WPA2_AUTH_CCKM |
#endif
			WPA2_AUTH_UNSPECIFIED;
	else
		val = WPA_AUTH_DISABLED;

	if (is_wps_conn(sme))
		val = WPA_AUTH_DISABLED;

#ifdef BCMWAPI_WPI
	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1) {
		WL_DBG((" * wl_set_wpa_version, set wpa_auth"
			" to WPA_AUTH_WAPI 0x400"));
		val = WAPI_AUTH_PSK | WAPI_AUTH_UNSPECIFIED;
	}
#endif
	WL_DBG(("setting wpa_auth to 0x%0x\n", val));
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", val, bssidx);
	if (unlikely(err)) {
		WL_ERR(("set wpa_auth failed (%d)\n", err));
		return err;
	}
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->wpa_versions = sme->crypto.wpa_versions;
	return err;
}

#ifdef BCMWAPI_WPI
static s32
wl_set_set_wapi_ie(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	WL_DBG((" %s \n", __FUNCTION__));

	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1) {
		err = wldev_iovar_setbuf_bsscfg(dev, "wapiie", (void *)sme->ie, sme->ie_len,
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);

		if (unlikely(err)) {
			WL_ERR(("===> set_wapi_ie Error (%d)\n", err));
			return err;
		}
	} else
		WL_DBG((" * skip \n"));
	return err;
}
#endif 

static s32
wl_set_auth_type(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	switch (sme->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		val = WL_AUTH_OPEN_SYSTEM;
		WL_DBG(("open system\n"));
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		val = WL_AUTH_SHARED_KEY;
		WL_DBG(("shared key\n"));
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		val = WL_AUTH_OPEN_SHARED;
		WL_DBG(("automatic\n"));
		break;
#ifdef BCMCCX
	case NL80211_AUTHTYPE_NETWORK_EAP:
		WL_DBG(("network eap\n"));
		val = DOT11_LEAP_AUTH;
		break;
#endif
	default:
		val = 2;
		WL_ERR(("invalid auth type (%d)\n", sme->auth_type));
		break;
	}

	err = wldev_iovar_setint_bsscfg(dev, "auth", val, bssidx);
	if (unlikely(err)) {
		WL_ERR(("set auth failed (%d)\n", err));
		return err;
	}
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->auth_type = sme->auth_type;
	return err;
}

static s32
wl_set_set_cipher(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wl_security *sec;
	s32 pval = 0;
	s32 gval = 0;
	s32 err = 0;
	s32 wsec_val = 0;

#ifdef BCMWAPI_WPI
	s32 val = 0;
#endif
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (sme->crypto.n_ciphers_pairwise) {
		switch (sme->crypto.ciphers_pairwise[0]) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			pval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			pval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_AES_CMAC:
			pval = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WLAN_CIPHER_SUITE_SMS4:
			val = SMS4_ENABLED;
			pval = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("invalid cipher pairwise (%d)\n",
				sme->crypto.ciphers_pairwise[0]));
			return -EINVAL;
		}
	}
#if defined(BCMSUP_4WAY_HANDSHAKE) && defined(WLAN_AKM_SUITE_FT_8021X)
	if (cfg->wdev->wiphy->features & NL80211_FEATURE_FW_4WAY_HANDSHAKE) {
		if (pval == AES_ENABLED)
			err = wldev_iovar_setint_bsscfg(dev, "sup_wpa", 1, bssidx);
		else
			err = wldev_iovar_setint_bsscfg(dev, "sup_wpa", 0, bssidx);

		if (err) {
			WL_ERR(("FBT: Error setting sup_wpa (%d)\n", err));
			return err;
		}
	}
#endif 
	if (sme->crypto.cipher_group) {
		switch (sme->crypto.cipher_group) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			gval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			gval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			gval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			gval = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WLAN_CIPHER_SUITE_SMS4:
			val = SMS4_ENABLED;
			gval = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("invalid cipher group (%d)\n",
				sme->crypto.cipher_group));
			return -EINVAL;
		}
	}

	WL_DBG(("pval (%d) gval (%d)\n", pval, gval));

	if (is_wps_conn(sme)) {
		if (sme->privacy)
			err = wldev_iovar_setint_bsscfg(dev, "wsec", 4, bssidx);
		else
			
			err = wldev_iovar_setint_bsscfg(dev, "wsec", 0, bssidx);
	} else {
#ifdef BCMWAPI_WPI
		if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_SMS4) {
			WL_DBG((" NO, is_wps_conn, WAPI set to SMS4_ENABLED"));
			err = wldev_iovar_setint_bsscfg(dev, "wsec", val, bssidx);
		} else {
#endif
			WL_DBG((" NO, is_wps_conn, Set pval | gval to WSEC"));
			wsec_val = pval | gval;

			WL_DBG((" Set WSEC to fW 0x%x \n", wsec_val));
			err = wldev_iovar_setint_bsscfg(dev, "wsec",
				wsec_val, bssidx);
#ifdef BCMWAPI_WPI
		}
#endif
	}
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->cipher_pairwise = sme->crypto.ciphers_pairwise[0];
	sec->cipher_group = sme->crypto.cipher_group;

	return err;
}

static s32
wl_set_key_mgmt(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;
	s32 bssidx;
#ifdef MFP
	s32 mfp = WL_MFP_NONE;
	bcm_tlv_t *wpa2_ie;
	u8 rsn_cap[2];
#endif 

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (sme->crypto.n_akm_suites) {
		err = wldev_iovar_getint(dev, "wpa_auth", &val);
		if (unlikely(err)) {
			WL_ERR(("could not get wpa_auth (%d)\n", err));
			return err;
		}
		if (val & (WPA_AUTH_PSK |
#ifdef BCMCCX
			WPA_AUTH_CCKM |
#endif
			WPA_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA_AUTH_PSK;
				break;
#ifdef BCMCCX
			case WLAN_AKM_SUITE_CCKM:
				val = WPA_AUTH_CCKM;
				break;
#endif
			default:
				WL_ERR(("invalid cipher group (%d)\n",
					sme->crypto.cipher_group));
				return -EINVAL;
			}
		} else if (val & (WPA2_AUTH_PSK |
#ifdef BCMCCX
			WPA2_AUTH_CCKM |
#endif
			WPA2_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA2_AUTH_UNSPECIFIED;
				break;
#ifdef MFP
			case WL_AKM_SUITE_SHA256_1X:
				val = WPA2_AUTH_1X_SHA256;
				break;
			case WL_AKM_SUITE_SHA256_PSK:
				val = WPA2_AUTH_PSK_SHA256;
				break;
#endif 
			case WLAN_AKM_SUITE_PSK:
				val = WPA2_AUTH_PSK;
				break;
#if defined(WLFBT) && defined(WLAN_AKM_SUITE_FT_8021X)
			case WLAN_AKM_SUITE_FT_8021X:
				val = WPA2_AUTH_UNSPECIFIED | WPA2_AUTH_FT;
				break;
#endif
#if defined(WLFBT) && defined(WLAN_AKM_SUITE_FT_PSK)
			case WLAN_AKM_SUITE_FT_PSK:
				val = WPA2_AUTH_PSK | WPA2_AUTH_FT;
				break;
#endif
#ifdef BCMCCX
			case WLAN_AKM_SUITE_CCKM:
				val = WPA2_AUTH_CCKM;
				break;
#endif
			default:
				WL_ERR(("invalid cipher group (%d)\n",
					sme->crypto.cipher_group));
				return -EINVAL;
			}
		}
#ifdef BCMWAPI_WPI
		else if (val & (WAPI_AUTH_PSK | WAPI_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_WAPI_CERT:
				val = WAPI_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_WAPI_PSK:
				val = WAPI_AUTH_PSK;
				break;
			default:
				WL_ERR(("invalid cipher group (%d)\n",
					sme->crypto.cipher_group));
				return -EINVAL;
			}
		}
#endif

#ifdef MFP
		if (((wpa2_ie = bcm_parse_tlvs((u8 *)sme->ie, sme->ie_len,
			DOT11_MNG_RSN_ID)) != NULL) &&
			(wl_cfg80211_get_rsn_capa(wpa2_ie, rsn_cap) == 0)) {
				
				if (rsn_cap[0] & RSN_CAP_MFPR) {
					mfp = WL_MFP_REQUIRED;
				} else if (rsn_cap[0] & RSN_CAP_MFPC) {
					mfp = WL_MFP_CAPABLE;
				}
		}
		err = wldev_iovar_setint(dev, "mfp", mfp);
		if (unlikely(err)) {
			if (!mfp && (err == BCME_UNSUPPORTED)) {
				WL_DBG(("fw doesn't support mfp \n"));
				err = 0;
			} else {
				WL_ERR(("mfp set failed ret:%d \n", err));
				return err;
			}
		} else {
			WL_DBG(("mfp set to 0x%x \n", mfp));
		}
#endif 

		WL_DBG(("setting wpa_auth to 0x%x\n", val));

		err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", val, bssidx);
		if (unlikely(err)) {
			WL_ERR(("could not set wpa_auth (%d)\n", err));
			return err;
		}
	}
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->wpa_auth = sme->crypto.akm_suites[0];

	return err;
}

static s32
wl_set_set_sharedkey(struct net_device *dev,
	struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wl_security *sec;
	struct wl_wsec_key key;
	s32 val;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	WL_DBG(("key len (%d)\n", sme->key_len));
	if (sme->key_len) {
		sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
		WL_DBG(("wpa_versions 0x%x cipher_pairwise 0x%x\n",
			sec->wpa_versions, sec->cipher_pairwise));
		if (!(sec->wpa_versions & (NL80211_WPA_VERSION_1 |
#ifdef BCMWAPI_WPI
			NL80211_WPA_VERSION_2 | NL80211_WAPI_VERSION_1)) &&
#else
			NL80211_WPA_VERSION_2)) &&
#endif
			(sec->cipher_pairwise & (WLAN_CIPHER_SUITE_WEP40 |
#ifdef BCMWAPI_WPI
		WLAN_CIPHER_SUITE_WEP104 | WLAN_CIPHER_SUITE_SMS4)))
#else
		WLAN_CIPHER_SUITE_WEP104)))
#endif
		{
			memset(&key, 0, sizeof(key));
			key.len = (u32) sme->key_len;
			key.index = (u32) sme->key_idx;
			if (unlikely(key.len > sizeof(key.data))) {
				WL_ERR(("Too long key length (%u)\n", key.len));
				return -EINVAL;
			}
			memcpy(key.data, sme->key, key.len);
			key.flags = WL_PRIMARY_KEY;
			switch (sec->cipher_pairwise) {
			case WLAN_CIPHER_SUITE_WEP40:
				key.algo = CRYPTO_ALGO_WEP1;
				break;
			case WLAN_CIPHER_SUITE_WEP104:
				key.algo = CRYPTO_ALGO_WEP128;
				break;
#ifdef BCMWAPI_WPI
			case WLAN_CIPHER_SUITE_SMS4:
				key.algo = CRYPTO_ALGO_SMS4;
				break;
#endif
			default:
				WL_ERR(("Invalid algorithm (%d)\n",
					sme->crypto.ciphers_pairwise[0]));
				return -EINVAL;
			}
			
			WL_DBG(("key length (%d) key index (%d) algo (%d)\n",
				key.len, key.index, key.algo));
			WL_DBG(("key \"%s\"\n", key.data));
			swap_key_from_BE(&key);
			err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key),
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
			if (unlikely(err)) {
				WL_ERR(("WLC_SET_KEY error (%d)\n", err));
				return err;
			}
			if (sec->auth_type == NL80211_AUTHTYPE_SHARED_KEY) {
				WL_DBG(("set auth_type to shared key\n"));
				val = WL_AUTH_SHARED_KEY;	
				err = wldev_iovar_setint_bsscfg(dev, "auth", val, bssidx);
				if (unlikely(err)) {
					WL_ERR(("set auth failed (%d)\n", err));
					return err;
				}
			}
		}
	}
	return err;
}

#if defined(ESCAN_RESULT_PATCH)
static u8 connect_req_bssid[6];
static u8 broad_bssid[6];
#endif 



#if defined(CUSTOM_SET_CPUCORE) || defined(CONFIG_TCPACK_FASTTX)
static bool wl_get_chan_isvht80(struct net_device *net, dhd_pub_t *dhd)
{
	u32 chanspec = 0;
	bool isvht80 = 0;

	if (wldev_iovar_getint(net, "chanspec", (s32 *)&chanspec) == BCME_OK)
		chanspec = wl_chspec_driver_to_host(chanspec);

	isvht80 = chanspec & WL_CHANSPEC_BW_80;
	WL_INFORM(("%s: chanspec(%x:%d)\n", __FUNCTION__, chanspec, isvht80));

	return isvht80;
}
#endif 

static s32
wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct ieee80211_channel *chan = sme->channel;
	wl_extjoin_params_t *ext_join_params;
	struct wl_join_params join_params;
	size_t join_params_size;
	s32 err = 0;
	wpa_ie_fixed_t *wpa_ie;
	bcm_tlv_t *wpa2_ie;
	u8* wpaie  = 0;
	u32 wpaie_len = 0;
	u32 chan_cnt = 0;
	struct ether_addr bssid;
	s32 bssidx = -1;
	int ret;
	int wait_cnt;

	WL_DBG(("In\n"));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	if (sme->channel_hint) {
		chan = sme->channel_hint;
		WL_DBG(("channel_hint (%d), channel_hint center_freq (%d)\n",
			ieee80211_frequency_to_channel(sme->channel_hint->center_freq),
			sme->channel_hint->center_freq));
	}
	if (sme->bssid_hint) {
		sme->bssid = sme->bssid_hint;
		WL_DBG(("bssid_hint "MACDBG" \n", MAC2STRDBG(sme->bssid_hint)));
	}
#endif 

	if (unlikely(!sme->ssid)) {
		WL_ERR(("Invalid ssid\n"));
		return -EOPNOTSUPP;
	}

	if (unlikely(sme->ssid_len > DOT11_MAX_SSID_LEN)) {
		WL_ERR(("Invalid SSID info: SSID=%s, length=%zd\n",
			sme->ssid, sme->ssid_len));
		return -EINVAL;
	}

	RETURN_EIO_IF_NOT_UP(cfg);

#if !defined(ESCAN_RESULT_PATCH)
	if (cfg->scan_request) {
		wl_notify_escan_complete(cfg, dev, true, true);
	}
#endif
#ifdef WL_SCHED_SCAN
	if (cfg->sched_scan_req) {
		wl_cfg80211_sched_scan_stop(wiphy, bcmcfg_to_prmry_ndev(cfg));
	}
#endif
#if defined(ESCAN_RESULT_PATCH)
	if (sme->bssid)
		memcpy(connect_req_bssid, sme->bssid, ETHER_ADDR_LEN);
	else
		bzero(connect_req_bssid, ETHER_ADDR_LEN);
	bzero(broad_bssid, ETHER_ADDR_LEN);
#endif
#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
	maxrxpktglom = 0;
#endif
#ifdef WL_HOST_BAND_MGMT
	if (wl_check_channel_band_mismatch(cfg->curr_band,
		ieee80211_frequency_to_channel(chan->center_freq)))
		return BCME_ERROR;
#endif 
	bzero(&bssid, sizeof(bssid));
	if (!wl_get_drv_status(cfg, CONNECTED, dev)&&
		(ret = wldev_ioctl(dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN, false)) == 0) {
		if (!ETHER_ISNULLADDR(&bssid)) {
			scb_val_t scbval;
			wl_set_drv_status(cfg, DISCONNECTING, dev);
			scbval.val = DOT11_RC_DISASSOC_LEAVING;
			memcpy(&scbval.ea, &bssid, ETHER_ADDR_LEN);
			scbval.val = htod32(scbval.val);

			WL_DBG(("drv status CONNECTED is not set, but connected in FW!" MACDBG "/n",
				MAC2STRDBG(bssid.octet)));
			err = wldev_ioctl(dev, WLC_DISASSOC, &scbval,
				sizeof(scb_val_t), true);
			if (unlikely(err)) {
				wl_clr_drv_status(cfg, DISCONNECTING, dev);
				WL_ERR(("error (%d)\n", err));
				return err;
			}
			wait_cnt = 500/10;
			while (wl_get_drv_status(cfg, DISCONNECTING, dev) && wait_cnt) {
				WL_DBG(("Waiting for disconnection terminated, wait_cnt: %d\n",
					wait_cnt));
				wait_cnt--;
				OSL_SLEEP(10);
			}
		} else
			WL_DBG(("Currently not associated!\n"));
	} else {
		
		wait_cnt = 500/10;
		while (wl_get_drv_status(cfg, DISCONNECTING, dev) && wait_cnt) {
			WL_DBG(("Waiting for disconnection terminated, wait_cnt: %d\n", wait_cnt));
			wait_cnt--;
			OSL_SLEEP(10);
		}
	}

	
	bzero(&bssid, sizeof(bssid));
	if (!wl_get_drv_status(cfg, DISCONNECTING, dev))
		wl_update_prof(cfg, dev, NULL, (void *)&bssid, WL_PROF_BSSID);

	if (p2p_is_on(cfg) && (dev != bcmcfg_to_prmry_ndev(cfg))) {
		
			if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
				WL_ERR(("Find p2p index from wdev(%p) failed\n",
					dev->ieee80211_ptr));
				return BCME_ERROR;
			}
			wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
				VNDR_IE_ASSOCREQ_FLAG, sme->ie, sme->ie_len);
	} else if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		
		if ((wpa2_ie = bcm_parse_tlvs((u8 *)sme->ie, sme->ie_len,
			DOT11_MNG_RSN_ID)) != NULL) {
			WL_DBG((" WPA2 IE is found\n"));
		}
		
		if ((wpa_ie = wl_cfgp2p_find_wpaie((u8 *)sme->ie,
			sme->ie_len)) != NULL) {
			WL_DBG((" WPA IE is found\n"));
		}
		if (wpa_ie != NULL || wpa2_ie != NULL) {
			wpaie = (wpa_ie != NULL) ? (u8 *)wpa_ie : (u8 *)wpa2_ie;
			wpaie_len = (wpa_ie != NULL) ? wpa_ie->length : wpa2_ie->len;
			wpaie_len += WPA_RSN_IE_TAG_FIXED_LEN;
			err = wldev_iovar_setbuf(dev, "wpaie", wpaie, wpaie_len,
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
			if (unlikely(err)) {
				WL_ERR(("wpaie set error (%d)\n", err));
				return err;
			}
		} else {
			err = wldev_iovar_setbuf(dev, "wpaie", NULL, 0,
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
			if (unlikely(err)) {
				WL_ERR(("wpaie set error (%d)\n", err));
				return err;
			}
		}

		if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
			WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
			return BCME_ERROR;
		}
		err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
			VNDR_IE_ASSOCREQ_FLAG, (const u8 *)sme->ie, sme->ie_len);
		if (unlikely(err)) {
			return err;
		}
	}
	if (chan) {
		cfg->channel = ieee80211_frequency_to_channel(chan->center_freq);
		chan_cnt = 1;
		WL_DBG(("channel (%d), center_req (%d), %d channels\n", cfg->channel,
			chan->center_freq, chan_cnt));
	} else
		cfg->channel = 0;
#ifdef BCMWAPI_WPI
	WL_DBG(("1. enable wapi auth\n"));
	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1) {
		WL_DBG(("2. set wapi ie  \n"));
		err = wl_set_set_wapi_ie(dev, sme);
		if (unlikely(err))
			return err;
	} else
		WL_DBG(("2. Not wapi ie  \n"));
#endif
	WL_DBG(("ie (%p), ie_len (%zd)\n", sme->ie, sme->ie_len));
	WL_DBG(("3. set wapi version \n"));
	err = wl_set_wpa_version(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid wpa_version\n"));
		return err;
	}
#ifdef BCMWAPI_WPI
	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1)
		WL_DBG(("4. WAPI Dont Set wl_set_auth_type\n"));
	else {
		WL_DBG(("4. wl_set_auth_type\n"));
#endif
		err = wl_set_auth_type(dev, sme);
		if (unlikely(err)) {
			WL_ERR(("Invalid auth type\n"));
			return err;
		}
#ifdef BCMWAPI_WPI
	}
#endif

	err = wl_set_set_cipher(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid ciper\n"));
		return err;
	}

	err = wl_set_key_mgmt(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid key mgmt\n"));
		return err;
	}

	err = wl_set_set_sharedkey(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid shared key\n"));
		return err;
	}

	join_params_size = WL_EXTJOIN_PARAMS_FIXED_SIZE +
		chan_cnt * sizeof(chanspec_t);
	ext_join_params =  (wl_extjoin_params_t*)kzalloc(join_params_size, GFP_KERNEL);
	if (ext_join_params == NULL) {
		err = -ENOMEM;
		wl_clr_drv_status(cfg, CONNECTING, dev);
		goto exit;
	}
	ext_join_params->ssid.SSID_len = min(sizeof(ext_join_params->ssid.SSID), sme->ssid_len);
	memcpy(&ext_join_params->ssid.SSID, sme->ssid, ext_join_params->ssid.SSID_len);
	wl_update_prof(cfg, dev, NULL, &ext_join_params->ssid, WL_PROF_SSID);
	ext_join_params->ssid.SSID_len = htod32(ext_join_params->ssid.SSID_len);
	ext_join_params->scan.active_time = chan_cnt ? WL_SCAN_JOIN_ACTIVE_DWELL_TIME_MS : -1;
	ext_join_params->scan.passive_time = chan_cnt ? WL_SCAN_JOIN_PASSIVE_DWELL_TIME_MS : -1;
	
	ext_join_params->scan.scan_type = -1;
	ext_join_params->scan.nprobes = chan_cnt ?
		(ext_join_params->scan.active_time/WL_SCAN_JOIN_PROBE_INTERVAL_MS) : -1;
	ext_join_params->scan.home_time = -1;

	if (sme->bssid)
		memcpy(&ext_join_params->assoc.bssid, sme->bssid, ETH_ALEN);
	else
		memcpy(&ext_join_params->assoc.bssid, &ether_bcast, ETH_ALEN);
	ext_join_params->assoc.chanspec_num = chan_cnt;
	if (chan_cnt) {
		u16 channel, band, bw, ctl_sb;
		chanspec_t chspec;
		channel = cfg->channel;
		band = (channel <= CH_MAX_2G_CHANNEL) ? WL_CHANSPEC_BAND_2G
			: WL_CHANSPEC_BAND_5G;

		
		bw = wl_cfg80211_ulb_get_min_bw_chspec(dev->ieee80211_ptr, bssidx);
		if (bw == INVCHANSPEC) {
			WL_ERR(("Invalid chanspec \n"));
			kfree(ext_join_params);
			return BCME_ERROR;
		}

		ctl_sb = WL_CHANSPEC_CTL_SB_NONE;
		chspec = (channel | band | bw | ctl_sb);
		ext_join_params->assoc.chanspec_list[0]  &= WL_CHANSPEC_CHAN_MASK;
		ext_join_params->assoc.chanspec_list[0] |= chspec;
		ext_join_params->assoc.chanspec_list[0] =
			wl_chspec_host_to_driver(ext_join_params->assoc.chanspec_list[0]);
	}
	ext_join_params->assoc.chanspec_num = htod32(ext_join_params->assoc.chanspec_num);
	if (ext_join_params->ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		WL_INFORM(("ssid \"%s\", len (%d)\n", ext_join_params->ssid.SSID,
			ext_join_params->ssid.SSID_len));
	}
	wl_set_drv_status(cfg, CONNECTING, dev);

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		kfree(ext_join_params);
		return BCME_ERROR;
	}
	err = wldev_iovar_setbuf_bsscfg(dev, "join", ext_join_params, join_params_size,
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);

	WL_ERR(("Connecting with" MACDBG " channel (%d) ssid \"%s\", len (%d)\n\n",
		MAC2STRDBG((u8*)(&ext_join_params->assoc.bssid)), cfg->channel,
		ext_join_params->ssid.SSID, ext_join_params->ssid.SSID_len));

	kfree(ext_join_params);
	if (err) {
		wl_clr_drv_status(cfg, CONNECTING, dev);
		if (err == BCME_UNSUPPORTED) {
			WL_DBG(("join iovar is not supported\n"));
			goto set_ssid;
		} else {
			WL_ERR(("error (%d)\n", err));
			goto exit;
		}
	} else
		goto exit;

set_ssid:
	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	join_params.ssid.SSID_len = min(sizeof(join_params.ssid.SSID), sme->ssid_len);
	memcpy(&join_params.ssid.SSID, sme->ssid, join_params.ssid.SSID_len);
	join_params.ssid.SSID_len = htod32(join_params.ssid.SSID_len);
	wl_update_prof(cfg, dev, NULL, &join_params.ssid, WL_PROF_SSID);
	if (sme->bssid)
		memcpy(&join_params.params.bssid, sme->bssid, ETH_ALEN);
	else
		memcpy(&join_params.params.bssid, &ether_bcast, ETH_ALEN);

	if (wl_ch_to_chanspec(dev, cfg->channel, &join_params, &join_params_size) < 0) {
		WL_ERR(("Invalid chanspec\n"));
		return -EINVAL;
	}

	WL_DBG(("join_param_size %zu\n", join_params_size));

	if (join_params.ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		WL_INFORM(("ssid \"%s\", len (%d)\n", join_params.ssid.SSID,
			join_params.ssid.SSID_len));
	}
	wl_set_drv_status(cfg, CONNECTING, dev);
	err = wldev_ioctl(dev, WLC_SET_SSID, &join_params, join_params_size, true);
	if (err) {
		WL_ERR(("error (%d)\n", err));
		wl_clr_drv_status(cfg, CONNECTING, dev);
	}
exit:
	return err;
}

static s32
wl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
	u16 reason_code)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	scb_val_t scbval;
	bool act = false;
	s32 err = 0;
	u8 *curbssid;
#ifdef CUSTOM_SET_CPUCORE
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
#endif 
	WL_ERR(("Reason %d\n", reason_code));
	RETURN_EIO_IF_NOT_UP(cfg);
	act = *(bool *) wl_read_prof(cfg, dev, WL_PROF_ACT);
	curbssid = wl_read_prof(cfg, dev, WL_PROF_BSSID);
#ifdef ESCAN_RESULT_PATCH
	if (wl_get_drv_status(cfg, CONNECTING, dev) && curbssid &&
		(memcmp(curbssid, connect_req_bssid, ETHER_ADDR_LEN) == 0)) {
		WL_ERR(("Disconnecting from connecting device: " MACDBG "\n",
			MAC2STRDBG(curbssid)));
		act = true;
	}
#endif 
	if (act) {
#if !defined(ESCAN_RESULT_PATCH)
		
		if (cfg->scan_request) {
			wl_notify_escan_complete(cfg, dev, true, true);
		}
#endif 

#ifdef CUSTOMER_HW_ONE
		scan_one = false;
		disc_called = true;
#endif 

		wl_set_drv_status(cfg, DISCONNECTING, dev);
		scbval.val = reason_code;
		memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
		scbval.val = htod32(scbval.val);
		err = wldev_ioctl(dev, WLC_DISASSOC, &scbval,
			sizeof(scb_val_t), true);
		if (unlikely(err)) {
			wl_clr_drv_status(cfg, DISCONNECTING, dev);
			WL_ERR(("error (%d)\n", err));
			return err;
		}
	}
#ifdef CUSTOM_SET_CPUCORE
	
	if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		dhd->chan_isvht80 &= ~DHD_FLAG_STA_MODE;
		if (!(dhd->chan_isvht80))
			dhd_set_cpucore(dhd, FALSE);
	}
#endif 

	return err;
}

#ifdef CUSTOMER_HW_ONE
void
wl_cfg80211_extra_disc()
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	WL_ERR(("Force to do disconnection by wl_cfg80211_extra_disc\n"));

	if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
		wl_clr_drv_status(cfg, CONNECTED, ndev);
		wl_set_drv_status(cfg, DISCONNECTING, ndev);
		cfg80211_disconnected(ndev, WLAN_REASON_DEAUTH_LEAVING,
			NULL, 0, GFP_KERNEL);
		wl_link_down(cfg);
		wl_init_prof(cfg, ndev);
		wl_clr_drv_status(cfg, DISCONNECTING, ndev);
	}
}
#endif 

#if defined(WL_CFG80211_P2P_DEV_IF)
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
	enum nl80211_tx_power_setting type, s32 mbm)
#else
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy,
	enum nl80211_tx_power_setting type, s32 dbm)
#endif 
{

	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = 0;
#if defined(WL_CFG80211_P2P_DEV_IF)
	s32 dbm = MBM_TO_DBM(mbm);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)) || \
	defined(WL_COMPAT_WIRELESS) || defined(WL_SUPPORT_BACKPORTED_KPATCHES)
	dbm = MBM_TO_DBM(dbm);
#endif 

	RETURN_EIO_IF_NOT_UP(cfg);
	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		break;
	case NL80211_TX_POWER_LIMITED:
		if (dbm < 0) {
			WL_ERR(("TX_POWER_LIMITTED - dbm is negative\n"));
			return -EINVAL;
		}
		break;
	case NL80211_TX_POWER_FIXED:
		if (dbm < 0) {
			WL_ERR(("TX_POWER_FIXED - dbm is negative..\n"));
			return -EINVAL;
		}
		break;
	}

	err = wl_set_tx_power(ndev, type, dbm);
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	cfg->conf->tx_power = dbm;

	return err;
}

#if defined(WL_CFG80211_P2P_DEV_IF)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy,
	struct wireless_dev *wdev, s32 *dbm)
#else
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm)
#endif 
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = 0;

	RETURN_EIO_IF_NOT_UP(cfg);
	err = wl_get_tx_power(ndev, dbm);
	if (unlikely(err))
		WL_ERR(("error (%d)\n", err));

	return err;
}

static s32
wl_cfg80211_config_default_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool unicast, bool multicast)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	u32 index;
	s32 wsec;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from dev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	WL_DBG(("key index (%d)\n", key_idx));
	RETURN_EIO_IF_NOT_UP(cfg);
	err = wldev_iovar_getint_bsscfg(dev, "wsec", &wsec, bssidx);
	if (unlikely(err)) {
		WL_ERR(("WLC_GET_WSEC error (%d)\n", err));
		return err;
	}
	if (wsec == WEP_ENABLED) {
		
		index = (u32) key_idx;
		index = htod32(index);
		err = wldev_ioctl(dev, WLC_SET_KEY_PRIMARY, &index,
			sizeof(index), true);
		if (unlikely(err)) {
			WL_ERR(("error (%d)\n", err));
		}
	}
	return err;
}

static s32
wl_add_keyext(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, const u8 *mac_addr, struct key_params *params)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wl_wsec_key key;
	s32 err = 0;
	s32 bssidx;
	s32 mode = wl_get_mode_by_netdev(cfg, dev);

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}
	memset(&key, 0, sizeof(key));
	key.index = (u32) key_idx;

	if (!ETHER_ISMULTI(mac_addr))
		memcpy((char *)&key.ea, (const void *)mac_addr, ETHER_ADDR_LEN);
	key.len = (u32) params->key_len;

	
	if (key.len == 0) {
		
		swap_key_from_BE(&key);
		err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("key delete error (%d)\n", err));
			return err;
		}
	} else {
		if (key.len > sizeof(key.data)) {
			WL_ERR(("Invalid key length (%d)\n", key.len));
			return -EINVAL;
		}
		WL_DBG(("Setting the key index %d\n", key.index));
		memcpy(key.data, params->key, key.len);

		if ((mode == WL_MODE_BSS) &&
			(params->cipher == WLAN_CIPHER_SUITE_TKIP)) {
			u8 keybuf[8];
			memcpy(keybuf, &key.data[24], sizeof(keybuf));
			memcpy(&key.data[24], &key.data[16], sizeof(keybuf));
			memcpy(&key.data[16], keybuf, sizeof(keybuf));
		}

		
		if (params->seq && params->seq_len == 6) {
			
			u8 *ivptr;
			ivptr = (u8 *) params->seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
				(ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = true;
		}

		switch (params->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
			key.algo = CRYPTO_ALGO_WEP1;
			WL_DBG(("WLAN_CIPHER_SUITE_WEP40\n"));
			break;
		case WLAN_CIPHER_SUITE_WEP104:
			key.algo = CRYPTO_ALGO_WEP128;
			WL_DBG(("WLAN_CIPHER_SUITE_WEP104\n"));
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			key.algo = CRYPTO_ALGO_TKIP;
			WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_DBG(("WLAN_CIPHER_SUITE_AES_CMAC\n"));
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_DBG(("WLAN_CIPHER_SUITE_CCMP\n"));
			break;
#ifdef BCMWAPI_WPI
		case WLAN_CIPHER_SUITE_SMS4:
			key.algo = CRYPTO_ALGO_SMS4;
			WL_DBG(("WLAN_CIPHER_SUITE_SMS4\n"));
			break;
#endif
		default:
			WL_ERR(("Invalid cipher (0x%x)\n", params->cipher));
			return -EINVAL;
		}
		swap_key_from_BE(&key);
		
		dhd_wait_pend8021x(dev);
		err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("WLC_SET_KEY error (%d)\n", err));
			return err;
		}
	}
	return err;
}

int
wl_cfg80211_enable_roam_offload(struct net_device *dev, int enable)
{
	int err;
	wl_eventmsg_buf_t ev_buf;

	if (dev != bcmcfg_to_prmry_ndev(g_bcm_cfg)) {
		
		return -1;
	}
	err = wldev_iovar_setint(dev, "roam_offload", enable);
	if (err)
		return err;

	bzero(&ev_buf, sizeof(wl_eventmsg_buf_t));
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_PSK_SUP, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_ASSOC_REQ_IE, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_ASSOC_RESP_IE, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_REASSOC, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_JOIN, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_ROAM, !enable);
	err = wl_cfg80211_apply_eventbuffer(dev, g_bcm_cfg, &ev_buf);
	if (!err) {
		g_bcm_cfg->roam_offload = enable;
	}
	return err;
}

#if defined(WL_VIRTUAL_APSTA)
int
wl_cfg80211_interface_create(struct net_device *dev, char *name)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	bcm_struct_cfgdev *new_cfgdev;

	new_cfgdev = wl_cfg80211_create_iface(cfg->wdev->wiphy,
			NL80211_IFTYPE_STATION, NULL, name);
	if (!new_cfgdev) {
		return BCME_ERROR;
	}
	else {
		WL_DBG(("Iface %s created successfuly\n", name));
		return BCME_OK;
	}
}

int
wl_cfg80211_interface_delete(struct net_device *dev, char *name)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct net_info *iter, *next;
	int err = BCME_ERROR;

	if (name == NULL) {
		return BCME_ERROR;
	}

	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			if (strcmp(iter->ndev->name, name) == 0) {
				err =  wl_cfg80211_del_iface(cfg->wdev->wiphy, cfg->bss_cfgdev);
				break;
			}
		}
	}
	if (!err) {
		WL_DBG(("Iface %s deleted successfuly", name));
	}
	return err;
}
#endif 

static s32
wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr,
	struct key_params *params)
{
	struct wl_wsec_key key;
	s32 val = 0;
	s32 wsec = 0;
	s32 err = 0;
	u8 keybuf[8];
	s32 bssidx = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 mode = wl_get_mode_by_netdev(cfg, dev);
	WL_DBG(("key index (%d)\n", key_idx));
	RETURN_EIO_IF_NOT_UP(cfg);

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from dev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (mac_addr &&
		((params->cipher != WLAN_CIPHER_SUITE_WEP40) &&
		(params->cipher != WLAN_CIPHER_SUITE_WEP104))) {
			wl_add_keyext(wiphy, dev, key_idx, mac_addr, params);
			goto exit;
	}
	memset(&key, 0, sizeof(key));
	
	memset(&cfg->wep_key, 0, sizeof(struct wl_wsec_key));

	key.len = (u32) params->key_len;
	key.index = (u32) key_idx;

	if (unlikely(key.len > sizeof(key.data))) {
		WL_ERR(("Too long key length (%u)\n", key.len));
		return -EINVAL;
	}
	memcpy(key.data, params->key, key.len);

	key.flags = WL_PRIMARY_KEY;
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		key.algo = CRYPTO_ALGO_WEP1;
		val = WEP_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_WEP40\n"));
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key.algo = CRYPTO_ALGO_WEP128;
		val = WEP_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_WEP104\n"));
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key.algo = CRYPTO_ALGO_TKIP;
		val = TKIP_ENABLED;
		
		if (mode == WL_MODE_BSS) {
			bcopy(&key.data[24], keybuf, sizeof(keybuf));
			bcopy(&key.data[16], &key.data[24], sizeof(keybuf));
			bcopy(keybuf, &key.data[16], sizeof(keybuf));
		}
		WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key.algo = CRYPTO_ALGO_AES_CCM;
		val = AES_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_AES_CMAC\n"));
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key.algo = CRYPTO_ALGO_AES_CCM;
		val = AES_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_CCMP\n"));
		break;
#ifdef BCMWAPI_WPI
	case WLAN_CIPHER_SUITE_SMS4:
		key.algo = CRYPTO_ALGO_SMS4;
		WL_DBG(("WLAN_CIPHER_SUITE_SMS4\n"));
		val = SMS4_ENABLED;
		break;
#endif 
#if defined(WLFBT) && defined(WLAN_CIPHER_SUITE_PMK)
	case WLAN_CIPHER_SUITE_PMK: {
		int j;
		wsec_pmk_t pmk;
		char keystring[WSEC_MAX_PSK_LEN + 1];
		char* charptr = keystring;
		uint len;
		struct wl_security *sec;

		sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
		if (sec->wpa_auth == WLAN_AKM_SUITE_8021X) {
			err = wldev_iovar_setbuf(dev, "okc_info_pmk", (void *)params->key,
				WSEC_MAX_PSK_LEN / 2, keystring, sizeof(keystring), NULL);
			if (err) {
				
				WL_INFORM(("Setting 'okc_info_pmk' failed, err=%d\n", err));
			}
		}
		
		for (j = 0; j < (WSEC_MAX_PSK_LEN / 2); j++) {
			charptr += snprintf(charptr, sizeof(keystring), "%02x", params->key[j]);
		}
		len = strlen(keystring);
		pmk.key_len = htod16(len);
		bcopy(keystring, pmk.key, len);
		pmk.flags = htod16(WSEC_PASSPHRASE);

		err = wldev_ioctl(dev, WLC_SET_WSEC_PMK, &pmk, sizeof(pmk), true);
		if (err)
			return err;
	} break;
#endif 
	default:
		WL_ERR(("Invalid cipher (0x%x)\n", params->cipher));
		return -EINVAL;
	}

	
	if ((mode == WL_MODE_IBSS) && (val & (TKIP_ENABLED | AES_ENABLED))) {
		WL_ERR(("IBSS KEY setted\n"));
		wldev_iovar_setint(dev, "wpa_auth", WPA_AUTH_NONE);
	}
	swap_key_from_BE(&key);
	if ((params->cipher == WLAN_CIPHER_SUITE_WEP40) ||
		(params->cipher == WLAN_CIPHER_SUITE_WEP104)) {
		WL_DBG(("Buffering WEP Keys \n"));
		memcpy(&cfg->wep_key, &key, sizeof(struct wl_wsec_key));
	}
	err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key), cfg->ioctl_buf,
		WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("WLC_SET_KEY error (%d)\n", err));
		return err;
	}

exit:
	err = wldev_iovar_getint_bsscfg(dev, "wsec", &wsec, bssidx);
	if (unlikely(err)) {
		WL_ERR(("get wsec error (%d)\n", err));
		return err;
	}

	wsec |= val;
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (unlikely(err)) {
		WL_ERR(("set wsec error (%d)\n", err));
		return err;
	}

	return err;
}

static s32
wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr)
{
	struct wl_wsec_key key;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}
	WL_DBG(("Enter\n"));

#ifndef MFP
	if ((key_idx >= DOT11_MAX_DEFAULT_KEYS) && (key_idx < DOT11_MAX_DEFAULT_KEYS+2))
		return -EINVAL;
#endif

	RETURN_EIO_IF_NOT_UP(cfg);
	memset(&key, 0, sizeof(key));

	key.flags = WL_PRIMARY_KEY;
	key.algo = CRYPTO_ALGO_OFF;
	key.index = (u32) key_idx;

	WL_DBG(("key index (%d)\n", key_idx));
	
	swap_key_from_BE(&key);
	err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key), cfg->ioctl_buf,
		WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		if (err == -EINVAL) {
			if (key.index >= DOT11_MAX_DEFAULT_KEYS) {
				
				WL_DBG(("invalid key index (%d)\n", key_idx));
			}
		} else {
			WL_ERR(("WLC_SET_KEY error (%d)\n", err));
		}
		return err;
	}
	return err;
}

static s32
wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr, void *cookie,
	void (*callback) (void *cookie, struct key_params * params))
{
	struct key_params params;
	struct wl_wsec_key key;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wl_security *sec;
	s32 wsec;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}
	WL_DBG(("key index (%d)\n", key_idx));
	RETURN_EIO_IF_NOT_UP(cfg);
	memset(&key, 0, sizeof(key));
	key.index = key_idx;
	swap_key_to_BE(&key);
	memset(&params, 0, sizeof(params));
	params.key_len = (u8) min_t(u8, DOT11_MAX_KEY_SIZE, key.len);
	memcpy((void *)params.key, key.data, params.key_len);

	err = wldev_iovar_getint_bsscfg(dev, "wsec", &wsec, bssidx);
	if (unlikely(err)) {
		WL_ERR(("WLC_GET_WSEC error (%d)\n", err));
		return err;
	}
	switch (WSEC_ENABLED(wsec)) {
		case WEP_ENABLED:
			sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
			if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP40) {
				params.cipher = WLAN_CIPHER_SUITE_WEP40;
				WL_DBG(("WLAN_CIPHER_SUITE_WEP40\n"));
			} else if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP104) {
				params.cipher = WLAN_CIPHER_SUITE_WEP104;
				WL_DBG(("WLAN_CIPHER_SUITE_WEP104\n"));
			}
			break;
		case TKIP_ENABLED:
			params.cipher = WLAN_CIPHER_SUITE_TKIP;
			WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
			break;
		case AES_ENABLED:
			params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
			WL_DBG(("WLAN_CIPHER_SUITE_AES_CMAC\n"));
			break;
#ifdef BCMWAPI_WPI
		case SMS4_ENABLED:
			params.cipher = WLAN_CIPHER_SUITE_SMS4;
			WL_DBG(("WLAN_CIPHER_SUITE_SMS4\n"));
			break;
#endif
#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
		
		case (AES_ENABLED | TKIP_ENABLED): 
			params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
			WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
			break;
#endif
		default:
			WL_ERR(("Invalid algo (0x%x)\n", wsec));
			return -EINVAL;
	}

	callback(cookie, &params);
	return err;
}

static s32
wl_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
	struct net_device *dev, u8 key_idx)
{
#ifdef MFP
	return 0;
#else
	WL_INFORM(("Not supported\n"));
	return -EOPNOTSUPP;
#endif 
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32
wl_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
	const u8 *mac, struct station_info *sinfo)
#else
static s32
wl_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
	u8 *mac, struct station_info *sinfo)
#endif
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	scb_val_t scb_val;
	s32 rssi;
	s32 rate;
	s32 err = 0;
	sta_info_t *sta;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) || defined(WL_COMPAT_WIRELESS)
	s8 eabuf[ETHER_ADDR_STR_LEN];
#endif
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	RETURN_EIO_IF_NOT_UP(cfg);
	if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP) {
		err = wldev_iovar_getbuf(dev, "sta_info", (struct ether_addr *)mac,
			ETHER_ADDR_LEN, cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
		if (err < 0) {
			WL_ERR(("GET STA INFO failed, %d\n", err));
			return err;
		}
		sinfo->filled = STATION_INFO_INACTIVE_TIME;
		sta = (sta_info_t *)cfg->ioctl_buf;
		sta->len = dtoh16(sta->len);
		sta->cap = dtoh16(sta->cap);
		sta->flags = dtoh32(sta->flags);
		sta->idle = dtoh32(sta->idle);
		sta->in = dtoh32(sta->in);
		sinfo->inactive_time = sta->idle * 1000;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) || defined(WL_COMPAT_WIRELESS)
		if (sta->flags & WL_STA_ASSOC) {
			sinfo->filled |= STATION_INFO_CONNECTED_TIME;
			sinfo->connected_time = sta->in;
		}
		WL_INFORM(("STA %s : idle time : %d sec, connected time :%d ms\n",
			bcm_ether_ntoa((const struct ether_addr *)mac, eabuf), sinfo->inactive_time,
			sta->idle * 1000));
#endif
	} else if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_BSS ||
		wl_get_mode_by_netdev(cfg, dev) == WL_MODE_IBSS) {
		get_pktcnt_t pktcnt;
		u8 *curmacp;

		if (cfg->roam_offload) {
			struct ether_addr bssid;
			err = wldev_ioctl(dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN, false);
			if (err) {
				WL_ERR(("Failed to get current BSSID\n"));
			} else {
				if (!ETHER_ISNULLADDR(&bssid.octet) &&
						memcmp(mac, &bssid.octet, ETHER_ADDR_LEN) != 0) {
					
					err = wl_cfg80211_delayed_roam(cfg, dev, &bssid);
					if (err)
						WL_ERR(("Failed to handle the delayed roam, "
							"err=%d", err));
					mac = (u8 *)bssid.octet;
				}
			}
		}
		if (!wl_get_drv_status(cfg, CONNECTED, dev) ||
			(dhd_is_associated(dhd, NULL, &err) == FALSE)) {
			WL_ERR(("NOT assoc\n"));
			if (err == -ERESTARTSYS)
				return err;
			err = -ENODEV;
			return err;
		}
		curmacp = wl_read_prof(cfg, dev, WL_PROF_BSSID);
		if (memcmp(mac, curmacp, ETHER_ADDR_LEN)) {
			WL_ERR(("Wrong Mac address: "MACDBG" != "MACDBG"\n",
				MAC2STRDBG(mac), MAC2STRDBG(curmacp)));
		}

		
		err = wldev_ioctl(dev, WLC_GET_RATE, &rate, sizeof(rate), false);
		if (err) {
			WL_ERR(("Could not get rate (%d)\n", err));
		} else {
#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
			int rxpktglom;
#endif
			rate = dtoh32(rate);
			sinfo->filled |= STATION_INFO_TX_BITRATE;
			sinfo->txrate.legacy = rate * 5;
			WL_DBG(("Rate %d Mbps\n", (rate / 2)));
#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
			rxpktglom = ((rate/2) > 150) ? 20 : 10;

			if (maxrxpktglom != rxpktglom) {
				maxrxpktglom = rxpktglom;
				WL_DBG(("Rate %d Mbps, update bus:maxtxpktglom=%d\n", (rate/2),
					maxrxpktglom));
				err = wldev_iovar_setbuf(dev, "bus:maxtxpktglom",
					(char*)&maxrxpktglom, 4, cfg->ioctl_buf,
					WLC_IOCTL_MAXLEN, NULL);
				if (err < 0) {
					WL_ERR(("set bus:maxtxpktglom failed, %d\n", err));
				}
			}
#endif
		}

		memset(&scb_val, 0, sizeof(scb_val));
		scb_val.val = 0;
		err = wldev_ioctl(dev, WLC_GET_RSSI, &scb_val,
			sizeof(scb_val_t), false);
		if (err) {
			WL_ERR(("Could not get rssi (%d)\n", err));
			goto get_station_err;
		}
		rssi = wl_rssi_offset(dtoh32(scb_val.val));
		sinfo->filled |= STATION_INFO_SIGNAL;
		sinfo->signal = rssi;
		WL_DBG(("RSSI %d dBm\n", rssi));
		err = wldev_ioctl(dev, WLC_GET_PKTCNTS, &pktcnt,
			sizeof(pktcnt), false);
		if (!err) {
			sinfo->filled |= (STATION_INFO_RX_PACKETS |
				STATION_INFO_RX_DROP_MISC |
				STATION_INFO_TX_PACKETS |
				STATION_INFO_TX_FAILED);
			sinfo->rx_packets = pktcnt.rx_good_pkt;
			sinfo->rx_dropped_misc = pktcnt.rx_bad_pkt;
			sinfo->tx_packets = pktcnt.tx_good_pkt;
			sinfo->tx_failed  = pktcnt.tx_bad_pkt;
		}
get_station_err:
		if (err && (err != -ERESTARTSYS)) {
			
			WL_ERR(("force cfg80211_disconnected: %d\n", err));
			wl_clr_drv_status(cfg, CONNECTED, dev);
			cfg80211_disconnected(dev, 0, NULL, 0, GFP_KERNEL);
			wl_link_down(cfg);
		}
#ifdef CUSTOMER_HW_ONE
		wl_android_traffic_monitor(dev);
#endif
	}
	else {
		WL_ERR(("Invalid device mode %d\n", wl_get_mode_by_netdev(cfg, dev)));
	}

	return err;
}

static s32
wl_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
	bool enabled, s32 timeout)
{
	s32 pm;
	s32 err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_info *_net_info = wl_get_netinfo_by_netdev(cfg, dev);

	RETURN_EIO_IF_NOT_UP(cfg);
	WL_DBG(("Enter\n"));
	if (cfg->p2p_net == dev || _net_info == NULL ||
		!wl_get_drv_status(cfg, CONNECTED, dev)) {
		return err;
	}
	
	wl_add_remove_pm_enable_work(cfg, FALSE, WL_HANDLER_PEND);

	pm = enabled ? PM_FAST : PM_OFF;
	if (_net_info->pm_block) {
		WL_ERR(("%s:Do not enable the power save for pm_block %d\n",
			dev->name, _net_info->pm_block));
		pm = PM_OFF;
	}
	pm = htod32(pm);
	WL_DBG(("%s:power save %s\n", dev->name, (pm ? "enabled" : "disabled")));
	err = wldev_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), true);
	if (unlikely(err)) {
		if (err == -ENODEV)
			WL_DBG(("net_device is not ready yet\n"));
		else
			WL_ERR(("error (%d)\n", err));
		return err;
	}
	wl_cfg80211_update_power_mode(dev);
	return err;
}

void wl_cfg80211_update_power_mode(struct net_device *dev)
{
	int err, pm = -1;

	err = wldev_ioctl(dev, WLC_GET_PM, &pm, sizeof(pm), true);
	if (err)
		WL_ERR(("%s:error (%d)\n", __FUNCTION__, err));
	else if (pm != -1 && dev->ieee80211_ptr)
		dev->ieee80211_ptr->ps = (pm == PM_OFF) ? false : true;
}

void wl_cfg80211_set_passive_scan(struct net_device *dev, char *command)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	if (strcmp(command, "SCAN-ACTIVE") == 0) {
		cfg->active_scan = 1;
	} else if (strcmp(command, "SCAN-PASSIVE") == 0) {
		cfg->active_scan = 0;
	} else
		WL_ERR(("Unknown command \n"));
}

static __used u32 wl_find_msb(u16 bit16)
{
	u32 ret = 0;

	if (bit16 & 0xff00) {
		ret += 8;
		bit16 >>= 8;
	}

	if (bit16 & 0xf0) {
		ret += 4;
		bit16 >>= 4;
	}

	if (bit16 & 0xc) {
		ret += 2;
		bit16 >>= 2;
	}

	if (bit16 & 2)
		ret += bit16 & 2;
	else if (bit16)
		ret += bit16;

	return ret;
}

static s32 wl_cfg80211_resume(struct wiphy *wiphy)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = BCME_OK;
#if ((LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)) && \
	!1
	int pkt_filter_id = WL_WOWLAN_PKT_FILTER_ID_FIRST;
#endif 

	if (unlikely(!wl_get_drv_status(cfg, READY, ndev))) {
		WL_INFORM(("device is not ready\n"));
		return err;
	}

#if ((LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)) && \
	!1
	while (pkt_filter_id <= WL_WOWLAN_PKT_FILTER_ID_LAST) {
		
		err = wldev_iovar_setbuf(ndev, "pkt_filter_delete", &pkt_filter_id,
			sizeof(pkt_filter_id), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			&cfg->ioctl_buf_sync);
		if (BCME_BADARG == err)
			err = BCME_OK;

		if (BCME_OK != err) {
			WL_ERR(("pkt_filter_delete failed, id=%d, err=%d\n",
				pkt_filter_id, err));
		}
		pkt_filter_id++;
	}
#endif 

	return err;
}

#if ((LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)) && \
	!1
static s32 wl_wowlan_config(struct wiphy *wiphy, struct cfg80211_wowlan *wow)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);

	u32 i = 0, j = 0;
	u32 buf_len = 0, pattern_size = 0;
	wl_pkt_filter_t	*pkt_filterp = NULL;
	wl_pkt_filter_enable_t	pkt_filter_enable;
	u8 mask_bytes_len = 0, mask_byte_idx = 0, mask_bit_idx = 0;
	const u32 max_buf_size = WL_PKT_FILTER_FIXED_LEN +
		WL_PKT_FILTER_PATTERN_FIXED_LEN + (2 * WL_WOWLAN_MAX_PATTERN_LEN);

	WL_DBG(("Enter\n"));

	if (wow == NULL) {
		WL_DBG(("wow config is null\n"));
		return err;
	}

	
	if (0 < wow->n_patterns) {

		pkt_filterp = (wl_pkt_filter_t *) kzalloc(max_buf_size, GFP_KERNEL);
		if (pkt_filterp == NULL) {
			WL_ERR(("Error allocating buffer for pkt filters\n"));
			return -ENOMEM;
		}

		WL_DBG(("Pattern count=%d\n", wow->n_patterns));
		while (i < wow->n_patterns) {

			
			buf_len = 0;
			memset(pkt_filterp, 0, max_buf_size);

			
			store32_ua(&pkt_filterp->id, (WL_WOWLAN_PKT_FILTER_ID_FIRST + i));

			
			store32_ua(&pkt_filterp->type, WL_PKT_FILTER_TYPE_PATTERN_MATCH);

			
			pattern_size = htod32(wow->patterns[i].pattern_len);
			store32_ua(&pkt_filterp->u.pattern.size_bytes, pattern_size);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
			
			store32_ua(&pkt_filterp->u.pattern.offset, wow->patterns[i].pkt_offset);
#endif 

			
			j = 0;
			mask_bit_idx = 0;
			mask_byte_idx = 0;
			mask_bytes_len = DIV_ROUND_UP(pattern_size, 8);
			while ((mask_byte_idx < mask_bytes_len) &&
					(mask_bit_idx < pattern_size)) {

				if (isbitset(wow->patterns[i].mask[mask_byte_idx], mask_bit_idx++))
					pkt_filterp->u.pattern.mask_and_pattern[j] = 0xFF;
				j++;
				if (mask_bit_idx >= 8) {
					
					mask_bit_idx = 0;
					mask_byte_idx++;
				}
			}

			
			memcpy(&pkt_filterp->u.pattern.mask_and_pattern[pattern_size],
				wow->patterns[i].pattern, pattern_size);

			
			buf_len += WL_PKT_FILTER_FIXED_LEN;
			buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + (2 * pattern_size));

			
			err = wldev_iovar_setbuf(ndev, "pkt_filter_add", pkt_filterp, buf_len,
				cfg->ioctl_buf, WLC_IOCTL_MEDLEN, &cfg->ioctl_buf_sync);
			if (BCME_OK != err) {
				WL_ERR(("pkt_filter_add failed, id=%d, err=%d\n",
					pkt_filterp->id, err));
				goto exit;
			}

			
			pkt_filter_enable.id = pkt_filterp->id;
			pkt_filter_enable.enable = TRUE;
			err = wldev_iovar_setbuf(ndev, "pkt_filter_enable", &pkt_filter_enable,
				sizeof(pkt_filter_enable),
				cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
			if (BCME_OK != err) {
				WL_ERR(("pkt_filter_enable failed, id=%d, err=%d\n",
					pkt_filterp->id, err));
				goto exit;
			}
			i++; 
		}
	} else
		WL_DBG(("wowlan filters not found\n"));

exit:
	if (pkt_filterp)
		kfree(pkt_filterp);

	return err;
}
#endif 

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)
static s32 wl_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow)
#else
static s32 wl_cfg80211_suspend(struct wiphy *wiphy)
#endif
{
	s32 err = BCME_OK;
#ifdef DHD_CLEAR_ON_SUSPEND
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_info *iter, *next;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	unsigned long flags;
	if (unlikely(!wl_get_drv_status(cfg, READY, ndev))) {
		WL_INFORM(("device is not ready : status (%d)\n",
			(int)cfg->status));
		return err;
	}
	for_each_ndev(cfg, iter, next) {
		
		if (iter->ndev)
			wl_set_drv_status(cfg, SCAN_ABORTING, iter->ndev);
		}
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	if (cfg->scan_request) {
		cfg80211_scan_done(cfg->scan_request, true);
		cfg->scan_request = NULL;
	}
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			wl_clr_drv_status(cfg, SCANNING, iter->ndev);
			wl_clr_drv_status(cfg, SCAN_ABORTING, iter->ndev);
		}
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			if (wl_get_drv_status(cfg, CONNECTING, iter->ndev)) {
				wl_bss_connect_done(cfg, iter->ndev, NULL, NULL, false);
			}
		}
	}
#endif 

#if ((LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)) && \
	!1
	err = wl_wowlan_config(wiphy, wow);
#endif 

	return err;
}

static s32
wl_update_pmklist(struct net_device *dev, struct wl_pmk_list *pmk_list,
	s32 err)
{
	int i, j;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct net_device *primary_dev = bcmcfg_to_prmry_ndev(cfg);

	if (!pmk_list) {
		WL_ERR(("pmk_list is NULL\n"));
		return -EINVAL;
	}
	if (primary_dev != dev) {
		WL_INFORM(("Not supporting Flushing pmklist on virtual"
			" interfaces than primary interface\n"));
		return err;
	}

	WL_DBG(("No of elements %d\n", pmk_list->pmkids.npmkid));
	for (i = 0; i < pmk_list->pmkids.npmkid; i++) {
		WL_DBG(("PMKID[%d]: %pM =\n", i,
			&pmk_list->pmkids.pmkid[i].BSSID));
		for (j = 0; j < WPA2_PMKID_LEN; j++) {
			WL_DBG(("%02x\n", pmk_list->pmkids.pmkid[i].PMKID[j]));
		}
	}
	if (likely(!err)) {
		err = wldev_iovar_setbuf(dev, "pmkid_info", (char *)pmk_list,
			sizeof(*pmk_list), cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	}

	return err;
}

static s32
wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	int i;

	RETURN_EIO_IF_NOT_UP(cfg);
	for (i = 0; i < cfg->pmk_list->pmkids.npmkid; i++)
		if (!memcmp(pmksa->bssid, &cfg->pmk_list->pmkids.pmkid[i].BSSID,
			ETHER_ADDR_LEN))
			break;
	if (i < WL_NUM_PMKIDS_MAX) {
		memcpy(&cfg->pmk_list->pmkids.pmkid[i].BSSID, pmksa->bssid,
			ETHER_ADDR_LEN);
		memcpy(&cfg->pmk_list->pmkids.pmkid[i].PMKID, pmksa->pmkid,
			WPA2_PMKID_LEN);
		if (i == cfg->pmk_list->pmkids.npmkid)
			cfg->pmk_list->pmkids.npmkid++;
	} else {
		err = -EINVAL;
	}
	WL_DBG(("set_pmksa,IW_PMKSA_ADD - PMKID: %pM =\n",
		&cfg->pmk_list->pmkids.pmkid[cfg->pmk_list->pmkids.npmkid - 1].BSSID));
	for (i = 0; i < WPA2_PMKID_LEN; i++) {
		WL_DBG(("%02x\n",
			cfg->pmk_list->pmkids.pmkid[cfg->pmk_list->pmkids.npmkid - 1].
			PMKID[i]));
	}

	err = wl_update_pmklist(dev, cfg->pmk_list, err);

	return err;
}

static s32
wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

	struct _pmkid_list pmkid = {.npmkid = 0};
	s32 err = 0;
	int i;

	RETURN_EIO_IF_NOT_UP(cfg);
	memcpy(&pmkid.pmkid[0].BSSID, pmksa->bssid, ETHER_ADDR_LEN);
	memcpy(pmkid.pmkid[0].PMKID, pmksa->pmkid, WPA2_PMKID_LEN);

	WL_DBG(("del_pmksa,IW_PMKSA_REMOVE - PMKID: %pM =\n",
		&pmkid.pmkid[0].BSSID));
	for (i = 0; i < WPA2_PMKID_LEN; i++) {
		WL_DBG(("%02x\n", pmkid.pmkid[0].PMKID[i]));
	}

	for (i = 0; i < cfg->pmk_list->pmkids.npmkid; i++)
		if (!memcmp
		    (pmksa->bssid, &cfg->pmk_list->pmkids.pmkid[i].BSSID,
		     ETHER_ADDR_LEN))
			break;

	if ((cfg->pmk_list->pmkids.npmkid > 0) &&
		(i < cfg->pmk_list->pmkids.npmkid)) {
		memset(&cfg->pmk_list->pmkids.pmkid[i], 0, sizeof(pmkid_t));
		for (; i < (cfg->pmk_list->pmkids.npmkid - 1); i++) {
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].BSSID,
				&cfg->pmk_list->pmkids.pmkid[i + 1].BSSID,
				ETHER_ADDR_LEN);
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].PMKID,
				&cfg->pmk_list->pmkids.pmkid[i + 1].PMKID,
				WPA2_PMKID_LEN);
		}
		cfg->pmk_list->pmkids.npmkid--;
	} else {
		err = -EINVAL;
	}

	err = wl_update_pmklist(dev, cfg->pmk_list, err);

	return err;

}

static s32
wl_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	RETURN_EIO_IF_NOT_UP(cfg);
	memset(cfg->pmk_list, 0, sizeof(*cfg->pmk_list));
	err = wl_update_pmklist(dev, cfg->pmk_list, err);
	return err;

}

static wl_scan_params_t *
wl_cfg80211_scan_alloc_params(int channel, int nprobes, int *out_params_size)
{
	wl_scan_params_t *params;
	int params_size;
	int num_chans;
	int bssidx = 0;

	*out_params_size = 0;

	
	params_size = WL_SCAN_PARAMS_FIXED_SIZE + 1 * sizeof(uint16);
	params = (wl_scan_params_t*) kzalloc(params_size, GFP_KERNEL);
	if (params == NULL) {
		WL_ERR(("mem alloc failed (%d bytes)\n", params_size));
		return params;
	}
	memset(params, 0, params_size);
	params->nprobes = nprobes;

	num_chans = (channel == 0) ? 0 : 1;

	memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
	params->bss_type = DOT11_BSSTYPE_ANY;
	params->scan_type = DOT11_SCANTYPE_ACTIVE;
	params->nprobes = htod32(1);
	params->active_time = htod32(-1);
	params->passive_time = htod32(-1);
	params->home_time = htod32(10);
	if (channel == -1)
		params->channel_list[0] = htodchanspec(channel);
	else
		params->channel_list[0] = wl_ch_host_to_driver(bssidx, channel);

	
	params->channel_num = htod32((0 << WL_SCAN_PARAMS_NSSID_SHIFT) |
		(num_chans & WL_SCAN_PARAMS_COUNT_MASK));

	*out_params_size = params_size;	
	return params;
}

#if defined(WL_CFG80211_P2P_DEV_IF)
static s32
wl_cfg80211_remain_on_channel(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct ieee80211_channel *channel, unsigned int duration, u64 *cookie)
#else
static s32
wl_cfg80211_remain_on_channel(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct ieee80211_channel * channel,
	enum nl80211_channel_type channel_type,
	unsigned int duration, u64 *cookie)
#endif 
{
	s32 target_channel;
	u32 id;
	s32 err = BCME_OK;
	struct ether_addr primary_mac;
	struct net_device *ndev = NULL;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	WL_DBG(("Enter, channel: %d, duration ms (%d) SCANNING ?? %s \n",
		ieee80211_frequency_to_channel(channel->center_freq),
		duration, (wl_get_drv_status(cfg, SCANNING, ndev)) ? "YES":"NO"));

	if (!cfg->p2p) {
		WL_ERR(("cfg->p2p is not initialized\n"));
		err = BCME_ERROR;
		goto exit;
	}

#ifndef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
	}
#endif 

	target_channel = ieee80211_frequency_to_channel(channel->center_freq);
	memcpy(&cfg->remain_on_chan, channel, sizeof(struct ieee80211_channel));
#if defined(WL_ENABLE_P2P_IF)
	cfg->remain_on_chan_type = channel_type;
#endif 
	id = ++cfg->last_roc_id;
	if (id == 0)
		id = ++cfg->last_roc_id;
	*cookie = id;

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	if (wl_get_drv_status(cfg, SCANNING, ndev)) {
		struct timer_list *_timer;
		WL_DBG(("scan is running. go to fake listen state\n"));

		if (duration > LONG_LISTEN_TIME) {
			wl_cfg80211_scan_abort(cfg);
		} else {
			wl_set_drv_status(cfg, FAKE_REMAINING_ON_CHANNEL, ndev);

			if (timer_pending(&cfg->p2p->listen_timer)) {
				WL_DBG(("cancel current listen timer \n"));
				del_timer_sync(&cfg->p2p->listen_timer);
			}

			_timer = &cfg->p2p->listen_timer;
			wl_clr_p2p_status(cfg, LISTEN_EXPIRED);

			INIT_TIMER(_timer, wl_cfgp2p_listen_expired, duration, 0);

			err = BCME_OK;
			goto exit;
		}
	}
#endif 

#ifdef WL_CFG80211_SYNC_GON
	if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM_LISTEN)) {
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
		wl_set_drv_status(cfg, FAKE_REMAINING_ON_CHANNEL, ndev);
#else
		wl_set_drv_status(cfg, REMAINING_ON_CHANNEL, ndev);
#endif 
		goto exit;
	}
#endif 
	if (cfg->p2p && !cfg->p2p->on) {
		get_primary_mac(cfg, &primary_mac);
		wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
		p2p_on(cfg) = true;
	}

	if (p2p_is_on(cfg)) {
		err = wl_cfgp2p_enable_discovery(cfg, ndev, NULL, 0);
		if (unlikely(err)) {
			goto exit;
		}
#ifndef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
		wl_set_drv_status(cfg, REMAINING_ON_CHANNEL, ndev);
#endif 
		err = wl_cfgp2p_discover_listen(cfg, target_channel, duration);

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
		if (err == BCME_OK) {
			wl_set_drv_status(cfg, REMAINING_ON_CHANNEL, ndev);
		} else {
			wl_set_drv_status(cfg, FAKE_REMAINING_ON_CHANNEL, ndev);
		}
#endif 
		err = BCME_OK;
	}

exit:
	if (err == BCME_OK) {
		WL_INFORM(("Success\n"));
#if defined(WL_CFG80211_P2P_DEV_IF)
		cfg80211_ready_on_channel(cfgdev, *cookie, channel,
			duration, GFP_KERNEL);
#else
		cfg80211_ready_on_channel(cfgdev, *cookie, channel,
			channel_type, duration, GFP_KERNEL);
#endif 
	} else {
		WL_ERR(("Fail to Set (err=%d cookie:%llu)\n", err, *cookie));
	}
	return err;
}

static s32
wl_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
	bcm_struct_cfgdev *cfgdev, u64 cookie)
{
	s32 err = 0;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

#ifdef P2PLISTEN_AP_SAMECHN
	struct net_device *dev;
#endif 

	RETURN_EIO_IF_NOT_UP(cfg);
#if defined(WL_CFG80211_P2P_DEV_IF)
	if (cfgdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		WL_DBG((" enter ) on P2P dedicated discover interface\n"));
	}
#else
	WL_DBG((" enter ) netdev_ifidx: %d \n", cfgdev->ifindex));
#endif 

#ifdef P2PLISTEN_AP_SAMECHN
	if (cfg && cfg->p2p_resp_apchn_status) {
		dev = bcmcfg_to_prmry_ndev(cfg);
		wl_cfg80211_set_p2p_resp_ap_chn(dev, 0);
		cfg->p2p_resp_apchn_status = false;
		WL_DBG(("p2p_resp_apchn_status Turn OFF \n"));
	}
#endif 

	if (cfg->last_roc_id == cookie) {
		wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
			wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
	} else {
		WL_ERR(("%s : ignore, request cookie(%llu) is not matched. (cur : %llu)\n",
			__FUNCTION__, cookie, cfg->last_roc_id));
	}

	return err;
}

static void
wl_cfg80211_afx_handler(struct work_struct *work)
{
	struct afx_hdl *afx_instance;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 ret = BCME_OK;

	BCM_SET_CONTAINER_OF(afx_instance, work, struct afx_hdl, work);
	if (afx_instance != NULL && cfg->afx_hdl->is_active) {
		if (cfg->afx_hdl->is_listen && cfg->afx_hdl->my_listen_chan) {
			ret = wl_cfgp2p_discover_listen(cfg, cfg->afx_hdl->my_listen_chan,
				(100 * (1 + (RANDOM32() % 3)))); 
		} else {
			ret = wl_cfgp2p_act_frm_search(cfg, cfg->afx_hdl->dev,
				cfg->afx_hdl->bssidx, cfg->afx_hdl->peer_listen_chan,
				NULL);
		}
		if (unlikely(ret != BCME_OK)) {
			WL_ERR(("ERROR occurred! returned value is (%d)\n", ret));
			if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL))
				complete(&cfg->act_frm_scan);
		}
	}
}

static s32
wl_cfg80211_af_searching_channel(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
	u32 max_retry = WL_CHANNEL_SYNC_RETRY;

	if (dev == NULL)
		return -1;

	WL_DBG((" enter ) \n"));

	wl_set_drv_status(cfg, FINDING_COMMON_CHANNEL, dev);
	cfg->afx_hdl->is_active = TRUE;

	while ((cfg->afx_hdl->retry < max_retry) &&
		(cfg->afx_hdl->peer_chan == WL_INVALID)) {
		cfg->afx_hdl->is_listen = FALSE;
		wl_set_drv_status(cfg, SCANNING, dev);
		WL_DBG(("Scheduling the action frame for sending.. retry %d\n",
			cfg->afx_hdl->retry));
		
		schedule_work(&cfg->afx_hdl->work);
		wait_for_completion_timeout(&cfg->act_frm_scan,
			msecs_to_jiffies(WL_AF_SEARCH_TIME_MAX));

		if ((cfg->afx_hdl->peer_chan != WL_INVALID) ||
			!(wl_get_drv_status(cfg, FINDING_COMMON_CHANNEL, dev)))
			break;

		if (cfg->afx_hdl->my_listen_chan) {
			WL_DBG(("Scheduling Listen peer in my listen channel = %d\n",
				cfg->afx_hdl->my_listen_chan));
			
			cfg->afx_hdl->is_listen = TRUE;
			schedule_work(&cfg->afx_hdl->work);
			wait_for_completion_timeout(&cfg->act_frm_scan,
				msecs_to_jiffies(WL_AF_SEARCH_TIME_MAX));
		}
		if ((cfg->afx_hdl->peer_chan != WL_INVALID) ||
			!(wl_get_drv_status(cfg, FINDING_COMMON_CHANNEL, dev)))
			break;

		cfg->afx_hdl->retry++;

		WL_AF_TX_KEEP_PRI_CONNECTION_VSDB(cfg);
	}

	cfg->afx_hdl->is_active = FALSE;

	wl_clr_drv_status(cfg, SCANNING, dev);
	wl_clr_drv_status(cfg, FINDING_COMMON_CHANNEL, dev);

	return (cfg->afx_hdl->peer_chan);
}

struct p2p_config_af_params {
	s32 max_tx_retry;	
	s32 mpc_onoff;
#ifdef WL_CFG80211_SYNC_GON
	bool extra_listen;
#endif
	bool search_channel;	
};

static s32
wl_cfg80211_config_p2p_pub_af_tx(struct wiphy *wiphy,
	wl_action_frame_t *action_frame, wl_af_params_t *af_params,
	struct p2p_config_af_params *config_af_params)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	wifi_p2p_pub_act_frame_t *act_frm =
		(wifi_p2p_pub_act_frame_t *) (action_frame->data);

	
#ifdef WL_CFG80211_SYNC_GON
	config_af_params->extra_listen = true;
#endif
	config_af_params->search_channel = false;
	config_af_params->max_tx_retry = WL_AF_TX_MAX_RETRY;
	config_af_params->mpc_onoff = -1;
	cfg->next_af_subtype = P2P_PAF_SUBTYPE_INVALID;

	switch (act_frm->subtype) {
	case P2P_PAF_GON_REQ: {
		WL_DBG(("P2P: GO_NEG_PHASE status set \n"));
		wl_set_p2p_status(cfg, GO_NEG_PHASE);

		config_af_params->mpc_onoff = 0;
		config_af_params->search_channel = true;
		cfg->next_af_subtype = act_frm->subtype + 1;

		
		af_params->dwell_time = WL_MED_DWELL_TIME;

		break;
	}
	case P2P_PAF_GON_RSP: {
		cfg->next_af_subtype = act_frm->subtype + 1;
		
		af_params->dwell_time = WL_MED_DWELL_TIME + 100;
		break;
	}
	case P2P_PAF_GON_CONF: {
		
		WL_DBG(("P2P: GO_NEG_PHASE status cleared \n"));
		wl_clr_p2p_status(cfg, GO_NEG_PHASE);

		
		config_af_params->mpc_onoff = 1;

		
		af_params->dwell_time = WL_MIN_DWELL_TIME;

#ifdef WL_CFG80211_SYNC_GON
		config_af_params->extra_listen = false;
#endif 
		break;
	}
	case P2P_PAF_INVITE_REQ: {
		config_af_params->search_channel = true;
		cfg->next_af_subtype = act_frm->subtype + 1;

		
		af_params->dwell_time = WL_MED_DWELL_TIME;
		break;
	}
	case P2P_PAF_INVITE_RSP:
		
		af_params->dwell_time = WL_MIN_DWELL_TIME;
#ifdef WL_CFG80211_SYNC_GON
		config_af_params->extra_listen = false;
#endif 
		break;
	case P2P_PAF_DEVDIS_REQ: {
		if (IS_ACTPUB_WITHOUT_GROUP_ID(&act_frm->elts[0],
			action_frame->len)) {
			config_af_params->search_channel = true;
		}

		cfg->next_af_subtype = act_frm->subtype + 1;
		
		af_params->dwell_time = WL_LONG_DWELL_TIME;
		break;
	}
	case P2P_PAF_DEVDIS_RSP:
		
		af_params->dwell_time = WL_MIN_DWELL_TIME;
#ifdef WL_CFG80211_SYNC_GON
		config_af_params->extra_listen = false;
#endif 
		break;
	case P2P_PAF_PROVDIS_REQ: {
		if (IS_ACTPUB_WITHOUT_GROUP_ID(&act_frm->elts[0],
			action_frame->len)) {
			config_af_params->search_channel = true;
		}

		config_af_params->mpc_onoff = 0;
		cfg->next_af_subtype = act_frm->subtype + 1;
		
		af_params->dwell_time = WL_MED_DWELL_TIME;
		break;
	}
	case P2P_PAF_PROVDIS_RSP: {
		cfg->next_af_subtype = P2P_PAF_GON_REQ;
		af_params->dwell_time = WL_MIN_DWELL_TIME;
#ifdef WL_CFG80211_SYNC_GON
		config_af_params->extra_listen = false;
#endif 
		break;
	}
	default:
		WL_DBG(("Unknown p2p pub act frame subtype: %d\n",
			act_frm->subtype));
		err = BCME_BADARG;
	}
	return err;
}

#ifdef WL11U
static bool
wl_cfg80211_check_DFS_channel(struct bcm_cfg80211 *cfg, wl_af_params_t *af_params,
	void *frame, u16 frame_len)
{
	struct wl_scan_results *bss_list;
	struct wl_bss_info *bi = NULL;
	bool result = false;
	s32 i;
	chanspec_t chanspec;

	
	if (af_params &&
		(af_params->channel >= 52 && af_params->channel <= 148)) {
		if (!wl_cfgp2p_is_p2p_action(frame, frame_len)) {
			bss_list = cfg->bss_list;
			bi = next_bss(bss_list, bi);
			for_each_bss(bss_list, bi, i) {
				chanspec = wl_chspec_driver_to_host(bi->chanspec);
				if (CHSPEC_IS5G(chanspec) &&
					((bi->ctl_ch ? bi->ctl_ch : CHSPEC_CHANNEL(chanspec))
					== af_params->channel)) {
					result = true;	
					break;
				}
			}
		}
	}
	else {
		result = true;
	}

	WL_DBG(("result=%s", result?"true":"false"));
	return result;
}
#endif 


static bool
wl_cfg80211_send_action_frame(struct wiphy *wiphy, struct net_device *dev,
	bcm_struct_cfgdev *cfgdev, wl_af_params_t *af_params,
	wl_action_frame_t *action_frame, u16 action_frame_len, s32 bssidx)
{
#ifdef WL11U
	struct net_device *ndev = NULL;
#endif 
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	bool ack = false;
	u8 category, action;
	s32 tx_retry;
	struct p2p_config_af_params config_af_params;
	struct net_info *netinfo;
#ifdef VSDB
	ulong off_chan_started_jiffies = 0;
#endif
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);


	af_params->dwell_time = WL_DWELL_TIME;

#ifdef WL11U
#if defined(WL_CFG80211_P2P_DEV_IF)
	ndev = dev;
#else
	ndev = ndev_to_cfgdev(cfgdev);
#endif 
#endif 

	category = action_frame->data[DOT11_ACTION_CAT_OFF];
	action = action_frame->data[DOT11_ACTION_ACT_OFF];

	
	tx_retry = 0;
	cfg->next_af_subtype = P2P_PAF_SUBTYPE_INVALID;
	config_af_params.max_tx_retry = WL_AF_TX_MAX_RETRY;
	config_af_params.mpc_onoff = -1;
	config_af_params.search_channel = false;
#ifdef WL_CFG80211_SYNC_GON
	config_af_params.extra_listen = false;
#endif

	
	
	if (category == DOT11_ACTION_CAT_PUBLIC) {
		if ((action == P2P_PUB_AF_ACTION) &&
			(action_frame_len >= sizeof(wifi_p2p_pub_act_frame_t))) {
			
			if (BCME_OK != wl_cfg80211_config_p2p_pub_af_tx(wiphy,
				action_frame, af_params, &config_af_params)) {
				WL_DBG(("Unknown subtype.\n"));
			}

		} else if (action_frame_len >= sizeof(wifi_p2psd_gas_pub_act_frame_t)) {
			
			if (action == P2PSD_ACTION_ID_GAS_IREQ ||
				action == P2PSD_ACTION_ID_GAS_CREQ) {
				

				config_af_params.search_channel = true;

				
				cfg->next_af_subtype = action + 1;

				af_params->dwell_time = WL_MED_DWELL_TIME;
			} else if (action == P2PSD_ACTION_ID_GAS_IRESP ||
				action == P2PSD_ACTION_ID_GAS_CRESP) {
				
				af_params->dwell_time = WL_MIN_DWELL_TIME;
			} else {
				WL_DBG(("Unknown action type: %d\n", action));
			}
		} else {
			WL_DBG(("Unknown Frame: category 0x%x, action 0x%x, length %d\n",
				category, action, action_frame_len));
	}
	} else if (category == P2P_AF_CATEGORY) {
		
	} else {
		WL_DBG(("Unknown Frame: category 0x%x, action 0x%x\n",
			category, action));
		if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
			wl_clr_drv_status(cfg, SENDING_ACT_FRM, dev);
			return false;
		}
	}

	
	if (config_af_params.mpc_onoff == 0) {
		wldev_iovar_setint(dev, "mpc", 0);
	}

	netinfo = wl_get_netinfo_by_bssidx(cfg, bssidx);
	
	if (config_af_params.search_channel && IS_P2P_SOCIAL(af_params->channel) &&
		netinfo && netinfo->bss.ies.probe_req_ie_len) {
		config_af_params.search_channel = true;
	} else {
		config_af_params.search_channel = false;
	}
#ifdef WL11U
	if (ndev == bcmcfg_to_prmry_ndev(cfg))
		config_af_params.search_channel = false;
#endif 

#ifdef VSDB
	
	if (wl_get_drv_status(cfg, CONNECTING, bcmcfg_to_prmry_ndev(cfg))) {
		OSL_SLEEP(50);
	}
#endif

	
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
	}

	
	if (discover_cfgdev(cfgdev, cfg)) {
		if (cfg->p2p_supported && cfg->p2p) {
			wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
				wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
		}
	}

#ifdef WL11U
	
	if (!wl_cfg80211_check_DFS_channel(cfg, af_params, action_frame->data, action_frame->len)) {
		return false;	
	}
#endif 

	
	if (cfg->next_af_subtype != P2P_PAF_SUBTYPE_INVALID) {
		
		wl_set_drv_status(cfg, WAITING_NEXT_ACT_FRM, dev);
	}
	wl_set_drv_status(cfg, SENDING_ACT_FRM, dev);
	memcpy(cfg->afx_hdl->tx_dst_addr.octet,
		af_params->action_frame.da.octet,
		sizeof(cfg->afx_hdl->tx_dst_addr.octet));

	
	cfg->afx_hdl->pending_tx_act_frm = af_params;

	if (wl_cfgp2p_is_p2p_gas_action(action_frame->data, action_frame->len)) {
		WL_DBG(("Set GAS action frame config.\n"));
		config_af_params.search_channel = false;
		config_af_params.max_tx_retry = 1;
	}

	
	if (config_af_params.search_channel) {
		
		if ((cfg->afx_hdl->bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
			WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
			goto exit;
		}
		cfg->afx_hdl->dev = dev;
		cfg->afx_hdl->retry = 0;
		cfg->afx_hdl->peer_chan = WL_INVALID;

		if (wl_cfg80211_af_searching_channel(cfg, dev) == WL_INVALID) {
			WL_ERR(("couldn't find peer's channel.\n"));
			wl_cfgp2p_print_actframe(true, action_frame->data, action_frame->len,
				af_params->channel);
			goto exit;
		}

		wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
		wl_notify_escan_complete(cfg, dev, true, true);
		if ((wl_cfgp2p_discover_enable_search(cfg, false)) < 0) {
			WL_ERR(("Can not disable discovery mode\n"));
			goto exit;
		}

		
		af_params->channel = cfg->afx_hdl->peer_chan;
	}

#ifdef VSDB
	off_chan_started_jiffies = jiffies;
#endif 

	wl_cfgp2p_print_actframe(true, action_frame->data, action_frame->len, af_params->channel);

	wl_cfgp2p_need_wait_actfrmae(cfg, action_frame->data, action_frame->len, true);

	
	ack = wl_cfgp2p_tx_action_frame(cfg, dev, af_params, bssidx) ? false : true;

	
	while ((ack == false) && (tx_retry++ < config_af_params.max_tx_retry)) {
#ifdef VSDB
		if (af_params->channel) {
			if (jiffies_to_msecs(jiffies - off_chan_started_jiffies) >
				OFF_CHAN_TIME_THRESHOLD_MS) {
				WL_AF_TX_KEEP_PRI_CONNECTION_VSDB(cfg);
				off_chan_started_jiffies = jiffies;
			} else
				OSL_SLEEP(AF_RETRY_DELAY_TIME);
		}
#endif 
		ack = wl_cfgp2p_tx_action_frame(cfg, dev, af_params, bssidx) ?
			false : true;
	}

	if (ack == false) {
		WL_ERR(("Failed to send Action Frame(retry %d)\n", tx_retry));
	}
	WL_DBG(("Complete to send action frame\n"));
exit:
	
	wl_clr_drv_status(cfg, SENDING_ACT_FRM, dev);

#ifdef WL_CFG80211_SYNC_GON
	if (ack && config_af_params.extra_listen &&
		wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM) &&
		cfg->af_sent_channel == cfg->afx_hdl->my_listen_chan) {
		s32 extar_listen_time;

		extar_listen_time = af_params->dwell_time -
			jiffies_to_msecs(jiffies - cfg->af_tx_sent_jiffies);

		if (extar_listen_time > 50) {
			wl_set_drv_status(cfg, WAITING_NEXT_ACT_FRM_LISTEN, dev);
			WL_DBG(("Wait more time! actual af time:%d,"
				"calculated extar listen:%d\n",
				af_params->dwell_time, extar_listen_time));
			if (wl_cfgp2p_discover_listen(cfg, cfg->af_sent_channel,
				extar_listen_time + 100) == BCME_OK) {
				wait_for_completion_timeout(&cfg->wait_next_af,
					msecs_to_jiffies(extar_listen_time + 100 + 300));
			}
			wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM_LISTEN, dev);
		}
	}
#endif 
	wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, dev);

	if (cfg->afx_hdl->pending_tx_act_frm)
		cfg->afx_hdl->pending_tx_act_frm = NULL;

	WL_INFORM(("-- sending Action Frame is %s, listen chan: %d\n",
		(ack) ? "Succeeded!!":"Failed!!", cfg->afx_hdl->my_listen_chan));


	
	if (config_af_params.mpc_onoff == 1) {
		wldev_iovar_setint(dev, "mpc", 1);
	}

	return ack;
}

#define MAX_NUM_OF_ASSOCIATED_DEV       64
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
static s32
wl_cfg80211_mgmt_tx(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct cfg80211_mgmt_tx_params *params, u64 *cookie)
#else
static s32
wl_cfg80211_mgmt_tx(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct ieee80211_channel *channel, bool offchan,
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3, 7, 0))
	enum nl80211_channel_type channel_type,
	bool channel_type_valid,
#endif 
	unsigned int wait, const u8* buf, size_t len,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)) || defined(WL_COMPAT_WIRELESS)
	bool no_cck,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)) || defined(WL_COMPAT_WIRELESS)
	bool dont_wait_for_ack,
#endif
	u64 *cookie)
#endif 
{
	wl_action_frame_t *action_frame;
	wl_af_params_t *af_params;
	scb_val_t scb_val;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	struct ieee80211_channel *channel = params->chan;
	const u8 *buf = params->buf;
	size_t len = params->len;
#endif
	const struct ieee80211_mgmt *mgmt;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *dev = NULL;
	s32 err = BCME_OK;
	s32 bssidx = 0;
	u32 id;
	bool ack = false;
	s8 eabuf[ETHER_ADDR_STR_LEN];

	WL_DBG(("Enter \n"));

	dev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (!dev) {
		WL_ERR(("dev is NULL\n"));
		return -EINVAL;
	}

	
	if (discover_cfgdev(cfgdev, cfg)) {
		if (!cfg->p2p_supported || !cfg->p2p) {
			WL_ERR(("P2P doesn't setup completed yet\n"));
			return -EINVAL;
		}
		bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	}
	else {
		if ((bssidx = wl_get_bssidx_by_wdev(cfg, cfgdev_to_wdev(cfgdev))) < 0) {
			WL_ERR(("Find p2p index failed\n"));
			return BCME_ERROR;
		}
	}

	WL_DBG(("TX target bssidx=%d\n", bssidx));

	if (p2p_is_on(cfg)) {
		if ((err = wl_cfgp2p_discover_enable_search(cfg, false)) < 0) {
			WL_ERR(("Can not disable discovery mode\n"));
			return -EFAULT;
		}
	}
	*cookie = 0;
	id = cfg->send_action_id++;
	if (id == 0)
		id = cfg->send_action_id++;
	*cookie = id;
	mgmt = (const struct ieee80211_mgmt *)buf;
	if (ieee80211_is_mgmt(mgmt->frame_control)) {
		if (ieee80211_is_probe_resp(mgmt->frame_control)) {
			s32 ie_offset =  DOT11_MGMT_HDR_LEN + DOT11_BCN_PRB_FIXED_LEN;
			s32 ie_len = len - ie_offset;
			if ((dev == bcmcfg_to_prmry_ndev(cfg)) && cfg->p2p) {
				bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
			}
			wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
				VNDR_IE_PRBRSP_FLAG, (const u8 *)(buf + ie_offset), ie_len);
			cfg80211_mgmt_tx_status(cfgdev, *cookie, buf, len, true, GFP_KERNEL);
#if defined(P2P_IE_MISSING_FIX)
			if (!cfg->p2p_prb_noti) {
				cfg->p2p_prb_noti = true;
				WL_DBG(("%s: TX 802_1X Probe Response first time.\n",
					__FUNCTION__));
			}
#endif
			goto exit;
		} else if (ieee80211_is_disassoc(mgmt->frame_control) ||
			ieee80211_is_deauth(mgmt->frame_control)) {
			char mac_buf[MAX_NUM_OF_ASSOCIATED_DEV *
				sizeof(struct ether_addr) + sizeof(uint)] = {0};
			int num_associated = 0;
			struct maclist *assoc_maclist = (struct maclist *)mac_buf;
			if (!bcmp((const uint8 *)BSSID_BROADCAST,
				(const struct ether_addr *)mgmt->da, ETHER_ADDR_LEN)) {
				assoc_maclist->count = MAX_NUM_OF_ASSOCIATED_DEV;
				err = wldev_ioctl(dev, WLC_GET_ASSOCLIST,
					assoc_maclist, sizeof(mac_buf), false);
				if (err < 0)
					WL_ERR(("WLC_GET_ASSOCLIST error %d\n", err));
				else
					num_associated = assoc_maclist->count;
			}
			memcpy(scb_val.ea.octet, mgmt->da, ETH_ALEN);
			scb_val.val = mgmt->u.disassoc.reason_code;
			err = wldev_ioctl(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scb_val,
				sizeof(scb_val_t), true);
			if (err < 0)
				WL_ERR(("WLC_SCB_DEAUTHENTICATE_FOR_REASON error %d\n", err));
			WL_ERR(("Disconnect STA : %s scb_val.val %d\n",
				bcm_ether_ntoa((const struct ether_addr *)mgmt->da, eabuf),
				scb_val.val));

			if (num_associated > 0 && ETHER_ISBCAST(mgmt->da))
				wl_delay(400);

			cfg80211_mgmt_tx_status(cfgdev, *cookie, buf, len, true, GFP_KERNEL);
			goto exit;

		} else if (ieee80211_is_action(mgmt->frame_control)) {
#ifndef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
			wl_notify_escan_complete(cfg, dev, true, true);
#endif 
		}

	} else {
		WL_ERR(("Driver only allows MGMT packet type\n"));
		goto exit;
	}

	af_params = (wl_af_params_t *) kzalloc(WL_WIFI_AF_PARAMS_SIZE, GFP_KERNEL);

	if (af_params == NULL)
	{
		WL_ERR(("unable to allocate frame\n"));
		return -ENOMEM;
	}

	action_frame = &af_params->action_frame;

	
	action_frame->packetId = *cookie;
	WL_DBG(("action frame %d\n", action_frame->packetId));
	
	memcpy(&action_frame->da, &mgmt->da[0], ETHER_ADDR_LEN);
	memcpy(&af_params->BSSID, &mgmt->bssid[0], ETHER_ADDR_LEN);

	
	action_frame->len = len - DOT11_MGMT_HDR_LEN;
	WL_DBG(("action_frame->len: %d\n", action_frame->len));

	
	af_params->channel =
		ieee80211_frequency_to_channel(channel->center_freq);
	
	cfg->afx_hdl->peer_listen_chan = af_params->channel;
	WL_DBG(("channel from upper layer %d\n", cfg->afx_hdl->peer_listen_chan));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	af_params->dwell_time = params->wait;
#else
	af_params->dwell_time = wait;
#endif

	memcpy(action_frame->data, &buf[DOT11_MGMT_HDR_LEN], action_frame->len);

	ack = wl_cfg80211_send_action_frame(wiphy, dev, cfgdev, af_params,
		action_frame, action_frame->len, bssidx);
	cfg80211_mgmt_tx_status(cfgdev, *cookie, buf, len, ack, GFP_KERNEL);

	kfree(af_params);
exit:
	return err;
}


static void
wl_cfg80211_mgmt_frame_register(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	u16 frame_type, bool reg)
{

	WL_DBG(("frame_type: %x, reg: %d\n", frame_type, reg));

	if (frame_type != (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ))
		return;

	return;
}


static s32
wl_cfg80211_change_bss(struct wiphy *wiphy,
	struct net_device *dev,
	struct bss_parameters *params)
{
	s32 err = 0;
	s32 ap_isolate = 0;

#ifdef PCIE_FULL_DONGLE
	s32 ifidx = DHD_BAD_IF;
#endif 

#if defined(SUPPORT_HOSTAPD_BGN_MODE)
	s32 gmode = -1, nmode = -1;
	s32 gmode_prev = -1, nmode_prev = -1;
#endif 

#if defined(PCIE_FULL_DONGLE) || defined(SUPPORT_HOSTAPD_BGN_MODE)
	dhd_pub_t *dhd;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd = (dhd_pub_t *)(cfg->pub);
#if defined(WL_ENABLE_P2P_IF)
	if (cfg->p2p_net == dev)
		dev = bcmcfg_to_prmry_ndev(cfg);
#endif
#endif 

	if (params->use_cts_prot >= 0) {
	}

	if (params->use_short_preamble >= 0) {
	}

	if (params->use_short_slot_time >= 0) {
	}

	if (params->basic_rates) {
#if defined(SUPPORT_HOSTAPD_BGN_MODE)
		switch ((int)(params->basic_rates[params->basic_rates_len -1])) {
			case 22: 
				gmode = 0;
				nmode = 0;
				break;
			case 108: 
				gmode = 2;
				nmode = 0;
				break;
			default:
				gmode = -1;
				nmode = -1;
				break;
		}
#endif 
	}

	if (params->ap_isolate >= 0) {
		ap_isolate = params->ap_isolate;
#ifdef PCIE_FULL_DONGLE
		ifidx = dhd_net2idx(dhd->info, dev);
		if (ifidx != DHD_BAD_IF) {
			err = dhd_set_ap_isolate(dhd, ifidx, ap_isolate);
		} else {
			WL_ERR(("Failed to set ap_isolate\n"));
		}
#else
		err = wldev_iovar_setint(dev, "ap_isolate", ap_isolate);
		if (unlikely(err))
		{
			WL_ERR(("set ap_isolate Error (%d)\n", err));
		}
#endif 
	}

	if (params->ht_opmode >= 0) {
#if defined(SUPPORT_HOSTAPD_BGN_MODE)
		nmode = 1;
		gmode = 1;
	} else {
		nmode = 0;
#endif 
	}

#if defined(SUPPORT_HOSTAPD_BGN_MODE)
	err = wldev_iovar_getint(dev, "nmode", &nmode_prev);
	if (unlikely(err)) {
		WL_ERR(("error reading nmode (%d)\n", err));
	}
	if (nmode == nmode_prev) {
		nmode = -1;
	}
	err = wldev_ioctl(dev, WLC_GET_GMODE, &gmode_prev, sizeof(gmode_prev), 0);
	if (unlikely(err)) {
		WL_ERR(("error reading gmode (%d)\n", err));
	}
	if (gmode == gmode_prev) {
		gmode = -1;
	}

	if (((dhd->op_mode & DHD_FLAG_HOSTAP_MODE) == DHD_FLAG_HOSTAP_MODE) &&
		((gmode > -1) || (nmode > -1))) {
		s32 val = 0;

		err = wldev_ioctl(dev, WLC_DOWN, &val, sizeof(s32), true);
		if (unlikely(err))
			WL_ERR(("WLC_DOWN command failed:[%d]\n", err));

		if (nmode > -1) {
			err = wldev_iovar_setint(dev, "nmode", nmode);
			if (unlikely(err))
				WL_ERR(("nmode command failed:mode[%d]:err[%d]\n", nmode, err));
		}

		if (gmode > -1) {
			err = wldev_ioctl(dev, WLC_SET_GMODE, &gmode, sizeof(s32), true);
			if (unlikely(err))
				WL_ERR(("WLC_SET_GMODE command failed:mode[%d]:err[%d]\n",
					gmode, err));
		}

		val = 0;
		err = wldev_ioctl(dev, WLC_UP, &val, sizeof(s32), true);
		if (unlikely(err))
			WL_ERR(("WLC_UP command failed:err[%d]\n", err));

	}
#endif 

	return err;
}

static s32
wl_cfg80211_set_channel(struct wiphy *wiphy, struct net_device *dev,
	struct ieee80211_channel *chan,
	enum nl80211_channel_type channel_type)
{
	s32 _chan;
	chanspec_t chspec = 0;
	chanspec_t fw_chspec = 0;
	u32 bw = WL_CHANSPEC_BW_20;
#ifdef WL11ULB
	u32 ulb_bw = wl_cfg80211_get_ulb_bw(dev->ieee80211_ptr);
#endif 

	s32 err = BCME_OK;
	s32 bw_cap = 0;
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#ifdef CUSTOM_SET_CPUCORE
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
#endif 

	dev = ndev_to_wlc_ndev(dev, cfg);
	_chan = ieee80211_frequency_to_channel(chan->center_freq);
	WL_ERR(("netdev_ifidx(%d), chan_type(%d) target channel(%d) \n",
		dev->ifindex, channel_type, _chan));


#ifdef WL11ULB
	if (ulb_bw) {
		WL_DBG(("[ULB] setting AP/GO BW to ulb_bw 0x%x \n", ulb_bw));
		bw = wl_cfg80211_ulbbw_to_ulbchspec(ulb_bw);
		goto set_channel;
	}
#endif 
	if (chan->band == IEEE80211_BAND_5GHZ) {
		param.band = WLC_BAND_5G;
		err = wldev_iovar_getbuf(dev, "bw_cap", &param, sizeof(param),
			cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
		WL_ERR(("bw_cap param %p len %zd iov %p size %d\n",
			&param, sizeof(param), cfg->ioctl_buf, WLC_IOCTL_SMLEN));
		if (err) {
			if (err != BCME_UNSUPPORTED) {
				WL_ERR(("bw_cap failed, %d\n", err));
				return err;
			} else {
				err = wldev_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
				if (err) {
					WL_ERR(("error get mimo_bw_cap (%d)\n", err));
				}
				if (bw_cap != WLC_N_BW_20ALL)
					bw = WL_CHANSPEC_BW_40;
			}
		} else {
			if (WL_BW_CAP_80MHZ(cfg->ioctl_buf[0]))
#ifdef CUSTOMER_HW_ONE
			{
				if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
					WL_ERR(("%s force 5G bw 80 to bw 40  \n",
						__FUNCTION__));
					bw = WL_CHANSPEC_BW_40;
				} else
					bw = WL_CHANSPEC_BW_80;
			}
#else
				bw = WL_CHANSPEC_BW_80;
#endif 
			else if (WL_BW_CAP_40MHZ(cfg->ioctl_buf[0]))
				bw = WL_CHANSPEC_BW_40;
			else
				bw = WL_CHANSPEC_BW_20;

		}

	} else if (chan->band == IEEE80211_BAND_2GHZ)
#if defined(CUSTOMER_HW_ONE) && defined(SUPPORT_2G_HT40)
	{
		if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
			if (hotspot_bw == 20)
				bw = WL_CHANSPEC_BW_20;
			else
				bw = WL_CHANSPEC_BW_40;
		}
		else
			bw = WL_CHANSPEC_BW_20;
	}
#else
		bw = WL_CHANSPEC_BW_20;
#endif 
set_channel:
	chspec = wf_channel2chspec(_chan, bw);
	if (wf_chspec_valid(chspec)) {
		fw_chspec = wl_chspec_host_to_driver(chspec);
		if (fw_chspec != INVCHANSPEC) {
			if ((err = wldev_iovar_setint(dev, "chanspec",
				fw_chspec)) == BCME_BADCHAN) {
				if (bw == WL_CHANSPEC_BW_80)
					goto change_bw;
				err = wldev_ioctl(dev, WLC_SET_CHANNEL,
					&_chan, sizeof(_chan), true);
				if (err < 0) {
					WL_ERR(("WLC_SET_CHANNEL error %d"
					"chip may not be supporting this channel\n", err));
				}
			} else if (err) {
				WL_ERR(("failed to set chanspec error %d\n", err));
			}
		} else {
			WL_ERR(("failed to convert host chanspec to fw chanspec\n"));
			err = BCME_ERROR;
		}
	} else {
change_bw:
		if (bw == WL_CHANSPEC_BW_80)
			bw = WL_CHANSPEC_BW_40;
		else if (bw == WL_CHANSPEC_BW_40)
			bw = WL_CHANSPEC_BW_20;
		else
			bw = 0;
		if (bw)
			goto set_channel;
		WL_ERR(("Invalid chanspec 0x%x\n", chspec));
		err = BCME_ERROR;
	}
#ifdef CUSTOM_SET_CPUCORE
	if (dhd->op_mode == DHD_FLAG_HOSTAP_MODE) {
		WL_DBG(("SoftAP mode do not need to set cpucore\n"));
	} else if (chspec & WL_CHANSPEC_BW_80) {
		
		if ((dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) &&
			dev != bcmcfg_to_prmry_ndev(cfg)) {
			
			dhd->chan_isvht80 |= DHD_FLAG_HOSTAP_MODE;
			dhd_set_cpucore(dhd, TRUE);
		} else if (is_p2p_group_iface(dev->ieee80211_ptr)) {
			
			dhd->chan_isvht80 |= DHD_FLAG_P2P_MODE;
			dhd_set_cpucore(dhd, TRUE);
		}
	}
#endif 
	return err;
}

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
struct net_device *
wl_cfg80211_get_remain_on_channel_ndev(struct bcm_cfg80211 *cfg)
{
	struct net_info *_net_info, *next;
	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
		if (_net_info->ndev &&
			test_bit(WL_STATUS_REMAINING_ON_CHANNEL, &_net_info->sme_state))
			return _net_info->ndev;
	}
	return NULL;
}
#endif 

static s32
wl_validate_opensecurity(struct net_device *dev, s32 bssidx, bool privacy)
{
	s32 err = BCME_OK;
	u32 wpa_val;
	s32 wsec = 0;

	
	err = wldev_iovar_setint_bsscfg(dev, "auth", 0, bssidx);
	if (err < 0) {
		WL_ERR(("auth error %d\n", err));
		return BCME_ERROR;
	}

	if (privacy) {
		
		wsec = WEP_ENABLED;
		WL_DBG(("Setting wsec to %d for WEP \n", wsec));
	}

	
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (err < 0) {
		WL_ERR(("wsec error %d\n", err));
		return BCME_ERROR;
	}

	
	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_ADHOC)
		wpa_val = WPA_AUTH_NONE;
	else
		wpa_val = WPA_AUTH_DISABLED;
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wpa_val, bssidx);
	if (err < 0) {
		WL_ERR(("wpa_auth error %d\n", err));
		return BCME_ERROR;
	}

	return 0;
}

static s32
wl_validate_wpa2ie(struct net_device *dev, bcm_tlv_t *wpa2ie, s32 bssidx)
{
	s32 len = 0;
	s32 err = BCME_OK;
	u16 auth = 0; 
	u32 wsec;
	u32 pval = 0;
	u32 gval = 0;
	u32 wpa_auth = 0;
	wpa_suite_mcast_t *mcast;
	wpa_suite_ucast_t *ucast;
	wpa_suite_auth_key_mgmt_t *mgmt;
	wpa_pmkid_list_t *pmkid;
	int cnt = 0;
#ifdef MFP
	int mfp = 0;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
#endif 

	u16 suite_count;
	u8 rsn_cap[2];
	u32 wme_bss_disable;

	if (wpa2ie == NULL)
		goto exit;

	WL_DBG(("Enter \n"));
	len =  wpa2ie->len - WPA2_VERSION_LEN;
	
	mcast = (wpa_suite_mcast_t *)&wpa2ie->data[WPA2_VERSION_LEN];
	switch (mcast->type) {
		case WPA_CIPHER_NONE:
			gval = 0;
			break;
		case WPA_CIPHER_WEP_40:
		case WPA_CIPHER_WEP_104:
			gval = WEP_ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			gval = TKIP_ENABLED;
			break;
		case WPA_CIPHER_AES_CCM:
			gval = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WAPI_CIPHER_SMS4:
			gval = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("No Security Info\n"));
			break;
	}
	if ((len -= WPA_SUITE_LEN) <= 0)
		return BCME_BADLEN;

	
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	suite_count = ltoh16_ua(&ucast->count);
	switch (ucast->list[0].type) {
		case WPA_CIPHER_NONE:
			pval = 0;
			break;
		case WPA_CIPHER_WEP_40:
		case WPA_CIPHER_WEP_104:
			pval = WEP_ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			pval = TKIP_ENABLED;
			break;
		case WPA_CIPHER_AES_CCM:
			pval = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WAPI_CIPHER_SMS4:
			pval = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("No Security Info\n"));
	}
	if ((len -= (WPA_IE_SUITE_COUNT_LEN + (WPA_SUITE_LEN * suite_count))) <= 0)
		return BCME_BADLEN;

	
	wsec = (pval | gval | SES_OW_ENABLED);
	
	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[suite_count];
	suite_count = cnt = ltoh16_ua(&mgmt->count);
	while (cnt--) {
		switch (mgmt->list[cnt].type) {
		case RSN_AKM_NONE:
				wpa_auth |= WPA_AUTH_NONE;
			break;
		case RSN_AKM_UNSPECIFIED:
				wpa_auth |= WPA2_AUTH_UNSPECIFIED;
			break;
		case RSN_AKM_PSK:
				wpa_auth |= WPA2_AUTH_PSK;
				break;
#ifdef MFP
			case RSN_AKM_MFP_PSK:
				wpa_auth |= WPA2_AUTH_PSK_SHA256;
				break;
			case RSN_AKM_MFP_1X:
				wpa_auth |= WPA2_AUTH_1X_SHA256;
			break;
#endif 
		default:
			WL_ERR(("No Key Mgmt Info\n"));
		}
	}

	if ((len -= (WPA_IE_SUITE_COUNT_LEN + (WPA_SUITE_LEN * suite_count))) >= RSN_CAP_LEN) {
		rsn_cap[0] = *(u8 *)&mgmt->list[suite_count];
		rsn_cap[1] = *((u8 *)&mgmt->list[suite_count] + 1);

		if (rsn_cap[0] & (RSN_CAP_16_REPLAY_CNTRS << RSN_CAP_PTK_REPLAY_CNTR_SHIFT)) {
			wme_bss_disable = 0;
		} else {
			wme_bss_disable = 1;
		}

#ifdef MFP
	if (rsn_cap[0] & RSN_CAP_MFPR) {
		WL_DBG(("MFP Required \n"));
		mfp = WL_MFP_REQUIRED;
		if (wpa_auth & WPA2_AUTH_PSK_SHA256) {
			wpa_auth |= WPA2_AUTH_PSK;
		} else if (wpa_auth & WPA2_AUTH_1X_SHA256) {
			wpa_auth |= WPA2_AUTH_UNSPECIFIED;
		}
	} else if (rsn_cap[0] & RSN_CAP_MFPC) {
		WL_DBG(("MFP Capable \n"));
		mfp = WL_MFP_CAPABLE;
	}
#endif 

		
		err = wldev_iovar_setint_bsscfg(dev, "wme_bss_disable", wme_bss_disable, bssidx);
		if (err < 0) {
			WL_ERR(("wme_bss_disable error %d\n", err));
			return BCME_ERROR;
		}
	} else {
		WL_DBG(("There is no RSN Capabilities. remained len %d\n", len));
	}

	len -= RSN_CAP_LEN;
	if (len >= WPA2_PMKID_COUNT_LEN) {
		pmkid = (wpa_pmkid_list_t *)((u8 *)&mgmt->list[suite_count] + RSN_CAP_LEN);
		cnt = ltoh16_ua(&pmkid->count);
		if (cnt != 0) {
			WL_ERR(("AP has non-zero PMKID count. Wrong!\n"));
			return BCME_ERROR;
		}
		
		
	}

#ifdef MFP
	len -= WPA2_PMKID_COUNT_LEN;
	if (len >= WPA_SUITE_LEN) {
		err = wldev_iovar_setbuf_bsscfg(dev, "bip",
		(void *)((u8 *)&mgmt->list[suite_count] + RSN_CAP_LEN + WPA2_PMKID_COUNT_LEN),
		WPA_SUITE_LEN,
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync);
		if (err < 0) {
			WL_ERR(("bip set error %d\n", err));
			return BCME_ERROR;
		}
	}
#endif

	
	err = wldev_iovar_setint_bsscfg(dev, "auth", auth, bssidx);
	if (err < 0) {
		WL_ERR(("auth error %d\n", err));
		return BCME_ERROR;
	}

	
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (err < 0) {
		WL_ERR(("wsec error %d\n", err));
		return BCME_ERROR;
	}

#ifdef MFP
	if (mfp) {
		if ((err = wldev_iovar_setint_bsscfg(dev, "mfp", mfp, bssidx)) < 0) {
			WL_ERR(("MFP Setting failed. ret = %d \n", err));
			return err;
		}
	}
#endif 

	
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wpa_auth, bssidx);
	if (err < 0) {
		WL_ERR(("wpa_auth error %d\n", err));
		return BCME_ERROR;
	}
exit:
	return 0;
}

static s32
wl_validate_wpaie(struct net_device *dev, wpa_ie_fixed_t *wpaie, s32 bssidx)
{
	wpa_suite_mcast_t *mcast;
	wpa_suite_ucast_t *ucast;
	wpa_suite_auth_key_mgmt_t *mgmt;
	u16 auth = 0; 
	u16 count;
	s32 err = BCME_OK;
	s32 len = 0;
	u32 i;
	u32 wsec;
	u32 pval = 0;
	u32 gval = 0;
	u32 wpa_auth = 0;
	u32 tmp = 0;

	if (wpaie == NULL)
		goto exit;
	WL_DBG(("Enter \n"));
	len = wpaie->length;    
	len -= WPA_IE_TAG_FIXED_LEN;
	
	if (len < WPA_SUITE_LEN) {
		WL_INFORM(("no multicast cipher suite\n"));
		goto exit;
	}

	
	mcast = (wpa_suite_mcast_t *)&wpaie[1];
	len -= WPA_SUITE_LEN;
	if (!bcmp(mcast->oui, WPA_OUI, WPA_OUI_LEN)) {
		if (IS_WPA_CIPHER(mcast->type)) {
			tmp = 0;
			switch (mcast->type) {
				case WPA_CIPHER_NONE:
					tmp = 0;
					break;
				case WPA_CIPHER_WEP_40:
				case WPA_CIPHER_WEP_104:
					tmp = WEP_ENABLED;
					break;
				case WPA_CIPHER_TKIP:
					tmp = TKIP_ENABLED;
					break;
				case WPA_CIPHER_AES_CCM:
					tmp = AES_ENABLED;
					break;
				default:
					WL_ERR(("No Security Info\n"));
			}
			gval |= tmp;
		}
	}
	
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		WL_INFORM(("no unicast suite\n"));
		goto exit;
	}
	
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	count = ltoh16_ua(&ucast->count);
	len -= WPA_IE_SUITE_COUNT_LEN;
	for (i = 0; i < count && len >= WPA_SUITE_LEN;
		i++, len -= WPA_SUITE_LEN) {
		if (!bcmp(ucast->list[i].oui, WPA_OUI, WPA_OUI_LEN)) {
			if (IS_WPA_CIPHER(ucast->list[i].type)) {
				tmp = 0;
				switch (ucast->list[i].type) {
					case WPA_CIPHER_NONE:
						tmp = 0;
						break;
					case WPA_CIPHER_WEP_40:
					case WPA_CIPHER_WEP_104:
						tmp = WEP_ENABLED;
						break;
					case WPA_CIPHER_TKIP:
						tmp = TKIP_ENABLED;
						break;
					case WPA_CIPHER_AES_CCM:
						tmp = AES_ENABLED;
						break;
					default:
						WL_ERR(("No Security Info\n"));
				}
				pval |= tmp;
			}
		}
	}
	len -= (count - i) * WPA_SUITE_LEN;
	
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		WL_INFORM((" no auth key mgmt suite\n"));
		goto exit;
	}
	
	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[count];
	count = ltoh16_ua(&mgmt->count);
	len -= WPA_IE_SUITE_COUNT_LEN;
	for (i = 0; i < count && len >= WPA_SUITE_LEN;
		i++, len -= WPA_SUITE_LEN) {
		if (!bcmp(mgmt->list[i].oui, WPA_OUI, WPA_OUI_LEN)) {
			if (IS_WPA_AKM(mgmt->list[i].type)) {
				tmp = 0;
				switch (mgmt->list[i].type) {
					case RSN_AKM_NONE:
						tmp = WPA_AUTH_NONE;
						break;
					case RSN_AKM_UNSPECIFIED:
						tmp = WPA_AUTH_UNSPECIFIED;
						break;
					case RSN_AKM_PSK:
						tmp = WPA_AUTH_PSK;
						break;
					default:
						WL_ERR(("No Key Mgmt Info\n"));
				}
				wpa_auth |= tmp;
			}
		}

	}
	
	wsec = (pval | gval | SES_OW_ENABLED);
	
	err = wldev_iovar_setint_bsscfg(dev, "auth", auth, bssidx);
	if (err < 0) {
		WL_ERR(("auth error %d\n", err));
		return BCME_ERROR;
	}
	
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (err < 0) {
		WL_ERR(("wsec error %d\n", err));
		return BCME_ERROR;
	}
	
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wpa_auth, bssidx);
	if (err < 0) {
		WL_ERR(("wpa_auth error %d\n", err));
		return BCME_ERROR;
	}
exit:
	return 0;
}

#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
static u32 wl_get_cipher_type(uint8 type)
{
	u32 ret = 0;
	switch (type) {
		case WPA_CIPHER_NONE:
			ret = 0;
			break;
		case WPA_CIPHER_WEP_40:
		case WPA_CIPHER_WEP_104:
			ret = WEP_ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			ret = TKIP_ENABLED;
			break;
		case WPA_CIPHER_AES_CCM:
			ret = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WAPI_CIPHER_SMS4:
			ret = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("No Security Info\n"));
	}
	return ret;
}

static u32 wl_get_suite_auth_key_mgmt_type(uint8 type)
{
	u32 ret = 0;
	switch (type) {
		case RSN_AKM_NONE:
			ret = WPA_AUTH_NONE;
			break;
		case RSN_AKM_UNSPECIFIED:
			ret = WPA_AUTH_UNSPECIFIED;
			break;
		case RSN_AKM_PSK:
			ret = WPA_AUTH_PSK;
			break;
		default:
			WL_ERR(("No Key Mgmt Info\n"));
	}
	return ret;
}

static s32
wl_validate_wpaie_wpa2ie(struct net_device *dev, wpa_ie_fixed_t *wpaie,
	bcm_tlv_t *wpa2ie, s32 bssidx)
{
	wpa_suite_mcast_t *mcast;
	wpa_suite_ucast_t *ucast;
	wpa_suite_auth_key_mgmt_t *mgmt;
	u16 auth = 0; 
	u16 count;
	s32 err = BCME_OK;
	u32 wme_bss_disable;
	u16 suite_count;
	u8 rsn_cap[2];
	s32 len = 0;
	u32 i;
	u32 wsec1, wsec2, wsec;
	u32 pval = 0;
	u32 gval = 0;
	u32 wpa_auth = 0;
	u32 wpa_auth1 = 0;
	u32 wpa_auth2 = 0;
	u8* ptmp;

	if (wpaie == NULL || wpa2ie == NULL)
		goto exit;

	WL_DBG(("Enter \n"));
	len = wpaie->length;    
	len -= WPA_IE_TAG_FIXED_LEN;
	
	if (len < WPA_SUITE_LEN) {
		WL_INFORM(("no multicast cipher suite\n"));
		goto exit;
	}

	
	mcast = (wpa_suite_mcast_t *)&wpaie[1];
	len -= WPA_SUITE_LEN;
	if (!bcmp(mcast->oui, WPA_OUI, WPA_OUI_LEN)) {
		if (IS_WPA_CIPHER(mcast->type)) {
			gval |= wl_get_cipher_type(mcast->type);
		}
	}
	WL_ERR(("\nwpa ie validate\n"));
	WL_ERR(("wpa ie mcast cipher = 0x%X\n", gval));

	
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		WL_INFORM(("no unicast suite\n"));
		goto exit;
	}

	
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	count = ltoh16_ua(&ucast->count);
	len -= WPA_IE_SUITE_COUNT_LEN;
	for (i = 0; i < count && len >= WPA_SUITE_LEN;
		i++, len -= WPA_SUITE_LEN) {
		if (!bcmp(ucast->list[i].oui, WPA_OUI, WPA_OUI_LEN)) {
			if (IS_WPA_CIPHER(ucast->list[i].type)) {
				pval |= wl_get_cipher_type(ucast->list[i].type);
			}
		}
	}
	WL_ERR(("wpa ie ucast count =%d, cipher = 0x%X\n", count, pval));

	
	wsec1 = (pval | gval | SES_OW_ENABLED);
	WL_ERR(("wpa ie wsec = 0x%X\n", wsec1));

	len -= (count - i) * WPA_SUITE_LEN;
	
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		WL_INFORM((" no auth key mgmt suite\n"));
		goto exit;
	}
	
	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[count];
	count = ltoh16_ua(&mgmt->count);
	len -= WPA_IE_SUITE_COUNT_LEN;
	for (i = 0; i < count && len >= WPA_SUITE_LEN;
		i++, len -= WPA_SUITE_LEN) {
		if (!bcmp(mgmt->list[i].oui, WPA_OUI, WPA_OUI_LEN)) {
			if (IS_WPA_AKM(mgmt->list[i].type)) {

				wpa_auth1 |= wl_get_suite_auth_key_mgmt_type(mgmt->list[i].type);
			}
		}

	}
	WL_ERR(("wpa ie wpa_suite_auth_key_mgmt count=%d, key_mgmt = 0x%X\n", count, wpa_auth1));
	WL_ERR(("\nwpa2 ie validate\n"));

	pval = 0;
	gval = 0;
	len =  wpa2ie->len;
	
	mcast = (wpa_suite_mcast_t *)&wpa2ie->data[WPA2_VERSION_LEN];
	ptmp = mcast->oui;
	gval = wl_get_cipher_type(ptmp[DOT11_OUI_LEN]);

	WL_ERR(("wpa2 ie mcast cipher = 0x%X\n", gval));
	if ((len -= WPA_SUITE_LEN) <= 0)
	{
		WL_ERR(("P:wpa2 ie len[%d]", len));
		return BCME_BADLEN;
	}

	
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	suite_count = ltoh16_ua(&ucast->count);
	WL_ERR((" WPA2 ucast cipher count=%d\n", suite_count));
	pval |= wl_get_cipher_type(ucast->list[0].type);

	if ((len -= (WPA_IE_SUITE_COUNT_LEN + (WPA_SUITE_LEN * suite_count))) <= 0)
		return BCME_BADLEN;

	WL_ERR(("wpa2 ie ucast cipher = 0x%X\n", pval));

	
	wsec2 = (pval | gval | SES_OW_ENABLED);
	WL_ERR(("wpa2 ie wsec = 0x%X\n", wsec2));

	
	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[suite_count];
	suite_count = ltoh16_ua(&mgmt->count);
	ptmp = (u8 *)&mgmt->list[0];
	wpa_auth2 = wl_get_suite_auth_key_mgmt_type(ptmp[DOT11_OUI_LEN]);
	WL_ERR(("wpa ie wpa_suite_auth_key_mgmt count=%d, key_mgmt = 0x%X\n", count, wpa_auth2));

	if ((len -= (WPA_IE_SUITE_COUNT_LEN + (WPA_SUITE_LEN * suite_count))) >= RSN_CAP_LEN) {
		rsn_cap[0] = *(u8 *)&mgmt->list[suite_count];
		rsn_cap[1] = *((u8 *)&mgmt->list[suite_count] + 1);
		if (rsn_cap[0] & (RSN_CAP_16_REPLAY_CNTRS << RSN_CAP_PTK_REPLAY_CNTR_SHIFT)) {
			wme_bss_disable = 0;
		} else {
			wme_bss_disable = 1;
		}
		WL_DBG(("P:rsn_cap[0]=[0x%X]:wme_bss_disabled[%d]\n", rsn_cap[0], wme_bss_disable));

		
		err = wldev_iovar_setint_bsscfg(dev, "wme_bss_disable", wme_bss_disable, bssidx);
		if (err < 0) {
			WL_ERR(("wme_bss_disable error %d\n", err));
			return BCME_ERROR;
		}
	} else {
		WL_DBG(("There is no RSN Capabilities. remained len %d\n", len));
	}

	wsec = (wsec1 | wsec2);
	wpa_auth = (wpa_auth1 | wpa_auth2);
	WL_ERR(("wpa_wpa2 wsec=0x%X wpa_auth=0x%X\n", wsec, wpa_auth));

	
	err = wldev_iovar_setint_bsscfg(dev, "auth", auth, bssidx);
	if (err < 0) {
		WL_ERR(("auth error %d\n", err));
		return BCME_ERROR;
	}
	
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (err < 0) {
		WL_ERR(("wsec error %d\n", err));
		return BCME_ERROR;
	}
	
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wpa_auth, bssidx);
	if (err < 0) {
		WL_ERR(("wpa_auth error %d\n", err));
		return BCME_ERROR;
	}
exit:
	return 0;
}
#endif 

static s32
wl_cfg80211_bcn_validate_sec(
	struct net_device *dev,
	struct parsed_ies *ies,
	u32 dev_role,
	s32 bssidx,
	bool privacy)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	wl_cfgbss_t *bss = wl_get_cfgbss_by_wdev(cfg, dev->ieee80211_ptr);

	if (!bss) {
		WL_ERR(("cfgbss is NULL \n"));
		return BCME_ERROR;
	}

	if (dev_role == NL80211_IFTYPE_P2P_GO && (ies->wpa2_ie)) {
		
		WL_DBG(("P2P GO: validating wpa2_ie"));
		if (wl_validate_wpa2ie(dev, ies->wpa2_ie, bssidx)  < 0)
			return BCME_ERROR;

	} else if (dev_role == NL80211_IFTYPE_AP) {

		WL_DBG(("SoftAP: validating security"));
		

#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
		if ((ies->wpa_ie != NULL && ies->wpa2_ie != NULL)) {
			if (wl_validate_wpaie_wpa2ie(dev, ies->wpa_ie, ies->wpa2_ie, bssidx)  < 0) {
				cfg->ap_info->security_mode = false;
				return BCME_ERROR;
			}
		}
		else {
#endif 
		if ((ies->wpa2_ie || ies->wpa_ie) &&
			((wl_validate_wpa2ie(dev, ies->wpa2_ie, bssidx)  < 0 ||
			wl_validate_wpaie(dev, ies->wpa_ie, bssidx) < 0))) {
			bss->security_mode = false;
			return BCME_ERROR;
		}

		bss->security_mode = true;
		if (bss->rsn_ie) {
			kfree(bss->rsn_ie);
			bss->rsn_ie = NULL;
		}
		if (bss->wpa_ie) {
			kfree(bss->wpa_ie);
			bss->wpa_ie = NULL;
		}
		if (bss->wps_ie) {
			kfree(bss->wps_ie);
			bss->wps_ie = NULL;
		}
		if (ies->wpa_ie != NULL) {
			
			bss->rsn_ie = NULL;
			bss->wpa_ie = kmemdup(ies->wpa_ie,
				ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
				GFP_KERNEL);
		} else if (ies->wpa2_ie != NULL) {
			
			bss->wpa_ie = NULL;
			bss->rsn_ie = kmemdup(ies->wpa2_ie,
				ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN,
				GFP_KERNEL);
		}
#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
		}
#endif 
		if (!ies->wpa2_ie && !ies->wpa_ie) {
			wl_validate_opensecurity(dev, bssidx, privacy);
			bss->security_mode = false;
		}

		if (ies->wps_ie) {
			bss->wps_ie = kmemdup(ies->wps_ie, ies->wps_ie_len, GFP_KERNEL);
		}
	}

	return 0;

}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || defined(WL_COMPAT_WIRELESS)
static s32 wl_cfg80211_bcn_set_params(
	struct cfg80211_ap_settings *info,
	struct net_device *dev,
	u32 dev_role, s32 bssidx)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 err = BCME_OK;

	WL_DBG(("interval (%d) \ndtim_period (%d) \n",
		info->beacon_interval, info->dtim_period));

	if (info->beacon_interval) {
		if ((err = wldev_ioctl(dev, WLC_SET_BCNPRD,
			&info->beacon_interval, sizeof(s32), true)) < 0) {
			WL_ERR(("Beacon Interval Set Error, %d\n", err));
			return err;
		}
	}

	if (info->dtim_period) {
		if ((err = wldev_ioctl(dev, WLC_SET_DTIMPRD,
			&info->dtim_period, sizeof(s32), true)) < 0) {
			WL_ERR(("DTIM Interval Set Error, %d\n", err));
			return err;
		}
	}

	if ((info->ssid) && (info->ssid_len > 0) &&
		(info->ssid_len <= 32)) {
		WL_DBG(("SSID (%s) len:%zd \n", info->ssid, info->ssid_len));
		if (dev_role == NL80211_IFTYPE_AP) {
			
			memset(cfg->hostapd_ssid.SSID, 0x00, 32);
			memcpy(cfg->hostapd_ssid.SSID, info->ssid, info->ssid_len);
			cfg->hostapd_ssid.SSID_len = info->ssid_len;
		} else {
				
			memset(cfg->p2p->ssid.SSID, 0x00, 32);
			memcpy(cfg->p2p->ssid.SSID, info->ssid, info->ssid_len);
			cfg->p2p->ssid.SSID_len = info->ssid_len;
		}
	}

	if (info->hidden_ssid) {
		if ((err = wldev_iovar_setint(dev, "closednet", 1)) < 0)
			WL_ERR(("failed to set hidden : %d\n", err));
		WL_DBG(("hidden_ssid_enum_val: %d \n", info->hidden_ssid));
	}

	return err;
}
#endif 

static s32
wl_cfg80211_parse_ies(u8 *ptr, u32 len, struct parsed_ies *ies)
{
	s32 err = BCME_OK;

	memset(ies, 0, sizeof(struct parsed_ies));

	
	if ((ies->wps_ie = wl_cfgp2p_find_wpsie(ptr, len)) != NULL) {
		WL_DBG(("WPSIE in beacon \n"));
		ies->wps_ie_len = ies->wps_ie->length + WPA_RSN_IE_TAG_FIXED_LEN;
	} else {
		WL_ERR(("No WPSIE in beacon \n"));
	}

	
	if ((ies->wpa2_ie = bcm_parse_tlvs(ptr, len,
		DOT11_MNG_RSN_ID)) != NULL) {
		WL_DBG((" WPA2 IE found\n"));
		ies->wpa2_ie_len = ies->wpa2_ie->len;
	}

	
	if ((ies->wpa_ie = wl_cfgp2p_find_wpaie(ptr, len)) != NULL) {
		WL_DBG((" WPA found\n"));
		ies->wpa_ie_len = ies->wpa_ie->length;
	}

	return err;

}

static s32
wl_cfg80211_bcn_bringup_ap(
	struct net_device *dev,
	struct parsed_ies *ies,
	u32 dev_role, s32 bssidx)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wl_join_params join_params;
	bool is_bssup = false;
	s32 infra = 1;
	s32 join_params_size = 0;
	s32 ap = 1;
	s32 pm;
	s32 wsec;
#ifdef DISABLE_11H_SOFTAP
	s32 spect = 0;
#endif 
	s32 err = BCME_OK;
	s32 is_rsdb_supported = BCME_ERROR;

	is_rsdb_supported = DHD_OPMODE_SUPPORTED(cfg->pub, DHD_FLAG_RSDB_MODE);
	if (is_rsdb_supported < 0)
		return (-ENODEV);

	WL_DBG(("Enter dev_role:%d bssidx:%d\n", dev_role, bssidx));

	
	wldev_iovar_setint(dev, "mpc", 0);

	if (dev_role == NL80211_IFTYPE_P2P_GO) {
		is_bssup = wl_cfgp2p_bss_isup(dev, bssidx);
		if (!is_bssup && (ies->wpa2_ie != NULL)) {

			err = wldev_ioctl(dev, WLC_SET_INFRA, &infra, sizeof(s32), true);
			if (err < 0) {
				WL_ERR(("SET INFRA error %d\n", err));
				goto exit;
			}

			err = wldev_iovar_setbuf_bsscfg(dev, "ssid", &cfg->p2p->ssid,
				sizeof(cfg->p2p->ssid), cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
				bssidx, &cfg->ioctl_buf_sync);
			if (err < 0) {
				WL_ERR(("GO SSID setting error %d\n", err));
				goto exit;
			}

			
			wl_cfg80211_scan_abort(cfg);

			if ((err = wl_cfgp2p_bss(cfg, dev, bssidx, 1)) < 0) {
				WL_ERR(("GO Bring up error %d\n", err));
				goto exit;
			}
		} else
			WL_DBG(("Bss is already up\n"));
	} else if ((dev_role == NL80211_IFTYPE_AP) &&
		(wl_get_drv_status(cfg, AP_CREATING, dev))) {
		

		WL_DBG(("Creating AP bssidx:%d dev_role:%d\n", bssidx, dev_role));

		if (bssidx == 0) {
				

			if (is_rsdb_supported) {
				if ((err = wl_cfg80211_add_del_bss(cfg, dev, bssidx,
					NL80211_IFTYPE_AP, 0, NULL)) < 0) {
					WL_ERR(("wl add_del_bss returned error:%d\n", err));
					goto exit;
				}
			} else if (is_rsdb_supported == 0) {
			
				err = wldev_ioctl(dev, WLC_DOWN, &ap, sizeof(s32), true);
				if (err < 0) {
					WL_ERR(("WLC_DOWN error %d\n", err));
					goto exit;
				}
				err = wldev_iovar_setint(dev, "apsta", 0);
				if (err < 0) {
					WL_ERR(("wl apsta 0 error %d\n", err));
					goto exit;
				}

				if ((err = wldev_ioctl(dev,
					WLC_SET_AP, &ap, sizeof(s32), true)) < 0) {
					WL_ERR(("setting AP mode failed %d \n", err));
					goto exit;
				}

			}

			pm = 0;
			if ((err = wldev_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), true)) != 0) {
				WL_ERR(("wl PM 0 returned error:%d\n", err));
				goto exit;
			}

			err = wldev_ioctl(dev, WLC_SET_INFRA, &infra, sizeof(s32), true);
			if (err < 0) {
				WL_ERR(("SET INFRA error %d\n", err));
				goto exit;
			}
		} else if (cfg->cfgdev_bssidx && (bssidx == cfg->cfgdev_bssidx)) {

			WL_DBG(("Bringup SoftAP on virtual Interface bssidx:%d \n", bssidx));

			if ((err = wl_cfg80211_add_del_bss(cfg, dev,
				bssidx, NL80211_IFTYPE_AP, 0, NULL)) < 0) {
				WL_ERR(("wl bss ap returned error:%d\n", err));
				goto exit;
			}

		}
#ifdef DISABLE_11H_SOFTAP
		err = wldev_ioctl(dev, WLC_SET_SPECT_MANAGMENT,
			&spect, sizeof(s32), true);
		if (err < 0) {
			WL_ERR(("SET SPECT_MANAGMENT error %d\n", err));
			goto exit;
		}
#endif 

		err = wldev_ioctl(dev, WLC_UP, &ap, sizeof(s32), true);
		if (unlikely(err)) {
			WL_ERR(("WLC_UP error (%d)\n", err));
			goto exit;
		}

		err = wldev_iovar_getint(dev, "wsec", (s32 *)&wsec);
		if (unlikely(err)) {
			WL_ERR(("Could not get wsec %d\n", err));
			goto exit;
		}
		if ((wsec == WEP_ENABLED) && cfg->wep_key.len) {
			WL_DBG(("Applying buffered WEP KEY \n"));
			err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &cfg->wep_key,
				sizeof(struct wl_wsec_key), cfg->ioctl_buf,
				WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
			
			memset(&cfg->wep_key, 0, sizeof(struct wl_wsec_key));
			if (unlikely(err)) {
				WL_ERR(("WLC_SET_KEY error (%d)\n", err));
				goto exit;
			}
		}

		memset(&join_params, 0, sizeof(join_params));
		
		join_params_size = sizeof(join_params.ssid);
		memcpy(join_params.ssid.SSID, cfg->hostapd_ssid.SSID,
			cfg->hostapd_ssid.SSID_len);
		join_params.ssid.SSID_len = htod32(cfg->hostapd_ssid.SSID_len);

		
		if ((err = wldev_ioctl(dev, WLC_SET_SSID, &join_params,
			join_params_size, true)) == 0) {
			WL_DBG(("SoftAP set SSID (%s) success\n", join_params.ssid.SSID));
			wl_clr_drv_status(cfg, AP_CREATING, dev);
			wl_set_drv_status(cfg, AP_CREATED, dev);
		} else
			WL_ERR(("SoftAP/GO set ssid failed! \n"));

		if (bssidx != 0) {
			
			if ((err = wl_cfgp2p_bss(cfg, dev, bssidx, 1)) < 0) {
				WL_ERR(("GO Bring up error %d\n", err));
				goto exit;
			}
		}
	}


exit:
	if (cfg->wep_key.len)
		memset(&cfg->wep_key, 0, sizeof(struct wl_wsec_key));
	return err;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || defined(WL_COMPAT_WIRELESS)
s32
wl_cfg80211_parse_ap_ies(
	struct net_device *dev,
	struct cfg80211_beacon_data *info,
	struct parsed_ies *ies)
{
	struct parsed_ies prb_ies;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	u8 *vndr = NULL;
	u32 vndr_ie_len = 0;
	s32 err = BCME_OK;

	
	if (wl_cfg80211_parse_ies((u8 *)info->tail,
		info->tail_len, ies) < 0) {
		WL_ERR(("Beacon get IEs failed \n"));
		err = -EINVAL;
		goto fail;
	}

#ifdef CUSTOMER_HW_ONE
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		
		
		bcm_tlv_t *max_assoc_ie;
		int max_assoc = MAX_ASSOC_NUMBER;
		s32 ie_offset = DOT11_MGMT_HDR_LEN + DOT11_BCN_PRB_FIXED_LEN;

		max_assoc_ie = bcm_parse_tlvs((u8 *)&info->head[ie_offset],
			info->head_len - ie_offset, DOT11_MNG_VS_ID + 1);
		if (max_assoc_ie != NULL) {
			max_assoc = max_assoc_ie->data[0];
			WL_ERR(("%s: got max_num_sta, max_assoc=%d\n", __func__, max_assoc));
		}
		err = wldev_iovar_setint(dev, "maxassoc", max_assoc);
		if (err)
			WL_ERR(("%s fail to set maxassoc, err=(%d)\n", __func__, err));
	}
#endif 

	vndr = (u8 *)info->proberesp_ies;
	vndr_ie_len = info->proberesp_ies_len;

	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		
		struct ieee80211_mgmt *mgmt;
		mgmt = (struct ieee80211_mgmt *)info->probe_resp;
		if (mgmt != NULL) {
			vndr = (u8 *)&mgmt->u.probe_resp.variable;
			vndr_ie_len = info->probe_resp_len -
				offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
		}
	}

	
	if (wl_cfg80211_parse_ies(vndr, vndr_ie_len, &prb_ies) < 0) {
		WL_ERR(("PROBE RESP get IEs failed \n"));
		err = -EINVAL;
	}

fail:

	return err;
}

s32
wl_cfg80211_set_ies(
	struct net_device *dev,
	struct cfg80211_beacon_data *info,
	s32 bssidx)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	u8 *vndr = NULL;
	u32 vndr_ie_len = 0;
	s32 err = BCME_OK;

	
	if ((err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_BEACON_FLAG, (const u8 *)info->tail,
		info->tail_len)) < 0) {
		WL_ERR(("Set Beacon IE Failed \n"));
	} else {
		WL_DBG(("Applied Vndr IEs for Beacon \n"));
	}

	vndr = (u8 *)info->proberesp_ies;
	vndr_ie_len = info->proberesp_ies_len;

	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		
		struct ieee80211_mgmt *mgmt;
		mgmt = (struct ieee80211_mgmt *)info->probe_resp;
		if (mgmt != NULL) {
			vndr = (u8 *)&mgmt->u.probe_resp.variable;
			vndr_ie_len = info->probe_resp_len -
				offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
		}
	}

	
	if ((err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_PRBRSP_FLAG, vndr, vndr_ie_len)) < 0) {
		WL_ERR(("Set Probe Resp IE Failed \n"));
	} else {
		WL_DBG(("Applied Vndr IEs for Probe Resp \n"));
	}

	return err;
}
#endif 

static s32 wl_cfg80211_hostapd_sec(
	struct net_device *dev,
	struct parsed_ies *ies,
	s32 bssidx)
{
	bool update_bss = 0;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	wl_cfgbss_t *bss = wl_get_cfgbss_by_wdev(cfg, dev->ieee80211_ptr);

	if (!bss) {
		WL_ERR(("cfgbss is NULL \n"));
		return -EINVAL;
	}

	if (ies->wps_ie) {
		if (bss->wps_ie &&
			memcmp(bss->wps_ie, ies->wps_ie, ies->wps_ie_len)) {
			WL_DBG((" WPS IE is changed\n"));
			kfree(bss->wps_ie);
			bss->wps_ie = kmemdup(ies->wps_ie, ies->wps_ie_len, GFP_KERNEL);
		} else if (bss->wps_ie == NULL) {
			WL_DBG((" WPS IE is added\n"));
			bss->wps_ie = kmemdup(ies->wps_ie, ies->wps_ie_len, GFP_KERNEL);
		}

#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
		if (ies->wpa_ie != NULL && ies->wpa2_ie != NULL) {
			WL_ERR(("update bss - wpa_ie and  wpa2_ie is not null\n"));
			if (!bss->security_mode) {
				
				update_bss = true;
				bss->wpa_ie =
					kmemdup(ies->wpa_ie,
					ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
				bss->rsn_ie =
					kmemdup(ies->wpa2_ie,
					ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
			} else {
				
				if (bss->wpa_ie) {
					if (memcmp(bss->wpa_ie,
					ies->wpa_ie, ies->wpa_ie->length +
					WPA_RSN_IE_TAG_FIXED_LEN)) {
						kfree(bss->wpa_ie);
						update_bss = true;
						bss->wpa_ie = kmemdup(ies->wpa_ie,
						ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
						GFP_KERNEL);
					}
				}
				else {
					update_bss = true;
					bss->wpa_ie =
						kmemdup(ies->wpa_ie,
						ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
						GFP_KERNEL);
				}
				if (bss->rsn_ie) {
					if (memcmp(bss->rsn_ie,
					ies->wpa2_ie,
					ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN)) {
						update_bss = true;
						kfree(bss->rsn_ie);
						bss->rsn_ie =
							kmemdup(ies->wpa2_ie,
							ies->wpa2_ie->len +
							WPA_RSN_IE_TAG_FIXED_LEN,
							GFP_KERNEL);
					}
				}
				else {
					update_bss = true;
					bss->rsn_ie =
						kmemdup(ies->wpa2_ie,
						ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN,
						GFP_KERNEL);
				}
			}
			WL_ERR(("update_bss=%d\n", update_bss));
			if (update_bss) {
				bss->security_mode = true;
				wl_cfgp2p_bss(cfg, dev, bssidx, 0);
				if (wl_validate_wpaie_wpa2ie(dev, ies->wpa_ie,
					ies->wpa2_ie, bssidx)  < 0) {
					return BCME_ERROR;
				}
				wl_cfgp2p_bss(cfg, dev, bssidx, 1);
			}

		}
		else
#endif 
		if ((ies->wpa_ie != NULL || ies->wpa2_ie != NULL)) {
			if (!bss->security_mode) {
				
				update_bss = true;
				if (ies->wpa_ie != NULL) {
					bss->wpa_ie = kmemdup(ies->wpa_ie,
					ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
				} else {
					bss->rsn_ie = kmemdup(ies->wpa2_ie,
					ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
				}
			} else if (bss->wpa_ie) {
				
				if (ies->wpa_ie != NULL) {
					update_bss = true;
					kfree(bss->rsn_ie);
					bss->rsn_ie = NULL;
					bss->wpa_ie = kmemdup(ies->wpa_ie,
					ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
				} else if (memcmp(bss->rsn_ie,
					ies->wpa2_ie, ies->wpa2_ie->len
					+ WPA_RSN_IE_TAG_FIXED_LEN)) {
					update_bss = true;
					kfree(bss->rsn_ie);
					bss->rsn_ie = kmemdup(ies->wpa2_ie,
					ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
					bss->wpa_ie = NULL;
				}
			}
			if (update_bss) {
				bss->security_mode = true;
				wl_cfgp2p_bss(cfg, dev, bssidx, 0);
				if (wl_validate_wpa2ie(dev, ies->wpa2_ie, bssidx)  < 0 ||
					wl_validate_wpaie(dev, ies->wpa_ie, bssidx) < 0) {
					return BCME_ERROR;
				}
				wl_cfgp2p_bss(cfg, dev, bssidx, 1);
			}
		}
	} else {
		WL_ERR(("No WPSIE in beacon \n"));
	}
	return 0;
}

#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	2, 0))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32
wl_cfg80211_del_station(
	struct wiphy *wiphy,
	struct net_device *ndev,
	const u8* mac_addr)
#else
static s32
wl_cfg80211_del_station(
	struct wiphy *wiphy,
	struct net_device *ndev,
	u8* mac_addr)
#endif
{
	struct net_device *dev;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	scb_val_t scb_val;
	s8 eabuf[ETHER_ADDR_STR_LEN];
	int err;
	char mac_buf[MAX_NUM_OF_ASSOCIATED_DEV *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};
	struct maclist *assoc_maclist = (struct maclist *)mac_buf;
	int num_associated = 0;

	WL_DBG(("Entry\n"));
	if (mac_addr == NULL) {
		WL_DBG(("mac_addr is NULL ignore it\n"));
		return 0;
	}

	dev = ndev_to_wlc_ndev(ndev, cfg);

	if (p2p_is_on(cfg)) {
		if ((wl_cfgp2p_discover_enable_search(cfg, false)) < 0) {
			WL_ERR(("Can not disable discovery mode\n"));
			return -EFAULT;
		}
	}

	assoc_maclist->count = MAX_NUM_OF_ASSOCIATED_DEV;
	err = wldev_ioctl(ndev, WLC_GET_ASSOCLIST,
		assoc_maclist, sizeof(mac_buf), false);
	if (err < 0)
		WL_ERR(("WLC_GET_ASSOCLIST error %d\n", err));
	else
		num_associated = assoc_maclist->count;

	memcpy(scb_val.ea.octet, mac_addr, ETHER_ADDR_LEN);
	scb_val.val = DOT11_RC_DEAUTH_LEAVING;
	err = wldev_ioctl(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scb_val,
		sizeof(scb_val_t), true);
	if (err < 0)
		WL_ERR(("WLC_SCB_DEAUTHENTICATE_FOR_REASON err %d\n", err));
	WL_ERR(("Disconnect STA : %s scb_val.val %d\n",
		bcm_ether_ntoa((const struct ether_addr *)mac_addr, eabuf),
		scb_val.val));

	if (num_associated > 0 && ETHER_ISBCAST(mac_addr))
		wl_delay(400);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32
wl_cfg80211_change_station(
	struct wiphy *wiphy,
	struct net_device *dev,
	const u8 *mac,
	struct station_parameters *params)
#else
static s32
wl_cfg80211_change_station(
	struct wiphy *wiphy,
	struct net_device *dev,
	u8 *mac,
	struct station_parameters *params)
#endif
{
	int err;
#ifdef DHD_LOSSLESS_ROAMING
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#endif

	WL_DBG(("SCB_AUTHORIZE mac_addr:"MACDBG" sta_flags_mask:0x%x "
				"sta_flags_set:0x%x iface:%s \n", MAC2STRDBG(mac),
				params->sta_flags_mask, params->sta_flags_set, dev->name));

	
	if (!(params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED))) {
		WL_ERR(("WLC_SCB_AUTHORIZE sta_flags_mask not set \n"));
		return -ENOTSUPP;
	}

	if (!(params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED))) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
		err = wldev_ioctl(dev, WLC_SCB_DEAUTHORIZE, (u8 *)mac, ETH_ALEN, true);
#else
		err = wldev_ioctl(dev, WLC_SCB_DEAUTHORIZE, mac, ETH_ALEN, true);
#endif
		if (err)
			WL_ERR(("WLC_SCB_DEAUTHORIZE error (%d)\n", err));
		return err;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	err = wldev_ioctl(dev, WLC_SCB_AUTHORIZE, (u8 *)mac, ETH_ALEN, true);
#else
	err = wldev_ioctl(dev, WLC_SCB_AUTHORIZE, mac, ETH_ALEN, true);
#endif
	if (err)
		WL_ERR(("WLC_SCB_AUTHORIZE error (%d)\n", err));
#ifdef DHD_LOSSLESS_ROAMING
	wl_del_roam_timeout(cfg);
#endif
	return err;
}
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || defined(WL_COMPAT_WIRELESS)

static s32
wl_cfg80211_start_ap(
	struct wiphy *wiphy,
	struct net_device *dev,
	struct cfg80211_ap_settings *info)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = BCME_OK;
	struct parsed_ies ies;
	s32 bssidx = 0;
	u32 dev_role = 0;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("Enter \n"));
	if ((dev == bcmcfg_to_prmry_ndev(cfg)) ||
		(dev == ((struct net_device *)cfgdev_to_ndev(cfg->bss_cfgdev)))) {
		WL_DBG(("Start AP req on iface: %s \n", dev->name));
		dev_role = NL80211_IFTYPE_AP;
	}
#if defined(WL_ENABLE_P2P_IF)
	else if (dev == cfg->p2p_net) {
		
		WL_DBG(("Start AP req on P2P iface: GO\n"));
		dev = bcmcfg_to_prmry_ndev(cfg);
		dev_role = NL80211_IFTYPE_P2P_GO;
	}
#endif 

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (p2p_is_on(cfg) && (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO)) {
		dev_role = NL80211_IFTYPE_P2P_GO;
	} else if (dev_role == NL80211_IFTYPE_AP) {
		dhd->op_mode |= DHD_FLAG_HOSTAP_MODE;
	} else {
		
		err = -EINVAL;
		goto fail;
	}

	if (!check_dev_role_integrity(cfg, dev_role)) {
		err = -EINVAL;
		goto fail;
	}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) && !defined(WL_COMPAT_WIRELESS))
	if ((err = wl_cfg80211_set_channel(wiphy, dev,
		dev->ieee80211_ptr->preset_chandef.chan,
		NL80211_CHAN_HT20) < 0)) {
		WL_ERR(("Set channel failed \n"));
		goto fail;
	}
#endif 

	if ((err = wl_cfg80211_bcn_set_params(info, dev,
		dev_role, bssidx)) < 0) {
		WL_ERR(("Beacon params set failed \n"));
		goto fail;
	}

	
	if ((err = wl_cfg80211_parse_ap_ies(dev, &info->beacon, &ies)) < 0) {
		WL_ERR(("Set IEs failed \n"));
		goto fail;
	}

	if ((err = wl_cfg80211_bcn_validate_sec(dev, &ies,
		dev_role, bssidx, info->privacy)) < 0)
	{
		WL_ERR(("Beacon set security failed \n"));
		goto fail;
	}

	if ((err = wl_cfg80211_bcn_bringup_ap(dev, &ies,
		dev_role, bssidx)) < 0) {
		WL_ERR(("Beacon bring up AP/GO failed \n"));
		goto fail;
	}

#if defined(CUSTOMER_HW_ONE) && defined(SUPPORT_2G_HT40)
	if (dev_role == NL80211_IFTYPE_AP) {
		wldev_iovar_setint(dev, "obss_coex", 1);
		WL_ERR(("enable obss coex mode \n"));
	}
#endif 

#ifdef CUSTOMER_HW_ONE
	if (dev_role == NL80211_IFTYPE_AP) {
		int retry_cnt = 15;
		err = wl_set_retry(dev, retry_cnt, false);
		if (err != BCME_OK) {
			printf("%s set srl failed %d \n", __FUNCTION__, err);
		}

		err = wl_set_retry(dev, retry_cnt, true);
		if (err != BCME_OK) {
			printf("%s set lrl failed %d \n", __FUNCTION__, err);
		}
	}
#endif 

	WL_DBG(("** AP/GO Created **\n"));

#ifdef WL_CFG80211_ACL
	
	if ((err = wl_cfg80211_set_mac_acl(wiphy, dev, info->acl)) < 0) {
		WL_ERR(("Set ACL failed\n"));
	}
#endif 

	
	if ((err = wl_cfg80211_set_ies(dev, &info->beacon, bssidx)) < 0)
		WL_ERR(("Set IEs failed \n"));

	
	if ((dev_role == NL80211_IFTYPE_AP) && (ies.wps_ie != NULL)) {
		bool pbc = 0;
		wl_validate_wps_ie((char *) ies.wps_ie, ies.wps_ie_len, &pbc);
		if (pbc) {
			WL_DBG(("set WLC_E_PROBREQ_MSG\n"));
			wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, true);
		}
	}

fail:
	if (err) {
		WL_ERR(("ADD/SET beacon failed\n"));
		wldev_iovar_setint(dev, "mpc", 1);
		if (dev_role == NL80211_IFTYPE_AP) {
			dhd->op_mode &= ~DHD_FLAG_HOSTAP_MODE;
		}
	}

	return err;
}

static s32
wl_cfg80211_stop_ap(
	struct wiphy *wiphy,
	struct net_device *dev)
{
	int err = 0;
	u32 dev_role = 0;
	int infra = 0;
	int ap = 0;
	s32 bssidx = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 is_rsdb_supported = BCME_ERROR;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("Enter \n"));

	is_rsdb_supported = DHD_OPMODE_SUPPORTED(cfg->pub, DHD_FLAG_RSDB_MODE);
	if (is_rsdb_supported < 0)
		return (-ENODEV);
	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
		dev_role = NL80211_IFTYPE_AP;
		WL_DBG(("stopping AP operation\n"));
	} else if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) {
		dev_role = NL80211_IFTYPE_P2P_GO;
		WL_DBG(("stopping P2P GO operation\n"));
	} else {
		WL_ERR(("no AP/P2P GO interface is operational.\n"));
		return -EINVAL;
	}

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (!check_dev_role_integrity(cfg, dev_role)) {
		WL_ERR(("role integrity check failed \n"));
		err = -EINVAL;
		goto exit;
	}

	if ((err = wl_cfgp2p_bss(cfg, dev, bssidx, 0)) < 0) {
		WL_ERR(("bss down error %d\n", err));
	}

	if (dev_role == NL80211_IFTYPE_AP) {
		if (is_rsdb_supported) {
			if ((err = wl_cfg80211_add_del_bss(cfg, dev,
				bssidx, NL80211_IFTYPE_STATION, 0, NULL)) < 0) {
				if ((err = wldev_ioctl(dev, WLC_SET_AP, &ap, sizeof(s32),
					true)) < 0) {
					WL_ERR(("setting AP mode failed %d \n", err));
					err = -ENOTSUPP;
					goto exit;
				}
			}
		} else if (is_rsdb_supported == 0) {
			err = wldev_ioctl(dev, WLC_SET_INFRA, &infra, sizeof(s32), true);
			if (err < 0) {
				WL_ERR(("SET INFRA error %d\n", err));
				err = -ENOTSUPP;
				goto exit;
			}
			err = wldev_ioctl(dev, WLC_UP, &ap, sizeof(s32), true);
			if (unlikely(err)) {
				WL_ERR(("WLC_UP error (%d)\n", err));
				err = -EINVAL;
				goto exit;
			}
		}

		wl_clr_drv_status(cfg, AP_CREATED, dev);
		
		wldev_iovar_setint(dev, "mpc", 1);

		 wl_cfg80211_clear_per_bss_ies(cfg, bssidx);
	} else {
		WL_DBG(("Stopping P2P GO \n"));
		DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE((dhd_pub_t *)(cfg->pub),
			DHD_EVENT_TIMEOUT_MS*3);
		DHD_OS_WAKE_LOCK_TIMEOUT((dhd_pub_t *)(cfg->pub));
	}

exit:

	if (dev_role == NL80211_IFTYPE_AP) {
		
		dhd->op_mode &= ~DHD_FLAG_HOSTAP_MODE;
	}
	return err;
}

static s32
wl_cfg80211_change_beacon(
	struct wiphy *wiphy,
	struct net_device *dev,
	struct cfg80211_beacon_data *info)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct parsed_ies ies;
	u32 dev_role = 0;
	s32 bssidx = 0;
	bool pbc = 0;

	WL_DBG(("Enter \n"));

	if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		dev_role = NL80211_IFTYPE_AP;
	}
#if defined(WL_ENABLE_P2P_IF)
	else if (dev == cfg->p2p_net) {
		
		dev = bcmcfg_to_prmry_ndev(cfg);
		dev_role = NL80211_IFTYPE_P2P_GO;
	}
#endif 

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) {
		dev_role = NL80211_IFTYPE_P2P_GO;
	}

	if (!check_dev_role_integrity(cfg, dev_role)) {
		err = -EINVAL;
		goto fail;
	}

	if ((dev_role == NL80211_IFTYPE_P2P_GO) && (cfg->p2p_wdev == NULL)) {
		WL_ERR(("P2P already down status!\n"));
		err = BCME_ERROR;
		goto fail;
	}

	
	if ((err = wl_cfg80211_parse_ap_ies(dev, info, &ies)) < 0) {
		WL_ERR(("Parse IEs failed \n"));
		goto fail;
	}

	
	if ((err = wl_cfg80211_set_ies(dev, info, bssidx)) < 0) {
		WL_ERR(("Set IEs failed \n"));
		goto fail;
	}

	if (dev_role == NL80211_IFTYPE_AP) {
		if (wl_cfg80211_hostapd_sec(dev, &ies, bssidx) < 0) {
			WL_ERR(("Hostapd update sec failed \n"));
			err = -EINVAL;
			goto fail;
		}
		
		if ((dev_role == NL80211_IFTYPE_AP) && (ies.wps_ie != NULL)) {
			wl_validate_wps_ie((char *) ies.wps_ie, ies.wps_ie_len, &pbc);
			WL_DBG((" WPS AP, wps_ie is exists pbc=%d\n", pbc));
			if (pbc)
				wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, true);
			else
				wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, false);
		}
	}

fail:
	return err;
}
#else
static s32
wl_cfg80211_add_set_beacon(struct wiphy *wiphy, struct net_device *dev,
	struct beacon_parameters *info)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 ie_offset = 0;
	s32 bssidx = 0;
	u32 dev_role = NL80211_IFTYPE_AP;
	struct parsed_ies ies;
	bcm_tlv_t *ssid_ie;
	bool pbc = 0;
	bool privacy;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("interval (%d) dtim_period (%d) head_len (%d) tail_len (%d)\n",
		info->interval, info->dtim_period, info->head_len, info->tail_len));

	if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		dev_role = NL80211_IFTYPE_AP;
	}
#if defined(WL_ENABLE_P2P_IF)
	else if (dev == cfg->p2p_net) {
		
		dev = bcmcfg_to_prmry_ndev(cfg);
		dev_role = NL80211_IFTYPE_P2P_GO;
	}
#endif 

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) {
		dev_role = NL80211_IFTYPE_P2P_GO;
	} else if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
		dhd->op_mode |= DHD_FLAG_HOSTAP_MODE;
	}

	if (!check_dev_role_integrity(cfg, dev_role)) {
		err = -ENODEV;
		goto fail;
	}

	if ((dev_role == NL80211_IFTYPE_P2P_GO) && (cfg->p2p_wdev == NULL)) {
		WL_ERR(("P2P already down status!\n"));
		err = BCME_ERROR;
		goto fail;
	}

	ie_offset = DOT11_MGMT_HDR_LEN + DOT11_BCN_PRB_FIXED_LEN;
	
	if ((ssid_ie = bcm_parse_tlvs((u8 *)&info->head[ie_offset],
		info->head_len - ie_offset,
		DOT11_MNG_SSID_ID)) != NULL) {
		if (dev_role == NL80211_IFTYPE_AP) {
			
			memset(&cfg->hostapd_ssid.SSID[0], 0x00, 32);
			memcpy(&cfg->hostapd_ssid.SSID[0], ssid_ie->data, ssid_ie->len);
			cfg->hostapd_ssid.SSID_len = ssid_ie->len;
		} else {
				
			memset(&cfg->p2p->ssid.SSID[0], 0x00, 32);
			memcpy(cfg->p2p->ssid.SSID, ssid_ie->data, ssid_ie->len);
			cfg->p2p->ssid.SSID_len = ssid_ie->len;
		}
	}

	if (wl_cfg80211_parse_ies((u8 *)info->tail,
		info->tail_len, &ies) < 0) {
		WL_ERR(("Beacon get IEs failed \n"));
		err = -EINVAL;
		goto fail;
	}

	if ((err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_BEACON_FLAG, (u8 *)info->tail,
		info->tail_len)) < 0) {
		WL_ERR(("Beacon set IEs failed \n"));
		goto fail;
	} else {
		WL_DBG(("Applied Vndr IEs for Beacon \n"));
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	if ((err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_PRBRSP_FLAG, (u8 *)info->proberesp_ies,
		info->proberesp_ies_len)) < 0) {
		WL_ERR(("ProbeRsp set IEs failed \n"));
		goto fail;
	} else {
		WL_DBG(("Applied Vndr IEs for ProbeRsp \n"));
	}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	privacy = info->privacy;
#else
	privacy = 0;
#endif
	if (!wl_cfgp2p_bss_isup(dev, bssidx) &&
		(wl_cfg80211_bcn_validate_sec(dev, &ies, dev_role, bssidx, privacy) < 0))
	{
		WL_ERR(("Beacon set security failed \n"));
		err = -EINVAL;
		goto fail;
	}

	
	if (info->interval) {
		if ((err = wldev_ioctl(dev, WLC_SET_BCNPRD,
			&info->interval, sizeof(s32), true)) < 0) {
			WL_ERR(("Beacon Interval Set Error, %d\n", err));
			return err;
		}
	}
	if (info->dtim_period) {
		if ((err = wldev_ioctl(dev, WLC_SET_DTIMPRD,
			&info->dtim_period, sizeof(s32), true)) < 0) {
			WL_ERR(("DTIM Interval Set Error, %d\n", err));
			return err;
		}
	}

	if ((err = wl_cfg80211_bcn_bringup_ap(dev, &ies, dev_role, bssidx)) < 0) {
		WL_ERR(("Beacon bring up AP/GO failed \n"));
		goto fail;
	}

	if (wl_get_drv_status(cfg, AP_CREATED, dev)) {
		
		if (wl_cfg80211_hostapd_sec(dev, &ies, bssidx) < 0) {
			WL_ERR(("Hostapd update sec failed \n"));
			err = -EINVAL;
			goto fail;
		}
	}

	
	if (((dev_role == NL80211_IFTYPE_P2P_GO) ||
		(dev_role == NL80211_IFTYPE_AP)) && (ies.wps_ie != NULL)) {
		wl_validate_wps_ie((char *) ies.wps_ie, ies.wps_ie_len, &pbc);
		if (pbc)
			wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, true);
	}

	WL_DBG(("** ADD/SET beacon done **\n"));

fail:
	if (err) {
		WL_ERR(("ADD/SET beacon failed\n"));
		wldev_iovar_setint(dev, "mpc", 1);
		if (dev_role == NL80211_IFTYPE_AP) {
			
			dhd->op_mode &= ~DHD_FLAG_HOSTAP_MODE;
		}
	}
	return err;

}
#endif 

#ifdef WL_SCHED_SCAN
#ifdef CUSTOMER_HW_ONE
#define PNO_TIME		75
#else
#define PNO_TIME		30
#endif 
#define PNO_REPEAT		4
#define PNO_FREQ_EXPO_MAX	2
static bool
is_ssid_in_list(struct cfg80211_ssid *ssid, struct cfg80211_ssid *ssid_list, int count)
{
	int i;

	if (!ssid || !ssid_list)
		return FALSE;

	for (i = 0; i < count; i++) {
		if (ssid->ssid_len == ssid_list[i].ssid_len) {
			if (strncmp(ssid->ssid, ssid_list[i].ssid, ssid->ssid_len) == 0)
				return TRUE;
		}
	}
	return FALSE;
}

static int
wl_cfg80211_sched_scan_start(struct wiphy *wiphy,
                             struct net_device *dev,
                             struct cfg80211_sched_scan_request *request)
{
	ushort pno_time = PNO_TIME;
	int pno_repeat = PNO_REPEAT;
	int pno_freq_expo_max = PNO_FREQ_EXPO_MAX;
	wlc_ssid_ext_t ssids_local[MAX_PFN_LIST_COUNT];
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct cfg80211_ssid *ssid = NULL;
	struct cfg80211_ssid *hidden_ssid_list = NULL;
	int ssid_cnt = 0;
	int i;
	int ret = 0;

	if (!request) {
		WL_ERR(("Sched scan request was NULL\n"));
		return -EINVAL;
	}

	WL_DBG(("Enter \n"));
	WL_PNO((">>> SCHED SCAN START\n"));
	WL_PNO(("Enter n_match_sets:%d   n_ssids:%d \n",
		request->n_match_sets, request->n_ssids));
	WL_PNO(("ssids:%d pno_time:%d pno_repeat:%d pno_freq:%d \n",
		request->n_ssids, pno_time, pno_repeat, pno_freq_expo_max));


	if (!request->n_ssids || !request->n_match_sets) {
		WL_ERR(("Invalid sched scan req!! n_ssids:%d \n", request->n_ssids));
		return -EINVAL;
	}

	memset(&ssids_local, 0, sizeof(ssids_local));

	if (request->n_ssids > 0) {
		hidden_ssid_list = request->ssids;
	}

	for (i = 0; i < request->n_match_sets && ssid_cnt < MAX_PFN_LIST_COUNT; i++) {
		ssid = &request->match_sets[i].ssid;
		
		if (ssid->ssid_len) {
			memcpy(ssids_local[ssid_cnt].SSID, ssid->ssid, ssid->ssid_len);
			ssids_local[ssid_cnt].SSID_len = ssid->ssid_len;
			if (is_ssid_in_list(ssid, hidden_ssid_list, request->n_ssids)) {
				ssids_local[ssid_cnt].hidden = TRUE;
				WL_PNO((">>> PNO hidden SSID (%s) \n", ssid->ssid));
			} else {
				ssids_local[ssid_cnt].hidden = FALSE;
				WL_PNO((">>> PNO non-hidden SSID (%s) \n", ssid->ssid));
			}
			if (request->match_sets[i].rssi_thold != NL80211_SCAN_RSSI_THOLD_OFF) {
				ssids_local[ssid_cnt].rssi_thresh =
				      (int8)request->match_sets[i].rssi_thold;
			}
			ssid_cnt++;
		}
	}

	if (ssid_cnt) {
		if ((ret = dhd_dev_pno_set_for_ssid(dev, ssids_local, ssid_cnt,
			pno_time, pno_repeat, pno_freq_expo_max, NULL, 0)) < 0) {
			WL_ERR(("PNO setup failed!! ret=%d \n", ret));
			return -EINVAL;
		}
		cfg->sched_scan_req = request;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int
wl_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

	WL_DBG(("Enter \n"));
	WL_PNO((">>> SCHED SCAN STOP\n"));

	if (dhd_dev_pno_stop_for_ssid(dev) < 0)
		WL_ERR(("PNO Stop for SSID failed"));

	if (cfg->scan_request && cfg->sched_scan_running) {
		WL_PNO((">>> Sched scan running. Aborting it..\n"));
		wl_notify_escan_complete(cfg, dev, true, true);
	}

	 cfg->sched_scan_req = NULL;
	 cfg->sched_scan_running = FALSE;

	return 0;
}
#endif 

#ifdef WL_SUPPORT_ACS
static int wl_parse_dump_obss(char *buf, struct wl_dump_survey *survey)
{
	int i;
	char *token;
	char delim[] = " \n";

	token = strsep(&buf, delim);
	while (token != NULL) {
		if (!strcmp(token, "OBSS")) {
			for (i = 0; i < OBSS_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->obss = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "IBSS")) {
			for (i = 0; i < IBSS_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->ibss = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "TXDur")) {
			for (i = 0; i < TX_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->tx = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "Category")) {
			for (i = 0; i < CTG_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->no_ctg = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "Packet")) {
			for (i = 0; i < PKT_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->no_pckt = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "Opp(time):")) {
			for (i = 0; i < IDLE_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->idle = simple_strtoul(token, NULL, 10);
		}

		token = strsep(&buf, delim);
	}

	return 0;
}

static int wl_dump_obss(struct net_device *ndev, cca_msrmnt_query req,
	struct wl_dump_survey *survey)
{
	cca_stats_n_flags *results;
	char *buf;
	int retry, err;

	buf = kzalloc(sizeof(char) * WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (unlikely(!buf)) {
		WL_ERR(("%s: buf alloc failed\n", __func__));
		return -ENOMEM;
	}

	retry = IOCTL_RETRY_COUNT;
	while (retry--) {
		err = wldev_iovar_getbuf(ndev, "dump_obss", &req, sizeof(req),
			buf, WLC_IOCTL_MAXLEN, NULL);
		if (err >=  0) {
			break;
		}
		WL_DBG(("attempt = %d, err = %d, \n",
			(IOCTL_RETRY_COUNT - retry), err));
	}

	if (retry <= 0)	{
		WL_ERR(("failure, dump_obss IOVAR failed\n"));
		err = -EINVAL;
		goto exit;
	}

	results = (cca_stats_n_flags *)(buf);
	wl_parse_dump_obss(results->buf, survey);
	kfree(buf);

	return 0;
exit:
	kfree(buf);
	return err;
}

static int wl_cfg80211_dump_survey(struct wiphy *wiphy, struct net_device *ndev,
	int idx, struct survey_info *info)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wl_dump_survey *survey;
	struct ieee80211_supported_band *band;
	struct ieee80211_channel*chan;
	cca_msrmnt_query req;
	int val, err, noise, retry;

	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	if (!(dhd->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		return -ENOENT;
	}
	band = wiphy->bands[IEEE80211_BAND_2GHZ];
	if (band && idx >= band->n_channels) {
		idx -= band->n_channels;
		band = NULL;
	}

	if (!band || idx >= band->n_channels) {
		
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
		if (idx >= band->n_channels) {
			return -ENOENT;
		}
	}

	chan = &band->channels[idx];
	
	if ((err = wl_cfg80211_set_channel(wiphy, ndev, chan,
		NL80211_CHAN_HT20) < 0)) {
		WL_ERR(("Set channel failed \n"));
	}

	if (!idx) {
		
		val = 0;
		err = wldev_iovar_setbuf_bsscfg(ndev, "mpc", (void *)&val,
			sizeof(val), cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0,
			&cfg->ioctl_buf_sync);
		if (err < 0) {
			WL_ERR(("set 'mpc' failed, error = %d\n", err));
		}

		
		val = 1;
		err = wldev_ioctl(ndev, WLC_UP, (void *)&val, sizeof(val), true);
		if (err < 0) {
			WL_ERR(("set interface up failed, error = %d\n", err));
		}
	}

	
	retry = IOCTL_RETRY_COUNT;
	while (retry--) {
		err = wldev_ioctl(ndev, WLC_GET_PHY_NOISE, &noise,
			sizeof(noise), false);
		if (err >=  0) {
			break;
		}
		WL_DBG(("attempt = %d, err = %d, \n",
			(IOCTL_RETRY_COUNT - retry), err));
	}

	if (retry <= 0)	{
		WL_ERR(("Get Phy Noise failed, error = %d\n", err));
		noise = CHAN_NOISE_DUMMY;
	}

	survey = (struct wl_dump_survey *) kzalloc(sizeof(struct wl_dump_survey),
		GFP_KERNEL);
	if (unlikely(!survey)) {
		WL_ERR(("%s: alloc failed\n", __func__));
		return -ENOMEM;
	}

	
	req.msrmnt_query = 0;
	req.time_req = ACS_MSRMNT_DELAY;
	if ((err = wl_dump_obss(ndev, req, survey)) < 0) {
		goto exit;
	}

	msleep(ACS_MSRMNT_DELAY + 10);

	
	req.msrmnt_query = 1;
	if ((err = wl_dump_obss(ndev, req, survey)) < 0) {
		goto exit;
	}

	info->channel = chan;
	info->noise = noise;
	info->channel_time = ACS_MSRMNT_DELAY;
	info->channel_time_busy = ACS_MSRMNT_DELAY - survey->idle;
	info->channel_time_rx = survey->obss + survey->ibss + survey->no_ctg +
		survey->no_pckt;
	info->channel_time_tx = survey->tx;
	info->filled = SURVEY_INFO_NOISE_DBM |SURVEY_INFO_CHANNEL_TIME |
		SURVEY_INFO_CHANNEL_TIME_BUSY |	SURVEY_INFO_CHANNEL_TIME_RX |
		SURVEY_INFO_CHANNEL_TIME_TX;
	kfree(survey);

	return 0;
exit:
	kfree(survey);
	return err;
}
#endif 

static struct cfg80211_ops wl_cfg80211_ops = {
	.add_virtual_intf = wl_cfg80211_add_virtual_iface,
	.del_virtual_intf = wl_cfg80211_del_virtual_iface,
	.change_virtual_intf = wl_cfg80211_change_virtual_iface,
#if defined(WL_CFG80211_P2P_DEV_IF)
	.start_p2p_device = wl_cfgp2p_start_p2p_device,
	.stop_p2p_device = wl_cfgp2p_stop_p2p_device,
#endif 
	.scan = wl_cfg80211_scan,
	.set_wiphy_params = wl_cfg80211_set_wiphy_params,
	.join_ibss = wl_cfg80211_join_ibss,
	.leave_ibss = wl_cfg80211_leave_ibss,
	.get_station = wl_cfg80211_get_station,
	.set_tx_power = wl_cfg80211_set_tx_power,
	.get_tx_power = wl_cfg80211_get_tx_power,
	.add_key = wl_cfg80211_add_key,
	.del_key = wl_cfg80211_del_key,
	.get_key = wl_cfg80211_get_key,
	.set_default_key = wl_cfg80211_config_default_key,
	.set_default_mgmt_key = wl_cfg80211_config_default_mgmt_key,
	.set_power_mgmt = wl_cfg80211_set_power_mgmt,
	.connect = wl_cfg80211_connect,
	.disconnect = wl_cfg80211_disconnect,
	.suspend = wl_cfg80211_suspend,
	.resume = wl_cfg80211_resume,
	.set_pmksa = wl_cfg80211_set_pmksa,
	.del_pmksa = wl_cfg80211_del_pmksa,
	.flush_pmksa = wl_cfg80211_flush_pmksa,
	.remain_on_channel = wl_cfg80211_remain_on_channel,
	.cancel_remain_on_channel = wl_cfg80211_cancel_remain_on_channel,
	.mgmt_tx = wl_cfg80211_mgmt_tx,
	.mgmt_frame_register = wl_cfg80211_mgmt_frame_register,
	.change_bss = wl_cfg80211_change_bss,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)) || defined(WL_COMPAT_WIRELESS)
	.set_channel = wl_cfg80211_set_channel,
#endif 
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)) && !defined(WL_COMPAT_WIRELESS)
	.set_beacon = wl_cfg80211_add_set_beacon,
	.add_beacon = wl_cfg80211_add_set_beacon,
#else
	.change_beacon = wl_cfg80211_change_beacon,
	.start_ap = wl_cfg80211_start_ap,
	.stop_ap = wl_cfg80211_stop_ap,
#endif 
#ifdef WL_SCHED_SCAN
	.sched_scan_start = wl_cfg80211_sched_scan_start,
	.sched_scan_stop = wl_cfg80211_sched_scan_stop,
#endif 
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	2, 0))
	.del_station = wl_cfg80211_del_station,
	.change_station = wl_cfg80211_change_station,
	.mgmt_tx_cancel_wait = wl_cfg80211_mgmt_tx_cancel_wait,
#endif 
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0)) || defined(WL_COMPAT_WIRELESS)
	.tdls_mgmt = wl_cfg80211_tdls_mgmt,
	.tdls_oper = wl_cfg80211_tdls_oper,
#endif 
#ifdef WL_SUPPORT_ACS
	.dump_survey = wl_cfg80211_dump_survey,
#endif 
#ifdef WL_CFG80211_ACL
	.set_mac_acl = wl_cfg80211_set_mac_acl,
#endif 
};

s32 wl_mode_to_nl80211_iftype(s32 mode)
{
	s32 err = 0;

	switch (mode) {
	case WL_MODE_BSS:
		return NL80211_IFTYPE_STATION;
	case WL_MODE_IBSS:
		return NL80211_IFTYPE_ADHOC;
	case WL_MODE_AP:
		return NL80211_IFTYPE_AP;
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}

	return err;
}

#ifdef CONFIG_CFG80211_INTERNAL_REGDB
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
static int
#else
static void
#endif 
wl_cfg80211_reg_notifier(
	struct wiphy *wiphy,
	struct regulatory_request *request)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(wiphy);
	int ret = 0;
	int revinfo = -1;

	if (!request || !cfg) {
		WL_ERR(("Invalid arg\n"));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
		return -EINVAL;
#else
		return;
#endif 
	}

	WL_DBG(("ccode: %c%c Initiator: %d\n",
		request->alpha2[0], request->alpha2[1], request->initiator));

	
	if ((request->initiator != NL80211_REGDOM_SET_BY_USER) &&
		(request->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE)) {
		WL_ERR(("reg_notifier for intiator:%d not supported : set default\n",
			request->initiator));
	}

	WL_ERR(("Set country code %c%c from %s\n",
		request->alpha2[0], request->alpha2[1],
		((request->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE) ? " 11d AP" : "User")));

	if ((ret = wldev_set_country(bcmcfg_to_prmry_ndev(cfg), request->alpha2,
		false, (request->initiator == NL80211_REGDOM_SET_BY_USER ? true : false),
		revinfo)) < 0) {
		WL_ERR(("set country Failed :%d\n", ret));
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
	return ret;
#else
	return;
#endif 
}
#endif 

#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
static const struct wiphy_wowlan_support brcm_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY,
	.n_patterns = WL_WOWLAN_MAX_PATTERNS,
	.pattern_min_len = WL_WOWLAN_MIN_PATTERN_LEN,
	.pattern_max_len = WL_WOWLAN_MAX_PATTERN_LEN,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	.max_pkt_offset = WL_WOWLAN_MAX_PATTERN_LEN,
#endif 
};
#endif 
#endif 

static s32 wl_setup_wiphy(struct wireless_dev *wdev, struct device *sdiofunc_dev, void *context)
{
	s32 err = 0;
#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	struct cfg80211_wowlan *brcm_wowlan_config = NULL;
#endif 
#endif 

#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0) || defined(WL_COMPAT_WIRELESS))
	dhd_pub_t *dhd = (dhd_pub_t *)context;
	BCM_REFERENCE(dhd);

	if (!dhd) {
		WL_ERR(("DHD is NULL!!"));
		err = -ENODEV;
		return err;
	}
#endif 

	wdev->wiphy =
	    wiphy_new(&wl_cfg80211_ops, sizeof(struct bcm_cfg80211));
	if (unlikely(!wdev->wiphy)) {
		WL_ERR(("Couldn not allocate wiphy device\n"));
		err = -ENOMEM;
		return err;
	}
	set_wiphy_dev(wdev->wiphy, sdiofunc_dev);
	wdev->wiphy->max_scan_ie_len = WL_SCAN_IE_LEN_MAX;
	
	wdev->wiphy->max_scan_ssids = WL_SCAN_PARAMS_SSID_MAX;
	wdev->wiphy->max_num_pmkids = WL_NUM_PMKIDS_MAX;
#ifdef WL_SCHED_SCAN
	wdev->wiphy->max_sched_scan_ssids = MAX_PFN_LIST_COUNT;
	wdev->wiphy->max_match_sets = MAX_PFN_LIST_COUNT;
	wdev->wiphy->max_sched_scan_ie_len = WL_SCAN_IE_LEN_MAX;
	wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
#endif 
	wdev->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION)
		| BIT(NL80211_IFTYPE_ADHOC)
#if !defined(WL_ENABLE_P2P_IF) && !defined(WL_CFG80211_P2P_DEV_IF)
		| BIT(NL80211_IFTYPE_MONITOR)
#endif 
#if defined(WL_IFACE_COMB_NUM_CHANNELS) || defined(WL_CFG80211_P2P_DEV_IF)
		| BIT(NL80211_IFTYPE_P2P_CLIENT)
		| BIT(NL80211_IFTYPE_P2P_GO)
#endif 
#if defined(WL_CFG80211_P2P_DEV_IF)
		| BIT(NL80211_IFTYPE_P2P_DEVICE)
#endif 
		| BIT(NL80211_IFTYPE_AP);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
	(defined(WL_IFACE_COMB_NUM_CHANNELS) || defined(WL_CFG80211_P2P_DEV_IF))
	WL_DBG(("Setting interface combinations for common mode\n"));
	wdev->wiphy->iface_combinations = common_iface_combinations;
	wdev->wiphy->n_iface_combinations =
		ARRAY_SIZE(common_iface_combinations);
#endif 

	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &__wl_band_2ghz;

	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wdev->wiphy->cipher_suites = __wl_cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(__wl_cipher_suites);
	wdev->wiphy->max_remain_on_channel_duration = 5000;
	wdev->wiphy->mgmt_stypes = wl_cfg80211_default_mgmt_stypes;
#ifndef WL_POWERSAVE_DISABLED
	wdev->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
#else
	wdev->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif				
	wdev->wiphy->flags |= WIPHY_FLAG_NETNS_OK |
		WIPHY_FLAG_4ADDR_AP |
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 39)) && !defined(WL_COMPAT_WIRELESS)
		WIPHY_FLAG_SUPPORTS_SEPARATE_DEFAULT_KEYS |
#endif
		WIPHY_FLAG_4ADDR_STATION;
#if (defined(ROAM_ENABLE) || defined(BCMFW_ROAM_ENABLE)) && ((LINUX_VERSION_CODE >= \
	KERNEL_VERSION(3, 2, 0)) || defined(WL_COMPAT_WIRELESS)) && !0
	wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_FW_ROAM;
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)) || defined(WL_COMPAT_WIRELESS)
	wdev->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
		WIPHY_FLAG_OFFCHAN_TX;
#endif
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	4, 0))
	wdev->wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;

#ifdef WL_CFG80211_ACL
	
	wdev->wiphy->max_acl_mac_addrs = MAX_NUM_MAC_FILT;
#endif

#if 1 && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0) || defined(WL_COMPAT_WIRELESS))
	if (dhd_get_fw_mode(dhd->info) == DHD_FLAG_HOSTAP_MODE) {
		wdev->wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;
		wdev->wiphy->probe_resp_offload = 0;
	}
#endif 
#endif 

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0)) || defined(WL_COMPAT_WIRELESS)
	wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
#endif

#if defined(CONFIG_PM) && defined(WL_CFG80211_P2P_DEV_IF)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	wdev->wiphy->wowlan = &brcm_wowlan_support;
	brcm_wowlan_config = kmalloc(sizeof(struct cfg80211_wowlan), GFP_KERNEL);
	if (brcm_wowlan_config) {
		brcm_wowlan_config->disconnect = true;
		brcm_wowlan_config->gtk_rekey_failure = true;
		brcm_wowlan_config->eap_identity_req = true;
		brcm_wowlan_config->four_way_handshake = true;
		brcm_wowlan_config->patterns = NULL;
		brcm_wowlan_config->n_patterns = 0;
		brcm_wowlan_config->tcp = NULL;
	} else {
		WL_ERR(("Can not allocate memory for brcm_wowlan_config,"
					" So wiphy->wowlan_config is set to NULL\n"));
	}
	wdev->wiphy->wowlan_config = brcm_wowlan_config;
#else
	wdev->wiphy->wowlan.flags = WIPHY_WOWLAN_ANY;
	wdev->wiphy->wowlan.n_patterns = WL_WOWLAN_MAX_PATTERNS;
	wdev->wiphy->wowlan.pattern_min_len = WL_WOWLAN_MIN_PATTERN_LEN;
	wdev->wiphy->wowlan.pattern_max_len = WL_WOWLAN_MAX_PATTERN_LEN;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	wdev->wiphy->wowlan.max_pkt_offset = WL_WOWLAN_MAX_PATTERN_LEN;
#endif 
#endif 
#endif 

	WL_DBG(("Registering custom regulatory)\n"));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	wdev->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;
#else
	wdev->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
#endif
	wiphy_apply_custom_regulatory(wdev->wiphy, &brcm_regdom);
#if defined(WL_VENDOR_EXT_SUPPORT)
	WL_ERR(("Registering Vendor80211\n"));
	err = wl_cfgvendor_attach(wdev->wiphy);
	if (unlikely(err < 0)) {
		WL_ERR(("Couldn not attach vendor commands (%d)\n", err));
	}
#endif 
	
	err = wiphy_register(wdev->wiphy);
	if (unlikely(err < 0)) {
		WL_ERR(("Couldn not register wiphy device (%d)\n", err));
		wiphy_free(wdev->wiphy);
	}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && (LINUX_VERSION_CODE <= \
	KERNEL_VERSION(3, 3, 0))) && defined(WL_IFACE_COMB_NUM_CHANNELS)
	wdev->wiphy->flags &= ~WIPHY_FLAG_ENFORCE_COMBINATIONS;
#endif

	return err;
}

static void wl_free_wdev(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev = cfg->wdev;
	struct wiphy *wiphy = NULL;
	if (!wdev) {
		WL_ERR(("wdev is invalid\n"));
		return;
	}
	if (wdev->wiphy) {
		wiphy = wdev->wiphy;

#if defined(WL_VENDOR_EXT_SUPPORT)
		wl_cfgvendor_detach(wdev->wiphy);
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
		
		WL_DBG(("wl_free_wdev Clearing wowlan Config \n"));
		wdev->wiphy->wowlan = NULL;
#endif 
		wiphy_unregister(wdev->wiphy);
		wdev->wiphy->dev.parent = NULL;
		wdev->wiphy = NULL;
	}

	wl_delete_all_netinfo(cfg);
	if (wiphy)
		wiphy_free(wiphy);

}

static s32 wl_inform_bss(struct bcm_cfg80211 *cfg)
{
	struct wl_scan_results *bss_list;
	struct wl_bss_info *bi = NULL;	
	s32 err = 0;
	s32 i;

	bss_list = cfg->bss_list;
	WL_DBG(("scanned AP count (%d)\n", bss_list->count));
	bi = next_bss(bss_list, bi);
	for_each_bss(bss_list, bi, i) {
		err = wl_inform_single_bss(cfg, bi, false);
		if (unlikely(err))
			break;
	}
	return err;
}

static s32 wl_inform_single_bss(struct bcm_cfg80211 *cfg, struct wl_bss_info *bi, bool roam)
{
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_channel *channel;
	struct ieee80211_supported_band *band;
	struct wl_cfg80211_bss_info *notif_bss_info;
	struct wl_scan_req *sr = wl_to_sr(cfg);
	struct beacon_proberesp *beacon_proberesp;
	struct cfg80211_bss *cbss = NULL;
	s32 mgmt_type;
	s32 signal;
	u32 freq;
	s32 err = 0;
	gfp_t aflags;

	if (unlikely(dtoh32(bi->length) > WL_BSS_INFO_MAX)) {
		WL_DBG(("Beacon is larger than buffer. Discarding\n"));
		return err;
	}
	aflags = (in_atomic()) ? GFP_ATOMIC : GFP_KERNEL;
	notif_bss_info = kzalloc(sizeof(*notif_bss_info) + sizeof(*mgmt)
		- sizeof(u8) + WL_BSS_INFO_MAX, aflags);
	if (unlikely(!notif_bss_info)) {
		WL_ERR(("notif_bss_info alloc failed\n"));
		return -ENOMEM;
	}
	mgmt = (struct ieee80211_mgmt *)notif_bss_info->frame_buf;
	notif_bss_info->channel =
		wf_chspec_ctlchan(wl_chspec_driver_to_host(bi->chanspec));

	if (notif_bss_info->channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!band) {
		WL_ERR(("No valid band"));
		kfree(notif_bss_info);
		return -EINVAL;
	}
	notif_bss_info->rssi = wl_rssi_offset(dtoh16(bi->RSSI));
	memcpy(mgmt->bssid, &bi->BSSID, ETHER_ADDR_LEN);
	mgmt_type = cfg->active_scan ?
		IEEE80211_STYPE_PROBE_RESP : IEEE80211_STYPE_BEACON;
	if (!memcmp(bi->SSID, sr->ssid.SSID, bi->SSID_len)) {
	    mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | mgmt_type);
	}
	beacon_proberesp = cfg->active_scan ?
		(struct beacon_proberesp *)&mgmt->u.probe_resp :
		(struct beacon_proberesp *)&mgmt->u.beacon;
	beacon_proberesp->timestamp = 0;
	beacon_proberesp->beacon_int = cpu_to_le16(bi->beacon_period);
	beacon_proberesp->capab_info = cpu_to_le16(bi->capability);
	wl_rst_ie(cfg);
	wl_update_hidden_ap_ie(bi, ((u8 *) bi) + bi->ie_offset, &bi->ie_length, roam);
	wl_mrg_ie(cfg, ((u8 *) bi) + bi->ie_offset, bi->ie_length);
	wl_cp_ie(cfg, beacon_proberesp->variable, WL_BSS_INFO_MAX -
		offsetof(struct wl_cfg80211_bss_info, frame_buf));
	notif_bss_info->frame_len = offsetof(struct ieee80211_mgmt,
		u.beacon.variable) + wl_get_ielen(cfg);
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 38) && !defined(WL_COMPAT_WIRELESS)
	freq = ieee80211_channel_to_frequency(notif_bss_info->channel);
	(void)band->band;
#else
	freq = ieee80211_channel_to_frequency(notif_bss_info->channel, band->band);
#endif
	if (freq == 0) {
		WL_ERR(("Invalid channel, fail to chcnage channel to freq\n"));
		kfree(notif_bss_info);
		return -EINVAL;
	}
	channel = ieee80211_get_channel(wiphy, freq);
	if (unlikely(!channel)) {
		WL_ERR(("ieee80211_get_channel error\n"));
		kfree(notif_bss_info);
		return -EINVAL;
	}
	WL_DBG(("SSID : \"%s\", rssi %d, channel %d, capability : 0x04%x, bssid %pM"
			"mgmt_type %d frame_len %d\n", bi->SSID,
			notif_bss_info->rssi, notif_bss_info->channel,
			mgmt->u.beacon.capab_info, &bi->BSSID, mgmt_type,
			notif_bss_info->frame_len));

	signal = notif_bss_info->rssi * 100;
	if (!mgmt->u.probe_resp.timestamp) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
		struct timespec ts;
		get_monotonic_boottime(&ts);
		mgmt->u.probe_resp.timestamp = ((u64)ts.tv_sec*1000000)
				+ ts.tv_nsec / 1000;
#else
		struct timeval tv;
		do_gettimeofday(&tv);
		mgmt->u.probe_resp.timestamp = ((u64)tv.tv_sec*1000000)
				+ tv.tv_usec;
#endif
	}


	cbss = cfg80211_inform_bss_frame(wiphy, channel, mgmt,
		le16_to_cpu(notif_bss_info->frame_len), signal, aflags);
	if (unlikely(!cbss)) {
		WL_ERR(("cfg80211_inform_bss_frame error\n"));
		kfree(notif_bss_info);
		return -EINVAL;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	cfg80211_put_bss(wiphy, cbss);
#else
	cfg80211_put_bss(cbss);
#endif 
	kfree(notif_bss_info);
	return err;
}

static bool wl_is_linkup(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e, struct net_device *ndev)
{
	u32 event = ntoh32(e->event_type);
	u32 status =  ntoh32(e->status);
	u16 flags = ntoh16(e->flags);

	WL_DBG(("event %d, status %d flags %x\n", event, status, flags));
	if (event == WLC_E_SET_SSID) {
		if (status == WLC_E_STATUS_SUCCESS) {
			if (!wl_is_ibssmode(cfg, ndev))
				return true;
		}
	} else if (event == WLC_E_LINK) {
		if (flags & WLC_EVENT_MSG_LINK)
			return true;
	}

	WL_DBG(("wl_is_linkup false\n"));
	return false;
}

static bool wl_is_linkdown(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e)
{
	u32 event = ntoh32(e->event_type);
	u16 flags = ntoh16(e->flags);

	if (event == WLC_E_DEAUTH_IND ||
	event == WLC_E_DISASSOC_IND ||
	event == WLC_E_DISASSOC ||
	event == WLC_E_DEAUTH) {
#if (WL_DBG_LEVEL > 0)
	WL_ERR(("Link down Reason : WLC_E_%s\n", wl_dbg_estr[event]));
#endif 
		return true;
	} else if (event == WLC_E_LINK) {
		if (!(flags & WLC_EVENT_MSG_LINK)) {
#if (WL_DBG_LEVEL > 0)
	WL_ERR(("Link down Reason : WLC_E_%s\n", wl_dbg_estr[event]));
#endif 
			return true;
		}
	}

	return false;
}

static bool wl_is_nonetwork(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e)
{
	u32 event = ntoh32(e->event_type);
	u32 status = ntoh32(e->status);

	if (event == WLC_E_LINK && status == WLC_E_STATUS_NO_NETWORKS)
		return true;
	if (event == WLC_E_SET_SSID && status != WLC_E_STATUS_SUCCESS)
		return true;

	return false;
}

static s32
wl_notify_connect_status_ap(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = ntoh32(e->event_type);
	u32 reason = ntoh32(e->reason);
	u32 len = ntoh32(e->datalen);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)) && !defined(WL_CFG80211_STA_EVENT) \
	&& !defined(WL_COMPAT_WIRELESS)
	bool isfree = false;
	u8 *mgmt_frame;
	u8 bsscfgidx = e->bsscfgidx;
	s32 freq;
	s32 channel;
	u8 *body = NULL;
	u16 fc = 0;

	struct ieee80211_supported_band *band;
	struct ether_addr da;
	struct ether_addr bssid;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	channel_info_t ci;
#else
	struct station_info sinfo;
#endif 

	WL_DBG(("event %d status %d reason %d\n", event, ntoh32(e->status), reason));
	
	if (event == WLC_E_LINK && reason == WLC_E_LINK_BSSCFG_DIS &&
		wl_get_p2p_status(cfg, IF_DELETING) && (ndev != bcmcfg_to_prmry_ndev(cfg))) {
		wl_add_remove_eventmsg(ndev, WLC_E_PROBREQ_MSG, false);
		WL_INFORM(("AP mode link down !! \n"));
		complete(&cfg->iface_disable);
		return 0;
	}

	if (event == WLC_E_DISASSOC_IND || event == WLC_E_DEAUTH_IND || event == WLC_E_DEAUTH) {
		WL_ERR(("event %s(%d) status %d reason %d\n",
		bcmevent_get_name(event), event, ntoh32(e->status), reason));
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)) && !defined(WL_CFG80211_STA_EVENT) \
	&& !defined(WL_COMPAT_WIRELESS)
	WL_DBG(("Enter \n"));
	if (!len && (event == WLC_E_DEAUTH)) {
		len = 2; 
		data = &reason;
	}
	if (len) {
		body = kzalloc(len, GFP_KERNEL);

		if (body == NULL) {
			WL_ERR(("wl_notify_connect_status: Failed to allocate body\n"));
			return WL_INVALID;
		}
	}
	memset(&bssid, 0, ETHER_ADDR_LEN);
	WL_DBG(("Enter event %d ndev %p\n", event, ndev));
	if (wl_get_mode_by_netdev(cfg, ndev) == WL_INVALID) {
		kfree(body);
		return WL_INVALID;
	}
	if (len)
		memcpy(body, data, len);

	wldev_iovar_getbuf_bsscfg(ndev, "cur_etheraddr",
		NULL, 0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, bsscfgidx, &cfg->ioctl_buf_sync);
	memcpy(da.octet, cfg->ioctl_buf, ETHER_ADDR_LEN);
	err = wldev_ioctl(ndev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN, false);
	switch (event) {
		case WLC_E_ASSOC_IND:
			fc = FC_ASSOC_REQ;
			break;
		case WLC_E_REASSOC_IND:
			fc = FC_REASSOC_REQ;
			break;
		case WLC_E_DISASSOC_IND:
			fc = FC_DISASSOC;
			break;
		case WLC_E_DEAUTH_IND:
			fc = FC_DISASSOC;
			break;
		case WLC_E_DEAUTH:
			fc = FC_DISASSOC;
			break;
		default:
			fc = 0;
			goto exit;
	}
	if ((err = wldev_ioctl(ndev, WLC_GET_CHANNEL, &ci, sizeof(ci), false))) {
		kfree(body);
		return err;
	}

	channel = dtoh32(ci.hw_channel);
	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!band) {
		WL_ERR(("No valid band"));
		if (body)
			kfree(body);
		return -EINVAL;
	}
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 38) && !defined(WL_COMPAT_WIRELESS)
	freq = ieee80211_channel_to_frequency(channel);
	(void)band->band;
#else
	freq = ieee80211_channel_to_frequency(channel, band->band);
#endif

	err = wl_frame_get_mgmt(fc, &da, &e->addr, &bssid,
		&mgmt_frame, &len, body);
	if (err < 0)
		goto exit;
	isfree = true;

	if (event == WLC_E_ASSOC_IND && reason == DOT11_SC_SUCCESS) {
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) && (LINUX_VERSION_CODE < \
	KERNEL_VERSION(3, 18, 0))) || defined(WL_COMPAT_WIRELESS)
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, GFP_ATOMIC);

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len);
#else
		cfg80211_rx_mgmt(ndev, freq, mgmt_frame, len, GFP_ATOMIC);
#endif 
	} else if (event == WLC_E_DISASSOC_IND) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || defined(WL_COMPAT_WIRELESS)
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, GFP_ATOMIC);
#else
		cfg80211_rx_mgmt(ndev, freq, mgmt_frame, len, GFP_ATOMIC);
#endif 
	} else if ((event == WLC_E_DEAUTH_IND) || (event == WLC_E_DEAUTH)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || defined(WL_COMPAT_WIRELESS)
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, GFP_ATOMIC);
#else
		cfg80211_rx_mgmt(ndev, freq, mgmt_frame, len, GFP_ATOMIC);
#endif 
	}

exit:
	if (isfree)
		kfree(mgmt_frame);
	if (body)
		kfree(body);
#else 
	sinfo.filled = 0;
	if (((event == WLC_E_ASSOC_IND) || (event == WLC_E_REASSOC_IND)) &&
		reason == DOT11_SC_SUCCESS) {
		sinfo.filled = STATION_INFO_ASSOC_REQ_IES;
		if (!data) {
			WL_ERR(("No IEs present in ASSOC/REASSOC_IND"));
			return -EINVAL;
		}
		sinfo.assoc_req_ies = data;
		sinfo.assoc_req_ies_len = len;
		cfg80211_new_sta(ndev, e->addr.octet, &sinfo, GFP_ATOMIC);
	} else if (event == WLC_E_DISASSOC_IND) {
		cfg80211_del_sta(ndev, e->addr.octet, GFP_ATOMIC);
	} else if ((event == WLC_E_DEAUTH_IND) || (event == WLC_E_DEAUTH)) {
		cfg80211_del_sta(ndev, e->addr.octet, GFP_ATOMIC);
	}
#endif 
	return err;
}

static s32
wl_get_auth_assoc_status(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e)
{
	u32 reason = ntoh32(e->reason);
	u32 event = ntoh32(e->event_type);
	struct wl_security *sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
	WL_DBG(("event type : %d, reason : %d\n", event, reason));
	if (sec) {
		switch (event) {
		case WLC_E_ASSOC:
		case WLC_E_AUTH:
				sec->auth_assoc_res_status = reason;
		default:
			break;
		}
	} else
		WL_ERR(("sec is NULL\n"));
	return 0;
}

static s32
wl_notify_connect_status_ibss(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = ntoh32(e->event_type);
	u16 flags = ntoh16(e->flags);
	u32 status =  ntoh32(e->status);
	bool active;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	struct ieee80211_channel *channel = NULL;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	u32 chanspec, chan;
	u32 freq, band;
#endif 

	if (event == WLC_E_JOIN) {
		WL_DBG(("joined in IBSS network\n"));
	}
	if (event == WLC_E_START) {
		WL_DBG(("started IBSS network\n"));
	}
	if (event == WLC_E_JOIN || event == WLC_E_START ||
		(event == WLC_E_LINK && (flags == WLC_EVENT_MSG_LINK))) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
		err = wldev_iovar_getint(ndev, "chanspec", (s32 *)&chanspec);
		if (unlikely(err)) {
			WL_ERR(("Could not get chanspec %d\n", err));
			return err;
		}
		chan = wf_chspec_ctlchan(wl_chspec_driver_to_host(chanspec));
		band = (chan <= CH_MAX_2G_CHANNEL) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
		freq = ieee80211_channel_to_frequency(chan, band);
		channel = ieee80211_get_channel(wiphy, freq);
#endif 
		if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
			
			u8 *cur_bssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
			if (memcmp(cur_bssid, &e->addr, ETHER_ADDR_LEN) == 0) {
				WL_DBG(("IBSS connected event from same BSSID("
					MACDBG "), ignore it\n", MAC2STRDBG(cur_bssid)));
				return err;
			}
			WL_INFORM(("IBSS BSSID is changed from " MACDBG " to " MACDBG "\n",
				MAC2STRDBG(cur_bssid), MAC2STRDBG((const u8 *)&e->addr)));
			wl_get_assoc_ies(cfg, ndev);
			wl_update_prof(cfg, ndev, NULL, (const void *)&e->addr, WL_PROF_BSSID);
			wl_update_bss_info(cfg, ndev, false);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
			cfg80211_ibss_joined(ndev, (const s8 *)&e->addr, channel, GFP_KERNEL);
#else
			cfg80211_ibss_joined(ndev, (const s8 *)&e->addr, GFP_KERNEL);
#endif
		}
		else {
			
			WL_INFORM(("IBSS connected to " MACDBG "\n",
				MAC2STRDBG((const u8 *)&e->addr)));
			wl_link_up(cfg);
			wl_get_assoc_ies(cfg, ndev);
			wl_update_prof(cfg, ndev, NULL, (const void *)&e->addr, WL_PROF_BSSID);
			wl_update_bss_info(cfg, ndev, false);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
			cfg80211_ibss_joined(ndev, (const s8 *)&e->addr, channel, GFP_KERNEL);
#else
			cfg80211_ibss_joined(ndev, (const s8 *)&e->addr, GFP_KERNEL);
#endif
			wl_set_drv_status(cfg, CONNECTED, ndev);
			active = true;
			wl_update_prof(cfg, ndev, NULL, (const void *)&active, WL_PROF_ACT);
		}
	} else if ((event == WLC_E_LINK && !(flags & WLC_EVENT_MSG_LINK)) ||
		event == WLC_E_DEAUTH_IND || event == WLC_E_DISASSOC_IND) {
		wl_clr_drv_status(cfg, CONNECTED, ndev);
		wl_link_down(cfg);
		wl_init_prof(cfg, ndev);
	}
	else if (event == WLC_E_SET_SSID && status == WLC_E_STATUS_NO_NETWORKS) {
		WL_DBG(("no action - join fail (IBSS mode)\n"));
	}
	else {
		WL_DBG(("no action (IBSS mode)\n"));
}
	return err;
}

static s32
wl_notify_connect_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	bool act;
	struct net_device *ndev = NULL;
	s32 err = 0;
	u32 event = ntoh32(e->event_type);
	struct wiphy *wiphy = NULL;
	struct cfg80211_bss *bss = NULL;
	struct wlc_ssid *ssid = NULL;
	u8 *bssid = 0;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_AP) {
		err = wl_notify_connect_status_ap(cfg, ndev, e, data);
	} else if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_IBSS) {
		err = wl_notify_connect_status_ibss(cfg, ndev, e, data);
	} else if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_BSS) {
		
#if 0
		WL_DBG(("wl_notify_connect_status : event %d status : %d ndev %p\n",
			ntoh32(e->event_type), ntoh32(e->status), ndev));
#else
		WL_ERR(("wl_notify_connect_status : event %d status : %d ndev %p\n",
			ntoh32(e->event_type), ntoh32(e->status), ndev));
#endif
		
		if (event == WLC_E_ASSOC || event == WLC_E_AUTH) {
			wl_get_auth_assoc_status(cfg, ndev, e);
			return 0;
		}
		DHD_DISABLE_RUNTIME_PM((dhd_pub_t *)cfg->pub);
		if (wl_is_linkup(cfg, e, ndev)) {
			wl_link_up(cfg);
			act = true;
#ifdef CUSTOMER_HW_ONE
			disc_called = false;
#endif 
			if (!wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
#ifdef DHD_LOSSLESS_ROAMING
					bool is_connected = wl_get_drv_status(cfg, CONNECTED, ndev);
#endif

					WL_ERR(("wl_bss_connect_done succeeded with " MACDBG "\n",
						MAC2STRDBG((const u8*)(&e->addr))));
					wl_bss_connect_done(cfg, ndev, e, data, true);
					WL_DBG(("joined in BSS network \"%s\"\n",
					((struct wlc_ssid *)
					 wl_read_prof(cfg, ndev, WL_PROF_SSID))->SSID));
#ifdef DHD_LOSSLESS_ROAMING
					if (event == WLC_E_LINK && is_connected &&
							!cfg->roam_offload) {
						wl_bss_roaming_done(cfg, ndev, e, data);
					}
#endif 
			
			} else {
				WL_ERR(("wl_notify_connect_status DISCONNECTING\n"));
			
			}
			wl_update_prof(cfg, ndev, e, &act, WL_PROF_ACT);
			wl_update_prof(cfg, ndev, NULL, (const void *)&e->addr, WL_PROF_BSSID);
#ifdef CUSTOMER_HW_ONE
			sta_connected = 1;
#endif 

		} else if (wl_is_linkdown(cfg, e)) {
#ifdef CUSTOMER_HW_ONE
			disc_called = false;
#endif 
#ifdef DHD_LOSSLESS_ROAMING
			wl_del_roam_timeout(cfg);
#endif
#ifdef P2PLISTEN_AP_SAMECHN
			if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
				wl_cfg80211_set_p2p_resp_ap_chn(ndev, 0);
				cfg->p2p_resp_apchn_status = false;
				WL_DBG(("p2p_resp_apchn_status Turn OFF \n"));
			}
#endif 
			wl_cfg80211_cancel_scan(cfg);

			
			wiphy = bcmcfg_to_wiphy(cfg);
			ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
			bssid = (u8 *)wl_read_prof(cfg, ndev, WL_PROF_BSSID);
			if (ssid && bssid) {
				bss = cfg80211_get_bss(wiphy, NULL, bssid,
					ssid->SSID, ssid->SSID_len, WLAN_CAPABILITY_ESS,
					WLAN_CAPABILITY_ESS);
				if (bss) {
					cfg80211_unlink_bss(wiphy, bss);
				}
			}

			if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
				scb_val_t scbval;
				u8 *curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
				s32 reason = 0;
				struct ether_addr bssid_dongle;

				if (event == WLC_E_DEAUTH_IND || event == WLC_E_DISASSOC_IND)
					reason = ntoh32(e->reason);
				
				reason = (reason == WLAN_REASON_UNSPECIFIED)? 0 : reason;

				WL_ERR(("link down if %s may call cfg80211_disconnected. "
					"event : %d, reason=%d from " MACDBG "\n",
					ndev->name, event, ntoh32(e->reason),
					MAC2STRDBG((const u8*)(&e->addr))));

				
				if (cfg->roam_offload) {
					if (wldev_ioctl(ndev, WLC_GET_BSSID, &bssid_dongle,
							sizeof(bssid_dongle), false) == BCME_OK) {
						curbssid = (u8 *)&bssid_dongle;
					}
				}
				if (memcmp(curbssid, &e->addr, ETHER_ADDR_LEN) != 0) {
					WL_ERR(("BSSID of event is not the connected BSSID"
						"(ignore it) cur: " MACDBG " event: " MACDBG"\n",
						MAC2STRDBG(curbssid),
						MAC2STRDBG((const u8*)(&e->addr))));
					return 0;
				}
				wl_clr_drv_status(cfg, CONNECTED, ndev);
				if (! wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
					scbval.val = WLAN_REASON_DEAUTH_LEAVING;

					memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
					scbval.val = htod32(scbval.val);
					err = wldev_ioctl(ndev, WLC_DISASSOC, &scbval,
						sizeof(scb_val_t), true);
					if (err < 0) {
						WL_ERR(("WLC_DISASSOC error %d\n", err));
						err = 0;
					}
					cfg80211_disconnected(ndev, reason, NULL, 0, GFP_KERNEL);
					wl_link_down(cfg);
					wl_init_prof(cfg, ndev);
					memset(&cfg->last_roamed_addr, 0, ETHER_ADDR_LEN);
				}
#ifdef CUSTOMER_HW_ONE
				sta_connected = 0;
				wl_android_traffic_monitor(ndev);
#endif 
			}
			else if (wl_get_drv_status(cfg, CONNECTING, ndev)) {
				WL_ERR(("link down, during connecting\n"));
#ifdef ESCAN_RESULT_PATCH
				if ((memcmp(connect_req_bssid, broad_bssid, ETHER_ADDR_LEN) == 0) ||
					(memcmp(&e->addr, broad_bssid, ETHER_ADDR_LEN) == 0) ||
					(memcmp(&e->addr, connect_req_bssid, ETHER_ADDR_LEN) == 0))
					
#endif 
					wl_bss_connect_done(cfg, ndev, e, data, false);
			}
			wl_clr_drv_status(cfg, DISCONNECTING, ndev);

			DHD_ENABLE_RUNTIME_PM((dhd_pub_t *)cfg->pub);
			
			if (ndev != bcmcfg_to_prmry_ndev(cfg))
				complete(&cfg->iface_disable);

		} else if (wl_is_nonetwork(cfg, e)) {
			WL_ERR(("connect failed event=%d e->status %d e->reason %d \n",
				event, (int)ntoh32(e->status), (int)ntoh32(e->reason)));
			
			wl_cfg80211_cancel_scan(cfg);
			if (wl_get_drv_status(cfg, CONNECTING, ndev))
				wl_bss_connect_done(cfg, ndev, e, data, false);
		} else {
			WL_DBG(("%s nothing\n", __FUNCTION__));
		}
	}
		else {
		WL_ERR(("Invalid ndev status %d\n", wl_get_mode_by_netdev(cfg, ndev)));
	}
	return err;
}

void wl_cfg80211_set_rmc_pid(int pid)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	if (pid > 0)
		cfg->rmc_event_pid = pid;
	WL_DBG(("set pid for rmc event : pid=%d\n", pid));
}

#ifdef WLAIBSS
void wl_cfg80211_set_txfail_pid(int pid)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	if (pid > 0)
		cfg->aibss_txfail_pid = pid;
	WL_DBG(("set pid for aibss fail event : pid=%d\n", pid));
}

static s32
wl_notify_aibss_txfail(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	u32 evt = ntoh32(e->event_type);
	int ret = -1;
#ifdef PCIE_FULL_DONGLE
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	u32 reason = ntoh32(e->reason);
	uint8 ifindex;
	ifindex = (uint8)dhd_ifname2idx(dhd->info, (char*)e->ifname);
#endif
	if (cfg->aibss_txfail_pid != 0) {
#ifdef PCIE_FULL_DONGLE
		if (reason == AIBSS_PEER_FREE) {
			WL_INFORM(("Peer freed. Flow rings delete for peer.\n"));
			dhd_flow_rings_delete_for_peer(dhd, ifindex,
				(void *)&e->addr.octet[0]);
			return 0;
		}
#endif
		ret = wl_netlink_send_msg(cfg->aibss_txfail_pid, AIBSS_EVENT_TXFAIL,
			cfg->aibss_txfail_seq++, (void *)&e->addr, ETHER_ADDR_LEN);
	}

	WL_DBG(("txfail : evt=%d, pid=%d, ret=%d, mac=" MACF "\n",
		evt, cfg->aibss_txfail_pid, ret, ETHERP_TO_MACF(&e->addr)));
	return ret;
}
#endif 
#ifdef WL_RELMCAST
static s32
wl_notify_rmc_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	u32 evt = ntoh32(e->event_type);
	u32 reason = ntoh32(e->reason);
	int ret = -1;

	switch (reason) {
		case WLC_E_REASON_RMC_AR_LOST:
		case WLC_E_REASON_RMC_AR_NO_ACK:
			if (cfg->rmc_event_pid != 0) {
				ret = wl_netlink_send_msg(cfg->rmc_event_pid,
					RMC_EVENT_LEADER_CHECK_FAIL,
					cfg->rmc_event_seq++, NULL, 0);
			}
			break;
		default:
			break;
	}
	WL_DBG(("rmcevent : evt=%d, pid=%d, ret=%d\n", evt, cfg->rmc_event_pid, ret));
	return ret;
}
#endif 

#ifdef GSCAN_SUPPORT
static s32
wl_handle_roam_exp_event(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	u32 datalen = be32_to_cpu(e->datalen);

	if (datalen) {
		wl_roam_exp_event_t *evt_data = (wl_roam_exp_event_t *)data;
		if (evt_data->version == ROAM_EXP_EVENT_VERSION) {
			wlc_ssid_t *ssid = &evt_data->cur_ssid;
			struct wireless_dev *wdev;
			ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
			if (ndev) {
				wdev = ndev->ieee80211_ptr;
				wdev->ssid_len = min(ssid->SSID_len, (uint32)DOT11_MAX_SSID_LEN);
				memcpy(wdev->ssid, ssid->SSID, wdev->ssid_len);
				WL_ERR(("SSID is %s\n", ssid->SSID));
				wl_update_prof(cfg, ndev, NULL, ssid, WL_PROF_SSID);
			} else {
				WL_ERR(("NULL ndev!\n"));
			}
		} else {
			WL_ERR(("Version mismatch %d, expected %d", evt_data->version,
			       ROAM_EXP_EVENT_VERSION));
		}
	}
	return BCME_OK;
}
#endif 

static s32 wl_handle_rssi_monitor_event(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{

#if defined(WL_VENDOR_EXT_SUPPORT) || defined(CONFIG_BCMDHD_VENDOR_EXT)
	u32 datalen = be32_to_cpu(e->datalen);
	struct net_device *ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	if (datalen) {
		wl_rssi_monitor_evt_t *evt_data = (wl_rssi_monitor_evt_t *)data;
		if (evt_data->version == RSSI_MONITOR_VERSION) {
			dhd_rssi_monitor_evt_t monitor_data;
			monitor_data.version = DHD_RSSI_MONITOR_EVT_VERSION;
			monitor_data.cur_rssi = evt_data->cur_rssi;
			memcpy(&monitor_data.BSSID, &e->addr, ETHER_ADDR_LEN);
			wl_cfgvendor_send_async_event(wiphy, ndev,
				GOOGLE_RSSI_MONITOR_EVENT,
				&monitor_data, sizeof(monitor_data));
		} else {
			WL_ERR(("Version mismatch %d, expected %d", evt_data->version,
			       RSSI_MONITOR_VERSION));
		}
	}
#endif 
	return BCME_OK;
}

static s32
wl_notify_roaming_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	bool act;
	struct net_device *ndev = NULL;
	s32 err = 0;
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);
#ifdef DHD_LOSSLESS_ROAMING
	struct wl_security *sec;
#endif
	
#if 0
	WL_DBG(("Enter \n"));
#else
	WL_ERR(("Enter: event = %d, status = %d \n", event, status));
#endif
	

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if ((!cfg->disable_roam_event) && (event == WLC_E_BSSID)) {
		wl_add_remove_eventmsg(ndev, WLC_E_ROAM, false);
		cfg->disable_roam_event = TRUE;
	}

	
#if 0
	if ((cfg->disable_roam_event) && (event == WLC_E_ROAM))
		return err;
#else
	if ((cfg->disable_roam_event) && (event == WLC_E_ROAM)) {
		WL_ERR(("disabled roam_event, Exit\n"));
		return err;
	}
#endif
	

	if ((event == WLC_E_ROAM || event == WLC_E_BSSID) && status == WLC_E_STATUS_SUCCESS) {
		if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
			if (cfg->roam_offload &&
				memcmp(&cfg->last_roamed_addr, &e->addr, ETHER_ADDR_LEN) == 0) {
				
#if 0
				WL_INFORM(("BSSID already updated\n"));
#else
				WL_ERR(("BSSID already updated\n"));
#endif
				
				return err;
			}
#ifdef DHD_LOSSLESS_ROAMING
			if (cfg->roam_offload) {
				wl_bss_roaming_done(cfg, ndev, e, data);
				wl_del_roam_timeout(cfg);
			}
			else {
				sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
				if (IS_AKM_SUITE_FT(sec)) {
					wl_bss_roaming_done(cfg, ndev, e, data);
				}
				if (IS_AKM_SUITE_FT(sec) || IS_AKM_SUITE_CCKM(sec)) {
					wl_del_roam_timeout(cfg);
				}
			}
#else
			wl_bss_roaming_done(cfg, ndev, e, data);
#endif 
			memcpy(&cfg->last_roamed_addr, (void *)&e->addr, ETHER_ADDR_LEN);
		} else {
			wl_bss_connect_done(cfg, ndev, e, data, true);
		}
		act = true;
		wl_update_prof(cfg, ndev, e, &act, WL_PROF_ACT);
		wl_update_prof(cfg, ndev, NULL, (const void *)&e->addr, WL_PROF_BSSID);
	}
#ifdef DHD_LOSSLESS_ROAMING
	else if ((event == WLC_E_ROAM || event == WLC_E_BSSID) && status != WLC_E_STATUS_SUCCESS) {
		wl_del_roam_timeout(cfg);
	}
#endif

	return err;
}

#ifdef QOS_MAP_SET
uint8 *
wl_get_up_table(void)
{
	return (uint8 *)(g_bcm_cfg->up_table);
}
#endif 

#ifdef DHD_LOSSLESS_ROAMING
static s32
wl_notify_roam_prep_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;

	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	dhdp->dequeue_prec_map = 1 << PRIO_8021D_NC;
	
	dhd_txflowcontrol(dhdp, ALL_INTERFACES, OFF);

	mod_timer(&cfg->roam_timeout, jiffies + msecs_to_jiffies(WL_ROAM_TIMEOUT_MS));

	return err;
}
#endif 

static s32 wl_get_assoc_ies(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	wl_assoc_info_t assoc_info;
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	s32 err = 0;
#ifdef QOS_MAP_SET
	bcm_tlv_t * qos_map_ie = NULL;
#endif 

	WL_DBG(("Enter \n"));
	err = wldev_iovar_getbuf(ndev, "assoc_info", NULL, 0, cfg->extra_buf,
		WL_ASSOC_INFO_MAX, NULL);
	if (unlikely(err)) {
		WL_ERR(("could not get assoc info (%d)\n", err));
		return err;
	}
	memcpy(&assoc_info, cfg->extra_buf, sizeof(wl_assoc_info_t));
	assoc_info.req_len = htod32(assoc_info.req_len);
	assoc_info.resp_len = htod32(assoc_info.resp_len);
	assoc_info.flags = htod32(assoc_info.flags);
	if (conn_info->req_ie_len) {
		conn_info->req_ie_len = 0;
		bzero(conn_info->req_ie, sizeof(conn_info->req_ie));
	}
	if (conn_info->resp_ie_len) {
		conn_info->resp_ie_len = 0;
		bzero(conn_info->resp_ie, sizeof(conn_info->resp_ie));
	}
	if (assoc_info.req_len) {
		err = wldev_iovar_getbuf(ndev, "assoc_req_ies", NULL, 0, cfg->extra_buf,
			WL_ASSOC_INFO_MAX, NULL);
		if (unlikely(err)) {
			WL_ERR(("could not get assoc req (%d)\n", err));
			return err;
		}
		conn_info->req_ie_len = assoc_info.req_len - sizeof(struct dot11_assoc_req);
		if (assoc_info.flags & WLC_ASSOC_REQ_IS_REASSOC) {
			conn_info->req_ie_len -= ETHER_ADDR_LEN;
		}
		if (conn_info->req_ie_len <= MAX_REQ_LINE)
			memcpy(conn_info->req_ie, cfg->extra_buf, conn_info->req_ie_len);
		else {
			WL_ERR(("IE size %d above max %d size \n",
				conn_info->req_ie_len, MAX_REQ_LINE));
			return err;
		}
	} else {
		conn_info->req_ie_len = 0;
	}
	if (assoc_info.resp_len) {
		err = wldev_iovar_getbuf(ndev, "assoc_resp_ies", NULL, 0, cfg->extra_buf,
			WL_ASSOC_INFO_MAX, NULL);
		if (unlikely(err)) {
			WL_ERR(("could not get assoc resp (%d)\n", err));
			return err;
		}
		conn_info->resp_ie_len = assoc_info.resp_len -sizeof(struct dot11_assoc_resp);
		if (conn_info->resp_ie_len <= MAX_REQ_LINE) {
			memcpy(conn_info->resp_ie, cfg->extra_buf, conn_info->resp_ie_len);
		} else {
			WL_ERR(("IE size %d above max %d size \n",
				conn_info->resp_ie_len, MAX_REQ_LINE));
			return err;
		}

#ifdef QOS_MAP_SET
		
		if ((qos_map_ie = bcm_parse_tlvs(conn_info->resp_ie, conn_info->resp_ie_len,
				DOT11_MNG_QOS_MAP_ID)) != NULL) {
			WL_DBG((" QoS map set IE found in assoc response\n"));
			if (!cfg->up_table) {
				cfg->up_table = kmalloc(UP_TABLE_MAX, GFP_KERNEL);
			}
			wl_set_up_table(cfg->up_table, qos_map_ie);
		} else {
			kfree(cfg->up_table);
			cfg->up_table = NULL;
		}
#endif 
	} else {
		conn_info->resp_ie_len = 0;
	}
	WL_DBG(("req len (%d) resp len (%d)\n", conn_info->req_ie_len,
		conn_info->resp_ie_len));

	return err;
}

static s32 wl_ch_to_chanspec(struct net_device *dev, int ch, struct wl_join_params *join_params,
        size_t *join_params_size)
{
	s32 bssidx = -1;
	chanspec_t chanspec = 0, chspec;
	if (ch != 0) {
		join_params->params.chanspec_num = 1;
		join_params->params.chanspec_list[0] = ch;

		if (join_params->params.chanspec_list[0] <= CH_MAX_2G_CHANNEL)
			chanspec |= WL_CHANSPEC_BAND_2G;
		else
			chanspec |= WL_CHANSPEC_BAND_5G;

		
		chspec = wl_cfg80211_ulb_get_min_bw_chspec(dev->ieee80211_ptr, bssidx);
		if (chspec == INVCHANSPEC) {
			WL_ERR(("Invalid chanspec \n"));
			return -EINVAL;
		}
		chanspec |= chspec;
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;

		*join_params_size += WL_ASSOC_PARAMS_FIXED_SIZE +
			join_params->params.chanspec_num * sizeof(chanspec_t);

		join_params->params.chanspec_list[0]  &= WL_CHANSPEC_CHAN_MASK;
		join_params->params.chanspec_list[0] |= chanspec;
		join_params->params.chanspec_list[0] =
			wl_chspec_host_to_driver(join_params->params.chanspec_list[0]);

		join_params->params.chanspec_num =
			htod32(join_params->params.chanspec_num);
		WL_DBG(("join_params->params.chanspec_list[0]= %X, %d channels\n",
			join_params->params.chanspec_list[0],
			join_params->params.chanspec_num));
	}
	return 0;
}

static s32 wl_update_bss_info(struct bcm_cfg80211 *cfg, struct net_device *ndev, bool roam)
{
	struct cfg80211_bss *bss;
	struct wl_bss_info *bi;
	struct wlc_ssid *ssid;
	struct bcm_tlv *tim;
	s32 beacon_interval;
	s32 dtim_period;
	size_t ie_len;
	u8 *ie;
	u8 *curbssid;
	s32 err = 0;
	struct wiphy *wiphy;
	u32 channel;

	wiphy = bcmcfg_to_wiphy(cfg);

	ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	bss = cfg80211_get_bss(wiphy, NULL, curbssid,
		ssid->SSID, ssid->SSID_len, WLAN_CAPABILITY_ESS,
		WLAN_CAPABILITY_ESS);

	mutex_lock(&cfg->usr_sync);

	*(u32 *) cfg->extra_buf = htod32(WL_EXTRA_BUF_MAX);
	err = wldev_ioctl(ndev, WLC_GET_BSS_INFO,
		cfg->extra_buf, WL_EXTRA_BUF_MAX, false);
	if (unlikely(err)) {
		WL_ERR(("Could not get bss info %d\n", err));
		goto update_bss_info_out;
	}
	bi = (struct wl_bss_info *)(cfg->extra_buf + 4);
	channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(bi->chanspec));
	wl_update_prof(cfg, ndev, NULL, &channel, WL_PROF_CHAN);

	if (!bss) {
		WL_DBG(("Could not find the AP\n"));
		if (memcmp(bi->BSSID.octet, curbssid, ETHER_ADDR_LEN)) {
			WL_ERR(("Bssid doesn't match\n"));
			err = -EIO;
			goto update_bss_info_out;
		}
		err = wl_inform_single_bss(cfg, bi, roam);
		if (unlikely(err))
			goto update_bss_info_out;

		ie = ((u8 *)bi) + bi->ie_offset;
		ie_len = bi->ie_length;
		beacon_interval = cpu_to_le16(bi->beacon_period);
	} else {
		WL_DBG(("Found the AP in the list - BSSID %pM\n", bss->bssid));
#if defined(WL_CFG80211_P2P_DEV_IF)
		ie = (u8 *)bss->ies->data;
		ie_len = bss->ies->len;
#else
		ie = bss->information_elements;
		ie_len = bss->len_information_elements;
#endif 
		beacon_interval = bss->beacon_interval;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
		cfg80211_put_bss(wiphy, bss);
#else
		cfg80211_put_bss(bss);
#endif 
	}

	tim = bcm_parse_tlvs(ie, ie_len, WLAN_EID_TIM);
	if (tim) {
		dtim_period = tim->data[1];
	} else {
		err = wldev_ioctl(ndev, WLC_GET_DTIMPRD,
			&dtim_period, sizeof(dtim_period), false);
		if (unlikely(err)) {
			WL_ERR(("WLC_GET_DTIMPRD error (%d)\n", err));
			goto update_bss_info_out;
		}
	}

	wl_update_prof(cfg, ndev, NULL, &beacon_interval, WL_PROF_BEACONINT);
	wl_update_prof(cfg, ndev, NULL, &dtim_period, WL_PROF_DTIMPERIOD);

update_bss_info_out:
	if (unlikely(err)) {
		WL_ERR(("Failed with error %d\n", err));
	}
	mutex_unlock(&cfg->usr_sync);
	return err;
}

static s32
wl_bss_roaming_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	s32 err = 0;
	u8 *curbssid;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct ieee80211_supported_band *band;
	struct ieee80211_channel *notify_channel = NULL;
	u32 *channel;
	u32 freq;
#endif 

#ifdef WLFBT
	uint32 data_len = 0;
	if (data)
		data_len = ntoh32(e->datalen);
#endif 

	wl_get_assoc_ies(cfg, ndev);
	wl_update_prof(cfg, ndev, NULL, (const void *)(e->addr.octet), WL_PROF_BSSID);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	wl_update_bss_info(cfg, ndev, true);
	wl_update_pmklist(ndev, cfg->pmk_list, err);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)
	
	channel = (u32 *)wl_read_prof(cfg, ndev, WL_PROF_CHAN);
	if (*channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	freq = ieee80211_channel_to_frequency(*channel, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);
#endif 
#ifdef WLFBT
	if (data && (data_len == FBT_KEYLEN)) {
		memcpy(cfg->fbt_key, data, FBT_KEYLEN);
	}
#endif 
	WL_ERR(("wl_bss_roaming_done succeeded to " MACDBG "\n",
		MAC2STRDBG((const u8*)(&e->addr))));

	cfg80211_roamed(ndev,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || defined(WL_COMPAT_WIRELESS)
		notify_channel,
#endif
		curbssid,
		conn_info->req_ie, conn_info->req_ie_len,
		conn_info->resp_ie, conn_info->resp_ie_len, GFP_KERNEL);
	WL_DBG(("Report roaming result\n"));

	wl_set_drv_status(cfg, CONNECTED, ndev);

	return err;
}

static s32
wl_bss_connect_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data, bool completed)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	struct wl_security *sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
#if defined(CUSTOM_SET_CPUCORE)
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
#endif 
	s32 err = 0;
	u8 *curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	if (!sec) {
		WL_ERR(("sec is NULL\n"));
		return -ENODEV;
	}
	WL_DBG((" enter\n"));
#ifdef ESCAN_RESULT_PATCH
	if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
		if (memcmp(curbssid, connect_req_bssid, ETHER_ADDR_LEN) == 0) {
			
#if 0
			WL_DBG((" Connected event of connected device e=%d s=%d, ignore it\n",
				ntoh32(e->event_type), ntoh32(e->status)));
#else
			WL_ERR((" Connected event of connected device e=%d s=%d, ignore it\n",
				ntoh32(e->event_type), ntoh32(e->status)));
#endif
			
			return err;
		}
	}
	if (memcmp(curbssid, broad_bssid, ETHER_ADDR_LEN) == 0 &&
		memcmp(broad_bssid, connect_req_bssid, ETHER_ADDR_LEN) != 0) {
		WL_DBG(("copy bssid\n"));
		memcpy(curbssid, connect_req_bssid, ETHER_ADDR_LEN);
	}

#else
	if (cfg->scan_request) {
		wl_notify_escan_complete(cfg, ndev, true, true);
	}
#endif 
	if (wl_get_drv_status(cfg, CONNECTING, ndev)) {
		wl_cfg80211_scan_abort(cfg);
		wl_clr_drv_status(cfg, CONNECTING, ndev);
		if (completed) {
			wl_get_assoc_ies(cfg, ndev);
			wl_update_prof(cfg, ndev, NULL, (const void *)(e->addr.octet),
				WL_PROF_BSSID);
			curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
			wl_update_bss_info(cfg, ndev, false);
			wl_update_pmklist(ndev, cfg->pmk_list, err);
			wl_set_drv_status(cfg, CONNECTED, ndev);
			if (ndev != bcmcfg_to_prmry_ndev(cfg)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
				init_completion(&cfg->iface_disable);
#else
				
				INIT_COMPLETION(cfg->iface_disable);
#endif
			}
#ifdef CUSTOM_SET_CPUCORE
			if (wl_get_chan_isvht80(ndev, dhd)) {
				if (ndev == bcmcfg_to_prmry_ndev(cfg))
					dhd->chan_isvht80 |= DHD_FLAG_STA_MODE; 
				else if (is_p2p_group_iface(ndev->ieee80211_ptr))
					dhd->chan_isvht80 |= DHD_FLAG_P2P_MODE; 
				dhd_set_cpucore(dhd, TRUE);
			}
#endif 

		}
		cfg80211_connect_result(ndev,
			curbssid,
			conn_info->req_ie,
			conn_info->req_ie_len,
			conn_info->resp_ie,
			conn_info->resp_ie_len,
			completed ? WLAN_STATUS_SUCCESS :
			(sec->auth_assoc_res_status) ?
			sec->auth_assoc_res_status :
			WLAN_STATUS_UNSPECIFIED_FAILURE,
			GFP_KERNEL);
		if (completed)
			
#if 0
			WL_INFORM(("Report connect result - connection succeeded\n"));
#else
			WL_ERR(("Report connect result - connection succeeded\n"));
#endif
			
		else
			WL_ERR(("Report connect result - connection failed\n"));
	}
#ifdef CONFIG_TCPACK_FASTTX
	if (wl_get_chan_isvht80(ndev, dhd))
		wldev_iovar_setint(ndev, "tcpack_fast_tx", 0);
	else
		wldev_iovar_setint(ndev, "tcpack_fast_tx", 1);
#endif 

	return err;
}

static s32
wl_notify_mic_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	u16 flags = ntoh16(e->flags);
	enum nl80211_key_type key_type;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	if (flags & WLC_EVENT_MSG_GROUP)
		key_type = NL80211_KEYTYPE_GROUP;
	else
		key_type = NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(ndev, (const u8 *)&e->addr, key_type, -1,
		NULL, GFP_KERNEL);
	mutex_unlock(&cfg->usr_sync);

	return 0;
}

#ifdef BT_WIFI_HANDOVER
static s32
wl_notify_bt_wifi_handover_req(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	u32 event = ntoh32(e->event_type);
	u32 datalen = ntoh32(e->datalen);
	s32 err;

	WL_ERR(("wl_notify_bt_wifi_handover_req: event_type : %d, datalen : %d\n", event, datalen));
	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	err = wl_genl_send_msg(ndev, event, data, (u16)datalen, 0, 0);

	return err;
}
#endif 

#ifdef PNO_SUPPORT
static s32
wl_notify_pfn_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;

	WL_ERR((">>> PNO Event\n"));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);


#ifndef WL_SCHED_SCAN
	mutex_lock(&cfg->usr_sync);
	
	cfg80211_disconnected(ndev, 0, NULL, 0, GFP_KERNEL);
	mutex_unlock(&cfg->usr_sync);
#else
	wl_notify_sched_scan_results(cfg, ndev, e, data);
#endif 
	return 0;
}
#endif 

#ifdef GSCAN_SUPPORT
static s32
wl_notify_gscan_event(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = be32_to_cpu(e->event_type);
	void *ptr;
	int send_evt_bytes = 0;
	int batch_event_result_dummy = 0;
	struct net_device *ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	u32 len = ntoh32(e->datalen);

	switch (event) {
		case WLC_E_PFN_SWC:
			ptr = dhd_dev_swc_scan_event(ndev, data, &send_evt_bytes);
			if (send_evt_bytes) {
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_GSCAN_SIGNIFICANT_EVENT, ptr, send_evt_bytes);
				kfree(ptr);
			}
			break;
		case WLC_E_PFN_BEST_BATCHING:
			err = dhd_dev_retrieve_batch_scan(ndev);
			if (err < 0) {
				WL_ERR(("Batch retrieval already in progress %d\n", err));
			} else {
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_GSCAN_BATCH_SCAN_EVENT,
				     &batch_event_result_dummy, sizeof(int));
			}
			break;
		case WLC_E_PFN_SCAN_COMPLETE:
			batch_event_result_dummy = WIFI_SCAN_COMPLETE;
			wl_cfgvendor_send_async_event(wiphy, ndev,
				GOOGLE_SCAN_COMPLETE_EVENT,
				&batch_event_result_dummy, sizeof(int));
			break;
		case WLC_E_PFN_BSSID_NET_FOUND:
			ptr = dhd_dev_hotlist_scan_event(ndev, data, &send_evt_bytes,
			      HOTLIST_FOUND);
			if (ptr) {
				wl_cfgvendor_send_hotlist_event(wiphy, ndev,
				 ptr, send_evt_bytes, GOOGLE_GSCAN_GEOFENCE_FOUND_EVENT);
				dhd_dev_gscan_hotlist_cache_cleanup(ndev, HOTLIST_FOUND);
			} else {
				err = -ENOMEM;
			}
			break;
		case WLC_E_PFN_BSSID_NET_LOST:
			if (len) {
				ptr = dhd_dev_hotlist_scan_event(ndev, data, &send_evt_bytes,
				            HOTLIST_LOST);
				if (ptr) {
					wl_cfgvendor_send_hotlist_event(wiphy, ndev,
					 ptr, send_evt_bytes, GOOGLE_GSCAN_GEOFENCE_LOST_EVENT);
					dhd_dev_gscan_hotlist_cache_cleanup(ndev, HOTLIST_LOST);
				} else {
					err = -ENOMEM;
				}
			} else {
				err = -EINVAL;
			}
			break;
		case WLC_E_PFN_GSCAN_FULL_RESULT:
			ptr = dhd_dev_process_full_gscan_result(ndev, data, &send_evt_bytes);
			if (ptr) {
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_SCAN_FULL_RESULTS_EVENT, ptr, send_evt_bytes);
				kfree(ptr);
			} else {
				err = -ENOMEM;
			}
			break;
		case WLC_E_PFN_SSID_EXT:
			ptr = dhd_dev_process_epno_result(ndev, data, event, &send_evt_bytes);
			if (ptr) {
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_SCAN_EPNO_EVENT, ptr, send_evt_bytes);
				kfree(ptr);
			} else {
				err = -ENOMEM;
			}
			break;
		case WLC_E_PFN_NET_FOUND:
			break;
		default:
			WL_ERR(("Unknown event %d\n", event));
			break;
	}
	return err;
}
#endif 

static s32
wl_notify_scan_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct channel_info channel_inform;
	struct wl_scan_results *bss_list;
	struct net_device *ndev = NULL;
	u32 len = WL_SCAN_BUF_MAX;
	s32 err = 0;
	unsigned long flags;

	WL_DBG(("Enter \n"));
	if (!wl_get_drv_status(cfg, SCANNING, ndev)) {
		WL_ERR(("scan is not ready \n"));
		return err;
	}
	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	wl_clr_drv_status(cfg, SCANNING, ndev);
	err = wldev_ioctl(ndev, WLC_GET_CHANNEL, &channel_inform,
		sizeof(channel_inform), false);
	if (unlikely(err)) {
		WL_ERR(("scan busy (%d)\n", err));
		goto scan_done_out;
	}
	channel_inform.scan_channel = dtoh32(channel_inform.scan_channel);
	if (unlikely(channel_inform.scan_channel)) {

		WL_DBG(("channel_inform.scan_channel (%d)\n",
			channel_inform.scan_channel));
	}
	cfg->bss_list = cfg->scan_results;
	bss_list = cfg->bss_list;
	memset(bss_list, 0, len);
	bss_list->buflen = htod32(len);
	err = wldev_ioctl(ndev, WLC_SCAN_RESULTS, bss_list, len, false);
	if (unlikely(err) && unlikely(!cfg->scan_suppressed)) {
		WL_ERR(("%s Scan_results error (%d)\n", ndev->name, err));
		err = -EINVAL;
		goto scan_done_out;
	}
	bss_list->buflen = dtoh32(bss_list->buflen);
	bss_list->version = dtoh32(bss_list->version);
	bss_list->count = dtoh32(bss_list->count);

	err = wl_inform_bss(cfg);

scan_done_out:
	del_timer_sync(&cfg->scan_timeout);
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	if (cfg->scan_request) {
		cfg80211_scan_done(cfg->scan_request, false);
		cfg->scan_request = NULL;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	WL_DBG(("cfg80211_scan_done\n"));
	mutex_unlock(&cfg->usr_sync);
	return err;
}

static s32
wl_frame_get_mgmt(u16 fc, const struct ether_addr *da,
	const struct ether_addr *sa, const struct ether_addr *bssid,
	u8 **pheader, u32 *body_len, u8 *pbody)
{
	struct dot11_management_header *hdr;
	u32 totlen = 0;
	s32 err = 0;
	u8 *offset;
	u32 prebody_len = *body_len;
	switch (fc) {
		case FC_ASSOC_REQ:
			
			totlen = DOT11_ASSOC_REQ_FIXED_LEN;
			*body_len += DOT11_ASSOC_REQ_FIXED_LEN;
			break;

		case FC_REASSOC_REQ:
			
			totlen = DOT11_REASSOC_REQ_FIXED_LEN;
			*body_len += DOT11_REASSOC_REQ_FIXED_LEN;
			break;
	}
	totlen += DOT11_MGMT_HDR_LEN + prebody_len;
	*pheader = kzalloc(totlen, GFP_KERNEL);
	if (*pheader == NULL) {
		WL_ERR(("memory alloc failed \n"));
		return -ENOMEM;
	}
	hdr = (struct dot11_management_header *) (*pheader);
	hdr->fc = htol16(fc);
	hdr->durid = 0;
	hdr->seq = 0;
	offset = (u8*)(hdr + 1) + (totlen - DOT11_MGMT_HDR_LEN - prebody_len);
	bcopy((const char*)da, (u8*)&hdr->da, ETHER_ADDR_LEN);
	bcopy((const char*)sa, (u8*)&hdr->sa, ETHER_ADDR_LEN);
	bcopy((const char*)bssid, (u8*)&hdr->bssid, ETHER_ADDR_LEN);
	if ((pbody != NULL) && prebody_len)
		bcopy((const char*)pbody, offset, prebody_len);
	*body_len = totlen;
	return err;
}


void
wl_stop_wait_next_action_frame(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
		if (timer_pending(&cfg->p2p->listen_timer)) {
			del_timer_sync(&cfg->p2p->listen_timer);
		}
		if (cfg->afx_hdl != NULL) {
			if (cfg->afx_hdl->dev != NULL) {
				wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
				wl_clr_drv_status(cfg, FINDING_COMMON_CHANNEL, cfg->afx_hdl->dev);
			}
			cfg->afx_hdl->peer_chan = WL_INVALID;
		}
		complete(&cfg->act_frm_scan);
		WL_DBG(("*** Wake UP ** Working afx searching is cleared\n"));
	} else if (wl_get_drv_status_all(cfg, SENDING_ACT_FRM)) {
		if (!(wl_get_p2p_status(cfg, ACTION_TX_COMPLETED) ||
			wl_get_p2p_status(cfg, ACTION_TX_NOACK)))
			wl_set_p2p_status(cfg, ACTION_TX_COMPLETED);

		WL_DBG(("*** Wake UP ** abort actframe iovar\n"));
		if (cfg->af_sent_channel)
			wl_cfg80211_scan_abort(cfg);
	}
#ifdef WL_CFG80211_SYNC_GON
	else if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM_LISTEN)) {
		WL_DBG(("*** Wake UP ** abort listen for next af frame\n"));
		
		wl_cfg80211_scan_abort(cfg);
	}
#endif 
}


int wl_cfg80211_get_ioctl_version(void)
{
	return ioctl_version;
}

static s32
wl_notify_rx_mgmt_frame(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct ieee80211_supported_band *band;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct ether_addr da;
	struct ether_addr bssid;
	bool isfree = false;
	s32 err = 0;
	s32 freq;
	struct net_device *ndev = NULL;
	wifi_p2p_pub_act_frame_t *act_frm = NULL;
	wifi_p2p_action_frame_t *p2p_act_frm = NULL;
	wifi_p2psd_gas_pub_act_frame_t *sd_act_frm = NULL;
	wl_event_rx_frame_data_t *rxframe =
		(wl_event_rx_frame_data_t*)data;
	u32 event = ntoh32(e->event_type);
	u8 *mgmt_frame;
	u8 bsscfgidx = e->bsscfgidx;
	u32 mgmt_frame_len = ntoh32(e->datalen) - sizeof(wl_event_rx_frame_data_t);
	u16 channel = ((ntoh16(rxframe->channel) & WL_CHANSPEC_CHAN_MASK));

	memset(&bssid, 0, ETHER_ADDR_LEN);

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!band) {
		WL_ERR(("No valid band"));
		return -EINVAL;
	}
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 38) && !defined(WL_COMPAT_WIRELESS)
	freq = ieee80211_channel_to_frequency(channel);
	(void)band->band;
#else
	freq = ieee80211_channel_to_frequency(channel, band->band);
#endif
	if (event == WLC_E_ACTION_FRAME_RX) {
		wldev_iovar_getbuf_bsscfg(ndev, "cur_etheraddr",
			NULL, 0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, bsscfgidx, &cfg->ioctl_buf_sync);

		err = wldev_ioctl(ndev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN, false);
		if (err < 0)
			 WL_ERR(("WLC_GET_BSSID error %d\n", err));
		memcpy(da.octet, cfg->ioctl_buf, ETHER_ADDR_LEN);
		err = wl_frame_get_mgmt(FC_ACTION, &da, &e->addr, &bssid,
			&mgmt_frame, &mgmt_frame_len,
			(u8 *)((wl_event_rx_frame_data_t *)rxframe + 1));
		if (err < 0) {
			WL_ERR(("Error in receiving action frame len %d channel %d freq %d\n",
				mgmt_frame_len, channel, freq));
			goto exit;
		}
		isfree = true;
		if (wl_cfgp2p_is_pub_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
			act_frm = (wifi_p2p_pub_act_frame_t *)
					(&mgmt_frame[DOT11_MGMT_HDR_LEN]);
		} else if (wl_cfgp2p_is_p2p_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
			p2p_act_frm = (wifi_p2p_action_frame_t *)
					(&mgmt_frame[DOT11_MGMT_HDR_LEN]);
			(void) p2p_act_frm;
		} else if (wl_cfgp2p_is_gas_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
#ifdef WL_SDO
			if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
				WL_ERR(("SD offload is in progress. Don't report the"
					"frame via rx_mgmt path\n"));
				goto exit;
			}
#endif

			sd_act_frm = (wifi_p2psd_gas_pub_act_frame_t *)
					(&mgmt_frame[DOT11_MGMT_HDR_LEN]);
			if (sd_act_frm && wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM)) {
				if (cfg->next_af_subtype == sd_act_frm->action) {
					WL_DBG(("We got a right next frame of SD!(%d)\n",
						sd_act_frm->action));
					wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

					
					wl_stop_wait_next_action_frame(cfg, ndev);
				}
			}
			(void) sd_act_frm;
#ifdef WLTDLS
		} else if (mgmt_frame[DOT11_MGMT_HDR_LEN] == TDLS_AF_CATEGORY) {
			WL_DBG((" TDLS Action Frame Received type = %d \n",
				mgmt_frame[DOT11_MGMT_HDR_LEN + 1]));

			if (mgmt_frame[DOT11_MGMT_HDR_LEN + 1] == TDLS_ACTION_SETUP_RESP) {
				cfg->tdls_mgmt_frame = mgmt_frame;
				cfg->tdls_mgmt_frame_len = mgmt_frame_len;
				cfg->tdls_mgmt_freq = freq;
				return 0;
			}

		} else if (mgmt_frame[DOT11_MGMT_HDR_LEN] == TDLS_VENDOR_SPECIFIC) {
			WL_DBG((" TDLS Vendor Specific Received type \n"));
#endif
#ifdef QOS_MAP_SET
		} else if (mgmt_frame[DOT11_MGMT_HDR_LEN] == DOT11_ACTION_CAT_QOS) {
			
			bcm_tlv_t * qos_map_ie = NULL;
			if ((qos_map_ie = bcm_parse_tlvs(&mgmt_frame[DOT11_MGMT_HDR_LEN],
					mgmt_frame_len - DOT11_MGMT_HDR_LEN,
					DOT11_MNG_QOS_MAP_ID)) != NULL) {
				WL_DBG((" QoS map set IE found in QoS action frame\n"));
				if (!cfg->up_table) {
					cfg->up_table = kmalloc(UP_TABLE_MAX, GFP_KERNEL);
				}
				wl_set_up_table(cfg->up_table, qos_map_ie);
			} else {
				kfree(cfg->up_table);
				cfg->up_table = NULL;
			}
#endif 
		} else {


			if (cfg->next_af_subtype != P2P_PAF_SUBTYPE_INVALID) {
				u8 action = 0;
				if (wl_get_public_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
					mgmt_frame_len - DOT11_MGMT_HDR_LEN, &action) != BCME_OK) {
					WL_DBG(("Recived action is not public action frame\n"));
				} else if (cfg->next_af_subtype == action) {
					WL_DBG(("Recived action is the waiting action(%d)\n",
						action));
					wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

					
					wl_stop_wait_next_action_frame(cfg, ndev);
				}
			}
		}

		if (act_frm) {

			if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM)) {
				if (cfg->next_af_subtype == act_frm->subtype) {
					WL_DBG(("We got a right next frame!(%d)\n",
						act_frm->subtype));
					wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

					if (cfg->next_af_subtype == P2P_PAF_GON_CONF) {
						OSL_SLEEP(20);
					}

					
					wl_stop_wait_next_action_frame(cfg, ndev);
				}
			}
		}

		wl_cfgp2p_print_actframe(false, &mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN, channel);
		if (act_frm && ((act_frm->subtype == P2P_PAF_GON_CONF) ||
			(act_frm->subtype == P2P_PAF_PROVDIS_RSP))) {
			wldev_iovar_setint(ndev, "mpc", 1);
		}
		if (act_frm && (act_frm->subtype == P2P_PAF_GON_CONF)) {
			WL_DBG(("P2P: GO_NEG_PHASE status cleared \n"));
			wl_clr_p2p_status(cfg, GO_NEG_PHASE);
		}
	} else if (event == WLC_E_PROBREQ_MSG) {

		struct parsed_ies prbreq_ies;
		u32 prbreq_ie_len = 0;
		bool pbc = 0;

		WL_DBG((" Event WLC_E_PROBREQ_MSG received\n"));
		mgmt_frame = (u8 *)(data);
		mgmt_frame_len = ntoh32(e->datalen);

		prbreq_ie_len = mgmt_frame_len - DOT11_MGMT_HDR_LEN;

		
		if (wl_cfg80211_parse_ies(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			prbreq_ie_len, &prbreq_ies) < 0) {
			WL_ERR(("Prob req get IEs failed\n"));
			return 0;
		}
		if (prbreq_ies.wps_ie != NULL) {
			wl_validate_wps_ie((char *)prbreq_ies.wps_ie, prbreq_ies.wps_ie_len, &pbc);
			WL_DBG((" wps_ie exist pbc = %d\n", pbc));
			
			if (!pbc)
				return 0;
		} else
			return 0;
	} else {
		mgmt_frame = (u8 *)((wl_event_rx_frame_data_t *)rxframe + 1);

		if (event == WLC_E_P2P_PROBREQ_MSG) {
			WL_DBG((" Event %s\n", (event == WLC_E_P2P_PROBREQ_MSG) ?
				"WLC_E_P2P_PROBREQ_MSG":"WLC_E_PROBREQ_MSG"));


			if (cfg->p2p &&
#if defined(P2P_IE_MISSING_FIX)
				cfg->p2p_prb_noti &&
#endif
				wl_get_p2p_status(cfg, GO_NEG_PHASE)) {
				WL_DBG(("Filtering P2P probe_req while "
					"being in GO-Neg state\n"));
				return 0;
			}
		}
	}

	if (discover_cfgdev(cfgdev, cfg))
		WL_DBG(("Rx Managment frame For P2P Discovery Interface \n"));
	else
		WL_DBG(("Rx Managment frame For Iface (%s) \n", ndev->name));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
	cfg80211_rx_mgmt(cfgdev, freq, 0,  mgmt_frame, mgmt_frame_len, 0);
#elif(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
	cfg80211_rx_mgmt(cfgdev, freq, 0,  mgmt_frame, mgmt_frame_len, 0, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || \
	defined(WL_COMPAT_WIRELESS)
	cfg80211_rx_mgmt(cfgdev, freq, 0, mgmt_frame, mgmt_frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_mgmt(cfgdev, freq, mgmt_frame, mgmt_frame_len, GFP_ATOMIC);
#endif 

	WL_DBG(("mgmt_frame_len (%d) , e->datalen (%d), channel (%d), freq (%d)\n",
		mgmt_frame_len, ntoh32(e->datalen), channel, freq));
exit:
	if (isfree)
		kfree(mgmt_frame);
	return 0;
}

#ifdef WL_SCHED_SCAN
#define FULL_ESCAN_ON_PFN_NET_FOUND		0
static s32
wl_notify_sched_scan_results(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	wl_pfn_net_info_t *netinfo, *pnetinfo;
	struct wiphy *wiphy	= bcmcfg_to_wiphy(cfg);
	int err = 0;
	struct cfg80211_scan_request *request = NULL;
	struct cfg80211_ssid ssid[MAX_PFN_LIST_COUNT];
	struct ieee80211_channel *channel = NULL;
	int channel_req = 0;
	int band = 0;
	struct wl_pfn_scanresults *pfn_result = (struct wl_pfn_scanresults *)data;
	uint n_pfn_results = pfn_result->count;
#ifdef CUSTOMER_HW_ONE
	uint len = 0;
#endif 

	WL_DBG(("Enter\n"));

	if ((e->event_type == WLC_E_PFN_NET_LOST) || !data) {
		WL_PNO(("Do Nothing %d\n", e->event_type));
		return 0;
	}
#ifdef CUSTOMER_HW_ONE
	len = sizeof(wl_pfn_scanresults_t) + sizeof(wl_pfn_net_info_t) * (n_pfn_results - 1);
	if (pfn_result->version != PFN_SCANRESULT_VERSION) {
		WL_ERR(("PFN scanresult version mismatch. Do Nothing \n"));
		bcm_print_bytes("pfn_result", (uchar *)data, (len < 512) ? len : 512);
		return 0;
	}
#endif 
	WL_PNO((">>> PFN NET FOUND event. count:%d \n", n_pfn_results));
	if (n_pfn_results > 0) {
		int i;

		if (n_pfn_results > MAX_PFN_LIST_COUNT)
			n_pfn_results = MAX_PFN_LIST_COUNT;
		pnetinfo = (wl_pfn_net_info_t *)(data + sizeof(wl_pfn_scanresults_t)
				- sizeof(wl_pfn_net_info_t));

		memset(&ssid, 0x00, sizeof(ssid));

		request = kzalloc(sizeof(*request)
			+ sizeof(*request->channels) * n_pfn_results,
			GFP_KERNEL);
		channel = (struct ieee80211_channel *)kzalloc(
			(sizeof(struct ieee80211_channel) * n_pfn_results),
			GFP_KERNEL);
		if (!request || !channel) {
			WL_ERR(("No memory"));
			err = -ENOMEM;
			goto out_err;
		}

		request->wiphy = wiphy;

		for (i = 0; i < n_pfn_results; i++) {
			netinfo = &pnetinfo[i];
			if (!netinfo) {
				WL_ERR(("Invalid netinfo ptr. index:%d", i));
				err = -EINVAL;
				goto out_err;
			}
#ifdef CUSTOMER_HW_ONE
			if (netinfo->pfnsubnet.SSID_len > DOT11_MAX_SSID_LEN) {
				WL_ERR(("Invalid SSID len. index:%d", i));
				bcm_print_bytes("pfn_result",
					(uchar *)data, (len < 512) ? len : 512);
				bcm_print_bytes("netinfo",
					(uchar *)netinfo, sizeof(wl_pfn_net_info_t));
				continue;
			}
#endif 
			WL_PNO((">>> SSID:%s Channel:%d \n",
				netinfo->pfnsubnet.SSID, netinfo->pfnsubnet.channel));
			ssid[i].ssid_len = MIN(netinfo->pfnsubnet.SSID_len, DOT11_MAX_SSID_LEN);
			memcpy(ssid[i].ssid, netinfo->pfnsubnet.SSID, ssid[i].ssid_len);
			request->n_ssids++;

			channel_req = netinfo->pfnsubnet.channel;
			band = (channel_req <= CH_MAX_2G_CHANNEL) ? NL80211_BAND_2GHZ
				: NL80211_BAND_5GHZ;
			channel[i].center_freq = ieee80211_channel_to_frequency(channel_req, band);
			channel[i].band = band;
			channel[i].flags |= IEEE80211_CHAN_NO_HT40;
			request->channels[i] = &channel[i];
			request->n_channels++;
		}

		
		if (request->n_ssids)
			request->ssids = &ssid[0];
#ifdef CUSTOMER_HW_ONE
		else {
			WL_ERR(("Invalid SSID count. %d", request->n_ssids));
			bcm_print_bytes("pfn_result", (uchar *)data, (len < 512) ? len : 512);
			err = -EINVAL;
			goto out_err;
		}
#endif 

		if (wl_get_drv_status_all(cfg, SCANNING)) {
			
			wl_notify_escan_complete(cfg, ndev, true, true);
		}

		if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
			WL_PNO((">>> P2P discovery was ON. Disabling it\n"));
			err = wl_cfgp2p_discover_enable_search(cfg, false);
			if (unlikely(err)) {
				wl_clr_drv_status(cfg, SCANNING, ndev);
				goto out_err;
			}
			p2p_scan(cfg) = false;
		}

		wl_set_drv_status(cfg, SCANNING, ndev);
#if FULL_ESCAN_ON_PFN_NET_FOUND
		WL_PNO((">>> Doing Full ESCAN on PNO event\n"));
		err = wl_do_escan(cfg, wiphy, ndev, NULL);
#else
		WL_PNO((">>> Doing targeted ESCAN on PNO event\n"));
		err = wl_do_escan(cfg, wiphy, ndev, request);
#endif
		if (err) {
			wl_clr_drv_status(cfg, SCANNING, ndev);
			goto out_err;
		}
		cfg->sched_scan_running = TRUE;
	}
	else {
		WL_ERR(("FALSE PNO Event. (pfn_count == 0) \n"));
	}
out_err:
	if (request)
		kfree(request);
	if (channel)
		kfree(channel);
	return err;
}
#endif 

static void wl_init_conf(struct wl_conf *conf)
{
	WL_DBG(("Enter \n"));
	conf->frag_threshold = (u32)-1;
	conf->rts_threshold = (u32)-1;
	conf->retry_short = (u32)-1;
	conf->retry_long = (u32)-1;
	conf->tx_power = -1;
}

static void wl_init_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	unsigned long flags;
	struct wl_profile *profile = wl_get_profile_by_netdev(cfg, ndev);

	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	memset(profile, 0, sizeof(struct wl_profile));
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
}

static void wl_init_event_handler(struct bcm_cfg80211 *cfg)
{
	memset(cfg->evt_handler, 0, sizeof(cfg->evt_handler));

	cfg->evt_handler[WLC_E_SCAN_COMPLETE] = wl_notify_scan_status;
	cfg->evt_handler[WLC_E_AUTH] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ASSOC] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_LINK] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_DEAUTH_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_DEAUTH] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_DISASSOC_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ASSOC_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_REASSOC_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ROAM] = wl_notify_roaming_status;
	cfg->evt_handler[WLC_E_MIC_ERROR] = wl_notify_mic_status;
	cfg->evt_handler[WLC_E_SET_SSID] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ACTION_FRAME_RX] = wl_notify_rx_mgmt_frame;
	cfg->evt_handler[WLC_E_PROBREQ_MSG] = wl_notify_rx_mgmt_frame;
	cfg->evt_handler[WLC_E_P2P_PROBREQ_MSG] = wl_notify_rx_mgmt_frame;
	cfg->evt_handler[WLC_E_P2P_DISC_LISTEN_COMPLETE] = wl_cfgp2p_listen_complete;
	cfg->evt_handler[WLC_E_ACTION_FRAME_COMPLETE] = wl_cfgp2p_action_tx_complete;
	cfg->evt_handler[WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE] = wl_cfgp2p_action_tx_complete;
	cfg->evt_handler[WLC_E_JOIN] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_START] = wl_notify_connect_status;
#ifdef PNO_SUPPORT
	cfg->evt_handler[WLC_E_PFN_NET_FOUND] = wl_notify_pfn_status;
#endif 
#ifdef GSCAN_SUPPORT
	cfg->evt_handler[WLC_E_PFN_BEST_BATCHING] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_SCAN_COMPLETE] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_GSCAN_FULL_RESULT] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_SWC] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_BSSID_NET_FOUND] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_BSSID_NET_LOST] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_SSID_EXT] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_GAS_FRAGMENT_RX] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_ROAM_EXP_EVENT] = wl_handle_roam_exp_event;
#endif 
	cfg->evt_handler[WLC_E_RSSI_LQM] = wl_handle_rssi_monitor_event;
#ifdef WL_SDO
	cfg->evt_handler[WLC_E_SERVICE_FOUND] = wl_svc_resp_handler;
	cfg->evt_handler[WLC_E_P2PO_ADD_DEVICE] = wl_notify_device_discovery;
	cfg->evt_handler[WLC_E_P2PO_DEL_DEVICE] = wl_notify_device_discovery;
#endif
#ifdef WLTDLS
	cfg->evt_handler[WLC_E_TDLS_PEER_EVENT] = wl_tdls_event_handler;
#endif 
	cfg->evt_handler[WLC_E_BSSID] = wl_notify_roaming_status;
#ifdef WLAIBSS
	cfg->evt_handler[WLC_E_AIBSS_TXFAIL] = wl_notify_aibss_txfail;
#endif 
#ifdef	WL_RELMCAST
	cfg->evt_handler[WLC_E_RMC_EVENT] = wl_notify_rmc_status;
#endif
#ifdef BT_WIFI_HANDOVER
	cfg->evt_handler[WLC_E_BT_WIFI_HANDOVER_REQ] = wl_notify_bt_wifi_handover_req;
#endif
#ifdef WL_NAN
	cfg->evt_handler[WLC_E_NAN] = wl_cfgnan_notify_nan_status;
	cfg->evt_handler[WLC_E_PROXD] = wl_cfgnan_notify_proxd_status;
#endif 
	cfg->evt_handler[WLC_E_CSA_COMPLETE_IND] = wl_csa_complete_ind;
#ifdef DHD_LOSSLESS_ROAMING
	cfg->evt_handler[WLC_E_ROAM_PREP] = wl_notify_roam_prep_status;
#endif
#if defined(CUSTOMER_HW_ONE) && defined(SOFTAP)
	cfg->evt_handler[WLC_E_RESET_COMPLETE] = wl_cfg80211_custom_event_process;
	cfg->evt_handler[WLC_E_LOAD_IND] = wl_cfg80211_custom_event_process;
	cfg->evt_handler[WLC_E_PRUNE] = wl_cfg80211_custom_event_process;
#endif 
}

#if defined(STATIC_WL_PRIV_STRUCT)
static void
wl_init_escan_result_buf(struct bcm_cfg80211 *cfg)
{
	cfg->escan_info.escan_buf = DHD_OS_PREALLOC(cfg->pub,
		DHD_PREALLOC_WIPHY_ESCAN0, ESCAN_BUF_SIZE);
	bzero(cfg->escan_info.escan_buf, ESCAN_BUF_SIZE);
}

static void
wl_deinit_escan_result_buf(struct bcm_cfg80211 *cfg)
{
	cfg->escan_info.escan_buf = NULL;

}
#endif 

static s32 wl_init_priv_mem(struct bcm_cfg80211 *cfg)
{
	WL_DBG(("Enter \n"));

	cfg->scan_results = (void *)kzalloc(WL_SCAN_BUF_MAX, GFP_KERNEL);
	if (unlikely(!cfg->scan_results)) {
		WL_ERR(("Scan results alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->conf = (void *)kzalloc(sizeof(*cfg->conf), GFP_KERNEL);
	if (unlikely(!cfg->conf)) {
		WL_ERR(("wl_conf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->scan_req_int =
	    (void *)kzalloc(sizeof(*cfg->scan_req_int), GFP_KERNEL);
	if (unlikely(!cfg->scan_req_int)) {
		WL_ERR(("Scan req alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->ioctl_buf = (void *)kzalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (unlikely(!cfg->ioctl_buf)) {
		WL_ERR(("Ioctl buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->escan_ioctl_buf = (void *)kzalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (unlikely(!cfg->escan_ioctl_buf)) {
		WL_ERR(("Ioctl buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->extra_buf = (void *)kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (unlikely(!cfg->extra_buf)) {
		WL_ERR(("Extra buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->pmk_list = (void *)kzalloc(sizeof(*cfg->pmk_list), GFP_KERNEL);
	if (unlikely(!cfg->pmk_list)) {
		WL_ERR(("pmk list alloc failed\n"));
		goto init_priv_mem_out;
	}
#if defined(STATIC_WL_PRIV_STRUCT)
	cfg->conn_info = (void *)kzalloc(sizeof(*cfg->conn_info), GFP_KERNEL);
	if (unlikely(!cfg->conn_info)) {
		WL_ERR(("cfg->conn_info  alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->ie = (void *)kzalloc(sizeof(*cfg->ie), GFP_KERNEL);
	if (unlikely(!cfg->ie)) {
		WL_ERR(("cfg->ie  alloc failed\n"));
		goto init_priv_mem_out;
	}
	wl_init_escan_result_buf(cfg);
#endif 
	cfg->afx_hdl = (void *)kzalloc(sizeof(*cfg->afx_hdl), GFP_KERNEL);
	if (unlikely(!cfg->afx_hdl)) {
		WL_ERR(("afx hdl  alloc failed\n"));
		goto init_priv_mem_out;
	} else {
		init_completion(&cfg->act_frm_scan);
		init_completion(&cfg->wait_next_af);

		INIT_WORK(&cfg->afx_hdl->work, wl_cfg80211_afx_handler);
	}
#ifdef WLTDLS
	if (cfg->tdls_mgmt_frame) {
		kfree(cfg->tdls_mgmt_frame);
		cfg->tdls_mgmt_frame = NULL;
	}
#endif 
	return 0;

init_priv_mem_out:
	wl_deinit_priv_mem(cfg);

	return -ENOMEM;
}

static void wl_deinit_priv_mem(struct bcm_cfg80211 *cfg)
{
	kfree(cfg->scan_results);
	cfg->scan_results = NULL;
	kfree(cfg->conf);
	cfg->conf = NULL;
	kfree(cfg->scan_req_int);
	cfg->scan_req_int = NULL;
	kfree(cfg->ioctl_buf);
	cfg->ioctl_buf = NULL;
	kfree(cfg->escan_ioctl_buf);
	cfg->escan_ioctl_buf = NULL;
	kfree(cfg->extra_buf);
	cfg->extra_buf = NULL;
	kfree(cfg->pmk_list);
	cfg->pmk_list = NULL;
#if defined(STATIC_WL_PRIV_STRUCT)
	kfree(cfg->conn_info);
	cfg->conn_info = NULL;
	kfree(cfg->ie);
	cfg->ie = NULL;
	wl_deinit_escan_result_buf(cfg);
#endif 
	if (cfg->afx_hdl) {
		cancel_work_sync(&cfg->afx_hdl->work);
		kfree(cfg->afx_hdl);
		cfg->afx_hdl = NULL;
	}

}

static s32 wl_create_event_handler(struct bcm_cfg80211 *cfg)
{
	int ret = 0;
	WL_DBG(("Enter \n"));

	
	cfg->event_tsk.thr_pid = -1;

	PROC_START(wl_event_handler, cfg, &cfg->event_tsk, 0, "wl_event_handler");
	if (cfg->event_tsk.thr_pid < 0)
		ret = -ENOMEM;
	return ret;
}

static void wl_destroy_event_handler(struct bcm_cfg80211 *cfg)
{
	if (cfg->event_tsk.thr_pid >= 0)
		PROC_STOP(&cfg->event_tsk);
}

void wl_terminate_event_handler(void)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	if (cfg) {
		wl_destroy_event_handler(cfg);
		wl_flush_eq(cfg);
	}
}

static void wl_scan_timeout(unsigned long data)
{
	wl_event_msg_t msg;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)data;
	struct wireless_dev *wdev = NULL;
	struct net_device *ndev = NULL;
	struct wl_scan_results *bss_list;
	struct wl_bss_info *bi = NULL;
	s32 i;
	u32 channel;
#if defined(DHD_DEBUG) && defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	uint32 prev_memdump_mode = dhdp->memdump_enabled;
#endif 

	if (!(cfg->scan_request)) {
		WL_ERR(("timer expired but no scan request\n"));
		return;
	}

	bss_list = wl_escan_get_buf(cfg, FALSE);
	if (!bss_list) {
		WL_ERR(("bss_list is null. Didn't receive any partial scan results\n"));
	} else {
		WL_ERR(("scanned AP count (%d)\n", bss_list->count));

		bi = next_bss(bss_list, bi);
		for_each_bss(bss_list, bi, i) {
			channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(bi->chanspec));
			WL_ERR(("SSID :%s  Channel :%d\n", bi->SSID, channel));
		}
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	if (cfg->scan_request->dev)
		wdev = cfg->scan_request->dev->ieee80211_ptr;
#else
	wdev = cfg->scan_request->wdev;
#endif 
	if (!wdev) {
		WL_ERR(("No wireless_dev present\n"));
		return;
	}
	ndev = wdev_to_wlc_ndev(wdev, cfg);

	bzero(&msg, sizeof(wl_event_msg_t));
	WL_ERR(("timer expired\n"));
#if defined(DHD_DEBUG) && defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
	if (dhdp->memdump_enabled) {
		DHD_OS_WAKE_LOCK(dhdp);
		dhdp->memdump_enabled = DUMP_MEMFILE;
		dhdp->memdump_type = DUMP_TYPE_SCAN_TIMEOUT;
		dhd_bus_mem_dump(dhdp);
		dhdp->memdump_enabled = prev_memdump_mode;
		DHD_OS_WAKE_UNLOCK(dhdp);
	}
#endif 
	msg.event_type = hton32(WLC_E_ESCAN_RESULT);
	msg.status = hton32(WLC_E_STATUS_TIMEOUT);
	msg.reason = 0xFFFFFFFF;
	wl_cfg80211_event(ndev, &msg, NULL);
}

#ifdef DHD_LOSSLESS_ROAMING
static void wl_del_roam_timeout(struct bcm_cfg80211 *cfg)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	
	dhdp->dequeue_prec_map = ALLPRIO;
	if (timer_pending(&cfg->roam_timeout)) {
		del_timer_sync(&cfg->roam_timeout);
	}

}

static void wl_roam_timeout(unsigned long data)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)data;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	WL_ERR(("roam timer expired\n"));

	
	dhdp->dequeue_prec_map = ALLPRIO;
}

#endif 

static s32
wl_cfg80211_netdev_notifier_call(struct notifier_block * nb,
	unsigned long state, void *ptr)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
	struct net_device *dev = ptr;
#else
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
#endif 
	struct wireless_dev *wdev = ndev_to_wdev(dev);
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	WL_DBG(("Enter \n"));

	if (!wdev || !cfg || dev == bcmcfg_to_prmry_ndev(cfg))
		return NOTIFY_DONE;

	switch (state) {
		case NETDEV_DOWN:
		{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
			int max_wait_timeout = 2;
			int max_wait_count = 100;
			int refcnt = 0;
			unsigned long limit = jiffies + max_wait_timeout * HZ;
			while (work_pending(&wdev->cleanup_work)) {
				if (refcnt%5 == 0) {
					WL_ERR(("[NETDEV_DOWN] wait for "
						"complete of cleanup_work"
						" (%d th)\n", refcnt));
				}
				if (!time_before(jiffies, limit)) {
					WL_ERR(("[NETDEV_DOWN] cleanup_work"
						" of CFG80211 is not"
						" completed in %d sec\n",
						max_wait_timeout));
					break;
				}
				if (refcnt >= max_wait_count) {
					WL_ERR(("[NETDEV_DOWN] cleanup_work"
						" of CFG80211 is not"
						" completed in %d loop\n",
						max_wait_count));
					break;
				}
				set_current_state(TASK_INTERRUPTIBLE);
				(void)schedule_timeout(100);
				set_current_state(TASK_RUNNING);
				refcnt++;
			}
#endif 
			break;
		}
		case NETDEV_UNREGISTER:
			
			wl_cfg80211_clear_per_bss_ies(cfg,
				wl_get_bssidx_by_wdev(cfg, wdev));
			wl_dealloc_netinfo_by_wdev(cfg, wdev);
			break;
		case NETDEV_GOING_DOWN:
			if (wl_get_drv_status(cfg, SCANNING, dev))
				wl_notify_escan_complete(cfg, dev, true, true);
			break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block wl_cfg80211_netdev_notifier = {
	.notifier_call = wl_cfg80211_netdev_notifier_call,
};

static bool wl_cfg80211_netdev_notifier_registered = FALSE;

static void wl_cfg80211_cancel_scan(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev = NULL;
	struct net_device *ndev = NULL;

	if (!cfg->scan_request)
		return;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	if (cfg->scan_request->dev)
		wdev = cfg->scan_request->dev->ieee80211_ptr;
#else
	wdev = cfg->scan_request->wdev;
#endif 

	if (!wdev) {
		WL_ERR(("No wireless_dev present\n"));
		return;
	}

	ndev = wdev_to_wlc_ndev(wdev, cfg);
	wl_notify_escan_complete(cfg, ndev, true, true);
	WL_ERR(("Scan aborted! \n"));
}

static void wl_cfg80211_scan_abort(struct bcm_cfg80211 *cfg)
{
	wl_scan_params_t *params = NULL;
	s32 params_size = 0;
	s32 err = BCME_OK;
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	if (!in_atomic()) {
		
		params = wl_cfg80211_scan_alloc_params(-1, 0, &params_size);
		if (params == NULL) {
			WL_ERR(("scan params allocation failed \n"));
			err = -ENOMEM;
		} else {
			
			err = wldev_ioctl(dev, WLC_SCAN, params, params_size, true);
			if (err < 0) {
				WL_ERR(("scan abort  failed \n"));
			}
			kfree(params);
		}
	}
#ifdef WLTDLS
	if (cfg->tdls_mgmt_frame) {
		kfree(cfg->tdls_mgmt_frame);
		cfg->tdls_mgmt_frame = NULL;
	}
#endif 
}

static s32 wl_notify_escan_complete(struct bcm_cfg80211 *cfg,
	struct net_device *ndev,
	bool aborted, bool fw_abort)
{
	s32 err = BCME_OK;
	unsigned long flags;
	struct net_device *dev;

	WL_DBG(("Enter \n"));

	mutex_lock(&cfg->scan_complete);

	if (!ndev) {
		WL_ERR(("ndev is null\n"));
		err = BCME_ERROR;
		goto out;
	}

	if (cfg->escan_info.ndev != ndev) {
		WL_ERR(("ndev is different %p %p\n", cfg->escan_info.ndev, ndev));
		err = BCME_ERROR;
		goto out;
	}

	if (cfg->scan_request) {
		dev = bcmcfg_to_prmry_ndev(cfg);
#if defined(WL_ENABLE_P2P_IF)
		if (cfg->scan_request->dev != cfg->p2p_net)
			dev = cfg->scan_request->dev;
#elif defined(WL_CFG80211_P2P_DEV_IF)
		if (cfg->scan_request->wdev->iftype != NL80211_IFTYPE_P2P_DEVICE)
			dev = cfg->scan_request->wdev->netdev;
#endif 
	}
	else {
		WL_DBG(("cfg->scan_request is NULL may be internal scan."
			"doing scan_abort for ndev %p primary %p",
				ndev, bcmcfg_to_prmry_ndev(cfg)));
		dev = ndev;
	}
	if (fw_abort && !in_atomic())
		wl_cfg80211_scan_abort(cfg);
	if (timer_pending(&cfg->scan_timeout))
		del_timer_sync(&cfg->scan_timeout);
#if defined(ESCAN_RESULT_PATCH)
	if (likely(cfg->scan_request)) {
		cfg->bss_list = wl_escan_get_buf(cfg, aborted);
		wl_inform_bss(cfg);
	}
#endif 
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
#ifdef WL_SCHED_SCAN
	if (cfg->sched_scan_req && !cfg->scan_request) {
		WL_PNO((">>> REPORTING SCHED SCAN RESULTS \n"));
		if (!aborted)
			cfg80211_sched_scan_results(cfg->sched_scan_req->wiphy);
		cfg->sched_scan_running = FALSE;
		cfg->sched_scan_req = NULL;
	}
#endif 
	if (likely(cfg->scan_request)) {
		cfg80211_scan_done(cfg->scan_request, aborted);
		cfg->scan_request = NULL;
		DHD_ENABLE_RUNTIME_PM((dhd_pub_t *)(cfg->pub));
	}
	if (p2p_is_on(cfg))
		wl_clr_p2p_status(cfg, SCANNING);
	wl_clr_drv_status(cfg, SCANNING, dev);
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
#ifdef WL_SDO
	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS) && !in_atomic()) {
		wl_cfg80211_resume_sdo(ndev, cfg);
	}
#endif

#ifdef CUSTOMER_HW_ONE
	if (disc_called) {
		if (!scan_one)
			scan_one = true;
		else {
			wl_cfg80211_extra_disc();
			disc_called = false;
			scan_one = false;
		}
	}
#endif 

out:
	mutex_unlock(&cfg->scan_complete);
	return err;
}

#ifdef ESCAN_BUF_OVERFLOW_MGMT
static void
wl_cfg80211_find_removal_candidate(wl_bss_info_t *bss, removal_element_t *candidate)
{
	int idx;
	for (idx = 0; idx < BUF_OVERFLOW_MGMT_COUNT; idx++) {
		int len = BUF_OVERFLOW_MGMT_COUNT - idx - 1;
		if (bss->RSSI < candidate[idx].RSSI) {
			if (len)
				memcpy(&candidate[idx + 1], &candidate[idx],
					sizeof(removal_element_t) * len);
			candidate[idx].RSSI = bss->RSSI;
			candidate[idx].length = bss->length;
			memcpy(&candidate[idx].BSSID, &bss->BSSID, ETHER_ADDR_LEN);
			return;
		}
	}
}

static void
wl_cfg80211_remove_lowRSSI_info(wl_scan_results_t *list, removal_element_t *candidate,
	wl_bss_info_t *bi)
{
	int idx1, idx2;
	int total_delete_len = 0;
	for (idx1 = 0; idx1 < BUF_OVERFLOW_MGMT_COUNT; idx1++) {
		int cur_len = WL_SCAN_RESULTS_FIXED_SIZE;
		wl_bss_info_t *bss = NULL;
		if (candidate[idx1].RSSI >= bi->RSSI)
			continue;
		for (idx2 = 0; idx2 < list->count; idx2++) {
			bss = bss ? (wl_bss_info_t *)((uintptr)bss + dtoh32(bss->length)) :
				list->bss_info;
			if (!bcmp(&candidate[idx1].BSSID, &bss->BSSID, ETHER_ADDR_LEN) &&
				candidate[idx1].RSSI == bss->RSSI &&
				candidate[idx1].length == dtoh32(bss->length)) {
				u32 delete_len = dtoh32(bss->length);
				WL_DBG(("delete scan info of " MACDBG " to add new AP\n",
					MAC2STRDBG(bss->BSSID.octet)));
				if (idx2 < list->count -1) {
					memmove((u8 *)bss, (u8 *)bss + delete_len,
						list->buflen - cur_len - delete_len);
				}
				list->buflen -= delete_len;
				list->count--;
				total_delete_len += delete_len;
				
				if (total_delete_len >= bi->length) {
					return;
				}
				break;
			}
			cur_len += dtoh32(bss->length);
		}
	}
}
#endif 

#if defined(DHD_DEBUG) && defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
static int last_scaned_ap_count = 0;
static int current_scaned_ap_count = 0;
#endif 

static s32 wl_escan_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = BCME_OK;
	s32 status = ntoh32(e->status);
	wl_bss_info_t *bi;
	wl_escan_result_t *escan_result;
	wl_bss_info_t *bss = NULL;
	wl_scan_results_t *list;
	wifi_p2p_ie_t * p2p_ie;
	struct net_device *ndev = NULL;
	u32 bi_length;
	u32 i;
	u8 *p2p_dev_addr = NULL;

	WL_DBG((" enter event type : %d, status : %d \n",
		ntoh32(e->event_type), ntoh32(e->status)));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	
	if (wl_get_p2p_status(cfg, SCANNING)) {
		if (wl_get_drv_status_all(cfg, SENDING_ACT_FRM))
			ndev = cfg->afx_hdl->dev;
		else
			ndev = cfg->escan_info.ndev;

	}
	if (!ndev || (!wl_get_drv_status(cfg, SCANNING, ndev) && !cfg->sched_scan_running)) {
		WL_ERR(("escan is not ready ndev %p drv_status 0x%x e_type %d e_states %d\n",
			ndev, wl_get_drv_status(cfg, SCANNING, ndev),
			ntoh32(e->event_type), ntoh32(e->status)));
		goto exit;
	}
	escan_result = (wl_escan_result_t *)data;

	if (status == WLC_E_STATUS_PARTIAL) {
		WL_INFORM(("WLC_E_STATUS_PARTIAL \n"));
		if (!escan_result) {
			WL_ERR(("Invalid escan result (NULL pointer)\n"));
			goto exit;
		}
		if (dtoh16(escan_result->bss_count) != 1) {
			WL_ERR(("Invalid bss_count %d: ignoring\n", escan_result->bss_count));
			goto exit;
		}
		bi = escan_result->bss_info;
		if (!bi) {
			WL_ERR(("Invalid escan bss info (NULL pointer)\n"));
			goto exit;
		}
		bi_length = dtoh32(bi->length);
		if (bi_length != (dtoh32(escan_result->buflen) - WL_ESCAN_RESULTS_FIXED_SIZE)) {
			WL_ERR(("Invalid bss_info length %d: ignoring\n", bi_length));
			goto exit;
		}
		if (wl_escan_check_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id) < 0)
			goto exit;

		if (!(bcmcfg_to_wiphy(cfg)->interface_modes & BIT(NL80211_IFTYPE_ADHOC))) {
			if (dtoh16(bi->capability) & DOT11_CAP_IBSS) {
				WL_DBG(("Ignoring IBSS result\n"));
				goto exit;
			}
		}

		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			p2p_dev_addr = wl_cfgp2p_retreive_p2p_dev_addr(bi, bi_length);
			if (p2p_dev_addr && !memcmp(p2p_dev_addr,
				cfg->afx_hdl->tx_dst_addr.octet, ETHER_ADDR_LEN)) {
				s32 channel = wf_chspec_ctlchan(
					wl_chspec_driver_to_host(bi->chanspec));

				if ((channel > MAXCHANNEL) || (channel <= 0))
					channel = WL_INVALID;
				else
					WL_ERR(("ACTION FRAME SCAN : Peer " MACDBG " found,"
						" channel : %d\n",
						MAC2STRDBG(cfg->afx_hdl->tx_dst_addr.octet),
						channel));

				wl_clr_p2p_status(cfg, SCANNING);
				cfg->afx_hdl->peer_chan = channel;
				complete(&cfg->act_frm_scan);
				goto exit;
			}

		} else {
			int cur_len = WL_SCAN_RESULTS_FIXED_SIZE;
#ifdef ESCAN_BUF_OVERFLOW_MGMT
			removal_element_t candidate[BUF_OVERFLOW_MGMT_COUNT];
			int remove_lower_rssi = FALSE;

			bzero(candidate, sizeof(removal_element_t)*BUF_OVERFLOW_MGMT_COUNT);
#endif 

			list = wl_escan_get_buf(cfg, FALSE);
			if (scan_req_match(cfg)) {
#ifdef WL_HOST_BAND_MGMT
				s32 channel = 0;
				s32 channel_band = 0;
				chanspec_t chspec;
#endif 
				
				if ((cfg->p2p->search_state != WL_P2P_DISC_ST_SCAN) &&
					(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
					goto exit;
				if ((p2p_ie = wl_cfgp2p_find_p2pie(((u8 *) bi) + bi->ie_offset,
					bi->ie_length)) == NULL) {
						WL_ERR(("Couldn't find P2PIE in probe"
							" response/beacon\n"));
						goto exit;
				}
#ifdef WL_HOST_BAND_MGMT
				chspec = wl_chspec_driver_to_host(bi->chanspec);
				channel = wf_chspec_ctlchan(chspec);
				channel_band = CHSPEC2WLC_BAND(chspec);

				if ((cfg->curr_band == WLC_BAND_5G) &&
					(channel_band == WLC_BAND_2G)) {
					
					if (wl_cfgp2p_retreive_p2pattrib(p2p_ie,
						P2P_SEID_GROUP_ID) != NULL)
						goto exit;
				}
#endif 
			}
#ifdef ESCAN_BUF_OVERFLOW_MGMT
			if (bi_length > ESCAN_BUF_SIZE - list->buflen)
				remove_lower_rssi = TRUE;
#endif 

			for (i = 0; i < list->count; i++) {
				bss = bss ? (wl_bss_info_t *)((uintptr)bss + dtoh32(bss->length))
					: list->bss_info;
#ifdef ESCAN_BUF_OVERFLOW_MGMT
				WL_TRACE(("%s("MACDBG"), i=%d bss: RSSI %d list->count %d\n",
					bss->SSID, MAC2STRDBG(bss->BSSID.octet),
					i, bss->RSSI, list->count));

				if (remove_lower_rssi)
					wl_cfg80211_find_removal_candidate(bss, candidate);
#endif 

#ifdef CUSTOMER_HW_ONE
				if ((!bcmp(&bi->BSSID, &bss->BSSID, ETHER_ADDR_LEN)) &&
					(!bi->RSSI)) {
					WL_ERR(("%s : bi->ssid[%s]("MACDBG"), "
					"bi RSSI ZERO not update it "
					"bi: RSSI %d bss->SSID [%s]("MACDBG"), "
					"bss: RSSI %d\n", __FUNCTION__,
					bi->SSID, MAC2STRDBG(bi->BSSID.octet), bi->RSSI,
					bss->SSID, MAC2STRDBG(bss->BSSID.octet), bss->RSSI));
					continue;
				}
#endif 
				if (!bcmp(&bi->BSSID, &bss->BSSID, ETHER_ADDR_LEN) &&
					(CHSPEC_BAND(wl_chspec_driver_to_host(bi->chanspec))
					== CHSPEC_BAND(wl_chspec_driver_to_host(bss->chanspec))) &&
					bi->SSID_len == bss->SSID_len &&
					!bcmp(bi->SSID, bss->SSID, bi->SSID_len)) {

					if (!(bss->flags & WL_BSS_FLAGS_FROM_BEACON) &&
						(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
						goto exit;

					WL_SCAN(("%s("MACDBG"), i=%d prev: RSSI %d"
						" flags 0x%x, new: RSSI %d flags 0x%x\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet), i,
						bss->RSSI, bss->flags, bi->RSSI, bi->flags));

					if ((bss->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) ==
						(bi->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL)) {
						WL_SCAN(("%s("MACDBG"), same onchan"
						", RSSI: prev %d new %d\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						bss->RSSI, bi->RSSI));
						bi->RSSI = MAX(bss->RSSI, bi->RSSI);
					} else if ((bss->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) &&
						(bi->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) == 0) {
						WL_SCAN(("%s("MACDBG"), prev onchan"
						", RSSI: prev %d new %d\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						bss->RSSI, bi->RSSI));
						bi->RSSI = bss->RSSI;
						bi->flags |= WL_BSS_FLAGS_RSSI_ONCHANNEL;
					}
					if (dtoh32(bss->length) != bi_length) {
						u32 prev_len = dtoh32(bss->length);

						WL_SCAN(("bss info replacement"
							" is occured(bcast:%d->probresp%d)\n",
							bss->ie_length, bi->ie_length));
						WL_DBG(("%s("MACDBG"), replacement!(%d -> %d)\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						prev_len, bi_length));

						if (list->buflen - prev_len + bi_length
							> ESCAN_BUF_SIZE) {
							WL_ERR(("Buffer is too small: keep the"
								" previous result of this AP\n"));
							
							bss->RSSI = bi->RSSI;
							bss->flags |= (bi->flags
								& WL_BSS_FLAGS_RSSI_ONCHANNEL);
							goto exit;
						}

						if (i < list->count - 1) {
							
							memmove((u8 *)bss + bi_length,
								(u8 *)bss + prev_len,
								list->buflen - cur_len - prev_len);
						}
						list->buflen -= prev_len;
						list->buflen += bi_length;
					}
					list->version = dtoh32(bi->version);
					memcpy((u8 *)bss, (u8 *)bi, bi_length);
					goto exit;
				}
				cur_len += dtoh32(bss->length);
			}
			if (bi_length > ESCAN_BUF_SIZE - list->buflen) {
#ifdef ESCAN_BUF_OVERFLOW_MGMT
				wl_cfg80211_remove_lowRSSI_info(list, candidate, bi);
				if (bi_length > ESCAN_BUF_SIZE - list->buflen) {
					WL_DBG(("RSSI(" MACDBG ") is too low(%d) to add Buffer\n",
						MAC2STRDBG(bi->BSSID.octet), bi->RSSI));
					goto exit;
				}
#else
				WL_ERR(("Buffer is too small: ignoring\n"));
				goto exit;
#endif 
			}
#ifdef CUSTOMER_HW_ONE
			if (!bi->RSSI) {
				WL_ERR(("%s("MACDBG"), bi RSSI ZERO (%d) not update\n",
					bi->SSID, MAC2STRDBG(bi->BSSID.octet), bi->RSSI));
				goto exit;
			}
#endif 

			memcpy(&(((char *)list)[list->buflen]), bi, bi_length);
			list->version = dtoh32(bi->version);
			list->buflen += bi_length;
			list->count++;

			if (wl_cfgp2p_is_p2p_specific_scan(cfg->scan_request)) {
				WL_ERR(("P2P assoc scan fast aborted.\n"));
				wl_notify_escan_complete(cfg, cfg->escan_info.ndev, false, true);
				goto exit;
			}
		}
	}
	else if (status == WLC_E_STATUS_SUCCESS) {
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, cfg->escan_info.cur_sync_id,
			escan_result->sync_id);

		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_INFORM(("ACTION FRAME SCAN DONE\n"));
			wl_clr_p2p_status(cfg, SCANNING);
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			WL_ERR(("ESCAN COMPLETED\n"));
			cfg->bss_list = wl_escan_get_buf(cfg, FALSE);
			if (!scan_req_match(cfg)) {
				WL_ERR(("SCAN COMPLETED: scanned AP count=%d\n",
					cfg->bss_list->count));

				
#if defined(DHD_DEBUG) && defined(BCMPCIE) && defined(DHD_FW_COREDUMP) && defined(CUSTOMER_HW_ONE)
				current_scaned_ap_count = cfg->bss_list->count;
				if ((last_scaned_ap_count != 0) && (current_scaned_ap_count == 0) && (get_radio_flag() & BIT(3))) {
					dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
					uint32 prev_memdump_mode = dhdp->memdump_enabled;
					if (dhdp->memdump_enabled) {
						DHD_OS_WAKE_LOCK(dhdp);
						dhdp->memdump_enabled = DUMP_MEMFILE;
						dhdp->memdump_type = DUMP_TYPE_SCAN_AP_COUNT_ZERO;
						dhd_bus_mem_dump(dhdp);
						dhdp->memdump_enabled = prev_memdump_mode;
						DHD_OS_WAKE_UNLOCK(dhdp);
					}
				}
				last_scaned_ap_count = current_scaned_ap_count;
#endif 
				
			}
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, false, false);
		}
		wl_escan_increment_sync_id(cfg, SCAN_BUF_NEXT);
	}
	else if (status == WLC_E_STATUS_ABORT) {
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id);
		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_INFORM(("ACTION FRAME SCAN DONE\n"));
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			wl_clr_p2p_status(cfg, SCANNING);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			WL_ERR(("ESCAN ABORTED\n"));
			cfg->bss_list = wl_escan_get_buf(cfg, TRUE);
			if (!scan_req_match(cfg)) {
				WL_ERR(("SCAN ABORTED: scanned AP count=%d\n",
					cfg->bss_list->count));
			}
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, true, false);
		}
		wl_escan_increment_sync_id(cfg, SCAN_BUF_CNT);
	} else if (status == WLC_E_STATUS_NEWSCAN) {
		WL_ERR(("WLC_E_STATUS_NEWSCAN : scan_request[%p]\n", cfg->scan_request));
		WL_ERR(("sync_id[%d], bss_count[%d]\n", escan_result->sync_id,
			escan_result->bss_count));
	} else if (status == WLC_E_STATUS_TIMEOUT) {
		WL_ERR(("WLC_E_STATUS_TIMEOUT : scan_request[%p]\n", cfg->scan_request));
		WL_ERR(("reason[0x%x]\n", e->reason));
		if (e->reason == 0xFFFFFFFF) {
			wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
		}
	} else {
		WL_ERR(("unexpected Escan Event %d : abort\n", status));
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id);
		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_INFORM(("ACTION FRAME SCAN DONE\n"));
			wl_clr_p2p_status(cfg, SCANNING);
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			cfg->bss_list = wl_escan_get_buf(cfg, TRUE);
			if (!scan_req_match(cfg)) {
				WL_ERR(("SCAN ABORTED(UNEXPECTED): "
					"scanned AP count=%d\n",
					cfg->bss_list->count));
			}
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, true, false);
		}
		wl_escan_increment_sync_id(cfg, 2);
	}
exit:
	mutex_unlock(&cfg->usr_sync);
	return err;
}

static void wl_cfg80211_concurrent_roam(struct bcm_cfg80211 *cfg, int enable)
{
	u32 connected_cnt  = wl_get_drv_status_all(cfg, CONNECTED);
	struct net_info *iter, *next;
	int err;

	if (!cfg->roamoff_on_concurrent)
		return;
	if (enable && connected_cnt > 1) {
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
		for_each_ndev(cfg, iter, next) {

			if (iter->ndev) {
				
				if ((err = wldev_iovar_getint(iter->ndev, "roam_off",
					(s32 *)&iter->roam_off)) != BCME_OK) {
					WL_ERR(("%s:Failed to get current roam setting err %d\n",
						iter->ndev->name, err));
					continue;
				}
				if ((err = wldev_iovar_setint(iter->ndev,
						 "roam_off", 1)) != BCME_OK) {
					WL_ERR((" %s:failed to set roam_off : %d\n",
						iter->ndev->name, err));
				}
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	}
	else if (!enable) {
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
		for_each_ndev(cfg, iter, next) {
			if (iter->ndev) {
				if (iter->roam_off != WL_INVALID) {
					if ((err = wldev_iovar_setint(iter->ndev, "roam_off",
						iter->roam_off)) == BCME_OK)
						iter->roam_off = WL_INVALID;
					else {
						WL_ERR((" %s:failed to set roam_off : %d\n",
							iter->ndev->name, err));
					}
				}
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	}
	return;
}

static void wl_cfg80211_determine_vsdb_mode(struct bcm_cfg80211 *cfg)
{
	struct net_info *iter, *next;
	u32 ctl_chan = 0;
	u32 chanspec = 0;
	u32 pre_ctl_chan = 0;
	u32 connected_cnt  = wl_get_drv_status_all(cfg, CONNECTED);
	cfg->vsdb_mode = false;

	if (connected_cnt <= 1)  {
		return;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		
		if (iter->ndev) {
			chanspec = 0;
			ctl_chan = 0;
			if (wl_get_drv_status(cfg, CONNECTED, iter->ndev)) {
				if (wldev_iovar_getint(iter->ndev, "chanspec",
					(s32 *)&chanspec) == BCME_OK) {
					chanspec = wl_chspec_driver_to_host(chanspec);
					ctl_chan = wf_chspec_ctlchan(chanspec);
					wl_update_prof(cfg, iter->ndev, NULL,
						&ctl_chan, WL_PROF_CHAN);
				}
				if (!cfg->vsdb_mode) {
					if (!pre_ctl_chan && ctl_chan)
						pre_ctl_chan = ctl_chan;
					else if (pre_ctl_chan && (pre_ctl_chan != ctl_chan)) {
						cfg->vsdb_mode = true;
					}
				}
			}
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	WL_ERR(("%s concurrency is enabled\n", cfg->vsdb_mode ? "Multi Channel" : "Same Channel"));
	return;
}


static s32 wl_notifier_change_state(struct bcm_cfg80211 *cfg, struct net_info *_net_info,
	enum wl_status state, bool set)
{
	s32 pm = PM_FAST;
	s32 err = BCME_OK;
	u32 mode;
	u32 chan = 0;
	struct net_info *iter, *next;
	struct net_device *primary_dev = bcmcfg_to_prmry_ndev(cfg);
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	if (dhd->busstate == DHD_BUS_DOWN) {
		WL_ERR(("%s : busstate is DHD_BUS_DOWN!\n", __FUNCTION__));
		return 0;
	}
	WL_DBG(("Enter state %d set %d _net_info->pm_restore %d iface %s\n",
		state, set, _net_info->pm_restore, _net_info->ndev->name));

	if (state != WL_STATUS_CONNECTED)
		return 0;
	mode = wl_get_mode_by_netdev(cfg, _net_info->ndev);
	if (set) {
		wl_cfg80211_concurrent_roam(cfg, 1);

		if (mode == WL_MODE_AP) {

			if (wl_add_remove_eventmsg(primary_dev, WLC_E_P2P_PROBREQ_MSG, false))
				WL_ERR((" failed to unset WLC_E_P2P_PROPREQ_MSG\n"));
		}
		wl_cfg80211_determine_vsdb_mode(cfg);
		if (cfg->vsdb_mode || _net_info->pm_block) {
			
			wl_add_remove_pm_enable_work(cfg, FALSE, WL_HANDLER_MAINTAIN);
			if (!_net_info->pm_block && (mode == WL_MODE_BSS)) {
				_net_info->pm = PM_FAST;
				_net_info->pm_restore = true;
			}
			pm = PM_OFF;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
			for_each_ndev(cfg, iter, next) {
				if (iter->pm_restore || (iter->ndev == NULL))
					continue;
				
				err = wldev_ioctl(iter->ndev, WLC_GET_PM, &iter->pm,
					sizeof(iter->pm), false);
				WL_DBG(("%s:power save %s\n", iter->ndev->name,
					iter->pm ? "enabled" : "disabled"));
				if (!err && iter->pm) {
					iter->pm_restore = true;
				}
			}
			for_each_ndev(cfg, iter, next) {
				if (iter->ndev) {
					if (!wl_get_drv_status(cfg, CONNECTED, iter->ndev))
						continue;
					if ((err = wldev_ioctl(iter->ndev, WLC_SET_PM, &pm,
						sizeof(pm), true)) != 0) {
						if (err == -ENODEV)
							WL_DBG(("%s:netdev not ready\n",
								iter->ndev->name));
						else
							WL_ERR(("%s:error (%d)\n",
								iter->ndev->name, err));
						wl_cfg80211_update_power_mode(iter->ndev);
					}
				}
			}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
		} else {

			pm = PM_OFF;
			if (!_net_info->pm_block) {
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
				for_each_ndev(cfg, iter, next) {
					if (iter->pm_restore || (iter->ndev == NULL))
						continue;
					
					err = wldev_ioctl(iter->ndev, WLC_GET_PM, &iter->pm,
						sizeof(iter->pm), false);
					WL_DBG(("%s:power save %s\n", iter->ndev->name,
						iter->pm ? "enabled" : "disabled"));
					if (!err && iter->pm) {
						iter->pm_restore = true;
					}
				}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
			}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
			for_each_ndev(cfg, iter, next) {
				
				if (iter->ndev) {
					if (!wl_get_drv_status(cfg, CONNECTED, iter->ndev))
						continue;
					if ((err = wldev_ioctl(iter->ndev, WLC_SET_PM, &pm,
						sizeof(pm), true)) != 0) {
						if (err == -ENODEV)
							WL_DBG(("%s:netdev not ready\n",
								iter->ndev->name));
						else
							WL_ERR(("%s:error (%d)\n",
								iter->ndev->name, err));
					}
				}
			}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
			if (cfg->pm_enable_work_on) {
				wl_add_remove_pm_enable_work(cfg, FALSE, WL_HANDLER_DEL);
			}

			cfg->pm_enable_work_on = true;
			wl_add_remove_pm_enable_work(cfg, TRUE, WL_HANDLER_NOTUSE);
		}
#if defined(WLTDLS)
#if defined(DISABLE_TDLS_IN_P2P)
		if (cfg->vsdb_mode || p2p_is_on(cfg))
#else
		if (cfg->vsdb_mode)
#endif 
		{

			err = wldev_iovar_setint(primary_dev, "tdls_enable", 0);
		}
#endif 

#ifdef CUSTOMER_HW_ONE
		if (cfg->vsdb_mode) {
			int val, ret;

			
			val = 8;
			ret = wldev_iovar_setint(primary_dev, "ampdu_ba_wsize", val);
			if (ret < 0) {
				WL_ERR(("set ampdu ba window size to %d failed. error %d\n",
					val, ret));
			}
		}
#endif
	}
	 else { 
		chan = 0;
		
		wl_update_prof(cfg, _net_info->ndev, NULL, &chan, WL_PROF_CHAN);
		wl_cfg80211_determine_vsdb_mode(cfg);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
		for_each_ndev(cfg, iter, next) {
			
			if (iter->ndev) {
				if (iter->pm_restore && iter->pm) {
					WL_DBG(("%s:restoring power save %s\n", iter->ndev->name,
						(iter->pm ? "enabled" : "disabled")));
					err = wldev_ioctl(iter->ndev,
						WLC_SET_PM, &iter->pm, sizeof(iter->pm), true);
					if (unlikely(err)) {
						if (err == -ENODEV)
							WL_DBG(("%s:netdev not ready\n",
								iter->ndev->name));
						else
							WL_ERR(("%s:error(%d)\n",
								iter->ndev->name, err));
						break;
					}
					iter->pm_restore = 0;
					wl_cfg80211_update_power_mode(iter->ndev);
				}
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
		wl_cfg80211_concurrent_roam(cfg, 0);
#if defined(WLTDLS)
		if (!cfg->vsdb_mode) {
			err = wldev_iovar_setint(primary_dev, "tdls_enable", 1);
		}
#endif 

#ifdef CUSTOMER_HW_ONE
		if (!cfg->vsdb_mode) {
			int val, ret;

			
#ifdef CUSTOM_AMPDU_BA_WSIZE
			val = CUSTOM_AMPDU_BA_WSIZE;
#else
			val = 64;
#endif 
			ret = wldev_iovar_setint(primary_dev, "ampdu_ba_wsize", val);
			if (ret < 0) {
				WL_ERR(("set ampdu ba window size to %d failed. error %d\n",
					val, ret));
			}
		}
#endif 
	}
	return err;
}
static s32 wl_init_scan(struct bcm_cfg80211 *cfg)
{
	int err = 0;

	cfg->evt_handler[WLC_E_ESCAN_RESULT] = wl_escan_handler;
	cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
	wl_escan_init_sync_id(cfg);

	
	init_timer(&cfg->scan_timeout);
	cfg->scan_timeout.data = (unsigned long) cfg;
	cfg->scan_timeout.function = wl_scan_timeout;

	return err;
}

#ifdef DHD_LOSSLESS_ROAMING
static s32 wl_init_roam_timeout(struct bcm_cfg80211 *cfg)
{
	int err = 0;

	
	init_timer(&cfg->roam_timeout);
	cfg->roam_timeout.data = (unsigned long) cfg;
	cfg->roam_timeout.function = wl_roam_timeout;

	return err;
}
#endif 

static s32 wl_init_priv(struct bcm_cfg80211 *cfg)
{
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = 0;

	cfg->scan_request = NULL;
	cfg->pwr_save = !!(wiphy->flags & WIPHY_FLAG_PS_ON_BY_DEFAULT);
	cfg->roam_on = false;
	cfg->active_scan = true;
	cfg->rf_blocked = false;
	cfg->vsdb_mode = false;
#if defined(BCMSDIO)
	cfg->wlfc_on = false;
#endif 
	cfg->roamoff_on_concurrent = true;
	cfg->disable_roam_event = false;
	cfg->cfgdev_bssidx = -1;
	
	set_bit(WL_STATUS_CONNECTED, &cfg->interrested_state);
	spin_lock_init(&cfg->cfgdrv_lock);
	mutex_init(&cfg->ioctl_buf_sync);
	init_waitqueue_head(&cfg->netif_change_event);
	init_completion(&cfg->send_af_done);
	init_completion(&cfg->iface_disable);
	wl_init_eq(cfg);
	err = wl_init_priv_mem(cfg);
	if (err)
		return err;
	if (wl_create_event_handler(cfg))
		return -ENOMEM;
	wl_init_event_handler(cfg);
	mutex_init(&cfg->usr_sync);
	mutex_init(&cfg->event_sync);
	mutex_init(&cfg->scan_complete);
	err = wl_init_scan(cfg);
	if (err)
		return err;
#ifdef DHD_LOSSLESS_ROAMING
	err = wl_init_roam_timeout(cfg);
	if (err) {
		return err;
	}
#endif 
	wl_init_conf(cfg->conf);
	wl_init_prof(cfg, ndev);
	wl_link_down(cfg);
	DNGL_FUNC(dhd_cfg80211_init, (cfg));

	return err;
}

static void wl_deinit_priv(struct bcm_cfg80211 *cfg)
{
	DNGL_FUNC(dhd_cfg80211_deinit, (cfg));
	wl_destroy_event_handler(cfg);
	wl_flush_eq(cfg);
	wl_link_down(cfg);
	del_timer_sync(&cfg->scan_timeout);
#ifdef DHD_LOSSLESS_ROAMING
	del_timer_sync(&cfg->roam_timeout);
#endif
	wl_deinit_priv_mem(cfg);
	if (wl_cfg80211_netdev_notifier_registered) {
		wl_cfg80211_netdev_notifier_registered = FALSE;
		unregister_netdevice_notifier(&wl_cfg80211_netdev_notifier);
	}
}

#if defined(WL_ENABLE_P2P_IF) || defined(WL_NEWCFG_PRIVCMD_SUPPORT)
static s32 wl_cfg80211_attach_p2p(void)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	WL_TRACE(("Enter \n"));

	if (wl_cfgp2p_register_ndev(cfg) < 0) {
		WL_ERR(("P2P attach failed. \n"));
		return -ENODEV;
	}

	return 0;
}

static s32  wl_cfg80211_detach_p2p(void)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wireless_dev *wdev;

	WL_DBG(("Enter \n"));
	if (!cfg) {
		WL_ERR(("Invalid Ptr\n"));
		return -EINVAL;
	} else
		wdev = cfg->p2p_wdev;

#ifndef WL_NEWCFG_PRIVCMD_SUPPORT
	if (!wdev) {
		WL_ERR(("Invalid Ptr\n"));
		return -EINVAL;
	}
#endif 

	wl_cfgp2p_unregister_ndev(cfg);

	cfg->p2p_wdev = NULL;
	cfg->p2p_net = NULL;
#ifndef WL_NEWCFG_PRIVCMD_SUPPORT
	WL_DBG(("Freeing 0x%p \n", wdev));
	kfree(wdev);
#endif 

	return 0;
}
#endif 

s32 wl_cfg80211_attach_post(struct net_device *ndev)
{
	struct bcm_cfg80211 * cfg = NULL;
	s32 err = 0;
	s32 ret = 0;
	WL_TRACE(("In\n"));
	if (unlikely(!ndev)) {
		WL_ERR(("ndev is invaild\n"));
		return -ENODEV;
	}
	cfg = g_bcm_cfg;
	if (unlikely(!cfg)) {
		WL_ERR(("cfg is invaild\n"));
		return -EINVAL;
	}
	if (!wl_get_drv_status(cfg, READY, ndev)) {
		if (cfg->wdev) {
			ret = wl_cfgp2p_supported(cfg, ndev);
			if (ret > 0) {
#if !defined(WL_ENABLE_P2P_IF)
				cfg->wdev->wiphy->interface_modes |=
					(BIT(NL80211_IFTYPE_P2P_CLIENT)|
					BIT(NL80211_IFTYPE_P2P_GO));
#endif 
				if ((err = wl_cfgp2p_init_priv(cfg)) != 0)
					goto fail;

#if defined(WL_ENABLE_P2P_IF)
				if (cfg->p2p_net) {
					
					memcpy(cfg->p2p_net->dev_addr, ndev->dev_addr, ETH_ALEN);
					cfg->p2p_net->dev_addr[0] |= 0x02;
					WL_ERR(("%s: p2p_dev_addr="MACDBG "\n",
						cfg->p2p_net->name,
						MAC2STRDBG(cfg->p2p_net->dev_addr)));
				} else {
					WL_ERR(("p2p_net not yet populated."
					" Couldn't update the MAC Address for p2p0 \n"));
					return -ENODEV;
				}
#endif 
				cfg->p2p_supported = true;
			} else if (ret == 0) {
				if ((err = wl_cfgp2p_init_priv(cfg)) != 0)
					goto fail;
			} else {
				
				err = -ENODEV;
				goto fail;
			}
		}
	}
	wl_set_drv_status(cfg, READY, ndev);
fail:
	return err;
}

s32 wl_cfg80211_attach(struct net_device *ndev, void *context)
{
	struct wireless_dev *wdev;
	struct bcm_cfg80211 *cfg;
	s32 err = 0;
	struct device *dev;

	WL_TRACE(("In\n"));
	if (!ndev) {
		WL_ERR(("ndev is invaild\n"));
		return -ENODEV;
	}
	WL_DBG(("func %p\n", wl_cfg80211_get_parent_dev()));
	dev = wl_cfg80211_get_parent_dev();

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (unlikely(!wdev)) {
		WL_ERR(("Could not allocate wireless device\n"));
		return -ENOMEM;
	}
	err = wl_setup_wiphy(wdev, dev, context);
	if (unlikely(err)) {
		kfree(wdev);
		return -ENOMEM;
	}
	wdev->iftype = wl_mode_to_nl80211_iftype(WL_MODE_BSS);
	cfg = (struct bcm_cfg80211 *)wiphy_priv(wdev->wiphy);
	cfg->wdev = wdev;
	cfg->pub = context;
	INIT_LIST_HEAD(&cfg->net_list);
	spin_lock_init(&cfg->net_list_sync);
	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;
	cfg->state_notifier = wl_notifier_change_state;
	err = wl_alloc_netinfo(cfg, ndev, wdev, WL_MODE_BSS, PM_ENABLE, 0);
	if (err) {
		WL_ERR(("Failed to alloc net_info (%d)\n", err));
		goto cfg80211_attach_out;
	}
	err = wl_init_priv(cfg);
	if (err) {
		WL_ERR(("Failed to init iwm_priv (%d)\n", err));
		goto cfg80211_attach_out;
	}

	err = wl_setup_rfkill(cfg, TRUE);
	if (err) {
		WL_ERR(("Failed to setup rfkill %d\n", err));
		goto cfg80211_attach_out;
	}
#ifdef DEBUGFS_CFG80211
	err = wl_setup_debugfs(cfg);
	if (err) {
		WL_ERR(("Failed to setup debugfs %d\n", err));
		goto cfg80211_attach_out;
	}
#endif
	if (!wl_cfg80211_netdev_notifier_registered) {
		wl_cfg80211_netdev_notifier_registered = TRUE;
		err = register_netdevice_notifier(&wl_cfg80211_netdev_notifier);
		if (err) {
			wl_cfg80211_netdev_notifier_registered = FALSE;
			WL_ERR(("Failed to register notifierl %d\n", err));
			goto cfg80211_attach_out;
		}
	}
#if defined(COEX_DHCP)
	cfg->btcoex_info = wl_cfg80211_btcoex_init(cfg->wdev->netdev);
	if (!cfg->btcoex_info)
		goto cfg80211_attach_out;
#endif 

	g_bcm_cfg = cfg;

#ifdef CONFIG_CFG80211_INTERNAL_REGDB
	wdev->wiphy->reg_notifier = wl_cfg80211_reg_notifier;
#endif 

#if defined(WL_ENABLE_P2P_IF) || defined(WL_NEWCFG_PRIVCMD_SUPPORT)
	err = wl_cfg80211_attach_p2p();
	if (err)
		goto cfg80211_attach_out;
#endif 

#ifdef CUSTOMER_HW_ONE
	disc_called = false;
#endif 

	return err;

cfg80211_attach_out:
	wl_setup_rfkill(cfg, FALSE);
	wl_free_wdev(cfg);
	return err;
}

void wl_cfg80211_detach(void *para)
{
	struct bcm_cfg80211 *cfg;

	(void)para;
	cfg = g_bcm_cfg;

	WL_TRACE(("In\n"));

	wl_add_remove_pm_enable_work(cfg, FALSE, WL_HANDLER_DEL);

#if defined(COEX_DHCP)
	wl_cfg80211_btcoex_deinit();
	cfg->btcoex_info = NULL;
#endif 

	wl_setup_rfkill(cfg, FALSE);
#ifdef DEBUGFS_CFG80211
	wl_free_debugfs(cfg);
#endif
	if (cfg->p2p_supported) {
		if (timer_pending(&cfg->p2p->listen_timer))
			del_timer_sync(&cfg->p2p->listen_timer);
		wl_cfgp2p_deinit_priv(cfg);
	}

	if (timer_pending(&cfg->scan_timeout))
		del_timer_sync(&cfg->scan_timeout);
#ifdef DHD_LOSSLESS_ROAMING
	if (timer_pending(&cfg->roam_timeout)) {
		del_timer_sync(&cfg->roam_timeout);
	}
#endif 

#if defined(WL_CFG80211_P2P_DEV_IF)
	wl_cfgp2p_del_p2p_disc_if(cfg->p2p_wdev, cfg);
#endif 
#if defined(WL_ENABLE_P2P_IF) || defined(WL_NEWCFG_PRIVCMD_SUPPORT)
	wl_cfg80211_detach_p2p();
#endif 

	wl_cfg80211_ibss_vsie_free(cfg);
	wl_cfg80211_clear_mgmt_vndr_ies(cfg);
	wl_deinit_priv(cfg);
	g_bcm_cfg = NULL;
	wl_cfg80211_clear_parent_dev();
	wl_free_wdev(cfg);
}

static void wl_wakeup_event(struct bcm_cfg80211 *cfg)
{
	if (cfg->event_tsk.thr_pid >= 0) {
		up(&cfg->event_tsk.sema);
	}
}

static s32 wl_event_handler(void *data)
{
	struct bcm_cfg80211 *cfg = NULL;
	struct wl_event_q *e;
	tsk_ctl_t *tsk = (tsk_ctl_t *)data;
	struct wireless_dev *wdev = NULL;

	cfg = (struct bcm_cfg80211 *)tsk->parent;

	WL_ERR(("tsk Enter, tsk = 0x%p\n", tsk));

	while (down_interruptible (&tsk->sema) == 0) {
		SMP_RD_BARRIER_DEPENDS();
		if (tsk->terminated) {
			break;
		}
		while ((e = wl_deq_event(cfg))) {
#ifdef CUSTOMER_HW_ONE
			char *macaddr = (char *)&(e->emsg.addr);
			u32 reason = ntoh32(e->emsg.reason);
			u8 *curbssid;
			struct net_device *netdev;
#endif 
			WL_DBG(("event type (%d), ifidx: %d bssidx: %d \n",
				e->etype, e->emsg.ifidx, e->emsg.bsscfgidx));

			if (e->emsg.ifidx > WL_MAX_IFS) {
				WL_ERR((" Event ifidx not in range. val:%d \n", e->emsg.ifidx));
				goto fail;
			}

			if (!(wdev = wl_get_wdev_by_bssidx(cfg, e->emsg.bsscfgidx))) {
				
				if (e->etype != WLC_E_IF)
					WL_ERR(("No wdev corresponding to bssidx: 0x%x found!"
						" Ignoring event.\n", e->emsg.bsscfgidx));
			} else if (e->etype < WLC_E_LAST && cfg->evt_handler[e->etype]) {
				dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
				if (dhd->busstate == DHD_BUS_DOWN) {
					WL_ERR((": BUS is DOWN.\n"));
				} else
#ifdef CUSTOMER_HW_ONE
				{
					netdev = bcmcfg_to_prmry_ndev(cfg);
#ifdef SOFTAP
					if (dhd_check_ap_mode_set((struct dhd_pub *)(cfg->pub)))
						wl_cfg80211_hotspot_event_process(netdev, &e->emsg, e->edata);
#endif 
					switch (e->etype) {
					case WLC_E_DEAUTH:
					{
						WL_ERR(("DEAUTH received "
							"%02X:%02X:%02X:%02X:%02X:%02X, reason:%d\n",
							macaddr[0], macaddr[1], macaddr[2], macaddr[3],
							macaddr[4], macaddr[5], reason));

						curbssid = wl_read_prof(cfg, netdev, WL_PROF_BSSID);
						if ((wl_get_mode_by_netdev(cfg, netdev) == WL_MODE_BSS) &&
							strncmp(curbssid, macaddr, ETHER_ADDR_LEN)) {
							WL_ERR(("Recv Deauth from not connected bssid, "
								"skip it ndev_name[%s]!\n", netdev->name));
							goto skip;
						}
						break;
					}
					case WLC_E_DEAUTH_IND:
					{
						WL_ERR(("DEAUTH_IND recv %02X:%02X:%02X:%02X:%02X:%02X reason:%d\n",
							macaddr[0], macaddr[1], macaddr[2], macaddr[3],
							macaddr[4], macaddr[5], reason));
						break;
					}
					case WLC_E_DISASSOC:
					{
						WL_ERR(("DISASSOC recv %02X:%02X:%02X:%02X:%02X:%02X reason:%d\n",
							macaddr[0], macaddr[1], macaddr[2], macaddr[3],
							macaddr[4], macaddr[5], reason));
						break;
					}
					case WLC_E_DISASSOC_IND:
					{
						WL_ERR(("DISASSOC_IND recv %02X:%02X:%02X:%02X:%02X:%02X r:%d\n",
							macaddr[0], macaddr[1], macaddr[2], macaddr[3],
							macaddr[4], macaddr[5], reason));
						break;
					}
					default:
						break;
					}
					cfg->evt_handler[e->etype](cfg, wdev_to_cfgdev(wdev),
						&e->emsg, e->edata);
				}
#else
					cfg->evt_handler[e->etype](cfg, wdev_to_cfgdev(wdev),
						&e->emsg, e->edata);
#endif 
			} else {
				WL_DBG(("Unknown Event (%d): ignoring\n", e->etype));
			}
#ifdef CUSTOMER_HW_ONE
skip:
#endif 
fail:
			wl_put_event(e);
			DHD_EVENT_WAKE_UNLOCK(cfg->pub);
		}
	}
	WL_ERR(("was terminated\n"));
	complete_and_exit(&tsk->completed, 0);
	return 0;
}

void
wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t * e, void *data)
{
	u32 event_type = ntoh32(e->event_type);
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct net_info *netinfo;

#if (WL_DBG_LEVEL > 0)
	s8 *estr = (event_type <= sizeof(wl_dbg_estr) / WL_DBG_ESTR_MAX - 1) ?
	    wl_dbg_estr[event_type] : (s8 *) "Unknown";
	WL_DBG(("event_type (%d):" "WLC_E_" "%s\n", event_type, estr));
#endif 

	if (cfg->event_tsk.thr_pid == -1) {
		WL_ERR(("Event handler is not created\n"));
		return;
	}

	if ((cfg == NULL) || (cfg->p2p_supported && cfg->p2p == NULL)) {
		WL_ERR(("Stale event ignored\n"));
		return;
	}

	if (wl_get_p2p_status(cfg, IF_CHANGING) || wl_get_p2p_status(cfg, IF_ADDING)) {
		WL_ERR(("during IF change, ignore event %d\n", event_type));
		return;
	}

	netinfo = wl_get_netinfo_by_bssidx(cfg, e->bsscfgidx);
	if (!netinfo) {
		WL_ERR(("ignore event %d, not interested\n", event_type));
		return;
	}

	if (event_type == WLC_E_PFN_NET_FOUND) {
		WL_DBG((" PNOEVENT: PNO_NET_FOUND\n"));
	}
	else if (event_type == WLC_E_PFN_NET_LOST) {
		WL_DBG((" PNOEVENT: PNO_NET_LOST\n"));
	}

	DHD_EVENT_WAKE_LOCK(cfg->pub);
	if (likely(!wl_enq_event(cfg, ndev, event_type, e, data))) {
		wl_wakeup_event(cfg);
	} else {
		DHD_EVENT_WAKE_UNLOCK(cfg->pub);
	}
}

static void wl_init_eq(struct bcm_cfg80211 *cfg)
{
	wl_init_eq_lock(cfg);
	INIT_LIST_HEAD(&cfg->eq_list);
}

static void wl_flush_eq(struct bcm_cfg80211 *cfg)
{
	struct wl_event_q *e;
	unsigned long flags;

	flags = wl_lock_eq(cfg);
	while (!list_empty_careful(&cfg->eq_list)) {
		BCM_SET_LIST_FIRST_ENTRY(e, &cfg->eq_list, struct wl_event_q, eq_list);
		list_del(&e->eq_list);
		kfree(e);
	}
	wl_unlock_eq(cfg, flags);
}


static struct wl_event_q *wl_deq_event(struct bcm_cfg80211 *cfg)
{
	struct wl_event_q *e = NULL;
	unsigned long flags;

	flags = wl_lock_eq(cfg);
	if (likely(!list_empty(&cfg->eq_list))) {
		BCM_SET_LIST_FIRST_ENTRY(e, &cfg->eq_list, struct wl_event_q, eq_list);
		list_del(&e->eq_list);
	}
	wl_unlock_eq(cfg, flags);

	return e;
}


static s32
wl_enq_event(struct bcm_cfg80211 *cfg, struct net_device *ndev, u32 event,
	const wl_event_msg_t *msg, void *data)
{
	struct wl_event_q *e;
	s32 err = 0;
	uint32 evtq_size;
	uint32 data_len;
	unsigned long flags;
	gfp_t aflags;

	data_len = 0;
	if (data)
		data_len = ntoh32(msg->datalen);
	evtq_size = sizeof(struct wl_event_q) + data_len;
	aflags = (in_atomic()) ? GFP_ATOMIC : GFP_KERNEL;
	e = kzalloc(evtq_size, aflags);
	if (unlikely(!e)) {
		WL_ERR(("event alloc failed\n"));
		return -ENOMEM;
	}
	e->etype = event;
	memcpy(&e->emsg, msg, sizeof(wl_event_msg_t));
	if (data)
		memcpy(e->edata, data, data_len);
	flags = wl_lock_eq(cfg);
	list_add_tail(&e->eq_list, &cfg->eq_list);
	wl_unlock_eq(cfg, flags);

	return err;
}

static void wl_put_event(struct wl_event_q *e)
{
	kfree(e);
}

static s32 wl_config_ifmode(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 iftype)
{
	s32 infra = 0;
	s32 err = 0;
	s32 mode = 0;
	switch (iftype) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR(("type (%d) : currently we do not support this mode\n",
			iftype));
		err = -EINVAL;
		return err;
	case NL80211_IFTYPE_ADHOC:
		mode = WL_MODE_IBSS;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		mode = WL_MODE_BSS;
		infra = 1;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		mode = WL_MODE_AP;
		infra = 1;
		break;
	default:
		err = -EINVAL;
		WL_ERR(("invalid type (%d)\n", iftype));
		return err;
	}
	infra = htod32(infra);
	err = wldev_ioctl(ndev, WLC_SET_INFRA, &infra, sizeof(infra), true);
	if (unlikely(err)) {
		WL_ERR(("WLC_SET_INFRA error (%d)\n", err));
		return err;
	}

	wl_set_mode_by_netdev(cfg, ndev, mode);

	return 0;
}

void wl_cfg80211_add_to_eventbuffer(struct wl_eventmsg_buf *ev, u16 event, bool set)
{
	if (!ev || (event > WLC_E_LAST))
		return;

	if (ev->num < MAX_EVENT_BUF_NUM) {
		ev->event[ev->num].type = event;
		ev->event[ev->num].set = set;
		ev->num++;
	} else {
		WL_ERR(("evenbuffer doesn't support > %u events. Update"
			" the define MAX_EVENT_BUF_NUM \n", MAX_EVENT_BUF_NUM));
		ASSERT(0);
	}
}

s32 wl_cfg80211_apply_eventbuffer(
	struct net_device *ndev,
	struct bcm_cfg80211 *cfg,
	wl_eventmsg_buf_t *ev)
{
	char eventmask[WL_EVENTING_MASK_LEN];
	int i, ret = 0;
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];

	if (!ev || (!ev->num))
		return -EINVAL;

	mutex_lock(&cfg->event_sync);

	
	bcm_mkiovar("event_msgs", NULL, 0, iovbuf,
		sizeof(iovbuf));
	ret = wldev_ioctl(ndev, WLC_GET_VAR, iovbuf, sizeof(iovbuf), false);
	if (unlikely(ret)) {
		WL_ERR(("Get event_msgs error (%d)\n", ret));
		goto exit;
	}
	memcpy(eventmask, iovbuf, WL_EVENTING_MASK_LEN);

	
	for (i = 0; i < ev->num; i++) {
		if (ev->event[i].set)
			setbit(eventmask, ev->event[i].type);
		else
			clrbit(eventmask, ev->event[i].type);
	}

	
	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf,
		sizeof(iovbuf));
	ret = wldev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf), true);
	if (unlikely(ret)) {
		WL_ERR(("Set event_msgs error (%d)\n", ret));
	}

exit:
	mutex_unlock(&cfg->event_sync);
	return ret;
}

s32 wl_add_remove_eventmsg(struct net_device *ndev, u16 event, bool add)
{
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];
	s8 eventmask[WL_EVENTING_MASK_LEN];
	s32 err = 0;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	if (!ndev || !cfg)
		return -ENODEV;

	mutex_lock(&cfg->event_sync);

	
	bcm_mkiovar("event_msgs", NULL, 0, iovbuf,
		sizeof(iovbuf));
	err = wldev_ioctl(ndev, WLC_GET_VAR, iovbuf, sizeof(iovbuf), false);
	if (unlikely(err)) {
		WL_ERR(("Get event_msgs error (%d)\n", err));
		goto eventmsg_out;
	}
	memcpy(eventmask, iovbuf, WL_EVENTING_MASK_LEN);
	if (add) {
		setbit(eventmask, event);
	} else {
		clrbit(eventmask, event);
	}
	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf,
		sizeof(iovbuf));
	err = wldev_ioctl(ndev, WLC_SET_VAR, iovbuf, sizeof(iovbuf), true);
	if (unlikely(err)) {
		WL_ERR(("Set event_msgs error (%d)\n", err));
		goto eventmsg_out;
	}

eventmsg_out:
	mutex_unlock(&cfg->event_sync);
	return err;
}

static int wl_construct_reginfo(struct bcm_cfg80211 *cfg, s32 bw_cap)
{
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	struct ieee80211_channel *band_chan_arr = NULL;
	wl_uint32_list_t *list;
	u32 i, j, index, n_2g, n_5g, band, channel, array_size;
	u32 *n_cnt = NULL;
	chanspec_t c = 0;
	s32 err = BCME_OK;
	bool update;
	bool ht40_allowed;
	u8 *pbuf = NULL;
	bool dfs_radar_disabled = FALSE;

#define LOCAL_BUF_LEN 1024
	pbuf = kzalloc(LOCAL_BUF_LEN, GFP_KERNEL);

	if (pbuf == NULL) {
		WL_ERR(("failed to allocate local buf\n"));
		return -ENOMEM;
	}
	list = (wl_uint32_list_t *)(void *)pbuf;
	list->count = htod32(WL_NUMCHANSPECS);


	err = wldev_iovar_getbuf_bsscfg(dev, "chanspecs", NULL,
		0, pbuf, LOCAL_BUF_LEN, 0, &cfg->ioctl_buf_sync);
	if (err != 0) {
		WL_ERR(("get chanspecs failed with %d\n", err));
		kfree(pbuf);
		return err;
	}
#undef LOCAL_BUF_LEN

	list = (wl_uint32_list_t *)(void *)pbuf;
	band = array_size = n_2g = n_5g = 0;
	for (i = 0; i < dtoh32(list->count); i++) {
		index = 0;
		update = false;
		ht40_allowed = false;
		c = (chanspec_t)dtoh32(list->element[i]);
		c = wl_chspec_driver_to_host(c);
		channel = wf_chspec_ctlchan(c);

		if (!CHSPEC_IS40(c) && ! CHSPEC_IS20(c)) {
			WL_DBG(("HT80/160/80p80 center channel : %d\n", channel));
			continue;
		}
		if (CHSPEC_IS2G(c) && (channel >= CH_MIN_2G_CHANNEL) &&
			(channel <= CH_MAX_2G_CHANNEL)) {
			band_chan_arr = __wl_2ghz_channels;
			array_size = ARRAYSIZE(__wl_2ghz_channels);
			n_cnt = &n_2g;
			band = IEEE80211_BAND_2GHZ;
			ht40_allowed = (bw_cap  == WLC_N_BW_40ALL)? true : false;
		} else if (CHSPEC_IS5G(c) && channel >= CH_MIN_5G_CHANNEL) {
			band_chan_arr = __wl_5ghz_a_channels;
			array_size = ARRAYSIZE(__wl_5ghz_a_channels);
			n_cnt = &n_5g;
			band = IEEE80211_BAND_5GHZ;
			ht40_allowed = (bw_cap  == WLC_N_BW_20ALL)? false : true;
		} else {
			WL_ERR(("Invalid channel Sepc. 0x%x.\n", c));
			continue;
		}
		if (!ht40_allowed && CHSPEC_IS40(c))
			continue;
		for (j = 0; (j < *n_cnt && (*n_cnt < array_size)); j++) {
			if (band_chan_arr[j].hw_value == channel) {
				update = true;
				break;
			}
		}
		if (update)
			index = j;
		else
			index = *n_cnt;
		if (index <  array_size) {
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 38) && !defined(WL_COMPAT_WIRELESS)
			band_chan_arr[index].center_freq =
				ieee80211_channel_to_frequency(channel);
#else
			band_chan_arr[index].center_freq =
				ieee80211_channel_to_frequency(channel, band);
#endif
			band_chan_arr[index].hw_value = channel;

			if (CHSPEC_IS40(c) && ht40_allowed) {
				u32 ht40_flag = band_chan_arr[index].flags & IEEE80211_CHAN_NO_HT40;
				if (CHSPEC_SB_UPPER(c)) {
					if (ht40_flag == IEEE80211_CHAN_NO_HT40)
						band_chan_arr[index].flags &=
							~IEEE80211_CHAN_NO_HT40;
					band_chan_arr[index].flags |= IEEE80211_CHAN_NO_HT40PLUS;
				} else {
					band_chan_arr[index].flags &= ~IEEE80211_CHAN_NO_HT40;
					if (ht40_flag == IEEE80211_CHAN_NO_HT40)
						band_chan_arr[index].flags |=
							IEEE80211_CHAN_NO_HT40MINUS;
				}
			} else {
				band_chan_arr[index].flags = IEEE80211_CHAN_NO_HT40;
				if (!dfs_radar_disabled) {
					if (band == IEEE80211_BAND_2GHZ)
						channel |= WL_CHANSPEC_BAND_2G;
					else
						channel |= WL_CHANSPEC_BAND_5G;
					channel |= WL_CHANSPEC_BW_20;
					channel = wl_chspec_host_to_driver(channel);
					err = wldev_iovar_getint(dev, "per_chan_info", &channel);
					if (!err) {
						if (channel & WL_CHAN_RADAR) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
							band_chan_arr[index].flags |=
								(IEEE80211_CHAN_RADAR
								| IEEE80211_CHAN_NO_IBSS);
#else
							band_chan_arr[index].flags |=
								IEEE80211_CHAN_RADAR;
#endif
						}

						if (channel & WL_CHAN_PASSIVE)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
							band_chan_arr[index].flags |=
								IEEE80211_CHAN_PASSIVE_SCAN;
#else
							band_chan_arr[index].flags |=
								IEEE80211_CHAN_NO_IR;
#endif
					} else if (err == BCME_UNSUPPORTED) {
						dfs_radar_disabled = TRUE;
						WL_ERR(("does not support per_chan_info\n"));
					}
				}
			}
			if (!update)
				(*n_cnt)++;
		}

	}
	__wl_band_2ghz.n_channels = n_2g;
	__wl_band_5ghz_a.n_channels = n_5g;
	kfree(pbuf);
	return err;
}

s32 wl_update_wiphybands(struct bcm_cfg80211 *cfg, bool notify)
{
	struct wiphy *wiphy;
	struct net_device *dev;
	u32 bandlist[3];
	u32 nband = 0;
	u32 i = 0;
	s32 err = 0;
	s32 index = 0;
	s32 nmode = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) || 0
	u32 j = 0;
	s32 vhtmode = 0;
	s32 txstreams = 0;
	s32 rxstreams = 0;
	s32 ldpc_cap = 0;
	s32 stbc_rx = 0;
	s32 stbc_tx = 0;
	s32 txbf_bfe_cap = 0;
	s32 txbf_bfr_cap = 0;
#endif
	bool rollback_lock = false;
	s32 bw_cap = 0;
	s32 cur_band = -1;
	struct ieee80211_supported_band *bands[IEEE80211_NUM_BANDS] = {NULL, };

	if (cfg == NULL) {
		cfg = g_bcm_cfg;
		mutex_lock(&cfg->usr_sync);
		rollback_lock = true;
	}
	dev = bcmcfg_to_prmry_ndev(cfg);

	memset(bandlist, 0, sizeof(bandlist));
	err = wldev_ioctl(dev, WLC_GET_BANDLIST, bandlist,
		sizeof(bandlist), false);
	if (unlikely(err)) {
		WL_ERR(("error read bandlist (%d)\n", err));
		goto end_bands;
	}
	err = wldev_ioctl(dev, WLC_GET_BAND, &cur_band,
		sizeof(s32), false);
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		goto end_bands;
	}

	err = wldev_iovar_getint(dev, "nmode", &nmode);
	if (unlikely(err)) {
		WL_ERR(("error reading nmode (%d)\n", err));
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) || 0
	err = wldev_iovar_getint(dev, "vhtmode", &vhtmode);
	if (unlikely(err)) {
		WL_ERR(("error reading vhtmode (%d)\n", err));
	}

	if (vhtmode) {
		err = wldev_iovar_getint(dev, "txstreams", &txstreams);
		if (unlikely(err)) {
			WL_ERR(("error reading txstreams (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "rxstreams", &rxstreams);
		if (unlikely(err)) {
			WL_ERR(("error reading rxstreams (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "ldpc_cap", &ldpc_cap);
		if (unlikely(err)) {
			WL_ERR(("error reading ldpc_cap (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "stbc_rx", &stbc_rx);
		if (unlikely(err)) {
			WL_ERR(("error reading stbc_rx (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "stbc_tx", &stbc_tx);
		if (unlikely(err)) {
			WL_ERR(("error reading stbc_tx (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "txbf_bfe_cap", &txbf_bfe_cap);
		if (unlikely(err)) {
			WL_ERR(("error reading txbf_bfe_cap (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "txbf_bfr_cap", &txbf_bfr_cap);
		if (unlikely(err)) {
			WL_ERR(("error reading txbf_bfr_cap (%d)\n", err));
		}
	}
#endif

	
	if (nmode ||
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) || 0
		vhtmode ||
#endif
		0) {
		err = wldev_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
		if (unlikely(err)) {
			WL_ERR(("error get mimo_bw_cap (%d)\n", err));
		}
	}

	err = wl_construct_reginfo(cfg, bw_cap);
	if (err) {
		WL_ERR(("wl_construct_reginfo() fails err=%d\n", err));
		if (err != BCME_UNSUPPORTED)
			goto end_bands;
		err = 0;
	}
	wiphy = bcmcfg_to_wiphy(cfg);
	nband = bandlist[0];

	for (i = 1; i <= nband && i < ARRAYSIZE(bandlist); i++) {
		index = -1;
		if (bandlist[i] == WLC_BAND_5G && __wl_band_5ghz_a.n_channels > 0) {
			bands[IEEE80211_BAND_5GHZ] =
				&__wl_band_5ghz_a;
			index = IEEE80211_BAND_5GHZ;
			if (nmode && (bw_cap == WLC_N_BW_40ALL || bw_cap == WLC_N_BW_20IN2G_40IN5G))
				bands[index]->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) || 0
			
			if (vhtmode) {
				
				bands[index]->vht_cap.vht_supported = TRUE;

				for (j = 1; j <= VHT_CAP_MCS_MAP_NSS_MAX; j++) {
					
					if (j <= txstreams) {
						VHT_MCS_MAP_SET_MCS_PER_SS(j, VHT_CAP_MCS_MAP_0_9,
							bands[index]->vht_cap.vht_mcs.tx_mcs_map);
					} else {
						VHT_MCS_MAP_SET_MCS_PER_SS(j, VHT_CAP_MCS_MAP_NONE,
							bands[index]->vht_cap.vht_mcs.tx_mcs_map);
					}

					
					if (j <= rxstreams) {
						VHT_MCS_MAP_SET_MCS_PER_SS(j, VHT_CAP_MCS_MAP_0_9,
							bands[index]->vht_cap.vht_mcs.rx_mcs_map);
					} else {
						VHT_MCS_MAP_SET_MCS_PER_SS(j, VHT_CAP_MCS_MAP_NONE,
							bands[index]->vht_cap.vht_mcs.rx_mcs_map);
					}
				}


				
				
				bands[index]->vht_cap.cap |=
					IEEE80211_VHT_CAP_SHORT_GI_80;

				if (WL_BW_CAP_160MHZ(bw_cap)) {
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_SHORT_GI_160;
				}

				bands[index]->vht_cap.cap |=
					IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;

				if (ldpc_cap)
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_RXLDPC;

				if (stbc_tx)
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_TXSTBC;

				if (stbc_rx)
					bands[index]->vht_cap.cap |=
						(stbc_rx << VHT_CAP_INFO_RX_STBC_SHIFT);

				if (txbf_bfe_cap)
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;

				if (txbf_bfr_cap) {
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
				}

				if (txbf_bfe_cap || txbf_bfr_cap) {
					bands[index]->vht_cap.cap |=
						(2 << VHT_CAP_INFO_NUM_BMFMR_ANT_SHIFT);
					bands[index]->vht_cap.cap |=
						((txstreams - 1) <<
							VHT_CAP_INFO_NUM_SOUNDING_DIM_SHIFT);
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB;
				}

				
				bands[index]->vht_cap.cap |=
					(7 << VHT_CAP_INFO_AMPDU_MAXLEN_EXP_SHIFT);
				WL_INFORM(("%s band[%d] vht_enab=%d vht_cap=%08x "
					"vht_rx_mcs_map=%04x vht_tx_mcs_map=%04x\n",
					__FUNCTION__, index,
					bands[index]->vht_cap.vht_supported,
					bands[index]->vht_cap.cap,
					bands[index]->vht_cap.vht_mcs.rx_mcs_map,
					bands[index]->vht_cap.vht_mcs.tx_mcs_map));
			}
#endif
		}
		else if (bandlist[i] == WLC_BAND_2G && __wl_band_2ghz.n_channels > 0) {
			bands[IEEE80211_BAND_2GHZ] =
				&__wl_band_2ghz;
			index = IEEE80211_BAND_2GHZ;
			if (bw_cap == WLC_N_BW_40ALL)
				bands[index]->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
		}

		if ((index >= 0) && nmode) {
			bands[index]->ht_cap.cap |=
				(IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_DSSSCCK40);
			bands[index]->ht_cap.ht_supported = TRUE;
			bands[index]->ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
			bands[index]->ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;
			
			bands[index]->ht_cap.mcs.rx_mask[0] = 0xff;
		}

	}

	wiphy->bands[IEEE80211_BAND_2GHZ] = bands[IEEE80211_BAND_2GHZ];
	wiphy->bands[IEEE80211_BAND_5GHZ] = bands[IEEE80211_BAND_5GHZ];

	
	if (wiphy->bands[IEEE80211_BAND_2GHZ] == NULL &&
		wiphy->bands[IEEE80211_BAND_5GHZ] == NULL) {
		
		wiphy->bands[IEEE80211_BAND_2GHZ] = &__wl_band_2ghz;
	}

	if (notify)
		wiphy_apply_custom_regulatory(wiphy, &brcm_regdom);

	end_bands:
		if (rollback_lock)
			mutex_unlock(&cfg->usr_sync);
	return err;
}

static s32 __wl_cfg80211_up(struct bcm_cfg80211 *cfg)
{
	s32 err = 0;
#ifdef WL_HOST_BAND_MGMT
	s32 ret = 0;
#endif 
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;

	WL_DBG(("In\n"));

	err = dhd_config_dongle(cfg);
	if (unlikely(err))
		return err;

	err = wl_config_ifmode(cfg, ndev, wdev->iftype);
	if (unlikely(err && err != -EINPROGRESS)) {
		WL_ERR(("wl_config_ifmode failed\n"));
		if (err == -1) {
			WL_ERR(("return error %d\n", err));
			return err;
		}
	}
	err = wl_update_wiphybands(cfg, true);
	if (unlikely(err)) {
		WL_ERR(("wl_update_wiphybands failed\n"));
		if (err == -1) {
			WL_ERR(("return error %d\n", err));
			return err;
		}
	}

	err = wl_create_event_handler(cfg);
	if (err) {
		WL_ERR(("wl_create_event_handler failed\n"));
		return err;
	}
	wl_init_event_handler(cfg);

	err = wl_init_scan(cfg);
	if (err) {
		WL_ERR(("wl_init_scan failed\n"));
		return err;
	}
#ifdef DHD_LOSSLESS_ROAMING
	if (timer_pending(&cfg->roam_timeout)) {
		del_timer_sync(&cfg->roam_timeout);
	}
#endif 

	err = dhd_monitor_init(cfg->pub);

#ifdef WL_HOST_BAND_MGMT
	
	if ((ret = wl_cfg80211_set_band(ndev, WLC_BAND_AUTO)) < 0) {
		if (ret == BCME_UNSUPPORTED) {
			WL_ERR(("WL_HOST_BAND_MGMT defined, "
				"but roam_band iovar not supported \n"));
		} else {
			WL_ERR(("roam_band failed. ret=%d", ret));
			err = -1;
		}
	}
#endif 
#if defined(DHCP_SCAN_SUPPRESS)
	
	init_timer(&cfg->scan_supp_timer);
	cfg->scan_supp_timer.data = (ulong)cfg;
	cfg->scan_supp_timer.function = wl_cfg80211_scan_supp_timerfunc;
	INIT_WORK(&cfg->wlan_work, wl_cfg80211_work_handler);
#endif 
	INIT_DELAYED_WORK(&cfg->pm_enable_work, wl_cfg80211_work_handler);
	wl_set_drv_status(cfg, READY, ndev);
	return err;
}

static s32 __wl_cfg80211_down(struct bcm_cfg80211 *cfg)
{
	s32 err = 0;
	unsigned long flags;
	struct net_info *iter, *next;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
#if defined(WL_CFG80211) && (defined(WL_ENABLE_P2P_IF)|| \
	defined(WL_NEWCFG_PRIVCMD_SUPPORT))
	struct net_device *p2p_net = cfg->p2p_net;
#endif 
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
#endif 
#endif 
	WL_DBG(("In\n"));
	
	wl_add_remove_pm_enable_work(cfg, FALSE, WL_HANDLER_DEL);

#ifdef WL_NAN
	wl_cfgnan_stop_handler(ndev, g_bcm_cfg, NULL, 0, NULL);
#endif 

	if (cfg->p2p_supported) {
		wl_clr_p2p_status(cfg, GO_NEG_PHASE);
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
		if (wl_cfgp2p_vif_created(cfg)) {
			bool enabled = false;
			dhd_wlfc_get_enable(dhd, &enabled);
			if (enabled && cfg->wlfc_on && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
				dhd->op_mode != DHD_FLAG_IBSS_MODE) {
				dhd_wlfc_deinit(dhd);
				cfg->wlfc_on = false;
			}
		}
#endif 
#endif 
	}

#if defined(DHCP_SCAN_SUPPRESS)
	
	if (cfg->scan_suppressed)
		wl_cfg80211_scan_suppress(ndev, 0);
	if (timer_pending(&cfg->scan_supp_timer))
		del_timer_sync(&cfg->scan_supp_timer);
	cancel_work_sync(&cfg->wlan_work);
#endif 

	
	if (wl_cfgp2p_bss_isup(ndev, 0)) {
		if (wl_cfgp2p_bss(cfg, ndev, 0, 0) < 0)
			WL_ERR(("BSS down failed \n"));
	}

	
	if (!wl_get_drv_status(cfg, READY, ndev))
		return err;	

	
	wl_cfg80211_clear_security(cfg);

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) 
			wl_set_drv_status(cfg, SCAN_ABORTING, iter->ndev);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
#ifdef WL_SDO
	wl_cfg80211_sdo_deinit(cfg);
#endif

	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	if (cfg->scan_request) {
		cfg80211_scan_done(cfg->scan_request, true);
		cfg->scan_request = NULL;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		
		if (iter->ndev == NULL)
			continue;
		wl_clr_drv_status(cfg, READY, iter->ndev);
		wl_clr_drv_status(cfg, SCANNING, iter->ndev);
		wl_clr_drv_status(cfg, SCAN_ABORTING, iter->ndev);
		wl_clr_drv_status(cfg, CONNECTING, iter->ndev);
		wl_clr_drv_status(cfg, CONNECTED, iter->ndev);
		wl_clr_drv_status(cfg, DISCONNECTING, iter->ndev);
		wl_clr_drv_status(cfg, AP_CREATED, iter->ndev);
		wl_clr_drv_status(cfg, AP_CREATING, iter->ndev);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	bcmcfg_to_prmry_ndev(cfg)->ieee80211_ptr->iftype =
		NL80211_IFTYPE_STATION;
#if defined(WL_CFG80211) && (defined(WL_ENABLE_P2P_IF)|| \
	defined(WL_NEWCFG_PRIVCMD_SUPPORT))
		if (p2p_net)
			dev_close(p2p_net);
#endif 

	
	mutex_unlock(&cfg->usr_sync);
	wl_destroy_event_handler(cfg);
	mutex_lock(&cfg->usr_sync);
	wl_flush_eq(cfg);
	wl_link_down(cfg);
	if (cfg->p2p_supported) {
		if (timer_pending(&cfg->p2p->listen_timer))
			del_timer_sync(&cfg->p2p->listen_timer);
		wl_cfgp2p_down(cfg);
	}

	if (timer_pending(&cfg->scan_timeout)) {
		del_timer_sync(&cfg->scan_timeout);
	}

	dhd_monitor_uninit();
#ifdef WLAIBSS_MCHAN
	bcm_cfg80211_del_ibss_if(cfg->wdev->wiphy, cfg->ibss_cfgdev);
#endif 

#if defined(WL_VIRTUAL_APSTA) || defined(DUAL_STA_STATIC_IF)
	
	if (cfg->bss_cfgdev)
		wl_cfg80211_del_iface(cfg->wdev->wiphy, cfg->bss_cfgdev);
#endif 

#ifdef WL11U
	
	if (cfg->wl11u) {
		cfg->wl11u = FALSE;
		cfg->iw_ie_len = 0;
		memset(cfg->iw_ie, 0, IW_IES_MAX_BUF_LEN);
	}
#endif 


	DNGL_FUNC(dhd_cfg80211_down, (cfg));

	return err;
}

s32 wl_cfg80211_up(void *para)
{
	struct bcm_cfg80211 *cfg;
	s32 err = 0;
	int val = 1;
	dhd_pub_t *dhd;
#ifdef DISABLE_PM_BCNRX
	s32 interr = 0;
	uint param = 0;
	s8 iovbuf[WLC_IOCTL_SMLEN];
#endif 

	(void)para;
	WL_DBG(("In\n"));
	cfg = g_bcm_cfg;

	if ((err = wldev_ioctl(bcmcfg_to_prmry_ndev(cfg), WLC_GET_VERSION, &val,
		sizeof(int), false) < 0)) {
		WL_ERR(("WLC_GET_VERSION failed, err=%d\n", err));
		return err;
	}
	val = dtoh32(val);
	if (val != WLC_IOCTL_VERSION && val != 1) {
		WL_ERR(("Version mismatch, please upgrade. Got %d, expected %d or 1\n",
			val, WLC_IOCTL_VERSION));
		return BCME_VERSION;
	}
	ioctl_version = val;
	WL_TRACE(("WLC_GET_VERSION=%d\n", ioctl_version));

	mutex_lock(&cfg->usr_sync);
	dhd = (dhd_pub_t *)(cfg->pub);
	if (!(dhd->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		err = wl_cfg80211_attach_post(bcmcfg_to_prmry_ndev(cfg));
		if (unlikely(err)) {
			mutex_unlock(&cfg->usr_sync);
			return err;
		}
	}
#if defined(BCMSUP_4WAY_HANDSHAKE) && defined(WLAN_AKM_SUITE_FT_8021X)
	if (dhd->fw_4way_handshake)
		cfg->wdev->wiphy->features |= NL80211_FEATURE_FW_4WAY_HANDSHAKE;
#endif
	err = __wl_cfg80211_up(cfg);
	if (unlikely(err))
		WL_ERR(("__wl_cfg80211_up failed\n"));

	
#ifdef DISABLE_PM_BCNRX
	bcm_mkiovar("pm_bcnrx", (char *)&param, 4, iovbuf, sizeof(iovbuf));
	interr = wldev_ioctl(bcmcfg_to_prmry_ndev(cfg), WLC_SET_VAR, iovbuf, sizeof(iovbuf), true);
	if (unlikely(interr))
		WL_ERR(("Set pm_bcnrx error (%d)\n", interr));
#endif 

	mutex_unlock(&cfg->usr_sync);

#ifdef WLAIBSS_MCHAN
	bcm_cfg80211_add_ibss_if(cfg->wdev->wiphy, IBSS_IF_NAME);
#endif 

#ifdef DUAL_STA_STATIC_IF
#ifdef WL_VIRTUAL_APSTA
#error "Both DUAL STA and DUAL_STA_STATIC_IF can't be enabled together"
#endif
	
	wl_cfg80211_create_iface(cfg->wdev->wiphy, NL80211_IFTYPE_STATION, NULL, "wlan%d");
#endif 

	return err;
}

int wl_cfg80211_hang(struct net_device *dev, u16 reason)
{
	struct bcm_cfg80211 *cfg;
#if defined(SOFTAP_SEND_HANGEVT)
	
	uint8 hang_mac[ETHER_ADDR_LEN] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
	dhd_pub_t *dhd;
#endif 
	if (!g_bcm_cfg) {
		return BCME_ERROR;
	}

	cfg = g_bcm_cfg;

	WL_ERR(("In : chip crash eventing\n"));
	wl_add_remove_pm_enable_work(cfg, FALSE, WL_HANDLER_DEL);
#if defined(SOFTAP_SEND_HANGEVT)
	dhd = (dhd_pub_t *)(cfg->pub);
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		cfg80211_del_sta(dev, hang_mac, GFP_ATOMIC);
	} else
#endif 
	{
		cfg80211_disconnected(dev, reason, NULL, 0, GFP_KERNEL);
	}
	if (cfg != NULL) {
		wl_link_down(cfg);
	}
	return 0;
}

s32 wl_cfg80211_down(void *para)
{
	struct bcm_cfg80211 *cfg;
	s32 err = 0;

	(void)para;
	WL_DBG(("In\n"));
	cfg = g_bcm_cfg;
	mutex_lock(&cfg->usr_sync);
	err = __wl_cfg80211_down(cfg);
	mutex_unlock(&cfg->usr_sync);

	return err;
}

static void *wl_read_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 item)
{
	unsigned long flags;
	void *rptr = NULL;
	struct wl_profile *profile = wl_get_profile_by_netdev(cfg, ndev);

	if (!profile)
		return NULL;
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	switch (item) {
	case WL_PROF_SEC:
		rptr = &profile->sec;
		break;
	case WL_PROF_ACT:
		rptr = &profile->active;
		break;
	case WL_PROF_BSSID:
		rptr = profile->bssid;
		break;
	case WL_PROF_SSID:
		rptr = &profile->ssid;
		break;
	case WL_PROF_CHAN:
		rptr = &profile->channel;
		break;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	if (!rptr)
		WL_ERR(("invalid item (%d)\n", item));
	return rptr;
}

static s32
wl_update_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, const void *data, s32 item)
{
	s32 err = 0;
	const struct wlc_ssid *ssid;
	unsigned long flags;
	struct wl_profile *profile = wl_get_profile_by_netdev(cfg, ndev);

	if (!profile)
		return WL_INVALID;
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	switch (item) {
	case WL_PROF_SSID:
		ssid = (const wlc_ssid_t *) data;
		memset(profile->ssid.SSID, 0,
			sizeof(profile->ssid.SSID));
		memcpy(profile->ssid.SSID, ssid->SSID, ssid->SSID_len);
		profile->ssid.SSID_len = ssid->SSID_len;
		break;
	case WL_PROF_BSSID:
		if (data)
			memcpy(profile->bssid, data, ETHER_ADDR_LEN);
		else
			memset(profile->bssid, 0, ETHER_ADDR_LEN);
		break;
	case WL_PROF_SEC:
		memcpy(&profile->sec, data, sizeof(profile->sec));
		break;
	case WL_PROF_ACT:
		profile->active = *(const bool *)data;
		break;
	case WL_PROF_BEACONINT:
		profile->beacon_interval = *(const u16 *)data;
		break;
	case WL_PROF_DTIMPERIOD:
		profile->dtim_period = *(const u8 *)data;
		break;
	case WL_PROF_CHAN:
		profile->channel = *(const u32*)data;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);

	if (err == -EOPNOTSUPP)
		WL_ERR(("unsupported item (%d)\n", item));

	return err;
}

void wl_cfg80211_dbg_level(u32 level)
{
	
}

static bool wl_is_ibssmode(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	return wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_IBSS;
}

static __used bool wl_is_ibssstarter(struct bcm_cfg80211 *cfg)
{
	return cfg->ibss_starter;
}

static void wl_rst_ie(struct bcm_cfg80211 *cfg)
{
	struct wl_ie *ie = wl_to_ie(cfg);

	ie->offset = 0;
}

static __used s32 wl_add_ie(struct bcm_cfg80211 *cfg, u8 t, u8 l, u8 *v)
{
	struct wl_ie *ie = wl_to_ie(cfg);
	s32 err = 0;

	if (unlikely(ie->offset + l + 2 > WL_TLV_INFO_MAX)) {
		WL_ERR(("ei crosses buffer boundary\n"));
		return -ENOSPC;
	}
	ie->buf[ie->offset] = t;
	ie->buf[ie->offset + 1] = l;
	memcpy(&ie->buf[ie->offset + 2], v, l);
	ie->offset += l + 2;

	return err;
}

static void wl_update_hidden_ap_ie(struct wl_bss_info *bi, const u8 *ie_stream, u32 *ie_size,
	bool roam)
{
	u8 *ssidie;
	
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	ssidie = (u8 *)cfg80211_find_ie(WLAN_EID_SSID, ie_stream, *ie_size);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	if (!ssidie)
		return;
	if (ssidie[1] != bi->SSID_len) {
		if (ssidie[1]) {
			WL_ERR(("%s: Wrong SSID len: %d != %d\n",
				__FUNCTION__, ssidie[1], bi->SSID_len));
		}
		if (roam) {
			WL_ERR(("Changing the SSID Info.\n"));
			memmove(ssidie + bi->SSID_len + 2,
				(ssidie + 2) + ssidie[1],
				*ie_size - (ssidie + 2 + ssidie[1] - ie_stream));
			memcpy(ssidie + 2, bi->SSID, bi->SSID_len);
			*ie_size = *ie_size + bi->SSID_len - ssidie[1];
			ssidie[1] = bi->SSID_len;
		}
		return;
	}
	if (*(ssidie + 2) == '\0')
		 memcpy(ssidie + 2, bi->SSID, bi->SSID_len);
	return;
}

static s32 wl_mrg_ie(struct bcm_cfg80211 *cfg, u8 *ie_stream, u16 ie_size)
{
	struct wl_ie *ie = wl_to_ie(cfg);
	s32 err = 0;

	if (unlikely(ie->offset + ie_size > WL_TLV_INFO_MAX)) {
		WL_ERR(("ei_stream crosses buffer boundary\n"));
		return -ENOSPC;
	}
	memcpy(&ie->buf[ie->offset], ie_stream, ie_size);
	ie->offset += ie_size;

	return err;
}

static s32 wl_cp_ie(struct bcm_cfg80211 *cfg, u8 *dst, u16 dst_size)
{
	struct wl_ie *ie = wl_to_ie(cfg);
	s32 err = 0;

	if (unlikely(ie->offset > dst_size)) {
		WL_ERR(("dst_size is not enough\n"));
		return -ENOSPC;
	}
	memcpy(dst, &ie->buf[0], ie->offset);

	return err;
}

static u32 wl_get_ielen(struct bcm_cfg80211 *cfg)
{
	struct wl_ie *ie = wl_to_ie(cfg);

	return ie->offset;
}

static void wl_link_up(struct bcm_cfg80211 *cfg)
{
	cfg->link_up = true;
}

static void wl_link_down(struct bcm_cfg80211 *cfg)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);

	WL_DBG(("In\n"));
	cfg->link_up = false;
	conn_info->req_ie_len = 0;
	conn_info->resp_ie_len = 0;
}

static unsigned long wl_lock_eq(struct bcm_cfg80211 *cfg)
{
	unsigned long flags;

	spin_lock_irqsave(&cfg->eq_lock, flags);
	return flags;
}

static void wl_unlock_eq(struct bcm_cfg80211 *cfg, unsigned long flags)
{
	spin_unlock_irqrestore(&cfg->eq_lock, flags);
}

static void wl_init_eq_lock(struct bcm_cfg80211 *cfg)
{
	spin_lock_init(&cfg->eq_lock);
}

static void wl_delay(u32 ms)
{
	if (in_atomic() || (ms < jiffies_to_msecs(1))) {
		OSL_DELAY(ms*1000);
	} else {
		OSL_SLEEP(ms);
	}
}

s32 wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct ether_addr primary_mac;
	if (!cfg->p2p)
		return -1;
	if (!p2p_is_on(cfg)) {
		get_primary_mac(cfg, &primary_mac);
		wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
	} else {
		memcpy(p2pdev_addr->octet, wl_to_p2p_bss_macaddr(cfg, P2PAPI_BSSCFG_DEVICE).octet,
			ETHER_ADDR_LEN);
	}

	return 0;
}
s32 wl_cfg80211_set_p2p_noa(struct net_device *net, char* buf, int len)
{
	struct bcm_cfg80211 *cfg;

	cfg = g_bcm_cfg;

	return wl_cfgp2p_set_p2p_noa(cfg, net, buf, len);
}

s32 wl_cfg80211_get_p2p_noa(struct net_device *net, char* buf, int len)
{
	struct bcm_cfg80211 *cfg;
	cfg = g_bcm_cfg;

	return wl_cfgp2p_get_p2p_noa(cfg, net, buf, len);
}

s32 wl_cfg80211_set_p2p_ps(struct net_device *net, char* buf, int len)
{
	struct bcm_cfg80211 *cfg;
	cfg = g_bcm_cfg;

	return wl_cfgp2p_set_p2p_ps(cfg, net, buf, len);
}
#ifdef P2PLISTEN_AP_SAMECHN
s32 wl_cfg80211_set_p2p_resp_ap_chn(struct net_device *net, s32 enable)
{
	s32 ret = wldev_iovar_setint(net, "p2p_resp_ap_chn", enable);

	if ((ret == 0) && enable) {
		
		s32 pm = PM_OFF;

		ret = wldev_ioctl(net, WLC_SET_PM, &pm, sizeof(pm), true);
	}

	return ret;
}
#endif 

s32 wl_cfg80211_channel_to_freq(u32 channel)
{
	int freq = 0;

#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 38) && !defined(WL_COMPAT_WIRELESS)
	freq = ieee80211_channel_to_frequency(channel);
#else
	{
		u16 band = 0;
		if (channel <= CH_MAX_2G_CHANNEL)
			band = IEEE80211_BAND_2GHZ;
		else
			band = IEEE80211_BAND_5GHZ;
		freq = ieee80211_channel_to_frequency(channel, band);
	}
#endif
	return freq;
}

#ifdef WL_SDO
#define MAX_QR_LEN NLMSG_GOODSIZE

typedef struct wl_cfg80211_dev_info {
	u16 band;
	u16 freq;
	s16 rssi;
	u16 ie_len;
	u8 bssid[ETH_ALEN];
} wl_cfg80211_dev_info_t;

static s32
wl_notify_device_discovery(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	int err = 0;
	u32 event = ntoh32(e->event_type);
	wl_cfg80211_dev_info_t info;
	struct wl_bss_info *bi = NULL;
	struct net_device *ndev = NULL;
	u8 *buf = NULL;
	u32 buflen = 0;
	u16 channel = 0;
	 wl_escan_result_t *escan_result;

	WL_SD(("Enter. type:%d \n", event));

	if ((event != WLC_E_P2PO_ADD_DEVICE) && (event != WLC_E_P2PO_DEL_DEVICE)) {
		WL_ERR(("Unknown Event\n"));
		return -EINVAL;
	}

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	if (event == WLC_E_P2PO_DEL_DEVICE) {
		WL_SD(("DEV_LOST MAC:"MACDBG" \n", MAC2STRDBG(e->addr.octet)));
		err = wl_genl_send_msg(ndev, event, (u8 *)e->addr.octet, ETH_ALEN, 0, 0);
	} else {

		escan_result = (wl_escan_result_t *) data;

		if (dtoh16(escan_result->bss_count) != 1) {
			WL_ERR(("Invalid bss_count %d: ignoring\n", escan_result->bss_count));
			err = -EINVAL;
			goto exit;
		}

		bi = escan_result->bss_info;
		buflen = dtoh32(bi->length);
		if (unlikely(buflen > WL_BSS_INFO_MAX)) {
			WL_DBG(("Beacon is larger than buffer. Discarding\n"));
			err = -EINVAL;
			goto exit;
		}

		
		bzero(&info, sizeof(wl_cfg80211_dev_info_t));
		channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(bi->chanspec));
		info.freq = wl_cfg80211_channel_to_freq(channel);
		info.rssi = wl_rssi_offset(dtoh16(bi->RSSI));
		memcpy(info.bssid, &bi->BSSID, ETH_ALEN);
		info.ie_len = buflen;

		WL_SD(("DEV_FOUND band:%x Freq:%d rssi:%x "MACDBG" \n",
			info.band, info.freq, info.rssi, MAC2STRDBG(info.bssid)));

		buf =  ((u8 *) bi) + bi->ie_offset;
		err = wl_genl_send_msg(ndev, event, buf,
			buflen, (u8 *)&info, sizeof(wl_cfg80211_dev_info_t));
	}
exit:
	mutex_unlock(&cfg->usr_sync);
	return err;
}

s32
wl_cfg80211_sdo_init(struct bcm_cfg80211 *cfg)
{
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;

	if (cfg->sdo) {
		WL_SD(("SDO already initialized\n"));
		return 0;
	}

	cfg->sdo = kzalloc(sizeof(sd_offload_t), kflags);
	if (!cfg->sdo) {
		WL_ERR(("malloc failed for SDO \n"));
		return -ENOMEM;
	}

	return  0;
}

s32
wl_cfg80211_sdo_deinit(struct bcm_cfg80211 *cfg)
{
	s32 bssidx;
	int ret = 0;
	int sdo_pause = 0;
	if (!cfg || !cfg->p2p) {
		WL_ERR(("Wl %p or cfg->p2p %p is null\n",
			cfg, cfg ? cfg->p2p : 0));
		return 0;
	}

	bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	if (!cfg->sdo) {
		WL_DBG(("SDO Not Initialized. Do nothing. \n"));
		return 0;
	}
	if (cfg->sdo->dd_state &&
		(ret = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg),
		"p2po_stop", (void*)&sdo_pause, sizeof(sdo_pause),
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, NULL)) < 0) {
		WL_ERR(("p2po_stop Failed :%d\n", ret));
	}
	kfree(cfg->sdo);
	cfg->sdo = NULL;

	WL_SD(("SDO Deinit Done \n"));

	return  0;
}

s32
wl_cfg80211_resume_sdo(struct net_device *dev, struct bcm_cfg80211 *cfg)
{
	wl_sd_listen_t sd_listen;
	int ret = 0;
	s32 bssidx =  wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);

	WL_DBG(("Enter\n"));

	if (!cfg->sdo) {
		return -EINVAL;
	}

	if (dev == NULL)
		dev = bcmcfg_to_prmry_ndev(cfg);

	
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, false);

	
	if (cfg->sdo->dd_state == WL_DD_STATE_SEARCH) {
		if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_find", NULL, 0,
			cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, NULL)) < 0) {
			WL_ERR(("p2po_find Failed :%d\n", ret));
		}
	} else if (cfg->sdo->dd_state == WL_DD_STATE_LISTEN) {
		sd_listen.interval = cfg->sdo->sd_listen.interval;
		sd_listen.period = cfg->sdo->sd_listen.period;

		if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen", (void*)&sd_listen,
			sizeof(wl_sd_listen_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, NULL)) < 0) {
			WL_ERR(("p2po_listen Failed :%d\n", ret));
		}

	}

	 
	 wl_add_remove_eventmsg(dev, WLC_E_SERVICE_FOUND, true);
	 wl_add_remove_eventmsg(dev, WLC_E_GAS_FRAGMENT_RX, true);
	 wl_add_remove_eventmsg(dev, WLC_E_GAS_COMPLETE, true);

	WL_SD(("SDO Resumed \n"));

	return ret;
}

s32 wl_cfg80211_pause_sdo(struct net_device *dev, struct bcm_cfg80211 *cfg)
{

	int ret = 0;
	s32 bssidx =  wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	int sdo_pause = 1;

	WL_DBG(("Enter \n"));

	if (!cfg->sdo) {
		WL_ERR(("SDO not initialized \n"));
		return -EINVAL;
	}

	if (dev == NULL)
		dev = bcmcfg_to_prmry_ndev(cfg);

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_stop",
		(void*)&sdo_pause, sizeof(sdo_pause),
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_stop Failed :%d\n", ret));
	}

	
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, true);

	WL_SD(("SDO Paused \n"));

	return ret;
}

static s32
wl_svc_resp_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	u32 event = ntoh32(e->event_type);
	struct net_device *ndev = NULL;
	u8 *dst_mac = (u8 *)e->addr.octet;
	int ret = 0;
	wl_event_sd_t *gas = NULL;
	int status = ntoh32(e->status);
	sdo_event_t sdo_hdr;
	u32 data_len = ntoh32(e->datalen);
	u8 *data_ptr = NULL;
	u32 tot_len = 0;


	WL_SD(("Enter event_type:%d status:%d\n", event, status));

	if (!cfg->sdo) {
		WL_ERR(("SDO Not initialized \n"));
		return -EINVAL;
	}

	if (!(cfg->sdo->sd_state & WL_SD_SEARCH_SVC)) {
		WL_ERR(("Bogus SDO Event. Do nothing.. \n"));
		return -1;
	}

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	if (event == WLC_E_SERVICE_FOUND) {

		if ((status != WLC_E_STATUS_SUCCESS) && (status != WLC_E_STATUS_PARTIAL)) {
			WL_ERR(("WLC_E_SERVICE_FOUND: unknown status \n"));
			goto exit;
		}

		gas = (wl_event_sd_t *)data;
		if (!gas) {
			ret = -EINVAL;
			goto exit;
		}

		bzero(&sdo_hdr, sizeof(sdo_event_t));
		sdo_hdr.freq = wl_cfg80211_channel_to_freq(gas->channel);
		sdo_hdr.count = gas->count;
		memcpy(sdo_hdr.addr, dst_mac, ETH_ALEN);
		data_ptr = (char *)gas->tlv;
		tot_len = data_len - (sizeof(wl_event_sd_t) - sizeof(wl_sd_tlv_t));

		WL_SD(("WLC_E_SERVICE_FOUND "MACDBG" data_len:%d tlv_count:%d \n",
			MAC2STRDBG(dst_mac), data_len, sdo_hdr.count));

		if (tot_len > NLMSG_DEFAULT_SIZE) {
			WL_ERR(("size(%u)  > %lu not supported \n", tot_len, NLMSG_DEFAULT_SIZE));
			ret = -ENOMEM;
			goto exit;
		}

		if (wl_genl_send_msg(ndev, event, data_ptr,
			tot_len, (u8 *)&sdo_hdr, sizeof(sdo_event_t)) < 0)
			WL_ERR(("Couldn't send up the NETLINK Event \n"));
		else
			WL_SD(("GAS event sent up \n"));
	} else {
		WL_ERR(("Unsupported Event: %d \n", event));
	}

exit:
	mutex_unlock(&cfg->usr_sync);
	return ret;
}

s32 wl_cfg80211_DsdOffloadParseProto(char* proto_str, u8* proto)
{
	s32 len = -1;
	int i = 0;

	for (i = 0; i < MAX_SDO_PROTO; i++) {
		if (strncmp(proto_str, wl_sdo_protos[i].str, strlen(wl_sdo_protos[i].str)) == 0) {
			WL_SD(("Matching proto (%d) found \n", wl_sdo_protos[i].val));
			*proto = wl_sdo_protos[i].val;
			len = strlen(wl_sdo_protos[i].str);
			break;
		}
	}
	return len;
}

#define UPNP_QUERY_VER_OFFSET 3
s32 wl_sd_handle_sd_req(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 bssidx = 0;
	wl_sd_qr_t *sdreq;
	u8 proto = 0;
	s32 ret = 0;
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	u32 tot_len = len + sizeof(wl_sd_qr_t);
	u16 version = 0;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("find_idx failed\n"));
		return -EINVAL;
	}
	
	if (!buf || (len < strlen("all"))) {
		WL_ERR(("Wrong Arg\n"));
		return -EINVAL;
	}

	if (tot_len > WLC_IOCTL_MAXLEN) {
		WL_ERR(("Length > %lu not supported \n", MAX_QR_LEN));
		return -EINVAL;
	}

	sdreq = kzalloc(tot_len, kflags);
	if (!sdreq) {
		WL_ERR(("malloc failed\n"));
		return -ENOMEM;
	}

	WL_SD(("%s Len: %d\n", buf, len));
	if ((ret = wl_cfg80211_DsdOffloadParseProto(buf, &proto)) < 0) {
		WL_ERR(("Unknown proto \n"));
		goto exit;
	}

	sdreq->protocol = proto;
	buf += ret;
	buf++; 
	sdreq->transaction_id = simple_strtoul(buf, NULL, 16);
	WL_SD(("transaction_id:%d\n", sdreq->transaction_id));
	buf += sizeof(sdreq->transaction_id);

	if (*buf == '\0') {
		WL_SD(("No Query present. Proto:%d \n", proto));
		sdreq->query_len = 0;
	} else {
		buf++; 
		
		if (sdreq->protocol == SVC_RPOTYPE_UPNP) {
			
			version = simple_strtoul(buf, NULL, 16);
			buf = buf + UPNP_QUERY_VER_OFFSET;
			buf[0] = version;
			WL_SD(("Upnp version: 0x%x \n", version));
		}

		len = strlen(buf);
		WL_SD(("Len after stripping proto: %d Query: %s\n", len, buf));
		
		memcpy(sdreq->qrbuf, buf, len);
		sdreq->query_len = len;
	}

	
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_sd_req_resp", (void*)sdreq,
		tot_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("Find SVC Failed \n"));
		goto exit;
	}

	cfg->sdo->sd_state |= WL_SD_SEARCH_SVC;

exit:
	kfree(sdreq);
	return ret;
}

s32 wl_sd_handle_sd_cancel_req(
	struct net_device *dev,
	u8 *buf)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 bssidx =  wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);

	if (wldev_iovar_setbuf_bsscfg(dev, "p2po_sd_cancel", NULL,
		0, cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		bssidx, &cfg->ioctl_buf_sync) < 0) {
		WL_ERR(("Cancel SD Failed \n"));
		return -EINVAL;
	}

	cfg->sdo->sd_state &= ~WL_SD_SEARCH_SVC;

	return 0;
}

s32 wl_sd_handle_sd_add_svc(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 bssidx = 0;
	wl_sd_qr_t *sdreq;
	u8 proto = 0;
	u16 version = 0;
	s32 ret = 0;
	u8 *resp = NULL;
	u8 *query = NULL;
	u32 tot_len = len + sizeof(wl_sd_qr_t);
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;

	if (!buf || !len)
		return -EINVAL;

	WL_SD(("%s Len: %d\n", buf, len));
	if (tot_len > WLC_IOCTL_MAXLEN) {
		WL_ERR(("Query-Resp length > %d not supported \n", WLC_IOCTL_MAXLEN));
		return -ENOMEM;
	}

	sdreq = kzalloc(tot_len, kflags);
	if (!sdreq) {
		WL_ERR(("malloc failed\n"));
		return -ENOMEM;
	}

	if ((ret = wl_cfg80211_DsdOffloadParseProto(buf, &proto)) < 0) {
		WL_ERR(("Unknown Proto \n"));
		goto exit;
	}

	sdreq->protocol = proto;
	buf += ret;

	if (*buf == '\0') {
		WL_ERR(("No Query Resp pair present \n"));
		ret = -EINVAL;
		goto exit;
	}

	buf++; 
	len = strlen(buf);
	query = strsep((char **)&buf, " ");
	if (!query || !buf) {
		WL_ERR(("No Query RESP Present\n"));
		ret = -EINVAL;
		goto exit;
	}
	resp = buf;

	if (sdreq->protocol == SVC_RPOTYPE_UPNP) {
		
		version = simple_strtoul(query, NULL, 16);
		query = query + UPNP_QUERY_VER_OFFSET;
		resp = resp + UPNP_QUERY_VER_OFFSET;
		query[0] = version;
		resp[0] = version;
		WL_SD(("Upnp version: 0x%x \n", version));
	}

	sdreq->query_len = strlen(query);
	sdreq->response_len = strlen(buf);
	WL_SD(("query:%s len:%u \n", query, sdreq->query_len));
	WL_SD(("resp:%s len:%u \n", buf, sdreq->response_len));

	memcpy(sdreq->qrbuf, query, sdreq->query_len);
	memcpy((sdreq->qrbuf + sdreq->query_len), resp, sdreq->response_len);

	
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_addsvc", (void*)sdreq,
		tot_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("FW Failed in doing p2po_addsvc. RET:%d \n", ret));
		goto exit;
	}

	cfg->sdo->sd_state |= WL_SD_ADV_SVC;

exit:
	kfree(sdreq);
	return ret;
}

s32 wl_sd_handle_sd_del_svc(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 bssidx = 0;
	wl_sd_qr_t *sdreq;
	u8 proto = 0;
	s32 ret = 0;
	u32 tot_len = len + sizeof(wl_sd_qr_t);
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	u16 version = 0;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("find_idx failed\n"));
		return -EINVAL;
	}

	sdreq = (wl_sd_qr_t *)kzalloc(tot_len, kflags);
	if (!sdreq) {
		WL_ERR(("malloc failed\n"));
		ret = -ENOMEM;
		goto exit;
	}

	
	if (buf && len >= strlen("all")) {
		WL_DBG(("%s Len: %d\n", buf, len));
		if ((ret = wl_cfg80211_DsdOffloadParseProto(buf, &proto)) < 0) {
			WL_ERR(("Unknown Proto \n"));
			goto exit;
		}
		sdreq->protocol = proto;
		buf += ret;

		if (*buf == ' ') {
			
			buf++; 
			
			if (sdreq->protocol == SVC_RPOTYPE_UPNP) {
				
				version = simple_strtoul(buf, NULL, 16);
				buf = buf + UPNP_QUERY_VER_OFFSET;
				buf[0] = version;
				WL_SD(("Upnp version: 0x%x \n", version));
			}
			memcpy(sdreq->qrbuf, buf, strlen(buf));
			sdreq->query_len = strlen(buf);
			WL_SD(("Query to be deleted:%s len:%d\n", buf, sdreq->query_len));
		}
	} else {
		
		proto = 0;
	}

	sdreq->protocol = proto;
	WL_SD(("Proto: %d \n", proto));

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_delsvc", (void*)sdreq,
		tot_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("FW Failed in doing sd_delsvc. ret=%d \n", ret));
		goto exit;
	}

	cfg->sdo->sd_state &= ~WL_SD_ADV_SVC;

exit:
	if (sdreq)
		kfree(sdreq);

	return ret;
}

s32 wl_sd_handle_sd_stop_discovery(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	int ret = 0;
	int sdo_pause = 0;

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_stop", (void*)&sdo_pause,
		sizeof(sdo_pause), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_stop Failed :%d\n", ret));
		return -1;
	}

	if (wldev_iovar_setint(dev, "mpc", 1) < 0) {
		
		WL_ERR(("mpc enabling back failed\n"));
		return -1;
	}

	
	cfg->sdo->dd_state = WL_DD_STATE_IDLE;
	wl_clr_p2p_status(cfg, DISC_IN_PROGRESS);

	bzero(&cfg->sdo->sd_listen, sizeof(wl_sd_listen_t));

	
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, true);

	return ret;
}

s32 wl_sd_handle_sd_find(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	int ret = 0;
	s32 disc_bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	vndr_ie_setbuf_t *ie_setbuf;
	vndr_ie_t *vndrie;
	vndr_ie_buf_t *vndriebuf;
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	int tot_len = 0;
	uint channel = 0;

	u8 p2pie_buf[] = {
				0x09, 0x02, 0x02, 0x00, 0x27, 0x0c, 0x06, 0x05, 0x00,
				0x55, 0x53, 0x04, 0x51, 0x0b, 0x11, 0x05, 0x00, 0x55,
				0x53, 0x04, 0x51, 0x0b
			  };

	
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		return -1;
	}

	if (buf && strncmp(buf, "chan=", strlen("chan=")) == 0) {
		buf += strlen("chan=");
		channel = simple_strtol(buf, NULL, 10);
		WL_SD(("listen_chan to be set:%d\n", channel));
		if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen_channel", (void*)&channel,
			sizeof(channel), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync)) < 0) {
				WL_ERR(("p2po_listen_channel Failed :%d\n", ret));
				return -1;
		}
	}

	tot_len = sizeof(vndr_ie_setbuf_t) + sizeof(p2pie_buf);
	ie_setbuf = (vndr_ie_setbuf_t *) kzalloc(tot_len, kflags);
	if (!ie_setbuf) {
		WL_ERR(("IE memory alloc failed\n"));
		return -ENOMEM;
	}

	
	strncpy(ie_setbuf->cmd, "add", sizeof(ie_setbuf->cmd));

	vndriebuf = &ie_setbuf->vndr_ie_buffer;
	vndriebuf->iecount = htod32(1);
	vndriebuf->vndr_ie_list[0].pktflag =  htod32(16);

	vndrie =  &vndriebuf->vndr_ie_list[0].vndr_ie_data;

	vndrie->id = (uchar) DOT11_MNG_PROPR_ID;
	vndrie->len = sizeof(p2pie_buf);
	memcpy(vndrie->oui, WFA_OUI, WFA_OUI_LEN);
	memcpy(vndrie->data, p2pie_buf, sizeof(p2pie_buf));

	
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, false);

	if (wldev_iovar_setbuf_bsscfg(dev, "ie", (void*)ie_setbuf,
		tot_len, cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		disc_bssidx, &cfg->ioctl_buf_sync) < 0) {
		WL_ERR(("p2p add_ie failed \n"));
		ret = -EINVAL;
		goto exit;
	} else
		WL_SD(("p2p add_ie applied successfully len:%d \n", tot_len));

	if (wldev_iovar_setint(dev, "mpc", 0) < 0) {
		
		WL_ERR(("mpc disabling faild\n"));
		ret = -1;
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_find", NULL, 0,
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_find Failed :%d\n", ret));
		ret = -1;
		goto exit;
	}

	
	cfg->sdo->dd_state = WL_DD_STATE_SEARCH;
	wl_set_p2p_status(cfg, DISC_IN_PROGRESS);

exit:
	if (ie_setbuf)
		kfree(ie_setbuf);

	
	if (ret)
		wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, true);

	return ret;
}

s32 wl_sd_handle_sd_listen(
	struct net_device *dev,
	u8 *buf,
	int len)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	s32 bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	wl_sd_listen_t sd_listen;
	int ret = 0;
	u8 * ptr = NULL;
	uint channel = 0;

	
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		return -1;
	}

	if (wldev_iovar_setint(dev, "mpc", 0) < 0) {
		
		WL_ERR(("mpc disabling faild\n"));
		return -1;
	}

	bzero(&sd_listen, sizeof(wl_sd_listen_t));

	if (len) {
		ptr = strsep((char **)&buf, " ");
		if (ptr == NULL) {
			
			WL_ERR(("Arguments in wrong format \n"));
			return -EINVAL;
		}
		else if (strncmp(ptr, "chan=", strlen("chan=")) == 0) {
			sd_listen.interval = 65535;
			sd_listen.period = 65535;
			ptr += strlen("chan=");
			channel = simple_strtol(ptr, NULL, 10);
		}
		else {
			sd_listen.period = simple_strtol(ptr, NULL, 10);
			ptr = strsep((char **)&buf, " ");
			if (ptr == NULL) {
				WL_ERR(("Arguments in wrong format \n"));
				return -EINVAL;
			}
			sd_listen.interval = simple_strtol(ptr, NULL, 10);
			if (buf && strncmp(buf, "chan=", strlen("chan=")) == 0) {
				buf += strlen("chan=");
				channel = simple_strtol(buf, NULL, 10);
			}
		}
		WL_SD(("listen_period:%d, listen_interval:%d and listen_channel:%d\n",
			sd_listen.period, sd_listen.interval, channel));
	}
	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen_channel", (void*)&channel,
		sizeof(channel), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
			WL_ERR(("p2po_listen_channel Failed :%d\n", ret));
			return -1;
	}

	WL_SD(("p2po_listen period:%d  interval:%d \n",
		sd_listen.period, sd_listen.interval));
	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen", (void*)&sd_listen,
		sizeof(wl_sd_listen_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_listen Failed :%d\n", ret));
		return -1;
	}

	
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, false);

	
	cfg->sdo->sd_listen.interval = sd_listen.interval;
	cfg->sdo->sd_listen.period = sd_listen.period;

	
	cfg->sdo->dd_state = WL_DD_STATE_LISTEN;
	wl_set_p2p_status(cfg, DISC_IN_PROGRESS);

	return 0;
}

s32 wl_cfg80211_sd_offload(struct net_device *dev, char *cmd, char* buf, int len)
{
	int ret = 0;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	WL_SD(("Entry cmd:%s arg_len:%d \n", cmd, len));

	if (!cfg->sdo) {
		WL_SD(("Initializing SDO \n"));
		if ((ret = wl_cfg80211_sdo_init(cfg)) < 0)
			goto exit;
	}

	if (strncmp(cmd, "P2P_SD_REQ", strlen("P2P_SD_REQ")) == 0) {
		ret = wl_sd_handle_sd_req(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_CANCEL_REQ", strlen("P2P_SD_CANCEL_REQ")) == 0) {
		ret = wl_sd_handle_sd_cancel_req(dev, buf);
	} else if (strncmp(cmd, "P2P_SD_SVC_ADD", strlen("P2P_SD_SVC_ADD")) == 0) {
		ret = wl_sd_handle_sd_add_svc(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_SVC_DEL", strlen("P2P_SD_SVC_DEL")) == 0) {
		ret = wl_sd_handle_sd_del_svc(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_FIND", strlen("P2P_SD_FIND")) == 0) {
		ret = wl_sd_handle_sd_find(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_LISTEN", strlen("P2P_SD_LISTEN")) == 0) {
		ret = wl_sd_handle_sd_listen(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_STOP", strlen("P2P_STOP")) == 0) {
		ret = wl_sd_handle_sd_stop_discovery(dev, buf, len);
	} else {
		WL_ERR(("Request for Unsupported CMD:%s \n", buf));
		ret = -EINVAL;
	}

exit:
	return ret;
}
#endif 

#ifdef WLTDLS
static s32
wl_tdls_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data) {

	struct net_device *ndev = NULL;
	u32 reason = ntoh32(e->reason);
	s8 *msg = NULL;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	switch (reason) {
	case WLC_E_TDLS_PEER_DISCOVERED :
		msg = " TDLS PEER DISCOVERD ";
		break;
	case WLC_E_TDLS_PEER_CONNECTED :
#ifdef PCIE_FULL_DONGLE
		dhd_tdls_update_peer_info(ndev, TRUE, (uint8 *)&e->addr.octet[0]);
#endif 
		if (cfg->tdls_mgmt_frame) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
			cfg80211_rx_mgmt(cfgdev, cfg->tdls_mgmt_freq, 0,
				cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len,
				0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
			cfg80211_rx_mgmt(cfgdev, cfg->tdls_mgmt_freq, 0,
				cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len,
				0, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || \
	defined(WL_COMPAT_WIRELESS)
			cfg80211_rx_mgmt(cfgdev, cfg->tdls_mgmt_freq, 0,
				cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len,
				GFP_ATOMIC);
#else
			cfg80211_rx_mgmt(cfgdev, cfg->tdls_mgmt_freq,
				cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len,
				GFP_ATOMIC);
#endif 
		}
		msg = " TDLS PEER CONNECTED ";
		break;
	case WLC_E_TDLS_PEER_DISCONNECTED :
#ifdef PCIE_FULL_DONGLE
		dhd_tdls_update_peer_info(ndev, FALSE, (uint8 *)&e->addr.octet[0]);
#endif 
		if (cfg->tdls_mgmt_frame) {
			kfree(cfg->tdls_mgmt_frame);
			cfg->tdls_mgmt_frame = NULL;
			cfg->tdls_mgmt_freq = 0;
		}
		msg = "TDLS PEER DISCONNECTED ";
		break;
	}
	if (msg) {
		WL_ERR(("%s: " MACDBG " on %s ndev\n", msg, MAC2STRDBG((u8*)(&e->addr)),
			(bcmcfg_to_prmry_ndev(cfg) == ndev) ? "primary" : "secondary"));
	}
	return 0;

}
#endif  

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0)) || defined(WL_COMPAT_WIRELESS)
static s32
#if (defined(CONFIG_ARCH_MSM) && defined(TDLS_MGMT_VERSION2)) || (LINUX_VERSION_CODE < \
	KERNEL_VERSION(3, 16, 0) && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
        u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
        u32 peer_capability, const u8 *data, size_t len)
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)) && \
		(LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)))
wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *data, size_t len)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, bool initiator, const u8 *data, size_t len)
#else
wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code, const u8 *data,
	size_t len)
#endif  
{
	s32 ret = 0;
#ifdef WLTDLS
	struct bcm_cfg80211 *cfg;
	tdls_wfd_ie_iovar_t info;
	memset(&info, 0, sizeof(tdls_wfd_ie_iovar_t));
	cfg = g_bcm_cfg;

#if defined(CONFIG_ARCH_MSM) && defined(TDLS_MGMT_VERSION2)
	BCM_REFERENCE(peer_capability);
#endif  

	switch (action_code) {
	case WLAN_TDLS_SET_PROBE_WFD_IE:
		WL_ERR(("%s WLAN_TDLS_SET_PROBE_WFD_IE\n", __FUNCTION__));
		info.mode = TDLS_WFD_PROBE_IE_TX;
		memcpy(&info.data, data, len);
		info.length = len;
		break;
	case WLAN_TDLS_SET_SETUP_WFD_IE:
		WL_ERR(("%s WLAN_TDLS_SET_SETUP_WFD_IE\n", __FUNCTION__));
		info.mode = TDLS_WFD_IE_TX;
		memcpy(&info.data, data, len);
		info.length = len;
		break;
	case WLAN_TDLS_SET_WFD_ENABLED:
		WL_ERR(("%s WLAN_TDLS_SET_MODE_WFD_ENABLED\n", __FUNCTION__));
		dhd_tdls_set_mode((dhd_pub_t *)(cfg->pub), true);
		goto out;
	case WLAN_TDLS_SET_WFD_DISABLED:
		WL_ERR(("%s WLAN_TDLS_SET_MODE_WFD_DISABLED\n", __FUNCTION__));
		dhd_tdls_set_mode((dhd_pub_t *)(cfg->pub), false);
		goto out;
	default:
		WL_ERR(("Unsupported action code : %d\n", action_code));
		goto out;
	}

	ret = wldev_iovar_setbuf(dev, "tdls_wfd_ie", &info, sizeof(info),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);

	if (ret) {
		WL_ERR(("tdls_wfd_ie error %d\n", ret));
	}
out:
#endif 
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32
wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, enum nl80211_tdls_operation oper)
#else
static s32
wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, enum nl80211_tdls_operation oper)
#endif
{
	s32 ret = 0;
#ifdef WLTDLS
	struct bcm_cfg80211 *cfg;
	tdls_iovar_t info;
	dhd_pub_t *dhdp;
	bool tdls_auto_mode = false;
	cfg = g_bcm_cfg;
	dhdp = (dhd_pub_t *)(cfg->pub);
	memset(&info, 0, sizeof(tdls_iovar_t));
	if (peer) {
		memcpy(&info.ea, peer, ETHER_ADDR_LEN);
	} else {
		return -1;
	}
	switch (oper) {
	case NL80211_TDLS_DISCOVERY_REQ:
		if (memcmp(peer, (const uint8 *)BSSID_BROADCAST, ETHER_ADDR_LEN) == 0) {
			info.mode = TDLS_MANUAL_EP_WFD_TPQ;
			WL_ERR(("%s TDLS TUNNELED PRBOBE REQUEST\n", __FUNCTION__));
		} else {
			info.mode = TDLS_MANUAL_EP_DISCOVERY;
		}
		break;
	case NL80211_TDLS_SETUP:
		if (dhdp->tdls_mode == true) {
			info.mode = TDLS_MANUAL_EP_CREATE;
			tdls_auto_mode = false;
			ret = dhd_tdls_enable(dev, false, tdls_auto_mode, NULL);
			if (ret < 0) {
				return ret;
			}
		} else {
			tdls_auto_mode = true;
		}
		break;
	case NL80211_TDLS_TEARDOWN:
		info.mode = TDLS_MANUAL_EP_DELETE;
		break;
	default:
		WL_ERR(("Unsupported operation : %d\n", oper));
		goto out;
	}
	
	ret = dhd_tdls_enable(dev, true, tdls_auto_mode, NULL);
	if (ret < 0) {
		return ret;
	}
	if (info.mode) {
		ret = wldev_iovar_setbuf(dev, "tdls_endpoint", &info, sizeof(info),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (ret) {
			WL_ERR(("tdls_endpoint error %d\n", ret));
		}
	}
out:
#endif 
	return ret;
}
#endif 

s32 wl_cfg80211_set_wps_p2p_ie(struct net_device *net, char *buf, int len,
	enum wl_management_type type)
{
	struct bcm_cfg80211 *cfg;
	struct net_device *ndev = NULL;
	struct ether_addr primary_mac;
	s32 ret = 0;
	s32 bssidx = 0;
	s32 pktflag = 0;
	cfg = g_bcm_cfg;

	if (wl_get_drv_status(cfg, AP_CREATING, net)) {
		goto exit;
	} else if (wl_get_drv_status(cfg, AP_CREATED, net)) {
		ndev = net;
		bssidx = 0;
	} else if (cfg->p2p) {
		net = ndev_to_wlc_ndev(net, cfg);
		if (!cfg->p2p->on) {
			get_primary_mac(cfg, &primary_mac);
			wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);

			p2p_on(cfg) = true;
			ret = wl_cfgp2p_enable_discovery(cfg, net, NULL, 0);

			if (unlikely(ret)) {
				goto exit;
			}
		}
		if (net  == bcmcfg_to_prmry_ndev(cfg)) {
			ndev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);
			bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
		} else {
			ndev = net;
			bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
			}
	}
	if (ndev != NULL) {
		switch (type) {
			case WL_BEACON:
				pktflag = VNDR_IE_BEACON_FLAG;
				break;
			case WL_PROBE_RESP:
				pktflag = VNDR_IE_PRBRSP_FLAG;
				break;
			case WL_ASSOC_RESP:
				pktflag = VNDR_IE_ASSOCRSP_FLAG;
				break;
		}
		if (pktflag)
			ret = wl_cfg80211_set_mgmt_vndr_ies(cfg,
				ndev_to_cfgdev(ndev), bssidx, pktflag, buf, len);
	}
exit:
	return ret;
}

#ifdef WL_SUPPORT_AUTO_CHANNEL
static s32
wl_cfg80211_set_auto_channel_scan_state(struct net_device *ndev)
{
	u32 val = 0;
	s32 ret = BCME_ERROR;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	
	val = 0;

	ret = wldev_iovar_setbuf_bsscfg(ndev, "mpc", (void *)&val,
		sizeof(val), cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0,
		&cfg->ioctl_buf_sync);
	if (ret < 0) {
		WL_ERR(("set 'mpc' failed, error = %d\n", ret));
		goto done;
	}

	
	val = 1;

	ret = wldev_ioctl(ndev, WLC_UP, (void *)&val, sizeof(val), true);
	if (ret < 0) {
		WL_ERR(("set interface up failed, error = %d\n", ret));
		goto done;
	}

	
	wl_set_drv_status(cfg, SCANNING, ndev);
	if (cfg->escan_info.ndev == NULL) {
		ret = BCME_OK;
		goto done;
	}
	ret = wl_notify_escan_complete(cfg, ndev, true, true);
	if (ret < 0) {
		WL_ERR(("set scan abort failed, error = %d\n", ret));
		goto done;
	}

done:
	return ret;
}

static bool
wl_cfg80211_valid_channel_p2p(int channel)
{
	bool valid = false;

	
	if ((channel >= 1) && (channel <= 14)) {
		valid = true;
	}
	
	else if ((channel >= 36) && (channel <= 48)) {
		valid = true;
	}
	
	else if ((channel >= 149) && (channel <= 161)) {
		valid = true;
	}
	else {
		valid = false;
		WL_INFORM(("invalid P2P chanspec, channel = %d\n", channel));
	}

	return valid;
}

s32
wl_cfg80211_get_chanspecs_2g(struct net_device *ndev, void *buf, s32 buflen)
{
	s32 ret = BCME_ERROR;
	struct bcm_cfg80211 *cfg = NULL;
	wl_uint32_list_t *list = NULL;
	chanspec_t chanspec = 0;

	memset(buf, 0, buflen);

	cfg = g_bcm_cfg;
	list = (wl_uint32_list_t *)buf;
	list->count = htod32(WL_NUMCHANSPECS);

	
	chanspec |= (WL_CHANSPEC_BAND_2G | WL_CHANSPEC_BW_20 |
		WL_CHANSPEC_CTL_SB_NONE);
	chanspec = wl_chspec_host_to_driver(chanspec);

	ret = wldev_iovar_getbuf_bsscfg(ndev, "chanspecs", (void *)&chanspec,
		sizeof(chanspec), buf, buflen, 0, &cfg->ioctl_buf_sync);
	if (ret < 0) {
		WL_ERR(("get 'chanspecs' failed, error = %d\n", ret));
	}

	return ret;
}

s32
wl_cfg80211_get_chanspecs_5g(struct net_device *ndev, void *buf, s32 buflen)
{
	u32 channel = 0;
	s32 ret = BCME_ERROR;
	s32 i = 0;
	s32 j = 0;
	struct bcm_cfg80211 *cfg = NULL;
	wl_uint32_list_t *list = NULL;
	chanspec_t chanspec = 0;

	memset(buf, 0, buflen);

	cfg = g_bcm_cfg;
	list = (wl_uint32_list_t *)buf;
	list->count = htod32(WL_NUMCHANSPECS);

	
	chanspec |= (WL_CHANSPEC_BAND_5G | WL_CHANSPEC_BW_20 |
		WL_CHANSPEC_CTL_SB_NONE);
	chanspec = wl_chspec_host_to_driver(chanspec);

	ret = wldev_iovar_getbuf_bsscfg(ndev, "chanspecs", (void *)&chanspec,
		sizeof(chanspec), buf, buflen, 0, &cfg->ioctl_buf_sync);
	if (ret < 0) {
		WL_ERR(("get 'chanspecs' failed, error = %d\n", ret));
		goto done;
	}

	
	for (i = 0, j = 0; i < dtoh32(list->count); i++) {
		chanspec = (chanspec_t) dtoh32(list->element[i]);
		channel = CHSPEC_CHANNEL(chanspec);

		ret = wldev_iovar_getint(ndev, "per_chan_info", &channel);
		if (ret < 0) {
			WL_ERR(("get 'per_chan_info' failed, error = %d\n", ret));
			goto done;
		}

		if (CHANNEL_IS_RADAR(channel) ||
			!(wl_cfg80211_valid_channel_p2p(CHSPEC_CHANNEL(chanspec)))) {
			continue;
		} else {
			list->element[j] = list->element[i];
		}

		j++;
	}

	list->count = j;

done:
	return ret;
}

static s32
wl_cfg80211_get_best_channel(struct net_device *ndev, void *buf, int buflen,
	int *channel)
{
	s32 ret = BCME_ERROR;
	int chosen = 0;
	int retry = 0;

	
	ret = wldev_ioctl(ndev, WLC_START_CHANNEL_SEL, buf, buflen, true);
	if (ret < 0) {
		WL_ERR(("can't start auto channel scan, error = %d\n", ret));
		*channel = 0;
		goto done;
	}

	
	retry = CHAN_SEL_RETRY_COUNT;

	while (retry--) {
		OSL_SLEEP(CHAN_SEL_IOCTL_DELAY);

		ret = wldev_ioctl(ndev, WLC_GET_CHANNEL_SEL, &chosen, sizeof(chosen),
			false);
		if ((ret == 0) && (dtoh32(chosen) != 0)) {
			*channel = (u16)(chosen & 0x00FF);
			WL_INFORM(("selected channel = %d\n", *channel));
			break;
		}
		WL_INFORM(("attempt = %d, ret = %d, chosen = %d\n",
			(CHAN_SEL_RETRY_COUNT - retry), ret, dtoh32(chosen)));
	}

	if (retry <= 0)	{
		WL_ERR(("failure, auto channel selection timed out\n"));
		*channel = 0;
		ret = BCME_ERROR;
	}

done:
	return ret;
}

static s32
wl_cfg80211_restore_auto_channel_scan_state(struct net_device *ndev)
{
	u32 val = 0;
	s32 ret = BCME_ERROR;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	
	wl_clr_drv_status(cfg, SCANNING, ndev);

	
	val = 1;

	ret = wldev_iovar_setbuf_bsscfg(ndev, "mpc", (void *)&val,
		sizeof(val), cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0,
		&cfg->ioctl_buf_sync);
	if (ret < 0) {
		WL_ERR(("set 'mpc' failed, error = %d\n", ret));
	}

	return ret;
}

s32
wl_cfg80211_get_best_channels(struct net_device *dev, char* cmd, int total_len)
{
	int channel = 0;
	s32 ret = BCME_ERROR;
	u8 *buf = NULL;
	char *pos = cmd;
	struct bcm_cfg80211 *cfg = NULL;
	struct net_device *ndev = NULL;

	memset(cmd, 0, total_len);

	buf = kmalloc(CHANSPEC_BUF_SIZE, GFP_KERNEL);
	if (buf == NULL) {
		WL_ERR(("failed to allocate chanspec buffer\n"));
		return -ENOMEM;
	}

	cfg = g_bcm_cfg;
	ndev = bcmcfg_to_prmry_ndev(cfg);

	ret = wl_cfg80211_set_auto_channel_scan_state(ndev);
	if (ret < 0) {
		WL_ERR(("can't set auto channel scan state, error = %d\n", ret));
		goto done;
	}

	
	ret = wl_cfg80211_get_chanspecs_2g(ndev, (void *)buf, CHANSPEC_BUF_SIZE);
	if (ret < 0) {
		WL_ERR(("can't get chanspecs in 2.4GHz, error = %d\n", ret));
		goto done;
	}

	ret = wl_cfg80211_get_best_channel(ndev, (void *)buf, CHANSPEC_BUF_SIZE,
		&channel);
	if (ret < 0) {
		WL_ERR(("can't select best channel scan in 2.4GHz, error = %d\n", ret));
		goto done;
	}

	if (CHANNEL_IS_2G(channel)) {
		channel = ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	} else {
		WL_ERR(("invalid 2.4GHz channel, channel = %d\n", channel));
		channel = 0;
	}

	pos += snprintf(pos, total_len, "%04d ", channel);

	
	ret = wl_cfg80211_get_chanspecs_5g(ndev, (void *)buf, CHANSPEC_BUF_SIZE);
	if (ret < 0) {
		WL_ERR(("can't get chanspecs in 5GHz, error = %d\n", ret));
		goto done;
	}

	ret = wl_cfg80211_get_best_channel(ndev, (void *)buf, CHANSPEC_BUF_SIZE,
		&channel);
	if (ret < 0) {
		WL_ERR(("can't select best channel scan in 5GHz, error = %d\n", ret));
		goto done;
	}

	if (CHANNEL_IS_5G(channel)) {
		channel = ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
	} else {
		WL_ERR(("invalid 5GHz channel, channel = %d\n", channel));
		channel = 0;
	}

	pos += snprintf(pos, total_len, "%04d ", channel);

	
	pos += snprintf(pos, total_len, "%04d ", channel);

done:
	if (NULL != buf) {
		kfree(buf);
	}

	
	ret = wl_cfg80211_restore_auto_channel_scan_state(ndev);
	if (ret < 0) {
		WL_ERR(("can't restore auto channel scan state, error = %d\n", ret));
	}

	return (pos - cmd);
}
#endif 

static const struct rfkill_ops wl_rfkill_ops = {
	.set_block = wl_rfkill_set
};

static int wl_rfkill_set(void *data, bool blocked)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)data;

	WL_DBG(("Enter \n"));
	WL_DBG(("RF %s\n", blocked ? "blocked" : "unblocked"));

	if (!cfg)
		return -EINVAL;

	cfg->rf_blocked = blocked;

	return 0;
}

static int wl_setup_rfkill(struct bcm_cfg80211 *cfg, bool setup)
{
	s32 err = 0;

	WL_DBG(("Enter \n"));
	if (!cfg)
		return -EINVAL;
	if (setup) {
		cfg->rfkill = rfkill_alloc("brcmfmac-wifi",
			wl_cfg80211_get_parent_dev(),
			RFKILL_TYPE_WLAN, &wl_rfkill_ops, (void *)cfg);

		if (!cfg->rfkill) {
			err = -ENOMEM;
			goto err_out;
		}

		err = rfkill_register(cfg->rfkill);

		if (err)
			rfkill_destroy(cfg->rfkill);
	} else {
		if (!cfg->rfkill) {
			err = -ENOMEM;
			goto err_out;
		}

		rfkill_unregister(cfg->rfkill);
		rfkill_destroy(cfg->rfkill);
	}

err_out:
	return err;
}

#ifdef DEBUGFS_CFG80211
static ssize_t
wl_debuglevel_write(struct file *file, const char __user *userbuf,
	size_t count, loff_t *ppos)
{
	char tbuf[S_SUBLOGLEVEL * ARRAYSIZE(sublogname_map)], sublog[S_SUBLOGLEVEL];
	char *params, *token, *colon;
	uint i, tokens, log_on = 0;
	memset(tbuf, 0, sizeof(tbuf));
	memset(sublog, 0, sizeof(sublog));
	if (copy_from_user(&tbuf, userbuf, min_t(size_t, (sizeof(tbuf) - 1), count)))
		return -EFAULT;

	params = &tbuf[0];
	colon = strchr(params, '\n');
	if (colon != NULL)
		*colon = '\0';
	while ((token = strsep(&params, " ")) != NULL) {
		memset(sublog, 0, sizeof(sublog));
		if (token == NULL || !*token)
			break;
		if (*token == '\0')
			continue;
		colon = strchr(token, ':');
		if (colon != NULL) {
			*colon = ' ';
		}
		tokens = sscanf(token, "%s %u", sublog, &log_on);
		if (colon != NULL)
			*colon = ':';

		if (tokens == 2) {
				for (i = 0; i < ARRAYSIZE(sublogname_map); i++) {
					if (!strncmp(sublog, sublogname_map[i].sublogname,
						strlen(sublogname_map[i].sublogname))) {
						if (log_on)
							wl_dbg_level |=
							(sublogname_map[i].log_level);
						else
							wl_dbg_level &=
							~(sublogname_map[i].log_level);
					}
				}
		} else
			WL_ERR(("%s: can't parse '%s' as a "
			       "SUBMODULE:LEVEL (%d tokens)\n",
			       tbuf, token, tokens));


	}
	return count;
}

static ssize_t
wl_debuglevel_read(struct file *file, char __user *user_buf,
	size_t count, loff_t *ppos)
{
	char *param;
	char tbuf[S_SUBLOGLEVEL * ARRAYSIZE(sublogname_map)];
	uint i;
	memset(tbuf, 0, sizeof(tbuf));
	param = &tbuf[0];
	for (i = 0; i < ARRAYSIZE(sublogname_map); i++) {
		param += snprintf(param, sizeof(tbuf) - 1, "%s:%d ",
			sublogname_map[i].sublogname,
			(wl_dbg_level & sublogname_map[i].log_level) ? 1 : 0);
	}
	*param = '\n';
	return simple_read_from_buffer(user_buf, count, ppos, tbuf, strlen(&tbuf[0]));

}
static const struct file_operations fops_debuglevel = {
	.open = NULL,
	.write = wl_debuglevel_write,
	.read = wl_debuglevel_read,
	.owner = THIS_MODULE,
	.llseek = NULL,
};

static s32 wl_setup_debugfs(struct bcm_cfg80211 *cfg)
{
	s32 err = 0;
	struct dentry *_dentry;
	if (!cfg)
		return -EINVAL;
	cfg->debugfs = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!cfg->debugfs || IS_ERR(cfg->debugfs)) {
		if (cfg->debugfs == ERR_PTR(-ENODEV))
			WL_ERR(("Debugfs is not enabled on this kernel\n"));
		else
			WL_ERR(("Can not create debugfs directory\n"));
		cfg->debugfs = NULL;
		goto exit;

	}
	_dentry = debugfs_create_file("debug_level", S_IRUSR | S_IWUSR,
		cfg->debugfs, cfg, &fops_debuglevel);
	if (!_dentry || IS_ERR(_dentry)) {
		WL_ERR(("failed to create debug_level debug file\n"));
		wl_free_debugfs(cfg);
	}
exit:
	return err;
}
static s32 wl_free_debugfs(struct bcm_cfg80211 *cfg)
{
	if (!cfg)
		return -EINVAL;
	if (cfg->debugfs)
		debugfs_remove_recursive(cfg->debugfs);
	cfg->debugfs = NULL;
	return 0;
}
#endif 

struct device *wl_cfg80211_get_parent_dev(void)
{
	return cfg80211_parent_dev;
}

void wl_cfg80211_set_parent_dev(void *dev)
{
	cfg80211_parent_dev = dev;
}

static void wl_cfg80211_clear_parent_dev(void)
{
	cfg80211_parent_dev = NULL;
}

void get_primary_mac(struct bcm_cfg80211 *cfg, struct ether_addr *mac)
{
	wldev_iovar_getbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg), "cur_etheraddr", NULL,
		0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);
	memcpy(mac->octet, cfg->ioctl_buf, ETHER_ADDR_LEN);
}
static bool check_dev_role_integrity(struct bcm_cfg80211 *cfg, u32 dev_role)
{
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	if (((dev_role == NL80211_IFTYPE_AP) &&
		!(dhd->op_mode & DHD_FLAG_HOSTAP_MODE)) ||
		((dev_role == NL80211_IFTYPE_P2P_GO) &&
		!(dhd->op_mode & DHD_FLAG_P2P_GO_MODE)))
	{
		WL_ERR(("device role select failed role:%d op_mode:%d \n", dev_role, dhd->op_mode));
		return false;
	}
	return true;
}

int wl_cfg80211_do_driver_init(struct net_device *net)
{
	struct bcm_cfg80211 *cfg = *(struct bcm_cfg80211 **)netdev_priv(net);

	if (!cfg || !cfg->wdev)
		return -EINVAL;

	if (dhd_do_driver_init(cfg->wdev->netdev) < 0)
		return -1;

	return 0;
}

void wl_cfg80211_enable_trace(bool set, u32 level)
{
	if (set)
		wl_dbg_level = level & WL_DBG_LEVEL;
	else
		wl_dbg_level |= (WL_DBG_LEVEL & level);
}
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	2, 0))
static s32
wl_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
	bcm_struct_cfgdev *cfgdev, u64 cookie)
{

	return 0;
}
#endif 

#ifdef WL11U
bcm_tlv_t *
wl_cfg80211_find_interworking_ie(u8 *parse, u32 len)
{
	bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, (u32)len, DOT11_MNG_INTERWORKING_ID))) {
			return (bcm_tlv_t *)ie;
	}
	return NULL;
}


static s32
wl_cfg80211_add_iw_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx, s32 pktflag,
            uint8 ie_id, uint8 *data, uint8 data_len)
{
	s32 err = BCME_OK;
	s32 buf_len;
	s32 iecount;
	ie_setbuf_t *ie_setbuf;

	if (ie_id != DOT11_MNG_INTERWORKING_ID)
		return BCME_UNSUPPORTED;

	
	if ((pktflag & ~(VNDR_IE_BEACON_FLAG | VNDR_IE_PRBRSP_FLAG |
	            VNDR_IE_ASSOCRSP_FLAG | VNDR_IE_AUTHRSP_FLAG |
	            VNDR_IE_PRBREQ_FLAG | VNDR_IE_ASSOCREQ_FLAG|
	            VNDR_IE_CUSTOM_FLAG))) {
		WL_ERR(("cfg80211 Add IE: Invalid packet flag 0x%x\n", pktflag));
		return -1;
	}

	
	pktflag = htod32(pktflag);

	buf_len = sizeof(ie_setbuf_t) + data_len - 1;
	ie_setbuf = (ie_setbuf_t *) kzalloc(buf_len, GFP_KERNEL);

	if (!ie_setbuf) {
		WL_ERR(("Error allocating buffer for IE\n"));
		return -ENOMEM;
	}

	if (cfg->iw_ie_len == data_len && !memcmp(cfg->iw_ie, data, data_len)) {
		WL_ERR(("Previous IW IE is equals to current IE\n"));
		err = BCME_OK;
		goto exit;
	}

	strncpy(ie_setbuf->cmd, "add", VNDR_IE_CMD_LEN - 1);
	ie_setbuf->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

	
	iecount = htod32(1);
	memcpy((void *)&ie_setbuf->ie_buffer.iecount, &iecount, sizeof(int));
	memcpy((void *)&ie_setbuf->ie_buffer.ie_list[0].pktflag, &pktflag, sizeof(uint32));

	
	ie_setbuf->ie_buffer.ie_list[0].ie_data.id = ie_id;

	
	if (cfg->iw_ie_len != 0) {
		WL_DBG(("Different IW_IE was already set. clear first\n"));

		ie_setbuf->ie_buffer.ie_list[0].ie_data.len = 0;

		err = wldev_iovar_setbuf_bsscfg(ndev, "ie", ie_setbuf, buf_len,
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);

		if (err != BCME_OK)
			goto exit;
	}

	ie_setbuf->ie_buffer.ie_list[0].ie_data.len = data_len;
	memcpy((uchar *)&ie_setbuf->ie_buffer.ie_list[0].ie_data.data[0], data, data_len);

	err = wldev_iovar_setbuf_bsscfg(ndev, "ie", ie_setbuf, buf_len,
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);

	if (err == BCME_OK) {
		memcpy(cfg->iw_ie, data, data_len);
		cfg->iw_ie_len = data_len;
		cfg->wl11u = TRUE;

		err = wldev_iovar_setint_bsscfg(ndev, "grat_arp", 1, bssidx);
	}

exit:
	if (ie_setbuf)
		kfree(ie_setbuf);
	return err;
}
#endif 

#ifdef WL_HOST_BAND_MGMT
s32
wl_cfg80211_set_band(struct net_device *ndev, int band)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	int ret = 0;
	char ioctl_buf[50];

	if ((band < WLC_BAND_AUTO) || (band > WLC_BAND_2G)) {
		WL_ERR(("Invalid band\n"));
		return -EINVAL;
	}

	if ((ret = wldev_iovar_setbuf(ndev, "roam_band", &band,
		sizeof(int), ioctl_buf, sizeof(ioctl_buf), NULL)) < 0) {
		WL_ERR(("seting roam_band failed code=%d\n", ret));
		return ret;
	}

	WL_DBG(("Setting band to %d\n", band));
	cfg->curr_band = band;

	return 0;
}

void
wl_set_band_disconnect(struct net_device *ndev)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	if (wl_check_channel_band_mismatch(cfg->curr_band, cfg->channel)) {
		wl_clr_drv_status(cfg, CONNECTED, ndev);
		if (! wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
			scb_val_t scbval;
			u8 *curbssid;
			s32 err = 0;

			curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
			wl_set_drv_status(cfg, DISCONNECTING, ndev);
			scbval.val = 0;
			memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
			err = wldev_ioctl(ndev, WLC_DISASSOC, &scbval,
				sizeof(scb_val_t), true);
			if (unlikely(err)) {
				wl_clr_drv_status(cfg, DISCONNECTING, ndev);
				WL_ERR(("error(%d)\n", err));
				return;
			}
			cfg80211_disconnected(ndev, WLAN_REASON_DEAUTH_LEAVING,
				NULL, 0, GFP_KERNEL);
			wl_link_down(cfg);
			wl_init_prof(cfg, ndev);
		}
	}
}

bool
wl_check_channel_band_mismatch(int band, int channel)
{
	bool ret = false;

	if ((band == WLC_BAND_2G && channel > CH_MAX_2G_CHANNEL) ||
		(band == WLC_BAND_5G && channel <= CH_MAX_2G_CHANNEL)) {
		ret = true;
	}

	return ret;
}
#endif 
s32
wl_cfg80211_dfs_ap_move(struct net_device *ndev, char *data, char *command, int total_len)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	char ioctl_buf[50];
	int err = 0;
	uint32 val = 0;
	chanspec_t chanspec = 0;
	int abort;
	int bytes_written = 0;
	wl_dfs_ap_move_status_t *status;
	char chanbuf[CHANSPEC_STR_LEN];
	const char *dfs_state_str[DFS_SCAN_S_MAX] = {
		"Radar Free On Channel",
		"Radar Found On Channel",
		"Radar Scan In Progress",
		"Radar Scan Aborted",
		"RSDB Mode switch in Progress For Scan"
	};
	if (ndev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		bytes_written = snprintf(command, total_len, "AP is not UP\n");
		return bytes_written;
	}
	if (!*data) {
		if ((err = wldev_iovar_getbuf(ndev, "dfs_ap_move", NULL, 0,
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN,  &cfg->ioctl_buf_sync))) {
			WL_ERR(("setting dfs_ap_move failed with err=%d \n", err));
			return err;
		}
		status = (wl_dfs_ap_move_status_t *)cfg->ioctl_buf;

		if (status->version != WL_DFS_AP_MOVE_VERSION) {
			err = BCME_UNSUPPORTED;
			WL_ERR(("err=%d version=%d\n", err, status->version));
			return err;
		}

		if (status->move_status != (int8) DFS_SCAN_S_IDLE) {
			chanspec = wl_chspec_driver_to_host(status->chanspec);
			if (chanspec != 0 && chanspec != INVCHANSPEC) {
				wf_chspec_ntoa(chanspec, chanbuf);
				bytes_written = snprintf(command, total_len,
					"AP Target Chanspec %s (0x%x)\n", chanbuf, chanspec);
			}
			bytes_written += snprintf(command + bytes_written, total_len,
					 "%s\n", dfs_state_str[status->move_status]);
			return bytes_written;
		} else {
			bytes_written = snprintf(command, total_len, "dfs AP move in IDLE state\n");
			return bytes_written;
		}

	}

	abort = bcm_atoi(data);
	if (abort == -1) {
		if ((err = wldev_iovar_setbuf(ndev, "dfs_ap_move", &abort,
				sizeof(int), ioctl_buf, sizeof(ioctl_buf), NULL)) < 0) {
			WL_ERR(("seting dfs_ap_move failed with err %d\n", err));
			return err;
		}
	} else {
		chanspec = wf_chspec_aton(data);
		if (chanspec != 0) {
			val = wl_chspec_host_to_driver(chanspec);
			if (val != INVCHANSPEC) {
				if ((err = wldev_iovar_setbuf(ndev, "dfs_ap_move", &val,
					sizeof(int), ioctl_buf, sizeof(ioctl_buf), NULL)) < 0) {
					WL_ERR(("seting dfs_ap_move failed with err %d\n", err));
					return err;
				}
				WL_DBG((" set dfs_ap_move successfull"));
			} else {
				err = BCME_USAGE_ERROR;
			}
		}
	}
	return err;
}

#if defined(DHCP_SCAN_SUPPRESS)
static void wl_cfg80211_scan_supp_timerfunc(ulong data)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)data;

	WL_DBG(("Enter \n"));
	schedule_work(&cfg->wlan_work);
}

int wl_cfg80211_scan_suppress(struct net_device *dev, int suppress)
{
	int ret = 0;
	struct wireless_dev *wdev;
	struct bcm_cfg80211 *cfg;
	if (!dev || ((suppress != 0) && (suppress != 1))) {
		ret = -EINVAL;
		goto exit;
	}
	wdev = ndev_to_wdev(dev);
	if (!wdev) {
		ret = -EINVAL;
		goto exit;
	}
	cfg = (struct bcm_cfg80211 *)wiphy_priv(wdev->wiphy);
	if (!cfg) {
		ret = -EINVAL;
		goto exit;
	}

	if (suppress == cfg->scan_suppressed) {
		WL_DBG(("No change in scan_suppress state. Ignoring cmd..\n"));
		return 0;
	}

	if (timer_pending(&cfg->scan_supp_timer))
		del_timer_sync(&cfg->scan_supp_timer);

	if ((ret = wldev_ioctl(dev, WLC_SET_SCANSUPPRESS,
		&suppress, sizeof(int), true)) < 0) {
		WL_ERR(("Scan suppress setting failed ret:%d \n", ret));
	} else {
		WL_DBG(("Scan suppress %s \n", suppress ? "Enabled" : "Disabled"));
		cfg->scan_suppressed = suppress;
	}

	
	if (cfg->scan_suppressed) {
		if (ret) {
			WL_ERR(("Retry scan_suppress reset at a later time \n"));
			mod_timer(&cfg->scan_supp_timer,
				jiffies + msecs_to_jiffies(WL_SCAN_SUPPRESS_RETRY));
		} else {
			WL_DBG(("Start wlan_timer to clear of scan_suppress \n"));
			mod_timer(&cfg->scan_supp_timer,
				jiffies + msecs_to_jiffies(WL_SCAN_SUPPRESS_TIMEOUT));
		}
	}
exit:
	return ret;
}
#endif 

int wl_cfg80211_scan_stop(bcm_struct_cfgdev *cfgdev)
{
	struct bcm_cfg80211 *cfg = NULL;
	struct net_device *ndev = NULL;
	unsigned long flags;
	int clear_flag = 0;
	int ret = 0;

	WL_TRACE(("Enter\n"));

	cfg = g_bcm_cfg;
	if (!cfg)
		return -EINVAL;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
#ifdef WL_CFG80211_P2P_DEV_IF
	if (cfg->scan_request && cfg->scan_request->wdev == cfgdev) {
#else
	if (cfg->scan_request && cfg->scan_request->dev == cfgdev) {
#endif
		cfg80211_scan_done(cfg->scan_request, true);
		cfg->scan_request = NULL;
		clear_flag = 1;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);

	if (clear_flag)
		wl_clr_drv_status(cfg, SCANNING, ndev);

	return ret;
}

bool wl_cfg80211_is_vsdb_mode(void)
{
	return (g_bcm_cfg && g_bcm_cfg->vsdb_mode);
}

void* wl_cfg80211_get_dhdp()
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;

	return cfg->pub;
}

bool wl_cfg80211_is_p2p_active(void)
{
	return (g_bcm_cfg && g_bcm_cfg->p2p);
}

static void wl_cfg80211_work_handler(struct work_struct * work)
{
	struct bcm_cfg80211 *cfg = NULL;
	struct net_info *iter, *next;
	s32 err = BCME_OK;
	s32 pm = PM_FAST;
	BCM_SET_CONTAINER_OF(cfg, work, struct bcm_cfg80211, pm_enable_work.work);
	WL_DBG(("Enter \n"));
	if (cfg->pm_enable_work_on) {
		cfg->pm_enable_work_on = false;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
		for_each_ndev(cfg, iter, next) {
			
			if (iter->ndev) {
				if (!wl_get_drv_status(cfg, CONNECTED, iter->ndev) ||
					(wl_get_mode_by_netdev(cfg, iter->ndev) != WL_MODE_BSS &&
					wl_get_mode_by_netdev(cfg, iter->ndev) != WL_MODE_IBSS))
					continue;
				if (iter->ndev) {
					if ((err = wldev_ioctl(iter->ndev, WLC_SET_PM,
						&pm, sizeof(pm), true)) != 0) {
						if (err == -ENODEV)
							WL_DBG(("%s:netdev not ready\n",
								iter->ndev->name));
						else
							WL_ERR(("%s:error (%d)\n",
								iter->ndev->name, err));
					} else
						wl_cfg80211_update_power_mode(iter->ndev);
				}
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	}
#if defined(DHCP_SCAN_SUPPRESS)
	else if (cfg->scan_suppressed) {
		
		WL_ERR(("Clean up from timer after %d msec\n", WL_SCAN_SUPPRESS_TIMEOUT));
		wl_cfg80211_scan_suppress(bcmcfg_to_prmry_ndev(cfg), 0);
	}
#endif 
}

u8
wl_get_action_category(void *frame, u32 frame_len)
{
	u8 category;
	u8 *ptr = (u8 *)frame;
	if (frame == NULL)
		return DOT11_ACTION_CAT_ERR_MASK;
	if (frame_len < DOT11_ACTION_HDR_LEN)
		return DOT11_ACTION_CAT_ERR_MASK;
	category = ptr[DOT11_ACTION_CAT_OFF];
	WL_INFORM(("Action Category: %d\n", category));
	return category;
}

int
wl_get_public_action(void *frame, u32 frame_len, u8 *ret_action)
{
	u8 *ptr = (u8 *)frame;
	if (frame == NULL || ret_action == NULL)
		return BCME_ERROR;
	if (frame_len < DOT11_ACTION_HDR_LEN)
		return BCME_ERROR;
	if (DOT11_ACTION_CAT_PUBLIC != wl_get_action_category(frame, frame_len))
		return BCME_ERROR;
	*ret_action = ptr[DOT11_ACTION_ACT_OFF];
	WL_INFORM(("Public Action : %d\n", *ret_action));
	return BCME_OK;
}

#ifdef WLFBT
void
wl_cfg80211_get_fbt_key(uint8 *key)
{
	memcpy(key, g_bcm_cfg->fbt_key, FBT_KEYLEN);
}
#endif 

static int
wl_cfg80211_delayed_roam(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const struct ether_addr *bssid)
{
	s32 err;
	wl_event_msg_t e;

	bzero(&e, sizeof(e));
	e.event_type = cpu_to_be32(WLC_E_BSSID);
	memcpy(&e.addr, bssid, ETHER_ADDR_LEN);
	
	WL_INFORM(("Delayed roam to " MACDBG "\n", MAC2STRDBG((u8*)(bssid))));
	err = wl_notify_roaming_status(cfg, ndev_to_cfgdev(ndev), &e, NULL);

	return err;
}

static s32
wl_cfg80211_parse_vndr_ies(u8 *parse, u32 len,
    struct parsed_vndr_ies *vndr_ies)
{
	s32 err = BCME_OK;
	vndr_ie_t *vndrie;
	bcm_tlv_t *ie;
	struct parsed_vndr_ie_info *parsed_info;
	u32 count = 0;
	s32 remained_len;

	remained_len = (s32)len;
	memset(vndr_ies, 0, sizeof(*vndr_ies));

	WL_INFORM(("---> len %d\n", len));
	ie = (bcm_tlv_t *) parse;
	if (!bcm_valid_tlv(ie, remained_len))
		ie = NULL;
	while (ie) {
		if (count >= MAX_VNDR_IE_NUMBER)
			break;
		if (ie->id == DOT11_MNG_VS_ID) {
			vndrie = (vndr_ie_t *) ie;
			
			if (vndrie->len < (VNDR_IE_MIN_LEN + 1)) {
				WL_ERR(("%s: invalid vndr ie. length is too small %d\n",
					__FUNCTION__, vndrie->len));
				goto end;
			}
			
			if (!bcmp(vndrie->oui, (u8*)WPA_OUI, WPA_OUI_LEN) &&
				((vndrie->data[0] == WPA_OUI_TYPE) ||
				(vndrie->data[0] == WME_OUI_TYPE))) {
				CFGP2P_DBG(("Found WPA/WME oui. Do not add it\n"));
				goto end;
			}

			parsed_info = &vndr_ies->ie_info[count++];

			
			parsed_info->ie_ptr = (char *)vndrie;
			parsed_info->ie_len = (vndrie->len + TLV_HDR_LEN);
			memcpy(&parsed_info->vndrie, vndrie, sizeof(vndr_ie_t));
			vndr_ies->count = count;

			WL_DBG(("\t ** OUI %02x %02x %02x, type 0x%02x \n",
			parsed_info->vndrie.oui[0], parsed_info->vndrie.oui[1],
			parsed_info->vndrie.oui[2], parsed_info->vndrie.data[0]));
		}
end:
		ie = bcm_next_tlv(ie, &remained_len);
	}
	return err;
}

s32
wl_cfg80211_clear_per_bss_ies(struct bcm_cfg80211 *cfg, s32 bssidx)
{
	s32 index;
	struct net_info *netinfo;
	s32 vndrie_flag[] = {VNDR_IE_BEACON_FLAG, VNDR_IE_PRBRSP_FLAG,
		VNDR_IE_ASSOCRSP_FLAG, VNDR_IE_PRBREQ_FLAG, VNDR_IE_ASSOCREQ_FLAG};

	netinfo = wl_get_netinfo_by_bssidx(cfg, bssidx);
	if (!netinfo || !netinfo->wdev) {
		WL_ERR(("netinfo or netinfo->wdev is NULL\n"));
		return -1;
	}

	WL_DBG(("clear management vendor IEs for bssidx:%d \n", bssidx));
	
	for (index = 0; index < ARRAYSIZE(vndrie_flag); index++) {
		if (wl_cfg80211_set_mgmt_vndr_ies(cfg, wdev_to_cfgdev(netinfo->wdev),
			bssidx, vndrie_flag[index], NULL, 0) < 0)
			WL_ERR(("vndr_ies clear failed. Ignoring.. \n"));
	}

	return 0;
}

s32
wl_cfg80211_clear_mgmt_vndr_ies(struct bcm_cfg80211 *cfg)
{
	struct net_info *iter, *next;

	WL_DBG(("clear management vendor IEs \n"));
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		wl_cfg80211_clear_per_bss_ies(cfg, iter->bssidx);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	return 0;
}

#define WL_VNDR_IE_MAXLEN 2048
static s8 g_mgmt_ie_buf[WL_VNDR_IE_MAXLEN];
int
wl_cfg80211_set_mgmt_vndr_ies(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	s32 bssidx, s32 pktflag, const u8 *vndr_ie, u32 vndr_ie_len)
{
	struct net_device *ndev = NULL;
	s32 ret = BCME_OK;
	u8  *curr_ie_buf = NULL;
	u8  *mgmt_ie_buf = NULL;
	u32 mgmt_ie_buf_len = 0;
	u32 *mgmt_ie_len = 0;
	u32 del_add_ie_buf_len = 0;
	u32 total_ie_buf_len = 0;
	u32 parsed_ie_buf_len = 0;
	struct parsed_vndr_ies old_vndr_ies;
	struct parsed_vndr_ies new_vndr_ies;
	s32 i;
	u8 *ptr;
	s32 remained_buf_len;
	wl_bss_vndr_ies_t *ies = NULL;
	struct net_info *netinfo;

	WL_DBG(("Enter. pktflag:0x%x bssidx:%x vnd_ie_len:%d \n",
		pktflag, bssidx, vndr_ie_len));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (bssidx > WL_MAX_IFS) {
		WL_ERR(("bssidx > supported concurrent Ifaces \n"));
		return -EINVAL;
	}

	netinfo = wl_get_netinfo_by_bssidx(cfg, bssidx);
	if (!netinfo) {
		WL_ERR(("net_info ptr is NULL \n"));
		return -EINVAL;
	}

	
	memset(g_mgmt_ie_buf, 0, sizeof(g_mgmt_ie_buf));
	curr_ie_buf = g_mgmt_ie_buf;
	ies = &netinfo->bss.ies;

	switch (pktflag) {
		case VNDR_IE_PRBRSP_FLAG :
			mgmt_ie_buf = ies->probe_res_ie;
			mgmt_ie_len = &ies->probe_res_ie_len;
			mgmt_ie_buf_len = sizeof(ies->probe_res_ie);
			break;
		case VNDR_IE_ASSOCRSP_FLAG :
			mgmt_ie_buf = ies->assoc_res_ie;
			mgmt_ie_len = &ies->assoc_res_ie_len;
			mgmt_ie_buf_len = sizeof(ies->assoc_res_ie);
			break;
		case VNDR_IE_BEACON_FLAG :
			mgmt_ie_buf = ies->beacon_ie;
			mgmt_ie_len = &ies->beacon_ie_len;
			mgmt_ie_buf_len = sizeof(ies->beacon_ie);
			break;
		case VNDR_IE_PRBREQ_FLAG :
			mgmt_ie_buf = ies->probe_req_ie;
			mgmt_ie_len = &ies->probe_req_ie_len;
			mgmt_ie_buf_len = sizeof(ies->probe_req_ie);
			break;
		case VNDR_IE_ASSOCREQ_FLAG :
			mgmt_ie_buf = ies->assoc_req_ie;
			mgmt_ie_len = &ies->assoc_req_ie_len;
			mgmt_ie_buf_len = sizeof(ies->assoc_req_ie);
			break;
		default:
			mgmt_ie_buf = NULL;
			mgmt_ie_len = NULL;
			WL_ERR(("not suitable packet type (%d)\n", pktflag));
			return BCME_ERROR;
	}

	if (vndr_ie_len > mgmt_ie_buf_len) {
		WL_ERR(("extra IE size too big\n"));
		ret = -ENOMEM;
	} else {
		
		if (vndr_ie && vndr_ie_len && curr_ie_buf) {
			ptr = curr_ie_buf;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
			if ((ret = wl_cfg80211_parse_vndr_ies((u8 *)vndr_ie,
				vndr_ie_len, &new_vndr_ies)) < 0) {
				WL_ERR(("parse vndr ie failed \n"));
				goto exit;
			}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
			for (i = 0; i < new_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
					&new_vndr_ies.ie_info[i];

				if ((parsed_ie_buf_len + vndrie_info->ie_len) > WL_VNDR_IE_MAXLEN) {
					WL_ERR(("IE size is too big (%d > %d)\n",
						parsed_ie_buf_len, WL_VNDR_IE_MAXLEN));
					ret = -EINVAL;
					goto exit;
				}

				memcpy(ptr + parsed_ie_buf_len, vndrie_info->ie_ptr,
					vndrie_info->ie_len);
				parsed_ie_buf_len += vndrie_info->ie_len;
			}
		}

		if (mgmt_ie_buf != NULL) {
			if (parsed_ie_buf_len && (parsed_ie_buf_len == *mgmt_ie_len) &&
				(memcmp(mgmt_ie_buf, curr_ie_buf, parsed_ie_buf_len) == 0)) {
				WL_INFORM(("Previous mgmt IE is equals to current IE"));
				goto exit;
			}

			
			if ((ret = wl_cfg80211_parse_vndr_ies(mgmt_ie_buf, *mgmt_ie_len,
				&old_vndr_ies)) < 0) {
				WL_ERR(("parse vndr ie failed \n"));
				goto exit;
			}
			
			for (i = 0; i < old_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
				&old_vndr_ies.ie_info[i];

				WL_INFORM(("DELETED ID : %d, Len: %d , OUI:%02x:%02x:%02x\n",
					vndrie_info->vndrie.id, vndrie_info->vndrie.len,
					vndrie_info->vndrie.oui[0], vndrie_info->vndrie.oui[1],
					vndrie_info->vndrie.oui[2]));

				del_add_ie_buf_len = wl_cfgp2p_vndr_ie(cfg, curr_ie_buf,
					pktflag, vndrie_info->vndrie.oui,
					vndrie_info->vndrie.id,
					vndrie_info->ie_ptr + VNDR_IE_FIXED_LEN,
					vndrie_info->ie_len - VNDR_IE_FIXED_LEN,
					"del");

				curr_ie_buf += del_add_ie_buf_len;
				total_ie_buf_len += del_add_ie_buf_len;
			}
		}

		*mgmt_ie_len = 0;
		
		if (mgmt_ie_buf && parsed_ie_buf_len) {
			ptr = mgmt_ie_buf;

			remained_buf_len = mgmt_ie_buf_len;

			
			for (i = 0; i < new_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
					&new_vndr_ies.ie_info[i];

				WL_INFORM(("ADDED ID : %d, Len: %d(%d), OUI:%02x:%02x:%02x\n",
					vndrie_info->vndrie.id, vndrie_info->vndrie.len,
					vndrie_info->ie_len - 2,
					vndrie_info->vndrie.oui[0], vndrie_info->vndrie.oui[1],
					vndrie_info->vndrie.oui[2]));

				del_add_ie_buf_len = wl_cfgp2p_vndr_ie(cfg, curr_ie_buf,
					pktflag, vndrie_info->vndrie.oui,
					vndrie_info->vndrie.id,
					vndrie_info->ie_ptr + VNDR_IE_FIXED_LEN,
					vndrie_info->ie_len - VNDR_IE_FIXED_LEN,
					"add");

				
				if (remained_buf_len >= vndrie_info->ie_len) {
					remained_buf_len -= vndrie_info->ie_len;
				} else {
					WL_ERR(("no space in mgmt_ie_buf: pktflag = %d, "
					"found vndr ies # = %d(cur %d), remained len %d, "
					"cur mgmt_ie_len %d, new ie len = %d\n",
					pktflag, new_vndr_ies.count, i, remained_buf_len,
					*mgmt_ie_len, vndrie_info->ie_len));
					break;
				}

				
				memcpy(ptr + (*mgmt_ie_len), vndrie_info->ie_ptr,
					vndrie_info->ie_len);
				*mgmt_ie_len += vndrie_info->ie_len;
				curr_ie_buf += del_add_ie_buf_len;
				total_ie_buf_len += del_add_ie_buf_len;
			}
		}

		if (total_ie_buf_len && cfg->ioctl_buf != NULL) {
			ret  = wldev_iovar_setbuf_bsscfg(ndev, "vndr_ie", g_mgmt_ie_buf,
				total_ie_buf_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
				bssidx, &cfg->ioctl_buf_sync);
			if (ret)
				WL_ERR(("vndr ie set error : %d\n", ret));
		}
	}
exit:

return ret;
}

#ifdef WL_CFG80211_ACL
static int
wl_cfg80211_set_mac_acl(struct wiphy *wiphy, struct net_device *cfgdev,
	const struct cfg80211_acl_data *acl)
{
	int i;
	int ret = 0;
	int macnum = 0;
	int macmode = MACLIST_MODE_DISABLED;
	struct maclist *list;

	
	if (acl && acl->acl_policy == NL80211_ACL_POLICY_DENY_UNLESS_LISTED) {
		macmode = MACLIST_MODE_ALLOW;
	} else if (acl && acl->acl_policy == NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED &&
	acl->n_acl_entries) {
		macmode = MACLIST_MODE_DENY;
	}

	
	if (macmode == MACLIST_MODE_DISABLED) {
		if ((ret = wl_android_set_ap_mac_list(cfgdev, macmode, NULL)) != 0)
			WL_ERR(("%s : Setting MAC list failed error=%d\n", __FUNCTION__, ret));

		return ret;
	}

	macnum = acl->n_acl_entries;
	if (macnum < 0 || macnum > MAX_NUM_MAC_FILT) {
		WL_ERR(("%s : invalid number of MAC address entries %d\n",
			__FUNCTION__, macnum));
		return -1;
	}

	
	list = (struct maclist*)kmalloc(sizeof(int) +
		sizeof(struct ether_addr) * macnum, GFP_KERNEL);
	if (!list) {
		WL_ERR(("%s : failed to allocate memory\n", __FUNCTION__));
		return -1;
	}

	
	list->count = htod32(macnum);
	for (i = 0; i < macnum; i++) {
		memcpy(&list->ea[i], &acl->mac_addrs[i], ETHER_ADDR_LEN);
	}
	
	if ((ret = wl_android_set_ap_mac_list(cfgdev, macmode, list)) != 0)
		WL_ERR(("%s : Setting MAC list failed error=%d\n", __FUNCTION__, ret));

	kfree(list);

	return ret;
}
#endif 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
int wl_chspec_chandef(chanspec_t chanspec,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	struct cfg80211_chan_def *chandef,
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, \
	\
	\
	0)))
	struct chan_info *chaninfo,
#endif 
struct wiphy *wiphy)

{
	uint16 freq = 0;
	int chan_type = 0;
	int channel = 0;

	if (!chandef) {
		return -1;
	}
	channel = CHSPEC_CHANNEL(chanspec);

	switch (CHSPEC_BW(chanspec)) {
		case WL_CHANSPEC_BW_20:
			chan_type = NL80211_CHAN_HT20;
			break;
		case WL_CHANSPEC_BW_40:
		{
			if (CHSPEC_SB_UPPER(chanspec)) {
				channel += CH_10MHZ_APART;
			} else {
				channel -= CH_10MHZ_APART;
			}
		}
			chan_type = NL80211_CHAN_HT40PLUS;
			break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
		case WL_CHANSPEC_BW_80:
		case WL_CHANSPEC_BW_8080:
		{
			uint16 sb = CHSPEC_CTL_SB(chanspec);

			if (sb == WL_CHANSPEC_CTL_SB_LL) {
				channel -= (CH_10MHZ_APART + CH_20MHZ_APART);
			} else if (sb == WL_CHANSPEC_CTL_SB_LU) {
				channel -= CH_10MHZ_APART;
			} else if (sb == WL_CHANSPEC_CTL_SB_UL) {
				channel += CH_10MHZ_APART;
			} else {
				
				channel += (CH_10MHZ_APART + CH_20MHZ_APART);
			}

			if (sb == WL_CHANSPEC_CTL_SB_LL || sb == WL_CHANSPEC_CTL_SB_LU)
				chan_type = NL80211_CHAN_HT40MINUS;
			else if (sb == WL_CHANSPEC_CTL_SB_UL || sb == WL_CHANSPEC_CTL_SB_UU)
				chan_type = NL80211_CHAN_HT40PLUS;
		}
			break;
#endif 
		default:
			chan_type = NL80211_CHAN_HT20;
			break;

	}

	if (CHSPEC_IS5G(chanspec))
		freq = ieee80211_channel_to_frequency(channel, NL80211_BAND_5GHZ);
	else
		freq = ieee80211_channel_to_frequency(channel, NL80211_BAND_2GHZ);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
	cfg80211_chandef_create(chandef, ieee80211_get_channel(wiphy, freq), chan_type);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, \
	\
	\
	0)))
	chaninfo->freq = freq;
	chaninfo->chan_type = chan_type;
#endif 
	return 0;
}

void
wl_cfg80211_ch_switch_notify(struct net_device *dev, uint16 chanspec, struct wiphy *wiphy)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
	struct cfg80211_chan_def chandef;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, \
	\
	\
	0)))
	struct chan_info chaninfo;
#endif 

	if (!wiphy) {
		WL_ERR(("wiphy is null\n"));
		return;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
	if (wl_chspec_chandef(chanspec, &chandef, wiphy)) {
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, \
	\
	\
	0)))
	if (wl_chspec_chandef(chanspec, &chaninfo, wiphy)) {
#endif 

		WL_ERR(("%s:chspec_chandef failed\n", __FUNCTION__));
		return;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
	cfg80211_ch_switch_notify(dev, &chandef);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, \
	\
	\
	0)))
	cfg80211_ch_switch_notify(dev, chan_info.freq, chan_info.chan_type);
#endif 
	return;
}
#endif 

#ifdef WL11ULB
s32
wl_cfg80211_set_ulb_mode(struct net_device *dev, int mode)
{
	int ret;
	int cur_mode;

	ret = wldev_iovar_getint(dev, "ulb_mode", &cur_mode);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] ulb_mode get failed. ret:%d \n", ret));
		return ret;
	}

	if (cur_mode == mode) {
		WL_INFORM(("[ULB] No change in ulb_mode. Do nothing.\n"));
		return 0;
	}

	
	ret = wldev_ioctl(dev, WLC_DOWN, NULL, 0, true);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] WLC_DOWN command failed:[%d]\n", ret));
		return ret;
	}

	if (mode >= MAX_SUPP_ULB_MODES) {
		WL_ERR(("[ULB] unsupported ulb_mode :[%d]\n", mode));
		return -EINVAL;
	}

	ret = wldev_iovar_setint(dev, "ulb_mode", mode);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] ulb_mode set failed. ret:%d \n", ret));
		return ret;
	}

	ret = wldev_ioctl(dev, WLC_UP, NULL, 0, true);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] WLC_DOWN command failed:[%d]\n", ret));
		return ret;
	}

	WL_DBG(("[ULB] ulb_mode set to %d successfully \n", mode));

	return ret;
}

static s32
wl_cfg80211_ulbbw_to_ulbchspec(u32 bw)
{
	if (bw == ULB_BW_DISABLED) {
		return WL_CHANSPEC_BW_20;
	} else if (bw == ULB_BW_10MHZ) {
		return WL_CHANSPEC_BW_10;
	} else if (bw == ULB_BW_5MHZ) {
		return WL_CHANSPEC_BW_5;
	} else if (bw == ULB_BW_2P5MHZ) {
		return WL_CHANSPEC_BW_2P5;
	} else {
		WL_ERR(("[ULB] unsupported value for ulb_bw \n"));
		return -EINVAL;
	}
}

static chanspec_t
wl_cfg80211_ulb_get_min_bw_chspec(struct wireless_dev *wdev, s32 bssidx)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct net_info *_netinfo;

	if (wdev) {
		
		_netinfo = wl_get_netinfo_by_wdev(cfg, wdev);
	} else if (bssidx >= 0) {
		
		_netinfo = wl_get_netinfo_by_bssidx(cfg, bssidx);
	} else {
		WL_ERR(("[ULB] wdev/bssidx not provided\n"));
		return INVCHANSPEC;
	}

	if (unlikely(!_netinfo)) {
		WL_ERR(("[ULB] net_info is null \n"));
		return INVCHANSPEC;
	}

	if (_netinfo->ulb_bw) {
		WL_DBG(("[ULB] wdev_ptr:%p ulb_bw:0x%x \n", _netinfo->wdev, _netinfo->ulb_bw));
		return wl_cfg80211_ulbbw_to_ulbchspec(_netinfo->ulb_bw);
	} else {
		return WL_CHANSPEC_BW_20;
	}
}

static s32
wl_cfg80211_get_ulb_bw(struct wireless_dev *wdev)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct net_info *_netinfo = wl_get_netinfo_by_wdev(cfg, wdev);

	if (unlikely(!_netinfo)) {
		WL_ERR(("[ULB] net_info is null \n"));
		return -1;
	}

	return _netinfo->ulb_bw;
}

s32
wl_cfg80211_set_ulb_bw(struct net_device *dev,
	u32 ulb_bw,  char *ifname)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	int ret;
	int mode;
	struct net_info *_netinfo = NULL, *iter, *next;
	u32 bssidx;
	enum nl80211_iftype iftype;

	if (!ifname)
		return -EINVAL;

	WL_DBG(("[ULB] Enter. bw_type:%d \n", ulb_bw));

	ret = wldev_iovar_getint(dev, "ulb_mode", &mode);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] ulb_mode not supported \n"));
		return ret;
	}

	if (mode != ULB_MODE_STD_ALONE_MODE) {
		WL_ERR(("[ULB] ulb bw modification allowed only in stand-alone mode\n"));
		return -EINVAL;
	}

	if (ulb_bw >= MAX_SUPP_ULB_BW) {
		WL_ERR(("[ULB] unsupported value (%d) for ulb_bw \n", ulb_bw));
		return -EINVAL;
	}

#ifdef WL_CFG80211_P2P_DEV_IF
	if (strcmp(ifname, "p2p-dev-wlan0") == 0) {
		iftype = NL80211_IFTYPE_P2P_DEVICE;
		
		if (likely(cfg->p2p_wdev)) {
			_netinfo = wl_get_netinfo_by_wdev(cfg, cfg->p2p_wdev);
		} else {
			return -ENODEV;
		}
	}
#endif 
	if (!_netinfo) {
		for_each_ndev(cfg, iter, next) {
			if (iter->ndev) {
				if (strncmp(iter->ndev->name, ifname, strlen(ifname)) == 0) {
					_netinfo = wl_get_netinfo_by_netdev(cfg, iter->ndev);
					iftype = NL80211_IFTYPE_STATION;
				}
			}
		}
	}

	if (!_netinfo)
		return -ENODEV;
	bssidx = _netinfo->bssidx;
	_netinfo->ulb_bw = ulb_bw;


	WL_DBG(("[ULB] Applying ulb_bw:%d for bssidx:%d \n", ulb_bw, bssidx));
	ret = wldev_iovar_setbuf_bsscfg(dev, "ulb_bw", (void *)&ulb_bw, 4,
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx,
		&cfg->ioctl_buf_sync);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] ulb_bw set failed. ret:%d \n", ret));
		return ret;
	}

	return ret;
}
#endif 

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
static s32
wl_csa_complete_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
const wl_event_msg_t *e, void *data)
{
	int error = 0;
	int chsp = 0;
	struct net_device *ndev = NULL;
	struct wiphy *wiphy = NULL;
	chanspec_t chanspec;

	WL_ERR(("%s\n", __FUNCTION__));
	if (e->status)
		return -1;
	if (cfgdev) {
		ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
		wiphy = bcmcfg_to_wiphy(cfg);
		error = wldev_iovar_getint(ndev, "chanspec", &chsp);
		if (error)
			return -1;
		chanspec = wl_chspec_driver_to_host(chsp);
		wl_cfg80211_ch_switch_notify(ndev, chanspec, wiphy);
	} else {
		WL_ERR(("%s:cfgdev is null\n", __FUNCTION__));
		return -1;
	}


	return 0;
}
#else
static s32
wl_csa_complete_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
const wl_event_msg_t *e, void *data)
{
	WL_ERR(("%s:Not sup for kernel < 3.5\n", __FUNCTION__));
	return 0;
}
#endif 


#ifdef WL_NAN
int
wl_cfg80211_nan_cmd_handler(struct net_device *ndev, char *cmd, int cmd_len)
{
	return wl_cfgnan_cmd_handler(ndev, g_bcm_cfg, cmd, cmd_len);
}
#endif 

void wl_cfg80211_clear_security(struct bcm_cfg80211 *cfg)
{
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	int err;

	
	err = wldev_iovar_setint(dev, "wsec", 0);
	if (unlikely(err)) {
		WL_ERR(("wsec clear failed \n"));
	}
	err = wldev_iovar_setint(dev, "auth", 0);
	if (unlikely(err)) {
		WL_ERR(("auth clear failed \n"));
	}
	err = wldev_iovar_setint(dev, "wpa_auth", WPA_AUTH_DISABLED);
	if (unlikely(err)) {
		WL_ERR(("wpa_auth clear failed \n"));
	}
}

#ifdef WL_CFG80211_P2P_DEV_IF
void wl_cfg80211_del_p2p_wdev(void)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct wireless_dev *wdev = NULL;

	WL_DBG(("Enter \n"));
	if (!cfg) {
		WL_ERR(("Invalid Ptr\n"));
		return;
	} else {
		wdev = cfg->p2p_wdev;
	}

	if (wdev && cfg->down_disc_if) {
		wl_cfgp2p_del_p2p_disc_if(wdev, cfg);
		cfg->down_disc_if = FALSE;
	}
}
#endif 


#if defined(WL_SUPPORT_AUTO_CHANNEL)
int
wl_cfg80211_set_spect(struct net_device *dev, int spect)
{
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	int down = 1;
	int up = 1;
	int err = BCME_OK;

	if (!wl_get_drv_status_all(cfg, CONNECTED)) {
		err = wldev_ioctl(dev, WLC_DOWN, &down, sizeof(down), true);
		if (err) {
			WL_ERR(("%s: WLC_DOWN failed: code: %d\n", __func__, err));
			return err;
		}

		err = wldev_ioctl(dev, WLC_SET_SPECT_MANAGMENT, &spect, sizeof(spect), true);
		if (err) {
			WL_ERR(("%s: error setting spect: code: %d\n", __func__, err));
			return err;
		}

		err = wldev_ioctl(dev, WLC_UP, &up, sizeof(up), true);
		if (err) {
			WL_ERR(("%s: WLC_UP failed: code: %d\n", __func__, err));
			return err;
		}
	}
	return err;
}

int
wl_cfg80211_get_sta_channel(struct net_device *dev)
{
	if (wl_get_drv_status(g_bcm_cfg, CONNECTED, dev)) {
		return g_bcm_cfg->channel;
	}
	return BCME_OK;
}
#endif 

#if defined(CUSTOMER_HW_ONE)
#if defined(SUPPORT_2G_HT40)
void wl_cfg80211_set_hotspot_bw(int bandwidth)
{
	hotspot_bw = bandwidth;
}
#endif 
#if defined(WL_VENDOR_EXT_SUPPORT)
int wl_cfg80211_send_priv_event(struct net_device *dev, char *event)
{
	int ret = 0;
	struct wireless_dev *wdev;
	struct bcm_cfg80211 *cfg;
	int size = 0;
	if (!dev) {
		ret = -EINVAL;
		goto exit;
	}
	wdev = ndev_to_wdev(dev);
	if (!wdev) {
		ret = -EINVAL;
		goto exit;
	}
	cfg = (struct bcm_cfg80211 *)wiphy_priv(wdev->wiphy);
	if (!cfg) {
		ret = -EINVAL;
		goto exit;
	}
	size = strlen(event);
	WL_ERR(("\"%s\" %d\n", event, size));
	ret = wl_cfgvendor_send_async_event(cfg->wdev->wiphy,
		dev, BRCM_VENDOR_EVENT_PRIV_STR, event, size);

	if (!ret)
		ret = -ENOMEM;
exit:
	return ret;
}
#endif 
#if defined(SOFTAP)
static void wl_cfg80211_hotspot_event_process(struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	u32 event_type = ntoh32(e->event_type);
	uint16 flags =  ntoh16(e->flags);

	switch (event_type) {
		case WLC_E_JOIN:
		case WLC_E_ASSOC_IND:
		case WLC_E_REASSOC_IND:
			WL_ERR(("STA connect received %d\n", event_type));
			sta_event_sent = 1;
			wl_cfg80211_send_priv_event(ndev, "STA_JOIN");
			break;
		case WLC_E_DEAUTH:
		case WLC_E_DEAUTH_IND:
		case WLC_E_DISASSOC_IND:
			WL_ERR(("STA disconnect received %d\n", event_type));
			sta_event_sent = 1;
			wl_cfg80211_send_priv_event(ndev, "STA_LEAVE");
			break;
		case WLC_E_LINK:
		case WLC_E_NDIS_LINK:
			if (block_ap_event) {
				WL_ERR(("Block ap event\n"));
				break;
			}
			if (!(flags & WLC_EVENT_MSG_LINK)) {
				WL_ERR(("AP DOWN %d\n", event_type));
				dhd_APUP = false;
				wl_cfg80211_send_priv_event(ndev, "AP_DOWN");
			} else {
				WL_ERR(("AP UP %d\n", event_type));
				dhd_APUP = true;
				wl_cfg80211_send_priv_event(ndev, "AP_UP");
			}
			break;

		default:
			break;
		}
	return;
}

static s32
wl_cfg80211_custom_event_process(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	s32 err = 0;
	u32 event_type;
	u32 status;
	u32 reason;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	event_type = ntoh32(e->event_type);
	status =  ntoh32(e->status);
	reason =  ntoh32(e->reason);

	switch (event_type) {
	case WLC_E_PRUNE:
		{
			char *macaddr = (char *)&e->addr;
			char mac_buf[32] = {0};
			WL_INFORM(("PRUNE received, %02X:%02X:%02X:%02X:%02X:%02X!\n",
				macaddr[0], macaddr[1], macaddr[2], macaddr[3],
				macaddr[4], macaddr[5]));

			if (status) {
				WL_ERR(("reach max!!\n"));
				wl_cfg80211_send_priv_event(ndev, "MAX_CLIENT");
				break;
			}

			if (wl_android_black_list_match(macaddr)) {
				WL_ERR(("STA block\n"));
				sprintf(mac_buf, "STA_BLOCK %02X:%02X:%02X:%02X:%02X:%02X",
					macaddr[0], macaddr[1], macaddr[2],
					macaddr[3], macaddr[4], macaddr[5]);
					wl_cfg80211_send_priv_event(ndev, mac_buf);
			}
			break;
		}
	case WLC_E_LOAD_IND:
		{
#ifdef DHDTCPACK_SUPPRESS
			dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif 
			WL_ERR(("WLC_E_LOAD_IND status %d\n", status));
			switch (status) {
				case WIFI_LOADING_LOW:
				{
					WL_ERR(("LOW INDICATE!!\n"));
					wlan_unlock_perf();
					wlan_unlock_multi_core(ndev);
#ifdef DHDTCPACK_SUPPRESS
					WL_ERR(("DISABLE TCPACK SUPPRESS!!\n"));
					dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_OFF);
#endif 
					break;
				}
				case WIFI_LOADING_HIGH:
				{
					WL_ERR(("HIGH INDICATE!!\n"));
					wlan_lock_perf();
					wlan_lock_multi_core(ndev);
					break;
				}
#ifdef DHDTCPACK_SUPPRESS
				case WIFI_LOADING_TCPACK_SUPPRESS_ON:
				{
					WL_ERR(("ENABLE TCPACK SUPPRESS !!\n"));
					dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_HOLD);
					break;
				}
#endif 
				default:
					break;
			}
			break;
		}
	case WLC_E_RESET_COMPLETE:
		{
			WL_ERR(("WLC_E_RESET_COMPLETE status %d reason %d\n", status, reason));
			break;
		}

	default:
		break;
	}
	return err;
}
#endif 

#ifdef TX_STUCK_DETECTION

#define PRINT_BUF_LEN 2048
char printcntersbuf[PRINT_BUF_LEN];

#define MAX_WLIW_IOCTL_LEN 1024
#define	PRNL()	\
	if ((PRINT_BUF_LEN - (pbuf - printcntersbuf)) > (snprintf(NULL, 0, "\n")))	\
		pbuf += sprintf(pbuf, "\n");	\
	else	\
		printf("%s Buffer is full, skip newline symbol\n", __FUNCTION__);

#define PRSTR(context)	\
	if ((PRINT_BUF_LEN - (pbuf - printcntersbuf)) > (snprintf(NULL, 0, "%s ", context)))	\
		pbuf += sprintf(pbuf, "%s ", context);	\
	else	\
		printf("%s Buffer is full, skip write string\n", __FUNCTION__);

#define	PRVAL(name)	\
	if ((PRINT_BUF_LEN - (pbuf - printcntersbuf)) > (snprintf(NULL, 0, "%s %u ", #name, dtoh32(cnt->name))))	\
		pbuf += sprintf(pbuf, "%s %u ", #name, dtoh32(cnt->name));	\
	else	\
		printf("%s Buffer is full, skip load %s\n", __FUNCTION__, #name);

#define PRVAL_RENAME(varname, prname)	\
	if ((PRINT_BUF_LEN - (pbuf - printcntersbuf)) > (snprintf(NULL, 0, "%s %u ", #prname, dtoh32(cnt->varname))))	\
		pbuf += sprintf(pbuf, "%s %u ", #prname, dtoh32(cnt->varname)); \
	else	\
		printf("%s Buffer is full, skip load %s\n", __FUNCTION__, #prname);

#define PRVALSF(name) if ((dtoh32(cnt->name) != INVALID_CNT_VAL) &&     \
         (len > ((uint8 *)&cnt->name - (uint8 *)cnt))) { PRVAL(name) }

#define PRVALSF_RENAME(varname, prname)	\
	if (dtoh32(cnt->varname) != INVALID_CNT_VAL) { PRVAL_RENAME(varname, prname) }

#define PRCNT_MACSTAT_TX_VER_GE11						\
do {										\
							\
						\
	PRVAL(txallfrm); PRVAL(txbcnfrm); PRVAL(txrtsfrm); PRVAL(txctsfrm);	\
	PRVAL(txackfrm); PRVAL(txback); PRVAL(txdnlfrm); PRNL();		\
	PRVAL(txampdu); PRVAL(txmpdu); PRVAL(txucast);				\
	PRVAL(rxrsptmout); PRVAL(txinrtstxop); PRVAL(txrtsfail); PRNL();	\
	if ((PRINT_BUF_LEN - (pbuf - printcntersbuf)) > (snprintf(NULL, 0, "txper_ucastdt %d%% txper_rts %d%%\n",	\
			cnt->txucast > 0 ?						\
			(100 - ((cnt->rxackucast + cnt->rxback) * 100		\
			 / cnt->txucast)) : 0,					\
			cnt->txrtsfrm > 0 ?						\
			(100 - (cnt->rxctsucast * 100				\
			/ cnt->txrtsfrm)) : 0)))	\
		pbuf += sprintf(pbuf, "txper_ucastdt %d%% txper_rts %d%%\n",	\
			cnt->txucast > 0 ?						\
			(100 - ((cnt->rxackucast + cnt->rxback) * 100		\
			 / cnt->txucast)) : 0,					\
			cnt->txrtsfrm > 0 ?						\
			(100 - (cnt->rxctsucast * 100				\
			/ cnt->txrtsfrm)) : 0);					\
	else	\
		printf("%s Buffer is full, skip write string\n", __FUNCTION__);	\
	PRSTR("txfunfl: ");	\
	for (i = 0; i < NFIFO; i++) {						\
		if ((PRINT_BUF_LEN - (pbuf - printcntersbuf)) > (snprintf(NULL, 0, "%d ", cnt->txfunfl[i])))	\
			pbuf += sprintf(pbuf, "%d ", cnt->txfunfl[i]);			\
		else	\
			printf("%s Buffer is full, skip write string\n", __FUNCTION__);	\
	}	\
	PRVAL(txtplunfl); PRVAL(txphyerror); PRNL(); PRNL();			\
} while (0)

#define PRCNT_MACSTAT_RX_VER_GE11						\
do {										\
								\
	PRVAL(rxctlucast); PRVAL(rxrtsucast); PRVAL(rxctsucast);		\
	PRVAL(rxackucast); PRVAL(rxback); PRNL();				\
	PRVAL(rxbeaconmbss); PRVAL(rxdtucastmbss);				\
	PRVAL(rxmgucastmbss); PRNL();						\
	PRVAL(rxbeaconobss); PRVAL(rxdtucastobss); PRVAL(rxdtocast);		\
	PRVAL(rxmgocast); PRNL();						\
	PRVAL(rxctlocast); PRVAL(rxrtsocast); PRVAL(rxctsocast); PRNL();	\
	PRVAL(rxctlmcast); PRVAL(rxdtmcast); PRVAL(rxmgmcast); PRNL(); PRNL();	\
										\
} while (0)

typedef struct {
	char *pbuf_ptr;
} wl_cnt_cbfn_info_t;

extern int wl_cntbuf_to_xtlv_format(void *ctx, void *cntbuf,
	int buflen, uint32 corerev);

static int wl_counters_cbfn(void *ctx, uint8 *data, uint16 type, uint16 len)
{
	int err = BCME_OK;
	wl_cnt_cbfn_info_t *cbfn_info = ctx;
	char *pbuf = cbfn_info->pbuf_ptr = printcntersbuf;
	uint i;

	printf("%s len %d\n", __FUNCTION__, len);
	switch (type) {
		case WL_CNT_XTLV_WLC: {
			
			wl_cnt_wlc_t *cnt = (wl_cnt_wlc_t *)data;
			if (len > sizeof(wl_cnt_wlc_t)) {
				printf("type %d: cntbuf length too long! %d > %d\n"
					"May need to use up-to-date wl utility.\n",
					type, len, (int)sizeof(wl_cnt_wlc_t));
			}

			printf("%s WL_CNT_XTLV_WLC:\n", __FUNCTION__);

			PRVALSF(reinit);
			
			if (cnt->reinitreason[0] != INVALID_CNT_VAL) {
				PRSTR("reinitreason_counts: ");
				for (i = 0; i < NREINITREASONCOUNT; i++) {
					if ((PRINT_BUF_LEN - (pbuf - printcntersbuf)) > (snprintf(NULL, 0, "%d(%d) ", i, cnt->reinitreason[i])))
						pbuf += sprintf(pbuf, "%d(%d) ", i, cnt->reinitreason[i]);
					else
						printf("%s Buffer is full, skip write string\n", __FUNCTION__);
				}
				PRNL();
			}

			PRVALSF(reset); PRVALSF(pciereset); PRVALSF(cfgrestore);
			PRVALSF(dma_hang); PRNL();

			PRVALSF(txframe); PRVALSF(txbyte); PRVALSF(txretrans);
			PRVALSF(txfail); PRVALSF(txchanrej); PRVALSF(tbtt); PRNL();
			PRVALSF(rxframe); PRVALSF(rxbyte); PRVALSF(rxerror); PRNL();
			PRVALSF(txprshort); PRVALSF(txdmawar); PRVALSF(txnobuf); PRVALSF(txnoassoc);
			PRVALSF(txchit); PRVALSF(txcmiss); PRNL();
			PRVALSF(txserr); PRVALSF(txphyerr); PRVALSF(txphycrs); PRVALSF(txerror);
			PRNL();
			PRVALSF_RENAME(txfrag, d11_txfrag); PRVALSF_RENAME(txmulti, d11_txmulti);
			PRVALSF_RENAME(txretry, d11_txretry);
			PRVALSF_RENAME(txretrie, d11_txretrie); PRNL();
			PRVALSF_RENAME(txrts, d11_txrts); PRVALSF_RENAME(txnocts, d11_txnocts);
			PRVALSF_RENAME(txnoack, d11_txnoack);
			PRVALSF_RENAME(txfrmsnt, d11_txfrmsnt); PRNL();

			PRVALSF(rxcrc); PRVALSF(rxnobuf); PRVALSF(rxnondata); PRVALSF(rxbadds);
			PRVALSF(rxbadcm); PRVALSF(rxdup); PRVALSF(rxfragerr); PRNL();

			PRVALSF(rxrunt); PRVALSF(rxgiant); PRVALSF(rxnoscb); PRVALSF(rxbadproto);
			PRVALSF(rxbadsrcmac); PRVALSF(rxrtry); PRNL();

			PRVALSF_RENAME(rxfrag, d11_rxfrag); PRVALSF_RENAME(rxmulti, d11_rxmulti);
			PRVALSF_RENAME(rxundec, d11_rxundec); PRNL();

			printf("%s %s \n", __FUNCTION__, cbfn_info->pbuf_ptr);
			memset(printcntersbuf, 0, PRINT_BUF_LEN);
			pbuf = printcntersbuf;

			PRVALSF(rxctl); PRVALSF(rxbadda); PRVALSF(rxfilter);
			if (cnt->rxuflo[0] != INVALID_CNT_VAL) {
				PRSTR("rxuflo: ");
				for (i = 0; i < NFIFO; i++) {
					if ((PRINT_BUF_LEN - (pbuf - printcntersbuf)) > (snprintf(NULL, 0, "%u ", dtoh32(cnt->rxuflo[i]))))
						pbuf += sprintf(pbuf, "%u ", dtoh32(cnt->rxuflo[i]));
					else
						printf("%s Buffer is full, skip write string\n", __FUNCTION__);
				}
				PRNL();
			}

			
			PRNL();
			PRVALSF(tkipmicfaill); PRVALSF(tkipicverr); PRVALSF(tkipcntrmsr); PRNL();
			PRVALSF(tkipreplay); PRVALSF(ccmpfmterr); PRVALSF(ccmpreplay); PRNL();
			PRVALSF(ccmpundec); PRVALSF(fourwayfail); PRVALSF(wepundec); PRNL();
			PRVALSF(wepicverr); PRVALSF(decsuccess); PRVALSF(rxundec); PRNL();
			PRNL();

			
			PRVALSF(rx1mbps); PRVALSF(rx2mbps); PRVALSF(rx5mbps5);
			PRVALSF(rx11mbps); PRNL();
			PRVALSF(rx6mbps); PRVALSF(rx9mbps); PRVALSF(rx12mbps);
			PRVALSF(rx18mbps); PRNL();
			PRVALSF(rx24mbps); PRVALSF(rx36mbps); PRVALSF(rx48mbps);
			PRVALSF(rx54mbps); PRNL();

			PRVALSF(txmpdu_sgi); PRVALSF(rxmpdu_sgi); PRVALSF(txmpdu_stbc);
			PRVALSF(rxmpdu_stbc); PRVALSF(rxmpdu_mu); PRNL();

			PRVALSF(cso_normal); PRVALSF(cso_passthrough);
			PRNL();
			PRVALSF(chained); PRVALSF(chainedsz1); PRVALSF(unchained);
			PRVALSF(maxchainsz); PRVALSF(currchainsz); PRNL();
			PRNL();
			printf("%s %s \n", __FUNCTION__, cbfn_info->pbuf_ptr);
			break;
		}
		case WL_CNT_XTLV_GE40_UCODE_V1: {
			wl_cnt_ge40mcst_v1_t *cnt = (wl_cnt_ge40mcst_v1_t *)data;
			if (len != sizeof(wl_cnt_ge40mcst_v1_t)) {
				printf("type %d: cnt struct length mismatch! %d != %d\n",
					type, len, (int)sizeof(wl_cnt_ge40mcst_v1_t));
			}
			printf("%s case WL_CNT_XTLV_GE40_UCODE_V1: \n", __FUNCTION__);
			PRCNT_MACSTAT_TX_VER_GE11;
			
			PRVAL(rxstrt); PRVAL(rxbadplcp); PRVAL(rxcrsglitch);
			PRVAL(rxtoolate); PRVAL(rxnodelim); PRNL();
			PRVAL(rxdrop20s); PRVAL(bphy_badplcp); PRVAL(bphy_rxcrsglitch); PRNL();
			PRVAL(rxbadfcs); PRVAL(rxfrmtoolong); PRVAL(rxfrmtooshrt);
			PRVAL(rxanyerr); PRNL();
			PRVAL(rxf0ovfl); PRVAL(rxf1ovfl); PRVAL(rxhlovfl); PRVAL(pmqovfl); PRNL();
			printf("%s %s \n", __FUNCTION__, cbfn_info->pbuf_ptr);
			memset(printcntersbuf, 0, PRINT_BUF_LEN);
			pbuf = printcntersbuf;
			PRCNT_MACSTAT_RX_VER_GE11;
			printf("%s %s \n", __FUNCTION__, cbfn_info->pbuf_ptr);
			memset(printcntersbuf, 0, PRINT_BUF_LEN);
			pbuf = printcntersbuf;
			PRVAL(rxcgprqfrm); PRVAL(rxcgprsqovfl); PRVAL(txcgprsfail);
			PRVAL(txcgprssuc); PRVAL(prs_timeout); PRNL();
			PRVAL(pktengrxducast); PRVAL(pktengrxdmcast);
			PRVAL(bcntxcancl);
			PRVAL(missbcn_dbg); PRNL();
			PRNL();
			printf("%s %s \n", __func__,cbfn_info->pbuf_ptr);
			break;
		}
		default:
			printf("Unknown counters type %d!! You may try updating wl utility.\n",
				type);
			break;
	}
	cbfn_info->pbuf_ptr = pbuf;
	return err;
}

int wl_get_wireless_stats(void)
{
	int res = 0;
	char cntbuf[WL_CNTBUF_MAX_SIZE] = {0};
	wl_cnt_info_t *cntinfo;
	uint16 ver;
	uint32 corerev = 0;
	wl_cnt_cbfn_info_t cbfn_info;
	struct bcm_cfg80211 *cfg = g_bcm_cfg;
	struct net_device *primary_dev = NULL;

	if (!cfg) {
		WL_ERR(("cfg is Null\n"));
		return -1;
	}

	if (!(primary_dev = bcmcfg_to_prmry_ndev(cfg))) {
		WL_ERR(("primary device is Null\n"));
		return -1;
	}
	WL_TRACE(("***** %s counters=%d\n *****", __FUNCTION__, WL_CNTBUF_MAX_SIZE));

	if (WL_CNTBUF_MAX_SIZE > MAX_WLIW_IOCTL_LEN)
	{
		WL_ERR(("%s buffer too short %d < %d\n",
			__FUNCTION__, WL_CNTBUF_MAX_SIZE, MAX_WLIW_IOCTL_LEN));
		res = BCME_BUFTOOSHORT;
		goto done;
	}

	memset(cntbuf, 0, WL_CNTBUF_MAX_SIZE);
	memset(printcntersbuf, 0, PRINT_BUF_LEN);

	res = wldev_iovar_getbuf(primary_dev, "counters", NULL, 0,
		cntbuf, WL_CNTBUF_MAX_SIZE, NULL);


	if (res)
	{
		WL_ERR(("%s counters failed error=%d ****** \n", __FUNCTION__, res));
		goto done;
	}

	cntinfo = (wl_cnt_info_t *)cntbuf;
	cntinfo->version = dtoh16(cntinfo->version);
	cntinfo->datalen = dtoh16(cntinfo->datalen);
	ver = cntinfo->version;
	printf("counters_version %2d\n", ver);
	if (ver > WL_CNT_T_VERSION) {
		WL_TRACE(("\tIncorrect version of counters struct: expected %d; got %d\n",
			WL_CNT_T_VERSION, ver));
		res = BCME_VERSION;
		goto done;
	}

	if (ver == WL_CNT_VERSION_11) {
		wlc_rev_info_t revinfo;
		memset(&revinfo, 0, sizeof(revinfo));
		res = wldev_ioctl(primary_dev, WLC_GET_REVINFO, &revinfo, sizeof(revinfo), 0);
		if (res) {
			WL_ERR(("%s: WLC_GET_REVINFO failed %d\n", __FUNCTION__, res));
			goto done;
		}
		corerev = dtoh32(revinfo.corerev);
	}

	printf("%s corerev %d \n", __FUNCTION__, corerev);
	res = wl_cntbuf_to_xtlv_format(NULL, cntinfo, WL_CNTBUF_MAX_SIZE, corerev);
	if (res) {
		WL_ERR(("%s: wl_cntbuf_to_xtlv_format failed %d\n", __FUNCTION__, res));
		goto done;
	}

	

	if ((res = bcm_unpack_xtlv_buf(&cbfn_info, cntinfo->data, cntinfo->datalen,
		BCM_XTLV_OPTION_ALIGN32, wl_counters_cbfn))) {
		goto done;
	}
done:
	return res;
}
#endif 

#endif 
