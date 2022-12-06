#include "AbiCamera.h"

SequenceThread::SequenceThread(AbiCamera* pCam)
	:m_intervalMs(100.0),
	m_numImages(0),
	m_imageCounter(0),
	m_stop(true),
	m_camera(pCam)
{};

SequenceThread::~SequenceThread() {};

void SequenceThread::Stop() {
	m_stop = true;
}

void SequenceThread::Start(long numImages, double intervalMs)
{
	m_numImages = numImages;
	m_intervalMs = intervalMs;
	m_imageCounter = 0;
	m_stop = false;
	activate();
}

bool SequenceThread::IsStopped() {
	return m_stop;
}

int SequenceThread::svc(void) throw()
{
	int ret = DEVICE_ERR;
	return ret;
}
