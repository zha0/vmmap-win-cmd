
#include "stdafx.h"
#include "commandline_options.h"
#include "process_list.h"
#include "process_memory.h"
#include "string_utils.h"
#include <typeinfo>
int _tmain(int argc, _TCHAR* argv[])
{
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);
	
	commandline_options options(L"h ?,:pid,all,most");
	
	if (options.has(L"h")) {
		std::tcout << "Gathers and reports on detailed memory usage by one or more process." << std::endl;
		std::tcout << std::endl;
		std::tcout << "command:"<<std::endl;
		std::tcout << "	VMMAP -pid <pid>"<<std::endl;
		std::tcout << "	VMMAP -all -pid <pid>" << std::endl;
		std::tcout << "	VMMAP -most -pid <pid>" << std::endl;
		std::tcout << std::endl;
		std::tcout << "Access column key:" << std::endl;
		std::tcout << "  r  Read       G  Guard Page" << std::endl;
		std::tcout << "  w  Write      C  No Cache" << std::endl;
		std::tcout << "  x  Execute    W  Write Combine" << std::endl;
		std::tcout << "  c  Copy on Write" << std::endl;
		return 0;
	}
	std::list<process> processes;
	{
		if (options.has(L"pid")) {
			const std::list<std::tstring> pids = options.gets(L"pid");
			for (std::list<std::tstring>::const_iterator pid = pids.begin(); pid != pids.end(); pid++) {
				if ((*pid).compare(L"self") == 0) {
					processes.push_back(process(GetCurrentProcessId()));

				} else {
					processes.push_back(process(_wtoi((*pid).c_str())));
				}
			}
		}
		processes.sort(std::less<DWORD>());
	}
	int just_show_important_mes = 1;
	int show_most_mes = 0;

	if (options.has(L"all")) {
		just_show_important_mes = 0;
	}
	if (options.has(L"most")) {
		show_most_mes = 1;
	}

	#define SUMMARY_ROW_SIZE (100)
	#define SUMMARY_SIZE (2 * SUMMARY_ROW_SIZE)
	{
		for (std::list<process>::const_iterator process = processes.begin(); process != processes.end(); process++) {
			process_memory memory(*process);
			for (std::map<unsigned long long, process_memory_group>::const_iterator it_group = memory.groups().begin(); it_group != memory.groups().end(); it_group++) {
				const process_memory_group& group = (*it_group).second;
				if ((group.type() == PMGT_FREE || group.type() == PMGT_UNUSABLE)) continue;

				std::wstring  group_type = format_process_memory_group_type(group.type());
				// show important

				if ((just_show_important_mes && ((!group_type.compare(L"Image"))|| (!group_type.compare(L"Heap"))|| (!group_type.compare(L"Stack"))))
					// show most
					|| (show_most_mes && 
					((!group_type.compare(L"Image")) || (!group_type.compare(L"Heap")) || (!group_type.compare(L"Stack"))|| (!group_type.compare(L"Private"))))

					|| !just_show_important_mes) // show all
				{
					std::tcout << std::setw(16) << std::setfill(L'0') << std::right << std::hex << group.base();
					std::tcout << "-";
					std::tcout << std::setw(16) << std::setfill(L'0') << std::right << std::hex << group.size() + group.base();
					std::tcout << "  " << std::setw(11) << std::setfill(L' ') << std::left << format_process_memory_group_type(group.type());
					std::tcout << "  " << std::setw(7) << std::setfill(L' ') << std::left << group.protection_str();
					std::tcout << "  " << std::setfill(L' ') << std::left << group.details();
					std::tcout << "\n";
				}
			}
		}
	}
	return 0;
}
