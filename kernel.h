#pragma once

#include "common.h"

/*
trap_frame 구조체는 kernel_entry에 저장된 프로그램 상태를 나타냄. 
READ_CSR, WRITE_CSR 매크로는 CSR 레지스터를 읽고 쓰기 위한 편리한 매크로임
*/
struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
} __attribute__((packed));

// PAGE TABLE
#define SATP_SV32 (1u << 31) // satp 레지스터 - 페이지 Sv32 모드 활성화
#define PAGE_V (1 << 0) //  Valid bit (entry is enabled)
#define PAGE_R (1 << 1) //  Readable
#define PAGE_W (1 << 2) //  Writable
#define PAGE_X (1 << 3) //  Executable
#define PAGE_U (1 << 4) //  User (accessible in user mode)


/*
CSR(제어 및 상태 레지스터)는 CPU 설정을 저장하는 레지스터, CSR 목록은 RISC-V 권한 사양에서 확인 가능
*/
#define READ_CSR(reg)                                                          \
    ({                                                                         \
        unsigned long __tmp;                                                   \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                  \
        __tmp;                                                                 \
    })

#define WRITE_CSR(reg, value)                                                  \
    do {                                                                       \
        uint32_t __tmp = (value);                                              \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                \
    } while (0)

struct sbiret {
    long error;
    long value;
};

#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)

#define PROCS_MAX 8
#define PROC_UNUSED 0
#define PROC_RUNNABLE 1

/*
커널 스택에는 저장된 CPU 레지스터, 반환 주소(호출된 위치), 로컬 변수가 포함되어 있음
각 프로세스에 대한 커널 스택을 준비하면 CPU 레지스터를 저장 및 복원하고 스택 포인터를 전환하여 컨텍스트 전환을 구현할 수 있음
*/
struct process {
    int pid;             // 프로세스 아이디
    int state;         // 프로세스 상태 
    vaddr_t sp;         // 스택 포인터
    uint32_t *page_table;
    uint8_t stack[8192]; // 커널 스택
};
/*
단일 커널 스택이라는 또다른 접근 방식존재
각 프로세스(스레드)에 커널 스택이 있는 대신 CPU당 스택이 하나만 있음
seL4는 이 방식을 채택. "where to store the program's context" 문제는 Go, Rust와 같은 프로그래밍 언어의 비동기 런타임에서 논의되는 주제
=> stackless async 검색
*/

paddr_t alloc_pages(uint32_t n);
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags);

/*
    The base virtual address of an application image.
    This neds to match the starting address defined in 'user.ld'
*/
#define USER_BASE 0x1000000

/*
애플리케이션을 실행하려면 사용자 모드라고 하는 CPU모드, 즉 RISC-V 용어로는 U-Mode를 사용
U-Mode로 전환하는 방법은 아래와 같음
*/
#define SSTATUS_SPIE (1 << 5)
#define SCAUSE_ECALL 8
#define PROC_EXITED 2

void yield(void);
void switch_context(uint32_t *prev_sp, uint32_t *next_sp);

/*
    Virtio Device는 virtqueue 구조를 가짐 
    -> 드라이버와 장치간 공유되는 대기열

    1. Descriptor Area 
    2. Available Ring
    3. Used Ring

    각 요청(request)는 디스크립터 체인이라고 하는 여러 디스크립터로 구성된다.
    여러 디스크립터로 분할하여 메모리 데이터를 지정하거나 (분산 - 수집 IO)
    다른 디스크립터 속성(장치에서 쓰기 가능 여부)을 부여할 수 있다. 

    ex) 디스크에 기록할 때 virtqueue는 다음과 같이 사용됨
    1. 드라이버가 디스크립터 영역에 읽기/쓰기 요청을 쓴다.
    2. 드라이버가 헤드 설명자의 인덱스를 사용 가능한 링에 추가한다. 
    3. 드라이버가 장치에 새 요청이 있음을 알린다. 
    4. 장치가 사용 가능한 링에서 요청을 읽고 처리한다. 
    5. 장치가 설명자 인덱스를 사용 가능한 링에 쓰고 완료되었음을 드라이버에 알린다.
    virtio spec
    https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html

*/

#define SECTOR_SIZE       512
#define VIRTQ_ENTRY_NUM   16
#define VIRTIO_DEVICE_BLK 2
#define VIRTIO_BLK_PADDR  0x10001000
#define VIRTIO_REG_MAGIC         0x00
#define VIRTIO_REG_VERSION       0x04
#define VIRTIO_REG_DEVICE_ID     0x08
#define VIRTIO_REG_QUEUE_SEL     0x30
#define VIRTIO_REG_QUEUE_NUM_MAX 0x34
#define VIRTIO_REG_QUEUE_NUM     0x38
#define VIRTIO_REG_QUEUE_ALIGN   0x3c
#define VIRTIO_REG_QUEUE_PFN     0x40
#define VIRTIO_REG_QUEUE_READY   0x44
#define VIRTIO_REG_QUEUE_NOTIFY  0x50
#define VIRTIO_REG_DEVICE_STATUS 0x70
#define VIRTIO_REG_DEVICE_CONFIG 0x100
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEAT_OK   8
#define VIRTQ_DESC_F_NEXT          1
#define VIRTQ_DESC_F_WRITE         2
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

// Virtqueue Desciptor area entry.
struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

// Virtqueue Available Ring.
struct virtq_avail{
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));


// Virtqueue Used Ring entry.
struct virtq_used_elem {
    uint32_t id; 
    uint32_t len; 
} __attribute__((packed));

// Virtqueue Used Ring.
struct virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue. 
struct virtio_virtq {
    struct virtq_desc descs[VIRTQ_ENTRY_NUM];
    struct virtq_avail avail;
    struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
    int queue_index;
    volatile uint16_t *used_index;
    uint16_t last_used_index;
} __attribute__((packed));

// Virtio-blk request
/*
    매번 처리가 완료될때까지 바쁘게 기다리기 때문에 링의 처음 3개의 디스크립터를 사용하면된다.
    하지만 실제로는 여러 요청을 동시에 처리하려면 사용 중이거나 사용 중인 디스크립터를 추적해야 한다.
*/
struct virtio_blk_req {
    // First descriptor : read-only from the device
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;

    // Second descriptor: writable by the device if it's a read operation(VIRTQ_DESC_F_WRITE)
    uint8_t data[512];

    // Thrid descriptor: writable by the device (VIRTQ_DESC_F_WRITE)
    uint8_t status;
} __attribute__((packed)); // 컴파일러가 구조체 패딩 없이 패킹하도록 지시


struct virtio_virtq *virtq_init(unsigned index);
void virtq_kick(struct virtio_virtq *vq, int desc_index);
void virtio_blk_init(void);
bool virtq_is_busy(struct virtio_virtq *vq);
void read_write_disk(void *buf, unsigned sector, int is_write);

#define FILES_MAX      2
#define DISK_MAX_SIZE  align_up(sizeof(struct file) * FILES_MAX, SECTOR_SIZE)

/*
    파일 시스템 구현에서 모든 파일은 부팅 시 디스크에서 메모리에 읽혀진다.
    FILES_MAX는 로드할 수 있는 최대 파일 수를 정의하고 DISK_MAX_SIZE는 디스크 이미지 최대 크기를 지정한다.
*/
struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
    char data[]; //Array pointing to the data area following the header
                // flexible array member
} __attribute__((packed));

struct file{
    bool in_use; // Indicates if this file entry is in use 
    char name[100]; // file name
    char data[1024]; //file content 
    size_t size; // file size
};

/*
    RISC-V에서 S-Mode(커널)의 동작은 SUM(감독자 사용자 메모리 접근 허용) 비트를 포함한 sstatus CSR을 통해 구성할 수 있다.
    SUM이 설정되어 있지 않으면 S-Mode 프로그램(커널)은 U-Mode(사용자) 페이지에 접근할 수 없다.
*/
#define SSTATUS_SUM (1 << 18)

struct file *fs_lookup(const char *filename);
void fs_init(void);
void fs_flush(void);