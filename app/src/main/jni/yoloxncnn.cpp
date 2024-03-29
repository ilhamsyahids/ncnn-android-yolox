// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>

#include <android/log.h>

#include <jni.h>
#include <pthread.h>

#include <string>
#include <vector>

#include <platform.h>
#include <benchmark.h>

#include "yolox.h"

#include "ndkcamera.h"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#if __ARM_NEON
#include <arm_neon.h>
#endif // __ARM_NEON

static int draw_unsupported(cv::Mat &rgb)
{
    const char text[] = "unsupported";

    int baseLine = 0;
    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 1.0, 1, &baseLine);

    int y = (rgb.rows - label_size.height) / 2;
    int x = (rgb.cols - label_size.width) / 2;

    cv::rectangle(rgb, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)),
                  cv::Scalar(255, 255, 255), -1);

    cv::putText(rgb, text, cv::Point(x, y + label_size.height),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0));

    return 0;
}

class Fps
{
protected:
    unsigned int m_fps;
    unsigned int m_fpscount;
    double last_time;
    double current_time;

public:
    // Constructor
    Fps() : m_fps(0), m_fpscount(0), last_time(0.f), current_time(0.f)
    {
    }

    // Update
    void update()
    {
        // increase the counter by one
        m_fpscount++;

        current_time = ncnn::get_current_time();

        // one second elapsed? (= 1000 milliseconds)
        if (current_time - last_time >= 1000)
        {
            // save the current counter value to m_fps
            m_fps = m_fpscount;

            // reset the counter and the interval
            m_fpscount = 0;
            last_time = current_time;
        }
    }

    // Get fps
    unsigned int get() const
    {
        return m_fps;
    }
};

static pthread_t thread;
static JavaVM *jvm;
static jobject jInstance;
static jclass jClassInstance;
static jmethodID callbackFunc;
static JNIEnv *jnv;
static unsigned int delegate_score = 1;

static void *sendResultInference(void *)
{
    int data = 10;

    jvm->AttachCurrentThread(&jnv, NULL);

    if (jClassInstance == NULL)
    {
        jClassInstance = jnv->GetObjectClass(jInstance);
    }

    if (callbackFunc == NULL)
    {
        callbackFunc = jnv->GetMethodID(jClassInstance, "callBack", "(I)V");
    }

    jnv->CallVoidMethod(jInstance, callbackFunc, delegate_score);

    return NULL;
}

static Fps fps;

double start_time = ncnn::get_current_time();
float avg_fps = 0;
int frame_counter = 0;

static int draw_fps(cv::Mat &rgb)
{
    float spent_time = (ncnn::get_current_time() - start_time);

    char text[32];
    sprintf(text, "FPS=%.2f", avg_fps);

    if (spent_time >= 1000)
    {
        avg_fps = frame_counter * 1000 / spent_time;
        frame_counter = 0;
        start_time = ncnn::get_current_time();
    }

    int baseLine = 0;
    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);

    int y = 0;
    int x = rgb.cols - label_size.width;

    cv::rectangle(rgb, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)),
                  cv::Scalar(255, 255, 255), -1);

    cv::putText(rgb, text, cv::Point(x, y + label_size.height),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));

    return 0;
}

static Yolox *g_yolox = 0;
static ncnn::Mutex lock;
static unsigned int n_count = 0;
static unsigned int n_rate;
static bool is_on = false;
static bool is_delegate = false;
static bool is_coco = false;
static std::vector<Object> objects;

class MyNdkCamera : public NdkCameraWindow
{
public:
    virtual void on_image_render(cv::Mat &rgb) const;
};

void MyNdkCamera::on_image_render(cv::Mat &rgb) const
{
    frame_counter++;

    if (is_on)
    {
        ncnn::MutexLockGuard g(lock);

        delegate_score = 1;

        if (g_yolox)
        {
            if (n_count % n_rate == 0)
            {
                n_count = 0;

                g_yolox->detect(rgb, objects);
            }

            g_yolox->draw(rgb, objects, is_delegate, delegate_score, is_coco);

            if (is_delegate)
                sendResultInference(NULL);

            n_count++;
        }
        else
        {
            draw_unsupported(rgb);
        }
    }

    draw_fps(rgb);
}

static MyNdkCamera *g_camera = 0;

static void setEnv(JNIEnv *env, jobject instance)
{
    jInstance = env->NewGlobalRef(instance);
    env->GetJavaVM(&jvm);
    pthread_create(&thread, NULL, sendResultInference, NULL);
}

extern "C"
{

    JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "JNI_OnLoad");

        g_camera = new MyNdkCamera;

        return JNI_VERSION_1_4;
    }

    JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved)
    {
        __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "JNI_OnUnload");

        {
            ncnn::MutexLockGuard g(lock);

            delete g_yolox;
            g_yolox = 0;
        }

        delete g_camera;
        g_camera = 0;
    }

    // public native boolean loadModel(AssetManager mgr, int modelid, int cpugpu);
    JNIEXPORT jboolean JNICALL Java_com_tencent_ncnnyolox_NcnnYolox_loadModel(JNIEnv *env, jobject thiz, jobject assetManager, jint modelid, jint cpugpu, jint samplingrate, jboolean ison, jboolean isdelegate, jboolean iscoco)
    {
        setEnv(env, thiz);

        objects.clear();

        if (modelid < 0 || modelid > 6 || cpugpu < 0 || cpugpu > 1 || samplingrate < 0 || samplingrate > 9)
        {
            return JNI_FALSE;
        }

        AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);

        __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "loadModel %p", mgr);

        const char *modeltypes[] =
            {
                "yolox-nano",
                "yolox-tiny",
                "yolox-nano-coco",
                "yolox-tiny-coco",
            };

        const int target_sizes[] =
            {
                416,
                416,
                416,
                416,
            };

        const float mean_vals[][3] =
            {
                {255.f * 0.485f, 255.f * 0.456, 255.f * 0.406f},
                {255.f * 0.485f, 255.f * 0.456, 255.f * 0.406f},
            };

        const float norm_vals[][3] =
            {
                {1 / (255.f * 0.229f), 1 / (255.f * 0.224f), 1 / (255.f * 0.225f)},
                {1 / (255.f * 0.229f), 1 / (255.f * 0.224f), 1 / (255.f * 0.225f)},
            };

        n_rate = samplingrate + 1;
        is_on = ison;
        is_coco = iscoco;
        is_delegate = !is_coco && isdelegate;

        int model_id = ((int)modelid) + (is_coco ? 2 : 0);

        const char *modeltype = modeltypes[model_id];
        int target_size = target_sizes[model_id];
        bool use_gpu = (int)cpugpu == 1;

        // reload
        {
            ncnn::MutexLockGuard g(lock);

            if (use_gpu && ncnn::get_gpu_count() == 0)
            {
                // no gpu
                delete g_yolox;
                g_yolox = 0;
            }
            else
            {
                if (!g_yolox)
                    g_yolox = new Yolox;
                g_yolox->load(mgr, modeltype, target_size, mean_vals[(int)modelid], norm_vals[(int)modelid], use_gpu);
            }
        }

        return JNI_TRUE;
    }

    // public native boolean openCamera(int facing);
    JNIEXPORT jboolean JNICALL Java_com_tencent_ncnnyolox_NcnnYolox_openCamera(JNIEnv *env, jobject thiz, jint facing)
    {
        setEnv(env, thiz);

        if (facing < 0 || facing > 1)
            return JNI_FALSE;

        __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "openCamera %d", facing);

        g_camera->open((int)facing);

        return JNI_TRUE;
    }

    // public native boolean closeCamera();
    JNIEXPORT jboolean JNICALL Java_com_tencent_ncnnyolox_NcnnYolox_closeCamera(JNIEnv *env, jobject thiz)
    {
        setEnv(env, thiz);

        __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "closeCamera");

        g_camera->close();

        return JNI_TRUE;
    }

    // public native boolean setOutputWindow(Surface surface);
    JNIEXPORT jboolean JNICALL Java_com_tencent_ncnnyolox_NcnnYolox_setOutputWindow(JNIEnv *env, jobject thiz, jobject surface)
    {
        setEnv(env, thiz);

        ANativeWindow *win = ANativeWindow_fromSurface(env, surface);

        __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "setOutputWindow %p", win);

        g_camera->set_window(win);

        return JNI_TRUE;
    }
}
