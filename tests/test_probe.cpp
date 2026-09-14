// test_probe.cpp — ffprobe 探测集成测试(系统 ffmpeg/ffprobe,缺失时 QSKIP)。
#include <QtTest/QtTest>

#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <stdexcept>
#include <string>

#include "core/probe.h"

using one6s::ProbeResult;
using one6s::probe;

class TestProbe : public QObject {
    Q_OBJECT

private slots:
    void synthWithAudio();
    void synthVideoOnly();
    void missingFileFails();

private:
    static QString ffmpegPath() { return QStandardPaths::findExecutable("ffmpeg"); }
    static QString ffprobePath() { return QStandardPaths::findExecutable("ffprobe"); }
    // 合成测试片段(时长 1s);输出路径经 out 返回。QtTest 宏只能用于 void 函数。
    void synth(const QTemporaryDir& dir, const QString& name, const QString& videoSrc,
               bool withAudio, QString& out, const QString& audioArgsExtra = {}) const;
};

void TestProbe::synth(const QTemporaryDir& dir, const QString& name,
                      const QString& videoSrc, bool withAudio, QString& out,
                      const QString& audioArgsExtra) const {
    const QString ff = ffmpegPath();
    out = dir.filePath(name);
    QStringList args{"-y", "-f", "lavfi", "-i", videoSrc};
    if (withAudio) args << QStringList{"-f", "lavfi", "-i", "sine=frequency=440:duration=1"};
    args << audioArgsExtra.split(' ', Qt::SkipEmptyParts)
         << QStringList{"-t", "1", out};
    QProcess gen;
    gen.start(ff, args);
    QVERIFY2(gen.waitForStarted(10000), "ffmpeg cannot start");
    QVERIFY2(gen.waitForFinished(60000),
             "ffmpeg timed out: " + gen.readAllStandardError().left(300));
    QVERIFY2(gen.exitCode() == 0,
             "ffmpeg failed: " + gen.readAllStandardError().left(300));
}

void TestProbe::synthWithAudio() {
    if (ffmpegPath().isEmpty() || ffprobePath().isEmpty())
        QSKIP("ffmpeg/ffprobe 不在 PATH,跳过探测集成测试");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString out;
    synth(dir, "out.mp4", "testsrc=duration=1:size=320x240:rate=10", true, out,
          "-c:v libx264 -c:a aac");
    ProbeResult r;
    try {
        r = probe(ffprobePath(), out);
    } catch (const std::exception& e) {
        QFAIL(e.what());
    }
    QVERIFY2(std::fabs(r.duration_s - 1.0) < 0.5,
             qPrintable(QStringLiteral("duration=%1").arg(r.duration_s)));
    QCOMPARE(r.width, 320);
    QCOMPARE(r.height, 240);
    QVERIFY2(r.has_audio, "expected audio stream");
    QCOMPARE(QString::fromStdString(r.codec), QStringLiteral("h264"));
}

void TestProbe::synthVideoOnly() {
    if (ffmpegPath().isEmpty() || ffprobePath().isEmpty())
        QSKIP("ffmpeg/ffprobe 不在 PATH,跳过探测集成测试");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString out;
    synth(dir, "silent.mp4", "testsrc=duration=1:size=640x360:rate=10", false, out,
          "-c:v libx264");
    ProbeResult r;
    try {
        r = probe(ffprobePath(), out);
    } catch (const std::exception& e) {
        QFAIL(e.what());
    }
    QVERIFY(!r.has_audio);
    QCOMPARE(r.width, 640);
    QCOMPARE(r.height, 360);
    QCOMPARE(QString::fromStdString(r.codec), QStringLiteral("h264"));
}

void TestProbe::missingFileFails() {
    if (ffmpegPath().isEmpty() || ffprobePath().isEmpty())
        QSKIP("ffmpeg/ffprobe 不在 PATH,跳过探测集成测试");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ghost = dir.filePath("nope.mp4");
    bool thrown = false;
    std::string msg;
    try {
        static_cast<void>(probe(ffprobePath(), ghost));
    } catch (const std::exception& e) {
        thrown = true;
        msg = e.what();
    }
    QVERIFY2(thrown, "expected runtime_error for missing file");
    QVERIFY2(msg.rfind("ffprobe failed: ", 0) == 0,
             qPrintable(QString::fromStdString(msg)));
}

QTEST_MAIN(TestProbe)
#include "test_probe.moc"
