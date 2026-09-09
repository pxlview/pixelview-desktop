"""Compile the actual canvas form/menu/Escape path in isolated offscreen Qt.

The projector QWidget is a geometry/lifecycle boundary, not a second renderer.
Native GPU rendering and physical display switching require app acceptance.
"""
import copy
import os
import pathlib
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from test_receive_preview_zoom import method

ROOT = pathlib.Path(__file__).resolve().parents[2]


class CanvasFullscreen(unittest.TestCase):
    def test_visible_canvas_button_and_native_escape_route(self):
        form = ET.parse(ROOT/'frontend/forms/OBSBasic.ui').getroot()
        canvas = copy.deepcopy(form.find(".//widget[@name='previewContainer']"))
        assert canvas is not None
        self.assertIsNotNone(canvas.find(".//widget[@name='previewFullscreenButton']"),
                             'Fullscreen must be a visible shared canvas control')
        # Generate the real canvas subtree, substituting only display/component
        # classes; their native layout, constraints, text and properties remain.
        for widget in canvas.iter('widget'):
            original_class = widget.get('class', 'QWidget')
            widget.set('class', {'OBSBasicPreview':'QWidget', 'OBSPreviewScalingLabel':'QLabel',
                                'OBSPreviewScalingComboBox':'QComboBox'}.get(original_class, original_class))
            for action in list(widget.findall('addaction')):
                widget.remove(action)
        # Production OBS translates these locale keys via its installed translator.
        translations = {}
        for line in (ROOT/'frontend/data/locale/en-US.ini').read_text().splitlines():
            if '=' in line:
                key,value=line.split('=',1); translations[key]=value.strip('"')
        for string in canvas.iter('string'):
            string.text=translations.get(string.text,string.text)
        ui=ET.Element('ui',version='4.0'); ET.SubElement(ui,'class').text='Canvas'; ui.append(canvas)
        main=(ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertIn('auto *canvasFullscreenMenu = new QMenu', main)
        wiring=main[main.index('\tauto *canvasFullscreenMenu = new QMenu'):main.index('\tui->previewFullscreenButton->setMenu(canvasFullscreenMenu);')+len('\tui->previewFullscreenButton->setMenu(canvasFullscreenMenu);')]
        header=(ROOT/'frontend/widgets/OBSBasic.hpp').read_text()
        monitors=method(header,'template<typename Receiver, typename... Args>\n\tstatic void AddProjectorMenuMonitors')
        projectors=(ROOT/'frontend/widgets/OBSBasic_Projectors.cpp').read_text()
        projector=(ROOT/'frontend/widgets/OBSProjector.cpp').read_text()
        methods='\n'.join(method(projectors,s) for s in (
            'QList<QString> OBSBasic::GetProjectorMenuMonitorsFormatted()',
            'void OBSBasic::OpenPreviewProjector()', 'void OBSBasic::DeleteProjector('))
        methods+='\n'+method(projector,'void OBSProjector::EscapeTriggered()')
        escape=projector[projector.index('\tQAction *action = new QAction(this);'):projector.index('\tsetAttribute(Qt::WA_DeleteOnClose, true);')]
        # The native preview projector has no sender-scene reference outside
        # studio mode and renders the main output texture (receive channel 0).
        self.assertIn('window->type == ProjectorType::Preview && !main->IsPreviewProgramMode()',projector)
        self.assertIn('obs_render_main_texture();',projector)
        receiving=(ROOT/'frontend/widgets/OBSBasic_PixelviewReceive.inc').read_text()
        self.assertIn('view->addMenu(QStringLiteral("Fullscreen canvas"))',receiving)
        code=r'''
#include <QApplication>
#include <QMenu>
#include <QScreen>
#include <QPointer>
#include <QKeyEvent>
#include <QTimer>
#include <cmath>
#include <cassert>
#include <iostream>
#include <vector>
#include "canvas.h"
#define QTStr(x) QStringLiteral(x)
class OBSProjector;
enum class ProjectorType { Preview };
class OBSBasic : public QWidget {
public:
 Ui::Canvas storage, *ui=&storage;
 static OBSBasic *instance;
 static OBSBasic *Get() { return instance; }
 std::vector<OBSProjector*> projectors;
 static QList<QString> GetProjectorMenuMonitorsFormatted();
 MONITORS
 void OpenPreviewProjector();
 void DeleteProjector(OBSProjector*);
 void OpenProjector(void*,int,ProjectorType);
 OBSBasic() { instance=this; ui->setupUi(this); WIRING }
};
OBSBasic *OBSBasic::instance=nullptr;
class OBSProjector : public QWidget {
public:
 OBSProjector() { ESCAPE }
 void EscapeTriggered();
};
void OBSBasic::OpenProjector(void *source,int monitor,ProjectorType type) {
 assert(source==nullptr && monitor==0 && type==ProjectorType::Preview);
 auto *p=new OBSProjector; projectors.push_back(p); p->showFullScreen(); p->activateWindow();
}
METHODS
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 OBSBasic w;
 auto *button=w.ui->previewFullscreenButton;
 assert(button->text()=="Fullscreen");
 assert(button->toolTip()==QString::fromUtf8("Fullscreen canvas · Esc to exit"));
 for (bool receiving : {false,true}) {
  // Sending's configuration guard can disable zoom without disabling fullscreen.
  w.ui->previewXContainer->setEnabled(receiving);
  for (auto size : {QSize(540,400),QSize(960,720)}) {
   w.resize(size); w.show(); app.processEvents();
   assert(button->isVisible() && button->isEnabled());
   assert(w.width()==size.width()); // no hidden minimum-size expansion
   auto canvas=w.ui->preview->geometry();
   auto b=QRect(button->mapTo(&w,QPoint()),button->size());
   assert(b.top()>=canvas.bottom());
   assert(w.rect().contains(b));
   for(auto *other : {static_cast<QWidget*>(w.ui->previewZoomInButton),static_cast<QWidget*>(w.ui->previewZoomOutButton),static_cast<QWidget*>(w.ui->previewScalingMode),static_cast<QWidget*>(w.ui->previewXScrollBar)}) {
    auto r=QRect(other->mapTo(&w,QPoint()),other->size());
    assert(w.rect().contains(r)); assert(!b.intersects(r));
   }
  }
  auto *menu=button->menu(); assert(menu);
  QTimer::singleShot(0,[&] {
   assert(menu->isVisible());
   assert(menu->actions().size()==QGuiApplication::screens().size());
   menu->actions().front()->trigger(); menu->close();
  });
  button->showMenu(); // real aboutToShow and monitor action, not QPushButton::click
  app.processEvents();
  assert(w.projectors.size()==1);
  QPointer<OBSProjector> projector=w.projectors.front();
  assert(projector->isFullScreen());
  auto actions=projector->actions(); assert(actions.size()==1 && actions[0]->shortcut()==QKeySequence(Qt::Key_Escape));
  // Activate the exact production Escape action; physical keyboard/native
  // shortcut-filter delivery is intentionally outside this isolated harness.
  actions[0]->trigger();
  QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  assert(projector.isNull() && w.projectors.empty());
 }
 std::cout<<"PASS shared Fullscreen button 540/960px, no overlap, native monitor menu, Preview projector dispatch and Escape deferred deletion\n";
}
'''.replace('MONITORS',monitors).replace('WIRING',wiring).replace('ESCAPE',escape).replace('METHODS',methods)
        qt=next((ROOT/'.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        with tempfile.TemporaryDirectory(prefix='pixelview-canvas-fullscreen-') as td:
            td=pathlib.Path(td); ET.ElementTree(ui).write(td/'canvas.ui',encoding='unicode')
            subprocess.run([str(qt.parent/'libexec/uic'),str(td/'canvas.ui'),'-o',str(td/'canvas.h')],check=True)
            (td/'native.cpp').write_text(code)
            cmd=['clang++','-std=c++17',str(td/'native.cpp'),'-o',str(td/'native'),'-F'+str(qt),'-Wl,-rpath,'+str(qt)]
            for module in ('Core','Gui','Widgets'):
                cmd += ['-I'+str(qt/f'Qt{module}.framework/Headers'),'-framework','Qt'+module]
            result=subprocess.run(cmd,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(td/'native')],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},text=True,capture_output=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout)


if __name__=='__main__': unittest.main()
