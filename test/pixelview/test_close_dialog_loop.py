"""Quitting while a blocking dialog of the main window is still inside exec()."""
import os
import pathlib
import subprocess
import tempfile
import unittest
from test_stream_lock import body
ROOT = pathlib.Path(__file__).resolve().parents[2]
BASIC = ROOT / 'frontend/widgets/OBSBasic.cpp'

HARNESS = r'''#include <QtWidgets/QtWidgets>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#define LOG_INFO 300
static int ends = 0, logs = 0;
static long endedWith = -1;
static void EndMacModalLoop(long response) { ++ends; endedWith = response; }
static void blog(int, const char *, ...) { ++logs; }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
struct OBSBasic : QWidget {
	bool pixelviewDialogWaitLogged = false;
	bool PixelviewDialogLoopsClosed();
};
bool OBSBasic::PixelviewDialogLoopsClosed()
METHOD
static QMessageBox *question(QWidget *parent)
{
	auto *box = new QMessageBox(QMessageBox::Question, "Confirm", "Start?", QMessageBox::Yes | QMessageBox::No, parent);
	box->setDefaultButton(QMessageBox::Yes);
	return box;
}
int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	OBSBasic window;
	window.show();
	// A parked (constructed, never exec'd) dialog must not hold the close forever.
	QDialog idle(&window);
	CHECK(window.PixelviewDialogLoopsClosed());

	// A visible box in exec() blocks the close and is dismissed like Esc: a
	// Yes/No question answers No, never NoButton, which "== No" checks miss.
	bool visibleClosed = true;
	{
		std::unique_ptr<QMessageBox> box(question(&window));
		QTimer::singleShot(0, &window, [&] { visibleClosed = window.PixelviewDialogLoopsClosed(); });
		CHECK(box->exec() == QMessageBox::No);
	}
	CHECK(!visibleClosed);
	CHECK(ends == 0);
	CHECK(logs == 1);
	CHECK(window.PixelviewDialogLoopsClosed());

	// Application quit closes every window first; a native alert can stay in
	// its modal loop after that. Still blocking, and the native loop is ended
	// with the escape button that close already chose.
	bool hiddenClosed = true;
	{
		std::unique_ptr<QMessageBox> box(question(&window));
		QTimer::singleShot(0, &window, [&] {
			box->close();
			hiddenClosed = window.PixelviewDialogLoopsClosed();
		});
		CHECK(box->exec() == QMessageBox::No);
	}
	CHECK(!hiddenClosed);
	CHECK(ends == 1 && endedWith == QMessageBox::No);

	// Hidden without a chosen button (custom-button box): abort the loop.
	{
		QMessageBox box(QMessageBox::Information, "Failure", "Connect failed", QMessageBox::NoButton, &window);
		box.addButton("OK", QMessageBox::AcceptRole);
		QTimer::singleShot(0, &window, [&] {
			box.hide();
			hiddenClosed = window.PixelviewDialogLoopsClosed();
		});
		box.exec();
	}
	CHECK(!hiddenClosed);
	CHECK(ends == 2 && endedWith == 0);
	CHECK(window.PixelviewDialogLoopsClosed());
	std::puts("dialog loop gate ok");
	return 0;
}
'''


class CloseDialogLoop(unittest.TestCase):
    def test_close_waits_for_dialog_loops(self):
        text = BASIC.read_text()
        close_event = body(text, 'closeEvent')
        gate = close_event.index('if (!PixelviewDialogLoopsClosed())')
        # Before the confirm-exit prompt (another nested loop) and before acceptance.
        self.assertLess(gate, close_event.index('shouldPromptForClose()'))
        self.assertLess(gate, close_event.index('QWidget::closeEvent(event);'))
        waiting = close_event[gate:close_event.index('shouldPromptForClose()')]
        self.assertIn('event->ignore();', waiting)
        self.assertIn('QTimer::singleShot(100, this, &OBSBasic::close);', waiting)
        close = body(text, 'closeWindow')
        # Teardown never runs under a dialog; aboutToQuit (no nested loop left) is exempt.
        self.assertIn('!pixelviewForceClose && !PixelviewDialogLoopsClosed()', close)
        self.assertLess(close.index('PixelviewDialogLoopsClosed()'), close.index('ClearSceneData();'))
        self.assertLess(close.index('PixelviewDialogLoopsClosed()'), close.index('isClosing_ = true;'))

    def test_compiled_dialog_loop_gate(self):
        text = BASIC.read_text()
        start = text.index('bool OBSBasic::PixelviewDialogLoopsClosed()\n{')
        method = text[text.index('{', start):text.index('\n}\n', start) + 3]
        qt = next((ROOT / '.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'gate.cpp'
            src.write_text(HARNESS.replace('METHOD', method))
            binary = pathlib.Path(tmp) / 'gate'
            subprocess.run(['clang++', '-std=c++17', '-fPIC', str(src), '-F' + str(qt),
                            '-I' + str(qt / 'QtWidgets.framework/Headers'), '-I' + str(qt / 'QtCore.framework/Headers'),
                            '-I' + str(qt / 'QtGui.framework/Headers'), '-framework', 'QtWidgets', '-framework', 'QtGui',
                            '-framework', 'QtCore', '-Wl,-rpath,' + str(qt), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'},
                                 timeout=30, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('dialog loop gate ok', run.stdout)


if __name__ == '__main__':
    unittest.main()
