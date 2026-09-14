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
- `jobmodel.{h,cpp}` — 任务队列:一次跑一个,FIFO;任务带类型(compress/split)。
  compress 作业以 (level, overrides) 描述,入队时 core::resolve,轮到时
  core::buildCommands;split 作业带 SplitParams(probe 数据 + 档位 + 预览确认的
  显式计划),轮到时创建 SplitRunner。queued 任务可移除(removeQueued),
  running 任务可取消(cancelTask);信号契约 taskAdded/taskRemoved/taskUpdated/
  queueAdvanced + M2 兼容的 job* 系列。M3 之后:并发槽、失败重试、持久化队列。
- `splitrunner.{h,cpp}` — 分割执行器(M3):planSplit 规划(或 UI 下发的显式
  计划)→ segmentCommand 流复制切分(FfmpegJob 同款编排,临时名 seg%05d.ext)
  → 逐段 ffprobe 后验 → overLimit 超限段递归再分割(T×超限比×0.9,最多 2 层)
  → 关键帧稀疏检测(段数少于计划或任一段时长 >1.5×计划段长 → 警告)→ 全部
  完成后按 partName 规则改名 <stem>.partNNofMM.<ext>。点数 = 每段
  max(ceil(GB), ceil(小时), 1)(tiers.json 公式)。
