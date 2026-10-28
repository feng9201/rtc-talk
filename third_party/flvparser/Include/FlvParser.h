#pragma once

#include <string>

#ifdef FLVPARSER_SHARED
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#   if defined(FLVPARSER_LIB)
#       define FLVPARSER_EXPORT __declspec(dllexport)
#   else
#       define FLVPARSER_EXPORT __declspec(dllimport)
#   endif
# else
#   define FLVPARSER_EXPORT
# endif
#else
#  define FLVPARSER_EXPORT
#endif

#include "packet/Packet.h"
#include <memory>
class FLVParserPrivate;

class FLVPARSER_EXPORT FLVParser {
public:
    FLVParser();
    ~FLVParser();

    bool parse(const uint8_t* data, size_t size);
    void setHasTrack(bool has_track);
    std::unique_ptr<FlvParser::Packet> getPacket();

    bool hasAudio() const;
    bool hasVideo() const;

    std::shared_ptr<FlvParser::Packet> getAudioSeqHeader();
    std::shared_ptr<FlvParser::Packet> getVideoSeqHeader();

    void quit();
private:
    size_t ParseHeader();
    size_t ParseTag();
private:
    std::unique_ptr<FLVParserPrivate> _d;
    bool _has_track = false;
};
