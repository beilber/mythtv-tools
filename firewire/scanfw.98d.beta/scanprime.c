/*
 *  scanprime -- primer for mythbuntu firewire scanner
 *  version .24 beta developed by majoridiot
 *
 *  incorporates code (c) by: Jim Lohmeyer, Jim Westfall and Dan Dennedy
 *
 *  Distributed as part of the mythbuntu distribution of
 *  MythTV under GPL v2 and later.
 *  
 *  This Package is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *  
 *  This package is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public
 *  License along with this package; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/select.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <assert.h>
#include <errno.h>
#include <libraw1394/raw1394.h>
#include <libavc1394/avc1394.h>
#include <libiec61883/iec61883.h>
#include <libraw1394/csr.h>
#include "firewire_utils.h"

#define SYNC_BYTE           0x47
#define MIN_PACKETS         25
#define MAX_NODATA          10
#define PLUGREPORT_GUID_HI 0x0C
#define PLUGREPORT_GUID_LO 0x10

octlet_t myguid;
octlet_t testguid;
int sync_failed = 0;
int nodata = 0;

raw1394handle_t phandle;

static octlet_t get_guid(raw1394handle_t phandle, nodeid_t node)
{
	quadlet_t       quadlet;
	octlet_t        offset;
	octlet_t        guid = 0;

	offset = CSR_REGISTER_BASE + CSR_CONFIG_ROM + PLUGREPORT_GUID_HI;

	int error = raw1394_read(phandle, node, offset, sizeof(quadlet_t), &quadlet);
	if (error == -1)
	{
        int offline = (errno == 11);
        if (!offline)
        {
            printf("get_guid HI error: %d", errno);
            exit(1);
        }
        return -1;
    }
        
	quadlet = htonl(quadlet);
	guid = quadlet;
	guid <<= 32;
	offset = CSR_REGISTER_BASE + CSR_CONFIG_ROM + PLUGREPORT_GUID_LO;

    error = raw1394_read(phandle, node, offset, sizeof(quadlet_t), &quadlet);
    if (error == -1)
  	{
         int offline = (errno == 11);
        if (!offline)
        {
            printf("get_guid HI error: %d", errno);
            exit(1);
        }
        return -1;
    }
	quadlet = htonl(quadlet);
	guid += quadlet;

	return guid;
}

octlet_t stb_offline(octlet_t tryguid, nodeid_t node)
{
    while (tryguid == -1)
    {
        ms_sleep(500);
        tryguid = get_guid(phandle, 0xffc0 | node);
    }
    return tryguid;
}

int find_stb(octlet_t myguid)
{
    int devices;
    int searchnode = 0;

    devices = raw1394_get_nodecount(phandle);

    for (; devices > 0; devices--)
    {
        testguid = get_guid(phandle, 0xffc0 | searchnode);
        if (testguid == -1)
            testguid = stb_offline(testguid, searchnode);

        if (testguid == myguid)
        {
            printf("STB jumped to node %d on bus reset\n", searchnode);
            return searchnode;
        }
        searchnode++;
    }

    printf("ERROR- can't find the STB after bus reset... sorry, aborting.\n");
    exit(1);
}

static int read_packet (unsigned char *tspacket, int len, 
                        unsigned int dropped, void *callback_data)
{
    int *count = (int *)callback_data;

    if (dropped)
        return 0;
    
    if (tspacket[0] != SYNC_BYTE)
    {
        sync_failed = 1;
        return 0;
    }
    nodata = 0;
    *count = *count + 1;
    return 1;
}

int test_connection(raw1394handle_t phandle, int channel)
{
    int count = 0;
    int retry = 0;
    int fd = raw1394_get_fd(phandle);
    iec61883_mpeg2_t mpeg;
    struct timeval tv;
    fd_set rfds;
    sync_failed = 0;

    mpeg = iec61883_mpeg2_recv_init(phandle, read_packet, (void*) &count);
    iec61883_mpeg2_recv_start(mpeg, channel);

    while(count < MIN_PACKETS && retry < 2 && !sync_failed 
          && nodata < MAX_NODATA)
    {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0)
        {
             nodata++;
             raw1394_loop_iterate(phandle);
        }
        else
        {
            retry++;
        }
    }
    iec61883_mpeg2_recv_stop(mpeg);
    iec61883_mpeg2_close(mpeg);

    if (sync_failed)
        return 0;
    
    return count;
}

int test_p2p(raw1394handle_t phandle, nodeid_t node) 
{
    int channel, count, success = 0;
    channel = node;

    fflush(stdout);

    if (iec61883_cmp_create_p2p_output(phandle, node | 0xffc0, 0, channel, 1 ) != 0)
    {
        printf("PRIME ERROR: iec61883_cmp_create_p2p_output failed\n");
        return 0;
    }
    
    count = test_connection(phandle, channel);  
    
    if (count >= MIN_PACKETS)   
        success = 1;

   iec61883_cmp_disconnect(phandle, node | 0xffc0, 0,
                            raw1394_get_local_id (phandle),
                            -1, channel, 0);
    return success;
}

int test_broadcast(raw1394handle_t phandle, nodeid_t node) 
{
    int count, success = 0;
    int channel = 63;

    if (iec61883_cmp_create_bcast_output(phandle, node | 0xffc0, 0, channel, 1 ) != 0)
    {
        printf("PRIME ERROR: iec61883_cmp_create_bcast_output failed\n");
        return 0;
    }

    count = test_connection(phandle, channel);  
    if (count >= MIN_PACKETS)   
        success = 1;		

    iec61883_cmp_disconnect(phandle, node | 0xffc0, 0, 
                            raw1394_get_local_id(phandle), 
                           -1, channel, 0);
    return success;
}

int reset_bus(nodeid_t node)
{
    myguid = get_guid(phandle, 0xffc0 | node);
    if (myguid == -1)
        myguid = stb_offline(myguid, node);
   
    if (!raw1394_reset_bus_new(phandle, RAW1394_LONG_RESET) == 0)
    {
        printf("Bus reset failed: %d: %s\n", errno, strerror( errno ));
        exit(1);
    }

    testguid = get_guid(phandle, 0xffc0 | node);
    if (testguid == -1)
        testguid = stb_offline(testguid, node);

    if (myguid != testguid)
      node = find_stb(myguid);

    return node;   
}

int moto_prime(nodeid_t node, int *finish_node)
{
    int retries;  
    int success = 0;
    int tries = 0;
    int loop = 0;

    for(; loop < 3; loop++)
        success += test_broadcast(phandle, node);

    if (success == 3) 
        return 1;  

    *finish_node = reset_bus(node);
    node = (node = *finish_node) ? node: *finish_node;
    tries = 0;
    success = 0;

    while (tries < 3)  
    {
	retries = 0;  
        while (retries < 10)  
        { 
            if (test_p2p(phandle, node))  
            {
                success = 0; 
                while (test_broadcast(phandle, node))  
                {
                    success++; 
                    if (success == 5)  
                       return 1;  
                }
            }
            retries++;  
        }
        tries++;  
    }
    return 0;
}

int sa_prime(nodeid_t node, int *finish_node)
{
    int success = 0;
    int tries = 0;
    int loop = 0;
    
    if (!tap_plug0(handle, node, 1, 1))
      return 0;        

    for(; loop < 5; loop++)
        success += test_p2p(phandle, node);

    if (!tap_plug0(handle, node, 1, 0))
        return 0;
            
    if (success == 5) 
        return 1;  

    *finish_node = reset_bus(node);
    node = (node = *finish_node) ? node: *finish_node;

    while (tries < 5)
    {
        if (!tap_plug0(handle, node, 1, 1))
            return 0;

        for(loop = 0; loop < 5; loop++)
            success += test_p2p(phandle, node);

        if (!tap_plug0(handle, node, 1, 0))
            return 0;

        if (success == 5);
            return 1;

        success = 0;
        tries++;  
    }
    return 0;
}

int prime_stb(int port, int *node_in, int p2p_stb) 
{
    int result;
    int prime_node;
    int finish_node;
    
    prime_node = *node_in;
    finish_node = prime_node;

    phandle = raw1394_new_handle_on_port(port);
    if (!phandle)
    {		
         printf("Failed to acquire handle on port %d.\n", port);
         return 1;
    }

    if (p2p_stb)
        result = sa_prime(prime_node, &finish_node);
    else
        result = moto_prime(prime_node, &finish_node);

    if (prime_node != finish_node)
        *node_in = finish_node;

    raw1394_destroy_handle(phandle);
    return result;  
}

