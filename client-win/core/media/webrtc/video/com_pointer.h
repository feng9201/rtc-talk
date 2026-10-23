// File: core/media/webrtc/video/com_pointer.h
// Brief: 轻量 COM 智能指针，供 MF 摄像头采集复用（与 camera_cap 同源）。
//
// 改动点：输出参数走 Put()，先释放旧指针再交给 COM，避免重复调用泄漏。
#ifndef TALK_MEDIA_WEBRTC_VIDEO_COM_POINTER_H_
#define TALK_MEDIA_WEBRTC_VIDEO_COM_POINTER_H_

#include <type_traits>

template <typename CT,
          typename = std::enable_if_t<std::is_base_of_v<IUnknown, CT>>>
class ComPointer {
 public:
  ComPointer() = default;

  ComPointer(CT* pointer) { SetPointerAndAddRef(pointer); }

  ComPointer(const ComPointer<CT>& other) {
    SetPointerAndAddRef(other.m_pointer);
  }
  ComPointer(ComPointer<CT>&& other) noexcept {
    m_pointer = other.m_pointer;
    other.m_pointer = nullptr;
  }

  ~ComPointer() { ClearPointer(); }

  ComPointer<CT>& operator=(const ComPointer<CT>& other) {
    if (this != &other) {
      ClearPointer();
      SetPointerAndAddRef(other.m_pointer);
    }
    return *this;
  }
  ComPointer<CT>& operator=(ComPointer<CT>&& other) noexcept {
    if (this != &other) {
      ClearPointer();
      m_pointer = other.m_pointer;
      other.m_pointer = nullptr;
    }
    return *this;
  }
  ComPointer<CT>& operator=(CT* other) {
    ClearPointer();
    SetPointerAndAddRef(other);
    return *this;
  }

  ULONG Release() { return ClearPointer(); }

  // 接管裸指针所有权（不加引用），用于接收已经 AddRef 过的返回值。
  void Attach(CT* pointer) {
    ClearPointer();
    m_pointer = pointer;
  }

  // 放弃所有权但不 Release，返回值由调用方负责。
  CT* Detach() {
    CT* pointer = m_pointer;
    m_pointer = nullptr;
    return pointer;
  }

  CT* GetRef() const {
    if (m_pointer) {
      m_pointer->AddRef();
      return m_pointer;
    }
    return nullptr;
  }
  CT* Get() const { return m_pointer; }

  // 用于接收 COM 输出参数（IID_PPV_ARGS / MFCreateXxx / ActivateObject）。
  // 必须先把旧指针 Release 掉：直接返回成员地址时 COM 会覆盖原值，
  // 原对象少一次 Release，重复调用就泄漏。
  CT** Put() {
    ClearPointer();
    return &m_pointer;
  }

  template <typename T>
  bool QueryInterface(ComPointer<T>& other, HRESULT* error_code = nullptr) {
    if (m_pointer) {
      HRESULT result = m_pointer->QueryInterface(IID_PPV_ARGS(&other));
      if (error_code) *error_code = result;
      return result == S_OK;
    }
    return false;
  }

  bool operator==(const ComPointer<CT>& other) const {
    return m_pointer == other.m_pointer;
  }
  bool operator==(const CT* other) const { return m_pointer == other; }

  CT* operator->() const { return m_pointer; }
  CT** operator&() { return Put(); }

  operator bool() const { return m_pointer != nullptr; }
  operator CT*() { return m_pointer; }

 private:
  ULONG ClearPointer() {
    ULONG new_ref = 0;
    if (m_pointer) {
      new_ref = m_pointer->Release();
      m_pointer = nullptr;
    }
    return new_ref;
  }

  void SetPointerAndAddRef(CT* pointer) {
    m_pointer = pointer;
    if (m_pointer) {
      m_pointer->AddRef();
    }
  }

  CT* m_pointer = nullptr;
};

#endif  // TALK_MEDIA_WEBRTC_VIDEO_COM_POINTER_H_
