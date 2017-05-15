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


typedef ADDRINT bbl_key_t;
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
	/* const */ UINT32 size;

	ADDRINT target[2]; // taken/not taken
	unsigned int target_count[2];

	int idx_for_printing;	// used only for printing
};

std::map<bbl_key_t, bbl_val_t> g_bbl_map;

VOID bbl_count(struct bbl_val_t* bbl_val_ptr)
{
	++bbl_val_ptr->counter;
}

VOID direct_edge_count(struct bbl_val_t* bbl_val_ptr, INT32 isTaken, ADDRINT fallthroughAddr, ADDRINT takenAddr)
{
	bbl_val_ptr->target[0] = fallthroughAddr;
	bbl_val_ptr->target[1] = takenAddr;
	//std::cout << "gilkup: " << std::hex << fallthroughAddr << "/" << takenAddr << std::dec << " Taken:" << isTaken <<  std::endl;
	++bbl_val_ptr->target_count[!!isTaken];
}

VOID fallthrough_edge_count(struct bbl_val_t* bbl_val_ptr, INT32 isTaken, ADDRINT fallthroughAddr)
{
	if (!isTaken)
	{
		bbl_val_ptr->target[0] = fallthroughAddr;
		++bbl_val_ptr->target_count[0];
	}
}

VOID Trace(TRACE trace, VOID *v)
{
	for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
	{
		bbl_key_t bbl_key = BBL_Address(bbl);

		INS first_ins = BBL_InsHead(bbl);
		INS last_ins = BBL_InsTail(bbl);
		
		RTN rtn = INS_Rtn(first_ins);
		if (!RTN_Valid(rtn)) continue;

		std::map<bbl_key_t, bbl_val_t>::iterator it = g_bbl_map.find(bbl_key);
		if(it == g_bbl_map.end()) {	// creating a new entry in the map
			struct bbl_val_t bbl_val;

			bbl_val.counter = 0;
			bbl_val.size = BBL_Size(bbl);
			bbl_val.first_ins = INS_Address(first_ins);
			bbl_val.last_ins = INS_Address(last_ins);
			bbl_val.rtn_id = RTN_Id(rtn);
			bbl_val.rtn_name = RTN_Name(rtn);
			bbl_val.rtn_addr = RTN_Address(rtn);
			bbl_val.target[0] = bbl_val.target[1] = 0;
			bbl_val.target_count[0] = bbl_val.target_count[1] = 0;

			it = g_bbl_map.insert(g_bbl_map.begin(), make_pair(bbl_key, bbl_val));
		}
		
		struct bbl_val_t* bbl_val_ptr = &(it->second);

		//std::cout << std::hex << bbl_val_ptr->last_ins << std::dec << ": " << INS_Disassemble(last_ins) << std::endl;

		BBL_InsertCall(bbl,
			IPOINT_BEFORE,
			(AFUNPTR)bbl_count,
			IARG_PTR, (void*)bbl_val_ptr,
			IARG_END);

		if(INS_IsDirectBranch(last_ins))
			INS_InsertCall(last_ins,
				IPOINT_BEFORE,
				(AFUNPTR)direct_edge_count,
				IARG_PTR, (void*)bbl_val_ptr,
				IARG_BRANCH_TAKEN,
				IARG_FALLTHROUGH_ADDR,
				IARG_BRANCH_TARGET_ADDR,
				IARG_END);

		else if(INS_IsBranchOrCall(last_ins))
			INS_InsertCall(last_ins,
				IPOINT_BEFORE,
				(AFUNPTR)fallthrough_edge_count,
				IARG_PTR, (void*)bbl_val_ptr,
				IARG_BRANCH_TAKEN,
				IARG_FALLTHROUGH_ADDR,
				IARG_END);

		else if (INS_HasRealRep(last_ins))
		/*
			INS_InsertCall(last_ins,
				IPOINT_AFTER,
				(AFUNPTR)direct_edge_count,
				IARG_PTR, (void*)bbl_val_ptr,
				IARG_BRANCH_TAKEN,
				IARG_FALLTHROUGH_ADDR,
				IARG_INST_PTR,
				IARG_END);
		*/;

		else
			INS_InsertCall(last_ins,
				IPOINT_BEFORE,
				(AFUNPTR)fallthrough_edge_count,
				IARG_PTR, (void*)bbl_val_ptr,
				IARG_BOOL, 0,
				IARG_FALLTHROUGH_ADDR,
				IARG_END);
	}
}

struct printing_edge_t {
	bbl_val_t* edge_from;	//  using bbl_val_t* as a unique bbl identifier
	bbl_val_t* edge_to;	//  using bbl_val_t* as a unique bbl identifier
	unsigned long edge_count;
};

struct aaaa{
	bool operator()(const printing_edge_t& n1, const printing_edge_t& n2) {
		if(n1.edge_count < n2.edge_count) return true;
		if(n1.edge_from->idx_for_printing < n2.edge_from->idx_for_printing) return true;
		return n1.edge_to->idx_for_printing < n2.edge_to->idx_for_printing;
	}
}print_edges_cmp;

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
		print_it->second.counter += (it->second.size * it->second.counter);
		print_it->second.printing_bbl_list.push_back(&(it->second));

		if (it->second.target_count[0])
		{
			printing_edge_t edge;
			edge.edge_from = &(it->second);
			edge.edge_to = &g_bbl_map[it->second.target[0]];
			edge.edge_count = it->second.target_count[0];
			if (edge.edge_from->rtn_id == edge.edge_to->rtn_id)
				print_it->second.printing_edges.push_back(edge);
			//std::cout << "0 from: " << std::hex << edge.edge_from->first_ins << " to: " << edge.edge_to->first_ins << std::dec << std::endl;
		}

		if (it->second.target_count[1])
		{
			printing_edge_t edge;
			edge.edge_from = &(it->second);
			edge.edge_to = &g_bbl_map[it->second.target[1]];
			edge.edge_count = it->second.target_count[1];
			if (edge.edge_from->rtn_id == edge.edge_to->rtn_id)
				print_it->second.printing_edges.push_back(edge);
			//std::cout << "1 from: " << std::hex << edge.edge_from->first_ins << " to: " << edge.edge_to->first_ins << std::dec << std::endl;
		}
	}

	for(std::map<INT32, printing_rtn_t>::iterator print_it = printing_ds.begin() ; print_it != printing_ds.end() ; ++print_it) { // TODO: this is not sorted by the counter
		file << (print_it->second.rtn_name) << " at 0x" << std::hex << (print_it->second.rtn_addr)  << std::dec << " : icount: " << (print_it->second.counter) << std::endl;
		std::sort(print_it->second.printing_bbl_list.begin(), print_it->second.printing_bbl_list.end(), cmp_bbl_val_t_ptr);
		int i = 0;
		for(std::vector<bbl_val_t*>::iterator rtn_it = print_it->second.printing_bbl_list.begin() ; rtn_it != print_it->second.printing_bbl_list.end() ; ++rtn_it) {
			file << "\tBB" << i << ": 0x" << std::hex << (*rtn_it)->first_ins << " - 0x" << (*rtn_it)->last_ins << std::dec << std::endl;
			(*rtn_it)->idx_for_printing = i;
			i++;
		}
//		std::sort(print_it->second.printing_edges.begin(), print_it->second.printing_edges.end(), print_edges_cmp);	// TODO: when I uncomment this line, I get a semnentation fault
		i = 0;
		for(std::vector<printing_edge_t>::iterator edge_it = print_it->second.printing_edges.begin() ; edge_it != print_it->second.printing_edges.end() ; ++edge_it) {
			file << "\t\tEdge" << i << ": BB" << edge_it->edge_from->idx_for_printing << " --> BB" << edge_it->edge_to->idx_for_printing << "\t" << edge_it->edge_count << std::endl;
			i++;
		}
	}
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

