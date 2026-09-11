#include "decklink-devices.hpp"

DeckLinkDeviceDiscovery *deviceEnum = nullptr;

void fill_out_devices(obs_property_t *list, bool inputOnly)
{
	deviceEnum->Lock();

	const std::vector<DeckLinkDevice *> &devices = deviceEnum->GetDevices();
	for (DeckLinkDevice *device : devices) {
		// Input properties must not offer output-only cards such as Mini Monitor.
		if (inputOnly && device->GetInputModes().empty())
			continue;
		obs_property_list_add_string(list, device->GetDisplayName().c_str(), device->GetHash().c_str());
	}

	deviceEnum->Unlock();
}
