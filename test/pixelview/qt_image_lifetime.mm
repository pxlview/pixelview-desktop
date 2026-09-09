// Standalone regression: no OBS plugins, configuration, or credentials.
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <QtGui/QCursor>
#include <QtGui/QImage>
#include <QtGui/QColorSpace>
#include <CoreGraphics/CoreGraphics.h>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle("Pixelview cursor lifetime regression");
    window.show();
    app.processEvents();
    window.setCursor(Qt::WaitCursor);
    app.processEvents();
    window.unsetCursor();
    const QImage::Format formats[] = {QImage::Format_ARGB32_Premultiplied,
                                     QImage::Format_RGBA8888, QImage::Format_RGB32};
    for (bool tagged : {false, true}) {
        for (auto format : formats) {
            for (int n = 0; n < 1000; ++n) {
                QImage image(16, 16, format);
                image.fill(0xff336699);
                if (tagged)
                    image.setColorSpace(QColorSpace(QColorSpace::Primaries::SRgb,
                                                   QColorSpace::TransferFunction::Gamma,
                                                   1.8f + n * 0.0001f));
                CGImageRef native = image.toCGImage();
                if (!native || CGImageGetWidth(native) != 16 ||
                    CGColorSpaceGetModel(CGImageGetColorSpace(native)) != kCGColorSpaceModelRGB)
                    return 1;
                image = QImage(); // The native image must retain its image data and color space.
                CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(native));
                if (!data || CFDataGetLength(data) != 1024)
                    return 2;
                CFRelease(data);
                CGImageRelease(native);
            }
        }
    }
    std::puts("PASS: 6000 tagged/untagged QImage-to-CGImage lifetime conversions");
}
