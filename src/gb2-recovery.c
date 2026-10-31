/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <info@gerhard-bertelsmann.de> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return Gerhard Bertelsmann
 * ----------------------------------------------------------------------------
 */

/* the hard analyzing work was done by Karsten - Thx */
/* Publication copy: fixed SYNTHETIC target UID47000001; replace before real use.
 * Isolated recovery fork: already-active type0010 bootloader.
 * Default: offline preflight. --execute --bootloader -i is required for CAN.
 * No reset, startup magic, automatic retry or application-start frame. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <stdint.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

#define MAXDG   4096		/* maximum datagram size */
#define MAXUDP  16		/* maximum datagram size */
#define TIMEOUT 5		/* wait seconds for response */


/*
 * MS2	MS2	60653	TI/LM	LM3S2414
 * MS2	MS2X	60657	Renesas	R5F562TABDFM
 */

static char *GB2_DEFAULT_FILENAME  = "016-gb2.bin";
static char *MS2_DEFAULT_FILENAME  = "050-ms2.bin";
/* static char *MS2X_DEFAULT_FILENAME = "051-ms2.bin"; */

#define GB2_BLOCK_SHIFT		9	/* 2^9 = 512 */
#define GB2_BLOCK_SIZE		(1<<GB2_BLOCK_SHIFT)
#define GB2_BOOT_BLOCK_SIZE	(1024/GB2_BLOCK_SIZE)
#define GB2_FILL_SBLOCK		(GB2_BLOCK_SIZE-1)
#define GB2_ID			0x47

#define MS2_BLOCK_SHIFT		10	/* 2^10 = 1024 */
#define MS2_BLOCK_SIZE		(1<<MS2_BLOCK_SHIFT)
#define MS2_BOOT_BLOCK_SIZE	(4096/MS2_BLOCK_SIZE)
#define MS2_FILL_SBLOCK		(MS2_BLOCK_SIZE-1)
#define MS2_ID			0x4d

struct update_config {
    unsigned int block_size;
    unsigned int shift;
    unsigned int boot_blocks;
    unsigned int padding;
    unsigned int id;
    char *filename;
};

struct updatefile {
    char *name;
    char *version_name;
    char *filename;
    int version_type;
    int version_storage;
    int blocksize;
    int bootblock;
    int fill_upto;
    uint8_t fill;
    uint32_t size;
    uint32_t rows;
    uint16_t major;
    uint16_t minor;
    uint16_t month;
    uint16_t year;
};

#define ACTUAL	0
#define OLD	1
#define ASCII	2
#define	BLOCK	1024

struct updatefile gb2_update_data[] = {
/*    name, version_name,	filename    , version type, version_storage, blocksize, bootblocks, fill upto,  fill */
    {"gb2s",	   "gb2",	"016-gb2.bin",	ACTUAL,		  6,		 512, 		2,   512,	0xff, 0,  0, 0, 0, 0, 0},
    {0},
};
#define GB2_UPDATE_DATA_SIZE ((sizeof(gb2_update_data) / sizeof(struct updatefile)) - 1)

struct updatefile ms2_update_data[] = {
/*    name, version name,	filename    , version type, version_storage, blocksize, bootblocks, fill upto,  fill */
    {"ms2s",	"",		"050-ms2.bin",	ACTUAL,		252, 		1024, 		4,   1024,	0xff, 0, 0, 0, 0, 0 ,0},
    {"ms2",	"ms2ver",	"050-ms2.bin",	ACTUAL,		252,		1024, 		0,   	8,	0x00, 0, 0, 0, 0, 0, 0},
    {"gb2",	"gb2ver",	"016-gb2.bin",	ACTUAL,		  6,		   0,		0,	8,	0x00, 0, 0, 0, 0, 0, 0},
    {"gfp",	"gfpver",	"000-gfp.bin",	ACTUAL,		  6,		   0,		0,	8,	0x00, 0, 0, 0, 0, 0, 0},
    {"ldb",	"ldbver",	"flashdb.ms2",	OLD,		  0, 		   0,		0,	8,	0x00, 0, 0, 0, 0, 0, 0},
    {"lang",	"langver",	"lang.ms2",	ACTUAL,		  0, 		   0, 		0,	8,	0x00, 0, 0, 0, 0, 0, 0},
    {"mfx",	"mfxver",	"mfxdefs.ms2",	ASCII,		  0, 		   0,		0,	8,	0x00, 0, 0, 0, 0, 0, 0},
    {"mfxb",	"mfxbver",	"mfxdefs.bin",	ACTUAL,		  0, 	 	   0,		0,	8,	0x00, 0, 0, 0, 0, 0, 0},
    {"ms2x",	"ms2xver",	"051-ms2.bin",	ACTUAL,		  0, 	 	   0,		0,	8,	0x00, 0, 0, 0, 0, 0, 0},
    {0},
};

#define MS2_UPDATE_DATA_SIZE ((sizeof(ms2_update_data) / sizeof(struct updatefile)) - 1)

uint16_t CRCCCITT(uint8_t *data, size_t length, uint16_t seed) {
    uint32_t crc = seed;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint32_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = ((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U)) & 0xffffU;
    }
    return (uint16_t)crc;
}

unsigned char netframe[MAXDG];

unsigned char M_GLEISBOX_MAGIC_START_SEQUENCE[]	= { 0x00, 0x36, 0x03, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00 };
unsigned char M_CAN_PING[] 			= { 0x00, 0x30, 0x47, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
unsigned char M_RESET[] 			= { 0x00, 0x00, 0x47, 0x11, 0x06, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x00, 0x00 };
unsigned char M_INIT_BOOTLOADER[] 		= { 0x00, 0x36, 0x47, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
unsigned char M_BLOCK[] 			= { 0x00, 0x36, 0x47, 0x11, 0x06, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00 };
unsigned char M_DATA[] 				= { 0x00, 0x36, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0x00 };
unsigned char M_CRC[] 				= { 0x00, 0x36, 0x47, 0x11, 0x07, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0x00 };

unsigned char M_MS2_MARK_END[] 			= { 0x00, 0x36, 0x47, 0x11, 0x05, 0x00, 0x00, 0x00, 0x00, 0xf5, 0x00, 0x00, 0x00 };
unsigned char M_MS2_SOFT_RESET[] 		= { 0x00, 0x36, 0x47, 0x11, 0x05, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00 };

unsigned char udpframe[MAXDG];

unsigned char *binfile;
unsigned int device_fsize, fsize;
int force = 0, verbose = 0, print_version = 0;
uint16_t device_file_version, version = 0;
unsigned int device_id = 0;
int sc, sb;			/* CAN socket, UDP Broadcast Socket */
int can_mode = 0;
unsigned char lastframe[13];
unsigned char checkframe[13];
unsigned char checkframe_nack[13];
unsigned char checkframe_block_id[13];
int dev_bin_blocks;
int last_bin_block;
int finished;
enum recovery_phase { REC_IDLE, REC_WAIT_BOOT, REC_WAIT_SELECT, REC_SENDING, REC_WAIT_CRC, REC_DONE, REC_ABORT };
static enum recovery_phase recovery_state;
static const uint8_t recovery_uid[4] = {0x47, 0x00, 0x00, 0x01};
static int execute_recovery, bootloader_only, have_boot_hash;
static uint16_t boot_hash, block_crc;
static uint64_t phase_deadline;
static unsigned sent_parts, acknowledged_blocks;
static volatile sig_atomic_t interrupted;
static FILE *recovery_log;
/* Independent Python binascii.crc_hqx over each FF-padded 512-byte block.
 * Detects accidental input corruption; full SHA256 is documented separately. */
static const uint16_t expected_crcs[53] = {
    0x6b94U,0x553aU,0x22b4U,0x5961U,0x3fdcU,0xb716U,0x01e6U,0xc8b0U,0xad98U,
    0xcf12U,0xda68U,0x2c89U,0x01dbU,0xc484U,0xd89aU,0xa96bU,0x10b5U,0xeabaU,
    0xe41fU,0x8ac3U,0xefdbU,0xba79U,0x07f1U,0x54a3U,0xa247U,0xb7d7U,0x7784U,
    0x54ebU,0xd33dU,0x1701U,0x16c1U,0x35aaU,0xd021U,0x1db0U,0x0ccfU,0xaa88U,
    0xd08bU,0xc7c9U,0x6651U,0xe9bdU,0x1a8aU,0xfb2bU,0x39cfU,0xb671U,0x990fU,
    0x8875U,0x8e27U,0xebabU,0x2dd3U,0xc147U,0x0970U,0x6a89U,0xab54U
};

void print_usage(char *prg) {
    fprintf(stderr,"Usage: %s [-i can0] [--bootloader] [--execute] [016-gb2.bin]\n",prg);
    fprintf(stderr,"Fixed recovery target UID47000001; type0010 bootloader, type0011 application.\n");
    fprintf(stderr,"Default: validate the pinned file and print the plan; ZERO sockets/writes.\n");
    fprintf(stderr,"--execute REQUIRES --bootloader and -i; one query, no reset/start/retries.\n");
}

uint16_t be16(uint8_t *u) {
    return (u[0] << 8) | u[1];
}

uint16_t le16(uint8_t *u) {
    return (u[1] << 8) | u[0];
}

uint32_t be32(uint8_t *u) {
    return ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3];
}

static const char *phase_name(void) {
    static const char *names[] = {"IDLE","WAIT_BOOT","WAIT_SELECT","SENDING","WAIT_CRC","DONE","ABORT"};
    return names[recovery_state];
}
static uint64_t recovery_now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0 || t.tv_sec < 0 ||
        (uint64_t)t.tv_sec > UINT64_MAX / 1000U - 1U) return UINT64_MAX;
    return (uint64_t)t.tv_sec * 1000U + (uint64_t)t.tv_nsec / 1000000U;
}
static int recovery_fail(const char *reason, const unsigned char *frame) {
    FILE *log = recovery_log ? recovery_log : stderr;
    fprintf(log,"RECOVERY ABORT: %s state=%s block=0x%02X file_index=%d offset=0x%04X parts=%u/64 crc=%04X acknowledged=%u/53\n",
        reason,phase_name(),last_bin_block+2,last_bin_block,last_bin_block < 0 ? 0U : (unsigned)last_bin_block*512U,
        sent_parts,(unsigned)block_crc,acknowledged_blocks);
    if (frame) {
        uint32_t id = ((uint32_t)frame[0]<<24)|((uint32_t)frame[1]<<16)|((uint32_t)frame[2]<<8)|frame[3];
        fprintf(log,"RX %08X#",id);
        for (unsigned i=0;i<frame[4] && i<8U;++i) fprintf(log,"%02X",frame[5+i]);
        fprintf(log," dlc=%u wire=",(unsigned)frame[4]);
        for (unsigned i=0;i<13U;++i) fprintf(log,"%02X",frame[i]);
        fputc('\n',log);
    }
    fflush(log); recovery_state=REC_ABORT; finished=-1; return -1;
}
static int set_deadline(void) {
    uint64_t now = recovery_now();
    if (now > UINT64_MAX-5000U) return recovery_fail("clock unavailable/overflow",NULL);
    phase_deadline=now+5000U; return 0;
}
static int check_deadline(void) {
    if (recovery_state==REC_IDLE || recovery_state==REC_DONE || recovery_state==REC_ABORT) return 0;
    if (interrupted) return recovery_fail("interrupted; no retry",NULL);
    if (recovery_now()>=phase_deadline) return recovery_fail("absolute phase timeout; no retry",NULL);
    return 0;
}
static int recovery_tx_allowed(unsigned char *f) {
    uint32_t id=be32(f);
    if (!execute_recovery || interrupted || f[4]>8U) return 0;
    if (id==0x00364711U) {
        if (f[4]==0U) return recovery_state==REC_WAIT_BOOT;
        if (memcmp(f+5,recovery_uid,4)!=0) return 0;
        if (f[4]==6U && f[9]==0x44U && recovery_state==REC_WAIT_SELECT)
            return last_bin_block>=0 && last_bin_block<=52 && f[10]==last_bin_block+2;
        return f[4]==7U && f[9]==0x88U && recovery_state==REC_SENDING && sent_parts==64U && be16(f+10)==block_crc;
    }
    return recovery_state==REC_SENDING && sent_parts<64U && f[4]==8U && id==0x00360300U+sent_parts;
}
static void recovery_interrupt(int signal_number) { (void)signal_number; interrupted=1; }

int time_stamp(char *timestamp) {
    struct timeval tv;
    struct tm *tm;

    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);

    sprintf(timestamp, "%02d:%02d:%02d.%03d", tm->tm_hour, tm->tm_min, tm->tm_sec, (int)tv.tv_usec / 1000);
    return 0;
}

void print_can_frame(char *format_string, unsigned char *netframe, int verbose) {
    int i, dlc;
    char timestamp[16];

    if (!verbose)
	return;

    dlc = netframe[4];
    time_stamp(timestamp);
    printf("%s   ", timestamp);
    printf(format_string, be32(netframe) & CAN_EFF_MASK, netframe[4]);
    for (i = 5; i < 5 + dlc; i++) {
	printf(" %02x", netframe[i]);
    }
    if (dlc < 8) {
	printf("(%02x", netframe[i]);
	for (i = 6 + dlc; i < 13; i++) {
	    printf(" %02x", netframe[i]);
	}
	printf(")");
    } else {
	printf(" ");
    }
    printf("  ");
    for (i = 5; i < 13; i++) {
	if (isprint(netframe[i]))
	    printf("%c", netframe[i]);
	else
	    putchar(46);
    }
    printf("\n");
}

void format_can_to_netframe(struct can_frame *frame, unsigned char *netframe) {
    uint32_t canid;

    memset(netframe,0,13);
    frame->can_id &= CAN_EFF_MASK;
    canid = htonl(frame->can_id);
    memcpy(netframe, &canid, 4);
    netframe[4] = frame->can_dlc;
    memcpy(&netframe[5], &frame->data, frame->can_dlc);
}

int netframe_to_net(int net_socket, unsigned char *netframe, int length) {
    if (send(net_socket, netframe, length, 0) != length) {
	fprintf(stderr, "couldn't send on UDP socket: %s\n", strerror(errno));
	return -1;
    }
    return 0;
}

int netframe_to_can(int can_socket, unsigned char *netframe) {
    struct can_frame frame;
    struct timespec to_wait;

    if (!recovery_tx_allowed(netframe)) return recovery_fail("TX gate refused frame",netframe);
    memset(&frame, 0, sizeof(frame));
    frame.can_id = be32(netframe);
    frame.can_id &= CAN_EFF_MASK;
    frame.can_id |= CAN_EFF_FLAG;
    frame.can_dlc = netframe[4];
    memcpy(&frame.data, &netframe[5], 8);

    /* send CAN frame */
    if (write(can_socket, &frame, sizeof(frame)) != sizeof(frame)) {
	fprintf(stderr, "%s: error writing CAN frame: %s\n", __func__, strerror(errno));
	return -1;
    }
    /* Preserve Gerd's conservative Gleisbox pacing: 20ms AFTER every write. */
    to_wait.tv_sec = 0;
    to_wait.tv_nsec = 20 * 1000000;
    while (nanosleep(&to_wait, &to_wait) != 0) {
        if (errno!=EINTR || interrupted) return -1;
    }
    if (interrupted) return -1;
    return 0;
}

int send_frame(unsigned char *netframe) {
    if (!can_mode || !execute_recovery) return recovery_fail("recovery is not armed for SocketCAN",netframe);
    if (netframe_to_can(sc,netframe)!=0)
        return recovery_state==REC_ABORT ? -1 : recovery_fail("CAN write/pacing failed; no retry",netframe);
    return 0;
}

static int begin_bootloader_recovery(void) {
    if (!execute_recovery || !bootloader_only || recovery_state!=REC_IDLE)
        return recovery_fail("explicit bootloader recovery required; one session only",NULL);
    memcpy(&device_id,recovery_uid,4); last_bin_block=dev_bin_blocks;
    recovery_state=REC_WAIT_BOOT;
    if (set_deadline()!=0) return -1;
    return send_frame(M_INIT_BOOTLOADER);
}

unsigned char *read_data(struct update_config *device_config) {
    FILE *fp;
    unsigned char *data;

    fp = fopen(device_config->filename, "rb");
    if (fp == NULL) {
	fprintf(stderr, "%s: error fopen failed [%s]\n", __func__, device_config->filename);
	return NULL;
    }

    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize!=27063U || device_config->id!=GB2_ID || device_config->block_size!=512U || device_config->boot_blocks!=2U) {
        fprintf(stderr,"Only the pinned 27063-byte 016-gb2.bin/GB2 profile is supported\n"); fclose(fp); return NULL;
    }
    /* prepare padding */
    device_fsize = (fsize + device_config->padding) & (0xFFFFFFFFUL - device_config->padding);

    if ((data = malloc(device_fsize)) == NULL) {
	fprintf(stderr, "%s: can't alloc %u bytes for data\n", __func__, fsize);
	fclose(fp);
	return NULL;
    }

    /* padding with 0xff */
    memset(data,0xff,device_fsize);
    if ((fread((void *)data, 1, fsize, fp)) != (unsigned int)fsize) {
	fprintf(stderr, "%s: error: fread failed for [%s]\n", __func__, device_config->filename);
	fclose(fp);
	free(data);
	return NULL;
    }

    fclose(fp);
    if (data[6]!=1U || data[7]!=70U || data[8]!=0U || data[9]!=0x10U) { free(data); return NULL; }
    for (unsigned block=0;block<53U;++block) {
        if (CRCCCITT(data+block*512U,512U,0xffffU)!=expected_crcs[block]) {
            fprintf(stderr,"Pinned firmware CRC mismatch at file block %u\n",block); free(data); return NULL;
        }
    }
    if (device_config->id == GB2_ID) {
	memcpy(&device_file_version, &data[6], 2);
	printf("[%s] Device File Version %u.%u\n", device_config->filename, data[6], data[7]);
    } else if (device_config->id == MS2_ID) {
	memcpy(&device_file_version, &data[252], 2);
	printf("[%s] Device File Version %u.%u\n", device_config->filename, data[252], data[253]);
    }
    return data;
}

int print_versions(struct update_config *device_config) {
    unsigned int i, read_data_size;
    FILE *fp;
    uint8_t *data;

    if ((data = malloc(BLOCK)) == NULL) {
        fprintf(stderr, "%s: can't alloc %d bytes for data\n", __func__, BLOCK);
        return EXIT_FAILURE;
    }

    if (device_config->id == GB2_ID) {
    } else if (device_config->id == MS2_ID) {
	for (i = 0; i < MS2_UPDATE_DATA_SIZE; ++i) {
	    fp = fopen(ms2_update_data[i].filename, "rb");
	    if (fp == NULL) {
		fprintf(stderr, "%s: error opening file [%s]\n", __func__, ms2_update_data[i].filename);
		free(data);
		return EXIT_FAILURE;
	    }
	    fseek(fp, 0, SEEK_END);
	    fsize = ftell(fp);
	    fseek(fp, 0, SEEK_SET);
	    read_data_size = fsize > BLOCK ? BLOCK : fsize;
	    if ((fread((void *)data, 1, read_data_size, fp)) != read_data_size) {
		fprintf(stderr, "%s: error: fread failed for [%s]\n", __func__, ms2_update_data[i].filename);
		fclose(fp);
		free(data);
		return EXIT_FAILURE;
	    }
	    if (ms2_update_data[i].version_type == ACTUAL) {
		ms2_update_data[i].major = data[ms2_update_data[i].version_storage];
		ms2_update_data[i].minor = data[ms2_update_data[i].version_storage + 1];
		ms2_update_data[i].size = fsize;
		printf("[%s] Device File Version %u.%u size %u\n", ms2_update_data[i].filename, ms2_update_data[i].major, ms2_update_data[i].minor, fsize);
	    } else if (ms2_update_data[i].version_type == OLD) {
		ms2_update_data[i].major = le16(&data[ms2_update_data[i].version_storage]);
		ms2_update_data[i].month = le16(&data[ms2_update_data[i].version_storage + 2]);
		ms2_update_data[i].year = le16(&data[ms2_update_data[i].version_storage + 4]);
		ms2_update_data[i].rows = le16(&data[ms2_update_data[i].version_storage + 6]);
		ms2_update_data[i].size = fsize;
		printf("[%s] Device File Version %u %u/%u size %u\n", ms2_update_data[i].filename, ms2_update_data[i].major, ms2_update_data[i].month, ms2_update_data[i].year, fsize);
	    } else if (ms2_update_data[i].version_type == ASCII) {
		printf("[%s] Device File Version TODO size %u\n", ms2_update_data[i].filename, fsize);
	    }
	    fclose(fp);
	}
    }
    printf("\n");
    free(data);
    return EXIT_SUCCESS;
}

/*
 .version=14
 .monat=5
 .jahr=2014
 .anzahl=1143
*/

/*
 .vhigh=2
 .vlow=7
 .bytes=84192
*/

int sprint_version(struct updatefile *udata, char *s) {
    if (udata->version_type == OLD)
	sprintf(s, " .version=%u\n .monat=%u\n .jahr=%u\n .anzahl=%u\n", udata->major, udata->month, udata->year, udata->rows);
    else if (udata->version_type == ACTUAL)
	sprintf(s, " .vhigh=%u\n .vlow=%u\n .bytes=%u\n", udata->major, udata->minor, udata->size);
    return strlen(s);
}

int send_next_block_id(int block, unsigned char *netframe) {
    if (last_bin_block<0 || last_bin_block>52 || block!=last_bin_block+2)
        return recovery_fail("invalid block selector",NULL);
    recovery_state=REC_WAIT_SELECT; sent_parts=0; block_crc=0;
    if (set_deadline()!=0) return -1;
    memcpy(netframe,M_BLOCK,13); memcpy(netframe+5,recovery_uid,4); netframe[10]=(uint8_t)block;
    return send_frame(netframe);
}

int device_setup(char *device, struct update_config *device_config) {
    int ret = -1;
    if (strncmp(device, "gb2", 3) == 0) {
	device_config->block_size = GB2_BLOCK_SIZE;
	device_config->shift = GB2_BLOCK_SHIFT;
	device_config->boot_blocks = GB2_BOOT_BLOCK_SIZE;
	device_config->padding = GB2_FILL_SBLOCK;
	device_config->filename = GB2_DEFAULT_FILENAME;
	device_config->id = GB2_ID;
	ret = 1;
    } else if (strncmp(device, "ms2", 3) == 0) {
	device_config->block_size = MS2_BLOCK_SIZE;
	device_config->shift = MS2_BLOCK_SHIFT;
	device_config->boot_blocks = MS2_BOOT_BLOCK_SIZE;
	device_config->padding = MS2_FILL_SBLOCK;
	device_config->filename = MS2_DEFAULT_FILENAME;
	device_config->id = MS2_ID;
	ret = 2;
    }
    return ret;
}

int send_block(unsigned char *bytes, int length, unsigned char *netframe) {
    if (recovery_state!=REC_WAIT_SELECT || length!=512 || last_bin_block<0 || last_bin_block>52 ||
        bytes!=binfile+(unsigned)last_bin_block*512U)
        return recovery_fail("data without matching selector ACK",NULL);
    recovery_state=REC_SENDING;
    if (set_deadline()!=0) return -1;
    block_crc=CRCCCITT(bytes,512U,0xffffU);
    for (int i=0;i<length;i+=8) {
        if (check_deadline()!=0) return -1;
        memcpy(netframe,M_DATA,13); netframe[3]=(uint8_t)sent_parts;
        memcpy(netframe+5,bytes+i,8);
        if (send_frame(netframe)!=0) return -1;
        ++sent_parts;
    }
    if (check_deadline()!=0) return -1;
    memcpy(netframe,M_CRC,13); memcpy(netframe+5,recovery_uid,4);
    netframe[10]=(uint8_t)(block_crc>>8); netframe[11]=(uint8_t)block_crc;
    if (send_frame(netframe)!=0) return -1;
    recovery_state=REC_WAIT_CRC;
    return set_deadline();
}

void fsm(unsigned char *frame, struct update_config *config) {
    uint32_t command=be32(frame)&0x1fff0000U;
    unsigned hash=be32(frame)&0xffffU;
    if (!execute_recovery || recovery_state==REC_DONE || recovery_state==REC_ABORT) return;
    if (check_deadline()!=0) return;
    if (frame[4]>8U) { (void)recovery_fail("invalid DLC",frame); return; }
    if (command!=0x00310000U && command!=0x00370000U) return;
    if (frame[4]<4U || memcmp(frame+5,recovery_uid,4)!=0) return;
    /* A normal ping NEVER resets or reinitializes this boot-only attempt. */
    if (command==0x00310000U) return;
    if (frame[4]>=5U && frame[9]>=0xf1U && frame[9]<=0xf6U) {
        (void)recovery_fail("target bootloader NACK; no retry",frame); return;
    }
    if (have_boot_hash && hash!=boot_hash) {
        (void)recovery_fail("target bootloader hash changed",frame); return;
    }
    if (frame[4]==8U) {
        if (frame[11]!=0U || frame[12]!=0x10U) {
            (void)recovery_fail("target bootloader type must be0010",frame); return;
        }
        if (recovery_state!=REC_WAIT_BOOT) return; /* Duplicate identity: no restart. */
        boot_hash=(uint16_t)hash; have_boot_hash=1;
        fprintf(recovery_log ? recovery_log : stderr,"Start recovery transfer UID47000001 boot_hash=%04X version=%u.%u blocks=0x36..0x02\n",
            hash,(unsigned)frame[9],(unsigned)frame[10]);
        { struct timespec delay={0,500000000L};
          while (nanosleep(&delay,&delay)!=0) {
              if (errno!=EINTR || interrupted) { (void)recovery_fail("boot readiness wait interrupted",NULL); return; }
          }
        }
        if (check_deadline()!=0) return;
        (void)send_next_block_id(last_bin_block+(int)config->boot_blocks,lastframe);
        return;
    }
    if (recovery_state==REC_WAIT_SELECT && frame[4]==6U && frame[9]==0x44U) {
        if (frame[10]!=last_bin_block+2) { (void)recovery_fail("selector ACK for wrong block",frame); return; }
        fprintf(recovery_log ? recovery_log : stderr,"Send block=0x%02X offset=0x%04X length=512\n",last_bin_block+2,(unsigned)last_bin_block*512U);
        (void)send_block(binfile+(unsigned)last_bin_block*512U,512,lastframe);
        return;
    }
    if (recovery_state==REC_WAIT_CRC && frame[4]==5U && frame[9]==0x88U && sent_parts==64U) {
        ++acknowledged_blocks; --last_bin_block;
        if (last_bin_block>=0) (void)send_next_block_id(last_bin_block+2,lastframe);
        else { recovery_state=REC_DONE; finished=1; }
        return;
    }
    (void)recovery_fail("unexpected/premature/duplicate ACK",frame);
}

int main(int argc, char **argv) {
    struct update_config config;
    struct sockaddr_can address;
    struct ifreq interface;
    struct can_filter filters[2]={{CAN_EFF_FLAG|0x00310000U,0xdfff0000U},{CAN_EFF_FLAG|0x00370000U,0xdfff0000U}};
    can_err_mask_t errors=CAN_ERR_MASK;
    const char *interface_name=NULL;
    int opt,zero=0,exit_status=1;
    static const struct option options[]={
        {"interface",required_argument,NULL,'i'}, {"bootloader",no_argument,NULL,'B'},
        {"execute",no_argument,NULL,'x'}, {"help",no_argument,NULL,'h'}, {NULL,0,NULL,0}
    };
    (void)device_setup("gb2",&config); /* No basename-selected MS2 mode. */
    while ((opt=getopt_long(argc,argv,"i:Bxpvh",options,NULL))!=-1) {
        switch (opt) {
        case 'i': interface_name=optarg; break;
        case 'B': bootloader_only=1; break;
        case 'x': execute_recovery=1; break;
        case 'p': print_version=1; break;
        case 'v': verbose=1; break;
        case 'h': print_usage(argv[0]); return 0;
        default: print_usage(argv[0]); return 2;
        }
    }
    if (optind<argc) config.filename=argv[optind++];
    if (optind!=argc || (execute_recovery && (!bootloader_only || interface_name==NULL))) {
        print_usage(argv[0]); return 2;
    }
    binfile=read_data(&config);
    if (binfile==NULL) return 1;
    dev_bin_blocks=(int)((device_fsize-1U)>>config.shift);
    printf("UID47000001 only: bytes=%u padded=%u blocks=53 wire=0x36..0x02 CRC16/CCITT seedFFFF paddingFF\n",fsize,device_fsize);
    if (!execute_recovery || print_version) {
        printf("DRY-RUN: no CAN socket, no query, no reset or flash. Activation requires --execute --bootloader -i can0.\n");
        free(binfile); binfile=NULL; return 0;
    }
    if (strlen(interface_name)>=sizeof(interface.ifr_name)) { free(binfile); return 2; }
    memset(&interface,0,sizeof(interface)); strcpy(interface.ifr_name,interface_name);
    memset(&address,0,sizeof(address));
    if (signal(SIGINT,recovery_interrupt)==SIG_ERR || signal(SIGTERM,recovery_interrupt)==SIG_ERR) { free(binfile); return 1; }
    can_mode=1;
    sc=socket(PF_CAN,SOCK_RAW|SOCK_NONBLOCK|SOCK_CLOEXEC,CAN_RAW);
    if (sc<0) { perror("CAN socket"); free(binfile); return 1; }
    if (ioctl(sc,SIOCGIFINDEX,&interface)<0 ||
        setsockopt(sc,SOL_CAN_RAW,CAN_RAW_RECV_OWN_MSGS,&zero,sizeof(zero))!=0 ||
        setsockopt(sc,SOL_CAN_RAW,CAN_RAW_FILTER,filters,sizeof(filters))!=0 ||
        setsockopt(sc,SOL_CAN_RAW,CAN_RAW_ERR_FILTER,&errors,sizeof(errors))!=0) {
        perror("CAN setup"); goto done;
    }
    address.can_family=AF_CAN; address.can_ifindex=interface.ifr_ifindex;
    if (bind(sc,(struct sockaddr *)&address,sizeof(address))!=0) { perror("CAN bind"); goto done; }
    /* No magic-start11 and no normal ping/reset. Query existing bootloader once. */
    if (begin_bootloader_recovery()!=0) goto done;
    while (!finished) {
        struct timeval timeout;
        struct can_frame frame;
        fd_set readers;
        uint64_t remaining;
        ssize_t count;
        int ready;
        if (check_deadline()!=0) break;
        remaining=phase_deadline-recovery_now();
        if (remaining>5000U) { (void)recovery_fail("clock/deadline changed",NULL); break; }
        timeout.tv_sec=(time_t)(remaining/1000U); timeout.tv_usec=(suseconds_t)((remaining%1000U)*1000U);
        FD_ZERO(&readers); FD_SET(sc,&readers);
        ready=select(sc+1,&readers,NULL,NULL,&timeout);
        if (check_deadline()!=0) break;
        if (ready<0) { if (errno==EINTR) continue; (void)recovery_fail("CAN wait failed",NULL); break; }
        if (ready==0) continue;
        count=read(sc,&frame,sizeof(frame));
        if (count<0 && (errno==EAGAIN || errno==EINTR)) continue;
        if (count!=(ssize_t)sizeof(frame) || frame.can_dlc>8U || (frame.can_id&CAN_ERR_FLAG)) {
            (void)recovery_fail("CAN read/error frame",NULL); break;
        }
        if (!(frame.can_id&CAN_EFF_FLAG) || (frame.can_id&CAN_RTR_FLAG)) continue;
        format_can_to_netframe(&frame,netframe); fsm(netframe,&config);
    }
    if (finished==1) {
        printf("All53 blocks acknowledged; no reset/start sent. Application1.70/flash commit NOT independently verified.\n");
        exit_status=0;
    }
 done:
    close(sc); free(binfile); binfile=NULL;
    return exit_status;
}
