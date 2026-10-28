// CpuFreq 实现
#include "tmcore/CpuFreq.h"
#include "pdh/PdhCpuFreq.h"

namespace tmcore
{

class CpuFreq::Impl
{
public:
    CPdhCpuFreq query;
};

CpuFreq::CpuFreq()
    : m_impl(new Impl())
{
}

CpuFreq::~CpuFreq()
{
    delete m_impl;
}

bool CpuFreq::GetFreq(float& freq)
{
    return m_impl->query.GetCpuFreq(freq);
}

} // namespace tmcore
