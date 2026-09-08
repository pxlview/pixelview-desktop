import importlib.util
from pathlib import Path
import unittest
SCRIPT = Path(__file__).parents[1]/'scripts/bundle-runtime.py'
class Packaging(unittest.TestCase):
 def test_curated_closure(self):
  self.assertTrue(SCRIPT.exists(), 'runtime packager missing')
  spec=importlib.util.spec_from_file_location('bundle_runtime', SCRIPT)
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  self.assertNotIn('libav',m.PLUGINS)
  self.assertNotIn('x264',m.PLUGINS)
  self.assertIn('applemedia',m.PLUGINS)
  self.assertIn('opus',m.PLUGINS)
  self.assertTrue(m.system_library('/usr/lib/libSystem.B.dylib'))
  self.assertFalse(m.system_library('/opt/homebrew/opt/glib/lib/libglib.dylib'))
 def test_packaged_minimum_os(self):
  import json
  repo=SCRIPT.parents[3]
  presets=json.loads((repo/'CMakePresets.json').read_text())
  mac=next(p for p in presets['configurePresets'] if p['name']=='macos')
  self.assertEqual(mac['cacheVariables']['CMAKE_OSX_DEPLOYMENT_TARGET']['value'], '14.0')
  self.assertIn('-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0', (repo/'cmake/macos/pixelview-build.sh').read_text())
 def test_rswebrtc_is_patched_not_brew(self):
  import json
  spec=importlib.util.spec_from_file_location('bundle_runtime', SCRIPT)
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  self.assertTrue(hasattr(m, 'patched_rswebrtc'), 'must require verified patched rswebrtc')
  lock=json.loads((SCRIPT.parents[1]/'runtime-lock.json').read_text())
  self.assertIn('rswebrtc_build', lock)
  self.assertFalse(any('libgstrswebrtc.dylib' in p for p in lock['inputs']))
class UpstreamAcquisition(unittest.TestCase):
 def test_package_hash_fails_closed(self):
  import hashlib, tempfile
  script=SCRIPT.parent/'fetch-gstreamer.py'
  self.assertTrue(script.exists(), 'public upstream package acquisition missing')
  spec=importlib.util.spec_from_file_location('fetch_gstreamer',script)
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  with tempfile.TemporaryDirectory() as tmp:
   archive=Path(tmp)/'runtime.pkg'; archive.write_bytes(b'package fixture')
   m.verify_archive(archive,hashlib.sha256(archive.read_bytes()).hexdigest())
   with self.assertRaisesRegex(RuntimeError,'hash mismatch'):
    m.verify_archive(archive,'0'*64)
 def test_sdk_merge_relocates_pc_and_rejects_escape(self):
  import tempfile
  spec=importlib.util.spec_from_file_location('fetch_gstreamer',SCRIPT.parent/'fetch-gstreamer.py')
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  self.assertTrue(hasattr(m,'merge_payload'), 'local SDK extraction missing')
  with tempfile.TemporaryDirectory() as tmp:
   payload=Path(tmp)/'Payload'; (payload/'lib/pkgconfig').mkdir(parents=True)
   (payload/'lib/pkgconfig/gst.pc').write_text('prefix=/Library/Frameworks/GStreamer.framework/Versions/1.0\nlibdir=${prefix}/lib\n')
   sdk=Path(tmp)/'sdk'; m.merge_payload(payload,sdk)
   self.assertIn('prefix='+str(sdk.resolve()),(sdk/'lib/pkgconfig/gst.pc').read_text())
   (payload/'escape').symlink_to('/etc/passwd')
   with self.assertRaisesRegex(RuntimeError,'symlink'):
    m.merge_payload(payload,sdk)
 def test_builder_isolates_sdk_from_host(self):
  spec=importlib.util.spec_from_file_location('build_rswebrtc',SCRIPT.parent/'build-rswebrtc.py')
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  self.assertTrue(hasattr(m,'sdk_environment'), 'builder SDK isolation missing')
  env=m.sdk_environment({'PKG_CONFIG_PATH':'/host/leak','PKG_CONFIG_LIBDIR':'/host/leak','DYLD_LIBRARY_PATH':'/host/leak'})
  self.assertEqual(env['PKG_CONFIG_PATH'],'')
  self.assertEqual(env['PKG_CONFIG_LIBDIR'],str(m.SDK/'lib/pkgconfig'))
  self.assertEqual(env['DYLD_LIBRARY_PATH'],str(m.SDK/'lib'))
  self.assertEqual(env.get('GST_PLUGIN_SYSTEM_PATH_1_0'),'')
  self.assertEqual(env.get('GST_PLUGIN_PATH_1_0'),'')
 def test_runtime_resolution_never_uses_host(self):
  import tempfile
  spec=importlib.util.spec_from_file_location('bundle_runtime',SCRIPT)
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  with tempfile.TemporaryDirectory() as tmp:
   owner=Path(tmp)/'owner'; owner.write_bytes(b'fixture')
   host=Path(tmp)/'host.dylib'; host.write_bytes(b'fixture')
   with self.assertRaisesRegex(RuntimeError,'Unapproved'):
    m.resolve(str(host),owner)
 def test_stage_uses_official_sdk_without_local_nice_build(self):
  spec=importlib.util.spec_from_file_location('bundle_runtime',SCRIPT)
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  self.assertEqual(m.GST, SCRIPT.parents[3]/'.deps/gstreamer-upstream-1.28.3/sdk')
  self.assertFalse(hasattr(m,'build_nice'), 'official package includes libgstnice')
 def test_product_build_acquires_sdk_before_patched_module(self):
  repo=SCRIPT.parents[3]
  build=(repo/'cmake/macos/pixelview-build.sh').read_text()
  self.assertIn('scripts/fetch-gstreamer.py',build)
  self.assertLess(build.index('scripts/fetch-gstreamer.py'),build.index('scripts/build-rswebrtc.py'))
  cmake=(SCRIPT.parents[1]/'CMakeLists.txt').read_text()
  self.assertNotIn('pkg_check_modules',cmake)
  self.assertIn('PIXELVIEW_GST_SDK',cmake)
 def test_minimum_os_rejects_newer_and_missing_metadata(self):
  spec=importlib.util.spec_from_file_location('bundle_runtime',SCRIPT)
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  self.assertTrue(hasattr(m,'validate_minimum_os'), 'per-file minOS gate missing')
  self.assertEqual(m.validate_minimum_os('cmd LC_BUILD_VERSION\n minos 11.0\n sdk 26.0'), '11.0')
  self.assertEqual(m.validate_minimum_os('cmd LC_VERSION_MIN_MACOSX\n version 10.13\n sdk 14.0'), '10.13')
  for value in ['cmd LC_BUILD_VERSION\n minos 14.1\n','cmd LC_BUILD_VERSION\n minos 26.0\n','']:
   with self.assertRaises(RuntimeError): m.validate_minimum_os(value)
 def test_sdk_publication_moves_old_tree_before_cleanup(self):
  import tempfile
  spec=importlib.util.spec_from_file_location('fetch_gstreamer',SCRIPT.parent/'fetch-gstreamer.py')
  m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
  self.assertTrue(hasattr(m,'publish_sdk'), 'SDK publication must not delete the live path')
  with tempfile.TemporaryDirectory() as tmp:
   final=Path(tmp)/'sdk'; final.mkdir(); (final/'old').write_text('old')
   stage=Path(tmp)/'temporary/new'; stage.mkdir(parents=True); (stage/'new').write_text('new')
   m.publish_sdk(stage,final)
   self.assertTrue((final/'new').is_file()); self.assertFalse((final/'old').exists())
   self.assertTrue((stage.parent/'previous-sdk/old').is_file())
if __name__=='__main__': unittest.main()
