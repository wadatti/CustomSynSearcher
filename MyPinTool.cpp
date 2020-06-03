#include <stdio.h>

#include <list>
#include <map>
#include <iostream>

#include "pin.H"

const int THRESHOLD = 10;

struct LoadTable
{
    ADDRINT pc;
    ADDRINT addr;
    int val;
    int counter;
    bool possible_spin;
};

struct ShadowMemory
{
    ADDRINT writepc;
    THREADID writeid;
};

std::map<THREADID, std::list<LoadTable> > loadTableMap;

// synTable<readpc, writepc>
std::map<ADDRINT, ADDRINT> synTable;

std::map<ADDRINT, ShadowMemory> shadowMemoryMap;

static void print_usage()
{
    std::string help = KNOB_BASE::StringKnobSummary();

    fprintf(stderr, "\nProfile call and jump targets\n");
    fprintf(stderr, "%s\n", help.c_str());
}

static void reset(std::list<LoadTable>::iterator itr, ADDRINT pc, ADDRINT addr, int val)
{
    itr->pc = pc;
    itr->addr = addr;
    itr->val = val;
    itr->counter = 1;
    itr->possible_spin = false;
}

static void load_instrument(THREADID tid, ADDRINT pc, ADDRINT addr)
{
    if (synTable.count(pc))
        return;

    int val = *((int *)addr);

    for (std::list<LoadTable>::iterator itr = loadTableMap[tid].begin(); itr != loadTableMap[tid].end(); ++itr)
    {
        if (itr->pc == pc)
        {
            int val = *((int *)addr);
            if (itr->addr != addr)
            {
                reset(itr, pc, addr, val);
                return;
            }
            if (itr->val == val)
            {
                if (itr->possible_spin)
                    return;
                itr->counter++;
                if (itr->counter == THRESHOLD)
                {
                    itr->possible_spin = true;
                }
                return;
            }
            else
            {
                if ((shadowMemoryMap[addr].writeid == tid) || (itr->possible_spin != true))
                {
                    reset(itr, pc, addr, val);
                    return;
                }
                synTable[pc] = shadowMemoryMap[addr].writepc;
                return;
            }
        }
    }
    if (loadTableMap[tid].size() == 3)
    {
        loadTableMap[tid].pop_front();
        LoadTable l = {pc, addr, val, 1, false};
        loadTableMap[tid].push_back(l);
    }
    else if (loadTableMap[tid].size() < 3)
    {
        LoadTable l = {pc, addr, val, 1, false};
        loadTableMap[tid].push_back(l);
    }
    else
    {
        fprintf(stderr, "load table size Error\n");
    }
}

static void store_instrument(ADDRINT pc, ADDRINT addr, THREADID tid)
{
    for (std::map<ADDRINT, ADDRINT>::iterator itr = synTable.begin(); itr != synTable.end(); ++itr)
    {
        if (itr->second == pc)
            return;
    }
    ShadowMemory temp = {pc, tid};
    shadowMemoryMap[addr] = temp;
}

static void instrument_insn(INS ins, void *v)
{
    if (!INS_IsMemoryRead(ins) && !INS_IsMemoryWrite(ins))
        return;

    if (INS_IsMemoryRead(ins))
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)load_instrument, IARG_THREAD_ID, IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_END);
    }
    else if (INS_IsMemoryWrite(ins))
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)store_instrument, IARG_INST_PTR, IARG_MEMORYWRITE_EA, IARG_THREAD_ID, IARG_END);
    }
}


//readpc:40074b   writepc:400771
static void print_results(INT32 code, void *v)
{
    std::cout << "*********Result*********" << std::endl;
    for (std::map<ADDRINT, ADDRINT>::iterator itr = synTable.begin(); itr != synTable.end(); ++itr){
        std::cout << "readpc:" << std::hex << itr->first << "   writepc:" << std::hex << itr->second << std::endl;
    }
    if(synTable.empty()){
        std::cout << "synTable is empty" << std::endl;
    }
    std::cout << "************************" << std::endl;
}

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv))
    {
        print_usage();
        return 1;
    }

    INS_AddInstrumentFunction(instrument_insn, NULL);

    PIN_AddFiniFunction(print_results, NULL);

    PIN_StartProgram();

    return 0;
}