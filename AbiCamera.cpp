#include "AbiCamera.h"
#include "ModuleInterface.h"

using namespace std;

const char* g_CameraName = "AbiCam";

const char* g_PixelType_8bit = "8bit";
const char* g_PixelType_16bit = "16bit";

const char* g_CameraModelProperty = "Model";
const char* const g_DevicePort = "COM Port";


///////////////////////////////////////////////////////////////////////////////
// Exported MMDevice API
///////////////////////////////////////////////////////////////////////////////

/**
 * List all supported hardware devices here
 */
MODULE_API void InitializeModuleData()
{
    RegisterDevice(g_CameraName, MM::CameraDevice, "Abisense development camera");
}

MODULE_API MM::Device* CreateDevice(const char* deviceName)
{
    if (!deviceName)
        return nullptr;

    // decide which device class to create based on the deviceName parameter
    if (strcmp(deviceName, g_CameraName) == 0)
    {
        // create camera
        return new AbiCamera();
    }

    return nullptr;
}

MODULE_API void DeleteDevice(MM::Device* pDevice)
{
    delete pDevice;
}

///////////////////////////////////////////////////////////////////////////////
// AbiCamera implementation
// ~~~~~~~~~~~~~~~~~~~~~~~

/**
* AbiCamera constructor.
* Setup default all variables and create device properties required to exist
* before intialization. In this case, no such properties were required. All
* properties will be created in the Initialize() method.
*
* As a general guideline Micro-Manager devices do not access hardware in the
* the constructor. We should do as little as possible in the constructor and
* perform most of the initialization in the Initialize() method.
*/
AbiCamera::AbiCamera() :
    m_constructionReturnCode(DEVICE_OK),
    m_binning(1),
    m_bytesPerPixel(1),
    m_bitDepth(8),
    m_initialized(false),
    m_exposureMs(10.0),
    m_roiStartX(0),
    m_roiStartY(0),
    m_thread(0)
{
    // call the base class method to set-up default error codes/messages
    InitializeDefaultErrorMessages();

    // Description property
    int ret = CreateProperty(MM::g_Keyword_Description, "AbiCamera development adapter", MM::String, true);
    assert(ret == DEVICE_OK);


    // COM Port property
    int vRet = CreatePropertyWithHandler(g_DevicePort, "Undefined", MM::String, false, &AbiCamera::OnPort, true);
    AddAllowedValue(g_DevicePort, "Undefined");
    if (vRet == DEVICE_OK)
    {
        string vComPortBaseName;
        string vComPortAddress;
        for (int i = 1; i <= 64; i++)
        {
            vComPortBaseName = "COM" + to_string(static_cast<long long>(i));
            vComPortAddress = "\\\\.\\" + vComPortBaseName;
            HANDLE hCom = CreateFileA(vComPortAddress.c_str(),
                GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, NULL);

            if (hCom != INVALID_HANDLE_VALUE)
            {
                AddAllowedValue(g_DevicePort, vComPortBaseName.c_str());
                CloseHandle(hCom);
            }
        }
    }
    else
    {
        m_constructionReturnCode = ERR_COMPORTPROPERTY_CREATION;
    }

    // create live video thread
    m_thread = new SequenceThread(this);
}

/**
* AbiCamera destructor.
* If this device used as intended within the Micro-Manager system,
* Shutdown() will be always called before the destructor. But in any case
* we need to make sure that all resources are properly released even if
* Shutdown() was not called.
*/
AbiCamera::~AbiCamera()
{
    if (m_initialized)
        Shutdown();

    delete m_thread;
}

/**
* Obtains device name.
* Required by the MM::Device API.
*/
void AbiCamera::GetName(char* name) const
{
    // We just return the name we use for referring to this
    // device adapter.
    CDeviceUtils::CopyLimitedString(name, g_CameraName);
}

/**
* Intializes the hardware.
* Typically we access and initialize hardware at this point.
* Device properties are typically created here as well.
* Required by the MM::Device API.
*/
int AbiCamera::Initialize()
{
    if (m_constructionReturnCode != DEVICE_OK)
    {
        return m_constructionReturnCode;
    }

    if (m_initialized)
        return DEVICE_OK;

    // set property list
    // -----------------

    // binning
    CPropertyAction* pAct = new CPropertyAction(this, &AbiCamera::OnBinning);
    int ret = CreateProperty(MM::g_Keyword_Binning, "1", MM::Integer, false, pAct);
    assert(ret == DEVICE_OK);

    vector<string> binningValues;
    binningValues.push_back("1");
    binningValues.push_back("2");
    binningValues.push_back("4");
    binningValues.push_back("8");
    binningValues.push_back("16");
    binningValues.push_back("32");
    binningValues.push_back("64");

    ret = SetAllowedValues(MM::g_Keyword_Binning, binningValues);
    assert(ret == DEVICE_OK);

    // pixel type
    pAct = new CPropertyAction(this, &AbiCamera::OnPixelType);
    ret = CreateProperty(MM::g_Keyword_PixelType, g_PixelType_8bit, MM::String, false, pAct);
    assert(ret == DEVICE_OK);

    vector<string> pixelTypeValues;
    pixelTypeValues.push_back(g_PixelType_8bit);
    pixelTypeValues.push_back(g_PixelType_16bit);

    ret = SetAllowedValues(MM::g_Keyword_PixelType, pixelTypeValues);
    assert(ret == DEVICE_OK);

    // Bit depth
    pAct = new CPropertyAction(this, &AbiCamera::OnBitDepth);
    ret = CreateIntegerProperty("BitDepth", 8, false, pAct);
    assert(ret == DEVICE_OK);

    vector<string> bitDepths;
    bitDepths.push_back("8");
    bitDepths.push_back("10");
    bitDepths.push_back("12");
    ret = SetAllowedValues("BitDepth", bitDepths);
    if (ret != DEVICE_OK)
        return ret;

    // synchronize all properties
    // --------------------------
    ret = UpdateStatus();
    if (ret != DEVICE_OK)
        return ret;

    // setup the buffer
    // ----------------
    ret = ResizeImageBuffer();
    if (ret != DEVICE_OK)
        return ret;

    m_initialized = true;

    std::stringstream ss{};
    ss << "Port : " << m_port << '\n';

    LogMessage(ss.str().c_str(), true);

    DetectDevice();
    return DEVICE_OK;
}

/**
* Shuts down (unloads) the device.
* Ideally this method will completely unload the device and release all resources.
* Shutdown() may be called multiple times in a row.
* Required by the MM::Device API.
*/
int AbiCamera::Shutdown()
{
    m_initialized = false;
    return DEVICE_OK;
}

MM::DeviceDetectionStatus AbiCamera::DetectDevice()
{
    if (m_initialized)
        return MM::CanCommunicate;

    // all conditions must be satisfied...
    MM::DeviceDetectionStatus result = MM::Misconfigured;
    char answerTO[MM::MaxStrLength];


    try
    {
        std::string portLowerCase = m_port;
        for (auto its = portLowerCase.begin(); its != portLowerCase.end(); ++its)
        {
            *its = (char)tolower(*its);
        }

        if (0 < portLowerCase.length() && 0 != portLowerCase.compare("undefined") && 0 != portLowerCase.compare("unknown"))
        {
            result = MM::CanNotCommunicate;
            // record the default answer time out
            GetCoreCallback()->GetDeviceProperty(m_port.c_str(), "AnswerTimeout", answerTO);

            // device specific default communication parameters
            // for Arduino Duemilanova
            GetCoreCallback()->SetDeviceProperty(m_port.c_str(), MM::g_Keyword_BaudRate, "2000000");
            // Arduino timed out in GetControllerVersion even if AnswerTimeout  = 300 ms
            GetCoreCallback()->SetDeviceProperty(m_port.c_str(), "AnswerTimeout", "500.0");
            GetCoreCallback()->SetDeviceProperty(m_port.c_str(), "DelayBetweenCharsMs", "0");
            MM::Device* pS = GetCoreCallback()->GetDevice(this, m_port.c_str());
            pS->Initialize();
            // The first second or so after opening the serial port, the Arduino is waiting for firmwareupgrades.  Simply sleep 2 seconds.
            CDeviceUtils::SleepMs(2000);
            MMThreadGuard myLock(m_portLock);
            PurgeComPort(m_port.c_str());

            result = MM::CanCommunicate;

            pS->Shutdown();
            // always restore the AnswerTimeout to the default
            GetCoreCallback()->SetDeviceProperty(m_port.c_str(), "AnswerTimeout", answerTO);
        }
    }
    catch (...)
    {
        LogMessage("Exception in DetectDevice!", false);
    }

    return result;
}

/**
* Performs exposure and grabs a single image.
* This function should block during the actual exposure and return immediately afterwards
* (i.e., before readout).  This behavior is needed for proper synchronization with the shutter.
* Required by the MM::Camera API.
*/
int AbiCamera::SnapImage()
{
    GenerateImage();
    //InsertImage();
    return DEVICE_OK;
}

/**
* Returns pixel data.
* Required by the MM::Camera API.
* The calling program will assume the size of the buffer based on the values
* obtained from GetImageBufferSize(), which in turn should be consistent with
* values returned by GetImageWidth(), GetImageHight() and GetImageBytesPerPixel().
* The calling program allso assumes that camera never changes the size of
* the pixel buffer on its own. In other words, the buffer can change only if
* appropriate properties are set (such as binning, pixel type, etc.)
*/
const unsigned char* AbiCamera::GetImageBuffer()
{
    return const_cast<unsigned char*>(m_imgBuf.GetPixels());
}

/**
* Returns image buffer X-size in pixels.
* Required by the MM::Camera API.
*/
unsigned AbiCamera::GetImageWidth() const
{
    return m_imgBuf.Width();
}

/**
* Returns image buffer Y-size in pixels.
* Required by the MM::Camera API.
*/
unsigned AbiCamera::GetImageHeight() const
{
    return m_imgBuf.Height();
}

/**
* Returns image buffer pixel depth in bytes.
* Required by the MM::Camera API.
*/
unsigned AbiCamera::GetImageBytesPerPixel() const
{
    return m_imgBuf.Depth();
}

/**
* Returns the bit depth (dynamic range) of the pixel.
* This does not affect the buffer size, it just gives the client application
* a guideline on how to interpret pixel values.
* Required by the MM::Camera API.
*/
unsigned AbiCamera::GetBitDepth() const
{
    return m_bitDepth;
}

/**
* Returns the size in bytes of the image buffer.
* Required by the MM::Camera API.
*/
long AbiCamera::GetImageBufferSize() const
{
    return m_imgBuf.Width() * m_imgBuf.Height() * GetImageBytesPerPixel();
}

/**
* Sets the camera Region Of Interest.
* Required by the MM::Camera API.
* This command will change the dimensions of the image.
* Depending on the hardware capabilities the camera may not be able to configure the
* exact dimensions requested - but should try do as close as possible.
* If the hardware does not have this capability the software should simulate the ROI by
* appropriately cropping each frame.
* This demo implementation ignores the position coordinates and just crops the buffer.
* @param x - top-left corner coordinate
* @param y - top-left corner coordinate
* @param xSize - width
* @param ySize - height
*/
int AbiCamera::SetROI(unsigned x, unsigned y, unsigned xSize, unsigned ySize)
{
    if (xSize == 0 && ySize == 0)
    {
        // effectively clear ROI
        ResizeImageBuffer();
        m_roiStartX = 0;
        m_roiStartY = 0;
    }
    else
    {
        // apply ROI
        m_imgBuf.Resize(xSize, ySize);
        m_roiStartX = x;
        m_roiStartY = y;
    }
    return DEVICE_OK;
}

/**
* Returns the actual dimensions of the current ROI.
* Required by the MM::Camera API.
*/
int AbiCamera::GetROI(unsigned& x, unsigned& y, unsigned& xSize, unsigned& ySize)
{
    x = m_roiStartX;
    y = m_roiStartY;

    xSize = m_imgBuf.Width();
    ySize = m_imgBuf.Height();

    return DEVICE_OK;
}

/**
* Resets the Region of Interest to full frame.
* Required by the MM::Camera API.
*/
int AbiCamera::ClearROI()
{
    ResizeImageBuffer();
    m_roiStartX = 0;
    m_roiStartY = 0;

    return DEVICE_OK;
}

/**
* Returns the current exposure setting in milliseconds.
* Required by the MM::Camera API.
*/
double AbiCamera::GetExposure() const
{
    return m_exposureMs;
}

/**
* Sets exposure in milliseconds.
* Required by the MM::Camera API.
*/
void AbiCamera::SetExposure(double exp)
{
    m_exposureMs = exp;
}

/**
* Returns the current binning factor.
* Required by the MM::Camera API.
*/
int AbiCamera::GetBinning() const
{
    return m_binning;
}

/**
* Sets binning factor.
* Required by the MM::Camera API.
*/
int AbiCamera::SetBinning(int binF)
{
    return SetProperty(MM::g_Keyword_Binning, CDeviceUtils::ConvertToString(binF));
}

int AbiCamera::PrepareSequenceAcqusition()
{
    if (IsCapturing())
        return DEVICE_CAMERA_BUSY_ACQUIRING;

    int ret = GetCoreCallback()->PrepareForAcq(this);
    if (ret != DEVICE_OK)
        return ret;

    return DEVICE_OK;
}


/**
 * Required by the MM::Camera API
 * Please implement this yourself and do not rely on the base class implementation
 * The Base class implementation is deprecated and will be removed shortly
 */
int AbiCamera::StartSequenceAcquisition(double interval) {

    return StartSequenceAcquisition(LONG_MAX, interval, false);
}

/**
* Stop and wait for the Sequence thread finished
*/
int AbiCamera::StopSequenceAcquisition()
{
    if (!m_thread->IsStopped()) {
        m_thread->Stop();
        m_thread->wait();
    }

    return DEVICE_OK;
}

/**
* Simple implementation of Sequence Acquisition
* A sequence acquisition should run on its own thread and transport new images
* coming of the camera into the MMCore circular buffer.
*/
int AbiCamera::StartSequenceAcquisition(long numImages, double interval_ms, bool stopOnOverflow)
{
    if (IsCapturing())
        return DEVICE_CAMERA_BUSY_ACQUIRING;

    int ret = GetCoreCallback()->PrepareForAcq(this);
    if (ret != DEVICE_OK)
        return ret;

    return DEVICE_OK;
}

/*
 * Inserts Image and MetaData into MMCore circular Buffer
 */
int AbiCamera::InsertImage()
{
    MM::MMTime timeStamp = this->GetCurrentMMTime();
    char label[MM::MaxStrLength];
    this->GetLabel(label);

    // Important:  metadata about the image are generated here:
    Metadata md;
    md.put("Camera", label);
    md.put(MM::g_Keyword_Metadata_StartTime, CDeviceUtils::ConvertToString(timeStamp.getMsec()));
    md.put(MM::g_Keyword_Metadata_ROI_X, CDeviceUtils::ConvertToString((long)m_roiStartX));
    md.put(MM::g_Keyword_Metadata_ROI_Y, CDeviceUtils::ConvertToString((long)m_roiStartY));

    char buf[MM::MaxStrLength];
    GetProperty(MM::g_Keyword_Binning, buf);
    md.put(MM::g_Keyword_Binning, buf);

    MMThreadGuard g(m_imgPixelsLock);

    const unsigned char* pI;
    pI = GetImageBuffer();

    unsigned int w = GetImageWidth();
    unsigned int h = GetImageHeight();
    unsigned int b = GetImageBytesPerPixel();

    int ret = GetCoreCallback()->InsertImage(this, pI, w, h, b, 1, md.Serialize().c_str());
    if (ret == DEVICE_BUFFER_OVERFLOW)
    {
        // do not stop on overflow - just reset the buffer
        GetCoreCallback()->ClearImageBuffer(this);
        // don't process this same image again...
        return GetCoreCallback()->InsertImage(this, pI, w, h, b, 1, md.Serialize().c_str(), false);
    }
    else
    {
        return ret;
    }
}


bool AbiCamera::IsCapturing() {
    return !m_thread->IsStopped();
}


///////////////////////////////////////////////////////////////////////////////
// AbiCamera Action handlers
///////////////////////////////////////////////////////////////////////////////

/**
* Handles "Binning" property.
*/
int AbiCamera::OnBinning(MM::PropertyBase* pProp, MM::ActionType eAct)
{
    if (eAct == MM::AfterSet)
    {
        long binSize;
        pProp->Get(binSize);
        m_binning = (int)binSize;
        return ResizeImageBuffer();
    }
    else if (eAct == MM::BeforeGet)
    {
        pProp->Set((long)m_binning);
    }

    return DEVICE_OK;
}

/**
* Handles "PixelType" property.
*/
int AbiCamera::OnPixelType(MM::PropertyBase* pProp, MM::ActionType eAct)
{
    int ret = DEVICE_ERR;
    if (eAct == MM::AfterSet)
    {
        if (IsCapturing())
            return DEVICE_CAMERA_BUSY_ACQUIRING;

        string val;
        pProp->Get(val);
        if (val.compare(g_PixelType_8bit) == 0)
        {
            m_bytesPerPixel = 1;
            ret = DEVICE_OK;
        }
        else if (val.compare(g_PixelType_16bit) == 0)
        {
            m_bytesPerPixel = 2;
            ret = DEVICE_OK;
        }
        else
        {
            ret = ERR_UNKNOWN_MODE;
        }

        ResizeImageBuffer();
    }
    else if (eAct == MM::BeforeGet)
    {
        if (m_bytesPerPixel == 1)
            pProp->Set(g_PixelType_8bit);
        else if (m_bytesPerPixel == 2)
            pProp->Set(g_PixelType_16bit);
        else
            assert(false); // this should never happen
        ret = DEVICE_OK;
    }

    return ret;
}

int AbiCamera::OnBitDepth(MM::PropertyBase* pProp, MM::ActionType eAct)
{
    int ret = DEVICE_ERR;
    switch (eAct)
    {
    case MM::AfterSet:
    {
        if (IsCapturing())
            return DEVICE_CAMERA_BUSY_ACQUIRING;

        long bitDepth;
        pProp->Get(bitDepth);

        unsigned int bytesPerComponent;

        switch (bitDepth) {
        case 8:
            bytesPerComponent = 1;
            m_bitDepth = 8;
            ret = DEVICE_OK;
            break;
        case 10:
            bytesPerComponent = 2;
            m_bitDepth = 10;
            ret = DEVICE_OK;
            break;
        case 12:
            bytesPerComponent = 2;
            m_bitDepth = 12;
            ret = DEVICE_OK;
            break;
        default:
            // on error switch to default pixel type
            bytesPerComponent = 1;

            pProp->Set((long)8);
            m_bitDepth = 8;
            ret = ERR_UNKNOWN_MODE;
            break;
        }

        char buf[MM::MaxStrLength];
        GetProperty(MM::g_Keyword_PixelType, buf);
        std::string pixelType(buf);

        // automagickally change pixel type when bit depth exceeds possible value
        if (pixelType.compare(g_PixelType_8bit) == 0)
        {
            if (2 == bytesPerComponent)
            {
                SetProperty(MM::g_Keyword_PixelType, g_PixelType_16bit);
                m_bytesPerPixel = 2;
            }
            else
            {
                m_bytesPerPixel = 1;
            }
        }
        else if (pixelType.compare(g_PixelType_16bit) == 0)
        {
            m_bytesPerPixel = 2;
        }
        ResizeImageBuffer();

    } break;
    case MM::BeforeGet:
    {
        pProp->Set((long)m_bitDepth);
        ret = DEVICE_OK;
    } break;
    default:
        break;
    }
    return ret;
}

int AbiCamera::OnPort(MM::PropertyBase* Prop, MM::ActionType Act)
{
    int vRet = DEVICE_OK;
    if (Act == MM::BeforeGet)
    {
        Prop->Set(m_port.c_str());
    }
    else if (Act == MM::AfterSet)
    {
        Prop->Get(m_port);
    }
    return vRet;
}


///////////////////////////////////////////////////////////////////////////////
// Private AbiCamera methods
///////////////////////////////////////////////////////////////////////////////

/**
* Sync internal image buffer size to the chosen property values.
*/
int AbiCamera::ResizeImageBuffer()
{
    m_imgBuf.Resize(IMAGE_WIDTH / m_binning, IMAGE_HEIGHT / m_binning, m_bytesPerPixel);

    return DEVICE_OK;
}

/**
 * Generate an image with fixed value for all pixels
 */
void AbiCamera::GenerateImage()
{
    MMThreadGuard g(m_imgPixelsLock);

    const int maxValue = (1 << MAX_BIT_DEPTH) - 1; // max for the 12 bit camera
    const double maxExp = 1000;
    double step = maxValue / maxExp;
    unsigned char* pBuf = const_cast<unsigned char*>(m_imgBuf.GetPixels());
    memset(pBuf, 128, m_imgBuf.Height() * m_imgBuf.Width() * m_imgBuf.Depth());
}
