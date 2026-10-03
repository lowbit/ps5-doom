#include <string.h>

#include "gpu.h"
#include "platform.h"
#include "present_kernel.h"
#include "ps5.h"
#include "sce.h"

#define AGC_VERSION 8
#define POOL_BYTES 0x200000
#define SLOTS 2
#define SLOT_BYTES 0x40000
#define COMMAND_BYTES 0x8000
#define SOURCE_OFFSET 0x10000
#define PALETTE_OFFSET 0x20000
#define KERNARG_OFFSET 0x21000
#define FENCE_OFFSET 0x22000
#define CODE_OFFSET (SLOTS * SLOT_BYTES)
#define INPUT_BYTES (KERNARG_OFFSET + 0x100 - SOURCE_OFFSET)

#define GROUP 64
#define INVALIDATE_SHADER_CACHES 0x4380
#define ACQUIRE_POLL 0xa0
#define FENCE_TIMEOUT_US 500000

#define REG_NUM_THREAD_X 0x207
#define REG_PGM_LO 0x20c
#define REG_PGM_RSRC1 0x212
#define REG_TMPRING_SIZE 0x218
#define REG_PGM_RSRC3 0x228
#define REG_USER_DATA 0x240

#define PKT3(op, body_words) (0xc0000000u | ((uint32_t)(body_words)-1) << 16 | (uint32_t)(op) << 8)
#define OP_WAIT_REG_MEM 0x3c
#define OP_RELEASE_MEM 0x49
#define RELEASE_FLUSH_AND_INVALIDATE_EOP 0x0070f514u
#define RELEASE_WRITE_32_AFTER_CONFIRM 0x23000000u
#define WAIT_MEMORY_EQUAL 0x13u
#define WAIT_POLL_INTERVAL 4u

typedef struct
{
    uint64_t src, palette, dst;
    uint32_t pitch, out_width, out_height, view_x, view_width, src_width, src_height;
} __attribute__((packed)) kernel_args_t;

_Static_assert(sizeof(kernel_args_t) == KERNEL_KERNARG_SIZE, "kernel argument layout");

static gpu_config_t config;
static uint8_t *pool;
static uint64_t code_address;
static int64_t slot_marker[SLOTS];
static int frame;
static volatile int out_of_space;

static uint8_t command_full(SceAgcCommandBuffer *cb, uint32_t words, void *user)
{
    (void)cb;
    (void)words;
    (void)user;
    out_of_space = 1;
    return 0;
}

static uint8_t *slot_base(int slot)
{
    return pool + slot * SLOT_BYTES;
}

static volatile uint32_t *slot_fence(int slot)
{
    return (volatile uint32_t *)(slot_base(slot) + FENCE_OFFSET);
}

static int emit(SceAgcCommandBuffer *cb, const uint32_t *words, int count)
{
    if (cb->down - cb->up < count)
        return -1;
    memcpy(cb->up, words, count * sizeof(*words));
    cb->up += count;
    return 0;
}

static int emit_fence_and_wait(SceAgcCommandBuffer *cb, volatile uint32_t *fence, uint32_t value)
{
    uint64_t address = (uintptr_t)fence;
    const uint32_t words[] = {
        PKT3(OP_RELEASE_MEM, 7),
        RELEASE_FLUSH_AND_INVALIDATE_EOP,
        RELEASE_WRITE_32_AFTER_CONFIRM,
        (uint32_t)address,
        (uint32_t)(address >> 32),
        value,
        0,
        0,
        PKT3(OP_WAIT_REG_MEM, 6),
        WAIT_MEMORY_EQUAL,
        (uint32_t)address,
        (uint32_t)(address >> 32),
        value,
        0xffffffffu,
        WAIT_POLL_INTERVAL,
    };

    return emit(cb, words, sizeof(words) / sizeof(words[0]));
}

static int set_registers(SceAgcCommandBuffer *cb, uint32_t offset, const uint32_t *values,
                         uint32_t count)
{
    return sceAgcCbSetShRegisterRangeDirect(cb, offset, values, count) ? 0 : -1;
}

static int wait_slot(int slot)
{
    uint64_t start = plat_ticks_us();

    while (slot_marker[slot])
    {
        ps5_cache_flush(slot_fence(slot), sizeof(uint32_t));
        if (*slot_fence(slot) == (uint32_t)slot_marker[slot])
            return 0;
        if (plat_ticks_us() - start > FENCE_TIMEOUT_US)
            return -1;
        plat_sleep_us(100);
    }
    return 0;
}

int gpu_init(const gpu_config_t *cfg)
{
    int result;

    config = *cfg;
    if ((result = sceAgcInit(AGC_VERSION)) != 0)
    {
        plat_log("gpu: sceAgcInit %#x\n", result);
        return -1;
    }
    pool = ps5_gpu_memory(POOL_BYTES);
    if (!pool)
        return -1;
    memset(pool, 0, POOL_BYTES);
    memcpy(pool + CODE_OFFSET, kernel_image, sizeof(kernel_image));
    code_address = (uintptr_t)(pool + CODE_OFFSET + KERNEL_ENTRY);
    ps5_cache_flush(pool, POOL_BYTES);
    plat_log("gpu: AGC ready, kernel at %#llx\n", (unsigned long long)code_address);
    return 0;
}

static void upload(int slot, const uint8_t *pixels, const uint32_t *rgba, int target)
{
    uint8_t *base = slot_base(slot);
    kernel_args_t args = {
        .src = (uintptr_t)(base + SOURCE_OFFSET),
        .palette = (uintptr_t)(base + PALETTE_OFFSET),
        .dst = (uintptr_t)config.targets[target],
        .pitch = config.pitch,
        .out_width = config.width,
        .out_height = config.height,
        .view_x = config.view_x,
        .view_width = config.view_width,
        .src_width = config.src_width,
        .src_height = config.src_height,
    };

    memcpy(base + SOURCE_OFFSET, pixels, (size_t)config.src_width * config.src_height);
    memcpy(base + PALETTE_OFFSET, rgba, 256 * sizeof(*rgba));
    memcpy(base + KERNARG_OFFSET, &args, sizeof(args));
    ps5_cache_flush(base + SOURCE_OFFSET, INPUT_BYTES);
}

static int record(int slot, int target, int64_t marker, SceAgcCommandBuffer *cb)
{
    uint8_t *base = slot_base(slot);
    uint64_t kernarg = (uintptr_t)(base + KERNARG_OFFSET);
    const uint32_t threads[3] = {GROUP, 1, 1};
    const uint32_t program[2] = {(uint32_t)(code_address >> 8), (uint32_t)(code_address >> 40)};
    const uint32_t resources[2] = {KERNEL_RSRC1, KERNEL_RSRC2};
    const uint32_t resources3 = KERNEL_RSRC3;
    const uint32_t no_scratch = 0;
    uint32_t user[KERNEL_USER_SGPRS] = {0};

    user[KERNEL_KERNARG_SGPR] = (uint32_t)kernarg;
    user[KERNEL_KERNARG_SGPR + 1] = (uint32_t)(kernarg >> 32);

    sceAgcDriverWaitUntilSafeForRendering(&cb->up, sceAgcDriverGetWaitRenderingPacketSizeInDwords(),
                                          0, (uint32_t)config.video, target);
    if (!sceAgcDcbAcquireMem(cb, 0, 0, INVALIDATE_SHADER_CACHES, (uintptr_t)(base + SOURCE_OFFSET),
                             INPUT_BYTES, ACQUIRE_POLL) ||
        set_registers(cb, REG_NUM_THREAD_X, threads, 3) ||
        set_registers(cb, REG_PGM_LO, program, 2) ||
        set_registers(cb, REG_PGM_RSRC1, resources, 2) ||
        set_registers(cb, REG_PGM_RSRC3, &resources3, 1) ||
        set_registers(cb, REG_TMPRING_SIZE, &no_scratch, 1) ||
        set_registers(cb, REG_USER_DATA, user, KERNEL_USER_SGPRS) ||
        !sceAgcCbDispatch(cb, config.height, 1, 1, 0) ||
        emit_fence_and_wait(cb, slot_fence(slot), (uint32_t)marker) ||
        !sceAgcDcbSetFlip(cb, (uint32_t)config.video, target, SCE_VIDEO_FLIP_VSYNC, marker))
        return -1;
    return out_of_space || cb->up > cb->top ? -1 : 0;
}

int gpu_present(const uint8_t *pixels, const uint32_t *rgba, int target, int64_t marker)
{
    int slot = frame++ % SLOTS;
    uint32_t *words = (uint32_t *)slot_base(slot);
    SceAgcCommandBuffer cb = {words, words + COMMAND_BYTES / 4, words, words + COMMAND_BYTES / 4,
                              command_full, NULL, 0, 0};
    SceAgcSubmission submission;
    int result;

    if (wait_slot(slot))
    {
        plat_log("gpu: frame %d never completed\n", frame - SLOTS);
        return -1;
    }
    upload(slot, pixels, rgba, target);
    *slot_fence(slot) = 0;
    ps5_cache_flush(slot_fence(slot), sizeof(uint32_t));
    out_of_space = 0;
    if (record(slot, target, marker, &cb))
    {
        plat_log("gpu: command recording failed\n");
        return -1;
    }
    ps5_cache_flush(words, (size_t)(cb.up - words) * sizeof(*words));

    submission.words = words;
    submission.word_count = (uint32_t)(cb.up - words);
    submission.flags = 0;
    memset(submission.padding, 0, sizeof(submission.padding));
    if ((result = sceAgcDriverSubmitDcb(&submission)) != 0)
    {
        plat_log("gpu: submit %#x\n", result);
        return -1;
    }
    sceAgcSuspendPoint();
    slot_marker[slot] = marker;
    return 0;
}

int gpu_finish(void)
{
    int slot;

    for (slot = 0; slot < SLOTS; slot++)
        if (wait_slot(slot))
            return -1;
    return 0;
}
