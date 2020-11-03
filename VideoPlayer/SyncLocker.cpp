//#include "StdAfx.h".
#include "pch.h"
#include "SyncLocker.h"


SyncLocker::SyncLocker(void)
{
	hMutex = CreateMutex(NULL,FALSE,NULL);
}


SyncLocker::~SyncLocker(void)
{
	if (hMutex)
	{
		CloseHandle(hMutex);
		hMutex = NULL;
	}
}

SyncLocker* SyncLocker::createNew()
{
	return new SyncLocker();
}

void SyncLocker::Lock(DWORD dwTimeout /*= INFINITE*/)
{
	if (hMutex)
	{
		WaitForSingleObject(hMutex,dwTimeout);
	}
}

void SyncLocker::unLock()
{
	if (hMutex)
	{
		ReleaseMutex(hMutex);
	}
}

void SyncLocker::reclaim()
{
	delete this;
}