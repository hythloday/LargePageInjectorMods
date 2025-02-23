#include <easyhook.h>
#include <string>
#include "framework.h"
#include "Mallocsigmatch.hpp"
#include <mimalloc.h>
#include <mimalloc-new-delete.h>

import Configuration;

//DWORD gFreqOffset = 0;
//BOOL WINAPI myBeepHook(DWORD dwFreq, DWORD dwDuration)
//{
//	std::cout << "\n    BeepHook: ****All your beeps belong to us!\n\n";
//	return Beep(dwFreq + gFreqOffset, dwDuration);
//}

// EasyHook will be looking for this export to support DLL injection. If not found then 
// DLL injection will fail.
extern "C" void __declspec(dllexport) __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO * inRemoteInfo);

void* AllocatePageNearAddress(void* targetAddr)
{
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	const uint64_t PAGE_SIZE = sysInfo.dwPageSize;

	uint64_t startAddr = (uint64_t(targetAddr) & ~(PAGE_SIZE - 1)); //round down to nearest page boundary
	uint64_t minAddr = min(startAddr - 0x7FFFFF00, (uint64_t)sysInfo.lpMinimumApplicationAddress);
	uint64_t maxAddr = max(startAddr + 0x7FFFFF00, (uint64_t)sysInfo.lpMaximumApplicationAddress);

	uint64_t startPage = (startAddr - (startAddr % PAGE_SIZE));

	uint64_t pageOffset = 1;
	while (1)
	{
		uint64_t byteOffset = pageOffset * PAGE_SIZE;
		uint64_t highAddr = startPage + byteOffset;
		uint64_t lowAddr = (startPage > byteOffset) ? startPage - byteOffset : 0;

		bool needsExit = highAddr > maxAddr && lowAddr < minAddr;

		if (highAddr < maxAddr)
		{
			void* outAddr = VirtualAlloc((void*)highAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (outAddr)
				return outAddr;
		}

		if (lowAddr > minAddr)
		{
			void* outAddr = VirtualAlloc((void*)lowAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (outAddr != nullptr)
				return outAddr;
		}

		pageOffset++;

		if (needsExit)
		{
			break;
		}
	}

	return nullptr;
}

void WriteAbsoluteJump64(void* absJumpMemory, void* addrToJumpTo)
{
	uint8_t absJumpInstructions[] =
	{
	  0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //mov r10, addr
	  0x41, 0xFF, 0xE2 //jmp r10
	};

	uint64_t addrToJumpTo64 = (uint64_t)addrToJumpTo;
	memcpy(&absJumpInstructions[2], &addrToJumpTo64, sizeof(addrToJumpTo64));
	memcpy(absJumpMemory, absJumpInstructions, sizeof(absJumpInstructions));
}

void InstallHook(void* func2hook, void* payloadFunction)
{
	void* relayFuncMemory = AllocatePageNearAddress(func2hook);
	WriteAbsoluteJump64(relayFuncMemory, payloadFunction); //write relay func instructions

	//now that the relay function is built, we need to install the E9 jump into the target func,
	//this will jump to the relay function
	DWORD oldProtect;
	VirtualProtect(func2hook, 1024, PAGE_EXECUTE_READWRITE, &oldProtect);

	//32 bit relative jump opcode is E9, takes 1 32 bit operand for jump offset
	uint8_t jmpInstruction[5] = { 0xE9, 0x0, 0x0, 0x0, 0x0 };

	//to fill out the last 4 bytes of jmpInstruction, we need the offset between 
	//the relay function and the instruction immediately AFTER the jmp instruction
	const uint64_t relAddr = (uint64_t)relayFuncMemory - ((uint64_t)func2hook + sizeof(jmpInstruction));
	memcpy(jmpInstruction + 1, &relAddr, 4);

	//install the hook
	memcpy(func2hook, jmpInstruction, sizeof(jmpInstruction));
}

void HookIfSigFound(std::string moduleName, MiMallocReplacedFunctions function, void* replacementFunctionPointer) {
	mallocsigmatch sigmatcher;
	std::vector<void*> functionAdresses = sigmatcher.GetFunctionAdress(moduleName, function);

	for (void* functionAdress : functionAdresses) {
		if (functionAdress) {
			InstallHook(functionAdress, replacementFunctionPointer);
			//Beep(1000, 100);
		}
		else {
			Beep(250, 100);
		}
	}

}

void HookAllMallocFunctions(const std::string& ModuleName) {
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::malloc, mi_malloc);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::free, mi_free);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::free_base, mi_free);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::realloc, mi_realloc);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::calloc, mi_calloc);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::_strdup, mi_strdup);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::_msize, mi_usable_size);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::_recalloc, mi_recalloc);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::_wcsdup, mi_wcsdup);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::_aligned_malloc, mi_malloc_aligned);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::_aligned_free, mi_free);
	HookIfSigFound(ModuleName, MiMallocReplacedFunctions::operator_new, mi_new);
}

extern "C" void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO* inRemoteInfo)
{
	
	//mi_version() to intilse mi malloc.
	mi_version();

	//Attach Console to parent process
	AttachConsole(ATTACH_PARENT_PROCESS);

	//Load configuration
	Configuration config;

	config.loadFromFile(".\\LargePageInjectorMods.config");

	for (auto moduleName : config.modulesToPatch) {
		HookAllMallocFunctions(moduleName);
	}
	
	//Let the game run
	Beep(1000, 100);
    RhWakeUpProcess();


	return;
}