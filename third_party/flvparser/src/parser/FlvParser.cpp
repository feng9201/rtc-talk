//
// Created by 16037 on 2023-07-04.
//
#include <thread>
#include <memory>
#include "info/FlvParserInfo.h"
#include "FlvParser.h"
#include "header/FlvHeader.h"
#include "tag/FlvTag.h"
#include <spsc/readerwritercircularbuffer.h>
#include <iostream>
#include <atomic>
#include "utils/CircularBuffer.h"


using namespace FlvParser;

class FLVParserPrivate {
public:
    bool _hasHeader{ false };
    bool _hasAudioSeqHeader{ false };
    bool _hasVideoSeqHeader{ false };
    CircularBuffer _buffer;
    std::atomic<bool> _quit{ false };

    std::unique_ptr<FlvHeader> _header;
    moodycamel::BlockingReaderWriterCircularBuffer<Packet*> _packets{128};
    std::shared_ptr<Packet> _audioSeqHeader;
    std::shared_ptr<Packet> _videoSeqHeader;
    std::shared_ptr<Packet> _pApplicationTagTmp;

    ~FLVParserPrivate() {
        Packet* pkt = nullptr;
        while (_packets.try_dequeue(pkt)) {
            delete pkt;
        }
    }
};


FLVParser::FLVParser() {
    _d = std::make_unique<FLVParserPrivate>();
}
FLVParser::~FLVParser() {
    _d.reset();
}

bool FLVParser::parse(const uint8_t* data, size_t size) {
    _d->_buffer.append((const char*)data, size);

    if (!_d->_hasHeader) {
        size_t headerSize = ParseHeader();
        if (headerSize == 0) {
            return false;
        }
        _d->_hasHeader = true;
    }

    while (_d->_buffer.size() > 0
        && !_d->_quit.load()) {
        size_t tagSize = ParseTag();
        if (tagSize == 0) {
            break;
        }
    }

    if (_d->_header->hasAudio() && !_d->_hasAudioSeqHeader) {
        return false;
    }
    if (_d->_header->hasVideo() && !_d->_hasVideoSeqHeader) {
        return false;
    }

    return true;
}

std::unique_ptr<FlvParser::Packet> FLVParser::getPacket() {
    Packet* pkt = nullptr;
    if (_d->_packets.try_dequeue(pkt)) {
        return std::unique_ptr<Packet>(pkt);
    }
    return nullptr;
}

bool FLVParser::hasAudio() const {
    return _d->_header->hasAudio();
}

bool FLVParser::hasVideo() const {
    return _d->_header->hasVideo();
}

std::shared_ptr<FlvParser::Packet> FLVParser::getAudioSeqHeader() {
    return _d->_audioSeqHeader;
}

std::shared_ptr<FlvParser::Packet> FLVParser::getVideoSeqHeader() {
    return _d->_videoSeqHeader;
}

size_t FLVParser::ParseHeader() {
    std::unique_ptr<FlvHeader> header = std::make_unique<FlvHeader>(_d->_buffer);
    if (header->isValid()) {
        _d->_header = std::move(header);
        return 9;
    }
    return 0;
}

void FLVParser::setHasTrack(bool has_track)
{
    _has_track = has_track;
}

size_t FLVParser::ParseTag() {
    std::unique_ptr<FlvTag> tag = std::make_unique<FlvTag>(_d->_buffer);
    if (tag.get() && tag->isVaild()) {
        std::unique_ptr<Packet> packet = tag->packet();
        if (packet) {
            //std::cout<<"packet index: "<<packet->index<<" pts: "<<packet->pts<<std::endl;
            if (packet->type == 0) {
                switch (packet->index)
                {
                case FLV_TAG_TYPE::FLV_TAG_TYPE_APPLICATION:
                {
                    _d->_pApplicationTagTmp = std::move(packet);
                    return 1;
                }
                break;
                case FLV_TAG_TYPE::FLV_TAG_TYPE_AUDIO:
                {
                    if (!_d->_hasAudioSeqHeader) {
                        _d->_audioSeqHeader = std::move(packet);
                        _d->_hasAudioSeqHeader = true;
                        return 1;
                    }
                }
                break;
                case FLV_TAG_TYPE::FLV_TAG_TYPE_VIDEO:
                {
                    if (!_d->_hasVideoSeqHeader) {
                        _d->_videoSeqHeader = std::move(packet);
                        _d->_hasVideoSeqHeader = true;
                        return 1;
                    }
                }
                break;
                default:
                    return 1;
                }
            }
            else if (packet->type == 1) {
                switch (packet->index) {
                case FLV_TAG_TYPE::FLV_TAG_TYPE_VIDEO:
                {
                    if (_has_track) {
                        if (_d->_pApplicationTagTmp) {
                            if (packet->pts == _d->_pApplicationTagTmp->pts) {
                                packet->track_id = _d->_pApplicationTagTmp->track_id;
                                packet->has_track_id = _d->_pApplicationTagTmp->has_track_id;
                            }
                            else {
                                std::cerr << "video tag pts != application tag pts" << std::endl;
                            }
                            _d->_pApplicationTagTmp.reset();
                        }
                    }
                }
                case FLV_TAG_TYPE::FLV_TAG_TYPE_AUDIO:
                    break;
                default:
                    return 1;
                }
            }
            while (!_d->_quit.load()) {
                if (_d->_packets.wait_enqueue_timed(packet.get(), std::chrono::milliseconds(5)))
                    break;
            }
            packet.release();
        }
    }
    return 0;
}

void FLVParser::quit() {
    std::cerr << "flvparser ========> quit" << std::endl;
    _d->_quit.store(true);
}
