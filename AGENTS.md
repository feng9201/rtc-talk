# rtc-talk-win development rules

1. Understand the user's request before editing. If a requirement, source of
   truth, dependency boundary, UI baseline, or acceptance criterion is unclear,
   ask the user before implementing.
2. Treat the user's latest clarification as authoritative over previous context
   summaries and handoff notes.
3. Never use another workspace as a build or runtime dependency. User-wrapped
   third-party source libraries must be copied into this repository.
4. Qt, Visual Studio, CMake, and vcpkg are development-environment tools and may
   be referenced through environment variables. Product dependencies bundled
   by this project must not use absolute paths outside the repository.
5. The old `rtc-talk` workspace is read-only. Do not modify or commit to it.
6. The user-approved desktop UI baseline is the P5 UI produced by
   `talk-desktop.exe`. Do not replace it with a newly invented design.
7. Validate visual changes against the real baseline executable with the same
   window size and screenshot path before declaring completion.
8. Do not run long stability tests unless the user explicitly requests them.