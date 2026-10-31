/* SPDX-License-Identifier: MIT */
/* Offline driver for the copied Gerd FSM. All possible CAN writes/sockets and
 * pacing/monotonic-clock calls are link-wrapped. No CAN interface is opened. */
#define main gb2_recovery_main
#include "../src/gb2-recovery.c"
#undef main
#include <assert.h>

static uint64_t simulated_ms;
static struct can_frame transmitted[4000];
static uint64_t transmitted_at[4000];
static unsigned writes_seen, sockets_seen, checks;
static unsigned fail_write_at=UINT32_MAX;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x); exit(1); } } while (0)

ssize_t __wrap_write(int fd,const void *data,size_t length) {
    CHECK(fd==42 && length==sizeof(struct can_frame));
    if (writes_seen==fail_write_at) { errno=EIO; return -1; }
    CHECK(writes_seen<4000U);
    if (writes_seen) CHECK(simulated_ms>=transmitted_at[writes_seen-1U]+20U);
    memcpy(&transmitted[writes_seen],data,length);
    transmitted_at[writes_seen++]=simulated_ms;
    return (ssize_t)length;
}
int __wrap_socket(int domain,int type,int protocol) {
    (void)domain; (void)type; (void)protocol; ++sockets_seen;
    CHECK(0); return -1;
}
ssize_t __wrap_send(int fd,const void *data,size_t length,int flags) {
    (void)fd; (void)data; (void)length; (void)flags;
    CHECK(0); return -1;
}
int __wrap_clock_gettime(clockid_t clock,struct timespec *value) {
    CHECK(clock==CLOCK_MONOTONIC);
    value->tv_sec=(time_t)(simulated_ms/1000U);
    value->tv_nsec=(long)(simulated_ms%1000U)*1000000L; return 0;
}
int __wrap_nanosleep(const struct timespec *delay,struct timespec *remaining) {
    CHECK(delay->tv_sec==0 && (delay->tv_nsec==20000000L || delay->tv_nsec==500000000L));
    simulated_ms+=(uint64_t)delay->tv_nsec/1000000U;
    if (remaining) memset(remaining,0,sizeof(*remaining));
    return 0;
}
static struct update_config test_config;
static void reset_attempt(void) {
    if (recovery_log) fclose(recovery_log);
    recovery_log=tmpfile(); CHECK(recovery_log!=NULL);
    recovery_state=REC_IDLE; finished=0; interrupted=0;
    execute_recovery=1; bootloader_only=1; can_mode=1; sc=42;
    have_boot_hash=0; boot_hash=0; block_crc=0;
    sent_parts=0; acknowledged_blocks=0; last_bin_block=52; dev_bin_blocks=52;
    writes_seen=0; fail_write_at=UINT32_MAX; simulated_ms=0; phase_deadline=0;
}
static void receive_frame(uint32_t id,unsigned dlc,const uint8_t *payload) {
    unsigned char f[13]={0};
    f[0]=(uint8_t)(id>>24); f[1]=(uint8_t)(id>>16); f[2]=(uint8_t)(id>>8); f[3]=(uint8_t)id;
    f[4]=(uint8_t)dlc; memcpy(f+5,payload,dlc); fsm(f,&test_config);
}
static void normal_ping(void) {
    const uint8_t data[8]={0x47,0x00,0x00,0x01,1,47,0,0x11};
    receive_frame(0x00312345U,8,data);
}
static void boot_identity(void) {
    /* Synthetic identity with the previously observed version/type: 00371234#47000001012F0010. */
    const uint8_t data[8]={0x47,0x00,0x00,0x01,1,47,0,0x10};
    receive_frame(0x00371234U,8,data);
}
static void reply(uint8_t code,uint8_t block) {
    const uint8_t data[8]={0x47,0x00,0x00,0x01,code,block,0,0};
    receive_frame(0x00371234U,code==0x44U ? 6U : 5U,data);
}
static uint16_t independent_crc(const uint8_t *data,size_t length) {
    uint16_t crc=0xffffU;
    for (size_t i=0;i<length;++i) for (unsigned bit=0;bit<8U;++bit) {
        unsigned feedback=((crc>>15)^((data[i]>>(7U-bit))&1U))&1U;
        crc=(uint16_t)(crc<<1);
        if (feedback) crc^=0x1021U;
    }
    return crc;
}
static void check_query(void) {
    CHECK(writes_seen==1U && transmitted[0].can_id==(CAN_EFF_FLAG|0x00364711U));
    CHECK(transmitted[0].can_dlc==0U);
}
static void send_and_check_block(unsigned block) {
    unsigned start=writes_seen;
    size_t offset=(size_t)(block-2U)*512U;
    CHECK(recovery_state==REC_WAIT_SELECT && last_bin_block==(int)block-2);
    CHECK(transmitted[start-1U].can_id==(CAN_EFF_FLAG|0x00364711U));
    CHECK(transmitted[start-1U].can_dlc==6U && transmitted[start-1U].data[4]==0x44U && transmitted[start-1U].data[5]==block);
    reply(0x44U,(uint8_t)block);
    CHECK(recovery_state==REC_WAIT_CRC && writes_seen==start+65U);
    CHECK(last_bin_block==(int)block-2 && sent_parts==64U);
    for (unsigned part=0;part<64U;++part) {
        const struct can_frame *f=&transmitted[start+part];
        CHECK(f->can_id==(CAN_EFF_FLAG|0x00360300U)+part && f->can_dlc==8U);
        CHECK(memcmp(f->data,binfile+offset+part*8U,8U)==0);
    }
    { const struct can_frame *f=&transmitted[start+64U];
      uint16_t crc=independent_crc(binfile+offset,512U);
      CHECK(f->can_id==(CAN_EFF_FLAG|0x00364711U) && f->can_dlc==7U);
      CHECK(memcmp(f->data,recovery_uid,4U)==0 && f->data[4]==0x88U);
      CHECK(f->data[5]==(uint8_t)(crc>>8) && f->data[6]==(uint8_t)crc);
    }
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    { char *dry[]={"gb2-recovery","-i","can0","--bootloader",argv[1],NULL};
      CHECK(gb2_recovery_main(5,dry)==0 && sockets_seen==0U && writes_seen==0U);
      execute_recovery=0; bootloader_only=0; print_version=0; optind=0;
      char *unarmed[]={"gb2-recovery","--execute",argv[1],NULL};
      CHECK(gb2_recovery_main(3,unarmed)==2 && sockets_seen==0U && writes_seen==0U);
    }
    CHECK(device_setup("gb2",&test_config)==1); test_config.filename=argv[1];
    binfile=read_data(&test_config); CHECK(binfile!=NULL);
    CHECK(fsize==27063U && device_fsize==27136U);
    CHECK(CRCCCITT((uint8_t *)"123456789",9U,0xffffU)==0x29b1U);
    for (unsigned i=27063U;i<27136U;++i) CHECK(binfile[i]==0xffU);

    reset_attempt(); execute_recovery=0; normal_ping(); boot_identity();
    CHECK(writes_seen==0U && recovery_state==REC_IDLE);
    CHECK(send_frame(M_BLOCK)==-1 && writes_seen==0U);

    /* Reproduce two normal replies, a repeated boot identity, blocks36/35, F2. */
    reset_attempt(); CHECK(begin_bootloader_recovery()==0); check_query();
    normal_ping(); normal_ping(); CHECK(writes_seen==1U && recovery_state==REC_WAIT_BOOT);
    boot_identity(); CHECK(writes_seen==2U && recovery_state==REC_WAIT_SELECT && boot_hash==0x1234U);
    boot_identity(); normal_ping(); CHECK(writes_seen==2U && last_bin_block==52);
    send_and_check_block(0x36U); CHECK(block_crc==0xab54U);
    reply(0x88U,0); send_and_check_block(0x35U); CHECK(block_crc==0x6a89U);
    reply(0xf2U,0); CHECK(recovery_state==REC_ABORT && finished==-1 && acknowledged_blocks==1U && last_bin_block==51);
    { char log[4096]; size_t n; unsigned sent=writes_seen;
      fflush(recovery_log); rewind(recovery_log); n=fread(log,1,sizeof(log)-1U,recovery_log); log[n]='\0';
      CHECK(strstr(log,"state=WAIT_CRC block=0x35")!=NULL);
      CHECK(strstr(log,"parts=64/64 crc=6A89 acknowledged=1/53")!=NULL);
      CHECK(strstr(log,"RX 00371234#47000001F2 dlc=5")!=NULL);
      puts("Synthetic F2 reproduction:"); fputs(log,stdout);
      boot_identity(); normal_ping(); reply(0x88U,0); CHECK(writes_seen==sent);
    }
    reset_attempt(); CHECK(begin_bootloader_recovery()==0); boot_identity();
    for (unsigned block=0x36U;block>=2U;--block) { send_and_check_block(block); reply(0x88U,0); }
    CHECK(recovery_state==REC_DONE && finished==1 && acknowledged_blocks==53U && writes_seen==3499U);
    { unsigned sent=writes_seen; normal_ping(); boot_identity(); CHECK(writes_seen==sent); }

    reset_attempt(); CHECK(begin_bootloader_recovery()==0); boot_identity(); send_and_check_block(0x36U);
    { const uint8_t other[8]={0x47,0x00,0x00,0x02,0x88,0,0,0}; unsigned sent=writes_seen;
      receive_frame(0x00371234U,5,other);
      CHECK(recovery_state==REC_WAIT_CRC && acknowledged_blocks==0U && writes_seen==sent);
      const uint8_t changed_hash[8]={0x47,0x00,0x00,0x01,0x88,0,0,0};
      receive_frame(0x00371235U,5,changed_hash);
      CHECK(recovery_state==REC_ABORT && acknowledged_blocks==0U && writes_seen==sent);
    }
    reset_attempt(); CHECK(begin_bootloader_recovery()==0);
    CHECK(send_frame(M_GLEISBOX_MAGIC_START_SEQUENCE)==-1 && writes_seen==1U);
    reset_attempt(); CHECK(begin_bootloader_recovery()==0);
    CHECK(send_frame(M_RESET)==-1 && writes_seen==1U);

    reset_attempt(); CHECK(begin_bootloader_recovery()==0);
    { const uint8_t other[8]={0x47,0x00,0x00,0x02,1,47,0,0x10};
      receive_frame(0x00371234U,8,other); CHECK(recovery_state==REC_WAIT_BOOT && writes_seen==1U);
    }
    for (unsigned time=1000U;time<=5000U;time+=1000U) { simulated_ms=time; normal_ping(); }
    CHECK(recovery_state==REC_ABORT && writes_seen==1U);

    reset_attempt(); CHECK(begin_bootloader_recovery()==0); boot_identity();
    simulated_ms=phase_deadline; normal_ping(); CHECK(recovery_state==REC_ABORT && writes_seen==2U);

    reset_attempt(); CHECK(begin_bootloader_recovery()==0); boot_identity(); send_and_check_block(0x36U);
    simulated_ms=phase_deadline; normal_ping(); CHECK(recovery_state==REC_ABORT && acknowledged_blocks==0U);

    reset_attempt(); CHECK(begin_bootloader_recovery()==0); boot_identity(); reply(0x88U,0);
    CHECK(recovery_state==REC_ABORT && writes_seen==2U);
    reset_attempt(); CHECK(begin_bootloader_recovery()==0); boot_identity(); reply(0x44U,0x35U);
    CHECK(recovery_state==REC_ABORT && writes_seen==2U);
    reset_attempt(); CHECK(begin_bootloader_recovery()==0); boot_identity(); fail_write_at=writes_seen;
    reply(0x44U,0x36U); CHECK(recovery_state==REC_ABORT && writes_seen==2U && sent_parts==0U);
    reset_attempt(); CHECK(begin_bootloader_recovery()==0); boot_identity(); interrupted=1; reply(0x44U,0x36U);
    CHECK(recovery_state==REC_ABORT && writes_seen==2U);

    { char name[]="/tmp/gb2-recovery-corrupt-XXXXXX"; int fd=mkstemp(name); FILE *file;
      CHECK(fd>=0); file=fdopen(fd,"wb"); CHECK(file!=NULL);
      binfile[10]^=1U; CHECK(fwrite(binfile,1,27063U,file)==27063U && fclose(file)==0); binfile[10]^=1U;
      test_config.filename=name; CHECK(read_data(&test_config)==NULL); CHECK(unlink(name)==0);
    }
    fclose(recovery_log); recovery_log=NULL; free(binfile); binfile=NULL;
    CHECK(sockets_seen==0U);
    printf("GB2 recovery offline: %u checks passed; double-ping/F2, 53 blocks/CRC, timeouts, ordering, I/O, interruption, corrupt file; ZERO sockets\n",checks);
    return 0;
}
