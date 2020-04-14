#ifndef MPGDECODER_H
#define MPGDECODER_H

#include <atomic>
#include <thread>
#include "mpg123.h"

class MpgDecoder {

public:
	MpgDecoder();
	~MpgDecoder();

	size_t decode(u_int8_t* audioData, int length);

	static const char* MPGDECODER_TAG;

private:
	int start_logger();
	void stop_logger();

private:
	mpg123_handle* m_mpg123Decoder{NULL};

    long m_sampleRateHz{0};
	int m_channels{0};
	int m_encoding{0};

	std::thread m_logthreadStdout;
	std::atomic<bool> m_stopLoggingStdout;

    std::thread m_logthreadStderr;
    std::atomic<bool> m_stopLoggingStderr{false};

	int m_pfdStdout[2];
    int m_pfdStderr[2];
};

#endif // MPGDECODER_H
