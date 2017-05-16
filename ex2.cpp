#include "pin.H"

#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

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
	/* const */ std::string rtn_name;
	/* const */ ADDRINT rtn_addr;
	/* const */ UINT32 size;

	/* const */ std::string img_name;
	/* const */ ADDRINT img_addr;

	ADDRINT target[2]; // taken/not taken
	unsigned int target_count[2];

	int idx_for_printing;	// used only for printing
};

typedef std::map<bbl_key_t, bbl_val_t> g_bbl_map_t;
g_bbl_map_t g_bbl_map;

typedef std::map<std::string, ADDRINT> g_img_map_t;
g_img_map_t g_img_map;

VOID bbl_count(struct bbl_val_t* bbl_val_ptr)
{
	++bbl_val_ptr->counter;
}

VOID direct_edge_count(struct bbl_val_t* bbl_val_ptr, INT32 isTaken, ADDRINT fallthroughAddr, ADDRINT takenAddr)
{
	bbl_val_ptr->target[0] = fallthroughAddr;
	bbl_val_ptr->target[1] = takenAddr;
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

VOID Img(IMG img, VOID *v)
{
	g_img_map[IMG_Name(img)] = IMG_LowAddress(img);
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

		IMG img = IMG_FindByAddress(bbl_key);

		std::map<bbl_key_t, bbl_val_t>::iterator it = g_bbl_map.find(bbl_key);
		if(it == g_bbl_map.end()) {	// creating a new entry in the map
			struct bbl_val_t bbl_val;

			bbl_val.counter = 0;
			bbl_val.size = BBL_Size(bbl);
			bbl_val.first_ins = INS_Address(first_ins);
			bbl_val.last_ins = INS_Address(last_ins);
			bbl_val.rtn_name = RTN_Name(rtn);
			bbl_val.rtn_addr = RTN_Address(rtn);
			bbl_val.img_name = IMG_Name(img);
			bbl_val.img_addr = IMG_LowAddress(img);
			bbl_val.target[0] = bbl_val.target[1] = 0;
			bbl_val.target_count[0] = bbl_val.target_count[1] = 0;

			it = g_bbl_map.insert(g_bbl_map.begin(), make_pair(bbl_key, bbl_val));
		}
		
		struct bbl_val_t* bbl_val_ptr = &(it->second);

		//std::cout << std::hex << bbl_val_ptr->first_ins << std::dec << ": " << bbl_val_ptr->size << std::endl;
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
		else
			INS_InsertCall(last_ins,
				IPOINT_BEFORE,
				(AFUNPTR)fallthrough_edge_count,
				IARG_PTR, (void*)bbl_val_ptr,
				IARG_BRANCH_TAKEN,
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
	ADDRINT img_addr;
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

#pragma pack(1)
struct bbl_to_file_t
{
	unsigned long counter;
	ADDRINT first_ins;
	ADDRINT last_ins;
	ADDRINT rtn_addr;
	UINT32 size;
	char img_name[255];
	ADDRINT target[2];
	unsigned int target_count[2];
};

void update_file(const std::string &file_name)
{
	int fd;
	char *buff, *p;
	unsigned int count = 0, map_size = 0;

	if ((fd = open(file_name.c_str(), O_RDONLY)) >= 0) { //read file

		map_size = sizeof(count);
		if ((p = buff = (char*)mmap(0, map_size, PROT_READ, MAP_SHARED, fd, 0)) == (caddr_t) -1) {std::cerr << "Error" << std::endl; return;}

		count = *(unsigned int*)p;
		map_size += count * sizeof(bbl_to_file_t);

		if ((p = buff = (char*)mmap(0, map_size, PROT_READ, MAP_SHARED, fd, 0)) == (caddr_t) -1) {std::cerr << "Error" << std::endl; return;}
		p += sizeof(count);

		for (unsigned int i = 0; i < count; ++i) {
			bbl_to_file_t *bbl = (bbl_to_file_t*)p;
			p += sizeof (*bbl);

			ADDRINT img_addr = g_img_map[bbl->img_name];

			bbl_val_t &bbl_val = g_bbl_map[bbl->first_ins + img_addr];

			bbl_val.counter += bbl->counter;
			bbl_val.size = bbl->size;
			bbl_val.first_ins = bbl->first_ins + img_addr;
			bbl_val.last_ins = bbl->last_ins + img_addr;
			bbl_val.rtn_addr = bbl->rtn_addr + img_addr;
			bbl_val.img_name = std::string(bbl->img_name);
			bbl_val.img_addr = img_addr;
			bbl_val.target[0] = bbl->target[0] + img_addr;
			bbl_val.target[1] = bbl->target[1] + img_addr;
			bbl_val.target_count[0] += bbl->target_count[0];
			bbl_val.target_count[1] += bbl->target_count[1];

			RTN rtn = RTN_FindByAddress(bbl_val.rtn_addr);
			if (RTN_Valid(rtn)) bbl_val.rtn_name = std::string(RTN_Name(rtn));
		}

		if (close(fd) < 0) {std::cerr << "Error" << std::endl; return;}
	}

	if ((fd = open(file_name.c_str(), O_RDWR | O_CREAT , S_IRWXU)) < 0) {std::cerr << "Error" << std::endl; return;}

	count = g_bbl_map.size();
	map_size = sizeof(count) + count * sizeof(bbl_to_file_t);

	//write dummy byte
	if (lseek(fd, map_size-1, 0) == -1) {std::cerr << "Error" << std::endl; return;}
	if (write(fd, "", 1) != 1) {std::cerr << "Error" << std::endl; return;}
	if (lseek(fd, 0, 0) == -1) {std::cerr << "Error" << std::endl; return;}

	if ((p = buff = (char*)mmap(0, map_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == (caddr_t) -1) {
		std::cerr << "Error" << std::endl;
		return;
	}

	*(unsigned int*)p = count;
	p += sizeof(count);

	for (g_bbl_map_t::const_iterator i = g_bbl_map.begin(); i != g_bbl_map.end(); ++i)
	{
		bbl_to_file_t *bbl = (bbl_to_file_t*)p;
		p += sizeof (*bbl);

		bbl->counter = i->second.counter;
		bbl->first_ins = i->second.first_ins - i->second.img_addr;
		bbl->last_ins = i->second.last_ins - i->second.img_addr;
		bbl->rtn_addr = i->second.rtn_addr - i->second.img_addr;
		bbl->size = i->second.size;
		strcpy(bbl->img_name, i->second.img_name.c_str());
		bbl->target[0] = i->second.target[0] - i->second.img_addr;
		bbl->target[1] = i->second.target[1] - i->second.img_addr;
		bbl->target_count[0] = i->second.target_count[0];
		bbl->target_count[1] = i->second.target_count[1];
	}

	if (close(fd) < 0) {std::cerr << "Error" << std::endl; return;}
}

void print(const std::string &file_name)
{
	std::ofstream file(file_name.c_str());

	std::map<ADDRINT, printing_rtn_t> printing_ds;

	for(std::map<bbl_key_t, bbl_val_t>::iterator it = g_bbl_map.begin() ; it != g_bbl_map.end() ; ++it) {
		std::map<ADDRINT, printing_rtn_t>::iterator print_it = printing_ds.find(it->second.rtn_addr);
		if(print_it == printing_ds.end()) {
			printing_rtn_t printing_rtn;
			printing_rtn.counter = 0;
			printing_rtn.rtn_name = it->second.rtn_name;
			printing_rtn.rtn_addr = it->second.rtn_addr;
			printing_rtn.img_addr = it->second.img_addr;
			print_it = printing_ds.insert(printing_ds.begin(), make_pair(it->second.rtn_addr, printing_rtn));
		}
		print_it->second.counter += (it->second.size * it->second.counter);
		print_it->second.printing_bbl_list.push_back(&(it->second));

		if (it->second.target_count[0])
		{
			g_bbl_map_t::iterator to_it = g_bbl_map.find(it->second.target[0]);
			if (to_it != g_bbl_map.end())
			{
				printing_edge_t edge;
				edge.edge_from = &(it->second);
				edge.edge_to = &to_it->second;
				edge.edge_count = it->second.target_count[0];
				if (edge.edge_from->rtn_addr == edge.edge_to->rtn_addr)
					print_it->second.printing_edges.push_back(edge);
			}
		}

		if (it->second.target_count[1])
		{
			g_bbl_map_t::iterator to_it = g_bbl_map.find(it->second.target[1]);
			if (to_it != g_bbl_map.end())
			{
				printing_edge_t edge;
				edge.edge_from = &(it->second);
				edge.edge_to = &to_it->second;
				edge.edge_count = it->second.target_count[1];
				if (edge.edge_from->rtn_addr == edge.edge_to->rtn_addr)
					print_it->second.printing_edges.push_back(edge);
			}
		}

	}

	for(std::map<ADDRINT, printing_rtn_t>::iterator print_it = printing_ds.begin() ; print_it != printing_ds.end() ; ++print_it) {
		// TODO: this is not sorted by the counter

		file << (print_it->second.rtn_name) <<
			" at 0x" << std::hex << print_it->second.rtn_addr -  print_it->second.img_addr <<
			std::dec << " : icount: " << (print_it->second.counter) << std::endl;

		std::sort(print_it->second.printing_bbl_list.begin(), print_it->second.printing_bbl_list.end(), cmp_bbl_val_t_ptr);
		int i = 0;
		for(std::vector<bbl_val_t*>::iterator rtn_it = print_it->second.printing_bbl_list.begin() ; rtn_it != print_it->second.printing_bbl_list.end() ; ++rtn_it) {
			file << "\tBB" << i << std::hex <<
				": 0x"  << (*rtn_it)->first_ins - (*rtn_it)->img_addr <<
				" - 0x" << (*rtn_it)->last_ins  - (*rtn_it)->img_addr <<
				std::dec << std::endl;
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

VOID Fini(INT32 code, VOID *v)
{
	update_file("__profile.map");
	print("rtn-output.txt");
}

int main(int argc, char *argv[])
{
	PIN_InitSymbols();
	if(PIN_Init(argc,argv)) return -1;
    
	TRACE_AddInstrumentFunction(Trace, 0);
	IMG_AddInstrumentFunction(Img, 0);
	PIN_AddFiniFunction(Fini, 0);

	PIN_StartProgram();
    
	return 0;
}

