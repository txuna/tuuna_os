#include "kernel.h"
#include "common.h"
typedef unsigned char uint8_t;
typedef unsigned int uint32_t; 
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[];
extern char __free_ram[], __free_ram_end[];
/*
    커널 페이지는 __kernel_base에서 __free_ram_end에 걸쳐있음
    이 접근 방식은 커널이 정적으로 할당된 영역(.text)과 동적으로 할당된 영역 모두에 항상 액세스할 수 있도록 보장
    alloc_pages로 관리하는 동적 할당 영역
*/
extern char __kernel_base[]; 

extern char _binary_shell_bin_start[], _binary_shell_bin_size[];

struct process *current_proc; // Current running process
struct process *idle_proc; // Idle process (실행 가능한 프로세스가 없을 때 실행할 유휴 프로세스)


/*
모든 SBI 함수는 하나의 바이너리 인코딩을 공유하므로 SBI 확장을 쉽게 혼합할 수 있다. 
SBI 사양은 아래의 호출 규칙을 따른다.

ECALL은 컨트롤 트랜스퍼 명령어로 쓰이며 supervisor와 SEE사이에서 쓰임
a7은 SBI extension ID (EID)
a6는 SBI v0.2 이후에 정의된 모든 SBI 확장에 대해 a7로 인코딩된 주어진 확장 ID에 대한 SBI 기능 ID(FID)를 인코딩

https://tools.cloudbear.ru/docs/riscv-sbi-2.0-20231006.pdf- Chapter 3
SBI 함수는 a0, a1 페어값을 반환한다. a0은 에러코드
*/
struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid){
    /* 컴파이러에 지정된 레지스터에 값을 배치하도록 요청 */
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3; 
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    /* 
        ecall 명령어 실행
        CPU 실행 모드가 커널모드(S-MODE)에서 OpenSBI모드(M-MODE)로 전환됨.
        OpenSBI의 처리 핸들러가 호출됨. 이 작업이 완료되면 다시 커널모드로 전환되고 ecall 명령어 이후 실행이 재개
     */

    /* ecall 명령은 어플리케이션이 커널을 호출할 때(syscall) 사용되기도 함. 이 명령어는 더 높은 권한의 CPU 모드로의 함수 호출처럼 동작 */
    __asm__ __volatile__("ecall"
                        : "=r"(a0), "=r"(a1)
                        : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                        "r"(a6), "r"(a7)
                        : "memory");
    return (struct sbiret){.error = a0, .value = a1};
}
/*
    long sbi_console_putchar(int ch)
    ch에 있는 데이터를 콘솔 디버깅에 쓴다. sbi_console_getchar()와 달리 이 SBI 호출은 전송할 대기 중인 문자가 남아 있거나 수신 터미널이 아직 바이트를 수신할 준비가 되지 않은 경우 차단된다.
    그러나 콘솔이 전혀 존재하지 않으면 문자가 버려진다. 
    이 SBI 호출은 성공시 0을 반환하거나 구현에 따라 에러 코드를 반환
    -> 디버그 콘솔에 전송
*/
void putchar(char ch){
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

/*
    "input to the debug console"로부터 값을 읽는다. 입력이 없으면 -1을 리턴한다.
    엄밀히 말하면 SBI는 키보드에서 문자를 읽는 것이 아니라 직렬포트에서 문자를 익는다. 
    키보드 또는(QEMU의 표준 입력)가 직렬 포트와 연결되어 있기때문에 동작한다.
*/
int getchar(void){
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}

/*
Context Switching. 
이는 프로세스간 실행 컨텍스를 스위칭

호출자가 저장한 레지스터를 스택에 저장하고 스택 포인터를 전환한 다음 호출자가 저장한 레지스터를 스택에서 복원한다. 

피호출자의 저장된 레지스터는 호출된 함수가 반환하기 전에 복원해야 하는 레지스터임
RISC-V에서 s0 ~ s11은 호출자 저장 레지스터이다.
a0과 같은 다른 레지스터는 호출자 저장 레지스터로, 호출자가 스택에 이미 저장해 둔다.
https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf
*/
__attribute__((naked)) void switch_context(uint32_t *prev_sp, uint32_t *next_sp){
    __asm__ __volatile__(
        "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers
        "sw ra, 0 * 4(sp)\n" // 피호출자의 레지스터를 저장한다.
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"
        "sw sp, (a0)\n" // *prev_sp = sp;
        "lw sp, (a1)\n" // switch stack pointer (sp) here
        "lw ra,  0  * 4(sp)\n"  // Restore callee-saved registers only
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n" // 13 * 4 바이트 스택에서 정리
        "ret\n" // 여기서 ret가 없어지면 ? 어케?
    );
}

struct process procs[PROCS_MAX]; //All process control structures

__attribute__((naked)) void user_entry(void){
    __asm__ __volatile__(
        "csrw sepc, %[sepc] \n" /* sepc 레지스터에서 U-Mode로 전환할 때의 프로그램 카운터를 설정, sret가 점프하는 위치 */
        // 현재 구현에서는 하드웨어 인터럽트를 사용하지 않고 폴링을 사용하므로 SPIE 비트를 설정할 필요는 없음
        "csrw sstatus, %[sstatus]\n" /* sstatus 레지스터에서 SPIE 비트를 설정 -> U-Mode에 진입할 때 하드웨어 인터럽트가 활성화되고 stvec 레지스터에 설정된 핸들러가 호출*/
        "sret \n" /* S-Mode에서 U-Mode로의 전환은 sret 명령으로 수해욈*/
        :
        :   [sepc] "r" (USER_BASE),
            [sstatus] "r" (SSTATUS_SPIE)
    );
}

/*
    MMIO 레지스터에 엑세스하는 것은 일반 메모리에 엑세스하는 것과는 다름
    컴파일러가 읽기/쓰기 작업을 최적화하지 못하도록 휘발성 키워드를 사용 
    MMIO에서 메모리 엑세스 부작용(장치에 명령 전송)을 유발할 수 있음
*/
uint32_t virtio_reg_read32(unsigned offset){
    return *((volatile uint32_t *) (VIRTIO_BLK_PADDR + offset));
}

uint64_t virtio_reg_read64(unsigned offset){
    return *((volatile uint64_t *) (VIRTIO_BLK_PADDR + offset));
}

void virtio_reg_write32(unsigned offset, uint32_t value){
    *((volatile uint32_t *) (VIRTIO_BLK_PADDR + offset)) = value;
}

void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value) {
    virtio_reg_write32(offset, virtio_reg_read32(offset) | value);
}

/*
    Virtio device 초기화
    https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-910003

    3.1.1 드라이버 요구 사항 : 장치 초기화 드라이버는 장치를 초기화하려는 다음 순서를 따라야 한다.
    1. 디바이스 리셋
    2. 게스트 OS가 장치를 인식했음을 알리는 ACKNOWLEDGE 상태 비트를 설정한다.
    3 드라이버 상태 비트 설정: 게스트 OS가 장치를 구동하는 방법을 알고 있다. 
    4. 디바이스 기능 비트를 읽고 OS와 드라이버가 이해하는 기능 비트의 하위 집합을 디바이스에 쓴다.
    이단계에서 드라이버는 디바이스를 수락하기 전에 디바이스를 지원할 수 있는지 확인하기 위해 디바이스별 구성 필드를 읽을 수 있지만 쓰면 안된다.

    5. FEATURES_OK 상태 비트를 설정한다. 이 단계 이후에는 드라이버가 새 feature 비트를 수락하지 않아야 한다.
    6. 장치 상태를 다시 읽어 FEATURES_OK 비트가 여전히 설정되어 있는지 확인한다. 
    그렇지 않으면 장치가 하위 기능 집합을 지원하지 않으므로 장치를 사용할 수 없다.

    7. 디바이스에 대한 버츄어 검색, 버스별 설정(선택 사항), 디바이스의 virtio 구성 공간 읽기 및 쓰기, virtqueue 등 디바이스별 설정 수행
    8. DRIVER_OK 비트를 설정 한다. 디비아시그ㅏ "LIVE"
*/
struct virtio_virtq *blk_request_vq;
struct virtio_blk_req *blk_req;
paddr_t blk_req_paddr;
unsigned blk_capacity;

void virtio_blk_init(void){
    if(virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976){
        PANIC("virtio: invalid magic value");
    }

    if(virtio_reg_read32(VIRTIO_REG_VERSION) != 1){
        PANIC("virtio: invalid version");
    }

    if(virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK){
        PANIC("virtio: invalid device id");
    }

    // 1. reset the device
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    
    // 2. Set the ACKNOWLEDGE status bit, Guest OS has noticed the device
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);

    // 3. Set the Driver status bit
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    
    // 5. Set the FEATURE_OK status bit
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FEAT_OK);

    // 7. Perform device-specific setup, including discovery of virqueues for the device
    blk_request_vq = virtq_init(0);

    // 8. Set the DRIVER_OK status bit
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // Get the disk capacity
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %d bytes\n", blk_capacity);

    // Allocate a region to store requests to the device
    blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = (struct virtio_blk_req *)blk_req_paddr;
}

/*
    Virtqueue 초기화

    1. QueueSel에서 index(첫 번째 큐는 0)를 쓰는 큐를 선택한다.
    2. 큐가 아직 사용중인지 확인: 반환값이 0일 것으로 예상하여 QueuePFN을 읽는다. 
    3. QueueNumMax에서 최대 대기열 크기(요소 수)를 읽는다. 반환된 값이 0이면 큐를 사용할 수 없는 것
    4. 인접한 가상 메모리에 큐 페이지를 할당하고 영점화하여 Used Ring을 Align(페이지 크기)한다.
    드라이버는 QueueNumMax보다 작거나 같은 큐 크기를 선택해야 한다.
    5. QueueNum에 큐 크기를 기록하여 큐 크기를 알린다.
    6. 사용된 정렬에 대한 값을 바이트 단위로 QueueAlign에 기록하여 장치에 알린다.
    7. 큐의 첫 번째 페이지의 실제 번호를 QueuePFN 레지스터에 기록한다.
*/

// This function allocates a memory region for a virtqueue and tells the its physical address to the device.
// The device will use this memory region to read/write requests 

// 초기화 프로세스에서 드라이버가 하는 일은 디바이스 capabilities/features를 확인하고 OS 리소스(ex. 메모리영역)을 할당하고, 매개변수를 설정한다.
struct virtio_virtq *virtq_init(unsigned index){
    // Allocate a region for the virtqueue.
    paddr_t virtq_paddr = alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *)virtq_paddr;
    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    
    // 1. Select the queue writing its index (first queue is 0) To QueueSel.
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    // 5. Notify the device about the queue size by writing the size to QueueNum.
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // 6. Notify the device about the used alignment by writing its valye in bytes
    virtio_reg_write32(VIRTIO_REG_QUEUE_ALIGN, 0);
    // 7. Write the physical number of the first page of the queue to the QueuePFN
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr);
    return vq;
}

// Notifies the device that there is a new request. 'desc_index' is the index
// of the head descriptor of the new request
void virtq_kick(struct virtio_virtq *vq, int desc_index){
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    vq->avail.index++;
    __sync_synchronize();
    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    vq->last_used_index++;
}

// Return whether there are requests being processed by the device
bool virtq_is_busy(struct virtio_virtq *vq){
    return vq->last_used_index != *vq->used_index;
}

// Reads/Writes from/to virtio-blk device
void read_write_disk(void *buf, unsigned sector, int is_write){
    if(sector >= blk_capacity / SECTOR_SIZE){
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n", 
                sector, blk_capacity / SECTOR_SIZE);
        return;
    }

    // Construct the request according to the virtio-blk spec
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if(is_write){
        memcpy(blk_req->data, buf, SECTOR_SIZE);
    }

    // Construct the virtqueue descriptors (using 3 descriptors).
    struct virtio_virtq *vq = blk_request_vq;
    vq->descs[0].addr = blk_req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

    // Notify the device that there is a new request
    virtq_kick(vq, 0);

    // Wait until the device finished processing.
    while(virtq_is_busy(vq)){
    }

    // virtio-blk: If a non-zero value is returned, it's an error
    if(blk_req->status != 0){
        printf("virtio: warn: failed to read/write sector=%d status=%d\n", sector, blk_req->status);
        return;
    }

    // For read operations, copy the data into the buffer
    if(!is_write){
        memcpy(buf, blk_req->data, SECTOR_SIZE);
    }
}


struct process *create_process(const void *image, size_t image_size){
    // Find an unused process control structure.
    struct process *proc = NULL; 
    int i; 
    for (i = 0;i < PROCS_MAX; i++){
        if (procs[i].state == PROC_UNUSED){
            proc = &procs[i];
            break;
        }
    }

    if(!proc){
        PANIC("no free process slots");
    }

    uint32_t *sp = (uint32_t *)&proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t ) user_entry; // ra

    // Map Kernel Pages. - 이건 왜 하는거지 음 
    uint32_t *page_table = (uint32_t *) alloc_pages(1);
    for (paddr_t paddr = (paddr_t) __kernel_base; paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE){
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
    }

    /* 
        먼저, 커널이 MMIO 레지스터에 접근할 수 있도록 virtio-blk MMIO 영역을 페이지 테이블에 매핑한다.
    */
    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);

    /*
        실행 이미지를 지정된 크기에 맞게 페이지별로 복사하여 프로세스의 페이지 테이블에 매핑한다.
        첫 번째 컨텍스트 전환의 점프 대상을 user_entry로 설정한다.

        실행 이미지를 복사하지 않고 직접 매핑하면 동일한 애플리케이션의 프로세스가 동일한 물리적 페이지를 공유하게됨 -> 메모밀 분리 실패
    */
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE){
        paddr_t page = alloc_pages(1);

        // Handle the case where the data to be copied is smaller than page size; 
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;
    
        memcpy((void*) page, image + off, copy_size); 
        map_page(page_table, USER_BASE + off, page, PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }

    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t )sp;
    proc->page_table = page_table;
    return proc;
}

/*
Exception의 라이프
1. CPU는 medelg 레지스터를 확인하여 예외를 처리할 동작 모드를 결정한다. 
OpenSBI는 이미 S-Mode의 핸들러에서 U-Mode / S-Mode 예외를 처리하도록 구성한다.

2. CPU는 자신의 상태(레지스터)를 다양한 CSR에 저장한다.
scause : 예외 유형, 커널은 이를 읽고 예외 유형을 식별한다.
stval : 예외에 대한 추가 정보(예외를 발생시킨 메모리 주소), 예외 유형에 따라 다름
sepc : 예외가 발생한 지점의 프로그램 카운터
sstatus : 예외가 발생한 경우 작동 모드(u or s)

3. stvec 레지스터의 값은 프로그램 카운터로 설정되어 커널의 예외 처리기로 이동한다.

4. 예외 처리기는 범용 레지스터(즉, 프로그램 상태)를 저장하고 예외를 처리함

5. 완료되면 예외 처리기는 저장된 실행 상태를 복원하고 sret 명령을 호출하여 예외가 발생한 지점부터 실행을 재개한다. 
*/

/*
sscratch 레지스터는 예외 발생시 스택포인터를 저장하는 임시 저장소로 사용, 나중에 복원

부동소수점 레지스터는 커널 내에서 사용되지 않으므로 여기에 저장할 필요가 없음. 일반적으로 스레드 전환 중에 저장되고 복원됨

스택포인터가 a0 레지스터에 설정되고 handle_trap 함수가 호출됨. 이떄 스택포인터가 가리키는 주소에는 나중에 설명하는 trap_frame 구조와 동일한 구조에 저장된 레지스터 값이 포함됨

__attribute((align(4)))을 추가하면 함수의 시작 주소가 4바이트 경계에 정렬됨
이는 stvec 레지스터가 예외 핸들러의 주소를 보유할 뿐만 아니라 하단 2비트에 모드를 나타내는 플래그를 가지고 있음

기존의 예외처리기는 실행 상태를 스택에 저장함. 
하지만 이제 각 프로세스마다 별도의 커널 스택을 사용하기 떄문에 프로세스 전환 시 sscratch 레지스터에 현재 실행중인 프로세스의 커널 스택 초기값을 설정
*/

/*
    csrrw 명령어
    tmp = sp; 
    sp = sscratch;
    sscratch = tmp;

    sp는 현재 실행 중인 프로세스의 커널(사용자 스택이 아닌) 스택을 가리킨다. 예외가 발생한 시점의 sp(사용자 스택)의 원래 값을 sscratch가 보유한다.
    다른 레지스터를 커널 스택에 저장한 후, 원래 sp 값을 복원하고 이를 커널 스택에 저장. 그런 다음 sscratch의 초기 값을 계산하여 복원.
    여기서 중요한 점은 프로세스가 독립적인 커널 스택을 가진다는 것. 컨텍스트 전환 중에 sscratch의 내용을 바꾸면 아무 일도 없었다는 것처럼 프로세스가 중단된 지점부터 실행을 재개할 수 있음
*/

/*
    sscratch를 조정하여 커널 스택으로 전환해야 하는 이유
    예외 발생 시 스택포인터를 신뢰해서는 안된다. 예외 처리기에는 다음 3가지 패턴을 고려해야딤
    1. 커널 모드에서 예외가 발생한 경우
    2. 커널모드에서 다른 예외(중첩 예외)를 처리할 때 예외가 발생한 경우
    3. 사용자 모드에서 예외가 발생한 경우 

    1번의 경우 스택 포인터를 재설정하지 않아도 문제 없음
    2번의 경우 저장된 영역을 덮어쓰지만, 중첩 예외에 대한 커널 패닉을 트리거 하는 구현이므로 괜찮음

    3번의 경우 sp는 사용자(어플리케이션) 스택 영역을 가리킴. sp를 그대로 사용(신뢰)하도록 구현하면 커널이 충돌하는 취약점이 발생할 수 있음
    만약 sscratch를 조정하지 않는다면 예외 발생시 스택 포인터가 매핑되지 않은 주소(userland)를 가리키기 때문에 이에 대한 또다른 예외를 불러옴
    -> 무한루프로 커널 중단 -> 즉, 신뢰할 수 있는 스택 영역을 처음부터 다시 가져와야 함
*/
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void){
    __asm__ __volatile__(
        // 실행 중인 프로세스의 커널 스택을 처음부터 다시 가져온다.
        "csrrw sp, sscratch, sp\n"
        "addi sp, sp, -4 * 31\n"
        "sw ra, 4 * 0(sp)\n"
        "sw gp, 4 * 1(sp)\n"
        "sw tp, 4 * 2(sp)\n"
        "sw t0, 4 * 3(sp)\n"
        "sw t1, 4 * 4(sp)\n"
        "sw t2, 4 * 5(sp)\n"
        "sw t3, 4 * 6(sp)\n"
        "sw t4, 4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        // 예외가 발생한 시점의 SP를 검색하여 저장한다.
        "csrr a0, sscratch\n"
        "sw a0, 4 * 30(sp)\n"

        // 커널스택 초기화
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        "lw ra, 4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
    );
}

/*
    트랩 핸들러에 저장된 "registers at the time of exception"의 구조를 받는다.
*/
void handle_syscall(struct trap_frame *f){
    switch(f->a3){
        case SYS_EXIT:
            printf("process %d exited\n", current_proc->pid);
            /*
                간단히 표현하기 위해 PROC_EXITED 플래그만 표시했지만 실질적인 OS는 페이지 테이블, 할당된 메모리 영역과 같이 프로세스가 보유한 리소스를 정리해야됨
            */
            current_proc->state = PROC_EXITED;
            yield();
            PANIC("unreachable");
        /*
            getchar 시스템 호출은 문자가 입력될때까지 SBI를 반복적으로 호출한다.
            단순 반복이면 CPU를 점유하기에 다른 프로세스에 양보하기 위해 yield호출
        */
        case SYS_GETCHAR:
            while(1){
                long ch = getchar();
                if (ch >= 0){
                    f->a0 = ch;
                    break;
                }

                yield();
            }
            break;

        case SYS_PUTCHAR:
            putchar(f->a0);
            break;
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}

// sret:: 트랩 핸들러에서 반환(프로그램 카운터, 작동 모드 등 복원)
/*
https://github.com/d0iasm/rvemu/blob/f55eb5b376f22a73c0cf2630848c03f8d5c93922/src/cpu.rs#L3357-L3400
The RISC-V Reader 참조
Supervisor-Mode Exception handler에서 Return한다. 
PC(Program Counter)를 CSRs(sepc)에 저장하고 privilege mode를 CSRs[sstatus].SPP, 
CSRs(sstatus).SIE를 CSRs[sstatus].SPIE에 저장, CSR[sstatus].SPIE를 1로 세팅 그리고 CSPs[sstatus].SPP를 0으로 세팅
QEMU에서는 mstatus를 sstatus대신 사용
*/


void handle_trap(struct trap_frame *f){
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    /*
        ecall 명령어가 호출되었는지 여부는 scause 값을 확인하여 확인이 가능하다.
        handle_syscall 함수를 호출하는 것 이외에도 sepc값에 4(ecall 명령어의 크기)를 더한다.
        이는 sepc가 예외를 발생시킨 프로그램 카운터를 가리키며 이 카운터가 ecall 명령어를 가리키기 때문이다. 
        이를 변경하지 않으면 커널이 같은 위치로 돌아가서 ecall 명령어가 반복적으로 실행된다.
    */
    if (scause == SCAUSE_ECALL){
        handle_syscall(f);
        user_pc += 4;
    } else {
        /*
            scause가 2이면 프로그램이 잘못된 명령어를 실행하려고 시도했음을 의미 unimp의 예상동작
            sepc의 값은 unimp 명령어가 호출되는 줄을 가리킴
        */
        PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
    }

    WRITE_CSR(sepc, user_pc);
}

/*
프로그램이 메모리에 접근할 때 CPU는 지정된 주소(가상 주소)를 실제 주소로 변환한다.
가상 주소를 물리적 주소에 매핑하는 테이블을 페이지 테이블이라고 함.
페이지 테이블을 전환하면 동일한 가상 주소가 다른 물리적 주소를 가리킬 수 있음 
이를 통해 메모리 공간(가상 주소 공간)을 격리하고 커널과 애플리케이션 메모리 영역을 분리하여 시스템보안 강화

2단계 페이지 테이블 사용하는 RISC-V의 페이징 메커니즘인 Sv32
32비트 가상 주소는 1단계 페이지 테이블 인덱스(VPN[1]과 2단계 인덱스 VPN[0]으로 나눠진다.

동일한 4KB 페이지 내의 주소는 동일한 페이지 테이블 항목에 있음
메모리에 접근할 때 CPU는 VPN[1], VPN[0]을 계산하여 상응하는 페이지테이블의 엔트리를 찾고 읽어서 매핑된 물리주소를 읽음
그리고 offset을 더해서 물리주소를 얻음 
*/

/*
링커 스크립트의 ALIGN(4096)으로 인해 __free_ram은 4KB 경계에 배치됨.
따라서 alloc_pages 함수는 항상 4KB에 정렬된 주솔르 반환한다. __free_ram_end를 초과해서 할당을 시도하면, 메모리 부족으로 커널 패닉 발생

메모리 해제를 위해서는 비트맵 기반 알고리즘이나 버디시스템 알고리즘 사용
 */
paddr_t alloc_pages(uint32_t n){
    // 정적 변수로 정의함. 전역 변수 처럼 동작
    // 할당할 다음 영역의 시작 주소를 가리킨다. 할당할 때 next_paddr은 할당되는 크기만큼 전진
    // next_paddr은 처음 __free_ram의 주소를 보유. 즉, 메모리는 __free_ram 부터 순차적으로 할당된다.
    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if(next_paddr > (paddr_t) __free_ram_end){
        PANIC("out of memory");
    }

    memset((void *)paddr, 0, n * PAGE_SIZE);
    return paddr;
}

/*
1단계 페이지 테이블(table1), 가상 주소(vaddr), 물리 주소(paddr), 페이지 테이블 항목 플래그를 받음
두 번째 수준의 페이지 테이블을 준비하고 두 번째 수준의 페이지 테이블 항목을 채운다.
항목에 실제 주소 자체가 아니라 실제 페이지 번호가 포함되어야 하므로 paddr을 PAGE_SIZE로 나눈다. 
 */
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags){
    if(!is_aligned(vaddr, PAGE_SIZE)) {
        PANIC("unaligned vaddr %x", vaddr);
    }

    if(!is_aligned(paddr, PAGE_SIZE)) {
        PANIC("unaligned paddr: %x", paddr);
    }

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0){
        // Create the non-existent 2nd level page table
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V; 
    }

    // set the 2nd level page table entry to map the physical page.
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *)((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10 | flags | PAGE_V);
}

struct process *proc_a; 
struct process *proc_b;

/* 
    Scheduler
    switch_context 함수를 직접 호출하여 "다음에 실행할 프로세스"를 지정함.
    프로세스 수가 많아지면 선택에 있어 골치아픔 -> 스케줄러 구현
*/

void yield(void){
    struct process *next = idle_proc;
    for(int i=0;i < PROCS_MAX; i++){
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if(proc->state == PROC_RUNNABLE && proc->pid > 0){
            next = proc; 
            break;
        }
    }

    // 실행할 수 있는 프로세스가 없다면
    if(next == current_proc){
        return;
    }

    // 스택포인터는 낮은 주소로 확장되므로 커널 스택의 초기 값으로 sizeof(next->stack)번째 바이트의 주소를 설정한다.
    __asm__ __volatile__(
        "sfence.vma\n"
        /*
             satp     80080258인것을 확인할 수 있음
             RISC_V Sv32모드에 따라 이 값을 해석하면 첫 번째 레벨 페이지 테이블의 시작 물리적 주소를 알 수 있음
             ( 80080258  & 0x3fffff) * 4096 =  0x80258000
            
            가상 메모리 주소인 0x80000000에 상응하는 second level page table을 알고싶다면 아래 명령어 사용
            xp /x 0x80258000+512 * 4
            (xp 명령어 대신 x를 사용하면 지정된 가상 주소에 대한 메모리 덤프를 볼 수 잇음
                이는 커널 공간과 달리 가상 주소가 실제 주소가 일치하지 않는 사용자 공간 메모리를 검사할 때 유용함
            )

            0x80000000 >> 22 = 512

            0000000080258800: 0x20096401
            두 번재 레벨 페이지 테이블은 (0x20096400 >> 10) * 4096 = 0x80259000에 위치
            xp /1024x 0x80259000

            초기값은 0으로 채워져 있지만 512번째 항목부터 값이 나타나기 시작함
            이는 __kernel_base가 0x80200000이고 VPN[1]이 0x200이기 때문에이다.
            QEMU에는 현재 페이지 테이블 매핑을 사람이 읽을 수 있는 형식으로 표시하는 명령어 info mem
        */
        /* satp에서 1단계 페이지 테이블을 지정하여 페이지 테이블을 전환할 수 있다. */
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        :   [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)), 
            [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );
    /*
        sfence.vma
        1. 페이지 테이블의 변경 사항이 제대로 완료되었는지 확인 (memory fence)
        2. 페이지 테이블 항목(TLB)의 캐시를 지우기
        
        커널이 시작되면 기본적으로 페이징이 비활성화됨. (satp 레지스터가 설정되지 않음)
        가상 주소는 실제 주소와 일치하는 것처럼 동작
    */

    // Context Switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}

void proc_a_entry(void){
    printf("starting process A\n");
    while(1){
        putchar('A');
        yield();
    
        for(int i=0;i<30000000;i++){
            __asm__ __volatile__("nop");
        }
    }
}

void proc_b_entry(void){
    printf("starting process B\n");
    while(1){
        putchar('B');
        yield();

        for(int i=0;i<30000000;i++){
            __asm__ __volatile__("nop");
        }
    }
}

/*
Hello World의 라이프 
1. 커널이 ecall을 실행함. CPU는 M-mode 트랩 핸들러(mtvec 레지스터)로 점프한다.
2. 레지스터 저장후 트랩 핸들러 호출 (https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_trap.c#L263)
3. eid기반으로 상응하는 SBI 함수 호출 (https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/sbi/sbi_ecall_legacy.c#L63C2-L65)
4. 디바이스 드라이버(8250 UART)는 QEMU에 문자를 전송한다. (https://github.com/riscv-software-src/opensbi/blob/0ad866067d7853683d88c10ea9269ae6001bcf6f/lib/utils/serial/uart8250.c#L77)
5. QEMU의 8250 UART 에뮬레이션 구현은 문자를 수신하여 표준 출력으로 전송
*/

void kernel_main(void){
   /*
    printf("\n\nHello %s\n", "Printf!");
    printf("1 + 2 = %d, %x\n", 1 + 2, 0x1234abcd);


    for (;;){
        __asm__ __volatile__("wfi");
    }
    */

    memset(__bss, 0, (size_t)__bss_end - (size_t) __bss);
    printf("\n\nHello Kernel\n");

    // stvec 레지스터에 예외 처리기의 주소를 저장한다.
    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    virtio_blk_init();

    char buf[SECTOR_SIZE];
    read_write_disk(buf, 0, false /* read from the disk */);
    printf("first sector: %s\n", buf);

    strcpy(buf, "Hello from kernel!!!!\n");
    read_write_disk(buf, 0, true /* write to the disk */);

    /*
        https://github.com/riscv-non-isa/riscv-asm-manual/blob/main/src/asm-manual.adoc#instruction-aliases
        csrrw x0, cycle, x0. 
        cycle은 read-only CSR임, 이 CSR의 존재 여부와 관계없이 여기에 쓰기를 시도하면 잘못된 명령어 예외가 생성됨 
    */
 
   //__asm__ __volatile__("unimp"); 

    /*
        current_proc = idle_proc를 통해 부팅 프로세스의 실행 컨텍스트가 유휴 프로세스의 실행 컨텍스트로 저장되고 복원된다.
        반환 함수를 처음 호출하는 동안 유휴 프로세스에서 프로세스 A로 전환하고, 다시 유휴 프로세스로 전환할 때는 이 반환 함수 호출에서 반환하는 것처럼 동작한다.
    */
    idle_proc = create_process(NULL, 0);
    idle_proc->pid = -1; // IDLE
    current_proc = idle_proc;

    create_process(_binary_shell_bin_start, (size_t)_binary_shell_bin_size);

    yield();
    PANIC("booted!");
}


/*
커널 실행은 링커 스크립트에서 진입 지점으로 지정된 부팅 함수에서 시작된다. 
이 함수에서 스택포인터(sp)는 링커 스크립트에 정의된 스택 영역의 끝 주소로 설정됨
그리고 kernel_main함수로 이동 
스택은 0을 향해 증가하므로 사용할 수록 줄어듬
즉, 스택 영역의 끝 주소(시작 주소가 아님)를 설정해야 함
 */

/*
부팅함수에는 링커 스크립트에서 함수의 위치를 제어하는 속성이 존재
OpenSBI는 진입점을 알 지 못한채 0x80200000으로 점프하기 때문에 부팅함수는 0x80200000에 배치해야 됨
*/

/*
컴파일러가 함수 본문 앞뒤에 반환 명령어와 같은 불필요한 코드를 생성하지 않도록 지시
*/
__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void){
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n" // Set the stack pointer
        "j kernel_main\n" // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // stack top 주소를 %[stack_top]에 넘김
    );
}