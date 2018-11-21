
#include "thread_task_mgr.h"

struct WorkerThreadContext
{
    int threadID;   //第几个thread
};


static void *WorkerMain(void *data)
{
    WorkerThreadContext *pContext = (WorkerThreadContext*)data;
    int threadID = pContext->threadID;

    IThreadTask *pTask = NULL;

    while(true)
    {
        pTask = ThreadTaskMgr::GetSharedMgr().GetTask(threadID);
        if(pTask == NULL)
        {
            continue;
        }

        pTask->OnRun(threadID);

        ThreadTaskMgr::GetSharedMgr().PushDoneTask(pTask, threadID);
    }
    return NULL;
}

ThreadTaskMgr::ThreadTaskMgr()
{
    m_inited = false;
    m_singleQueue = true;
}

ThreadTaskMgr::~ThreadTaskMgr()
{
    CleanUp();
}

ThreadTaskMgr& ThreadTaskMgr::GetSharedMgr()
{
    static ThreadTaskMgr inst;
    return inst;
}

void ThreadTaskMgr::Update()
{
    return HandlerFinishedTask();
}

bool ThreadTaskMgr::Init(int maxWorkerThread, bool singleQueue)
{
    if(m_inited)
    {
        //log??
        return false;
    }

    m_singleQueue = singleQueue;
    m_maxWorkerThread = maxWorkerThread;

    m_pSems = (sem_t**)malloc(sizeof(sem_t*) * maxWorkerThread);
    m_sems = new sem_t[maxWorkerThread];
    m_requestQueueMutexs = new pthread_mutex_t[maxWorkerThread];
    m_responseQueueMutexs = new pthread_mutex_t[maxWorkerThread];
    m_pRequestQueues = new queue<IThreadTask*>[maxWorkerThread]();
    m_pResponseQueues = new queue<IThreadTask*>[maxWorkerThread]();
    m_tids = new pthread_t[maxWorkerThread];
    for(int i = 0; i < maxWorkerThread; ++i)
    {
        m_tids[i] = -1;
    }

    for(int i = 0; i < maxWorkerThread; ++i)
    {
        WorkerThreadContext * pContext = new WorkerThreadContext;
        pContext->threadID = i;

        int semInitRet = sem_init(&m_sems[i], 0, 0);
        if(semInitRet != 0)
        {
            //log_err?
            CleanUp();
            return false;
        }

        m_pSems[i] = &m_sems[i];
        pthread_mutex_init(&m_requestQueueMutexs[i], NULL);
        pthread_mutex_init(&m_responseQueueMutexs[i], NULL);

        int pret = pthread_create(&m_tids[i], NULL, WorkerMain, pContext);
        if(pret != 0)
        {
            //log_err?
            CleanUp();
            return false;
        }
    }

    m_inited = true;
    return true;
}

void ThreadTaskMgr::CleanUp()
{
    if(m_sems != NULL)
    {
        for(int i = 0; i < m_maxWorkerThread; ++i)
        {
            sem_destroy(&m_sems[i]);
        }
        delete [] m_sems;
        m_sems = NULL;
        free(m_pSems);
        m_pSems = NULL;
    }

    if(m_requestQueueMutexs != NULL)
    {
        delete [] m_requestQueueMutexs;
        m_requestQueueMutexs = NULL;
    }
    if(m_responseQueueMutexs != NULL)
    {
        delete [] m_responseQueueMutexs;
        m_responseQueueMutexs = NULL;
    }
    if(m_pRequestQueues)
    {
        delete [] m_pRequestQueues;
        m_pRequestQueues = NULL;
    }

    if(m_pResponseQueues)
    {
        delete [] m_pResponseQueues;
        m_pResponseQueues = NULL;
    }

    if(m_tids != NULL)
    {
        for(int i = 0; i < m_maxWorkerThread; ++i)
        {
            if(m_tids[i] > 0)
            {
                pthread_cancel(m_tids[i]);
            }
        }

        delete [] m_tids;
        m_tids = NULL;
    }
}


void ThreadTaskMgr::AddTask(IThreadTask *task, int threadID)
{
    if(NULL == task)
    {
        return ;
    }

    if(m_singleQueue)
    {
        //一个消息队列
        threadID = 0;
    }
    if(threadID < 0 || threadID >= m_maxWorkerThread)
    {
        //log_err
        return ;
    }

    pthread_mutex_lock(&m_requestQueueMutexs[threadID]);
    m_pRequestQueues[threadID].push(task);
    pthread_mutex_unlock(&m_requestQueueMutexs[threadID]);

    sem_post(m_pSems[threadID]);
}

IThreadTask *ThreadTaskMgr::GetTask(int threadID)
{
    if(m_singleQueue)
    {
        threadID = 0;
    }

    if(threadID < 0 || threadID >= m_maxWorkerThread)
    {
        //log_error?
        return NULL;
    }

    int semWaitRet = sem_wait(m_pSems[threadID]);
    if(semWaitRet < 0)
    {
        //log_error?
        return NULL;
    }

    std::queue<IThreadTask*> *pQueue = &m_pRequestQueues[threadID];

    pthread_mutex_lock(&m_requestQueueMutexs[threadID]);
    if(pQueue->empty())
    {
        pthread_mutex_unlock(&m_requestQueueMutexs[threadID]);
        return NULL;    
    }
    else
    {
        IThreadTask *pTask = pQueue->front();
        pQueue->pop();
        pthread_mutex_unlock(&m_requestQueueMutexs[threadID]);
        return pTask;
    }  
}

void ThreadTaskMgr::PushDoneTask(IThreadTask *task, int threadID)
{
    if(m_singleQueue)
    {
        threadID = 0;
    }

    if(threadID < 0 || threadID >= m_maxWorkerThread)
    {
        //log_error?
        return ;
    }

    //添加到返回队列
    pthread_mutex_lock(&m_responseQueueMutexs[threadID]);
    m_pResponseQueues[threadID].push(task);
    pthread_mutex_unlock(&m_responseQueueMutexs[threadID]);  
}

void ThreadTaskMgr::HandlerFinishedTask()
{
    static int MAX_CHANNEL_MSG_NUM_ONCE = 10;   //消息通道每次处理10个包
    int threadNum = m_maxWorkerThread;
    if(m_singleQueue)
    {
        threadNum = 1;
    }

    for(int i = 0; i < threadNum; ++i)
    {
        pthread_mutex_lock(&m_responseQueueMutexs[i]);
        if(m_pResponseQueues[i].empty())
        {
            pthread_mutex_unlock(&m_responseQueueMutexs[i]); 
            continue;    
        }
        else
        {
            int taskNum = std::min(MAX_CHANNEL_MSG_NUM_ONCE / threadNum, (int)m_pResponseQueues[i].size());

            std::queue<IThreadTask*> queTask;
            for(int j = 0; j < taskNum; ++j)
            {
                IThreadTask *pTask = m_pRequestQueues[i].front();
                if(pTask)
                {
                    queTask.push(pTask);
                    m_pResponseQueues[i].pop();
                }
            }

            pthread_mutex_unlock(&m_responseQueueMutexs[i]); 

            for(int j = 0; j < taskNum; ++j)
            {
                IThreadTask *pTask = queTask.front();
                queTask.pop();
                if(pTask)
                {
                    bool bDelete = pTask->IsDeleteByThreadTaskMgr();
                    pTask->OnFinish();

                    if(bDelete)
                    {
                        delete pTask;
                    }
                }
            }
        }
    }
}



