#pragma once

#include <stdio.h>
#include <vector>
#include <string>
#include <queue>
#include <semaphore.h>
using namespace std;

class IThreadTask
{
public:
    virtual ~IThreadTask(){}

    virtual void OnRun(int threadID) = 0; // 在子线程中调用
    virtual void OnFinish() = 0;  // 在主线程中调用
    virtual bool IsDeleteByThreadTaskMgr() const = 0;		// 是否由ThreadTaskMgr 来负责delete 
};

class ThreadTaskMgr
{
public:
    ~ThreadTaskMgr();
    ThreadTaskMgr();

    static ThreadTaskMgr &GetSharedMgr();

    bool Init(int maxWorkerThread, bool singleQueue = true);

    void Update();

    void AddTask(IThreadTask *task, int threadID = 0);

    IThreadTask *GetTask(int threadID);

    void PushDoneTask(IThreadTask *task, int threadID);

    int GetThreadNum(){return m_maxWorkerThread;}

    int GetRequestQueueSize(int threadID)
    {
        if(threadID < 0 || threadID >= m_maxWorkerThread)
        {
            //log?
            return 0;
        }
        return m_pRequestQueues[threadID].size();
    }
 
    int GetResponseQueueSize(int threadID)
    {
        if(threadID < 0 || threadID >= m_maxWorkerThread)
        {
            //log?
            return 0;
        }
        return m_pResponseQueues[threadID].size();
    }
private:
    void HandlerFinishedTask();

    void CleanUp();

private:
    bool m_inited;
    
    bool m_singleQueue;
        
    sem_t ** m_pSems;   // 就是为了方便使用-》m_sems
    sem_t * m_sems;
    pthread_mutex_t      * m_requestQueueMutexs;
    pthread_mutex_t      * m_responseQueueMutexs;

    std::queue<IThreadTask*>* m_pRequestQueues = NULL;
	std::queue<IThreadTask*>* m_pResponseQueues = NULL;

    pthread_t * m_tids = NULL;
    
    int m_maxWorkerThread;
};