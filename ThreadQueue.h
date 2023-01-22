#pragma once

#include <cassert>
#include <deque>

#include <windows.h>

template<typename T>
class ThreadQueue
{
public:
    ThreadQueue()
    {
        InitializeConditionVariable(&mNotEmptyCondition);
        InitializeCriticalSection(&mCriticalSection);
    }
    
    ~ThreadQueue()
    {
        DeleteCriticalSection(&mCriticalSection);
    }

    void push_back(const T& t)
    {
        EnterCriticalSection(&mCriticalSection);
        mQueue.push_back(t);
        LeaveCriticalSection(&mCriticalSection);
        WakeConditionVariable(&mNotEmptyCondition);
    }
    
    T pop_front()
    {
        EnterCriticalSection(&mCriticalSection);

        while(mQueue.empty())
        {
            SleepConditionVariableCS(&mNotEmptyCondition, &mCriticalSection, INFINITE);
        }

        T t = mQueue.front();
        mQueue.pop_front();
        LeaveCriticalSection(&mCriticalSection);

        return(t);
    }
    
private:    
    CONDITION_VARIABLE  mNotEmptyCondition;
    CRITICAL_SECTION    mCriticalSection;

    std::deque<T>       mQueue;
};
