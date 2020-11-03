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
*		ͬ�����ඨ�壻
*/

#pragma once



class SyncLocker
{
public:
	/**
	*	Description:
	*		�����µ�ͬ��������
	*/
	static SyncLocker *createNew();

	/**
	*	Description:
	*		����ͬ����������Դ��
	*/
	void reclaim();

	/**
	*	Description:
	*		ͬ����������Դ��
	*/
	void Lock(DWORD dwTimeout = INFINITE);

	/**
	*	Description:
	*		ͬ����������Դ��
	*/
	void unLock();


protected:
	SyncLocker(void);
	~SyncLocker(void);

	HANDLE hMutex;	//������������
};

#endif