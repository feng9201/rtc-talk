# muxPlayer source baseline

- Provider: bundled `device_tool/muxPlayer` source snapshot
- Commit: `4c3da7bf8ec92c0826e143918403cba5d61ab1db`
- Repository copy: complete source tree (68 upstream files)
- Original build file: `CMakeLists.upstream.txt`
- Project integration build: `CMakeLists.txt`
- Current S1 target: `muxplayer_render`
- Current renderer: CPU I420 -> software OpenGL
- Full WGL/D3D11 zero-copy sources are retained in-tree for later activation
- User dependencies mtAudio, mtAudioPlayer, tmcore, and flvparser are bundled
  under `third_party`