#define VERBOSE(args...)    do { if (verbose) printf(args); } while (0)

#define iec61883_get_oPCR0(h,n,v) iec61883_plug_get((h), (n), CSR_O_PCR_0, (quadlet_t *)(v))
#define iec61883_set_oPCR0(h,n,v) iec61883_plug_set((h), (n), CSR_O_PCR_0, *((quadlet_t *)&(v)))

#define AVC1394_PANEL_KEYTUNE               0x67
#define AVC1394_SA3250_OPERAND_KEY_PRESS    0xe7

#define POWER_ON        AVC1394_CTYPE_CONTROL | AVC1394_SUBUNIT_TYPE_UNIT | AVC1394_SUBUNIT_ID_IGNORE | \
                        AVC1394_COMMAND_POWER | AVC1394_CMD_OPERAND_POWER_ON
#define COMMON_CMD0     AVC1394_CTYPE_CONTROL | AVC1394_SUBUNIT_TYPE_PANEL | \
                        AVC1394_SUBUNIT_ID_0 | AVC1394_PANEL_COMMAND_PASS_THROUGH | \
                        AVC1394_PANEL_OPERAND_PRESS | AVC1394_PANEL_KEYTUNE

#define MOTOSING_CMD0   AVC1394_CTYPE_CONTROL | AVC1394_SUBUNIT_TYPE_PANEL | \
                        AVC1394_SUBUNIT_ID_0 | AVC1394_PANEL_COMMAND_PASS_THROUGH | \
                        AVC1394_CMD_CONNECT_AV

#define SA3250HD_CMD0   AVC1394_CTYPE_CONTROL | AVC1394_SUBUNIT_TYPE_PANEL | \
                        AVC1394_SUBUNIT_ID_0 | AVC1394_PANEL_COMMAND_PASS_THROUGH

#define COMMON_CMD1     ( 0x04 << 24 )
#define COMMON_CMD2     ( 0xff << 24 )

#define SYNC_BYTE       0x47
#define MIN_PACKETS     25
#define MAX_NODATA      10
#define CSR_O_PCR_0     0x904 
#define TUNER           AVC1394_SUBUNIT_TYPE_TUNER
#define PANEL           AVC1394_SUBUNIT_TYPE_PANEL

#define PLUGREPORT_GUID_HI 0x0C
#define PLUGREPORT_GUID_LO 0x10

#if ( __BYTE_ORDER == __BIG_ENDIAN )
struct iec61883_oPCR
{
	unsigned int online:1;
	unsigned int bcast_connection:1;
	unsigned int n_p2p_connections:6;
	unsigned int reserved:2;
	unsigned int channel:6;
	unsigned int data_rate:2;
	unsigned int overhead_id:4;
	unsigned int payload:10;
};
#else
struct iec61883_oPCR
{
	unsigned int payload:10;
	unsigned int overhead_id:4;
	unsigned int data_rate:2;
	unsigned int channel:6;
	unsigned int reserved:2;
	unsigned int n_p2p_connections:6;
	unsigned int bcast_connection:1;
	unsigned int online:1;
};
#endif

struct STBList
{
    int port;
    int node;
    int p2p;
    int changer;
    octlet_t guid;
};    

const char *sa_name[] =
{
    "SA3250HD", "SA4200HD", "SA4250HDC", "SA8300HD"
};
    
const char *moto_name[] =
{
    "DCH-3200", "DCH-3200", "DCT-3412", "DCT-3416",
    "DCT-3416", "DCT-6200", "DCT-6200", "DCT-6212",
    "DCT-6212", "DCT-6216", "DCT-6216", "QIP-6200",
};

const int sa_vendor[] =
{
    0x11e6, 0x14f8, 0x1692, 0x1947,
    0x0f21, 0x1ac3, 0x0a73, 0x1cea,
    0x223a, 0x1e6b,
};
    
const int moto_vendor[] =
{
    0x1c11, 0x1fc4, 0x1bdd, 0x159a,
    0x0ce5, 0x0e5c, 0x1225, 0x0f9f,
    0x1180, 0x12c9, 0x11ae, 0x152f,
    0x14e8, 0x16b5, 0x1371, 0x19a6,
    0x1aad, 0x0b06, 0x195e, 0x0f9f,
    0x152f, 0x17ee, 0x1a66, 0x1e5a,
    0x1106, 0x211e,
};
     
const int sa_model[] = 
{
    0x0be0, 0x1072, 0x10cc, 0x206c
};
    
const int moto_model[] =
{
    0xd330, 0xd330, 0x34cb, 0x346b,
    0xb630, 0x6200, 0x620a, 0x64ca, 
    0x64cb, 0x646a, 0x646b, 0x7100,
};

enum stb_list  
{
    SA3250HD = 1, 
    SA4200HD = 2,
    SA4250HDC = 3,
    SA4250HDC_alt = 4,    
    SA8300HD = 5,
    MotoFast = 6,
    MotoSing = 7,
};

extern 
int iec61883_plug_set(raw1394handle_t handle, nodeid_t node, nodeaddr_t a, quadlet_t value);

extern 
int iec61883_plug_get(raw1394handle_t h, nodeid_t n, nodeaddr_t a, quadlet_t *value);

raw1394handle_t handle;

void ms_sleep(unsigned long millisecs);
void ChStb1(raw1394handle_t handle, nodeid_t node, int stbchn, int changer);


