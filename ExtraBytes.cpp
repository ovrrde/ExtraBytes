#include <stdio.h>
#include "Memory/Memory.h"
#include "Utils/Timer.h"

int main(int argc, char* argv[]) {
    const char* target = "notepad.exe";
    if (argc > 1) target = argv[1];

    ebp::Memory mem;
    if (!mem.Init(target))
        return 1;

    uintptr_t user32 = ebp::Memory::GetModuleBase("user32.dll");
    printf("\nuser32.dll base = 0x%llX\n", (uint64_t)user32);

    auto dosSignature = mem.Read<uint16_t>(user32);
    printf("DOS signature   = 0x%04X\n", dosSignature);

    auto eLfanew = mem.Read<uint32_t>(user32 + 0x3C);
    auto peSig   = mem.Read<uint32_t>(user32 + eLfanew);
    printf("PE signature    = 0x%08X\n", peSig);

    printf("\nRead speed test below:\n\n");

    ebp::Timer timer;
    const int sizes[] = { 64, 256, 1024, 4096, 8192 };

    for (int size : sizes) {
        int reads = size / 8;
        uint8_t* buf = new uint8_t[size];

        timer.Start();
        mem.ReadBytes(user32, buf, size);
        double ms = timer.ElapsedMs();

        double usPerRead = (ms * 1000.0) / reads;
        double kbPerSec  = (size / 1024.0) / (ms / 1000.0);

        printf("  %5d bytes  (%4d reads)  %8.2f ms  |  %.1f us/read  |  %.1f KB/s\n",
            size, reads, ms, usPerRead, kbPerSec);

        delete[] buf;
    }

    /* 
     * The first command opens calculator inside the target process via _wsystem
     * This will open multiple calculator instances in my experience,
     * I believe this is due to WM_LBUTTONDBLCLK firing multiple times internally.
     *
     * The second command, "call", will resolve the exported function and calls it
     * internally from the process.
     */

    // mem.Execute("calc");
    // mem.Call("kernel32.dll", "ExitProcess");

    printf("\nDone.\n");
    return 0;
}
