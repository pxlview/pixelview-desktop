"""Native Qt clicks + compiled production receive routing/viewport scaling.

No network, credentials, running app, GPU or DeckLink device is opened. libobs
owns real private/send scenes; only video-info supplies fixed HD fixture geometry.
"""
import os
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class ReceivePreviewZoom(unittest.TestCase):
    def test_locked_receive_buttons_reach_native_viewport(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        preview = (ROOT / 'frontend/widgets/OBSBasicPreview.cpp').read_text()
        header = (ROOT / 'frontend/widgets/OBSBasicPreview.hpp').read_text()
        rendering = (ROOT / 'frontend/widgets/OBSBasic_Preview.cpp').read_text()
        receive = (ROOT / 'frontend/widgets/OBSBasic_PixelviewReceive.inc').read_text()
        desktop = (ROOT / 'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        methods = '\n'.join(method(preview, 'void OBSBasicPreview::' + name) for name in (
            'ResetScrollingOffset()', 'SetScalingLevel(', 'SetScalingAmount(',
            'SetScalingLevelAndAmount(', 'increaseScalingLevel()', 'decreaseScalingLevel()',
            'resetScalingLevel()', 'ClampScrollingOffsets()'))
        methods += '\n' + method(rendering, 'void OBSBasic::ResizePreview(')
        methods += '\n' + method(rendering, 'void OBSBasic::UpdatePreviewControls()')
        methods += '\n' + method(receive, 'void OBSBasic::SelectPixelviewMode(')
        methods += '\n' + method(desktop, 'bool OBSBasic::PixelviewConfigurationLocked() const')
        state = '\n'.join(line for line in header.splitlines() if re.match(
            r'\s*(bool (locked|fixedScaling)|int32_t scalingLevel|float scalingAmount|vec2 scrollingOffset)', line))
        api = header[header.index('\tinline void SetLocked'):header.index('\tvoid xScrollBarChanged')]
        gate = main[main.index('\tconst bool busy = PixelviewConfigurationLocked();', main.index('void OBSBasic::RefreshPixelviewDevices')):main.index('\tif (properties) properties->setEnabled(!busy);', main.index('void OBSBasic::RefreshPixelviewDevices'))]
        gate = '\n'.join(line for line in gate.splitlines() if 'pixelviewDevices->' not in line and 'pixelviewSettings->' not in line and 'pixelviewFit->' not in line)
        connections = '\n'.join(line for line in main.splitlines() if 'connect(ui->previewZoom' in line)
        code = r'''
#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QAction>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cassert>
#include <obs.hpp>
#include <util/config-file.h>
#include <utility/display-helpers.hpp>
#include <QDir>
namespace fixture {
// Real libobs user configuration on disk: mode selection persists the tab.
struct Application {
 config_t *config=nullptr;
 config_t *GetUserConfig() {
  if (!config) assert(config_open(&config, (QDir::tempPath()+"/pixelview-receive-zoom-fixture.ini").toUtf8().constData(), CONFIG_OPEN_ALWAYS)==CONFIG_SUCCESS);
  return config;
 }
} application;
Application *App() { return &application; }
using namespace std;
#define MAX_SCALING_LEVEL 32
#define MAX_SCALING_AMOUNT 8.0f
#define ZOOM_SENSITIVITY pow(MAX_SCALING_AMOUNT, 1.0f / MAX_SCALING_LEVEL)
#define PREVIEW_EDGE_SIZE 10
// No GPU/device; the geometry consumed by the production resize method.
#define obs_get_video_info fixture_video_info
bool fixture_video_info(obs_video_info *v) { *v = {}; v->base_width=1920; v->base_height=1080; return true; }
class OBSBasicPreview : public QWidget {
 Q_OBJECT
public:
 using QWidget::QWidget;
 STATE
 API
 void ClampScrollingOffsets();
 void UpdateXScrollBar(float) {}
 void UpdateYScrollBar(float) {}
signals:
 void scalingChanged(float);
 void fixedScalingChanged(bool);
 void DisplayResized();
};
class OBSBasic : public QWidget {
public:
 struct UI {
  OBSBasicPreview *preview;
  QWidget *previewXContainer;
  QPushButton *previewZoomInButton, *previewZoomOutButton;
  QScrollBar *previewXScrollBar, *previewYScrollBar;
  QAction *actionPreviewZoomIn, *actionPreviewZoomOut, *actionPreviewResetZoom;
 } storage, *ui=&storage;
 bool pixelviewReceiving=false, pixelviewPairingDurable=true, pixelviewSendPreviewLocked=false;
 struct { bool intent=false; } pixelviewLease;
 bool sendingBusy=false;
 QWidget *properties=nullptr;
 OBSSceneAutoRelease pixelviewReceiveScene;
 OBSSourceAutoRelease pixelviewSendOutput, pixelviewReceiveSource;
 void PixelviewBindDeckLinkReceive(obs_source_t *, bool) {}
 OBSSceneAutoRelease sender;
 float previewScale=1;
 int previewX=0, previewY=0;
 bool PixelviewSettingsBusy() const { return sendingBusy; }
 bool PixelviewModeBusy() { return false; }
 // Canvas-reset transaction is covered by test_receive_precision_canvas;
 // this no-GPU fixture isolates actual viewport controls and scene routing.
 bool SetPixelviewReceivePrecision(bool) { return true; }
 enum class ReceiveStatusTone { Error };
 void SetPixelviewReceiveStatus(const QString &, ReceiveStatusTone) { assert(false); }
 bool PixelviewConfigurationLocked() const;
 void ClearPixelviewAudio() {}
 void RefreshPixelviewModes() {}
 void DisconnectPixelviewReceiveMedia() {}
 OBSScene GetCurrentScene() { return OBSScene(sender); }
 void SelectPixelviewMode(int);
 void ResizePreview(uint32_t,uint32_t);
 void UpdatePreviewControls();
 void RefreshPixelviewDevices() { GATE }
 OBSBasic() {
  ui->preview=new OBSBasicPreview(this); ui->preview->resize(960,540); ui->preview->ResetScrollingOffset();
  ui->previewXContainer=new QWidget(this);
  ui->previewZoomInButton=new QPushButton(ui->previewXContainer);
  ui->previewZoomOutButton=new QPushButton(ui->previewXContainer);
  ui->previewXScrollBar=new QScrollBar(ui->previewXContainer);
  ui->previewYScrollBar=new QScrollBar(this);
  ui->actionPreviewZoomIn=new QAction(this); ui->actionPreviewZoomOut=new QAction(this); ui->actionPreviewResetZoom=new QAction(this);
  CONNECTIONS
  connect(ui->preview,&OBSBasicPreview::DisplayResized,this,[this] { ResizePreview(1920,1080); UpdatePreviewControls(); });
 }
};
METHODS
}
#include "native.moc"
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 assert(obs_startup("en-US",nullptr,nullptr));
 {
 fixture::OBSBasic w;
 w.sender=OBSSceneAutoRelease(obs_scene_create("Synthetic sender"));
 OBSSceneAutoRelease child=obs_scene_create_private("Synthetic source");
 auto *item=obs_scene_add(w.sender,obs_scene_get_source(child));
 vec2 pos; vec2_set(&pos,123,456); obs_sceneitem_set_pos(item,&pos);
 obs_set_output_source(0,obs_scene_get_source(w.sender));
 w.ui->preview->SetScalingLevelAndAmount(-4,0.7f);
 w.ui->preview->SetFixedScaling(true);
 w.ui->preview->SetScrollingOffset(15,25);
 w.ResizePreview(1920,1080);
 const auto originalScale=w.previewScale;
 w.SelectPixelviewMode(1); // actual private scene / lock / output switch
 assert(w.ui->preview->Locked());
 assert(!w.ui->preview->isEnabled()); // no sender context-menu/source-delete input in receive
 assert(w.GetCurrentScene()==w.sender);
 assert(w.previewScale==originalScale); // no mode-entry refit or reset
 auto *output=obs_get_output_source(0);
 assert(output==obs_scene_get_source(w.pixelviewReceiveScene));
 const auto transforms=OBSDataAutoRelease(obs_scene_save_transform_states(w.sender,false));
 const std::string before=obs_data_get_json(transforms);
 auto *preview=w.ui->preview;
 // Real QWidget inherited disablement must not swallow +/- while receiving.
 const auto level=preview->GetScalingLevel();
 w.ui->previewZoomInButton->click();
 if(preview->GetScalingLevel()!=level+1) { std::cerr<<"FAIL: receive + click was swallowed by disabled canvas controls\n"; return 1; }
 const auto enlarged=w.previewScale;
 assert(enlarged>originalScale);
 assert(w.previewScale==preview->GetScalingAmount());
 w.RefreshPixelviewDevices(); // polling must not re-disable the controls
 w.ui->previewZoomOutButton->click();
 assert(preview->GetScalingLevel()==level && w.previewScale<enlarged);
 assert(preview->IsFixedScaling() && preview->Locked());
 auto *stillOutput=obs_get_output_source(0); assert(stillOutput==output);
 obs_source_release(stillOutput); obs_source_release(output);
 const auto after=OBSDataAutoRelease(obs_scene_save_transform_states(w.sender,false));
 assert(before==obs_data_get_json(after));
 preview->SetFixedScaling(false); w.ResizePreview(1920,1080);
 auto fit=w.previewScale; w.ui->previewZoomInButton->click();
 assert(preview->IsFixedScaling() && w.previewScale>fit);
 auto zoom=w.previewScale;
 w.SelectPixelviewMode(0);
 assert(!preview->Locked() && w.previewScale==zoom); // keep shared viewport state, not source framing
 stillOutput=obs_get_output_source(0); assert(stillOutput==obs_scene_get_source(w.sender)); obs_source_release(stillOutput);
 w.sendingBusy=true; w.RefreshPixelviewDevices(); assert(!preview->isEnabled());
 obs_set_output_source(0,nullptr);
 std::cout<<"PASS locked receive +/- and fit-to-fixed zoom, periodic refresh, unchanged sender transforms and program routing, shared zoom retained on mode switch\n";
 }
 obs_shutdown();
}
'''.replace('STATE', state).replace('API', api).replace('GATE', gate).replace('CONNECTIONS', connections).replace('METHODS', methods)
        qt = next((ROOT / '.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        frameworks = ROOT / 'build_macos/libobs/RelWithDebInfo'
        deps = next((ROOT / '.deps').glob('obs-deps-20*/lib'))
        with tempfile.TemporaryDirectory(prefix='pixelview-receive-zoom-') as td:
            td = pathlib.Path(td)
            src = td / 'native.cpp'; src.write_text(code)
            subprocess.run([str(qt.parent/'libexec/moc'),str(src),'-o',str(td/'native.moc')],check=True)
            cmd = ['clang++','-std=c++17','-fPIC',str(src),'-o',str(td/'native'),'-I'+str(deps.parent/'include'),'-I'+str(ROOT/'frontend'),'-I'+str(ROOT/'libobs'),'-I'+str(ROOT/'build_macos/config'),'-I'+str(ROOT/'build_macos/libobs'),'-F'+str(qt),'-F'+str(frameworks),'-framework','libobs','-Wl,-rpath,'+str(qt),'-Wl,-rpath,'+str(frameworks),'-Wl,-rpath,'+str(deps)]
            for module in ('Core','Gui','Widgets'):
                cmd += ['-I'+str(qt/f'Qt{module}.framework/Headers'),'-framework','Qt'+module]
            compiled=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
            run=subprocess.run([str(td/'native')],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print('\n'.join(line for line in run.stdout.splitlines() if line.startswith('PASS ')))


if __name__ == '__main__':
    unittest.main()
