#pragma once
/*
* @brief:使用miniaudio对pcm进行重采样
*        支持片段直接转wav
*/
#include <vector>
#include <string>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>


class MiniResampling
{
public:
    MiniResampling();
	~MiniResampling();

    // 重采样配置
    struct ResampleConfig {
        int inputFormat = 16;
        int outputFormat = 16;
        int inputChannels =1;
        int outputChannels = 1;
        int inputSampleRate = 8000;
        int outputSampleRate = 16000;
    };

    // 重采样结果
    struct ResampleResult {
        std::vector<uint8_t> data;
        size_t      inputFramesProcessed;
        size_t      outputFramesGenerated;
        bool        success;
        std::string errorMessage;

        ResampleResult() : inputFramesProcessed(0), outputFramesGenerated(0), success(false) {}
    };

    // wav头部
    struct WavHeader {
        uint16_t numChannels = 1;        // 声道数 (单声道)
        uint32_t sampleRate = 16000;     // 采样率 (16000Hz)
        uint16_t bitsPerSample = 16;     // 位深度 (16bit)
        uint32_t byteRate = 32000;       // 字节率 = sampleRate * numChannels * bitsPerSample / 8
        uint16_t blockAlign = 2;         // 块对齐 = numChannels * bitsPerSample / 8
    };

    bool initialize(const ResampleConfig& config);
    ResampleResult resampleAudio(const char* inputData, size_t inputSize);
    ResampleResult resampleBatch(const char* inputData, size_t inputSize, size_t chunkSize);
    ResampleResult resampleToWav(const char* inputData, size_t inputSize);
private:
    void   cleanup();
    void   reset();
    std::vector<uint8_t> createWavHeader(uint32_t pcmDataSize, const WavHeader& config) const;
    size_t getExpectedOutputSize(size_t inputSize) const;
    size_t calculateInputFrames(size_t inputSize) const;
    size_t calculateOutputSize(size_t frames) const;
private:
    ResampleConfig          config_;
    ma_data_converter       converter_;
    bool                    initialized_;
};

