#pragma once

#include <stdlib.h>
#include <debug.h>
#include <stdio.h>

#include <netman.h>
#include <ps2ip.h>

#include <kernel.h>

static int ethApplyNetIFConfig(int mode)
{
	int result;
	// By default, auto-negotiation is used.
	static int CurrentMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;

	if (CurrentMode != mode)
	{ // Change the setting, only if different.
		if ((result = NetManSetLinkMode(mode)) == 0)
			CurrentMode = mode;
	}
	else
		result = 0;

	return result;
}

static void EthStatusCheckCb(s32 alarm_id, u16 time, void* common)
{
	iWakeupThread(*(int*)common);
}

static int WaitValidNetState(int (*checkingFunction)(void))
{
	int ThreadID, retry_cycles;

	// Wait for a valid network status;
	ThreadID = GetThreadId();
	for (retry_cycles = 0; checkingFunction() == 0; retry_cycles++)
	{ // Sleep for 1000ms.
		SetAlarm(1000 * 16, &EthStatusCheckCb, &ThreadID);
		SleepThread();

		if (retry_cycles >= 10) // 10s = 10*1000ms
			return -1;
	}

	return 0;
}

static int ethGetNetIFLinkStatus(void)
{
	return (NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP);
}

static int ethWaitValidNetIFLinkState(void)
{
	return WaitValidNetState(&ethGetNetIFLinkStatus);
}

static int ethGetDHCPStatus(void)
{
	t_ip_info ip_info;
	int result;

	if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0)
	{ // Check for a successful state if DHCP is enabled.
		if (ip_info.dhcp_enabled)
			result = (ip_info.dhcp_status == DHCP_STATE_BOUND || (ip_info.dhcp_status == DHCP_STATE_OFF));
		else
			result = -1;
	}

	return result;
}

static int ethWaitValidDHCPState(void)
{
	return WaitValidNetState(&ethGetDHCPStatus);
}

static int ethApplyIPConfig(int use_dhcp, const struct ip4_addr* ip, const struct ip4_addr* netmask, const struct ip4_addr* gateway)
{
	t_ip_info ip_info;
	int result;

	// SMAP is registered as the "sm0" device to the TCP/IP stack.
	if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0)
	{
		// Check if it's the same. Otherwise, apply the new configuration.
		if ((use_dhcp != ip_info.dhcp_enabled) || (!use_dhcp &&
													  (!ip_addr_cmp(ip, (struct ip4_addr*)&ip_info.ipaddr) ||
														  !ip_addr_cmp(netmask, (struct ip4_addr*)&ip_info.netmask) ||
														  !ip_addr_cmp(gateway, (struct ip4_addr*)&ip_info.gw))))
		{
			if (use_dhcp)
			{
				ip_info.dhcp_enabled = 1;
			}
			else
			{ // Copy over new settings if DHCP is not used.
				ip_addr_set((struct ip4_addr*)&ip_info.ipaddr, ip);
				ip_addr_set((struct ip4_addr*)&ip_info.netmask, netmask);
				ip_addr_set((struct ip4_addr*)&ip_info.gw, gateway);

				ip_info.dhcp_enabled = 0;
			}

			// Update settings.
			result = ps2ip_setconfig(&ip_info);
		}
		else
			result = 0;
	}

	return result;
}

static void ethdprintIPConfig(void)
{
	t_ip_info ip_info;
	u8 ip_address[4], netmask[4], gateway[4];

	// SMAP is registered as the "sm0" device to the TCP/IP stack.
	if (ps2ip_getconfig("sm0", &ip_info) >= 0)
	{
		// Obtain the current DNS server settings.

		ip_address[0] = ip4_addr1((struct ip4_addr*)&ip_info.ipaddr);
		ip_address[1] = ip4_addr2((struct ip4_addr*)&ip_info.ipaddr);
		ip_address[2] = ip4_addr3((struct ip4_addr*)&ip_info.ipaddr);
		ip_address[3] = ip4_addr4((struct ip4_addr*)&ip_info.ipaddr);

		netmask[0] = ip4_addr1((struct ip4_addr*)&ip_info.netmask);
		netmask[1] = ip4_addr2((struct ip4_addr*)&ip_info.netmask);
		netmask[2] = ip4_addr3((struct ip4_addr*)&ip_info.netmask);
		netmask[3] = ip4_addr4((struct ip4_addr*)&ip_info.netmask);

		gateway[0] = ip4_addr1((struct ip4_addr*)&ip_info.gw);
		gateway[1] = ip4_addr2((struct ip4_addr*)&ip_info.gw);
		gateway[2] = ip4_addr3((struct ip4_addr*)&ip_info.gw);
		gateway[3] = ip4_addr4((struct ip4_addr*)&ip_info.gw);


		scr_printf("IP:\t%d.%d.%d.%d\n",
			ip_address[0], ip_address[1], ip_address[2], ip_address[3],
			netmask[0], netmask[1], netmask[2], netmask[3],
			gateway[0], gateway[1], gateway[2], gateway[3]);
	}
	else
	{
		scr_printf("Unable to read IP address.\n");
	}
}

s32 network_init(void)
{
	struct ip4_addr *IP, *NM, *GW;

		// Using DHCP
		IP = malloc(sizeof(struct ip4_addr));
		NM = malloc(sizeof(struct ip4_addr));
		GW = malloc(sizeof(struct ip4_addr));

		// The DHCP server will provide us this information.
		ip4_addr_set_zero(IP);
		ip4_addr_set_zero(NM);
		ip4_addr_set_zero(GW);

	// Initialize NETMAN
	NetManInit();

	// The network interface link mode/duplex can be set.
	int EthernetLinkMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;

	// Attempt to apply the new link setting.
	if (ethApplyNetIFConfig(EthernetLinkMode) != 0)
	{
		scr_printf("Error: failed to set link mode.\n");
		goto failure;
	}

	// Initialize the TCP/IP protocol stack.
	ps2ipInit(IP, NM, GW);

	ethApplyIPConfig(1, IP, NM, GW);
	// Wait for the link to become ready.
	scr_printf("Waiting for connection...\n");
	if (ethWaitValidNetIFLinkState() != 0)
	{
		scr_printf("Error: failed to get valid link status.\n");
		goto failure;
	}

		scr_printf("Waiting for DHCP lease...");
		if (ethWaitValidDHCPState() != 0)
		{
			scr_printf("DHCP failed\n.");
			goto failure;
		}
		scr_printf("done!\n");

	scr_printf("Initialized:\n");

	ethdprintIPConfig();

	return 0;

failure:
	ps2ipDeinit();
	NetManDeinit();
	// TODO: Clean up allocated memory?
	return -1;
}

void networking_ip(u8* ip)
{
	t_ip_info ip_info;
	ps2ip_getconfig("sm0", &ip_info);
	ip[0] = ip4_addr1((struct ip4_addr*)&ip_info.ipaddr);
	ip[1] = ip4_addr2((struct ip4_addr*)&ip_info.ipaddr);
	ip[2] = ip4_addr3((struct ip4_addr*)&ip_info.ipaddr);
	ip[3] = ip4_addr4((struct ip4_addr*)&ip_info.ipaddr);
	return;
}
