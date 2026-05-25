#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

using uc_err = int;

static constexpr uint32_t UC_ARCH_X86 = 1;
static constexpr uint32_t UC_MODE_64 = 1u << 3;

static constexpr int UC_X86_REG_RAX = 35;
static constexpr int UC_X86_REG_RBX = 36;
static constexpr int UC_X86_REG_RCX = 37;
static constexpr int UC_X86_REG_RDX = 38;
static constexpr int UC_X86_REG_RSI = 39;
static constexpr int UC_X86_REG_RDI = 40;
static constexpr int UC_X86_REG_RBP = 41;
static constexpr int UC_X86_REG_RSP = 42;
static constexpr int UC_X86_REG_R8 = 43;
static constexpr int UC_X86_REG_R9 = 44;
static constexpr int UC_X86_REG_R10 = 45;
static constexpr int UC_X86_REG_R11 = 46;
static constexpr int UC_X86_REG_R12 = 47;
static constexpr int UC_X86_REG_R13 = 48;
static constexpr int UC_X86_REG_R14 = 49;
static constexpr int UC_X86_REG_R15 = 50;
static constexpr int UC_X86_REG_RIP = 51;
static constexpr int UC_X86_REG_EFLAGS = 65;

static constexpr uint32_t UC_PROT_NONE = 0;
static constexpr uint32_t UC_PROT_READ = 1;
static constexpr uint32_t UC_PROT_WRITE = 2;
static constexpr uint32_t UC_PROT_EXEC = 4;

using uc_open_fn = uc_err (*)(uint32_t arch, uint32_t mode, void** eng);
using uc_close_fn = uc_err (*)(void* eng);
using uc_mem_map_fn = uc_err (*)(void* eng, uint64_t addr, size_t size, uint32_t perms);
using uc_mem_write_fn = uc_err (*)(void* eng, uint64_t addr, const uint8_t* bytes, size_t size);
using uc_mem_read_fn = uc_err (*)(void* eng, uint64_t addr, uint8_t* bytes, size_t size);
using uc_reg_write_fn = uc_err (*)(void* eng, int regid, const uint8_t* value);
using uc_reg_read_fn = uc_err (*)(void* eng, int regid, uint8_t* value);
using uc_emu_start_fn = uc_err (*)(void* eng, uint64_t begin, uint64_t until, uint64_t timeout, size_t count);
using uc_strerror_fn = const char* (*)(uc_err err);

class DecryptEmulator {
public:
    DecryptEmulator()
        : m_hMod(nullptr),
          m_engine(nullptr),
          m_uc_open(nullptr),
          m_uc_close(nullptr),
          m_uc_mem_map(nullptr),
          m_uc_mem_write(nullptr),
          m_uc_mem_read(nullptr),
          m_uc_reg_write(nullptr),
          m_uc_reg_read(nullptr),
          m_uc_emu_start(nullptr),
          m_uc_strerror(nullptr),
          m_loaded(false),
          m_verbose(false) {
        m_hMod = LoadLibraryA("unicorn.dll");
        if (!m_hMod) {
            return;
        }

        m_uc_open = reinterpret_cast<uc_open_fn>(GetProcAddress(m_hMod, "uc_open"));
        m_uc_close = reinterpret_cast<uc_close_fn>(GetProcAddress(m_hMod, "uc_close"));
        m_uc_mem_map = reinterpret_cast<uc_mem_map_fn>(GetProcAddress(m_hMod, "uc_mem_map"));
        m_uc_mem_write = reinterpret_cast<uc_mem_write_fn>(GetProcAddress(m_hMod, "uc_mem_write"));
        m_uc_mem_read = reinterpret_cast<uc_mem_read_fn>(GetProcAddress(m_hMod, "uc_mem_read"));
        m_uc_reg_write = reinterpret_cast<uc_reg_write_fn>(GetProcAddress(m_hMod, "uc_reg_write"));
        m_uc_reg_read = reinterpret_cast<uc_reg_read_fn>(GetProcAddress(m_hMod, "uc_reg_read"));
        m_uc_emu_start = reinterpret_cast<uc_emu_start_fn>(GetProcAddress(m_hMod, "uc_emu_start"));
        m_uc_strerror = reinterpret_cast<uc_strerror_fn>(GetProcAddress(m_hMod, "uc_strerror"));

        m_loaded = m_uc_open && m_uc_close && m_uc_mem_map && m_uc_mem_write &&
                   m_uc_mem_read && m_uc_reg_write && m_uc_reg_read &&
                   m_uc_emu_start && m_uc_strerror;
    }

    ~DecryptEmulator() {
        CloseEngine();
        if (m_hMod) {
            FreeLibrary(m_hMod);
            m_hMod = nullptr;
        }
    }

    bool IsLoaded() const {
        return m_loaded;
    }

    bool InitEngine() {
        if (!m_loaded) {
            std::printf("[Unicorn] unicorn.dll is not loaded or exports are missing\n");
            return false;
        }

        CloseEngine();

        void* engine = nullptr;
        uc_err err = m_uc_open(UC_ARCH_X86, UC_MODE_64, &engine);
        if (err != 0) {
            // Official Unicorn headers use UC_ARCH_X86 = 4. Keep the requested
            // local constant above, but tolerate DLLs built with the upstream enum.
            err = m_uc_open(4, UC_MODE_64, &engine);
        }
        if (err != 0) {
            PrintError("uc_open", err);
            return false;
        }

        m_engine = engine;
        return true;
    }

    void CloseEngine() {
        if (m_engine && m_uc_close) {
            m_uc_close(m_engine);
            m_engine = nullptr;
        }
    }

    bool EmulateAt(uint64_t code_addr, const uint8_t* code, size_t code_size,
                   uint64_t& result, bool verbose = false) {
        if (!PrepareEngine(code_addr, code, code_size, verbose)) {
            return false;
        }
        return RunAndReadRax(code_addr, code_addr + code_size, result);
    }

    bool EmulateGetGlobalKey(const uint8_t* code, size_t code_size,
                             uint64_t base_addr, uint64_t& rax_result,
                             bool verbose = false) {
        return EmulateAt(base_addr, code, code_size, rax_result, verbose);
    }

    bool EmulateDecryptComponent(const uint8_t* code, size_t code_size,
                                 uint64_t parent, uint8_t idx,
                                 uint64_t table_entry1, uint64_t table_entry2,
                                 uint64_t& result, bool verbose = false) {
        static constexpr uint64_t kCodeAddr = 0x50020000;
        static constexpr uint64_t kDataAddr = 0x60000000;

        if (!PrepareEngine(kCodeAddr, code, code_size, verbose)) {
            return false;
        }

        if (!WriteMemory(kDataAddr, parent, "decrypt component input")) {
            return false;
        }
        if (!WriteMemory(kDataAddr + 0x08, table_entry1, "decrypt component table_entry1")) {
            return false;
        }
        if (!WriteMemory(kDataAddr + 0x10, table_entry2, "decrypt component table_entry2")) {
            return false;
        }
        if (!WriteMemory(kDataAddr + 0x18, idx, "decrypt component idx")) {
            return false;
        }

        if (!WriteReg64(UC_X86_REG_RCX, parent, "RCX") ||
            !WriteReg64(UC_X86_REG_RDX, static_cast<uint64_t>(idx), "RDX") ||
            !WriteReg64(UC_X86_REG_R8, table_entry1, "R8") ||
            !WriteReg64(UC_X86_REG_R9, table_entry2, "R9")) {
            return false;
        }

        return RunAndReadRax(kCodeAddr, kCodeAddr + code_size, result);
    }

    bool EmulateGetParent(const uint8_t* code, size_t code_size,
                          uint64_t encrypted, uint64_t& result,
                          bool verbose = false) {
        static constexpr uint64_t kCodeAddr = 0x50010000;
        static constexpr uint64_t kDataAddr = 0x60000000;

        if (!PrepareEngine(kCodeAddr, code, code_size, verbose)) {
            return false;
        }

        if (!WriteMemory(kDataAddr, encrypted, "get parent input")) {
            return false;
        }

        if (!WriteReg64(UC_X86_REG_RAX, encrypted, "RAX") ||
            !WriteReg64(UC_X86_REG_RCX, encrypted, "RCX")) {
            return false;
        }

        return RunAndReadRax(kCodeAddr, kCodeAddr + code_size, result);
    }

private:
    static constexpr uint64_t kPageSize = 0x1000;
    static constexpr uint64_t kStackBase = 0x7FF00000;
    static constexpr size_t kStackSize = 0x100000;
    static constexpr uint64_t kStackPointer = kStackBase + 0xF0000;
    static constexpr uint64_t kDataBase = 0x60000000;
    static constexpr size_t kDataSize = 0x100000;

    static uint64_t AlignDown(uint64_t value) {
        return value & ~(kPageSize - 1);
    }

    static uint64_t AlignUp(uint64_t value) {
        return (value + kPageSize - 1) & ~(kPageSize - 1);
    }

    static int RuntimeRegId(int regid) {
        // Official Unicorn x86 register enum values differ from the fixed IDs
        // requested above. Translate at the DLL boundary so callers can use the
        // local constants without breaking upstream Unicorn builds.
        switch (regid) {
            case UC_X86_REG_RAX: return 35;
            case UC_X86_REG_RBX: return 37;
            case UC_X86_REG_RCX: return 38;
            case UC_X86_REG_RDX: return 40;
            case UC_X86_REG_RSI: return 43;
            case UC_X86_REG_RDI: return 39;
            case UC_X86_REG_RBP: return 36;
            case UC_X86_REG_RSP: return 44;
            case UC_X86_REG_R8: return 106;
            case UC_X86_REG_R9: return 107;
            case UC_X86_REG_R10: return 108;
            case UC_X86_REG_R11: return 109;
            case UC_X86_REG_R12: return 110;
            case UC_X86_REG_R13: return 111;
            case UC_X86_REG_R14: return 112;
            case UC_X86_REG_R15: return 113;
            case UC_X86_REG_RIP: return 41;
            case UC_X86_REG_EFLAGS: return 25;
            default: return regid;
        }
    }

    bool PrepareEngine(uint64_t code_addr, const uint8_t* code, size_t code_size,
                       bool verbose) {
        m_verbose = verbose;
        if (!code || code_size == 0) {
            std::printf("[Unicorn] empty code buffer\n");
            return false;
        }

        if (!InitEngine()) {
            return false;
        }

        const uint64_t code_map_base = AlignDown(code_addr);
        const uint64_t code_map_end = AlignUp(code_addr + code_size);
        const size_t code_map_size = static_cast<size_t>(code_map_end - code_map_base);

        if (!MapRegion(code_map_base, code_map_size, UC_PROT_READ | UC_PROT_EXEC, "code")) {
            return false;
        }
        if (!MapRegion(kStackBase, kStackSize, UC_PROT_READ | UC_PROT_WRITE, "stack")) {
            return false;
        }
        if (!MapRegion(kDataBase, kDataSize, UC_PROT_READ | UC_PROT_WRITE, "data")) {
            return false;
        }

        uc_err err = m_uc_mem_write(m_engine, code_addr, code, code_size);
        if (err != 0) {
            PrintError("uc_mem_write(code)", err);
            return false;
        }

        const uint64_t return_addr = code_addr + code_size;
        if (!WriteMemory(kStackPointer, return_addr, "synthetic return address")) {
            return false;
        }
        if (!WriteReg64(UC_X86_REG_RSP, kStackPointer, "RSP")) {
            return false;
        }
        return true;
    }

    bool MapRegion(uint64_t addr, size_t size, uint32_t perms, const char* name) {
        const uc_err err = m_uc_mem_map(m_engine, addr, size, perms);
        if (err != 0) {
            char op[96] = {};
            std::snprintf(op, sizeof(op), "uc_mem_map(%s 0x%llX, 0x%zX)",
                          name,
                          static_cast<unsigned long long>(addr),
                          size);
            PrintError(op, err);
            return false;
        }
        return true;
    }

    template <typename T>
    bool WriteMemory(uint64_t addr, const T& value, const char* op_name) {
        const uc_err err = m_uc_mem_write(
            m_engine,
            addr,
            reinterpret_cast<const uint8_t*>(&value),
            sizeof(T));
        if (err != 0) {
            PrintError(op_name, err);
            return false;
        }
        return true;
    }

    bool WriteReg64(int regid, uint64_t value, const char* name) {
        const uc_err err = m_uc_reg_write(
            m_engine,
            RuntimeRegId(regid),
            reinterpret_cast<const uint8_t*>(&value));
        if (err != 0) {
            PrintError(name, err);
            return false;
        }
        return true;
    }

    bool RunAndReadRax(uint64_t begin, uint64_t until, uint64_t& result) {
        if (m_verbose) {
            std::printf("[Unicorn] Emulating 0x%llX-0x%llX...\n",
                        static_cast<unsigned long long>(begin),
                        static_cast<unsigned long long>(until));
        }

        uc_err err = m_uc_emu_start(m_engine, begin, until, 10000000, 0);
        if (err != 0) {
            PrintError("uc_emu_start", err);
            return false;
        }

        err = m_uc_reg_read(
            m_engine,
            RuntimeRegId(UC_X86_REG_RAX),
            reinterpret_cast<uint8_t*>(&result));
        if (err != 0) {
            PrintError("uc_reg_read(RAX)", err);
            return false;
        }
        return true;
    }

    void PrintError(const char* op, uc_err err) const {
        const char* message = m_uc_strerror ? m_uc_strerror(err) : "unknown error";
        std::printf("[Unicorn] %s failed: %s (%d)\n", op, message, err);
    }

    HMODULE m_hMod;
    void* m_engine;

    uc_open_fn m_uc_open;
    uc_close_fn m_uc_close;
    uc_mem_map_fn m_uc_mem_map;
    uc_mem_write_fn m_uc_mem_write;
    uc_mem_read_fn m_uc_mem_read;
    uc_reg_write_fn m_uc_reg_write;
    uc_reg_read_fn m_uc_reg_read;
    uc_emu_start_fn m_uc_emu_start;
    uc_strerror_fn m_uc_strerror;

    bool m_loaded;
    bool m_verbose;
};
