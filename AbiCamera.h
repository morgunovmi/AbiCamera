#pragma once

#include "DeviceBase.h"
#include "ImgBuffer.h"
#include "DeviceThreads.h"
#include "ImgBuffer.h"

#include <chrono>

#define ERR_UNKNOWN_MODE         102
#define ERR_LIBRARY_INIT 103
#define ERR_IMAGE_READ 104
#define ERR_COM_RESPONSE 120
#define ERR_COMPORTPROPERTY_CREATION 119

class SequenceThread;

class AbiCamera : public CCameraBase<AbiCamera>
{
public:
    AbiCamera();
    ~AbiCamera();

    // MMDevice API
    // ------------
    int Initialize();
    int Shutdown();

    void GetName(char* name) const;

    // MMCamera API
    // ------------
    int SnapImage();
    const unsigned char* GetImageBuffer();
    unsigned GetImageWidth() const;
    unsigned GetImageHeight() const;
    unsigned GetImageBytesPerPixel() const;
    unsigned GetBitDepth() const;
    long GetImageBufferSize() const;
    double GetExposure() const;
    void SetExposure(double exp);
    int SetROI(unsigned x, unsigned y, unsigned xSize, unsigned ySize);
    int GetROI(unsigned& x, unsigned& y, unsigned& xSize, unsigned& ySize);
    int ClearROI();
    int PrepareSequenceAcqusition();
    int StartSequenceAcquisition(double interval);
    int StartSequenceAcquisition(long numImages, double interval_ms, bool stopOnOverflow);
    int StopSequenceAcquisition();
    bool IsCapturing();
    int GetBinning() const;
    int SetBinning(int binSize);
    int IsExposureSequenceable(bool& seq) const { seq = false; return DEVICE_OK; }

    // action interface
    // ----------------
    int OnBinning(MM::PropertyBase* pProp, MM::ActionType eAct);
    int OnPixelType(MM::PropertyBase* pProp, MM::ActionType eAct);
    int OnBitDepth(MM::PropertyBase* pProp, MM::ActionType eAct);
    int OnPort(MM::PropertyBase* Prop, MM::ActionType Act);
    int OnBackground(MM::PropertyBase* Prop, MM::ActionType Act);
    int OnCCDTemp(MM::PropertyBase* pProp, MM::ActionType eAct);
    int OnCold(MM::PropertyBase* Prop, MM::ActionType Act);

private:
    friend class SequenceThread;
    static const int IMAGE_WIDTH = 512;
    static const int IMAGE_HEIGHT = 512;
    static const int MAX_BIT_DEPTH = 12;
    static const int TEMP_READ_DELAY_MS = 200;
    static const int ADC_V = 330;

    std::string m_port;
    MMThreadLock m_portLock;
    bool m_initialized;

    SequenceThread* m_thread;
    MMThreadLock m_imgPixelsLock;
    int m_binning;
    int m_bytesPerPixel;
    int m_bitDepth;
    int m_subtractBackground;
    int m_cold;
    double m_ccdT;
    std::chrono::high_resolution_clock::time_point m_lastTempRead;

    double m_exposureMs;
    ImgBuffer m_imgBuf;
    ImgBuffer m_bkgBuf;
    int m_roiStartX, m_roiStartY;

    int ResizeImageBuffer();
    void GenerateImage();
    int ShotAndResponse(double exposure);
    int ReadImage(ImgBuffer& buf);
    int Help();
    int InsertImage();
};

class SequenceThread : public MMDeviceThreadBase
{
public:
    SequenceThread(AbiCamera* pCam);
    ~SequenceThread();
    void Stop();
    void Start(long numImages, double intervalMs);
    bool IsStopped();
    double GetIntervalMs() { return m_intervalMs; }
    void SetLength(long images) { m_numImages = images; }
    long GetLength() const { return m_numImages; }
    long GetImageCounter() { return m_imageCounter; }

private:
    int svc(void) throw();
    AbiCamera* m_camera;
    bool m_stop;
    long m_numImages;
    long m_imageCounter;
    double m_intervalMs;
};