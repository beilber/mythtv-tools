/*
 *  mythprime -- a mythtv firewire primer
 *  developed for mythbuntu MythTV 2008 by majoridiot
 *
 *  attempts to intelligently locate and stabilize connections to
 *  cable stbs on the firewire bus
 *
 *  called by /etc/init.d/mythtv-backend on -start and -restart
 *
 *  returns: 0 on successful prime
 *           1 on handle creation error
 *	         2 on priming error
 *
 *  incorporates code (c) by: Jim Lohmeyer, Jim Westfall, Dan Dennedy, Stacy D. Son, 
 *                             Matt Porter and Chris Ingrassia, et. al
 *
 *  depends on: libraw1394-dev libiec61883-dev libavc1394-dev
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
#include <time.h>
#include <libraw1394/csr.h>
#include <netinet/in.h>
#include <libraw1394/raw1394.h>
#include <libavc1394/avc1394.h>
#include <libavc1394/rom1394.h>
#include <libiec61883/iec61883.h>
#include "firewire_utils.h"
#include "stbs.h"

#define VERSION             ".69h beta"

struct STBList STBS[6];

int channel = 0;
int PowerOn = 0;
int ResetFirst = 0;
int WasReset = 0;
int ForcedChanger = -1;
    
int sync_failed = 0;
int nodata = 0;
int verbose = 0;
int port = 0;		

void ms_sleep(unsigned long millisecs)
{
    struct timespec req = { 0 };
    time_t secs = (int)(millisecs / 1000);
    millisecs = millisecs -( secs * 1000);
    req.tv_sec = secs;
    req.tv_nsec = millisecs * 1000000L;

    while(nanosleep(&req,&req) == -1)
         continue;
}

void bad_id(void)
{
    fprintf(stderr, "UNKNOWN STB\n\n");
    fprintf(stderr, "Please email the Vendor and Model ID along with\n");
    fprintf(stderr, "the model number of your STB to: major.idiot@gmail.com\n\n");
    fprintf(stderr, "Please run scanfw again with the -f option to force a channel changer for your STB\n\n");
    exit(0);
}

static octlet_t get_guid(raw1394handle_t handle, nodeid_t node)
{
	quadlet_t       quadlet;
	octlet_t        offset;
	octlet_t        guid = 0;

	offset = CSR_REGISTER_BASE + CSR_CONFIG_ROM + PLUGREPORT_GUID_HI;

	int error = raw1394_read(handle, node, offset, sizeof(quadlet_t), &quadlet);
	if (error == -1)
	{
        int offline = (errno == 11);
        if (!offline)
        {
            fprintf(stderr, "get_guid HI error: %d", errno);
            exit(1);
        }
        return -1;
    }
        
	quadlet = htonl(quadlet);
	guid = quadlet;
	guid <<= 32;
	offset = CSR_REGISTER_BASE + CSR_CONFIG_ROM + PLUGREPORT_GUID_LO;

    error = raw1394_read(handle, node, offset, sizeof(quadlet_t), &quadlet);
    if (error == -1)
  	{
         int offline = (errno == 11);
        if (!offline)
        {
            fprintf(stderr, "get_guid HI error: %d", errno);
            exit(1);
        }
        return -1;
    }
	quadlet = htonl(quadlet);
	guid += quadlet;

	return guid;
}

octlet_t stb_offline(octlet_t TryGUID, nodeid_t node)
{
    while (TryGUID == -1)
    {
        ms_sleep(500);
        TryGUID = get_guid(handle, 0xffc0 | node);
    }
    return TryGUID;
}

int bus_reset(raw1394handle_t handle)
{
    if (!raw1394_reset_bus_new(handle, RAW1394_LONG_RESET) == 0)
    {
        fprintf(stderr, "firewire bus reset FAILED! %d: %s\n", errno, strerror( errno ));
        raw1394_destroy_handle(handle);        
        return 1;
     
    }
    ms_sleep(2500);
    return 0;
}

void FindSTBs(raw1394handle_t handle, nodeid_t node, int stb)
{
    
    int i;
    int j;
    int vendor_count;
    int model_count;
    octlet_t guid;
    octlet_t TryGUID;
    rom1394_directory romdir;

    if (rom1394_get_directory(handle, node, &romdir) < 0) 
    {
        fprintf(stderr, "ERROR reading config rom directory for node %d\n", node);
        exit(1);
    }

    VERBOSE("STB Vendor ID: 0x%04x Model ID: 0x%04x, ", 
        romdir.vendor_id, romdir.model_id); 

    STBS[stb].port = port;
    STBS[stb].node = node;
    STBS[stb].p2p = -1;
    STBS[stb].changer = -1;

    guid = get_guid(handle, 0xffc0 | node);
    if (guid == -1)
        guid = stb_offline(TryGUID, node);

    STBS[stb].guid = guid;

    vendor_count = ( sizeof(sa_vendor) / sizeof(sa_vendor[0]) );
    model_count = ( sizeof(sa_model) / sizeof(sa_model[0]) );
    
    for (i = 0; i < vendor_count; i++)
    {
        if (sa_vendor[i] == romdir.vendor_id)
        {
            VERBOSE("Scientific Atlanta Model ");
            STBS[stb].p2p = 1;

            for (j = 0; j < model_count; j++)
            {
                if (sa_model[j] == romdir.model_id)
                {
                    STBS[stb].changer = j + 1;
                    VERBOSE("%s\n\n", sa_name[j]);
                }
            }
        }
    }
    
    vendor_count = ( sizeof(moto_vendor) / sizeof(moto_vendor[0]) );
    model_count = ( sizeof(moto_model) / sizeof(moto_model[0]) );
    
    for (i = 0; i < vendor_count; i++)
    {
        if (moto_vendor[i] == romdir.vendor_id)
        {
            STBS[stb].changer = 7;
            VERBOSE("Motorola Model ");
            STBS[stb].p2p = 0;
            for (j = 0; j < model_count; j++)
            {
                if (moto_model[j] == romdir.model_id)
                    {
                        VERBOSE("%s\n", moto_name[j]);
                        break;
                    }
            }

            if (j == model_count)
                VERBOSE("Unknown");
        }
    }

    if (ForcedChanger != -1)
    {
        VERBOSE("(Using channel changer #%d)\n\n", ForcedChanger);
        STBS[stb].changer = ForcedChanger;
        STBS[stb].p2p = (ForcedChanger > 5) ? 0: 1;        
        return;
    }
         
    if (STBS[stb].p2p == -1 || STBS[stb].changer == -1)
        bad_id();
    VERBOSE("\n");
    return;
    
}

void ChStb1(raw1394handle_t handle, nodeid_t node, int stbchn, int changer)
{
    quadlet_t AVCcmd[3];

    unsigned int retlen = 0;
    int i;
    int digit[3];

    digit[0] = (stbchn % 1000) / 100;
    digit[1] = (stbchn % 100)  / 10;
    digit[2] = (stbchn % 10);

    switch (changer)
    {
        case MotoSing:
           for (i=0; i<3; i++)
           {
               AVCcmd[0] = MOTOSING_CMD0 | digit[i];
               AVCcmd[1] = 0x0;

               quadlet_t *AVCret = avc1394_transaction_block2(handle, node, AVCcmd, 2, &retlen, 1);
               if ( (AVCret[0] >> 24) != AVC1394_RESP_ACCEPTED)
               {
                   fprintf(stderr, "CHANGER ERROR- return: %08x %08x %08x\n", 
                       (quadlet_t) AVCret[0], (quadlet_t) AVCret[1], (quadlet_t) AVCret[2]);
                   exit(1);
               }
               ms_sleep(750);
            }
            return;
            
        case MotoFast:
            AVCcmd[0] = COMMON_CMD0;
            AVCcmd[1] = COMMON_CMD1 | (stbchn << 8) | 0x000000ff;
            AVCcmd[2] = COMMON_CMD2;
            break;
       
        case SA3250HD:
            AVCcmd[0] = SA3250HD_CMD0 | AVC1394_SA3250_OPERAND_KEY_PRESS;
            AVCcmd[1] = COMMON_CMD1 | (digit[2] << 16) | (digit[1] << 8) | digit[0];
            AVCcmd[2] = COMMON_CMD2;
            break;

        case SA4200HD:
        case SA8300HD:
            AVCcmd[0] = SA3250HD_CMD0 | AVC1394_SA3250_OPERAND_KEY_PRESS;
            AVCcmd[1] = COMMON_CMD1 | (stbchn << 8);
            AVCcmd[2] = 0x00;
            break;

        case SA4250HDC:
            AVCcmd[0] = SA3250HD_CMD0 | AVC1394_SA3250_OPERAND_KEY_PRESS;
            AVCcmd[1] = COMMON_CMD1 | (stbchn << 8);
            AVCcmd[2] = COMMON_CMD2;
            break;
            
        case SA4250HDC_alt:
            AVCcmd[0] = COMMON_CMD0;
            AVCcmd[1] = COMMON_CMD1 | (stbchn << 8);
            AVCcmd[2] = 0x00;
            break;

        default:
            fprintf(stderr, "Invalid changer option\n");
            exit(1);
    }

    ms_sleep(750);

    quadlet_t *AVCret = avc1394_transaction_block2(handle, node, AVCcmd, 3, &retlen, 2);

    if ( (AVCret[0] >> 24) != AVC1394_RESP_ACCEPTED)
    {
        fprintf(stderr, "CHANGER ERROR- return: %08x %08x %08x\n", 
            (quadlet_t) AVCret[0], (quadlet_t) AVCret[1], (quadlet_t) AVCret[2]);
        exit(1);
    }

    switch (changer)
    {
        case SA3250HD:
            AVCcmd[0] = SA3250HD_CMD0 | AVC1394_PANEL_KEYTUNE;
            AVCcmd[1] = COMMON_CMD1 | (digit[0] << 16) | (digit[1] << 8) | digit[2];
            AVCcmd[2] = COMMON_CMD2;
            break;

        case SA4250HDC_alt:
            AVCcmd[0] |= AVC1394_PANEL_OPERAND_RELEASE;

        default:
            return;
    }

    AVCret = avc1394_transaction_block2(handle, node, AVCcmd, 3, &retlen, 2);

    if ( (AVCret[0] >> 24) != AVC1394_RESP_ACCEPTED)
    {
        fprintf(stderr, "CHANGER ERROR- return: %08x %08x %08x\n", (quadlet_t) AVCret[0], (quadlet_t) AVCret[1], (quadlet_t) AVCret[2]);
        exit(1);
    }

    return;
}

static int read_packet (unsigned char *tspacket, int len, 
                        unsigned int dropped, void *callback_data)
{
    int *count = (int *)callback_data;

    if (dropped)
    {
        VERBOSE("Dropped %d packet(s)...\n", dropped);
        return 0;
    }

    if (tspacket[0] != SYNC_BYTE)
    {
        sync_failed = 1;
        return 0;
    }
    nodata = 0;
    *count = *count + 1;
    return 1;
}

int test_connection(raw1394handle_t handle, int channel)
{
    int count = 0;
    int retry = 0;
    int fd = raw1394_get_fd(handle);
    iec61883_mpeg2_t mpeg;
    struct timeval tv;
    fd_set rfds;
    sync_failed = 0;

    mpeg = iec61883_mpeg2_recv_init(handle, read_packet, (void*) &count);
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
             raw1394_loop_iterate(handle);
        }
        else
        {
            retry++;
        }
    }
    iec61883_mpeg2_recv_stop(mpeg);
    iec61883_mpeg2_close(mpeg);

    if (sync_failed)
    {
        VERBOSE("Error: could not find stream synch byte.  Is STB tuned to a valid/unencrypted channel? ");
        return 0;
    }

    return count;
}

int test_p2p(raw1394handle_t handle, nodeid_t node) {

    int channel, count, success = 0;
    channel = node;

    VERBOSE("Establishing P2P connection on node %d, channel %d... ", node, channel);
    fflush(stdout);

    if (iec61883_cmp_create_p2p_output(handle, node | 0xffc0, 0, channel,
                                       1 /* fix me, speed */ ) != 0)
    {
        fprintf(stderr, "ERROR: iec61883_cmp_create_p2p_output failed\n");
        return 0;
    }
    
    VERBOSE("P2P: connection established.\n");
    VERBOSE("P2P: receiving packets... ");

    count = test_connection(handle, channel);  
    
    if (count >= MIN_PACKETS)   
    {
        VERBOSE("%d packets received\n", count);
        success = 1;
    }
    else
    {
        VERBOSE("FAILED.\n");
    }

    VERBOSE("Disconnecting P2P connection on node %d, channel %d\n", node, channel);
    iec61883_cmp_disconnect(handle, node | 0xffc0, 0,
                            raw1394_get_local_id (handle),
                            -1, channel, 0);
    return success;
}

int test_broadcast(raw1394handle_t handle, nodeid_t node) {

    int channel, count, success = 0;
    channel = 63 - node;

    VERBOSE("Creating Broadcast connection on node %d, channel %d... ", node, channel);
    fflush(stdout);

    if (iec61883_cmp_create_bcast_output(handle, node | 0xffc0, 0, channel, 
                                         1 ) != 0)
    {
        VERBOSE("ERROR: iec61883_cmp_create_bcast_output failed\n");
        return 0;
    }
    
    VERBOSE("Broadcast: connection established.\n");
    VERBOSE("Broadcast: receiving packets... ");

    count = test_connection(handle, channel);  
    if (count >= MIN_PACKETS)   
    {
        VERBOSE("%d packets received.\n", count);
        success = 1;		
    }
    else
    {
        VERBOSE("FAILED.\n");
    }

    VERBOSE("Disconnecting broadcast connection on node %d, channel %d\n", node, channel);
    iec61883_cmp_disconnect(handle, node | 0xffc0, 0,
                            raw1394_get_local_id (handle),
                            -1, channel, 0);
    return success;
}  

int tap_plug0(raw1394handle_t handle, nodeid_t node, int action, int plugstate) {

    struct iec61883_oPCR o_pcr;
    int plugact;
    int plugnode = (0xffc0 + node);

    if (!action)
    {
        plugact = iec61883_get_oPCR0(handle, plugnode, &o_pcr);  
        if (plugact < 0)
            return 0; 
        else
            return 1;
    }

    o_pcr.n_p2p_connections = plugstate;
    plugact = iec61883_set_oPCR0(handle, plugnode, o_pcr);
        if (plugact < 0)
        {
            VERBOSE("ERROR setting 0PCR0: %s\n", strerror( errno ));
            return 0;  
        }        
       
    return 1;
}

int moto_prime(int stb)
{
    int retries;  
    int success = 0;
    int tries = 0;
    int loop;
    int node = STBS[stb].node;
    
    handle = raw1394_new_handle_on_port(STBS[stb].port);
    if (!handle)
    {		
        fprintf(stderr, "Failed to acquire priming handle on port %d.\n", STBS[stb].port);
        raw1394_destroy_handle(handle);        
        return 1;
    }    
    
    if (channel)
    {
        ChStb1(handle, node, channel, STBS[stb].changer);
        ms_sleep(2000);
    }

    for(loop = 0; loop < 5; loop++)
        success += test_broadcast(handle, node);

    if (success == 5)
    {
        raw1394_destroy_handle(handle);    
        return 1;  
    }
    
    VERBOSE("\nconnection unstable, resetting firewire bus... ");

    if (!bus_reset(handle))
        VERBOSE("firewire bus reset\n");
    else
        goto error;
       
    tries = 0;
    success = 0;

    while (tries < 3)  
    {
	retries = 0;  
        while (retries < 10)  
        { 
            if (test_p2p(handle, node))  
            {
                success = 0; 
                while (test_broadcast(handle, node))  
                {
                    success++; 
                    if (success == 5)
                    {
                         raw1394_destroy_handle(handle);
                         return 1;
                    }    
                }
            }
            retries++;  
        }
        tries++;  
    }
error:
    raw1394_destroy_handle(handle);    
    return 0;
}

int sa_prime(int stb)
{
    int success = 0;
    int tries = 0;
    int loop = 0;

    int node = STBS[stb].node;
    
    handle = raw1394_new_handle_on_port(STBS[stb].port);
    if (!handle)
    {		
        fprintf(stderr, "Failed to acquire priming handle on port %d.\n", STBS[stb].port);
        raw1394_destroy_handle(handle);        
        return 1;
    }    
    
    if (channel)
    {
        ChStb1(handle, node, channel, STBS[stb].changer);
        ms_sleep(2000);
    }
    
    if (!tap_plug0(handle, node, 1, 1))
        goto error;

    for(; loop < 5; loop++)
        success += test_p2p(handle, node);

    if (!tap_plug0(handle, node, 1, 0))
        goto error;
    
    if (success == 5)
    {
        raw1394_destroy_handle(handle);         
        return 1;  
    }

    VERBOSE("\nconnection unstable, resetting firewire bus... ");

    if (!bus_reset(handle))
        VERBOSE("firewire bus reset\n");
    else
        goto error;
    
    while (tries < 5)
    {
        if (!tap_plug0(handle, node, 1, 1))
            goto error;
            
        for(loop = 0; loop < 5; loop++)
            success += test_p2p(handle, node);

        if (!tap_plug0(handle, node, 1, 0))
            goto error;
        
        if (success == 5)
        {
            raw1394_destroy_handle(handle);    
            return 1;
        }
        
        success = 0;
        tries++;  
    }
error:
    raw1394_destroy_handle(handle);        
    return 0;
}

int prime_stb(int stb) 
{
    int result;
    
    if (STBS[stb].p2p)
        result = sa_prime(stb);
    else
        result = moto_prime(stb);
    return result;  
}

void usage(void) {
    fprintf(stderr, "mythprime [-c <channel#>] [-f <changer#>] [-P] [-R] [-v][-V][-h]\n");
    fprintf(stderr, " Options:\n");
    fprintf(stderr, "    -c <channel number>\tTune to safe <channel> before priming\n");
    fprintf(stderr, "    -f <changer> Force a channel changer:\n");
    fprintf(stderr, "    \t\t  1 = SA3250HD\n");
    fprintf(stderr, "    \t\t  2 = SA4200HD (and some 3250s)\n");
    fprintf(stderr, "    \t\t  3 = SA4250HDC\n");
    fprintf(stderr, "    \t\t  4 = SA4250HDC alternate (and some 4240HDCs)\n");    
    fprintf(stderr, "    \t\t  5 = SA8300\n");
    fprintf(stderr, "    \t\t  6 = Motorola Fast (DCH and DCT series)\n");
    fprintf(stderr, "    \t\t  7 = Motorola Single-digit (default)\n");    
    fprintf(stderr, "    -P\t\tpower on STB (default: no)\n");
    fprintf(stderr, "    -R\t\treset firewire bus before doing anything else (default: no)\n");    
    fprintf(stderr, "    -v          Verbose output\n");
    fprintf(stderr, "    -V          Display version information and exit\n");
    fprintf(stderr, "    -h          Display this help and exit\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int c;
    int devices;
    int primeloop;
    int searchnode = 0;
    int TotalPorts;
    int STBcount = 0;
    int primed = 0;
    int failed = 0;
    int skipped = 0;
    int ignored = 0;
    int runs = 0;

    opterr = 0;

    while ((c = getopt(argc, argv, "c:f:PRvVh")) != -1)
    {
        switch (c) 
        {
            case 'c':
                channel = atoi(optarg);
                break;

            case 'f':
                ForcedChanger = atoi(optarg);
                if ( ForcedChanger <= 0 || ForcedChanger > 7)
                {
                    fprintf(stderr, "Invalid channel changer option\n");
                    usage();
                }
                ForcedChanger = (ForcedChanger == 5) ? 2: ForcedChanger;
                break;

            case 'P':
                PowerOn= 1;
                break;

            case 'R':
                ResetFirst= 1;
                break;                
                                                
            case 'v':
                verbose = 1;
                break;

            case 'V':
                fprintf(stderr, "\nmythprime version %s\n\n", VERSION);
                exit(1);
                break;

            case 'h':
                usage();
                exit(1);
                break;

            default:
                fprintf(stderr, "\nInvalid command line\n");
                usage();
                
        }
    }

    VERBOSE("\nmythprime %s\n\n", VERSION);

    VERBOSE("Acquiring firewire handle... ");

    if (!(handle = raw1394_new_handle()))
    {
        VERBOSE("FAILED!\n");
        return 1;
    }

    VERBOSE("OK.\n");

    TotalPorts = raw1394_get_port_info(handle, NULL, 0);
    VERBOSE("%d port(s) found\n", TotalPorts);
    
    if (ResetFirst)
    {
        if (!bus_reset(handle))
            VERBOSE("Initial bus reset succeeded\n");
        else
            return 1;
    }

    while (port < TotalPorts)
    {
        VERBOSE("\nAcquiring handle on port %d... ", port);
        handle = raw1394_new_handle_on_port(port);
    
        if (!handle)
        {		
            fprintf(stderr, "Failed!\n");
            return 1;
        }

        devices = raw1394_get_nodecount(handle);
        VERBOSE("%d devices detected.\n", devices - 1);
                     
        for ( ; devices > 0; devices--)
        {
            if (!tap_plug0(handle, searchnode, 0, 0))
            {
                VERBOSE("Skipping empty node %d\n", searchnode);
                skipped++; 
                searchnode++;
                continue;
            }

            if (!avc1394_check_subunit_type(handle, searchnode, TUNER) ||
                !avc1394_check_subunit_type(handle, searchnode, PANEL))
            {
                fprintf(stderr, "node %d is not an STB and will not be primed\n", searchnode); 
                ignored++;  
                searchnode++;
                continue;  
            }
            
            if (PowerOn)
            {
                VERBOSE("Powering device on port %d node %d ", port, searchnode);
                if (avc1394_send_command(handle, searchnode, POWER_ON))
                    VERBOSE("failed.\n");
                else
                    VERBOSE("succeeded... pausing 5 seconds.\n");
                 ms_sleep(5000);
            }            

            FindSTBs(handle, searchnode, STBcount);
            STBcount++;
            searchnode++;
        }
        port++;
    }
    
    raw1394_destroy_handle(handle);
            
    VERBOSE("%d STB(s) found:\n-------------\n", STBcount);
    for (c = 0; c < STBcount; c++)
        VERBOSE("STB %d: port=%d, node=%d, p2p=%d, changer=%d, GUID=0x%08x%08x\n\n", c+1, 
            STBS[c].port, STBS[c].node, STBS[c].p2p, STBS[c].changer,
            (quadlet_t) (STBS[c].guid >> 32), (quadlet_t) (STBS[c].guid & 0xffffffff));

    for (primeloop = 0; primeloop < STBcount; primeloop++)
    {
        VERBOSE("Priming STB %d:\n", primeloop+1);
        for (runs = 0; runs < 3; runs++)
        {
            if (!prime_stb(primeloop))
            {
                VERBOSE("Failed.\n");      
                continue;
            }
            else
            {
                VERBOSE("Successfully primed stb on port %d node %d.\n\n", port, searchnode);      
                primed++;
                failed--;
                break; 
            }
        } 
        failed++;
    }
    
    if (!failed)
    {
        fprintf(stderr, "%d stbs primed, %d non-stbs ignored and %d empty nodes skipped on " \
           "%d ports.\n", primed, ignored, skipped, TotalPorts);      
        return 0;
    }
        
    fprintf(stderr, "Priming Errors encountered: %d stbs primed, %d stbs failed to prime, "\
        "%d non-stbs ignored and %d empty nodes skipped on %d ports in %d runs.\n",\
        primed, failed, ignored, skipped, TotalPorts, runs);      

    return 2;   
}

