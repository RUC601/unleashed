#pragma once
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <TlHelp32.h>
#include "leechcore.h"
#include "vmmdll.h"

#ifndef LOG
#define LOG(...) printf(__VA_ARGS__)
#endif

enum class DmaDeviceType
{
	FTD3XX = 0,
	FTD3XXWU,
	RAWTCP,
	HVSAVEDSTATE
};

class Memory
{
private:
	struct CurrentProcessInformation
	{
		int PID = 0;
		size_t base_address = 0;
		size_t base_size = 0;
		std::string process_name = "";
	};

	CurrentProcessInformation current_process{};

	std::atomic<bool> DMA_INITIALIZED{ false };
	std::atomic<bool> PROCESS_INITIALIZED{ false };

	HMODULE hVMM = nullptr;
	HMODULE hFTD3XX = nullptr;
	HMODULE hFTD3XXWU = nullptr;
	HMODULE hLEECHCORE = nullptr;
	HMODULE hLEECHCORE_DEVICE_RAWTCP = nullptr;
	HMODULE hLEECHCORE_DEVICE_HVSAVEDSTATE = nullptr;
	HMODULE hLEECHCORE_DRIVER = nullptr;
	HMODULE hHELPER64 = nullptr;

	DmaDeviceType m_deviceType = DmaDeviceType::FTD3XX;
	std::string m_rawTcpHost = "";
	std::string m_deviceString = "fpga://algo=0";

	bool EnsureRuntimeLibrariesLoaded();
	bool DumpMemoryMap(const std::string& outputPath, bool debug = false);
	bool SetFPGA();
	std::string BuildDeviceString(DmaDeviceType deviceType) const;
	const char* GetDeviceTypeName(DmaDeviceType deviceType) const;

public:
	struct ProcessLookupResult
	{
		DWORD pid = 0;
		std::string name = "";

		explicit operator bool() const { return pid != 0; }
	};

	Memory() = default;
	~Memory();

	// -- Initialization --
	bool LoadDmaDeviceConfig(const std::string& configPath = "");
	DmaDeviceType GetDmaDeviceType() const { return m_deviceType; }
	const std::string& GetDmaDeviceString() const { return m_deviceString; }
	bool InitDma(DmaDeviceType deviceType, bool memMap = true, bool debug = false);
	bool InitDma(bool memMap = true, bool debug = false);
	bool AttachToProcess(const std::string& process_name, bool applyCr3Fix = true);
	bool Init(std::string process_name, bool memMap = true, bool debug = false);
	void DetachProcess();
	void CloseDma();

	// -- Process information --
	bool IsProcessInitialized() const { return PROCESS_INITIALIZED.load(); }
	int GetCurrentProcessId() const { return PROCESS_INITIALIZED.load() ? current_process.PID : 0; }
	DWORD GetPidFromName(const std::string& process_name);
	std::vector<int> GetPidListFromName(const std::string& name);
	ProcessLookupResult FindProcessByNamePrefix(const std::string& namePrefix);
	ProcessLookupResult FindProcessByPrefix(const std::string& prefix) { return FindProcessByNamePrefix(prefix); }
	std::vector<std::string> GetModuleList(const std::string& process_name);
	VMMDLL_PROCESS_INFORMATION GetProcessInformation();
	size_t GetBaseDaddy(std::string module_name);
	size_t GetBaseSize(std::string module_name);
	bool FixCr3();

	// -- Export / Import resolution --
	uintptr_t GetExportTableAddress(std::string import, std::string process, std::string module);
	uintptr_t GetImportTableAddress(std::string import, std::string process, std::string module);

	// -- Memory dumping / scanning --
	bool DumpMemory(uintptr_t address, std::string path);
	uint64_t FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID = 0);

	// -- Direct read (raw) --
	bool Read(uintptr_t address, void* buffer, size_t size) const;
	bool Read(uintptr_t address, void* buffer, size_t size, int pid) const;

	// -- Direct read (typed) --
	template <typename T>
	T Read(uint64_t address) const
	{
		T buffer{};
		Read(static_cast<uintptr_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T));
		return buffer;
	}

	template <typename T>
	T Read(uint64_t address, int pid) const
	{
		T buffer{};
		Read(static_cast<uintptr_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T), pid);
		return buffer;
	}

	// -- Direct write (raw) --
	bool Write(uintptr_t address, const void* buffer, size_t size) const;
	bool Write(uintptr_t address, const void* buffer, size_t size, int pid) const;

	// -- Direct write (typed) --
	template <typename T>
	bool Write(uint64_t address, const T& value) const
	{
		return Write(static_cast<uintptr_t>(address), reinterpret_cast<const void*>(&value), sizeof(T));
	}

	template <typename T>
	bool Write(uint64_t address, const T& value, int pid) const
	{
		return Write(static_cast<uintptr_t>(address), reinterpret_cast<const void*>(&value), sizeof(T), pid);
	}

	// -- Convenience aliases (raw) --
	bool ReadMemory(uintptr_t address, void* buffer, size_t size) const { return Read(address, buffer, size); }
	bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const { return Write(address, buffer, size); }

	// -- Convenience aliases (typed) --
	template <typename T>
	bool ReadMemory(uint64_t address, T& value) const
	{
		return Read(static_cast<uintptr_t>(address), reinterpret_cast<void*>(&value), sizeof(T));
	}

	template <typename T>
	bool WriteMemory(uint64_t address, const T& value) const
	{
		return Write<T>(address, value);
	}

	// -- Scatter read --
	VMMDLL_SCATTER_HANDLE CreateScatterHandle() const;
	VMMDLL_SCATTER_HANDLE CreateScatterHandle(int pid) const;
	void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);
	void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);
	void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size, DWORD* bytesRead);

	template <typename T>
	void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, T* buffer)
	{
		AddScatterReadRequest(handle, address, reinterpret_cast<void*>(buffer), sizeof(T));
	}

	template <typename T>
	void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, T* buffer, DWORD* bytesRead)
	{
		AddScatterReadRequest(handle, address, reinterpret_cast<void*>(buffer), sizeof(T), bytesRead);
	}

	bool ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0);

	// -- FPGA handle --
	VMM_HANDLE vHandle = nullptr;
};

inline Memory mem;
