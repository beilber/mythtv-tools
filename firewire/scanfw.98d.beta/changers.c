/*
 *  changers.c -- channel changers and STB identification for scanfw
 *  version .14 developed for mythbuntu MythTV 2008 by majoridiot
 *
 *  incorporates code (c) Jim Lohmeyer, Stacy D. Son, Matt Porter 
 *                        Chris Ingrassia, and Jim Westfall, et. al.
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
#include <libraw1394/raw1394.h>
#include <libavc1394/avc1394.h>
#include <libiec61883/iec61883.h>
#include <libavc1394/rom1394.h>
#include "firewire_utils.h"
#include "stbs.h"

void bad_id(void)
{
    printf("UNKNOWN STB\n\n");
    printf("Please email the Vendor and Model ID along with\n");
    printf("the model number of your STB to: major.idiot@gmail.com\n\n");
    printf("Please run scanfw again with the -f option to force a channel changer for your STB\n\n");
    exit(0);
}

void id_stb(raw1394handle_t handle, nodeid_t node, int *p2p_stb, int *changer)
{
    int i;
    int j;
    int vendor_count;
    int model_count;

    rom1394_directory romdir;

    if (rom1394_get_directory(handle, node, &romdir) < 0) 
    {
        printf("ERROR reading config rom directory for node %d\n", node);
        exit(1);
    }
    
    printf("STB Vendor ID: 0x%04x Model ID: 0x%04x, ", 
        romdir.vendor_id, romdir.model_id); 

    if (*changer == -1)
    {
        vendor_count = ( sizeof(sa_vendor) / sizeof(sa_vendor[0]) );
        model_count = ( sizeof(sa_model) / sizeof(sa_model[0]) );
    
        for (i = 0; i < vendor_count; i++)
        {
            if (sa_vendor[i] == romdir.vendor_id)
            {
                printf("Scientific Atlanta Model ");
                *p2p_stb = 1;
    
                for (j = 0; j < model_count; j++)
                {
                    if (sa_model[j] == romdir.model_id)
                    {
                        *changer = j + 1;
                        printf("%s\n\n", sa_name[j]);
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
            printf("Motorola Model ");
            *changer = 7;
            *p2p_stb = 0;
            for (j = 0; j < model_count; j++)
            {
                if (moto_model[j] == romdir.model_id)
                {
                    printf("%s\n\n", moto_name[j]);
                    break;
                }
            }

            if (j == model_count)
                printf("Unknown\n\n");
            }
        }
    }
    else
    {
        printf("\nUsing user selected changer %d\n", *changer);
        return;
    }
         
    if (*p2p_stb == -1 || *changer == -1)
        bad_id();
}

void ChStb1(raw1394handle_t avchandle, nodeid_t node, int stbchn, int wait, int changer)
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

               quadlet_t *AVCret = avc1394_transaction_block2(avchandle, node, AVCcmd, 2, &retlen, 1);
               if ( (AVCret[0] >> 24) != AVC1394_RESP_ACCEPTED)
               {
                   printf("CHANGER ERROR- return: %08x %08x %08x\n", (quadlet_t) AVCret[0], (quadlet_t) AVCret[1], (quadlet_t) AVCret[2]);
                   exit(1);
               }
               ms_sleep(750);
            }
            goto finished;
            
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
            printf("Invalid changer option\n");
            exit(1);
    }

    ms_sleep(750);

    quadlet_t *AVCret = avc1394_transaction_block2(avchandle, node, AVCcmd, 3, &retlen, 2);

    if ( (AVCret[0] >> 24) != AVC1394_RESP_ACCEPTED)
    {
        printf("CHANGER ERROR- return: %08x %08x %08x\n", (quadlet_t) AVCret[0], (quadlet_t) AVCret[1], (quadlet_t) AVCret[2]);
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
            goto finished;
    }

    AVCret = avc1394_transaction_block2(avchandle, node, AVCcmd, 3, &retlen, 2);

    if ( (AVCret[0] >> 24) != AVC1394_RESP_ACCEPTED)
    {
        printf("CHANGER ERROR- return: %08x %08x %08x\n", (quadlet_t) AVCret[0], (quadlet_t) AVCret[1], (quadlet_t) AVCret[2]);
        exit(1);
    }

finished:
    raw1394_iso_recv_flush(handle);
    setcrypt(-1);
    ms_sleep(wait * 1000);
}

