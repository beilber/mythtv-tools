/*
 *  scanfw.c firewire CCI/EMI channel scanner
 *  developed for mythbuntu MythTV 2008 by majoridiot
 *
 *  incorporates code (c) by: Jim Lohmeyer, Jim Westfall and Dan Dennedy, et. al.
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
#include <signal.h>
#include <sys/select.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <libraw1394/raw1394.h>
#include <libavc1394/avc1394.h>
#include <libiec61883/iec61883.h>
#include "firewire_utils.h"

#define VERSION             ".98d beta"

#define RAW_BUF_SIZE        4096
#define BUFSIZE             32

struct chans
{
    int channum;
    char channame[10];
};

struct chans chanlist[999];

time_t curtime;
struct tm *loctime;

enum raw1394_iso_dma_recv_mode mode = RAW1394_DMA_DEFAULT;

raw1394handle_t handle;
raw1394handle_t avchandle;

pthread_mutex_t cci_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t Listener;

int nocheck = 0;
int avail_ports = 0;
int searchport = 0;
int searchnode = 0;
int devices;
int eflag = 0;
int singlechan = 0;
int singleflag = 0;
int startchan = 0;
int watchchan = 0;
int watchflag = 0;
int errorflag = 0;
int wait = 4;
int p2p_stb = -1;
int changer = -1;;
int channel = 0;
int chanloop = 0;
int goodchan[999] ;
int badchan[999];
int errorchan[999];
int errored[999];
int primechan = -1;
int total_chans;
int good = 0;
int bad = 0;
int error = 0;
int finalerr = 0;
int crypt;
int newcrypt;
int port = 0;
int node = -1;
char timebuf[BUFSIZE];

void setcrypt(int newcrypt)
{
    assert( pthread_mutex_lock( &cci_mutex ) == 0);
    crypt = newcrypt;
    assert( pthread_mutex_unlock( &cci_mutex ) == 0);
}


static enum raw1394_iso_disposition
iso_handler(raw1394handle_t handle, unsigned char *data,
        unsigned int length, unsigned char channel,
        unsigned char tag, unsigned char sy, unsigned int cycle,
        unsigned int dropped)
{
    if (dropped)
       return RAW1394_ISO_OK;

    if (length > 8) 
    {
        if (sy < 8)
        {
            if (crypt == 0)
                return RAW1394_ISO_OK;
            else
            {
                setcrypt(0);
                return RAW1394_ISO_OK;
            }
        }

        if (crypt == 1)
            return RAW1394_ISO_OK;
        else
            {
                setcrypt(1);
                return RAW1394_ISO_OK;
            }
    }
    return RAW1394_ISO_OK;
}

void *listener( void *arg )
{
    int fd = raw1394_get_fd(handle);
    struct timeval tv;
    fd_set rfds;

    raw1394_iso_recv_flush(handle);

    while(1)
    {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0)
        {
             raw1394_loop_iterate(handle);
        }
    }
}

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

int tap_plug0(raw1394handle_t handle, nodeid_t node, int action, int plugstate)
{
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
            return 0;  
        else      
            return 1;
}

void stop_iso_xfer(void)
{
    pthread_cancel(Listener);
    raw1394_iso_shutdown(handle);
    raw1394_destroy_handle(handle);

}

void start_iso_xfer(void)
{
    handle = raw1394_new_handle_on_port(port);

    if (!handle)
    { 
        printf("handle acquisition FAILED!\n");
        exit(1);
    }

    if (p2p_stb)
    {
        channel = node;

        if (iec61883_cmp_create_p2p_output(handle, node | 0xffc0, 0, channel, 1 ) != 0)
        {
            printf("iec61883_cmp_create_p2p_output failed\n");
            exit(1);
        }
    }
    else
    {
        channel = 63;

        if (iec61883_cmp_create_bcast_output(handle, node | 0xffc0, 0, channel, 1 ) != 0)
        {
            printf("iso_xfer iec61883_cmp_create_bcast_output failed\n");
            exit(1);
        }
    }

    if(raw1394_iso_recv_init(handle, iso_handler, 2000, 2048, channel, mode, -1) < 0)
    {
        printf("init ERROR/n/n");
        exit(1);
    }  

    if (raw1394_iso_recv_start(handle, -1, -1, 0) != 0) 
    {
        printf("ERROR starting isochronous receive\n");
        stop_iso_xfer();
        raw1394_destroy_handle(avchandle);
        exit(1);
    }

    assert(pthread_create( &Listener, NULL, listener, NULL) == 0);

}

int check_cci(void)
{
    switch (crypt)
    {
        case 0:
            goodchan[good++] = chanloop;
            printf("channel %d %s is clear\n", chanlist[chanloop].channum, 
                chanlist[chanloop].channame);
            return 0;

        case 1:
            badchan[bad++] = chanloop;
            printf("channel %d %s is encrypted\n", chanlist[chanloop].channum, 
                chanlist[chanloop].channame);
            return 0;

        default:
            return 1;
    }
}

void fw_recover(void)
{
    printf("ERROR scanning channel %d, recovering... \n", chanlist[chanloop].channum);
    fflush(stdout);

    stop_iso_xfer();
    ChStb1(avchandle, node, primechan, wait, changer);

    if (!prime_stb(port, &node, p2p_stb))
    {
        start_iso_xfer();
        goto recover_error;
    }

    start_iso_xfer();

    ChStb1(avchandle, node, chanlist[chanloop].channum, wait, changer);

    if (!check_cci())
        return;

    recover_error:
        errorchan[error++] = chanloop;
        printf("ERROR scanning channel %d %s\n", chanlist[chanloop].channum, 
            chanlist[chanloop].channame);
        return;
}

void final_report(void)
{
    int i;

    printf("\n");

    if (!eflag && good)
    {
        printf("clear channels:\n");

        for (i = 0; i < good; i++)
            printf("%d %s\n", chanlist[goodchan[i]].channum, chanlist[goodchan[i]].channame);
    }

    if (bad)
    {
        printf("\nencrypted channels:\n");

        for (i = 0; i < bad; i++)
            printf("%d %s\n", chanlist[badchan[i]].channum, chanlist[badchan[i]].channame);
    }

    if (finalerr)
    {
        printf("\nerrored channels:\n");

        for (i = 0; i < finalerr; i++)
            printf("%d %s\n", chanlist[errored[i]].channum, chanlist[errored[i]].channame);
    }

    printf("\n\n%3d channels scanned:\n%3d clear channels\n%3d encrypted channels\n%3d channels errored\n\n",
        good+bad+finalerr, good, bad, finalerr);

}

void scan(void)
{

    int c, i;

    start_iso_xfer();

    while ( chanloop < total_chans )
    {
        ChStb1(avchandle, node, chanlist[chanloop].channum, wait, changer);

        if (check_cci())
            fw_recover();
        
        chanloop++;
    }

    if ( error )
    {
        printf("\nErrors encountered during scan.  Attempting to re-scan %d channels.\n", error);

        stop_iso_xfer();
        ChStb1(avchandle, node, primechan, wait, changer);
        ms_sleep(1500);
        c = 0; 

        while (c < 3)
        {
            if (!prime_stb(port, &node, p2p_stb))
            {
                ChStb1(avchandle, node, chanlist[chanloop].channum, wait, changer);
                ms_sleep(1500);
                ChStb1(avchandle, node, primechan, wait, changer);
                c++;
            }
            else
                break;
        }

        if (c == 3)
        {
            printf("failed to prime stb- aborting re-scan\n");
            finalerr = error;
            for (i = 0; i < finalerr; i++)
                errored[i] = errorchan[i];
            final_report();
            printf("\n");
            exit(1);
        }

        start_iso_xfer();

        printf("Re-scanning errored channels...\n");

        for (i = 0 ; i < error; i++)
        {
            ChStb1(avchandle, node, chanlist[chanloop].channum, wait, changer);

            switch (crypt)
            {
                case 0:
                    goodchan[good++] = errorchan[i];
                    printf("channel %d %s is clear\n", chanlist[errorchan[i]].channum, 
                        chanlist[errorchan[i]].channame);
                    fflush(stdout);
                    break;

                case 1:
                    badchan[bad++] = errorchan[i];
                    printf("channel %d %s is encrypted\n", chanlist[errorchan[i]].channum, 
                        chanlist[errorchan[i]].channame);
                    fflush(stdout);
                    break;

                default:
                    while (i < (error - 1))
                    {
                        errored[finalerr++] = errorchan[i];
                        printf("ERROR scanning channel %d %s\n", chanlist[errorchan[i]].channum, 
                            chanlist[errorchan[i]].channame);
                        stop_iso_xfer();
                        ChStb1(avchandle, node, chanlist[chanloop].channum, wait, changer);
                        prime_stb(port, &node, p2p_stb);
                        start_iso_xfer();
                        ms_sleep(500);
                        break;
                     }
                     break;
            } 
                
        }
    }

    final_report();
    stop_iso_xfer();
    raw1394_destroy_handle(avchandle);
    printf("\n");
}

void get_time(void)
{
    curtime = time(NULL);
    loctime = localtime(&curtime);
    strftime(timebuf, BUFSIZE, "%a %b %d %T\n", loctime);
}

int findchan(int chan)
{
    int found;

    for (found = 0; found < total_chans; found++)
    {
        if (chanlist[found].channum == chan)
            return found;
    }

    return -1;
}

void watch_channel(int offset)
{
    int state = -1;

    printf("Watching the CCI for channel %d %s:\n", chanlist[offset].channum, 
        chanlist[offset].channame);
    fflush(stdout);

    start_iso_xfer();
    ChStb1(avchandle, node, chanlist[chanloop].channum, wait, changer);

    while (1)
    {
        switch (crypt)
        {
            case 0:

                if (state != 0)
                {
                    state = 0;
                    get_time();
                    printf("%s is clear\t", chanlist[offset].channame);
                    fputs (timebuf, stdout);
                    fflush(stdout);
                }

                break;

            case 1:

                if (state != 1)
                {
                    state = 1;
                    get_time();
                    printf("%s is encrypted\t", chanlist[offset].channame);
                    fputs (timebuf, stdout);
                    fflush(stdout);
                }

                break;

             default:

                printf("ERROR scanning channel %d %s- recovering...\n", chanlist[offset].channum, 
                    chanlist[offset].channame);
                stop_iso_xfer();
                ChStb1(avchandle, node, chanlist[chanloop].channum, wait, changer);
                prime_stb(port, &node, p2p_stb);
                start_iso_xfer();
                ChStb1(avchandle, node, chanlist[chanloop].channum, wait, changer);
                break;
        } 
    }
}

int init_firewire(void)
{
    printf("Checking for available firewire ports... ");

    if (!(handle = raw1394_new_handle()))
    {
        printf("FAILED!\n");
        exit(1);
    }

    avail_ports = raw1394_get_port_info(handle, NULL, 0);
    printf("%d port(s) found\n", avail_ports);

    while (searchport < avail_ports && node < 0)
    {
        printf("Acquiring handle on port %d.\n", searchport);

        handle = raw1394_new_handle_on_port(searchport);

        if (!handle)
        {
            printf("ERROR-- could not get 1394 handle on port %d!\n", searchport);
            exit(1);
        }

        printf("Searching for an stb on port %d\n", searchport);
        devices = raw1394_get_nodecount(handle);

        for (devices=devices; devices > 0; devices--)
        {
            if (!tap_plug0(handle, searchnode, 0, 0))
            {
                searchnode++;
                continue;
            }
            if (!avc1394_check_subunit_type(handle, searchnode, TUNER) ||
                !avc1394_check_subunit_type(handle, searchnode, PANEL))
            {
                searchnode++;
                continue;  
            }

            node = searchnode;             
            port = searchport;
            return node;
        }
        searchport++;
        searchnode = 0;
    }

    raw1394_destroy_handle(handle);
    return node;
}

void user_abort(int sig)
{
    int i;
    
    printf("\nSCAN ABORTED BY USER...\n");
    finalerr = error;
    for (i = 0; i < finalerr; i++)
       errored[i] = errorchan[i];
    final_report();
    printf("\n");
    (void) signal(SIGINT, SIG_DFL);
    exit(0);
}

void usage(void)
{
    printf("\nUsage: scanfw -i <channel file> -p <channel number> [OPTION]...\n");
    printf("Scans the CCI status of channels on the first stb found on the firewire bus.\n");
    printf("Version %s developed for the mythbuntu distribution of MythTV\n", VERSION); 
    printf("\n<channel file> is the .txt file generated by mythtv.firewire.channels.pl\n");
    printf("included with this distribution.\n\n");
    printf("Mandatory arguments:\n");
    printf("  -i <filename>\t\tchannnel file\n");
    printf("  -p <channel number>\tknown unencrypted channel for priming\n\n");
    printf(" Options:\n");
    printf("  -b <channel number>\tbegin scan at this channel\n");
    printf("  -c <channel number>\tscan single channel only\n");
    printf("  -e \t\t\tshow only encrypted and errored channels in final report\n");
    printf("  -f <changer>\t\tforce channel changer:\n");
    printf("              \t\t  1 = SA3250HD\n");
    printf("              \t\t  2 = SA4200HD (and some 3250s)\n");
    printf("              \t\t  3 = SA4250HDC\n");
    printf("              \t\t  4 = SA4250HDC alternate (and 4240HDCs with 4250 firmware)\n");    
    printf("              \t\t  5 = SA8300\n");
    printf("              \t\t  6 = Motorola Fast (DCH and DCT series)\n");
    printf("              \t\t  7 = Motorola alternate\n");    
    printf("  -n <number>\t\tnumber of channels to scan\n");
    printf("  -s <seconds>\t\tpause <seconds> before reading CCI of channel\n");
    printf("              \t\t(minimum 3, deafult 4, recommended 6+)\n");
    printf("  -w <channel number>\tcontinuously watch CCI of channel (^c to stop)\n");
    printf("  -h help\t\tdisplay usage information\n\n");
    printf("NOTE: -c and -w are either/or and can not be combined\n\n");
    exit(2);
}

int main(int argc, char **argv)
{

    extern char *optarg;
    extern int optopt;

    int i = 0;
    int c;
    int scannum = 0;
    FILE *infile;
    char *filename;
    char temp[5];
    char temp2[10];
    const char *unknown = "UNKNOWN";
    
    signal(SIGINT, user_abort);

    while ((c = getopt(argc, argv, "b:c:ef:i:n:p:s:w:h")) != -1)
    {
        switch (c) 
        {
            case 'b':
                startchan = atoi(optarg);
                break;

            case 'c':
                if (watchflag)
                    errorflag = 1;
                else
                {
                    singlechan = atoi(optarg);
                    singleflag = 1;
                }
                break;

            case 'e':
                eflag = 1;
                break;

            case 'f':
                changer = atoi(optarg);
                changer = (changer == 4) ? 2: changer;
                p2p_stb = (changer == MotoFast || changer == MotoSing) ? 0: 1;
                if ( changer <= 0 || changer > 7)
                {
                    printf("Invalid channel changer option\n");
                    usage();
                }
                break;

            case 'h':
                usage();
                exit(0);

            case 'i':
                filename = optarg;
                break;

            case 'n':
                scannum = atoi(optarg);
                break;

            case 'p':
                primechan = atoi(optarg);
                break;

            case 's':
                wait = atoi(optarg);
                wait = (wait < 3) ? 3: wait;
                break;

            case 'w':
                if (singleflag)
                    errorflag++;
                else
                {
                    watchchan = atoi(optarg);
                    watchflag = 1;
                }
                break;

            case ':':
                errorflag++;
                break;

            case '?':
                errorflag++;
        }
    }

    if ( errorflag || primechan < 1)
        usage();
  
    printf("\nmythbuntu firewire scanner %s\n\n", VERSION);

    if ((infile = fopen(filename, "r")) == NULL)
    {
        printf("Error- can not open file: %s\n", filename);
        exit(1);
    }

    if (fscanf(infile, "%s", temp) == 1)
        total_chans = atoi(temp);

    printf("reading channel file... ");

    while (i < total_chans)
    {
        if (fscanf(infile, "%s" "%s", temp, temp2) == 2)
        {
            chanlist[i].channum = atoi(temp);
            strcpy(chanlist[i].channame, temp2);
            i++;
        }
        else
        {
            if (i < total_chans)
            {
                printf("format ERROR in file %s... aborting.\n", filename);
                fclose(infile);
                exit(1);
            }
        }
    }

    printf("done.\n");
    fclose(infile);

    node = init_firewire();

    if (node < 0)
    {
        printf("ERROR- no STBs found!\n");
        return 1;
    }

    avchandle = raw1394_new_handle_on_port(port);
    if (!avchandle)
        { 
            printf("AVC handle acquisition FAILED!\n");
            return 1;
        }

    printf("Using STB located on port %d node %d\n" , port, node);

    id_stb(handle, node, &p2p_stb, &changer);

    if (!singleflag && !watchflag && !startchan)
    {
        scan();
        return 0;
    }

    if (singleflag)
    {
        i = findchan(singlechan);
        if ( i >= 0 )
        {
            chanlist[0].channum = singlechan;
            strcpy(chanlist[0].channame, chanlist[i].channame);            
        }
        else
        {
            printf("channel %d is not in your channel line-up- attempting scan anyway...\n",
                singlechan);
            chanlist[0].channum = singlechan;
            strcpy(chanlist[0].channame, unknown);
        }
     
        chanloop = 0;
        total_chans = 1;
        scan();
        
        return 0;
    }
    
    if (watchflag)
    {
        i = findchan(watchchan);

        if ( i >= 0 )
            watch_channel(i);
        
        printf("channel %d is not in your channel line-up- attempting to watch anyway...\n",
            watchchan);
        chanlist[0].channum = watchchan;
        strcpy(chanlist[0].channame, unknown);
        watch_channel(0);
    }
    
    if (startchan)
    {
        i = findchan(startchan);

        if ( i >= 0 )
        {
            chanloop = i;
            if (scannum)
                total_chans = chanloop + scannum;
            scan();
            return 0;
        }

        printf("ERROR- channel %d is not in your channel list!\n", startchan);
        exit(1);
    }

return 0;
}

