#include <AMF/core/Factory.h>
#include <AMF/core/Trace.h>
#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderAV1.h>

#include <util/windows/ComPtr.hpp>

#include <dxgi.h>
#include <d3d11.h>
#include <d3d11_1.h>

#include <vector>
#include <string>
#include <map>

using namespace amf;

#ifdef _MSC_VER
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

#define AMD_VENDOR_ID 0x1002

struct adapter_caps {
	bool is_amd = false;
	bool supports_avc = false;
	bool supports_hevc = false;
	bool supports_av1 = false;
	bool supports_hevc_10bit = false;
};

static AMFFactory *amf_factory = nullptr;
static std::vector<uint64_t> luid_order;
static std::map<uint32_t, adapter_caps> adapter_info;

static bool has_encoder(AMFContextPtr &amf_context, const wchar_t *encoder_name)
{
	AMFComponentPtr encoder;
	AMF_RESULT res = amf_factory->CreateComponent(amf_context, encoder_name, &encoder);
	return res == AMF_OK;
}

/* Pixelview: report HEVC Main10 support so the encoder only offers profiles the
 * hardware can encode. Prefer the reported maximum profile; drivers that do not
 * report it are checked for P010 among the native input formats. */
static bool has_hevc_10bit(AMFContextPtr &amf_context)
{
	AMFComponentPtr encoder;
	if (amf_factory->CreateComponent(amf_context, AMFVideoEncoder_HEVC, &encoder) != AMF_OK)
		return false;

	AMFCapsPtr encoder_caps;
	if (encoder->GetCaps(&encoder_caps) != AMF_OK)
		return false;

	amf_int64 max_profile = 0;
	if (encoder_caps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_PROFILE, &max_profile) == AMF_OK)
		return max_profile >= AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN_10;

	AMFIOCapsPtr input_caps;
	if (encoder_caps->GetInputCaps(&input_caps) != AMF_OK)
		return false;

	for (amf_int32 i = 0; i < input_caps->GetNumOfFormats(); i++) {
		AMF_SURFACE_FORMAT format;
		amf_bool native = false;
		if (input_caps->GetFormatAt(i, &format, &native) == AMF_OK && format == AMF_SURFACE_P010 && native)
			return true;
	}

	return false;
}

static inline uint32_t get_adapter_idx(uint32_t adapter_idx, LUID luid)
{
	for (size_t i = 0; i < luid_order.size(); i++) {
		if (luid_order[i] == *(uint64_t *)&luid) {
			return (uint32_t)i;
		}
	}

	return adapter_idx;
}

static bool get_adapter_caps(IDXGIFactory *factory, uint32_t adapter_idx)
{
	AMF_RESULT res;
	HRESULT hr;

	ComPtr<IDXGIAdapter> adapter;
	hr = factory->EnumAdapters(adapter_idx, &adapter);
	if (FAILED(hr)) {
		return false;
	}

	DXGI_ADAPTER_DESC desc;
	adapter->GetDesc(&desc);

	uint32_t luid_idx = get_adapter_idx(adapter_idx, desc.AdapterLuid);
	adapter_caps &caps = adapter_info[luid_idx];

	if (desc.VendorId != AMD_VENDOR_ID) {
		return true;
	}

	caps.is_amd = true;

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
			       nullptr, &context);
	if (FAILED(hr)) {
		return true;
	}

	AMFContextPtr amf_context;
	res = amf_factory->CreateContext(&amf_context);
	if (res != AMF_OK) {
		return true;
	}

	res = amf_context->InitDX11(device);
	if (res != AMF_OK) {
		return true;
	}

	caps.supports_avc = has_encoder(amf_context, AMFVideoEncoderVCE_AVC);
	caps.supports_hevc = has_encoder(amf_context, AMFVideoEncoder_HEVC);
	caps.supports_av1 = has_encoder(amf_context, AMFVideoEncoder_AV1);
	caps.supports_hevc_10bit = caps.supports_hevc && has_hevc_10bit(amf_context);

	return true;
}

DWORD WINAPI TimeoutThread(LPVOID param)
{
	HANDLE hMainThread = (HANDLE)param;

	DWORD ret = WaitForSingleObject(hMainThread, 2500);
	if (ret == WAIT_TIMEOUT) {
		TerminateProcess(GetCurrentProcess(), STATUS_TIMEOUT);
	}

	CloseHandle(hMainThread);

	return 0;
}

int main(int argc, char *argv[])
try {
	ComPtr<IDXGIFactory> factory;
	AMF_RESULT res;
	HRESULT hr;

	HANDLE hMainThread;
	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &hMainThread, 0, FALSE,
			DUPLICATE_SAME_ACCESS);
	DWORD threadId;
	HANDLE hThread;
	hThread = CreateThread(NULL, 0, TimeoutThread, hMainThread, 0, &threadId);
	CloseHandle(hThread);

	/* --------------------------------------------------------- */
	/* try initializing amf, I guess                             */

	HMODULE amf_module = LoadLibraryW(AMF_DLL_NAME);
	if (!amf_module) {
		throw "Failed to load AMF lib";
	}

	auto init = (AMFInit_Fn)GetProcAddress(amf_module, AMF_INIT_FUNCTION_NAME);
	if (!init) {
		throw "Failed to get init func";
	}

	res = init(AMF_FULL_VERSION, &amf_factory);
	if (res != AMF_OK) {
		throw "AMFInit failed";
	}

	/* --------------------------------------------------------- */
	/* parse expected LUID order                                 */

	for (int i = 1; i < argc; i++) {
		luid_order.push_back(strtoull(argv[i], NULL, 16));
	}

	/* --------------------------------------------------------- */
	/* obtain adapter compatibility information                  */

	hr = CreateDXGIFactory1(__uuidof(IDXGIFactory), (void **)&factory);
	if (FAILED(hr)) {
		throw "CreateDXGIFactory1 failed";
	}

	uint32_t idx = 0;
	while (get_adapter_caps(factory, idx++))
		;

	for (auto &[idx, caps] : adapter_info) {
		printf("[%u]\n", idx);
		printf("is_amd=%s\n", caps.is_amd ? "true" : "false");
		printf("supports_avc=%s\n", caps.supports_avc ? "true" : "false");
		printf("supports_hevc=%s\n", caps.supports_hevc ? "true" : "false");
		printf("supports_av1=%s\n", caps.supports_av1 ? "true" : "false");
		printf("supports_hevc_10bit=%s\n", caps.supports_hevc_10bit ? "true" : "false");
	}

	return 0;
} catch (const char *text) {
	printf("[error]\nstring=%s\n", text);
	return 0;
}
