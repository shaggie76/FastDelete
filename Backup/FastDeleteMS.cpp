#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>

#include <tchar.h>
#include <windows.h>

#define ARRAY_COUNT(a) _countof(a)

typedef std::vector<const TCHAR*> TCharVector;

static TP_CALLBACK_ENVIRON sCallbackEnv = {};

static volatile LONG sWorkQueued = 0;
// InterlockedIncrement

static void CALLBACK FastDeleteWork(PTP_CALLBACK_INSTANCE, PVOID parameter, PTP_WORK);

static void FastDeleteDir(const TCHAR* directory)
{
    errno_t error;

    TCHAR wildcard[MAX_PATH];
    error = _tcscpy_s(wildcard, ARRAY_COUNT(wildcard), directory);

    assert(!error);
    if(error)
    {
        InterlockedDecrement(&sWorkQueued);
        return;
    }

    size_t dirLen = _tcslen(wildcard);
    assert(dirLen < MAX_PATH);

    error = _tcscpy_s(wildcard + dirLen, ARRAY_COUNT(wildcard) - dirLen, TEXT("*"));

    assert(!error);
    if(error)
    {
        InterlockedDecrement(&sWorkQueued);
        return;
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
        InterlockedDecrement(&sWorkQueued);
        return;
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

            PTP_WORK threadpoolWork = CreateThreadpoolWork(FastDeleteWork, parameter, &sCallbackEnv);
        
            if(!threadpoolWork)
            {
                _ftprintf(stderr, TEXT("CreateThreadpoolWork() failed [0x%08X]\n"), GetLastError());
                delete[] parameter;
                break;
            }
        
            SubmitThreadpoolWork(threadpoolWork);
            InterlockedIncrement(&sWorkQueued);
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

    InterlockedDecrement(&sWorkQueued);

    if(haveFiles)
    {
        return;
    }

    if(haveDirectories)
    {
        // TODO: queue for cleanup later
        return;
    }
   
    // _tprintf(_T("RemoveDirectory %s\n"), directory);
        
    if(!RemoveDirectory(directory))
    {
        _ftprintf(stderr, TEXT("RemoveDirectory(%s) failed [0x%08X]\n"), directory, GetLastError());
    }
}

static void CALLBACK FastDeleteWork(PTP_CALLBACK_INSTANCE, PVOID parameter, PTP_WORK)
{
    const TCHAR* directory = reinterpret_cast<const TCHAR*>(parameter);
    FastDeleteDir(directory);
    delete[] parameter;
}

static void FastDelete(PTP_CLEANUP_GROUP cleanupGroup, const TCharVector& directories)
{
    for(TCharVector::const_iterator i = directories.begin(), end = directories.end(); i != end; ++i)
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

        PTP_WORK threadpoolWork = CreateThreadpoolWork(FastDeleteWork, parameter, &sCallbackEnv);
        
        if(!threadpoolWork)
        {
            _ftprintf(stderr, TEXT("CreateThreadpoolWork() failed [0x%08X]\n"), GetLastError());
            break;
        }
        
        SubmitThreadpoolWork(threadpoolWork);
        InterlockedIncrement(&sWorkQueued);
    }

    while(sWorkQueued)
    {
        CloseThreadpoolCleanupGroupMembers(cleanupGroup, false, nullptr);
    }
}

static void FastDelete(PTP_POOL threadPool, const TCharVector& directories)
{
    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);

    const DWORD threads = systemInfo.dwNumberOfProcessors;

    SetThreadpoolThreadMaximum(threadPool, threads);
    
    if(!SetThreadpoolThreadMinimum(threadPool, threads))
    {
        _ftprintf(stderr, TEXT("SetThreadpoolThreadMinimum(%d) failed [0x%08X]\n"), threads, GetLastError());
        return;
    }

    PTP_CLEANUP_GROUP cleanupGroup = CreateThreadpoolCleanupGroup();

    if(!cleanupGroup)
    {
        _ftprintf(stderr, TEXT("CreateThreadpoolCleanupGroup() failed [0x%08X]\n"), GetLastError());
        return; 
    }

    SetThreadpoolCallbackCleanupGroup(&sCallbackEnv, cleanupGroup, nullptr);

    FastDelete(cleanupGroup, directories);

    CloseThreadpoolCleanupGroup(cleanupGroup);
}

static void FastDelete(const TCharVector& directories)
{
    PTP_POOL threadPool = CreateThreadpool(nullptr);
    
    if(!threadPool)
    {
        _ftprintf(stderr, TEXT("CreateThreadpool failed [0x%08X]\n"), GetLastError());
        return;
    }
    
    FastDelete(threadPool, directories);
    CloseThreadpool(threadPool);
}

int _tmain(int argc, TCHAR* argv[])
{
    if(argc < 2)
    {
        _fputts(TEXT("Syntax: FastDelete <Directory1> [Directory2 ...]\n"), stderr);
        return(1);
    }

    TCharVector directories;
    directories.reserve(argc - 1);

    for(int arg = 1; arg < argc; ++arg)
    {
        const TCHAR* directory = argv[arg];

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

        directories.push_back(directory);
    }

    if(directories.empty())
    {
        return(0);
    }

    InitializeThreadpoolEnvironment(&sCallbackEnv);
    
    FastDelete(directories);

    DestroyThreadpoolEnvironment(&sCallbackEnv);

    return(0);
}


