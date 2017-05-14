#include "pin.H"

#include <fstream>
#include <string>
#include <set>
#include <map>
#include <iostream> // TODO: delete this line
#include <algorithm>

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
	/* const */ ADDRINT rtn_addr;
	/* const */ INT32 rtn_id;	// this is just for runtime, for performance
				// because on each jump, the invoked BBL has
				// to check if last instruction (from global
				// variable) was from the same RTN
	/* const */ ADDRINT target_nt_addr;	// target if branch is not taken.
					// fill this value in instrumentation time
	bbl_val_t* target_nt;	// target if branch is NOT taken. Fill this only AFTER branch was taken
	unsigned long counter_nt;
	bbl_val_t* target_t;	// target if branch is taken. Fill this only AFTER branch was taken
	unsigned long counter_t;

	int idx_for_printing;	// used only for printing
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
	if(bbl_val_ptr->first_ins == g_last_bbl_val_ptr->target_nt_addr) {
		g_last_bbl_val_ptr->target_nt = bbl_val_ptr;
		(g_last_bbl_val_ptr->counter_nt) ++;
		goto out;
	}
	g_last_bbl_val_ptr->target_t = bbl_val_ptr;
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
			bbl_val.rtn_addr = RTN_Address(rtn);
			bbl_val.target_nt_addr = bbl_val.last_ins + INS_Size(last_ins);
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

struct printing_edge_t {
	bbl_val_t* edge_from;	//  using bbl_val_t* as a unique bbl identifier
	bbl_val_t* edge_to;	//  using bbl_val_t* as a unique bbl identifier
	unsigned long edge_count;
};

bool operator<(const printing_edge_t& n1, const printing_edge_t& n2)
{
	if(n1.edge_count < n2.edge_count) return true;
	if(n1.edge_from < n2.edge_from) return true;
	return n1.edge_to < n2.edge_to;
};

struct printing_rtn_t {
	unsigned long counter;	//counter*bbl_size
	string rtn_name;
	ADDRINT rtn_addr;
	std::vector<bbl_val_t*> printing_bbl_list; //  using bbl_val_t* as a unique bbl identifier
	std::vector<printing_edge_t> printing_edges; // TODO: use this
};

struct aaa{
	bool operator()(bbl_val_t* a, bbl_val_t* b) const {   
		if(a->first_ins < b->first_ins) return true;
		return (a->last_ins < b->last_ins);
        }   
} cmp_bbl_val_t_ptr;

bool operator<(const printing_rtn_t& n1, const printing_rtn_t& n2)
{
        if (n1.counter < n2.counter) return true;
	return n1.rtn_name < n2.rtn_name;
}


VOID Fini(INT32 code, VOID *v)
{
	std::ofstream file("rtn-output.txt");

	std::map<INT32, printing_rtn_t> printing_ds;	// RTN_id to printing_rtn_t

	for(std::map<bbl_key_t, bbl_val_t>::iterator it = g_bbl_map.begin() ; it != g_bbl_map.end() ; ++it) {
		std::map<INT32, printing_rtn_t>::iterator print_it = printing_ds.find(it->second.rtn_id);
		if(print_it == printing_ds.end()) {
			printing_rtn_t printing_rtn;
			printing_rtn.counter = 0;
			printing_rtn.rtn_name = it->second.rtn_name;
			printing_rtn.rtn_addr = it->second.rtn_addr;
			print_it = printing_ds.insert(printing_ds.begin(), make_pair(it->second.rtn_id, printing_rtn));
		}
		print_it->second.counter += (it->first.second) * (it->second.counter);
		print_it->second.printing_bbl_list.push_back(&(it->second));
		// TODO: make sure I don't print edges with count 0
		printing_edge_t edge_nt, edge_t;

		edge_nt.edge_from = &(it->second);
		edge_nt.edge_to = it->second.target_nt;
		edge_nt.edge_count = it->second.counter_nt;

		edge_t.edge_from = &(it->second);
		edge_t.edge_to = it->second.target_t;
		edge_t.edge_count = it->second.counter_t;

		print_it->second.printing_edges.push_back(edge_nt);
		print_it->second.printing_edges.push_back(edge_t);
	}

	for(std::map<INT32, printing_rtn_t>::iterator print_it = printing_ds.begin() ; print_it != printing_ds.end() ; ++print_it) { // TODO: this is not sorted by the counter
		file << (print_it->second.rtn_name) << " at 0x" << std::hex << (print_it->second.rtn_addr)  << std::dec << " : icount: " << (print_it->second.counter) << std::endl;
		std::sort(print_it->second.printing_bbl_list.begin(), print_it->second.printing_bbl_list.end(), cmp_bbl_val_t_ptr);
		int i = 0;
		for(std::vector<bbl_val_t*>::iterator rtn_it = print_it->second.printing_bbl_list.begin() ; rtn_it != print_it->second.printing_bbl_list.end() ; ++rtn_it) {
			file << "BB" << i << ": 0x" << std::hex << (*rtn_it)->first_ins << " - 0x" << (*rtn_it)->last_ins << std::dec << std::endl;
			(*rtn_it)->idx_for_printing = i;
		}
	}
/*
	sorted_func_list_t sorted_func_list;

	for (func_list_t::const_iterator i = g_func_list.begin(); i != g_func_list.end(); ++i)
		sorted_func_list.insert(*i);

	for (sorted_func_list_t::const_iterator i = sorted_func_list.begin(); i != sorted_func_list.end(); ++i)
		file << g_func_names[i->first] << " icount: " << i->second << std::endl;
*/
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

