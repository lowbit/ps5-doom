#ifndef SCE_H
#define SCE_H

#include <stddef.h>
#include <stdint.h>

#define SCE_USER_SYSTEM 0xff

#define SCE_MEMORY_GPU_SHARED 12
#define SCE_PROT_CPU_GPU_RW 0x33
#define SCE_MEMORY_ALIGN 0x200000

#define SCE_VIDEO_BUS_MAIN 0
#define SCE_VIDEO_FLIP_VSYNC 1
#define SCE_VIDEO_FORMAT_RGBA8_SRGB UINT64_C(0x8000000022000000)
#define SCE_VIDEO_TILING_TILED 0

#define SCE_PAD_PORT_STANDARD 0
#define SCE_PAD_L3 0x0002
#define SCE_PAD_R3 0x0004
#define SCE_PAD_OPTIONS 0x0008
#define SCE_PAD_UP 0x0010
#define SCE_PAD_RIGHT 0x0020
#define SCE_PAD_DOWN 0x0040
#define SCE_PAD_LEFT 0x0080
#define SCE_PAD_L2 0x0100
#define SCE_PAD_R2 0x0200
#define SCE_PAD_L1 0x0400
#define SCE_PAD_R1 0x0800
#define SCE_PAD_TRIANGLE 0x1000
#define SCE_PAD_CIRCLE 0x2000
#define SCE_PAD_CROSS 0x4000
#define SCE_PAD_SQUARE 0x8000
#define SCE_PAD_TOUCHPAD 0x100000

#define SCE_AUDIO_PORT_MAIN 0
#define SCE_AUDIO_S16_STEREO 1

typedef struct
{
    void *data;
    void *metadata;
    void *reserved[2];
} SceVideoOutBuffer;

typedef struct
{
    uint8_t opaque[80];
} SceVideoOutAttribute;

typedef struct
{
    uint64_t count;
    uint64_t process_time;
    uint64_t tsc;
    int64_t flip_arg;
    uint8_t reserved[96];
} SceVideoOutFlipStatus;

typedef struct
{
    uint16_t x, y;
    uint8_t finger;
    uint8_t reserved[3];
} ScePadTouch;

typedef struct
{
    uint8_t fingers;
    uint8_t reserved1[3];
    uint32_t reserved2;
    ScePadTouch touch[2];
} ScePadTouchData;

typedef struct
{
    uint32_t buttons;
    uint8_t lx, ly, rx, ry;
    uint8_t l2, r2;
    uint16_t reserved;
    float orientation[4];
    float velocity[3];
    float acceleration[3];
    ScePadTouchData touch;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t extension[16];
    uint8_t count;
    uint8_t reserved2[15];
} ScePadData;

typedef struct
{
    uint8_t large_motor;
    uint8_t small_motor;
} ScePadVibration;

typedef struct
{
    uint8_t r, g, b, a;
} ScePadColor;

typedef struct
{
    char reserved[45];
    char message[3075];
} SceNotificationRequest;

int64_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t start, int64_t end, size_t length, size_t alignment,
                                  int type, int64_t *physical);
int sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                             int64_t physical, size_t alignment);
uint64_t sceKernelGetProcessTime(void);
int sceKernelUsleep(uint32_t microseconds);
int sceKernelSendNotificationRequest(int device, SceNotificationRequest *request, size_t size,
                                     int blocking);
int sceKernelDebugOutText(int channel, const char *text);

int sceSystemServiceHideSplashScreen(void);
int sceSystemServiceLoadExec(const char *path, char *const *argv);

int sceUserServiceInitialize(void *params);
int sceUserServiceGetInitialUser(int *user);

int sceVideoOutOpen(int user, int bus, int index, const void *params);
int sceVideoOutClose(int handle);
int sceVideoOutSetFlipRate(int handle, int rate);
void sceVideoOutSetBufferAttribute2(SceVideoOutAttribute *attribute, uint64_t format,
                                    uint32_t tiling, uint32_t width, uint32_t height,
                                    uint64_t option, uint32_t dcc_control, uint64_t dcc_clear);
int sceVideoOutRegisterBuffers2(int handle, int set, int first, SceVideoOutBuffer *buffers,
                                int count, SceVideoOutAttribute *attribute, int category,
                                void *option);
int sceVideoOutSubmitFlip(int handle, int index, uint32_t mode, int64_t argument);
int sceVideoOutIsFlipPending(int handle);
int sceVideoOutWaitVblank(int handle);
int sceVideoOutGetFlipStatus(int handle, SceVideoOutFlipStatus *status);

int scePadInit(void);
int scePadOpen(int user, int type, int index, const void *params);
int scePadReadState(int handle, ScePadData *data);
int scePadSetVibrationMode(int handle, int mode);
int scePadSetVibration(int handle, const ScePadVibration *vibration);
int scePadSetLightBar(int handle, const ScePadColor *color);
int scePadClose(int handle);

typedef struct SceAgcCommandBuffer SceAgcCommandBuffer;
typedef uint8_t (*SceAgcOutOfSpace)(SceAgcCommandBuffer *buffer, uint32_t words, void *user);

struct SceAgcCommandBuffer
{
    uint32_t *bottom;
    uint32_t *top;
    uint32_t *up;
    uint32_t *down;
    SceAgcOutOfSpace out_of_space;
    void *user;
    uint32_t reserved_words;
    uint32_t padding;
};

typedef struct
{
    void *words;
    uint32_t word_count;
    uint8_t flags;
    uint8_t padding[3];
} SceAgcSubmission;

int sceAgcInit(uint32_t version);
uint32_t *sceAgcCbSetShRegisterRangeDirect(SceAgcCommandBuffer *cb, uint32_t offset,
                                           const uint32_t *values, uint32_t count);
uint32_t *sceAgcCbDispatch(SceAgcCommandBuffer *cb, uint32_t x, uint32_t y, uint32_t z,
                           uint32_t initiator);
uint32_t *sceAgcDcbAcquireMem(SceAgcCommandBuffer *cb, uint8_t engine, uint32_t cb_db_op,
                              uint32_t gcr_control, uint64_t base, uint64_t size,
                              uint32_t poll_interval);
uint32_t *sceAgcDcbSetFlip(SceAgcCommandBuffer *cb, uint32_t video, int index, uint32_t mode,
                           int64_t argument);
int sceAgcSuspendPoint(void);
uint32_t sceAgcDriverGetWaitRenderingPacketSizeInDwords(void);
uint32_t sceAgcDriverWaitUntilSafeForRendering(uint32_t **up, uint32_t words, uint32_t reserved,
                                               uint32_t video, int index);
int sceAgcDriverSubmitDcb(SceAgcSubmission *submission);

int sceAudioOutInit(void);
int sceAudioOutOpen(int user, int port, int index, uint32_t grain, uint32_t rate, uint32_t format);
int sceAudioOutOutput(int handle, const void *samples);
int sceAudioOutClose(int handle);

#endif
