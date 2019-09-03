// vmmap.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <windows.h>
#include<Memoryapi.h>
//#include<TlHelp32.h>
#include<Tlhelp32.h>
#include<Psapi.h>
#include<map>
#include<tchar.h>
#include<wchar.h>
#include <iomanip>
#include<string>
#include<list>
#include<iostream>

#ifdef UNICODE
#define tstring wstring
#define tcout wcout
#define tcerr wcerr
#define tstrlen wcslen
#define to_tstring to_wstring
#else
#define tstring string
#define tcout cout
#define tcerr cerr
#define tstrlen strlen
#define to_tstring to_string 
#endif

#define M_r (PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY|PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY)
#define M_w (PAGE_EXECUTE_READWRITE | PAGE_READWRITE | PAGE_WRITECOPY)
#define M_x (PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)
#define M_noCFG PAGE_TARGETS_INVALID
#define M_noUpdate PAGE_TARGETS_NO_UPDATE

#define P_r 1        // cam read
#define P_w 2        // can write
#define P_x 4        // can exec
#define P_p 8        // private
#define P_C 16       // can cfg
#define P_K 32
#define P_R 64       // reverse

struct node {
	size_t AllocationBase=0;
	size_t ID=0;
	size_t type = 0;
};

struct reg {
	size_t base = 0;
	size_t end = 0;
	std::tstring type;             // heap stack image mapped private share
	std::tstring protect;
	std::tstring details;
};

std::list<node> heaps;
std::list<node>stacks;
std::list<node>modules;
std::list<node> all;

class process {
public:
	int pid;
	HANDLE hProcess;
	HANDLE hSnapshot;
	unsigned long long memorySize=0xffffffff;
	BOOL is_64 = true;

	process(int pid) {
		this->setToekn();
		this->pid = pid;
		this->hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,false,this->pid);
		if (NULL == hProcess) {
			return;
		}
		BOOL is_32 = false;
		if (!IsWow64Process(this->hProcess, &is_32)) {
			this->is_64 = false;
			return;
		}
		if (!is_32) {
			this->is_64 = true;
			this->memorySize = 0xffffffffffffffff;
		}
		this->parseStackHeap();
    }
	
	void setToekn() {
		HANDLE hToken;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) 
		{
			return;
		}

		LUID luid;
		if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
			return;
		}

		TOKEN_PRIVILEGES tp;
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = luid;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), (PTOKEN_PRIVILEGES)NULL, (PDWORD)NULL)) {
			return;
		}
		if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
			return;
		}
	}

	const std::tstring mapPath(unsigned long long addr) {
		std::tstring filepath;
		filepath.assign(MAX_PATH, '\0');
		unsigned long long filepath_length = (unsigned long long)GetMappedFileName(this->hProcess, (LPVOID)addr, (LPWSTR)&*filepath.begin(), filepath.size());
		filepath.resize(filepath_length);
		if (filepath.size()) {
			std::map<std::tstring, std::tstring> MapDevicePathToDrivePathCache;
			if (MapDevicePathToDrivePathCache.size() == 0) {
				// Construct the cache of device paths to drive letters (e.g.
				// "\Device\HarddiskVolume1\" -> "C:\", "\Device\CdRom0\" -> "D:\").
				std::tstring drives(27, '\0');
				unsigned long long drives_length =
					GetLogicalDriveStrings(drives.size(), &*drives.begin());
				if (drives_length) {
					drives.resize(drives_length);
					std::tstring::size_type start = 0;
					std::tstring::size_type end = drives.find(_T('\0'));
					while (end < drives.size()) {
						std::tstring drive = drives.substr(start, end - start - 1);
						std::tstring device(MAX_PATH, '\0');
						unsigned long long device_length = QueryDosDevice(drive.c_str(), &*device.begin(), device.size());
						if (device_length) {
							device.resize(device_length - 2);
							device += '\\';
							drive += '\\';
							MapDevicePathToDrivePathCache[device] = drive;
						}
						start = end + 1;
						end = drives.find(_T('\0'), start);
					}
				}
			}

			// Replace a matching device filepath with the appropriate drive letter.
			for (std::map<std::tstring, std::tstring>::iterator map =
				MapDevicePathToDrivePathCache.begin();
				map != MapDevicePathToDrivePathCache.end(); map++) {
				if (filepath.compare(0, (*map).first.size(), (*map).first) == 0) {
					return (*map).second + filepath.substr((*map).first.size());
				}
			}
		}
		return filepath;
	}
	
	void parseStackHeap(){
            this->hSnapshot = CreateToolhelp32Snapshot(
                TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32 | TH32CS_SNAPTHREAD |
                    TH32CS_SNAPHEAPLIST,
                this->pid);
            THREADENTRY32 thread = {sizeof(thread)};
            if (Thread32First(this->hSnapshot, &thread)) {
                do {
                    if (thread.th32OwnerProcessID == this->pid) {
                        HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION |
                                                        THREAD_GET_CONTEXT |
                                                        THREAD_SUSPEND_RESUME,
                                                    false, thread.th32ThreadID);
                        if (NULL != hThread) {
                            if (GetCurrentThreadId() != thread.th32ThreadID) {
                                SuspendThread(hThread);
                            }
                            CONTEXT context = {};
                            context.ContextFlags = CONTEXT_CONTROL;
                            if (GetThreadContext(hThread, &context)) {
                                unsigned long long sp = 0;
		#ifdef _WIN64
                                sp = context.Rsp;
		#elif _WIN32
                                sp = context.Esp;
		#endif
                                MEMORY_BASIC_INFORMATION info = {0};
                                unsigned long long info_size =
                                    (unsigned long long)VirtualQueryEx(
                                        this->hProcess, (void *)sp, &info,
                                        sizeof(info));
                                node *c = new node();
                                c->AllocationBase = (size_t)info.AllocationBase;
                                c->ID = (size_t)thread.th32ThreadID;                  //stack
								c->type = 0;
                                stacks.push_back(*c);
								all.push_back(*c);
                            }
                            if (GetCurrentThreadId() != thread.th32ThreadID) {
                                ResumeThread(hThread);
                            }
                        }
                    }
                } while (Thread32Next(this->hSnapshot, &thread));
            }

            HEAPLIST32 heap = {sizeof(heap)};
            unsigned long heap_index = 0;
            if (Heap32ListFirst(this->hSnapshot, &heap)) {
                do {  // heap.th32HeapID is base. adddr
                    node *c = new node();
                    c->AllocationBase = heap.th32HeapID;
                    c->ID = heap_index++;
					c->type = 1;
                    heaps.push_back(*c);
					all.push_back(*c);

                } while (Heap32ListNext(this->hSnapshot, &heap));
            }
			MODULEENTRY32 module = { sizeof(module) };
			if (Module32First(hSnapshot, &module)) {
				do {
					node *c = new node();
					c->AllocationBase= (size_t)module.modBaseAddr;
					c->type = 2;
					modules.push_back(*c);
					all.push_back(*c);
				} while (Module32Next(hSnapshot, &module));
			}
			auto com = [](const node &node1, const node &node2) {
				return (bool)(node1.AllocationBase <=
								node2.AllocationBase);
			};
			// heaps.sort(com);
			// stacks.sort(com);
			// modules.sort(com);
			all.sort(com);
        }
	
	


};

process *p;

class mapReg {
public:
	size_t base = 0;
	size_t end = 1;
	std::list<reg> regs;

	BOOL addReg(MEMORY_BASIC_INFORMATION &info) {
		if (this->end == 1) {
			this->base = (size_t)info.AllocationBase;
			this->end = this->base;             
		}
		if ((size_t)info.AllocationBase == this->base) {

			reg *c = new reg();
			c->base = (size_t)info.BaseAddress;
			c->end = (size_t)info.BaseAddress + (size_t)info.RegionSize;

			std::tstring _type;
			std::tstring _details;
			_type.clear();
			_details.clear();
			this->parse_Type_Details(info,_type,_details);

			c->type=_type;
			c->protect = this->parseProtect(info);
			c->details = _details;
			
			(this->regs).push_back(*c);
			this->end += (size_t)info.RegionSize;
			// std::list<node>::iterator itr;
			// std::tcout << "stack" << std::endl;
			// for (itr = stacks.begin(); itr != stacks.end(); itr++) {
			// 	std::tcout << std::hex << (*itr).AllocationBase << std::endl;
			// }
			// std::tcout << "heap" << std::endl;
			// for (itr = heaps.begin(); itr != heaps.end(); itr++) {
			// 	std::tcout << std::hex << (*itr).AllocationBase << std::endl;
			// }
			// std::tcout << "ptr" << std::endl;
			// std::tcout << std::hex << this->base << " " << this->end << std::endl;
			// std::tcout << std::hex << (*regs.begin()).base << " " << (*regs.begin()).end << " " << (*regs.begin()).type << " " << (*regs.begin()).protect << " " << (*regs.begin()).details << std::endl;
			return true;
		}
		return false;
	}
	
	std::tstring parseProtect(MEMORY_BASIC_INFORMATION &info) {
		// rwxpCKR
		std::tstring rs;

		if (info.Protect & M_r) 
			rs.push_back('r'); 
		else 
			rs.push_back('-');
		if (info.Protect&M_w) 
			rs.push_back('w'); 
		else 
			rs.push_back('-');
		if (info.Protect&M_x) 
			rs.push_back('x'); 
		else 
			rs.push_back('-');
		if (info.Type & MEM_PRIVATE) 
			rs.push_back('p'); 
		else 
			rs.push_back('-');
		if (info.Protect&M_noCFG) 
			rs.push_back('-'); 
		else 
			rs.push_back('C');
		if (info.Protect&M_noUpdate) 
			rs.push_back('K'); 
		else 
			rs.push_back('-');
		if (info.State & MEM_RESERVE) 
			rs.push_back('R'); 
		else 
			rs.push_back('-');
		return rs;
	}
	
	void parse_Type_Details(MEMORY_BASIC_INFORMATION &info, std::tstring &tp,std::tstring &details) {
		// image
		// stack
		// heap
		// mapped
		if (info.Type == MEM_IMAGE) {
			tp=L"image";                     // 0: image
			details=this->mapPath(info);
		}
		else {
			// private: mapped, heap, stack
			size_t base=(size_t)info.AllocationBase;
			std::list<node>::iterator itr;
			for(itr=stacks.begin();itr!=stacks.end();itr++){
				if((size_t)(*itr).AllocationBase==base){
					tp=L"stack";    // stack
					details=L"Thread ID: "+std::to_tstring((size_t)(*itr).ID);
					return;
				}
			}
			for(itr=heaps.begin();itr!=heaps.end();itr++){
				if((size_t)(*itr).AllocationBase==base){
					tp=L"heap";     // heap
					details=L"Heap ID: "+std::to_tstring((size_t)(*itr).ID);
					return;
				}
			}
			if (info.Type == MEM_MAPPED) {
					tp = L"mapped";              // 2: mapped
					details = this->mapPath(info);
					return;
			}
			if (!(info.Type&MEM_PRIVATE)) {
					tp = L"Shareable";            // 1: shareable
					details = this->mapPath(info);
					return;
			}
			tp=L"private"; // private
			details = this->mapPath(info);
		}
	}

    const std::tstring mapPath(MEMORY_BASIC_INFORMATION &info) {
			size_t addr=(size_t)info.BaseAddress;
            std::tstring filepath;
            filepath.assign(MAX_PATH, '\0');
            unsigned long long filepath_length =
                (unsigned long long)GetMappedFileName(
                    p->hProcess, (LPVOID)addr, (LPWSTR) & *filepath.begin(),
                    filepath.size());

            filepath.resize(filepath_length);
            if (filepath.size()) {
                std::map<std::tstring, std::tstring>
                    MapDevicePathToDrivePathCache;
                if (MapDevicePathToDrivePathCache.size() == 0) {
                    std::tstring drives(27, '\0');
                    unsigned long long drives_length =
                        GetLogicalDriveStrings(drives.size(), &*drives.begin());
                    if (drives_length) {
                        drives.resize(drives_length);
                        std::tstring::size_type start = 0;
                        std::tstring::size_type end = drives.find(_T('\0'));
                        while (end < drives.size()) {
                            std::tstring drive =
                                drives.substr(start, end - start - 1);
                            std::tstring device(MAX_PATH, '\0');
                            unsigned long long device_length = QueryDosDevice(
                                drive.c_str(), &*device.begin(), device.size());
                            if (device_length) {
                                device.resize(device_length - 2);
                                device += '\\';
                                drive += '\\';
                                MapDevicePathToDrivePathCache[device] = drive;
                            }
                            start = end + 1;
                            end = drives.find(_T('\0'), start);
                        }
                    }
                }

                // Replace a matching device filepath with the appropriate drive
                // letter.
                for (std::map<std::tstring, std::tstring>::iterator map =
                         MapDevicePathToDrivePathCache.begin();
                     map != MapDevicePathToDrivePathCache.end(); map++) {
                    if (filepath.compare(0, (*map).first.size(),
                                         (*map).first) == 0) {
                        return (*map).second +
                               filepath.substr((*map).first.size());
                    }
                }
            }
            return filepath;
	}
};

void vmmap(int pid,int addr=0) {
	MEMORY_BASIC_INFORMATION info = { 0 };
	p = new process(pid);
	std::list<mapReg> mapRegs;
	size_t p_size = 0;
	mapReg *a = new mapReg();

#ifdef _WIN64
	std::list<node>::iterator itr0;
	for (itr0 = all.begin(); itr0 != all.end(); itr0++) {
		size_t AlloactionBase = (*itr0).AllocationBase;
		for (size_t i = AlloactionBase; i < p->memorySize; i += p_size) {
			unsigned long long info_size = (unsigned long long)VirtualQueryEx(p->hProcess, (void*)i, &info, sizeof(info));
			p_size = info.RegionSize;
			if (!(a->addReg(info))) {
				mapRegs.push_back(*a);
				a = new mapReg();
				break;
			}
		}
	}

#elif _WIN32
	for (unsigned long long i = 0; i <= p->memorySize; i += p_size) {
		unsigned long long info_size = (unsigned long long)VirtualQueryEx(p->hProcess, (void*)i, &info, sizeof(info));
		p_size = info.RegionSize;
		if (info.State == MEM_FREE) {
			continue;
		}
		if (!(a->addReg(info))) {
			mapRegs.push_back(*a);                
			a = new mapReg();
		}
	}
#endif

	std::list<mapReg>::iterator itr;
	for (itr = mapRegs.begin(); itr != mapRegs.end(); itr++) {
		std::list<reg>::iterator itr1;
		for (itr1 = (*itr).regs.begin(); itr1 != (*itr).regs.end(); itr1++) {
			// std::tcout << std::hex << (*itr1).base << " " << (*itr1).end << " " << (*itr1).type << " " << (*itr1).protect << " " << (*itr1).details << std::endl;
			std::tcout << std::setw(16) << std::setfill(L'0') << std::right << std::hex << (*itr1).base;
            std::tcout << "-";
            std::tcout << std::setw(16) << std::setfill(L'0') << std::right
                       << std::hex <<(*itr1).end;
            std::tcout << "  " << std::setw(11) << std::setfill(L' ')
                       << std::left <<(*itr1).type;
            std::tcout << "  " << std::setw(7) << std::setfill(L' ')
                       << std::left <<(*itr1).protect;
            std::tcout << "  " << std::setfill(L' ') << std::left
                       << (*itr1).details;
			std::tcout << std::endl;
            }
	}
}

int main(int argc,char**argv)
{
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);
	if(argv[1]){
		if (!strcmp(argv[1], "-h")) {
			std::tcout << "vmmap -pid proc_pid" << std::endl;
			// std::tcout << "vmmap -pid proc_pid -addr an_addreess" << std::endl;
		}
		if (!strcmp(argv[1], "-pid")) {
			if(argv[2]){
			int pid = std::stoi(argv[2]);
			size_t addr=0;
			if (argv[3]) {
				if (!strcmp(argv[3], "-addr")) {
					addr = std::stoi(argv[4]);
				}
			}
			vmmap(pid,addr);
			}
			
		}
	}
}