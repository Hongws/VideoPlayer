#ifndef __SYNC_LOCKER_H__
#define __SYNC_LOCKER_H__

#include <Windows.h>


/**
*	Copyright(C),2013-2023,Hisome
*	FileName:	SyncLocker.h
*	Author:		zhousy
*	Version:	1.0
*	Data:		2013/09/26
*	Description:
*		同步锁类定义；
*/

#pragma once



class SyncLocker
{
public:
	/**
	*	Description:
	*		创建新的同步锁对象；
	*/
	static SyncLocker *createNew();

	/**
	*	Description:
	*		回收同步锁对象资源；
	*/
	void reclaim();

	/**
	*	Description:
	*		同步锁锁定资源；
	*/
	void Lock(DWORD dwTimeout = INFINITE);

	/**
	*	Description:
	*		同步锁解锁资源；
	*/
	void unLock();


protected:
	SyncLocker(void);
	~SyncLocker(void);

	HANDLE hMutex;	//互斥量对象句柄
};

#endif