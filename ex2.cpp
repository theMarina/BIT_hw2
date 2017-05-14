#include "pin.H"

#include <fstream>
#include <string>
#include <set>
#include <map>

typedef std::pair<UINT32, unsigned int> func_t;
struct more_func{ bool operator() (const func_t& x, const func_t& y)
	{return (x.second  > y.second) || 
		(x.second == y.second && x.first > y.first);}};

typedef std::map<INT32, unsigned int> func_list_t;
typedef std::map<INT32,std::string> func_names_t;
typedef std::set<func_t, more_func> sorted_func_list_t;

func_list_t g_func_list;
func_names_t g_func_names;

typedef std::pair<ADDRINT, USIZE> bbl_key_t;	// <ADDRINT bbl_addr, USIZE bbl_size>

struct bbl_val_t
{
	unsigned long counter;	// #times this BBL was executed
	/* const */ ADDRINT first_ins;
	/* const */ ADDRINT last_ins;
	/* const */ string rtn_name;
	/* const */ INT32 rtn_id;	// this is just for runtime, for performance
				// because on each jump, the invoked BBL has
				// to check if last instruction (from global
				// variable) was from the same RTN
	/* const */ ADDRINT target_nt;	// target if branch is not taken.
					// fill this value in instrumentation time
	unsigned long counter_nt;
	ADDRINT target_t;	// target if branch is taken. Fill this only AFTER branch was taken
	unsigned long counter_t;
};

struct bbl_val_t* g_last_bbl_val_ptr = NULL;	// this is the last BBL that was executeda

std::map<bbl_key_t, bbl_val_t> g_bbl_map;

VOID CountBbl(struct bbl_val_t* bbl_val_ptr)
{
	(bbl_val_ptr->counter) ++;
	if(!g_last_bbl_val_ptr)
		goto out;
	if(bbl_val_ptr->rtn_id != g_last_bbl_val_ptr->rtn_id)
		goto out;	// the last bbl was a different function than the current bbl
	if(bbl_val_ptr->first_ins == g_last_bbl_val_ptr->target_nt) {
		(g_last_bbl_val_ptr->counter_nt) ++;
		goto out;
	}
	g_last_bbl_val_ptr->target_t = bbl_val_ptr->first_ins;
	(g_last_bbl_val_ptr->counter_t) ++;
out:
	g_last_bbl_val_ptr = bbl_val_ptr;
}


VOID Trace(TRACE trace, VOID *v)
{
	for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
	{
		bbl_key_t bbl_key;
		bbl_key.first = BBL_Address(bbl);
		bbl_key.second = BBL_Size(bbl);
		
		struct bbl_val_t* bbl_val_ptr;

		std::map<bbl_key_t, bbl_val_t>::iterator it = g_bbl_map.find(bbl_key);
		if(it == g_bbl_map.end()) {	// creating a new entry in the map
			struct bbl_val_t bbl_val;
			INS first_ins = BBL_InsHead(bbl);
			INS last_ins = BBL_InsTail(bbl);
			RTN rtn = INS_Rtn(first_ins);
			if (!RTN_Valid(rtn)) continue;

			bbl_val.counter = 0;
			bbl_val.first_ins = INS_Address(first_ins);
			bbl_val.last_ins = INS_Address(last_ins);
			bbl_val.rtn_id = RTN_Id(rtn);
			bbl_val.rtn_name = RTN_Name(rtn);
			bbl_val.target_nt = bbl_val.last_ins + INS_Size(last_ins);
			bbl_val.counter_nt = 0;
			bbl_val.counter_t = 0;
			it = g_bbl_map.insert(g_bbl_map.begin(), make_pair(bbl_key, bbl_val));
		}
		bbl_val_ptr = &(it->second);

		
		BBL_InsertCall(bbl,
			IPOINT_ANYWHERE,
			(AFUNPTR)CountBbl,
			IARG_PTR,
			(void*)bbl_val_ptr,
			IARG_END);
	}
}

VOID Fini(INT32 code, VOID *v)
{
	std::ofstream file("rtn-output.txt");

//	std::map<ADDRINT	// RTN_id -> sigma(counter*bbl_size)
				// RTN_id -> list<>

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

