// Copyright (c) 2023 Ty (Fobes) Lamontagne
// Example of a simple HTTP server using Mongoose and the ps2_drivers library.
// Licensed under the GPL v3 license
// Requires the ps2_drivers library. Mongoose is included in-tree.
// Requires a PS2 with a hard drive and network adapter.
// This example is configured to use DHCP.

// Undefine this if you don't have a hard drive :(
#define EXAMPLE_USE_HDD

#include "mongoose.h"

#include "network.h"

#include <debug.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>

#include <ps2_hdd_driver.h>
#include <ps2_eeip_driver.h>
#include <ps2_dev9_driver.h>

u64 req_count = 0;
static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data)
{
	if (ev == MG_EV_HTTP_MSG) // HTTP Request
	{
		struct mg_http_message *hm = (struct mg_http_message *)ev_data;
		// Print the request to the screen with a request count
		scr_printf("%.*s %.*s (#%d)\n", (int)hm->method.len, hm->method.ptr, (int)hm->uri.len, hm->uri.ptr,
				   ++req_count);

#ifdef EXAMPLE_USE_HDD
		// Set our root directory to the mounted hdd0:WWW partition
		struct mg_http_serve_opts opts = {.root_dir = "pfs:."};
		// Allow Mongoose to serve the request
		mg_http_serve_dir(c, hm, &opts);
#else
		const char *response = "<h1>Hello! I'm mongoose running on the PS2!<h1/><br/>"
							   "Unfortunately, I can't serve you any files because you I'm not configured to serve a hard drive partition<br/>\n"
							   "I can tell you that your request was for %.*s with the method %.*s\n";

		mg_http_reply(c, 200, "", response, (int)hm->uri.len, hm->uri.ptr, (int)hm->method.len, hm->method.ptr);

#endif
	}
}

extern unsigned char ps2ip_irx[];
extern unsigned int size_ps2ip_irx;

void loadIOPModules()
{
	// Reset the IOP
	SifInitRpc(0);
	while (!SifIopReset("", 0))
		;
	// Sync with the IOP
	while (!SifIopSync())
		;
	SifInitRpc(0);

	// Enable the patch that lets us (ps2_drivers) load modules from memory
	sbv_patch_enable_lmb();

#ifdef EXAMPLE_USE_HDD
	// Init the HDD driver
	scr_printf("HDD DRIVER INIT %d\n", init_hdd_driver(true, false));
#else
	// init_hdd_driver inits dev9 driver, so we need to init it manually
	scr_printf("DEV9 DRIVER INIT %d\n", init_dev9_driver());
#endif

	// Init the network drivers
	scr_printf("EEIP DRIVER INIT %d\n", init_eeip_driver(true));

	// Init the network interface. Currently uses DHCP
	network_init();
}

int main(int argc, char *argv[])
{
	// Init debug screen printing
	init_scr();
	scr_setCursor(0);

	// Load the hard drive and network dependencies
	loadIOPModules();

	// Mount the WWW partition
#ifdef EXAMPLE_USE_HDD
	scr_printf("HDD_MOUNT_STATUS: %d\n", mount_hdd_partition("pfs:", "hdd0:WWW"));
#endif

	scr_printf("Starting mongoose version %s\n", MG_VERSION);

	struct mg_mgr mgr;

	// Init Mongoose
	mg_mgr_init(&mgr);

	// Listen on port 80. Set callback function
	mg_http_listen(&mgr, "http://0.0.0.0:80", fn, &mgr);

	// Loop forever, accepting new connections
	for (;;)
		mg_mgr_poll(&mgr, 1000);

	// Clean up
	mg_mgr_free(&mgr);
	return 0;
}
