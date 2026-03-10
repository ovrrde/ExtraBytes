#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <type_traits>

//  Target: Windows 10 22H2 (build 19045), x64

namespace ebp {

class Memory {
public:
    Memory();
    ~Memory();

    bool Init(const char* processName);

    void Detach();

    bool IsAttached() const;

    template<typename T>
    T Read(uintptr_t address);

    template<typename T>
    void Write(uintptr_t address, T value);

    // Read/write raw byte buffers. Stride in 8-byte chunks internally.
    bool ReadBytes(uintptr_t address, void* buffer, size_t length);
    bool WriteBytes(uintptr_t address, const void* buffer, size_t length);

    // Execute a shell command inside the target process.
    // Uses the _wsystem callback trick: sets Edit text to the command,
    // points the wordbreak callback at msvcrt!_wsystem, triggers it.
    bool Execute(const char* command);

    // Call an exported function in the target process via the wordbreak
    // callback. Resolves dll!func, points the callback at it, and triggers
    // WM_LBUTTONDBLCLK. The first argument passed to the function will be
    // a pointer to the current Edit text (wide).
    bool Call(const char* dll, const char* func);

    static uintptr_t GetModuleBase(const char* moduleName);

    DWORD GetPid() const;

    HWND GetEditHwnd() const;

private:
    HWND     m_hEdit;
    LONG_PTR m_origExtra;
    LRESULT  m_origWbp;
    DWORD    m_pid;
    bool     m_attached;

    bool PrimRead8(uintptr_t target, uint64_t* out);
    bool PrimWrite8(uintptr_t target, uint64_t val);

    static DWORD FindProcessByName(const char* name);
    static HWND  FindEditInProcess(DWORD pid);
    static uintptr_t ResolveExport(const char* dll, const char* func);
};


template<typename T>
T Memory::Read(uintptr_t address) {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
    T result{};
    ReadBytes(address, &result, sizeof(T));
    return result;
}

template<typename T>
void Memory::Write(uintptr_t address, T value) {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
    WriteBytes(address, &value, sizeof(T));
}

} // namespace ebp
