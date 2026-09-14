# AGENTS.md — 1f6s-desktop 操作守则

1f6s 在线压缩服务的桌面客户端(本地 ffmpeg 转码,参数与网站端同一套规范)。

## 技术栈与基线

- C++17 + Qt 6.2(Widgets/Network/Test,均在 qtbase)+ CMake(≥3.16)+ Ninja
- 发布基线:Linux glibc 2.31 / macOS 10.14;ffmpeg 以静态引擎随包分发(见 packaging/)
- 唯一参数真相源在网站仓 `1f6s-service-site/spec/levels.json`(八级规范)与
  `spec/tiers.json`(档位/预检);本仓 `assets/spec/` 是它的同步副本,**不手编**

## 构建

本机 Qt 装在 `/home/hanlie/tools/Qt/6.2.4/gcc_64/`,cmake/ninja 在 `~/tools/aqt/bin/`:

```bash
export PATH="$HOME/tools/aqt/bin:$PATH"
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/home/hanlie/tools/Qt/6.2.4/gcc_64
cmake --build build
ctest --test-dir build          # M1 起有测试
```

## 黄金向量对拍(核心验收手段)

`vectors/vectors.json` 是 C++ 参数解析/命令构造的 oracle,由网站仓真实代码生成。
再生成方法(用网站仓 venv,cwd 随意):

```bash
/home/hanlie/hhhermes/1f6s-service-site/.venv/bin/python tools/gen_vectors.py
# SITE_REPO=/path/to/1f6s-service-site 可覆盖网站仓位置
```

**网站仓改了 `spec/levels.json` 或 `app/services/compressor.py` 之后,必须重跑
gen_vectors.py,并让 C++ 实现对拍(vectors.json 全部 case)通过后才算完成。**
生成器自带 3 条抽样自查,输出为 0 才可信。已知行为勿"修正"向量:
L7 无 target_mb 一律报错;JSON 整数 `target_mb:0` 被静默丢弃(falsy),
字符串 `"0"` 才触发越界错误——oracle 记录的是网站真实行为。

## 有意偏离(唯一)

C++ 端 2-pass 的 pass1 输出用 `-f null -` 而非网站端的 `-f mp4 /dev/null`
(前者不落盘更稳;两遍统计等价)。对拍时 pass1 尾部三 token
(`-f`,`mp4`,`DEVNULL`)按等价规则归一后再比较,其余 token 必须逐一对齐。

## 规范同步

改参数先改网站仓 `spec/levels.json`(或 tiers.json),再同步到本仓:

```bash
tools/sync_spec.sh        # levels.json 原样拷贝;precheck 节与档位上限从 tiers.json 抽取
```

禁止在 C++ 代码里硬编码等级参数;改完同步后同样要重跑黄金向量。

## 许可

- Qt 以 LGPL 授权,**必须动态链接**,不得静态链入闭源发行物
- 随包分发的 ffmpeg 为 GPL 构建,以**子进程调用**,GPL 不传染主程序;
  但再分发包时必须附 ffmpeg 源码链接与许可文本(打包脚本负责,见 packaging/)
- 本仓代码许可证待定(与网站仓一致后补 LICENSE)

## 铁律

1. 提交信息中文,写明改了什么与为什么
2. 不手编 `assets/spec/*.json`(由 sync_spec.sh 生成)、不手编 `vectors/vectors.json`(由 gen_vectors.py 生成)
3. 不做 UA 嗅探/隐藏行为;与网站端行为差异只能来自 AGENTS.md 记录的有意偏离
4. i18n:`assets/i18n/en.json` 与 `zh.json` key 必须对齐
