/**
 *  Copyright (c) 2014-2015, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "LibavVideoDec.h"

#if 0
#undef LOG_INFO
#undef LOG_DEBUG
#define LOG_INFO printf
#define LOG_DEBUG printf
#endif

#define DROP_B_THRESHOLD	30000

#if (ANDROID_VERSION >= ICS) && defined(MX5X)
#define ALIGN_STRIDE(x)  (((x)+63)&(~63))
#define ALIGN_CHROMA(x) (((x) + 4095)&(~4095))
#else
#define ALIGN_STRIDE(x)  (((x)+31)&(~31))
#define ALIGN_CHROMA(x) (x)
#endif
#define G_N_ELEMENTS(arr)		(sizeof (arr) / sizeof ((arr)[0]))

#define LIBAV_COMP_NAME_AVCDEC "OMX.Freescale.std.video_decoder.avc.sw-based"
#define LIBAV_COMP_NAME_MPEG4DEC "OMX.Freescale.std.video_decoder.mpeg4.sw-based"
#define LIBAV_COMP_NAME_H263DEC	"OMX.Freescale.std.video_decoder.h263.sw-based"
#define LIBAV_COMP_NAME_MPEG2DEC "OMX.Freescale.std.video_decoder.mpeg2.sw-based"
#define LIBAV_COMP_NAME_VP8	"OMX.Freescale.std.video_decoder.vp8.sw-based"
#define LIBAV_COMP_NAME_VP9	"OMX.Freescale.std.video_decoder.vp9.sw-based"
#define LIBAV_COMP_NAME_HEVCDEC "OMX.Freescale.std.video_decoder.hevc.sw-based"


LibavVideoDec::LibavVideoDec()
{
    fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_AVCDEC);
    ComponentVersion.s.nVersionMajor = 0x0;
    ComponentVersion.s.nVersionMinor = 0x1;
    ComponentVersion.s.nRevision = 0x0;
    ComponentVersion.s.nStep = 0x0;
    role_cnt = 1;
    role[0] = (OMX_STRING)"video_decoder.avc";

    SetDefaultSetting();

}

OMX_ERRORTYPE LibavVideoDec::SetDefaultSetting()
{
    fsl_osal_memset(&sInFmt, 0, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
    sInFmt.nFrameWidth = 320;
    sInFmt.nFrameHeight = 240;
    sInFmt.xFramerate = 30 * Q16_SHIFT;
    sInFmt.eColorFormat = OMX_COLOR_FormatUnused;
    sInFmt.eCompressionFormat = OMX_VIDEO_CodingAVC;

    nInPortFormatCnt = 0;
    nOutPortFormatCnt = 1;
    eOutPortPormat[0] = OMX_COLOR_FormatYUV420Planar;

    sOutFmt.nFrameWidth = 320;
    sOutFmt.nFrameHeight = 240;
    sOutFmt.nStride = 320;
    sOutFmt.nSliceHeight = ALIGN_STRIDE(240);
    sOutFmt.eColorFormat = OMX_COLOR_FormatYUV420Planar;
    sOutFmt.eCompressionFormat = OMX_VIDEO_CodingUnused;

    bFilterSupportPartilInput = OMX_FALSE;
    nInBufferCnt = 1;
    nInBufferSize = 512*1024;
    nOutBufferCnt = 3;
    nOutBufferSize = sOutFmt.nFrameWidth * sOutFmt.nFrameHeight \
                     * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;

    pInBuffer = pOutBuffer = NULL;
    nInputSize = nInputOffset = 0;
    bInEos = OMX_FALSE;
    nFrameSize = 0;
    pFrameBuffer = NULL;

    OMX_INIT_STRUCT(&sOutCrop, OMX_CONFIG_RECTTYPE);
    sOutCrop.nPortIndex = OUT_PORT;
    sOutCrop.nLeft = sOutCrop.nTop = 0;
    sOutCrop.nWidth = sInFmt.nFrameWidth;
    sOutCrop.nHeight = sInFmt.nFrameHeight;

    codecID = AV_CODEC_ID_NONE;
    codecContext = NULL;
    picture = NULL;
    pClock = NULL;
    CodingType = OMX_VIDEO_CodingUnused;
    bNeedReportOutputFormat = OMX_TRUE;
    bLibavHoldOutputBuffer = OMX_FALSE;
    bOutEos = OMX_FALSE;
    nInvisibleFrame = 0;

    eDecState = LIBAV_DEC_INIT;

    return OMX_ErrorNone;

}

OMX_ERRORTYPE  LibavVideoDec::SetRoleFormat(OMX_STRING role)
{
    if(fsl_osal_strcmp(role, "video_decoder.avc") == 0) {
        CodingType = OMX_VIDEO_CodingAVC;
        codecID = AV_CODEC_ID_H264;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_AVCDEC);
    } else if(fsl_osal_strcmp(role, "video_decoder.mpeg4") == 0) {
        CodingType = OMX_VIDEO_CodingMPEG4;
        codecID = AV_CODEC_ID_MPEG4;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_MPEG4DEC);
    } else if(fsl_osal_strcmp(role, "video_decoder.h263") == 0) {
        CodingType = OMX_VIDEO_CodingH263;
        codecID = AV_CODEC_ID_H263;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_H263DEC);
    } else if(fsl_osal_strcmp(role, "video_decoder.hevc") == 0) {
        CodingType = (OMX_VIDEO_CODINGTYPE)OMX_VIDEO_CodingHEVC;
        codecID = AV_CODEC_ID_HEVC;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_HEVCDEC);
    } else if(fsl_osal_strcmp(role, "video_decoder.mpeg2") == 0) {
        CodingType = OMX_VIDEO_CodingMPEG2;
        codecID = AV_CODEC_ID_MPEG2VIDEO;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_MPEG2DEC);
    } else if(fsl_osal_strcmp(role, "video_decoder.vp8") == 0) {
        CodingType = (OMX_VIDEO_CODINGTYPE)OMX_VIDEO_CodingVP8;
        codecID = AV_CODEC_ID_VP8;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_VP8);
    } else if(fsl_osal_strcmp(role, "video_decoder.vp9") == 0) {
        CodingType = (OMX_VIDEO_CODINGTYPE)OMX_VIDEO_CodingVP9;
        codecID = AV_CODEC_ID_VP9;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_VP9);
    } else {
        CodingType = OMX_VIDEO_CodingUnused;
        codecID = AV_CODEC_ID_NONE;
        LOG_ERROR("%s: failure: unknow role: %s \r\n",__FUNCTION__,role);
        return OMX_ErrorUndefined;
    }

    //check input change
    if(sInFmt.eCompressionFormat != CodingType) {
        sInFmt.eCompressionFormat = CodingType;
        InputFmtChanged();
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::GetParameter(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch((int)nParamIndex){
        case OMX_IndexParamStandardComponentRole:
            fsl_osal_strcpy((OMX_STRING)((OMX_PARAM_COMPONENTROLETYPE*) \
                        pComponentParameterStructure)->cRole,(OMX_STRING)cRole);
            break;
        case OMX_IndexParamVideoProfileLevelQuerySupported:
            struct CodecProfileLevel {
                OMX_U32 mProfile;
                OMX_U32 mLevel;
            };

            static const CodecProfileLevel kH263ProfileLevels[] = {
                { OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level10 },
                { OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level20 },
                { OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level30 },
                { OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level45 },
                { OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level50 },
                { OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level60 },
                { OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level70 },
                { OMX_VIDEO_H263ProfileISWV2,    OMX_VIDEO_H263Level10 },
                { OMX_VIDEO_H263ProfileISWV2,    OMX_VIDEO_H263Level20 },
                { OMX_VIDEO_H263ProfileISWV2,    OMX_VIDEO_H263Level30 },
                { OMX_VIDEO_H263ProfileISWV2,    OMX_VIDEO_H263Level45 },
                { OMX_VIDEO_H263ProfileISWV2,    OMX_VIDEO_H263Level50 },
                { OMX_VIDEO_H263ProfileISWV2,    OMX_VIDEO_H263Level60 },
                { OMX_VIDEO_H263ProfileISWV2,    OMX_VIDEO_H263Level70 },
            };

            static const CodecProfileLevel kProfileLevels[] = {
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1  },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1b },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel11 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel12 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel13 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel2  },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel21 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel22 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel3  },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel31 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel32 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel4  },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel41 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel42 },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel5  },
                { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel51 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1  },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1b },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel11 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel12 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel13 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel2  },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel21 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel22 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel3  },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel31 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel32 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel4  },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel41 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel42 },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel5  },
                { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel51 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1  },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1b },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel11 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel12 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel13 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel2  },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel21 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel22 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel3  },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel31 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel32 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel4  },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel41 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel42 },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel5  },
                { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel51 },
            };

            static const CodecProfileLevel kM4VProfileLevels[] = {
                { OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level0 },
                { OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level0b },
                { OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level1 },
                { OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level2 },
                { OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4Level3 },
                { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level0 },
                { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level0b },
                { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level1 },
                { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level2 },
                { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level3 },
                { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level4 },
                { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level4a },
                { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level5 },
            };

            static const CodecProfileLevel kMpeg2ProfileLevels[] = {
                { OMX_VIDEO_MPEG2ProfileSimple, OMX_VIDEO_MPEG2LevelLL },
                { OMX_VIDEO_MPEG2ProfileSimple, OMX_VIDEO_MPEG2LevelML },
                { OMX_VIDEO_MPEG2ProfileSimple, OMX_VIDEO_MPEG2LevelH14},
                { OMX_VIDEO_MPEG2ProfileSimple, OMX_VIDEO_MPEG2LevelHL},
                { OMX_VIDEO_MPEG2ProfileMain, OMX_VIDEO_MPEG2LevelLL },
                { OMX_VIDEO_MPEG2ProfileMain, OMX_VIDEO_MPEG2LevelML },
                { OMX_VIDEO_MPEG2ProfileMain, OMX_VIDEO_MPEG2LevelH14},
                { OMX_VIDEO_MPEG2ProfileMain, OMX_VIDEO_MPEG2LevelHL},
            };

            OMX_VIDEO_PARAM_PROFILELEVELTYPE  *pPara;
            OMX_S32 index;
            OMX_S32 nProfileLevels;

            pPara = (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)pComponentParameterStructure;
            switch(CodingType)
            {
                case OMX_VIDEO_CodingAVC:
                    index = pPara->nProfileIndex;

                    nProfileLevels =sizeof(kProfileLevels) / sizeof(kProfileLevels[0]);
                    if (index >= nProfileLevels) {
                        return OMX_ErrorNoMore;
                    }

                    pPara->eProfile = kProfileLevels[index].mProfile;
                    pPara->eLevel = kProfileLevels[index].mLevel;
                    break;
                case OMX_VIDEO_CodingMPEG4:
                    index = pPara->nProfileIndex;

                    nProfileLevels =sizeof(kM4VProfileLevels) / sizeof(kM4VProfileLevels[0]);
                    if (index >= nProfileLevels) {
                        return OMX_ErrorNoMore;
                    }

                    pPara->eProfile = kM4VProfileLevels[index].mProfile;
                    pPara->eLevel = kM4VProfileLevels[index].mLevel;
                    break;
                case OMX_VIDEO_CodingH263:
                    index = pPara->nProfileIndex;

                    nProfileLevels =sizeof(kH263ProfileLevels) / sizeof(kH263ProfileLevels[0]);
                    if (index >= nProfileLevels) {
                        return OMX_ErrorNoMore;
                    }

                    pPara->eProfile = kH263ProfileLevels[index].mProfile;
                    pPara->eLevel = kH263ProfileLevels[index].mLevel;
                    break;
                case OMX_VIDEO_CodingMPEG2:
                    index = pPara->nProfileIndex;

                    nProfileLevels =sizeof(kMpeg2ProfileLevels) / sizeof(kMpeg2ProfileLevels[0]);
                    if (index >= nProfileLevels) {
                        return OMX_ErrorNoMore;
                    }

                    pPara->eProfile = kMpeg2ProfileLevels[index].mProfile;
                    pPara->eLevel = kMpeg2ProfileLevels[index].mLevel;
                    break;
                default:
                    ret = OMX_ErrorUnsupportedIndex;
                    break;
            }
        default:
            ret = OMX_ErrorUnsupportedIndex;
            break;
    }

    return ret;
}

OMX_ERRORTYPE LibavVideoDec::SetParameter(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch((int)nParamIndex){
        case OMX_IndexParamStandardComponentRole:
            fsl_osal_strcpy( (fsl_osal_char *)cRole,(fsl_osal_char *) \
                    ((OMX_PARAM_COMPONENTROLETYPE*)pComponentParameterStructure)->cRole);
            if(OMX_ErrorNone != SetRoleFormat((OMX_STRING)cRole))
            {
                ret = OMX_ErrorBadParameter;
            }
            break;
        case OMX_IndexParamVideoDecChromaAlign:
            {
                OMX_U32* pAlignVal=(OMX_U32*)pComponentParameterStructure;
                nChromaAddrAlign=*pAlignVal;
                LOG_DEBUG("set OMX_IndexParamVideoDecChromaAlign: %d \r\n",nChromaAddrAlign);
                if(nChromaAddrAlign==0) nChromaAddrAlign=1;
                break;
            }
        default:
            ret = OMX_ErrorUnsupportedIndex;
            break;
    }

    return ret;
}

OMX_ERRORTYPE LibavVideoDec::GetConfig(OMX_INDEXTYPE nParamIndex, OMX_PTR pComponentParameterStructure)
{
    if(nParamIndex == OMX_IndexConfigCommonOutputCrop) {
        OMX_CONFIG_RECTTYPE *pRecConf = (OMX_CONFIG_RECTTYPE*)pComponentParameterStructure;
        if(pRecConf->nPortIndex == OUT_PORT) {
            pRecConf->nTop = sOutCrop.nTop;
            pRecConf->nLeft = sOutCrop.nLeft;
            pRecConf->nWidth = sOutCrop.nWidth;
            pRecConf->nHeight = sOutCrop.nHeight;
        }
        return OMX_ErrorNone;
    }
    else
        return OMX_ErrorUnsupportedIndex;
}

OMX_ERRORTYPE LibavVideoDec::SetConfig(OMX_INDEXTYPE nIndex, OMX_PTR pComponentConfigStructure)
{
    OMX_CONFIG_CLOCK *pC;

    switch((int)nIndex)
    {
        case OMX_IndexConfigClock:
            pC = (OMX_CONFIG_CLOCK*) pComponentConfigStructure;
            pClock = pC->hClock;
            break;
        default:
            break;
    }
    return OMX_ErrorNone;
}

static void libav_log_callback (void *ptr, int level, const char *fmt, va_list vl)
{
    LogLevel log_level;

    switch (level) {
        case AV_LOG_QUIET:
            log_level = LOG_LEVEL_NONE;
            break;
        case AV_LOG_ERROR:
            log_level = LOG_LEVEL_ERROR;
            break;
        case AV_LOG_INFO:
            log_level = LOG_LEVEL_INFO;
            break;
        case AV_LOG_DEBUG:
            log_level = LOG_LEVEL_DEBUG;
            break;
        default:
            log_level = LOG_LEVEL_INFO;
            break;
    }

    LOG2(log_level, fmt, vl);
}

OMX_ERRORTYPE LibavVideoDec::InitFilterComponent()
{
    av_log_set_level(AV_LOG_DEBUG);
    av_log_set_callback (libav_log_callback);

    /* register all the codecs */
    avcodec_register_all();

    LOG_DEBUG("libav load done.");

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::DeInitFilterComponent()
{
    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::InitFilter()
{

    int n;

    AVCodec *codec =  avcodec_find_decoder(codecID);
    if(!codec){
        LOG_ERROR("find decoder fail, codecID %d" , codecID);
        return OMX_ErrorUndefined;
    }

    codecContext = avcodec_alloc_context3(codec);
    if(!codecContext){
        LOG_ERROR("alloc context fail");
        return OMX_ErrorUndefined;
    }

    n = sysconf(_SC_NPROCESSORS_CONF);
    if (n < 1)
        n = 1;
    LOG_INFO("configure processor count: %d\n", n);
    codecContext->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
    codecContext->thread_count = n;

    codecContext->width = sInFmt.nFrameWidth;
    codecContext->height = sInFmt.nFrameHeight;
    codecContext->coded_width = 0;
    codecContext->coded_height = 0;
    codecContext->extradata = (uint8_t *)pCodecData;
    codecContext->extradata_size = nCodecDataLen;

    if(avcodec_open2(codecContext, codec, NULL) < 0){
        LOG_ERROR("codec open fail");
        return OMX_ErrorUndefined;
    }

    picture = av_frame_alloc();
    if(picture == NULL){
        LOG_ERROR("alloc frame fail");
        return OMX_ErrorInsufficientResources;
    }

    LOG_DEBUG("libav init done.");
    eDecState = LIBAV_DEC_RUN;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::DeInitFilter()
{
    if(codecContext) {
        avcodec_close(codecContext);
        av_free(codecContext);
        codecContext = NULL;
    }

    if(picture) {
        av_frame_free(&picture);
        picture = NULL;
    }

    eDecState = LIBAV_DEC_INIT;
    SetDefaultSetting();

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::SetInputBuffer(
        OMX_PTR pBuffer,
        OMX_S32 nSize,
        OMX_BOOL bLast)
{
    if(pBuffer == NULL)
        return OMX_ErrorBadParameter;

    pInBuffer = pBuffer;
    nInputSize = nSize;
    nInputOffset = 0;
    bInEos = bLast;
    if(nSize == 0 && !bLast){
        nInputSize = 0;
        pInBuffer = NULL;
    }

    if(eDecState == LIBAV_DEC_STOP &&  nSize > 0)
        eDecState = LIBAV_DEC_RUN;

    LOG_DEBUG("libav dec set input buffer: %p:%d:%d\n", pBuffer, nSize, bInEos);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::SetOutputBuffer(OMX_PTR pBuffer)
{
    if(pBuffer == NULL)
        return OMX_ErrorBadParameter;

    pOutBuffer = pBuffer;

    LOG_DEBUG("libav dec set output buffer: %p\n", pBuffer);

    return OMX_ErrorNone;
}

FilterBufRetCode LibavVideoDec::FilterOneBuffer()
{
    FilterBufRetCode ret = FILTER_OK;

    switch(eDecState) {
        case LIBAV_DEC_INIT:
            ret = FILTER_DO_INIT;
            break;
        case LIBAV_DEC_RUN:
            ret = DecodeOneFrame();
            break;
        case LIBAV_DEC_STOP:
            break;
        default:
            break;
    }

    LOG_DEBUG("libav FilterOneBuffer ret: %d\n", ret);

    return ret;
}

typedef struct
{
    OMX_COLOR_FORMATTYPE format;
    enum PixelFormat pixfmt;
} PixToFmt;

static const PixToFmt pixtofmttable[] = {
    {OMX_COLOR_FormatYUV420Planar, PIX_FMT_YUV420P},
    {OMX_COLOR_FormatYUV420SemiPlanar, PIX_FMT_NV12},
};

static OMX_COLOR_FORMATTYPE LibavFmtToOMXFmt(enum PixelFormat pixfmt)
{
    OMX_U32 i;

    for (i = 0; i < G_N_ELEMENTS (pixtofmttable); i++)
        if (pixtofmttable[i].pixfmt == pixfmt)
            return pixtofmttable[i].format;

    LOG_DEBUG ("Unknown pixel format %d", pixfmt);
    return OMX_COLOR_FormatUnused;
}

OMX_ERRORTYPE LibavVideoDec::DetectOutputFmt()
{
    nPadWidth = (codecContext->coded_width +15)&(~15);
    nPadHeight = (codecContext->coded_height +15)&(~15);

    nOutBufferCnt = 5;
    sOutFmt.eColorFormat = LibavFmtToOMXFmt(codecContext->pix_fmt);
    sOutFmt.nFrameWidth = ALIGN_STRIDE(nPadWidth);
    sOutFmt.nFrameHeight = ALIGN_STRIDE(nPadHeight);
    sOutFmt.nStride = sOutFmt.nFrameWidth;
    sOutFmt.nSliceHeight = sOutFmt.nFrameHeight;
    nOutBufferSize = sOutFmt.nFrameWidth * sOutFmt.nFrameHeight * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;
    LOG_DEBUG("output color format: %d\n", sOutFmt.eColorFormat);

    sOutCrop.nLeft = 0;
    sOutCrop.nTop = 0;
    sOutCrop.nWidth = codecContext->width & (~7);
    sOutCrop.nHeight = codecContext->height;

    LOG_DEBUG("Width: %d, Height: %d\n", codecContext->width, codecContext->height);
    LOG_DEBUG("nPadWidth: %d, nPadHeight: %d\n", nPadWidth, nPadHeight);
    LOG_DEBUG("coded Width: %d, coded Height: %d\n", codecContext->coded_width, codecContext->coded_height);

    VideoFilter::OutputFmtChanged();

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::ProcessQOS()
{
    OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;
    OMX_TIME_CONFIG_SCALETYPE sScale;
    OMX_TICKS nTimeStamp;
    OMX_S32 param;

    nTimeStamp=QueryStreamTs();
    if(nTimeStamp >= 0 && pClock!=NULL)
    {
        OMX_INIT_STRUCT(&sScale, OMX_TIME_CONFIG_SCALETYPE);
        OMX_GetConfig(pClock, OMX_IndexConfigTimeScale, &sScale);
        if(!IS_NORMAL_PLAY(sScale.xScale)){
            return OMX_ErrorNone;
        }
        OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
        OMX_GetConfig(pClock, OMX_IndexConfigTimeCurrentMediaTime, &sCur);
        if(sCur.nTimestamp > (nTimeStamp - DROP_B_THRESHOLD))
        {
            LOG_DEBUG("drop B frame \r\n");
            codecContext->skip_frame = AVDISCARD_NONREF;
        }
        else
        {
            LOG_DEBUG("needn't drop B frame \r\n");
            codecContext->skip_frame = AVDISCARD_NONE;
        }
    }

    return OMX_ErrorNone;
}

FilterBufRetCode LibavVideoDec::DecodeOneFrame()
{
    FilterBufRetCode ret = FILTER_OK;
    AVPacket pkt;
    int got_picture = 0;
    int len = 0;

    if(bLibavHoldOutputBuffer == OMX_TRUE && pOutBuffer != NULL)
        return FILTER_HAS_OUTPUT;

    if(bOutEos == OMX_TRUE && pOutBuffer != NULL)
        return FILTER_LAST_OUTPUT;

    if((bLibavHoldOutputBuffer == OMX_TRUE || bOutEos == OMX_TRUE) \
            && pOutBuffer == NULL)
        return FILTER_NO_OUTPUT_BUFFER;

    if(pInBuffer == NULL && bInEos != OMX_TRUE)
        return FILTER_NO_INPUT_BUFFER;

    if(bNeedReportOutputFormat) {
        bInit = OMX_FALSE;
    }

    av_init_packet(&pkt);
    /* set end of buffer to 0 (this ensures that no overreading happens for
     * damaged mpeg streams) */
    /* ensure allocate more FF_INPUT_BUFFER_PADDING_SIZE for input buffer. */
    if(pInBuffer) {
        fsl_osal_memset((fsl_osal_ptr)((OMX_U32)pInBuffer + nInputSize), 0, \
                FF_INPUT_BUFFER_PADDING_SIZE);
    }
    pkt.data = (uint8_t *)pInBuffer;
    pkt.size = nInputSize;

    ProcessQOS();

    if(codecID == AV_CODEC_ID_VP8) {
        if(pInBuffer && !(((uint8_t *)pInBuffer)[0] & 0x10)) {
            LOG_DEBUG("invisible: %d", !(((uint8_t *)pInBuffer)[0] & 0x10));
            nInvisibleFrame ++;
        }
    }

    LOG_DEBUG("input buffer size: %d codec data size: %d\n", nInputSize, nCodecDataLen);
    len = avcodec_decode_video2(codecContext, picture, &got_picture, &pkt);

    if(bInEos == OMX_TRUE && (len < 0 || got_picture == 0)) {
        if(pOutBuffer == NULL) {
            ret = (FilterBufRetCode) (ret | FILTER_NO_OUTPUT_BUFFER);
            bOutEos = OMX_TRUE;
            return ret;
        } else {
            ret = (FilterBufRetCode) (ret | FILTER_LAST_OUTPUT);
            eDecState = LIBAV_DEC_STOP;
            return ret;
        }
    }

    if(len < 0){
        LOG_WARNING("libav decode fail %d", len);
    }
    if(len != (int)nInputSize) {
        LOG_WARNING("libav only support au alignment input.");
    }
    pInBuffer = NULL;
    nInputSize = 0;
    ret = FILTER_INPUT_CONSUMED;

    LOG_DEBUG("buffer len %d, consumed %d", nInputSize, len);
    LOG_DEBUG("nInvisibleFrame %d", nInvisibleFrame);

    if(got_picture) {
        bLibavHoldOutputBuffer = OMX_TRUE;

        if(bNeedReportOutputFormat) {
            DetectOutputFmt();
            bNeedReportOutputFormat = OMX_FALSE;
            bInit = OMX_TRUE;
            return ret;
        }

        if(pOutBuffer == NULL) {
            ret = (FilterBufRetCode) (ret | FILTER_NO_OUTPUT_BUFFER);
        } else {
            ret = (FilterBufRetCode) (ret | FILTER_HAS_OUTPUT);
        }
    } else {
        if (codecContext->skip_frame == AVDISCARD_NONREF)
            ret = (FilterBufRetCode) (ret | FILTER_SKIP_OUTPUT);
        if (nInvisibleFrame) {
            nInvisibleFrame --;
            ret = (FilterBufRetCode) (ret | FILTER_SKIP_OUTPUT);
        }
    }

    return ret;
}

OMX_ERRORTYPE LibavVideoDec::GetOutputBuffer(OMX_PTR *ppBuffer,OMX_S32* pOutSize)
{
    OMX_S32 Ysize = sOutFmt.nFrameWidth * sOutFmt.nFrameHeight;
    OMX_S32 Usize = Ysize/4;
    OMX_S32 i;
    OMX_U8* y = (OMX_U8*)pOutBuffer;
    OMX_U8* u = (OMX_U8*)ALIGN_CHROMA((OMX_U32)pOutBuffer+Ysize);
    OMX_U8* v = (OMX_U8*)ALIGN_CHROMA((OMX_U32)u+Usize);
    OMX_U8* ysrc;
    OMX_U8* usrc;
    OMX_U8* vsrc;

    //LOG_DEBUG ("Frame w:h %d:%d, codecContext w:h %d:%d, linesize %d:%d:%d\n",
    //        sOutFmt.nFrameWidth, sOutFmt.nFrameHeight,
    //        codecContext->width, codecContext->height,
    //        picture->linesize[0], picture->linesize[1], picture->linesize[2]);

    if(!bLibavHoldOutputBuffer){
        *ppBuffer = pOutBuffer;
        *pOutSize = 0;
        pOutBuffer = NULL;
        return OMX_ErrorNone;
    }
    ysrc = picture->data[0];
    usrc = picture->data[1];
    vsrc = picture->data[2];

    LOG_DEBUG("libav dec get output buffer: %p\n", pOutBuffer);
    for(i=0;i<codecContext->height;i++)
    {
        fsl_osal_memcpy((OMX_PTR)y, (OMX_PTR)ysrc, codecContext->width);
        y+=sOutFmt.nFrameWidth;
        ysrc+=picture->linesize[0];
    }
    for(i=0;i<codecContext->height/2;i++)
    {
        fsl_osal_memcpy((OMX_PTR)u, (OMX_PTR)usrc, codecContext->width/2);
        fsl_osal_memcpy((OMX_PTR)v, (OMX_PTR)vsrc, codecContext->width/2);
        u+=sOutFmt.nFrameWidth/2;
        v+=sOutFmt.nFrameWidth/2;
        usrc+=picture->linesize[1];
        vsrc+=picture->linesize[2];
    }
    *ppBuffer = pOutBuffer;
    *pOutSize = sOutFmt.nFrameWidth * sOutFmt.nFrameHeight * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;
    pOutBuffer = NULL;

    bLibavHoldOutputBuffer = OMX_FALSE;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::FlushInputBuffer()
{
    pInBuffer = NULL;
    nInputSize = 0;
    nInputOffset = 0;
    bInEos = OMX_FALSE;
    bLibavHoldOutputBuffer = OMX_FALSE;
    bOutEos = OMX_FALSE;

    nInvisibleFrame = 0;

    if(codecContext)
        avcodec_flush_buffers(codecContext);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavVideoDec::FlushOutputBuffer()
{
    pOutBuffer = NULL;
    return OMX_ErrorNone;
}
OMX_ERRORTYPE LibavVideoDec::SetCropInfo(OMX_CONFIG_RECTTYPE *sCrop)
{
    if(sCrop == NULL || sCrop->nPortIndex != OUT_PORT)
        return OMX_ErrorBadParameter;

    sOutCrop.nTop = sCrop->nTop;
    sOutCrop.nLeft = sCrop->nLeft;
    sOutCrop.nWidth = sCrop->nWidth;
    sOutCrop.nHeight = sCrop->nHeight;

    return OMX_ErrorNone;
}

/**< C style functions to expose entry point for the shared library */
extern "C" {
    OMX_ERRORTYPE LibavVideoDecoderInit(OMX_IN OMX_HANDLETYPE pHandle)
    {
        OMX_ERRORTYPE ret = OMX_ErrorNone;
        LibavVideoDec *obj = NULL;
        ComponentBase *base = NULL;

        obj = FSL_NEW(LibavVideoDec, ());
        if(obj == NULL)
            return OMX_ErrorInsufficientResources;

        base = (ComponentBase*)obj;
        ret = base->ConstructComponent(pHandle);
        if(ret != OMX_ErrorNone)
            return ret;

        return ret;
    }
}

/* File EOF */
