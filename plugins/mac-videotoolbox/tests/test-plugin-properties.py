"""Read actual built plugin properties via libobs (no GUI/graphics reset)."""
import ctypes as c
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
obs = c.CDLL(str(root / "libobs/RelWithDebInfo/libobs.framework/Versions/A/libobs"), mode=c.RTLD_GLOBAL)


def api(name, result, *args):
    function = getattr(obs, name)
    function.restype = result
    function.argtypes = list(args)
    return function


startup = api("obs_startup", c.c_bool, c.c_char_p, c.c_char_p, c.c_void_p)
open_module = api("obs_open_module", c.c_int, c.POINTER(c.c_void_p), c.c_char_p, c.c_char_p)
init_module = api("obs_init_module", c.c_bool, c.c_void_p)
post_load = api("obs_post_load_modules", None)
enumerate_encoder = api("obs_enum_encoder_types", c.c_bool, c.c_size_t, c.POINTER(c.c_char_p))
codec = api("obs_get_encoder_codec", c.c_char_p, c.c_char_p)
properties = api("obs_get_encoder_properties", c.c_void_p, c.c_char_p)
get_property = api("obs_properties_get", c.c_void_p, c.c_void_p, c.c_char_p)
count = api("obs_property_list_item_count", c.c_size_t, c.c_void_p)
string_item = api("obs_property_list_item_string", c.c_char_p, c.c_void_p, c.c_size_t)
int_item = api("obs_property_list_item_int", c.c_longlong, c.c_void_p, c.c_size_t)
destroy = api("obs_properties_destroy", None, c.c_void_p)
shutdown = api("obs_shutdown", None)

assert startup(b"en-US", None, None)
module = c.c_void_p()
bundle = root / "plugins/mac-videotoolbox/RelWithDebInfo/mac-videotoolbox.plugin/Contents"
assert open_module(c.byref(module), str(bundle / "MacOS/mac-videotoolbox").encode(), str(bundle / "Resources").encode()) == 0
assert init_module(module)
post_load()
identifier = c.c_char_p()
index = 0
found = False
while enumerate_encoder(index, c.byref(identifier)):
    index += 1
    if identifier.value != b"com.apple.videotoolbox.videoencoder.ave.hevc":
        continue
    found = True
    assert codec(identifier.value) == b"hevc"
    props = properties(identifier.value)
    for name, expected, reader in [
        (b"profile", [b"main", b"main10", b"main42210"], string_item),
        (b"rate_control", [b"CBR", b"ABR", b"CRF"], string_item),
        (b"spatial_aq_mode", [1, 2, 3], int_item),
    ]:
        prop = get_property(props, name)
        assert prop
        actual = [reader(prop, i) for i in range(count(prop))]
        assert actual == expected, (name, actual)
        print(identifier.value.decode(), name.decode(), actual)
    assert get_property(props, b"bframes")
    assert get_property(props, b"keyint_sec")
    destroy(props)
assert found, "Apple Silicon hardware HEVC encoder not registered"
shutdown()
print("PASS: built plugin hardware HEVC registration and actual settings properties")
