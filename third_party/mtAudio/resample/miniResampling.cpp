#include "MiniResampling.h"
#include <iostream>
#include <chrono>


MiniResampling::MiniResampling()
    : initialized_(false)
    
{
    memset(&converter_, 0, sizeof(converter_));
}

MiniResampling::~MiniResampling()
{
    cleanup();
}

ma_format convertToMaformat(int format) {
    ma_format ma_fm = ma_format::ma_format_unknown;
    switch (format){
    case 8:
        ma_fm = ma_format::ma_format_u8;
        break;
    case 16:
        ma_fm = ma_format::ma_format_s16;
        break;
    case 24:
        ma_fm = ma_format::ma_format_s24;
        break;
    case 32:
        ma_fm = ma_format::ma_format_s32;
        break;
    case 64:
        ma_fm = ma_format::ma_format_f32;
        break;
    default:
        break;
    }
    return ma_fm;
}

bool MiniResampling::initialize(const ResampleConfig& config)
{
    cleanup();

    config_ = config;

    ma_data_converter_config converterConfig = ma_data_converter_config_init(
        convertToMaformat(config.inputFormat), convertToMaformat(config.outputFormat),
        config.inputChannels,
        config.outputChannels,
        config.inputSampleRate,
        config.outputSampleRate
    );

    ma_result result = ma_data_converter_init(&converterConfig, nullptr, &converter_);
    if (result != MA_SUCCESS) {
        initialized_ = false;
        return false;
    }

    initialized_ = true;
    return true;
}

MiniResampling::ResampleResult MiniResampling::resampleAudio(const char* inputData, size_t inputSize)
{
    ResampleResult result;
    if (!initialized_) {
        result.errorMessage = "Resampler not initialized";
        return result;
    }
    if (!inputData || inputSize == 0) {
        result.errorMessage = "Invalid input data";
        return result;
    }
    try {
        // 计算输入帧数
        size_t inputFrames = calculateInputFrames(inputSize);
        if (inputFrames == 0) {
            result.errorMessage = "Invalid input size for given format";
            return result;
        }

        // 获取预期输出帧数
        ma_uint64 expectedOutputFrames = 0;
        ma_result maResult = ma_data_converter_get_expected_output_frame_count(
            &converter_, inputFrames, &expectedOutputFrames);

        if (maResult != MA_SUCCESS) {
            result.errorMessage = "Failed to get expected output frame count";
            return result;
        }

        // 分配输出缓冲区
        size_t outputSize = calculateOutputSize(expectedOutputFrames);
        result.data.resize(outputSize);

        // 准备转换参数
        ma_uint64 framesIn = inputFrames;
        ma_uint64 framesOut = expectedOutputFrames;
        const void* pInput = static_cast<const void*>(inputData);
        void* pOutput = static_cast<void*>(result.data.data());

        // 执行重采样
        maResult = ma_data_converter_process_pcm_frames(
            &converter_, pInput, &framesIn, pOutput, &framesOut);

        if (maResult != MA_SUCCESS) {
            result.errorMessage = "Failed to process PCM frames";
            return result;
        }

        // 更新实际处理结果
        result.inputFramesProcessed = framesIn;
        result.outputFramesGenerated = framesOut;

        // 调整输出缓冲区到实际大小
        size_t actualOutputSize = calculateOutputSize(framesOut);
        if (actualOutputSize < outputSize) {
            result.data.resize(actualOutputSize);
        }

        result.success = true;

    }
    catch (const std::exception& e) {
        result.errorMessage = std::string("Exception during resampling: ") + e.what();
    }

    return result;
}

MiniResampling::ResampleResult MiniResampling::resampleToWav(const char* inputData, size_t inputSize)
{
    auto result = resampleAudio(inputData, inputSize);
    if (!result.success) {
        return result;
    }

    try {
        WavHeader header;
        auto wavData = createWavHeader(result.data.size(), header);
        wavData.insert(wavData.end(),
            result.data.data(),
            result.data.data() + result.data.size());
        result.data.swap(wavData);
    }
    catch (const std::exception& e) {
        std::cerr << "[" << this << "] WAV data conversion error: " << e.what() << std::endl;
    }
    return result;
}

MiniResampling::ResampleResult MiniResampling::resampleBatch(const char* inputData, size_t inputSize, size_t chunkSize) 
{
    ResampleResult result;

    if (!initialized_) {
        result.errorMessage = "Resampler not initialized";
        return result;
    }

    if (!inputData || inputSize == 0) {
        result.errorMessage = "Invalid input data";
        return result;
    }

    try {
        size_t bytesPerInputFrame = ma_get_bytes_per_frame(
            convertToMaformat(config_.inputFormat), config_.inputChannels);

        if (bytesPerInputFrame == 0) {
            result.errorMessage = "Invalid audio format configuration";
            return result;
        }

        // 确保块大小是帧大小的整数倍
        chunkSize = (chunkSize / bytesPerInputFrame) * bytesPerInputFrame;
        if (chunkSize == 0) {
            chunkSize = bytesPerInputFrame;
        }

        const char* currentPos = inputData;
        size_t remainingSize = inputSize;

        while (remainingSize > 0) {
            size_t currentChunkSize = (remainingSize > chunkSize) ? chunkSize : remainingSize;

            // 处理当前块
            ResampleResult chunkResult = resampleAudio(currentPos, currentChunkSize);

            if (!chunkResult.success) {
                result.errorMessage = "Batch processing failed: " + chunkResult.errorMessage;
                return result;
            }

            // 合并结果
            size_t oldSize = result.data.size();
            result.data.resize(oldSize + chunkResult.data.size());
            memcpy(result.data.data() + oldSize, chunkResult.data.data(), chunkResult.data.size());

            result.inputFramesProcessed += chunkResult.inputFramesProcessed;
            result.outputFramesGenerated += chunkResult.outputFramesGenerated;

            currentPos += currentChunkSize;
            remainingSize -= currentChunkSize;
        }

        result.success = true;

    }
    catch (const std::exception& e) {
        result.errorMessage = std::string("Exception during batch resampling: ") + e.what();
    }

    return result;
}

size_t MiniResampling::getExpectedOutputSize(size_t inputSize) const 
{
    if (!initialized_) return 0;

    size_t inputFrames = calculateInputFrames(inputSize);
    if (inputFrames == 0) return 0;

    ma_uint64 outputFrames = 0;
    ma_result result = ma_data_converter_get_expected_output_frame_count(
        &converter_, inputFrames, &outputFrames);

    if (result != MA_SUCCESS) return 0;

    return calculateOutputSize(outputFrames);
}

void MiniResampling::reset() 
{
    if (initialized_) {
        ma_data_converter_reset(&converter_);
    }
}

void MiniResampling::cleanup() 
{
    if (initialized_) {
        ma_data_converter_uninit(&converter_, nullptr);
        initialized_ = false;
    }
}

size_t MiniResampling::calculateInputFrames(size_t inputSize) const 
{
    size_t bytesPerFrame = ma_get_bytes_per_frame(
        convertToMaformat(config_.inputFormat), config_.inputChannels);
    return (bytesPerFrame > 0) ? (inputSize / bytesPerFrame) : 0;
}

size_t MiniResampling::calculateOutputSize(size_t frames) const 
{
    return frames * ma_get_bytes_per_frame(
        convertToMaformat(config_.outputFormat), config_.outputChannels);
}

std::vector<uint8_t> MiniResampling::createWavHeader(uint32_t pcmDataSize, const WavHeader& config) const
{
    std::vector<uint8_t> header(44); // WAV 标准头大小为 44 字节

    // RIFF 块
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';

    uint32_t fileSize = pcmDataSize + 36; // 文件总大小 - 8
    header[4] = static_cast<uint8_t>(fileSize);
    header[5] = static_cast<uint8_t>(fileSize >> 8);
    header[6] = static_cast<uint8_t>(fileSize >> 16);
    header[7] = static_cast<uint8_t>(fileSize >> 24);

    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

    // fmt 块
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';

    uint32_t fmtSize = 16; // PCM 格式块大小固定为 16
    header[16] = static_cast<uint8_t>(fmtSize);
    header[17] = static_cast<uint8_t>(fmtSize >> 8);
    header[18] = static_cast<uint8_t>(fmtSize >> 16);
    header[19] = static_cast<uint8_t>(fmtSize >> 24);

    uint16_t audioFormat = 1; // PCM = 1
    header[20] = static_cast<uint8_t>(audioFormat);
    header[21] = static_cast<uint8_t>(audioFormat >> 8);

    header[22] = static_cast<uint8_t>(config.numChannels);
    header[23] = static_cast<uint8_t>(config.numChannels >> 8);

    header[24] = static_cast<uint8_t>(config.sampleRate);
    header[25] = static_cast<uint8_t>(config.sampleRate >> 8);
    header[26] = static_cast<uint8_t>(config.sampleRate >> 16);
    header[27] = static_cast<uint8_t>(config.sampleRate >> 24);

    header[28] = static_cast<uint8_t>(config.byteRate);
    header[29] = static_cast<uint8_t>(config.byteRate >> 8);
    header[30] = static_cast<uint8_t>(config.byteRate >> 16);
    header[31] = static_cast<uint8_t>(config.byteRate >> 24);

    header[32] = static_cast<uint8_t>(config.blockAlign);
    header[33] = static_cast<uint8_t>(config.blockAlign >> 8);

    header[34] = static_cast<uint8_t>(config.bitsPerSample);
    header[35] = static_cast<uint8_t>(config.bitsPerSample >> 8);

    // data 块
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';

    header[40] = static_cast<uint8_t>(pcmDataSize);
    header[41] = static_cast<uint8_t>(pcmDataSize >> 8);
    header[42] = static_cast<uint8_t>(pcmDataSize >> 16);
    header[43] = static_cast<uint8_t>(pcmDataSize >> 24);

    return header;
}