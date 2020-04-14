#include <jni.h>
#include <android/log.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "mpgdecoder.h"
#include "common.h"

//Globals
static JavaVM * g_vm = NULL;
static jobject g_obj = NULL;
static jclass g_decoderClass = NULL;
static jmethodID g_decoderDataCallbackMId = NULL;
static jmethodID g_outputFormatChangedCallbackMId = NULL;

static MpgDecoder* g_mpgDecoder = NULL;

const size_t INPBUF_SIZE = 32*1024;
static unsigned char g_mpgInp[INPBUF_SIZE];

const size_t OUTBUF_SIZE = 128*1024;
static unsigned char g_mpgOut[OUTBUF_SIZE];

const char* MpgDecoder::MPGDECODER_TAG = "MpgDecoder";

/* the two end points of a pipe */
const int READ_FD = 0;
const int WRITE_FD = 1;

MpgDecoder::MpgDecoder() {
	__android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Constructing");

	start_logger();

	int errCode;
    errCode = mpg123_init();
    if (errCode != MPG123_OK) {
        __android_log_print(ANDROID_LOG_ERROR, MPGDECODER_TAG, "Error initialising decoder: %d, %s",
                            errCode, mpg123_plain_strerror(errCode));
    } else {
        m_mpg123Decoder = mpg123_new(NULL, &errCode);
        if (m_mpg123Decoder != NULL) {
            /* Brabble a bit about the parsing/decoding. */
            errCode = mpg123_param(m_mpg123Decoder, MPG123_VERBOSE, 2, 0.);
            if (errCode == MPG123_OK) {
                __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Setting verbose");
            } else {
                __android_log_print(ANDROID_LOG_WARN, MPGDECODER_TAG, "Error Setting verbose: %d, %s",
                                    errCode, mpg123_plain_strerror(errCode));
            }
            //errCode = mpg123_param(m_mpg123Decoder, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO, 0);
            //__android_log_print(ANDROID_LOG_INFO, "MpgDecoder", "Setting force Stereo: %d", errCode);

            errCode = mpg123_open_feed(m_mpg123Decoder);
            if (errCode == MPG123_OK) {
                __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Opening decoder okay");

            } else {
                __android_log_print(ANDROID_LOG_ERROR, MPGDECODER_TAG,
                                    "Error opening decoder: %d, %s",
                                    errCode, mpg123_plain_strerror(errCode));
            }
        } else {
            __android_log_print(ANDROID_LOG_ERROR, MPGDECODER_TAG, "Error creating decoder: %d, %s",
                                errCode, mpg123_plain_strerror(errCode));
        }
    }
    __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Constructed");
}

MpgDecoder::~MpgDecoder() {
	__android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Destructing");

	if(m_mpg123Decoder != NULL) {
        mpg123_close(m_mpg123Decoder);
        mpg123_delete(m_mpg123Decoder);
        mpg123_exit();
        m_mpg123Decoder = NULL;
        m_sampleRateHz = 0L;
        m_channels = m_encoding = 0;
    }

	stop_logger();
    __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Destructed");
}

size_t MpgDecoder::decode(u_int8_t* audioData, size_t length) {
    size_t retVal = 0;
    size_t done;
    size_t outc = 0;
    if (m_mpg123Decoder != NULL && audioData != NULL && length > 0) {
        /* Feed input chunk and get first chunk of decoded audio. */
        int ret;
        /* When you give zero-sized output buffer the input will be parsed until decoded data is available.
         * This enables you to get MPG123_NEW_FORMAT (and query it) without taking decoded data. */
        ret = mpg123_decode(m_mpg123Decoder, audioData, length, NULL, 0, &done);

        if (ret == MPG123_NEW_FORMAT) {
            long rate;
            int channels, enc;
            if (MPG123_OK == mpg123_getformat(m_mpg123Decoder, &rate, &channels, &enc)) {
                __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG,
                                    "MPG123_NEW_FORMAT: %li Hz, %i channels, encoding value %i",
                                    rate, channels, enc);
                m_sampleRateHz = rate;
                m_channels = channels;
                m_encoding = enc;
                /* Callback with format changed */
                JNIEnv* env;
                g_vm->GetEnv ((void **) &env, JNI_VERSION_1_6);
                env->CallVoidMethod(g_obj, g_outputFormatChangedCallbackMId, rate, channels);
            }
        }
        outc += done;


        while (ret != MPG123_ERR && ret != MPG123_NEED_MORE)  {
            /* Get all decoded audio that is available now before feeding more input. */
            ret = mpg123_decode(m_mpg123Decoder, NULL, 0, g_mpgOut, OUTBUF_SIZE, &done);
            outc += done;
        }

        if (ret == MPG123_ERR) {
            __android_log_print(ANDROID_LOG_WARN, MPGDECODER_TAG, "MPG123_ERR: %s", mpg123_strerror(m_mpg123Decoder));
            retVal = 0;
        } else if (ret == MPG123_NEED_MORE) {
            if (outc > 0) {
                /* Callback with decoded data. */
                JNIEnv *env;
                g_vm->GetEnv((void **) &env, JNI_VERSION_1_6);
                jbyteArray decData = env->NewByteArray(outc);
                env->SetByteArrayRegion(decData, 0, (int) outc, (jbyte *) g_mpgOut);
                env->CallVoidMethod(g_obj, g_decoderDataCallbackMId, decData, m_sampleRateHz, m_channels);
                env->DeleteLocalRef(decData);
            }
            retVal = length;
        }
    }
    return retVal;
}

void logThreadProc(const int fd, const char* tag, std::atomic<bool> * stopThread) {
    pid_t self = pthread_self();
    char name[13]; // 4 + 8 + '\0'
    snprintf(name, 12, "MPGLOG-%08x", (int) self);
    name[12] = '\0';
    pthread_setname_np(pthread_self(), name);

    __android_log_print(ANDROID_LOG_INFO, MpgDecoder::MPGDECODER_TAG, "mpg123 log thread tag=%s started: %s",
                        tag, name);
    // for testing the redirect
    //fprintf(stdout, "stdout: mpg123 log thread started: %s\n", name);
    //fprintf(stderr, "stderr: mpg123 log thread started: %s\n", name);

    char* alog_buf = new char[1024];

    ssize_t rdsz;
    while (!std::atomic_load(stopThread)) {
        rdsz = read(fd, alog_buf, 1023);
        if (rdsz > 0) {
            if(alog_buf[rdsz - 1] == '\n') --rdsz;
            alog_buf[rdsz] = 0;  /* add null-terminator */
            __android_log_write(ANDROID_LOG_INFO, tag, alog_buf);
        } else if (rdsz == 0) {
            __android_log_print(ANDROID_LOG_WARN, MpgDecoder::MPGDECODER_TAG, "%s mpg123 read EOF", name);
            break;
        } else {
            __android_log_print(ANDROID_LOG_WARN, MpgDecoder::MPGDECODER_TAG, "%s mpg123 read error",
                                name, errno, strerror(errno));
            break;
        }
    }
    close(fd);
    delete[] alog_buf;

    __android_log_print(ANDROID_LOG_INFO, MpgDecoder::MPGDECODER_TAG, "mpg123 log thread tag=%s stopped: %s",
                        tag, name);
}

int MpgDecoder::start_logger() {
    /* inspired by
     * https://codelab.wordpress.com/2014/11/03/how-to-use-standard-output-streams-for-logging-in-android-apps/
     */

    /* make stdout line-buffered and stderr unbuffered */
    setvbuf(stdout, 0, _IOLBF, 0);
    setvbuf(stderr, 0, _IONBF, 0);

    /* create the pipe and redirect stdout to the write end of the pipe */
    if (pipe(m_pfdStdout) == -1) {
        __android_log_print(ANDROID_LOG_ERROR, MPGDECODER_TAG, "creating stdout pipe failed, %d, %s",
                errno, strerror(errno));
        return -1;
    }
    if (dup2(m_pfdStdout[1], STDOUT_FILENO) == -1) {
        __android_log_print(ANDROID_LOG_ERROR, MPGDECODER_TAG, "dup stdout failed, %d, %s",
                errno, strerror(errno));
        return -1;
    }

    /* create the pipe and redirect stderr to the write end of the pipe */
    if (pipe(m_pfdStderr) == -1) {
        __android_log_print(ANDROID_LOG_ERROR, MPGDECODER_TAG, "creating stderr pipe failed, %d, %s",
                            errno, strerror(errno));
        return -1;
    }
    if (dup2(m_pfdStderr[1], STDERR_FILENO) == -1) {
        __android_log_print(ANDROID_LOG_ERROR, MPGDECODER_TAG, "dup stderr failed, %d, %s",
                            errno, strerror(errno));
        return -1;
    }

    /* spawn the stdout logging thread */
    m_logthreadStdout = std::thread(logThreadProc, m_pfdStdout[READ_FD], "mpg123", &m_stopLoggingStdout);
    /* spawn the stderr logging thread */
    m_logthreadStderr = std::thread(logThreadProc, m_pfdStderr[READ_FD], "mpg123err", &m_stopLoggingStderr);

    return 0;
}

void MpgDecoder::stop_logger() {
    char const * WAKEUP = "WAKEUP";
    m_stopLoggingStdout = true;
    if (m_logthreadStdout.joinable()) {
        __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Stopping stdout logger...");
        // wake up thread likely blocked in read() function
        write(m_pfdStdout[WRITE_FD], WAKEUP, sizeof(WAKEUP));
        m_logthreadStdout.join();
        __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Stopped stdout logger");
        close(m_pfdStdout[WRITE_FD]);
    }
    m_stopLoggingStderr = true;
    if (m_logthreadStderr.joinable()) {
        __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Stopping stderr logger...");
        write(m_pfdStderr[WRITE_FD], WAKEUP, sizeof(WAKEUP));
        m_logthreadStderr.join();
        __android_log_print(ANDROID_LOG_INFO, MPGDECODER_TAG, "Stopped stderr logger");
        close(m_pfdStderr[WRITE_FD]);
    }
}

extern "C" {

	JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
		JNIEnv* env;
		vm->GetEnv ((void **) &env, JNI_VERSION_1_6);

		g_decoderClass = (jclass)env->NewGlobalRef(env->FindClass("de/irt/dabmpg123decoderplugin/Mpg123Decoder"));
        if(g_decoderClass != NULL) {
			g_decoderDataCallbackMId = env->GetMethodID(g_decoderClass, "decodedDataCallback", "([BII)V");
			g_outputFormatChangedCallbackMId = env->GetMethodID(g_decoderClass, "outputFormatChangedCallback", "(II)V");
		} else {
	    	__android_log_print(ANDROID_LOG_INFO, MpgDecoder::MPGDECODER_TAG, "######### Decoder Class NOT found!!! ########");
		}

		return JNI_VERSION_1_6;
	}

	JNIEXPORT jint JNICALL Java_de_irt_dabmpg123decoderplugin_Mpg123Decoder_init(JNIEnv* env, jobject thiz) {
		env->GetJavaVM(&g_vm);
		g_obj = env->NewGlobalRef(thiz);
		g_mpgDecoder = new MpgDecoder();

		return 0;
	}

    JNIEXPORT jint JNICALL Java_de_irt_dabmpg123decoderplugin_Mpg123Decoder_deinit(JNIEnv* env, jobject thiz) {
        env->GetJavaVM(&g_vm);
        if (g_mpgDecoder != NULL) {
            delete g_mpgDecoder;
            g_mpgDecoder = NULL;
        }
        if (g_obj != NULL) {
            env->DeleteGlobalRef(g_obj);
            g_obj = NULL;
        }
        return 0;
    }

	JNIEXPORT jint JNICALL Java_de_irt_dabmpg123decoderplugin_Mpg123Decoder_decode(JNIEnv* env, jobject thiz, jbyteArray audioData, int dataLength) {
		if (dataLength > INPBUF_SIZE) {
            __android_log_print(ANDROID_LOG_ERROR, MpgDecoder::MPGDECODER_TAG, "audioData too big: %i, max %zu bytes", dataLength, INPBUF_SIZE);
            return 0;
		}
		if (g_mpgDecoder != NULL) {
		    if (audioData != NULL && dataLength > 0) {
                env->GetByteArrayRegion(audioData, 0, dataLength,
                                        reinterpret_cast<jbyte *>(g_mpgInp));
                return g_mpgDecoder->decode(g_mpgInp, (size_t) dataLength);
            } else {
                __android_log_print(ANDROID_LOG_ERROR, MpgDecoder::MPGDECODER_TAG, "invalid audio data");
                return 0;
		    }
        } else {
            __android_log_print(ANDROID_LOG_ERROR, MpgDecoder::MPGDECODER_TAG, "MpgDecoder null");
            return 0;
		}
	}
}
