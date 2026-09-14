# src/jobs/ — M2:ffmpeg 进程编排(UI 无关,编成 jobs_lib,可 headless 测试)

- `engine.{h,cpp}` — 引擎定位:优先主程序同目录的 ffmpeg/ffprobe(打包形态),
  退回系统 PATH;`verifyEncoders` 启动自检 libx264/libx265/aac。
- `ffmpegjob.{h,cpp}` — 单作业执行器(QProcess,信号驱动):每条命令在最后
  一个 token 前插 `-progress pipe:1 -nostats`(对齐网站 worker.py:238);
  解析 `out_time_us`/`out_time_ms`(实为微秒)/`out_time=`(worker.py:52-68);
  进度 `min(base + int(out/dur×(100-base)), 99)`,两遍 pass1 不发百分比、
  pass2 base=50(worker.py:250-258);取消 kill + 清理部分输出;stderr 末
  800 字符入错误(worker.py:262-263);POSIX nice 10 低优先级(可关)。
  `PasslogDir` 用 QTemporaryDir 放两遍 passlogfile,作业结束整目录清理。
- `jobmodel.{h,cpp}` — 极简队列:一次跑一个,FIFO;作业以 (level, overrides)
  描述,入队时 core::resolve,轮到时 core::buildCommands。M3 扩展并发/重试。
