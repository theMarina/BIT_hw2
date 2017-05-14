#include "pin.H"

#include <fstream>
#include <string>
#include <set>

typedef std::pair<UINT32, unsigned int> func_t;
struct more_func{ bool operator() (const func_t& x, const func_t& y)
	{return (x.second  > y.second) || 
		(x.second == y.second && x.first > y.first);}};

typedef std::map<INT32, unsigned int> func_list_t;
typedef std::map<INT32,std::string> func_names_t;
typedef std::set<func_t, more_func> sorted_func_list_t;

func_list_t g_func_list;
func_names_t g_func_names;

VOID CountBbl(UINT32 numInstInBbl, unsigned int *counter)
{
	*counter += numInstInBbl;
}

VOID Trace(TRACE trace, VOID *v)
{
	for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
	{
		RTN rtn = INS_Rtn(BBL_InsHead(bbl));
		if (!RTN_Valid(rtn)) continue;

		INT32 id = RTN_Id(rtn);
		std::string &temp = g_func_names[id];
		if (temp.empty()) temp = RTN_Name(rtn);
		
		BBL_InsertCall(bbl,
			IPOINT_ANYWHERE,
			(AFUNPTR)CountBbl,
			IARG_UINT32, BBL_NumIns(bbl),
			IARG_PTR, &g_func_list[id],
			IARG_END);
	}
}

VOID Fini(INT32 code, VOID *v)
{
	std::ofstream file("rtn-output.txt");

	sorted_func_list_t sorted_func_list;

	for (func_list_t::const_iterator i = g_func_list.begin(); i != g_func_list.end(); ++i)
		sorted_func_list.insert(*i);

	for (sorted_func_list_t::const_iterator i = sorted_func_list.begin(); i != sorted_func_list.end(); ++i)
		file << g_func_names[i->first] << " icount: " << i->second << std::endl;
}

int main(int argc, char *argv[])
{
	PIN_InitSymbols();
	if(PIN_Init(argc,argv)) return -1;
    
	TRACE_AddInstrumentFunction(Trace, 0);
	PIN_AddFiniFunction(Fini, 0);

	PIN_StartProgram();
    
	return 0;
}

