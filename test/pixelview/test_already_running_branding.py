"""Exercise the real already-running dialog block offscreen, never launch OBS."""
import json
import os
import pathlib
import subprocess
import tempfile
import unittest
from test_first_launch_defaults import ROOT


class AlreadyRunningBranding(unittest.TestCase):
    def test_visible_product_name_cancel_default_and_multi_bypass(self):
        source=(ROOT/'frontend/obs-main.cpp').read_text()
        block=source[source.index('\t\tif (!multi) {'):source.index('\n\t\tif (cancel_launch) {')]
        locale=(ROOT/'frontend/data/locale/en-US.ini').read_text()
        translations={}
        for line in locale.splitlines():
            if line.startswith(('AlreadyRunning.', 'Cancel=')):
                key,value=line.split('=',1);translations[key]=value.strip('"')
        lookup='\n'.join('if (key == '+json.dumps(k)+') return QString::fromUtf8('+json.dumps(v)+');' for k,v in translations.items())
        code=r'''
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtCore/QTimer>
#include <cassert>
QString QTStr(const QString &key){LOOKUP return key;}
bool prompt(bool multi){bool cancel_launch=false;BLOCK return cancel_launch;}
int main(int argc,char **argv){
 QApplication app(argc,argv);
 for(bool cancel:{true,false}) {
  bool shown=false;
  QTimer::singleShot(0,[&]{
   auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());assert(box);shown=true;
   assert(box->text().contains("Pixelview Desktop is already running"));
   assert(!box->text().contains("OBS"));
   assert(box->buttons().size()==2);
   QPushButton *no=nullptr,*yes=nullptr;
   for(auto *b:box->buttons()) {
    if(box->buttonRole(b)==QMessageBox::NoRole)no=qobject_cast<QPushButton*>(b);
    if(box->buttonRole(b)==QMessageBox::YesRole)yes=qobject_cast<QPushButton*>(b);
   }
   assert(no && yes && box->defaultButton()==no);
   (cancel?no:yes)->click();
  });
  assert(prompt(false)==cancel);assert(shown);
 }
 assert(!prompt(true)); // No event loop/dialog: --multi bypass still applies.
}
'''.replace('LOOKUP',lookup).replace('BLOCK',block)
        with tempfile.TemporaryDirectory(prefix='pixelview-already-running-') as tmp:
            cpp=pathlib.Path(tmp)/'dialog.cpp';cpp.write_text(code)
            qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++','-std=c++17','-fPIC','-F'+str(qt),'-framework','QtCore','-framework','QtGui','-framework','QtWidgets','-Wl,-rpath,'+str(qt),str(cpp),'-o',tmp+'/dialog'],check=True)
            subprocess.run([tmp+'/dialog'],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},check=True,timeout=15)
        # Keep the native detection, early exit and crash-sentinel exception.
        self.assertIn('CheckIfAlreadyRunning(already_running)',source)
        self.assertIn('if (cancel_launch) {\n\t\t\treturn 0;',source)
        self.assertIn('unclean_shutdown = false;',source)
        self.assertIn('QStringLiteral("Pixelview Desktop")',block)


if __name__ == '__main__':
    unittest.main()
