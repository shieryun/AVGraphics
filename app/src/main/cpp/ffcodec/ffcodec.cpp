//
// Created by zzh on 2018/8/13 0013.
//

#include <jni.h>
#include <string>
#include <result.h>
#include <ThreadSafeQueue.h>
#include <YuvProcessor.h>
#include <format.h>
#include "log.h"
#include "Decoder.h"
#include "ffcodec.h"
#include "Encoder.h"
#include "FrameFilter.h"
#include "ffheader.h"

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

using namespace std;

RecordParams::RecordParams(const char *dstFile, int width, int height, int frameRate,
                           int sampleRate, int pixelFormatFlag, int sampleFormatFlag, int channels)
        : dstFile(dstFile), width(width), height(height), frameRate(frameRate),
          sampleRate(sampleRate), pixelFormatFlag(pixelFormatFlag),
          sampleFormatFlag(sampleFormatFlag), channels(channels), maxBitRate(0), quality(0),
          cropX(0), cropY(0), cropW(0), cropH(0), rotateDegree(0), scaleW(0), scaleH(0),
          mirror(false), videoFilter(nullptr), audioFilter(nullptr),
          callbackObj(nullptr), callbackMethod(nullptr) {

}

void RecordParams::setMaxBitRate(jlong maxBitRate) {
    RecordParams::maxBitRate = maxBitRate;
}

void RecordParams::setQuality(int quality) {
    RecordParams::quality = quality;
}

void RecordParams::setCrop(int cropX, int cropY, int cropW, int cropH) {
    this->cropX = cropX;
    this->cropY = cropY;
    this->cropW = cropW;
    this->cropH = cropH;
}

void RecordParams::setRotate(int rotateDegree) {
    RecordParams::rotateDegree = rotateDegree;
}

void RecordParams::setScale(int scaleW, int scaleH) {
    this->scaleW = scaleW;
    this->scaleH = scaleH;
}

void RecordParams::setMirror(bool mirror) {
    RecordParams::mirror = mirror;
}

void RecordParams::setVideoFilter(const char *videoFilter) {
    RecordParams::videoFilter = videoFilter;
}

void RecordParams::setAudioFilter(const char *audioFilter) {
    RecordParams::audioFilter = audioFilter;
}

void RecordParams::setCallback(jobject callbackObj, jmethodID callbackMethod) {
    this->callbackObj = callbackObj;
    this->callbackMethod = callbackMethod;
}

struct DecodeParams {
    string srcFilePath;
    string dstVideoPath;
    string dstAudioPath;
};


uint64_t getCurrentTimeMs();

void release();

void *decodeThreadFunc(void *arg);

void *recordThreadCallback(void *arg);

static JavaVM *gvm = nullptr;
Encoder *encoder = nullptr;
FrameFilter *filter = nullptr;
YuvProcessor *yuvProcessor = nullptr;
ThreadSafeQueue<AVModel *> *frameQueue = nullptr;
volatile bool isRecording = false;

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    gvm = vm;
    return JNI_VERSION_1_6;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_steven_avgraphics_module_av_FFCodec__1decode(JNIEnv *env, jclass type, jstring srcFile_,
                                                      jstring yuvDst_, jstring pcmDst_) {
    const char *srcFile = env->GetStringUTFChars(srcFile_, 0);
    const char *yuvDst = env->GetStringUTFChars(yuvDst_, 0);
    const char *pcmDst = env->GetStringUTFChars(pcmDst_, 0);

    LOGI("decode file: %s\nyuv data save to :%s\npcm data save to: %s", srcFile, yuvDst, pcmDst);
    DecodeParams *param = new DecodeParams;
    param->srcFilePath = srcFile;
    param->dstVideoPath = yuvDst;
    param->dstAudioPath = pcmDst;
    pthread_t decodeThread;
    pthread_create(&decodeThread, nullptr, decodeThreadFunc, param);

    env->ReleaseStringUTFChars(srcFile_, srcFile);
    env->ReleaseStringUTFChars(yuvDst_, yuvDst);
    env->ReleaseStringUTFChars(pcmDst_, pcmDst);
}

void *decodeThreadFunc(void *arg) {
    DecodeParams *param = (DecodeParams *) arg;
    Decoder *decoder = new Decoder();
    decoder->decode(param->srcFilePath.c_str(), param->dstVideoPath.c_str(),
                    param->dstAudioPath.c_str());

    delete decoder;
    delete param;

    return (void *) 1;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_steven_avgraphics_module_av_FFCodec__1initRecorder(JNIEnv *env, jobject instance,
                                                            jstring dstFilePath_, jint width,
                                                            jint height, jint pixelFormat,
                                                            jint frameRate, jint sampleFormat,
                                                            jint sampleRate, jint channels,
                                                            jlong maxBitRate, jint quality,
                                                            jint cropX, jint cropY, jint cropW,
                                                            jint cropH, jint rotateDegree,
                                                            jint scaleW, jint scaleH,
                                                            jboolean mirror, jstring videoFilter_,
                                                            jstring audioFilter_) {
    const char *dstFilePath = env->GetStringUTFChars(dstFilePath_, 0);
    const char *videoFilter = env->GetStringUTFChars(videoFilter_, 0);
    const char *audioFilter = env->GetStringUTFChars(audioFilter_, 0);

    RecordParams *params = new RecordParams(dstFilePath, width, height, frameRate,
                                            sampleRate, pixelFormat, sampleFormat, channels);
    params->setMaxBitRate(maxBitRate);
    params->setQuality(quality);
    params->setCrop(cropX, cropY, cropW, cropH);
    params->setRotate(rotateDegree);
    params->setScale(scaleW, scaleH);
    params->setMirror(mirror);
    params->setVideoFilter(videoFilter);
    params->setAudioFilter(audioFilter);
    jclass clz = env->GetObjectClass(instance);
    jobject obj = env->NewGlobalRef(instance);
    jmethodID method = env->GetMethodID(clz, "getRecordResultFromNative", "(Z)V");
    params->setCallback(obj, method);
    int result = initRecorder(params);

    env->ReleaseStringUTFChars(dstFilePath_, dstFilePath);
    env->ReleaseStringUTFChars(videoFilter_, videoFilter);
    env->ReleaseStringUTFChars(audioFilter_, audioFilter);

    return result;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_steven_avgraphics_module_av_FFCodec__1recordImage(JNIEnv *env, jclass type,
                                                           jbyteArray data_, jint length,
                                                           jint width, jint height,
                                                           jint pixelFormat) {
    uint8_t *yuvData = (uint8_t *) malloc((size_t) length);
    if (yuvData == nullptr) {
        LOGE("Could not allocate memory");
        return FAILED;
    }
    jbyte *data = env->GetByteArrayElements(data_, 0);
    memcpy(yuvData, data, (size_t) length);
    env->ReleaseByteArrayElements(data_, data, 0);

    return recordImage(yuvData, length, width, height, pixelFormat);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_steven_avgraphics_module_av_FFCodec__1recordSamples(JNIEnv *env, jclass type,
                                                             jbyteArray data_, jint length) {
    uint8_t *pcmData = (uint8_t *) malloc((size_t) length);
    if (pcmData == nullptr) {
        LOGE("Could not allocate memory");
        return FAILED;
    }
    jbyte *data = env->GetByteArrayElements(data_, 0);
    memcpy(pcmData, data, (size_t) length);
    env->ReleaseByteArrayElements(data_, data, 0);

    return recordSample(pcmData, length);;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_steven_avgraphics_module_av_FFCodec__1stopRecord(JNIEnv *env, jclass type) {
    stopRecord();
}

int initRecorder(RecordParams *params) {
    if (encoder != nullptr) {
        LOGE("encoder is working");
        return FAILED;
    }

    if (params->rotateDegree % 90 != 0) {
        LOGE("invalid rotate degree: %d", params->rotateDegree);
        return FAILED;
    }

    AVPixelFormat pixelFormat = getPixelFormat(params->pixelFormatFlag);
    AVSampleFormat sampleFormat = getSampleFormat(params->sampleFormatFlag);
    if (pixelFormat == AV_PIX_FMT_NONE || sampleFormat == AV_SAMPLE_FMT_NONE) {
        LOGE("pixel format or sample format invalid: %d, %d", params->pixelFormatFlag,
             params->sampleFormatFlag);
        return FAILED;
    }

    int ret;
    isRecording = true;
    frameQueue = new ThreadSafeQueue<AVModel *>();

    LOGI("Record to file: %s, width: %d, height: %d", params->dstFile, params->width,
         params->height);

    int outputWidth = params->width;
    int outputHeight = params->height;
    yuvProcessor = new YuvProcessor();
    YuvProcessor::Parameter *yuvParams = new YuvProcessor::Parameter(params->width, params->height,
                                                                     params->pixelFormatFlag);
    yuvParams->setCrop(params->cropX, params->cropY, params->cropW, params->cropH);
    yuvParams->setRotate(params->rotateDegree);
    yuvParams->setScale(params->scaleW, params->scaleH);
    yuvParams->setMirror(params->mirror);
    if (yuvProcessor->init(yuvParams) < 0) {
        delete yuvProcessor;
        yuvProcessor = nullptr;
    } else {
        pixelFormat = AV_PIX_FMT_YUV420P;
        outputWidth = yuvProcessor->getOutputWidth();
        outputHeight = yuvProcessor->getOutputHeight();
        if (outputWidth == 0 || outputHeight == 0) {
            outputWidth = params->rotateDegree % 180 == 0 ? params->width : params->height;
            outputHeight = params->rotateDegree % 180 == 0 ? params->height : params->width;
        }
    }

    if ((params->videoFilter && strcmp(params->videoFilter, "null") != 0)
        || (params->audioFilter && strcmp(params->audioFilter, "anull") != 0)) {
        filter = new FrameFilter();
        FrameFilter::Parameter *filterParams = new FrameFilter::Parameter();
        filterParams->setVideoParams(outputWidth, outputHeight, pixelFormat, params->frameRate,
                                     params->videoFilter);
        filterParams->setAudioParams(params->sampleRate, sampleFormat, params->channels,
                                     params->audioFilter);
        filterParams->setOutputPixelFormat(AV_PIX_FMT_YUV420P);
        ret = filter->init(filterParams);
        if (ret < 0) {
            LOGE("filter init failed, params: %s", filterParams->toString().c_str());
            delete filter;
            filter = nullptr;
        } else {
            pixelFormat = AV_PIX_FMT_YUV420P;
        }
    }

    Encoder::Parameter *encodeParams = new Encoder::Parameter(params->dstFile, outputWidth,
                                                              outputHeight, params->frameRate,
                                                              params->sampleRate, params->channels,
                                                              pixelFormat, sampleFormat);
    // 录制视频时，帧率无法确定，提示 encoder 记录时间戳来计算 pts
    encodeParams->setFrameRateFixed(false);
    encodeParams->setMaxBitRate(params->maxBitRate);
    encodeParams->setQuality(params->quality > 0 ? params->quality : 23);
    encodeParams->addEncodeOptions("preset", "ultrafast");

    encoder = new Encoder();
    ret = encoder->init(encodeParams);
    if (ret < 0) {
        LOGE("encoder init failed: %s", encodeParams->toString().c_str());
        release();
        return ret;
    }

    pthread_t recordThread;
    pthread_create(&recordThread, nullptr, recordThreadCallback, (void *) params);

    return SUCCEED;
}

int recordImage(uint8_t *image, int imageLen, int width, int height, int pixelFormat) {
    if (encoder == nullptr) {
        LOGE("recorder does not init!");
        return FAILED;
    }

    AVModel *model = new AVModel();
    model->image = image;
    model->imageLen = imageLen;
    model->width = width;
    model->height = height;
    model->flag = MODEL_FLAG_VIDEO;
    model->pts = getCurrentTimeMs();
    model->pixelFormat = pixelFormat;
    frameQueue->push(model);

    return SUCCEED;
}

int recordSample(uint8_t *sample, int sampleLen) {
    if (encoder == nullptr) {
        LOGE("recorder does not init!");
        return FAILED;
    }

    AVModel *model = new AVModel();
    model->sample = sample;
    model->sampleLen = sampleLen;
    model->flag = MODEL_FLAG_AUDIO;
    frameQueue->push(model);

    return SUCCEED;
}

void stopRecord() {
    isRecording = false;
}

void release() {
    if (encoder != nullptr) {
        delete encoder;
        encoder = nullptr;
    }

    if (filter != nullptr) {
        delete filter;
        filter = nullptr;
    }

    if (yuvProcessor != nullptr) {
        delete yuvProcessor;
        yuvProcessor = nullptr;
    }

    if (frameQueue != nullptr) {
        delete frameQueue;
        frameQueue = nullptr;
    }

    isRecording = false;
}

uint64_t getCurrentTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t us = (uint64_t) (tv.tv_sec) * 1000 * 1000 + (uint64_t) (tv.tv_usec);
    return us / 1000;
}

void *recordThreadCallback(void *arg) {
    bool encodeWell = true;
    while (isRecording || !frameQueue->empty()) {
        if (!frameQueue->empty()) {
            AVModel *model = frameQueue->pop();
            if (model->flag == MODEL_FLAG_VIDEO && yuvProcessor != nullptr &&
                yuvProcessor->process(model) < 0) {
                encodeWell = false;
                delete model;
                continue;
            }
            if (filter != nullptr) {
                filter->process(model);
            }
            if (encoder->encode(model) < 0) {
                encodeWell = false;
            }
            delete model;
        }
    }
    encoder->stop();

    RecordParams *params = (RecordParams *) arg;
    if (params->callbackObj && params->callbackMethod) {
        JNIEnv *env = nullptr;
        if (gvm->AttachCurrentThread(&env, nullptr) == 0) {
            jobject obj = params->callbackObj;
            jmethodID method = params->callbackMethod;
            env->CallVoidMethod(obj, method, (jboolean) encodeWell);
            env->DeleteGlobalRef(obj);
            gvm->DetachCurrentThread();
        }
    }
    delete params;
    release();

    return (void *) 1;
}

