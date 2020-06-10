#include <stdio.h>

#include <list>
#include <map>
#include <iostream>

#include "pin.H"

const int THRESHOLD = 10;
const int ENTRY_NUM = 3;
PIN_LOCK pinLock;
std::map<ADDRINT,ADDRINT> tempReadAddr;
std::map<ADDRINT,ADDRINT> tempWriteAddr;

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

static void memoryRead_ea_store(ADDRINT pc, ADDRINT addr)
{
    PIN_GetLock(&pinLock, PIN_GetTid());
    tempReadAddr[pc] = addr;
    PIN_ReleaseLock(&pinLock);
}

static void memoryWrite_ea_store(ADDRINT pc, ADDRINT addr)
{
    PIN_GetLock(&pinLock, PIN_GetTid());
    tempWriteAddr[pc] = addr;
    PIN_ReleaseLock(&pinLock);
}

static void load_instrument(ADDRINT pc, THREADID tid)
{
    PIN_GetLock(&pinLock, PIN_GetTid());

    ADDRINT *addr_ptr = (ADDRINT *)tempReadAddr[pc];
    ADDRINT value;
    PIN_SafeCopy(&value, addr_ptr, sizeof(ADDRINT));
    int val = (int)value;

    for (std::list<LoadTable>::iterator itr = loadTableMap[tid].begin(); itr != loadTableMap[tid].end(); ++itr)
    {
        if (itr->pc == pc)
        {
            if (itr->addr != tempReadAddr[pc])
            {
                reset(itr, pc, tempReadAddr[pc], val);
                PIN_ReleaseLock(&pinLock);
                return;
            }
            if (itr->val == val)
            {
                if (itr->possible_spin)
                {
                    PIN_ReleaseLock(&pinLock);
                    return;
                }

                itr->counter++;

                if (itr->counter == THRESHOLD)
                {
                    itr->possible_spin = true;
                }
                PIN_ReleaseLock(&pinLock);
                return;
            }
            else
            {
                if ((shadowMemoryMap[tempReadAddr[pc]].writeid == tid) || (itr->possible_spin != true))
                {
                    reset(itr, pc, tempReadAddr[pc], val);
                    PIN_ReleaseLock(&pinLock);
                    return;
                }
                synTable[pc] = shadowMemoryMap[tempReadAddr[pc]].writepc;
                PIN_ReleaseLock(&pinLock);
                return;
            }
        }
    }
    if (loadTableMap[tid].size() == ENTRY_NUM)
    {
        loadTableMap[tid].pop_front();
        LoadTable l = {pc, tempReadAddr[pc], val, 1, false};
        loadTableMap[tid].push_back(l);
    }
    else if (loadTableMap[tid].size() < ENTRY_NUM)
    {
        LoadTable l = {pc, tempReadAddr[pc], val, 1, false};
        loadTableMap[tid].push_back(l);
    }
    else
    {
        fprintf(stderr, "load table size Error\n");
    }
    PIN_ReleaseLock(&pinLock);
}

static void store_instrument(ADDRINT pc, ADDRINT addr, THREADID tid)
{
    PIN_GetLock(&pinLock,PIN_GetTid());
    for (std::map<ADDRINT, ADDRINT>::iterator itr = synTable.begin(); itr != synTable.end(); ++itr)
    {
        if (itr->second == pc)
        {
            return;
        }
    }
    ShadowMemory temp = {pc, tid};
    shadowMemoryMap[tempWriteAddr[pc]] = temp;
    PIN_ReleaseLock(&pinLock);
}

static void instrument_insn(INS ins, void *v)
{
    IMG img = IMG_FindByAddress(INS_Address(ins));
    if (!IMG_Valid(img) || !IMG_IsMainExecutable(img))
    {
        return;
    }
    if (INS_IsMemoryRead(ins))
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)memoryRead_ea_store, IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_END);
        if (INS_IsValidForIpointAfter(ins))
        {
            INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)load_instrument, IARG_INST_PTR, IARG_THREAD_ID, IARG_END);
        }
    }
    else if (INS_IsMemoryWrite(ins))
    {
        INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)memoryWrite_ea_store, IARG_INST_PTR, IARG_MEMORYWRITE_EA, IARG_END);
        if (INS_IsValidForIpointAfter(ins))
        {
            INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)store_instrument, IARG_INST_PTR, IARG_THREAD_ID, IARG_END);
        }
    }
}

static void print_results(INT32 code, void *v)
{
    std::cout << "*********Result*********" << std::endl;
    for (std::map<ADDRINT, ADDRINT>::iterator itr = synTable.begin(); itr != synTable.end(); ++itr)
    {
        std::cout << "readpc:" << std::hex << itr->first << "   writepc:" << std::hex << itr->second << std::endl;
    }
    if (synTable.empty())
    {
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

    PIN_InitLock(&pinLock);

    INS_AddInstrumentFunction(instrument_insn, NULL);

    PIN_AddFiniFunction(print_results, NULL);

    PIN_StartProgram();

    return 0;
}
