#ifndef POKEHEARTGOLD_UNK_0200E320_H
#define POKEHEARTGOLD_UNK_0200E320_H

#include "types.h"

#include "trainer_data.h"

typedef struct SysTaskQueue SysTaskQueue;
typedef struct SysTask SysTask;
typedef void (*SysTaskFunc)(SysTask *task, void *data);

typedef struct TaskManagerUnkSub1C {
    u32 unk0;
} TaskManagerUnkSub1C;

typedef struct TaskManager TaskManager;
typedef struct FieldSystem FieldSystem;

typedef BOOL (*TaskFunc)(TaskManager *taskman);

typedef struct SysTask {
    SysTaskQueue *queue;
    SysTask *prev;
    SysTask *next;
    u32 priority;
    void *data;
    SysTaskFunc func;
    u32 runNow;
} SysTask;

typedef struct SysTaskQueue {
    u16 limit;
    u16 activeCount;
    SysTask headSentinel;
    SysTask **taskStack;
    SysTask *taskList;
    BOOL isInsertingTask;
    SysTask *runningTask;
    SysTask *nextTask;
} SysTaskQueue;

struct TaskManager { // declared in field_system.h
    TaskManager *prev;
    TaskFunc func;
    u32 state;
    void *env;
    u32 unk10;
    void *unk14;
    FieldSystem *fieldSystem;
    TaskManagerUnkSub1C *unk1C; // size=4
};

SysTask *LONG_CALL CreateSysTask(SysTaskFunc func, void *data, int priority);
void LONG_CALL DestroySysTask(SysTask *task);
SysTask *LONG_CALL SysTask_CreateOnVBlankQueue(SysTaskFunc func, void *data, int priority);
void LONG_CALL TaskManager_Call(TaskManager *taskman, TaskFunc taskFunc, void *env);
// Starts an event script from INSIDE a TaskFunc, which include/script.h's EventSet_Script cannot
// do: that one ends in FieldSystem_CreateTask, whose GF_ASSERT(taskman == NULL) is compiled into
// retail and which then overwrites fieldSystem->taskman with a task whose prev is NULL, orphaning
// the running chain - the field-task pump re-reads taskman after the task function returns, so a
// script started that way is freed the same frame. This one ends in TaskManager_Jump, which
// REUSES the calling task struct in place, so the caller MUST `return FALSE`: returning TRUE
// frees the very task just aimed at the script. All 13 of pret's call sites are TaskFuncs
// returning FALSE, with no counter-example.
// The third parameter is the "last interacted" map object, mirroring EventSet_Script's own
// `void *obj` (include/script.h:105); pass NULL when the script is attached to no object.
void LONG_CALL StartScriptFromMenu(TaskManager *taskman, u16 script, void *lastInteracted);
BOOL LONG_CALL Task_TutorialBattle(TaskManager *taskManager);

void LONG_CALL CallTask_StartEncounter(TaskManager *taskManager, BattleSetup *setup, s32 effect, s32 bgm, u32 *winFlag);

#endif
