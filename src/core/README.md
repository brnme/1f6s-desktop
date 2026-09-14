# src/core/

M1 已填充:规范解析(levels.json 参数表、微调白名单、ffmpeg 命令构造)、ffprobe
探测、超时预检、分割规划。纯逻辑、不依赖 GUI,编成静态库 `core_lib` 供
tests(黄金向量对拍)与 app 共用。

- `spec.{h,cpp}` — params.py 语义:resolve(level, overrides),错误文案与
  Python 逐字一致(SpecError)
- `encode.{h,cpp}` — compressor.py 语义:buildCommands(唯一有意偏离:pass1
  尾部 `-f null -`,见根 AGENTS.md)、estimateMb(round1 为 Python half-even)、
  outputPath(永不覆盖)
- `probe.{h,cpp}` — ffprobe JSON 探测(QProcess 同步,120s 超时)
- `precheck.{h,cpp}` — 超时预检因子表与回退链
- `splitter.{h,cpp}` — 大文件分割规划(纯函数,不跑 ffmpeg)

验收:`ctest` 全绿,其中 test_vectors 覆盖 vectors/vectors.json 全部 261 case。
