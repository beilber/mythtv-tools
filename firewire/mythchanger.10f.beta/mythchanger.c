/*
 *  mythchanger -- a mythtv firewire channger changer
 *  developed for mythbuntu MythTV November 2008 by majoridiot
 *
 *
 *  incorporates code (c) by: Jim Lohmeyer, Jim Westfall, Dan Dennedy, Stacy D. Son, 
 *                            Matt Porter and Chris Ingrassia, et. al
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

#define VERSION ".10f beta"

struct STBList STBS[6];

int channel = 0;
int PowerOn = 0;
int ResetFirst = 0;
int ForcedChanger = -1;
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
    fprintf(stderr, "Please run mythchanger again with the -f option to force a channel changer for your STB\n\n");
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
            for (j = 0; j < model_count; j++)
            {
                if (moto_model[j] == romdir.model_id)
                    {
                        VERBOSE("%s\n\n", moto_name[j]);
                        break;
                    }
            }

            if (j == model_count)
                VERBOSE("Unknown\n\n");
        }
    }

    if (ForcedChanger != -1)
    {
        VERBOSE("(Using user selected channel changer #%d)\n\n", ForcedChanger);
        STBS[stb].changer = ForcedChanger;
        return;
    }
         
    if (STBS[stb].changer == -1)
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
            AVCcmd[2] = 0x00;
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

void usage(void) {
    fprintf(stderr, "mythchanger [-c <channel#>] [-f <changer#>] [-g <GUID>] [-P] [-R] [-v][-V][-h]\n");
    fprintf(stderr, " Options:\n");
    fprintf(stderr, "    -c <channel number>\n");
    fprintf(stderr, "    -f <changer> force channel changer:\n");
    fprintf(stderr, "              \t  1 = SA3250HD\n");
    fprintf(stderr, "              \t  2 = SA4200HD (and some 3250s)\n");
    fprintf(stderr, "              \t  3 = SA4250HDC\n");
    fprintf(stderr, "              \t  4 = SA4250HDC alternate (and 4240HDCs with 4250 firmware)\n");    
    fprintf(stderr, "              \t  5 = SA8300\n");
    fprintf(stderr, "              \t  6 = Motorola Fast (DCH and DCT series)\n");
    fprintf(stderr, "              \t  7 = Motorola Single-digit (default)\n");
    fprintf(stderr, "    -g <GUID>\tGUID of target STB (for multiple STB configurations)\n");    
    fprintf(stderr, "    -P\t\tpower on STB (default: no)\n");
    fprintf(stderr, "    -R\t\treset firewire bus before doing anything else (default: no)\n");    
    fprintf(stderr, "    -v          - verbose output\n");
    fprintf(stderr, "    -V          - Display version information and exit\n");
    fprintf(stderr, "    -h          - display this help and exit\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int c;
    int devices;
    int searchnode = 0;
    int TotalPorts;
    int STBcount = 0;
    int skipped = 0;
    int ignored = 0;
    octlet_t TargetGUID = 0;

    opterr = 0;

    while ((c = getopt(argc, argv, "c:f:g:PRvVh")) != -1)
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

            case 'g':
                TargetGUID = strtoll(optarg, 0, 16);
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
                fprintf(stderr, "\nmythchanger version %s\n\n", VERSION);
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

    if (!channel)
        usage();

    VERBOSE("\nmythchanger %s\n\n", VERSION);

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
                fprintf(stderr, "node %d is not an STB\n", searchnode); 
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
    
      
    VERBOSE("%d STB(s) found:\n-------------\n", STBcount);
    for (c = 0; c < STBcount; c++)
        VERBOSE("STB %d: port=%d, node=%d, changer=%d, GUID=0x%08x%08x\n\n", c+1, 
            STBS[c].port, STBS[c].node, STBS[c].changer,
            (quadlet_t) (STBS[c].guid >> 32), (quadlet_t) (STBS[c].guid & 0xffffffff));

    if (TargetGUID)
    {
        for (c = 0; c < STBcount; c++)
        {
            if (TargetGUID == STBS[c].guid)
            {
                ChStb1(handle, STBS[c].node, channel, STBS[c].changer);
                raw1394_destroy_handle(handle);
                return 0;
            }
        }

    fprintf(stderr, "\nERROR- STB with GUID=0x%08x%08x was not found!\n\n",  
            (quadlet_t) (TargetGUID >> 32), (quadlet_t) (TargetGUID & 0xffffffff));          
    raw1394_destroy_handle(handle);
    return 1;
    }

    ChStb1(handle, STBS[0].node, channel, STBS[0].changer);
    raw1394_destroy_handle(handle);
    return 0;
}

