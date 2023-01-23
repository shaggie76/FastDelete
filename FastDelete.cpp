#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>

#include <process.h>
#include <tchar.h>
#include <windows.h>

#include "ThreadQueue.h"

#define ARRAY_COUNT(a) _countof(a)

typedef std::vector<TCHAR*> TCharVector;
typedef std::vector<HANDLE> HandleVector;

static TCharVector sRootDirectories;
static bool sKeepRoot = false;

typedef ThreadQueue<TCHAR*> DirectoryQueue;
static DirectoryQueue sDirectoryQueue;
static volatile long sQueuedJobs = 0;
static HANDLE sQueueCompleted = nullptr;

static TCharVector sDeferredDirectories;
static CRITICAL_SECTION sDeferredDirectoriesCS;

// Returns if was directory needs to be deferred
static bool FastDeleteDir(const TCHAR* directory)
{
    errno_t error;

    TCHAR wildcard[MAX_PATH];
    error = _tcscpy_s(wildcard, ARRAY_COUNT(wildcard), directory);

    assert(!error);
    if(error)
    {
        return(false);
    }

    size_t dirLen = _tcslen(wildcard);
    assert(dirLen < MAX_PATH);

    error = _tcscpy_s(wildcard + dirLen, ARRAY_COUNT(wildcard) - dirLen, TEXT("*"));

    assert(!error);
    if(error)
    {
        return(false);
    }

    WIN32_FIND_DATA findData = {0};

    HANDLE findHandle = FindFirstFileEx
    (
        wildcard,
        FindExInfoBasic,
        &findData,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH
    );

    assert(findHandle != INVALID_HANDLE_VALUE);
    if(findHandle == INVALID_HANDLE_VALUE)
    {
        return(false);
    }

    bool haveFiles = false;
    bool haveDirectories = false;

    do
    {
        if(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if((findData.cFileName[0] == '.') && (!findData.cFileName[1] || ((findData.cFileName[1] == '.') && !findData.cFileName[2])))
            {
                continue;
            }
       
            size_t paramSize = dirLen + _tcslen(findData.cFileName) + 1 + 1; // Dir char + null char
    
            TCHAR* parameter = new TCHAR[paramSize];
            
            assert(parameter);
            if(!parameter)
            {
                break;
            }
       
            memcpy(parameter, directory, dirLen * sizeof(TCHAR));
            error = _tcscpy_s(parameter + dirLen, paramSize - dirLen, findData.cFileName);

            assert(!error);

            if(error)
            {
                delete[] parameter;
                break;
            }

            parameter[paramSize - 2] = '\\';
            parameter[paramSize - 1] = '\0';

            InterlockedIncrement(&sQueuedJobs);
            sDirectoryQueue.push_back(parameter);
            haveDirectories = true;
            continue;
        }

        TCHAR fileName[MAX_PATH];
        memcpy(fileName, directory, dirLen * sizeof(TCHAR));
        error = _tcscpy_s(fileName + dirLen, ARRAY_COUNT(fileName) - dirLen, findData.cFileName);

        // if(!(findData.dwFileAttributes & FILE_ATTRIBUTE_NORMAL))
        // {
        //     _ftprintf(stderr, TEXT("Ignoring %s\n"), fileName);
        //     haveFiles = true;
        //     continue;
        // }

        // _tprintf(_T("DeleteFile %s\n"), fileName);
        
        if(!DeleteFile(fileName))
        {
            _ftprintf(stderr, TEXT("DeleteFile(%s) failed [0x%08X]\n"), fileName, GetLastError());
            haveFiles = true;
        }
    } while(FindNextFile(findHandle, &findData));

    FindClose(findHandle);

    if(haveFiles)
    {
        return(false);
    }
   
    if(sKeepRoot)
    {
        for(TCharVector::const_iterator i = sRootDirectories.begin(), end = sRootDirectories.end(); i != end; ++i)
        {
            // Note directory has trailing dir char but root may or may not: 
            const TCHAR* root = *i;
            if(_tcsnccmp(directory, root, dirLen - 1))
            {
                continue;
            }
            
            if(!root[dirLen - 1] || ((root[dirLen - 1] == '\\') && !root[dirLen]))
            {
                return(false);
            }
        }
    }

    if(haveDirectories)
    {
        // defer for cleanup later
        return(true);
    }

    // _tprintf(_T("RemoveDirectory %s\n"), directory);
        
    if(!RemoveDirectory(directory))
    {
        _ftprintf(stderr, TEXT("RemoveDirectory(%s) failed [0x%08X]\n"), directory, GetLastError());
    }

    return(false);
}

static unsigned CALLBACK FastDeleteThread(void*)
{
    for(;;)
    {
        TCHAR* directory = sDirectoryQueue.pop_front();

        if(!directory)
        {
            break;
        }

        if(FastDeleteDir(directory))
        {
            EnterCriticalSection(&sDeferredDirectoriesCS);
            sDeferredDirectories.push_back(directory);
            LeaveCriticalSection(&sDeferredDirectoriesCS);
        }
        else
        {
            delete[] directory;
        }

        if(!InterlockedDecrement(&sQueuedJobs))
        {
            SetEvent(sQueueCompleted);
        }
    }

    return(0);
}

static void PrintHelp()
{
    _fputts(TEXT("Syntax: FastDelete [--keep-root] <Directory1> [Directory2 ...]\n"), stderr);
}

int _tmain(int argc, TCHAR* argv[])
{
    if(argc < 2)
    {
        PrintHelp();
        return(1);
    }

    sRootDirectories.reserve(argc - 1);

    for(int i = 1; i < argc; ++i)
    {
        TCHAR* arg = argv[i];

        if((arg[0] == '-') || (arg[0] == '/'))
        {
            ++arg;
            if(!_tcsicmp(arg, TEXT("h")) || !_tcsicmp(arg, TEXT("?")))
            {
                PrintHelp();
                return(1);
            }

            if(!_tcscmp(arg, TEXT("-keep-root")))
            {
                sKeepRoot = true;
                continue;
            }

            _ftprintf(stderr, TEXT("Unrecognized arg: %s\n"), argv[i]);
            PrintHelp();
            return(1);
        }

        TCHAR* directory = arg;

        DWORD attribs = GetFileAttributes(directory);
        if(attribs == INVALID_FILE_ATTRIBUTES)
        {
            // TODO: implement -f to suppress this like rm -f
            _ftprintf(stderr, TEXT("Could not find %s\n"), directory);
            return(1);
        }

        if(attribs != FILE_ATTRIBUTE_DIRECTORY)
        {
            _ftprintf(stderr, TEXT("%s is not a directory\n"), directory);
            return(1);
        }
    
        sRootDirectories.push_back(directory);
    }

    if(sRootDirectories.empty())
    {
        return(0);
    }

    sQueueCompleted = CreateEvent(nullptr, true, false, nullptr);

    if(!sQueueCompleted)
    {
        _ftprintf(stderr, TEXT("CreateEvent failed [0x%08X]\n"), GetLastError());
        return(1);
    }

    InitializeCriticalSection(&sDeferredDirectoriesCS);

    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);
    
    HandleVector threads;
    threads.reserve(systemInfo.dwNumberOfProcessors);

    for(DWORD i = 0; i < systemInfo.dwNumberOfProcessors; ++i)
    {
        const unsigned STACK_SIZE = 64 * 1024;
        const unsigned creationFlags = 0;
        
        unsigned int id = 0;

        HANDLE handle = reinterpret_cast<HANDLE>(_beginthreadex
        (
            nullptr, 
            STACK_SIZE,
            &FastDeleteThread,
            nullptr,
            creationFlags,
            &id
        ));

        if(!handle && ((errno == EACCES) || (errno == ENOMEM)))
        {
            _ftprintf(stderr, TEXT("_beginthreadex failed [0x%08X]\n"), GetLastError());
            break;
        }

        threads.push_back(handle);
    }

    if(threads.size() == systemInfo.dwNumberOfProcessors)
    {
        for(TCharVector::const_iterator i = sRootDirectories.begin(), end = sRootDirectories.end(); i != end; ++i)
        {
            const TCHAR* directory = *i;
            size_t paramSize = _tcslen(directory) + 1;
    
            bool appendDirChar = (directory[paramSize - 2] != '\\') && (directory[paramSize - 2] != '/');
        
            if(appendDirChar)
            {
                ++paramSize;
            }

            TCHAR* parameter = new TCHAR[paramSize];
            assert(parameter);

            if(!parameter)
            {
                break;
            }

            errno_t error = _tcscpy_s(parameter, paramSize, directory);
            assert(!error);

            if(error)
            {
                delete[] parameter;
                break;
            }

            if(appendDirChar)
            {
                parameter[paramSize - 2] = '\\';
                parameter[paramSize - 1] = '\0';
            }

            InterlockedIncrement(&sQueuedJobs);
            sDirectoryQueue.push_back(parameter);
        }
    
        // We have to wait for all nested jobs to be queued before we can signal shutdown
        // since the queue is executed in FIFO order:
        if(WaitForSingleObject(sQueueCompleted, INFINITE) != WAIT_OBJECT_0)
        {
            _ftprintf(stderr, TEXT("WaitForSingleObject(sQueueCompleted) failed [0x%08X]\n"), GetLastError());
        }
    }

    // Signal shutdown:
    for(size_t i = 0, n = threads.size(); i != n; ++i)
    {
        sDirectoryQueue.push_back(nullptr);
    }

    if(WaitForMultipleObjects(static_cast<DWORD>(threads.size()), threads.data(), TRUE, INFINITE) != WAIT_OBJECT_0)
    {
        _ftprintf(stderr, TEXT("WaitForMultipleObjects() failed [0x%08X]\n"), GetLastError());
    }

    CloseHandle(sQueueCompleted);
    sQueueCompleted = nullptr;

    for(TCharVector::iterator i = sDeferredDirectories.begin(), end = sDeferredDirectories.end(); i != end; ++i)
    {
        TCHAR* directory = *i;

        if(!RemoveDirectory(directory))
        {
            _ftprintf(stderr, TEXT("RemoveDirectory(%s) failed [0x%08X]\n"), directory, GetLastError());
        }

        delete[] directory;
    }

    sDeferredDirectories.clear();

    DeleteCriticalSection(&sDeferredDirectoriesCS);

    return(0);
}


