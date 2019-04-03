// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the main libnx system header, for Switch development
#include <switch.h>

static Service g_grcdSrv;
static Service g_grccSrv;
static u64 g_refCnt;


Result grcExit(void) {
    if (atomicDecrement64(&g_refCnt) == 0) {
        serviceClose(&g_grccSrv);
        serviceClose(&g_grcdSrv);
    }
    return 0;
}

Result grcInitialize(void) {
    Result rc = 0;
    
    atomicIncrement64(&g_refCnt);

    if (serviceIsActive(&g_grccSrv))
        return 0;

    rc = smGetService(&g_grccSrv, "grc:c");
    if (R_FAILED(rc)) grcExit();
    rc = smGetService(&g_grcdSrv, "grc:d");
    if (R_FAILED(rc)) grcExit();

    return rc;
}

Result grcdCmd1()
{
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 1;

    Result rc = serviceIpcDispatch(&g_grcdSrv);
    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);
        struct {
            u64 magic;
            u64 result;
        } *resp = r.Raw;
        rc = resp->result;
    }
    return rc;
}

Result grcdCmd2(u32 i, void *buff, size_t len, u32 *n_frames_out, u64 *ts_out, size_t *size_out)
{
    IpcCommand c;
    ipcInitialize(&c);
    ipcAddRecvBuffer(&c, buff, len, 0);

    struct {
        u64 magic;
        u64 cmd_id;
        u32 input;
    } PACKED *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 2;
    raw->input = i;

    Result rc = serviceIpcDispatch(&g_grcdSrv);
    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);
        struct {
            u64 magic;
            u64 result;
            u32 n_frames;
            u32 size;
            u64 start_ts;
        } *resp = r.Raw;

        rc = resp->result;

        if(n_frames_out) *n_frames_out = resp->n_frames;
        if(size_out) *size_out = resp->size;
        if(ts_out) *ts_out = resp->start_ts;

        printf("got size=%u, %u frames, ts=%lu\n", resp->size, resp->n_frames, resp->start_ts);
    }
    return rc;
}

typedef struct ContinuousRecorder
{
    TransferMemory tmem;
    Service s;
} ContinuousRecorder;


// 0xcad4 = your tmem is too small

Result grcGetIContinuousRecorder(ContinuousRecorder* out)
{
    IpcCommand c;
    ipcInitialize(&c);
    u64 grc_transfermem_size = 0x10000000;
    Result rc = tmemCreate(&out->tmem, grc_transfermem_size, Perm_None);

    ipcSendHandleCopy(&c, out->tmem.handle);
    struct {
        u64 magic;
        u64 cmd_id;
        
        // data blob start
        u64 unk_1; // 0
        char padding[8]; // 8
        u32 unk_2; // 16
        u32 unk_3; // 20
        u32 unk_4; // 24
        u16 unk_5; // 28
        char fps;
        char unk_6; // 31
        u64 unk_7; // some size?

        char test[64 - 8 - 8 - 4 - 4 - 4 - 2 - 1 - 1 - 8];
        u64 your_transfer_memory_size;
    } PACKED *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    if(R_SUCCEEDED(rc))
    {
        raw->magic = SFCI_MAGIC;
        raw->cmd_id = 1;
        raw->fps = 60;
        raw->unk_1 = 0; // idk
        raw->unk_2 = 0; // idk
        raw->unk_3 = 1; // some time
        raw->unk_4 = 1; // some time
        raw->unk_5 = 0; // idk - changing this changes frame size
        raw->unk_6 = 0; // some flag
        raw->unk_7 = 0; // some size

        raw->your_transfer_memory_size = grc_transfermem_size;

        rc = serviceIpcDispatch(&g_grccSrv);
        if (R_SUCCEEDED(rc)) {
            IpcParsedCommand r;
            ipcParse(&r);
            struct {
                u64 magic;
                u64 result;
            } *resp = r.Raw;
            rc = resp->result;

            if (R_SUCCEEDED(rc)) {
                serviceCreate(&out->s, r.Handles[0]);
            }
        }
    }
    
    if(R_FAILED(rc))
    {
        tmemClose(&out->tmem);
    }
    
    return rc;
}

Result grcContinuousRecorder_cmd_1(ContinuousRecorder *rec)
{
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
    } PACKED *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 1;

    Result rc = serviceIpcDispatch(&rec->s);
    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);
        struct {
            u64 magic;
            u64 result;
        } *resp = r.Raw;
        rc = resp->result;
    }
    return rc;
}

// Main program entrypoint
int main(int argc, char* argv[])
{
    consoleInit(NULL);

    // ok.
    Result rc = grcInitialize();
    printf("grcInitialize %08x\n", rc);
   
    // Other initialization goes here. As a demonstration, we print hello world.
    printf("Hello World!\n");

    ContinuousRecorder rec;

    rc = grcGetIContinuousRecorder(&rec);
    if(R_FAILED(rc))
    {
        printf("grcGetIContinuousRecorder failed %08x\n", rc);
    }

    rc = grcContinuousRecorder_cmd_1(&rec);
    if(R_FAILED(rc))
    {
        printf("grcContinuousRecorder_cmd_1 %08x\n", rc);
    }

    // Call this once, ever!
    /*rc = grcdCmd1();
    if(R_FAILED(rc))
    {
        printf("grcdCmd1 %08x\n", rc);
    }*/

    unsigned int buff_size = 0x100000;
    char *buff = malloc(buff_size);

    FILE *cap_file = fopen("test_capture.h264", "wb");

    // Main loop
    while (appletMainLoop())
    {
        //printf("Hello World!\n");
        // Scan all the inputs. This should be done once for each frame
        hidScanInput();

        // hidKeysDown returns information about which buttons have been
        // just pressed in this frame compared to the previous one
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_PLUS)
            break; // break in order to return to hbmenu

        // Your code goes here

        // Update the console, sending a new frame to the display
        consoleUpdate(NULL);

        u32 n_frames;
        u64 ts;
        size_t actual_size;

        memset(buff, 0xff, buff_size);
        rc = grcdCmd2(0, buff, buff_size, &n_frames, &ts, &actual_size);
        printf("grcdCmd2 %08x\n", rc);
        for(int i = 0; i < 0x100; i++)
        {
            printf("%02x ", buff[i]);
        }

        int n = fwrite(buff, 1, actual_size, cap_file);
        printf("wrote %i bytes\n", n);

        svcSleepThread(1e9);
    }

    fclose(cap_file);
    free(buff);

    serviceClose(&rec.s);
    tmemClose(&rec.tmem);
    grcExit();
    // Deinitialize and clean up resources used by the console (important!)
    consoleExit(NULL);
    return 0;
}
