"""Branding source integration contracts; native packaging/GUI requires a rebuild."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text()


class AppBranding(unittest.TestCase):
    def test_qt_display_name_does_not_change_configuration_identity(self):
        app = source('frontend/OBSApp.cpp')
        self.assertIn('setApplicationDisplayName(QStringLiteral("Pixelview Desktop"))', app)
        self.assertNotIn('setApplicationName(', app)
        main = source('frontend/widgets/OBSBasic.cpp')
        self.assertIn('setWindowTitle(QStringLiteral("Pixelview Desktop"))', main)
        self.assertIn('addMenu(QStringLiteral("Pixelview Desktop"))', main)
        self.assertIn('QStringLiteral("Quit Pixelview Desktop")', main)

    def test_macos_bundle_display_and_native_icon_keep_identity(self):
        cmake = source('cmake/macos/helpers.cmake').split('if(target STREQUAL obs-studio)', 1)[1].split('get_property(obs_dependencies', 1)[0]
        self.assertIn('OUTPUT_NAME Pixelview', cmake)
        self.assertIn('PRODUCT_NAME Pixelview', cmake)
        self.assertIn('PRODUCT_BUNDLE_IDENTIFIER com.pixelview.desktop', cmake)
        self.assertIn('INFOPLIST_KEY_CFBundleDisplayName "Pixelview Desktop"', cmake)
        self.assertIn('INFOPLIST_KEY_CFBundleName "Pixelview Desktop"', cmake)
        self.assertNotIn('ASSETCATALOG_COMPILER_APPICON_NAME', cmake)
        plist = source('frontend/cmake/macos/Info.plist.in')
        self.assertIn('<key>CFBundleIconFile</key>\n\t<string>pixelview-app.icns</string>', plist)
        resources = source('cmake/macos/helpers.cmake')
        self.assertIn('target_add_resource(${target} "${CMAKE_CURRENT_SOURCE_DIR}/data/images/pixelview-app.icns")', resources)
        self.assertNotIn('target_add_resource(${target} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/Assets.xcassets")', resources)

    @unittest.skipUnless(sys.platform == 'darwin', 'requires macOS/Xcode')
    def test_xcode_processed_plist_keeps_display_name_separate_from_product(self):
        """Exercise real Xcode plist processing, not just the correct input template.

        Build only a dependency-free fixture using the production branding block;
        never build, sign, or launch the real application.
        """
        import os
        import plistlib
        import shutil
        import subprocess
        import tempfile

        if not shutil.which('cmake') or not shutil.which('xcodebuild'):
            self.skipTest('requires CMake and Xcode')
        helper = source('cmake/macos/helpers.cmake')
        xcode_function = helper.split('function(set_target_xcode_properties target)', 1)[1].split('endfunction()', 1)[0]
        branding = helper.split('if(target STREQUAL obs-studio)', 1)[1].split('get_property(obs_dependencies', 1)[0]
        env = os.environ.copy()
        if 'DEVELOPER_DIR' not in env and pathlib.Path('/Applications/Xcode.app/Contents/Developer').exists():
            env['DEVELOPER_DIR'] = '/Applications/Xcode.app/Contents/Developer'
        with tempfile.TemporaryDirectory(prefix='pixelview-plist-test-') as directory:
            fixture = pathlib.Path(directory)
            template = fixture / 'cmake/macos/Info.plist.in'
            template.parent.mkdir(parents=True)
            template.write_text(source('frontend/cmake/macos/Info.plist.in'))
            (fixture / 'main.c').write_text('int main(void) { return 0; }\n')
            (fixture / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.28)\n'
                'project(BrandingProbe LANGUAGES C)\n'
                'set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)\n'
                'set(OBS_BUILD_NUMBER 99)\n'
                'set(PIXELVIEW_BUILD_NUMBER 1)\n'
                'set(PIXELVIEW_VERSION 0.0.1)\n'
                'set(PIXELVIEW_SOURCE_COMMIT test-source)\n'
                'set(PIXELVIEW_SOURCE_TAG v0.0.1)\n'
                'set(PIXELVIEW_OBS_BASE_VERSION 32.2.1)\n'
                'set(PIXELVIEW_OBS_BASE_DESCRIBE 32.2.1-66-g6b3e55072)\n'
                'set(PIXELVIEW_OBS_BASE_COMMIT 6b3e550729f125b6c5b3767df88c08f5aef9d264)\n'
                'set(SPARKLE_UPDATE_INTERVAL 0)\n'
                'set(OBS_VERSION_CANONICAL 32.2.1)\n'
                'string(TIMESTAMP CURRENT_YEAR "%Y")\n'
                'function(set_target_xcode_properties target)\n' + xcode_function + 'endfunction()\n'
                'add_executable(obs-studio main.c)\n'
                'set(target obs-studio)\n' + branding
            )
            build = fixture / 'build'
            compiler = subprocess.run(
                ['xcrun', '--find', 'clang'],
                env=env,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            env['SDKROOT'] = subprocess.run(
                ['xcrun', '--sdk', 'macosx', '--show-sdk-path'],
                env=env,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            commands = [
                ['cmake', '-S', str(fixture), '-B', str(build), '-G', 'Xcode',
                 f'-DCMAKE_C_COMPILER={compiler}'],
                ['xcodebuild', '-project', str(build / 'BrandingProbe.xcodeproj'),
                 '-target', 'obs-studio', '-configuration', 'Debug',
                 'CODE_SIGNING_ALLOWED=NO', 'build'],
            ]
            for command in commands:
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            app = build / 'Debug/Pixelview.app'
            plist = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
            self.assertEqual(plist['CFBundleName'], 'Pixelview Desktop')
            self.assertEqual(plist['CFBundleDisplayName'], 'Pixelview Desktop')
            self.assertEqual(plist['CFBundleExecutable'], 'Pixelview')
            self.assertTrue((app / 'Contents/MacOS/Pixelview').is_file())
            self.assertEqual(plist['CFBundleIdentifier'], 'com.pixelview.desktop')
            self.assertEqual(plist['CFBundleVersion'], '1')
            self.assertEqual(plist['CFBundleShortVersionString'], '0.0.1')
            self.assertEqual(plist['PixelviewSourceCommit'], 'test-source')
            self.assertEqual(plist['PixelviewSourceTag'], 'v0.0.1')
            self.assertEqual(plist['PixelviewOBSBaseVersion'], '32.2.1')
            self.assertEqual(plist['PixelviewOBSBaseDescribe'], '32.2.1-66-g6b3e55072')
            self.assertEqual(plist['PixelviewOBSBaseCommit'], '6b3e550729f125b6c5b3767df88c08f5aef9d264')
            self.assertEqual(plist['CFBundlePackageType'], 'APPL')
            self.assertEqual(plist['LSMinimumSystemVersion'], '13.0')
            self.assertEqual(plist['CFBundleIconFile'], 'pixelview-app.icns')
            for key in ('NSCameraUsageDescription', 'NSMicrophoneUsageDescription', 'NSAppleEventsUsageDescription'):
                self.assertTrue(plist[key].startswith('Pixelview needs '))
            self.assertIn('Lain Bailey', plist['NSHumanReadableCopyright'])
            for key in ('SUFeedURL', 'SUPublicEDKey', 'SUScheduledCheckInterval',
                        'NSCameraReactionEffectGesturesEnabledDefault', 'NSHighResolutionCapable', 'LSAppNapIsDisabled'):
                self.assertIn(key, plist)

    def test_qt_icons_and_all_tray_states_use_pixelview_resources(self):
        import xml.etree.ElementTree as ET
        qrc = ET.fromstring(source('frontend/forms/obs.qrc'))
        aliases = {f.get('alias', f.text): f.text for f in qrc.iter('file')}
        for name in ('app', 'tray', 'tray-active', 'tray-paused', 'tray-macos', 'tray-active-macos', 'tray-paused-macos'):
            filename = f'pixelview-{name}.png'
            self.assertEqual(aliases.get('images/' + filename), '../data/images/' + filename)
        for path in ('frontend/OBSApp.cpp', 'frontend/widgets/OBSBasicStats.cpp', 'frontend/widgets/OBSProjector.cpp'):
            text = source(path)
            self.assertIn('QIcon(":/res/images/pixelview-app.png")', text)
            self.assertNotIn('QIcon::fromTheme("obs"', text)
        for path in ('frontend/widgets/OBSBasic.hpp', 'frontend/widgets/OBSBasic_Recording.cpp', 'frontend/widgets/OBSBasic_SysTray.cpp'):
            text = source(path)
            self.assertNotIn('QIcon::fromTheme("obs-tray', text)
            self.assertNotIn(':/res/images/obs', text)
            self.assertNotIn(':/res/images/tray_active', text)
            self.assertIn('setIsMask(true)', text)
        for path in ('frontend/widgets/OBSBasic.hpp', 'frontend/widgets/OBSBasic_Recording.cpp'):
            text = source(path)
            for state in ('active', 'paused'):
                self.assertIn(f'pixelview-tray-{state}.png', text)
                self.assertIn(f'pixelview-tray-{state}-macos.png', text)
        tray = source('frontend/widgets/OBSBasic_SysTray.cpp')
        self.assertIn('setToolTip("Pixelview Desktop")', tray)
        self.assertIn('showMessage("Pixelview Desktop"', tray)

    def test_windows_and_linux_native_branding_matches_executable(self):
        self.assertIn('OUTPUT_NAME Pixelview)', source('frontend/CMakeLists.txt'))
        rc = source('frontend/cmake/windows/obs.rc.in')
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}/data/images/pixelview-app.ico"', rc)
        for key in ('FileDescription', 'ProductName'):
            self.assertIn(f'VALUE "{key}", "Pixelview Desktop"', rc)
        self.assertIn('VALUE "OriginalFilename", "Pixelview.exe"', rc)
        self.assertIn('${OBS_LEGAL_COPYRIGHT}', rc)
        self.assertIn('<description>Pixelview Desktop</description>', source('frontend/cmake/windows/obs.manifest'))
        self.assertIn('setDesktopFileName("com.pixelview.desktop")', source('frontend/OBSApp.cpp'))
        linux = source('frontend/cmake/os-linux.cmake')
        self.assertIn('cmake/linux/com.pixelview.desktop.desktop', linux)
        self.assertNotIn('com.obsproject.Studio', linux)
        self.assertIn('FILES data/images/pixelview-app.png', linux)
        self.assertIn('icons/hicolor/1024x1024/apps', linux)
        self.assertIn('RENAME com.pixelview.desktop.png', linux)
        desktop_path = ROOT / 'frontend/cmake/linux/com.pixelview.desktop.desktop'
        self.assertTrue(desktop_path.exists())
        desktop = desktop_path.read_text()
        for line in ('Name=Pixelview Desktop', 'Exec=Pixelview', 'Icon=com.pixelview.desktop', 'StartupWMClass=Pixelview'):
            self.assertIn(line, desktop.splitlines())
        metainfo_path = ROOT / 'frontend/cmake/linux/com.pixelview.desktop.metainfo.xml'
        self.assertTrue(metainfo_path.exists())
        meta = metainfo_path.read_text()
        self.assertIn('<id>com.pixelview.desktop</id>', meta)
        self.assertIn('<name>Pixelview Desktop</name>', meta)
        self.assertIn('<launchable type="desktop-id">com.pixelview.desktop.desktop</launchable>', meta)
        self.assertIn('GPL-2.0-or-later', meta)

    def test_generated_ui_cannot_override_app_icon_or_reenable_onboarding(self):
        import xml.etree.ElementTree as ET
        for name in ('OBSBasic', 'OBSPermissions'):
            ui = ET.fromstring(source(f'frontend/forms/{name}.ui'))
            icon = ui.find("./widget/property[@name='windowIcon']/iconset/normaloff")
            assert icon is not None
            self.assertEqual(icon.text, ':/res/images/pixelview-app.png')
        self.assertNotIn('check.exec();', source('frontend/obs-main.cpp'))

    def test_native_icon_is_not_relocated_by_recursive_data_install(self):
        helper = source('cmake/macos/helpers.cmake').split('function(target_install_resources target)', 1)[1].split('endfunction()', 1)[0]
        self.assertIn('data_file STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/data/images/pixelview-app.icns"', helper)
        self.assertIn('continue()', helper)
        self.assertLess(helper.index('continue()'), helper.index('PROPERTY MACOSX_PACKAGE_LOCATION'))

    def test_bsd_launcher_uses_same_branded_identity(self):
        bsd = source('frontend/cmake/os-freebsd.cmake')
        self.assertIn('cmake/linux/com.pixelview.desktop.desktop', bsd)
        self.assertIn('FILES data/images/pixelview-app.png', bsd)
        self.assertNotIn('com.obsproject.Studio', bsd)


if __name__ == '__main__':
    unittest.main()
